#!/usr/bin/env python3
"""Install and select Water Click's Xcursor overlays on explicit request."""

import argparse
import configparser
import ctypes as c
import fcntl
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time


ASSETS = Path(__file__).resolve().parent
SHAPES = {
    "arrow": ("WaterClick-Arrow", 12, 7),
    "left-handed": ("WaterClick-LeftHanded", 52, 7),
    "crosshair": ("WaterClick-Crosshair", 32, 32),
}
SIZES = (24, 32, 40, 48, 56, 64)
POINTER_ALIASES = ("default", "left_ptr", "left-ptr", "arrow", "top_left_arrow")
OWNER = ".water-click-owned"
OWNER_CONTENT = "io.github.i4w7w4a.water-click\n"
STATE_DIR = Path(os.environ.get("XDG_STATE_HOME") or Path.home() / ".local/state") / "water-click"
STATE_PATH = STATE_DIR / "cursor.json"
CURSOR_QUERY = '''return table.concat({
  tostring(omarchy_startup_cursor_pending == true),
  os.getenv("XCURSOR_THEME") or "default",
  os.getenv("XCURSOR_SIZE") or "24",
  os.getenv("HYPRCURSOR_THEME") or "",
  os.getenv("HYPRCURSOR_SIZE") or "24",
  tostring(hl.get_config("cursor.enable_hyprcursor")),
  tostring(hl.get_config("cursor.sync_gsettings_theme")),
  os.getenv("XCURSOR_PATH") or ""
}, "\\n")'''


def command(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True, timeout=4).stdout.strip()


def session_cursor():
    deadline = time.monotonic() + 18
    while True:
        fields = command("hyprctl", "repl", CURSOR_QUERY).split("\n", 7)
        if len(fields) < 7 or fields[0] not in {"true", "false"}:
            raise RuntimeError("Could not read Hyprland cursor settings")
        pending, theme, size, hypr_theme, hypr_size, use_hypr, sync_gtk, *path = fields
        if pending == "false":
            return (theme, size, hypr_theme if use_hypr == "true" else "", hypr_size,
                    sync_gtk == "true", path[0] if path else "")
        if time.monotonic() >= deadline:
            raise RuntimeError("Omarchy startup cursor is still active")
        time.sleep(0.5)


def theme_name(value):
    return (isinstance(value, str) and re.fullmatch(r"[^/;,\x00-\x1f]{1,120}", value)
            and not value.startswith("-")
            and value == value.strip() and value not in {".", ".."}
            and all(value != theme[0] for theme in SHAPES.values()))


def cursor_size(value):
    try:
        size = int(value)
        if 1 <= size <= 256:
            return size
    except (TypeError, ValueError):
        pass
    return 24


def original_cursor(session):
    theme, size, hypr_theme, hypr_size, sync_gtk, _ = session
    # An explicitly configured Xcursor theme is authoritative. Omarchy's login
    # reveal can leave GTK's setting at a different value while sync is paused.
    if sync_gtk and (not theme_name(theme) or theme == "default") and shutil.which("gsettings"):
        try:
            gtk_theme = command("gsettings", "get", "org.gnome.desktop.interface", "cursor-theme")
            gtk_size = command("gsettings", "get", "org.gnome.desktop.interface", "cursor-size")
            if gtk_theme.startswith("'") and gtk_theme.endswith("'") and theme_name(gtk_theme[1:-1]):
                theme, size = gtk_theme[1:-1], gtk_size
        except subprocess.SubprocessError:
            pass
    return {
        "theme": theme if theme_name(theme) else "default",
        "size": cursor_size(size),
        "hypr_theme": hypr_theme if theme_name(hypr_theme) else "",
        "hypr_size": cursor_size(hypr_size),
    }


