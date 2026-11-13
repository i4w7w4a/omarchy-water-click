# Water Click

[Русский](README.ru.md)

A left mouse press makes a short water ripple at the pointer in Hyprland. The
wave refracts what's under it, reaches up to 80 logical pixels, and fades
within 250 ms. A new left click replaces the previous wave. Hyprland delivers
the click to the desktop, window, or panel as usual.

![Three left clicks with the short Water effect](docs/water-click.gif)

The [terminal recording](docs/water-click-desktop.gif) shows the same short
Water profile from the original HyprRipple prototype in a live Omarchy session.

Water Click pauses while the session is locked or Hyprland animations are
off. It schedules frames only while a ripple is visible. The ripple is drawn
in Hyprland's compositor, so fullscreen direct scanout can bypass it. The Omarchy status
panel follows the shell language, with Russian and English translations.

## Install on Omarchy

Water Click needs a local build of its native Hyprland module. Use Hyprland
headers and libraries from the same build as your running compositor, plus
`make`, a C++23 compiler such as `g++`, `pkg-config`, and the development
files for pixman, libdrm, and OpenGL ES. The module supports Hyprland 0.56
and 0.57 and was tested on 0.56.2 and 0.57.2. Omarchy Shell 4.5 provides the
optional status panel.

```sh
omarchy plugin add https://github.com/i4w7w4a/omarchy-water-click --enable
cd "$HOME/.config/omarchy/plugins/io.github.i4w7w4a.water-click" &&
  make &&
  install -Dm755 build/waterclick.so "$HOME/.local/lib/waterclick/waterclick.so" &&
  hyprctl plugin load "$HOME/.local/lib/waterclick/waterclick.so" &&
  hyprctl waterclick get
```

Open the status panel with
`omarchy-shell shell toggle io.github.i4w7w4a.water-click`. If another
click-effect plugin is active, disable its left-click trigger to see one wave.

To load Water Click at startup, add this line to your Hyprland Lua config:

```lua
hl.plugin.load(os.getenv("HOME") .. "/.local/lib/waterclick/waterclick.so")
```

After upgrading Hyprland, rebuild with matching headers. Run `make clean` and
`make` before installing the new binary. To replace a module currently loaded,
unload it before installing the replacement and load it again afterwards.

## Remove

Remove the startup line if you added it. Unload the module if it is loaded,
then remove the binary and the Omarchy panel.

```sh
hyprctl plugin unload "$HOME/.local/lib/waterclick/waterclick.so"
rm -f "$HOME/.local/lib/waterclick/waterclick.so"
omarchy plugin remove io.github.i4w7w4a.water-click
```

## Credits

Water Click reuses and adapts the Water rendering code from
[HyprRipple](https://github.com/jgarza9788/hyprripple) by jgarza, including the
short profile developed in [HyprRipple PR #1](https://github.com/jgarza9788/hyprripple/pull/1).
Water Click is an independent project under the [MIT License](LICENSE).
