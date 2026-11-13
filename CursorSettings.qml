pragma ComponentBehavior: Bound
import QtQuick
import Quickshell
import Quickshell.Io

Item {
  id: root

  readonly property var shapes: [
    { value: "arrow", preview: Qt.resolvedUrl("cursor/arrow.svg") },
    { value: "left-handed", preview: Qt.resolvedUrl("cursor/left-handed.svg") },
    { value: "crosshair", preview: Qt.resolvedUrl("cursor/crosshair.svg") }
  ]
  readonly property var sizes: [24, 32, 40, 48, 56, 64]
  readonly property url preview: shapes.find(entry => entry.value === shape)?.preview || ""
  readonly property bool available: Quickshell.env("HYPRLAND_INSTANCE_SIGNATURE") !== ""
  readonly property bool busy: operation !== ""
  readonly property bool dirty: !configured || shape !== appliedShape || size !== appliedSize
  property bool configured: false
  property string status: "checking"
  property string shape: "arrow"
  property int size: 24
  property string appliedShape: "arrow"
  property int appliedSize: 24
  property string operation: ""

  readonly property string statePath: (Quickshell.env("XDG_STATE_HOME")
    || Quickshell.env("HOME") + "/.local/state") + "/water-click/cursor.json"
  readonly property string helperDirectory: Qt.resolvedUrl("cursor").toString()

  function readSelection(result) {
    if (!result || !shapes.some(entry => entry.value === result.shape) || !sizes.includes(result.size))
      return false
    configured = true
    appliedShape = result.shape
    appliedSize = result.size
    rollback()
    return true
  }

  function refresh() {
    if (!available) status = "unavailable"
    else if (!busy && status !== "error" && status !== "limited") status = "ready"
  }

  function rollback() {
    shape = appliedShape
    size = appliedSize
  }

  function run(action, args) {
    if (!available || busy) return
    operation = action
    status = "applying"
    helper.exec(["python3", "-B", "settings.py", action].concat(args))
  }

  function apply() {
    if (dirty) run("apply", [shape, String(size)])
  }

  function restore() {
    if (configured) run("restore", [])
  }

  function finish(exitCode) {
    const action = operation
    operation = ""
    if (errors.text.trim()) console.warn("Water Click cursor:", errors.text.trim())
    try {
      if (exitCode === 0) {
        const result = JSON.parse(output.text)
        if (action === "restore" || (action === "resume" && result.selection === null)) {
          configured = false
          appliedShape = "arrow"
          appliedSize = 24
          rollback()
          status = result.refreshed ? (action === "restore" ? "restored" : "ready") : "limited"
          return
        }
        if (readSelection(result.selection)) {
          status = result.refreshed ? (action === "resume" ? "ready" : "applied") : "limited"
          return
        }
      }
    } catch (error) {
      console.warn("Water Click cursor:", error)
    }
    status = "error"
  }

  FileView {
    id: saved
    path: root.statePath
    blockLoading: true
    printErrors: false
  }

  Process {
    id: helper
    workingDirectory: decodeURIComponent(root.helperDirectory.replace(/^file:\/\//, ""))
    stdout: StdioCollector { id: output }
    stderr: StdioCollector { id: errors }
    onExited: function(exitCode) { root.finish(exitCode) }
    onRunningChanged: if (!running && root.busy) {
      // A missing executable can fail to launch without emitting exited.
      Qt.callLater(function() { if (!helper.running && root.busy) root.finish(-1) })
    }
  }

  Component.onCompleted: {
    refresh()
    const text = saved.text()
    if (!text || !available) return
    try {
      if (!readSelection(JSON.parse(text))) throw new Error("invalid saved cursor")
      run("resume", [])
    } catch (error) {
      status = "error"
      console.warn("Water Click cursor:", error)
    }
  }
}