def read_selection():
    try:
        selection = json.loads(STATE_PATH.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return None
    base = selection.get("base") if isinstance(selection, dict) else None
    if (not isinstance(base, dict) or not isinstance(selection.get("shape"), str)
            or selection["shape"] not in SHAPES or selection.get("size") not in SIZES
            or not theme_name(base.get("theme")) or base.get("size") != cursor_size(base.get("size"))
            or (base.get("hypr_theme") != "" and not theme_name(base.get("hypr_theme")))
            or base.get("hypr_size") != cursor_size(base.get("hypr_size"))):
        raise RuntimeError("Invalid saved cursor selection")
    return selection


def cursor_paths(path):
    if not path:
        library = c.CDLL("libXcursor.so.1")
        library.XcursorLibraryPath.restype = c.c_char_p
        path = os.fsdecode(library.XcursorLibraryPath())
    paths = (Path(part).expanduser() for part in path.split(":") if part)
    return [path for path in paths if path.is_absolute()]


def user_icon_dirs():
    return (Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share") / "icons",
            Path.home() / ".local/share/icons", Path.home() / ".icons")


def user_icon_dir(paths):
    for directory in user_icon_dirs():
        if directory in paths:
            return directory
    raise RuntimeError("XCURSOR_PATH contains no supported user icon directory")


def fallback_cursors(theme, paths):
    pending = [theme, "default", "Adwaita"]
    visited = set()
    cursors = {}
    while pending:
        theme = pending.pop(0)
        if theme in visited or not theme_name(theme):
            continue
        visited.add(theme)
        inherited = []
        for path in paths:
            directory = path / theme
            if (directory / "cursors").is_dir():
                for entry in (directory / "cursors").iterdir():
                    if entry.is_file():
                        cursors.setdefault(entry.name, entry.resolve())
            index = configparser.ConfigParser(interpolation=None, strict=False)
            try:
                index.read(directory / "index.theme", encoding="utf-8")
                inherited.extend(index.get("Icon Theme", "Inherits", fallback="").split(","))
            except (configparser.Error, UnicodeError):
                pass
        pending[:0] = [name.strip() for name in inherited if name.strip()]
    return cursors


def bind(library, name, result, *arguments):
    function = getattr(library, name)
    function.restype = result
    function.argtypes = arguments
    return function


def make_xcursor(shape):
    class Rectangle(c.Structure):
        _fields_ = [(name, c.c_double) for name in ("x", "y", "width", "height")]

    pointer = c.c_void_p
    cairo = c.CDLL("libcairo.so.2")
    svg = c.CDLL("librsvg-2.so.2")
    load = bind(svg, "rsvg_handle_new_from_file", pointer, c.c_char_p, pointer)
    render = bind(svg, "rsvg_handle_render_document", c.c_int, pointer, pointer, c.POINTER(Rectangle), pointer)
    release = bind(svg, "g_object_unref", None, pointer)
    surface_new = bind(cairo, "cairo_image_surface_create", pointer, c.c_int, c.c_int, c.c_int)
    surface_destroy = bind(cairo, "cairo_surface_destroy", None, pointer)
    surface_flush = bind(cairo, "cairo_surface_flush", None, pointer)
    surface_data = bind(cairo, "cairo_image_surface_get_data", pointer, pointer)
    context_new = bind(cairo, "cairo_create", pointer, pointer)
    context_destroy = bind(cairo, "cairo_destroy", None, pointer)

    document = load(os.fsencode(ASSETS / (shape + ".svg")), None)
    if not document:
        raise RuntimeError("Could not load cursor artwork")
    _, hot_x, hot_y = SHAPES[shape]
    sizes = sorted(set(SIZES) | {2 * size for size in SIZES})
    image_type = 0xFFFD0002
    table = bytearray(struct.pack("<4sIII", b"Xcur", 16, 0x10000, len(sizes)))
    images = bytearray()
    try:
        for size in sizes:
            surface = surface_new(0, size, size)  # CAIRO_FORMAT_ARGB32
            context = context_new(surface)
            try:
                if not render(document, context, c.byref(Rectangle(0, 0, size, size)), None):
                    raise RuntimeError("Could not render cursor artwork")
                surface_flush(surface)
                pixels = c.string_at(surface_data(surface), size * size * 4)
                if sys.byteorder != "little":
                    pixels = struct.pack(f"<{size * size}I", *struct.unpack(f"={size * size}I", pixels))
                table.extend(struct.pack("<3I", image_type, size, 16 + 12 * len(sizes) + len(images)))
                images.extend(struct.pack("<9I", 36, image_type, size, 1, size, size,
                                          round(hot_x * size / 64), round(hot_y * size / 64), 0))
                images.extend(pixels)
            finally:
                context_destroy(context)
                surface_destroy(surface)
    finally:
        release(document)
    return table + images


def owns_theme(directory):
    return (not directory.is_symlink() and (directory / OWNER).is_file()
            and (directory / OWNER).read_text(encoding="utf-8") == OWNER_CONTENT)


def install_theme(selection, paths):
    shape = selection["shape"]
    theme = SHAPES[shape][0]
    parent = user_icon_dir(paths)
    parent.mkdir(parents=True, exist_ok=True)
    target = parent / theme
    if target.exists() or target.is_symlink():
        if not owns_theme(target):
            raise RuntimeError(f"Cursor theme already exists and is owned by another program: {target}")
        return theme

    temporary = Path(tempfile.mkdtemp(prefix=".water-click-", dir=parent))
    try:
        (temporary / OWNER).write_text(OWNER_CONTENT, encoding="utf-8")
        (temporary / "index.theme").write_text(f"[Icon Theme]\nName={theme}\n", encoding="utf-8")
        cursors = temporary / "cursors"
        cursors.mkdir()
        for name, source in fallback_cursors(selection["base"]["theme"], paths).items():
            if name not in POINTER_ALIASES:
                (cursors / name).symlink_to(source)
        (cursors / "default").write_bytes(make_xcursor(shape))
        for name in POINTER_ALIASES[1:]:
            (cursors / name).symlink_to("default")
        # mkdtemp starts private. Sandboxed clients also need to read installed cursor themes.
        temporary.chmod(0o755)
        temporary.rename(target)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return theme


def set_cursor(theme, size):
    reply = command("hyprctl", "setcursor", theme, str(size))
    if reply != "ok":
        raise RuntimeError(f"Hyprland could not set the cursor: {reply}")


def restore_cursor(base):
    set_cursor(base["theme"], base["size"])
    if base["hypr_theme"]:
        set_cursor(base["hypr_theme"], base["hypr_size"])


def refresh_cursor():
    try:
        reply = command("hyprctl", "waterclick", "cursor-refresh")
        if reply == "ok":
            return True
        if reply != "unknown request":
            print(f"Hyprland cursor refresh: {reply}", file=sys.stderr)
        return not json.loads(command("hyprctl", "-j", "version"))["version"].startswith("0.56.")
    except (KeyError, ValueError, OSError, subprocess.SubprocessError) as error:
        print(f"Hyprland cursor refresh: {error}", file=sys.stderr)
        return False


def commit_selection(selection, theme, previous, paths):
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=STATE_DIR, delete=False) as file:
        temporary = Path(file.name)
        try:
            json.dump(selection, file)
            file.write("\n")
            file.close()
            set_cursor(theme, selection["size"])
            try:
                temporary.replace(STATE_PATH)
            except OSError:
                if previous:
                    set_cursor(install_theme(previous, paths), previous["size"])
                else:
                    restore_cursor(selection["base"])
                refresh_cursor()
                raise
        finally:
            temporary.unlink(missing_ok=True)


def remove_themes():
    for path in dict.fromkeys(user_icon_dirs()):
        for name, _, _ in SHAPES.values():
            directory = path / name
            try:
                if owns_theme(directory):
                    shutil.rmtree(directory)
            except OSError as error:
                print(f"Could not remove Water Click theme {directory}: {error}", file=sys.stderr)


def run(args):
    if not os.environ.get("HYPRLAND_INSTANCE_SIGNATURE"):
        raise RuntimeError("No Hyprland session")
    if args.action in {"resume", "restore"} and not STATE_PATH.exists():
        return {"selection": None, "refreshed": True}
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    with (STATE_DIR / "cursor.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        previous = read_selection()
        if args.action != "apply" and previous is None:
            return {"selection": None, "refreshed": True}
        session = session_cursor()
        paths = cursor_paths(session[-1])
        if args.action == "restore":
            restore_cursor(previous["base"])
            try:
                STATE_PATH.unlink()
            except OSError:
                set_cursor(install_theme(previous, paths), previous["size"])
                refresh_cursor()
                raise
            refreshed = refresh_cursor()
            remove_themes()
            return {"selection": None, "refreshed": refreshed}
        if args.action == "resume":
            set_cursor(install_theme(previous, paths), previous["size"])
            return {"selection": previous, "refreshed": refresh_cursor()}
        selection = {"shape": args.shape, "size": args.size,
                     "base": previous["base"] if previous else original_cursor(session)}
        theme = install_theme(selection, paths)
        commit_selection(selection, theme, previous, paths)
        return {"selection": selection, "refreshed": refresh_cursor()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    apply = actions.add_parser("apply")
    apply.add_argument("shape", choices=SHAPES)
    apply.add_argument("size", type=int, choices=SIZES)
    actions.add_parser("resume")
    actions.add_parser("restore")
    try:
        selection = run(parser.parse_args())
    except subprocess.CalledProcessError as error:
        print(error.stderr.strip() or error.stdout.strip() or str(error), file=sys.stderr)
        return 1
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        return 1
    print(json.dumps(selection))
    return 0


if __name__ == "__main__":
    sys.exit(main())
