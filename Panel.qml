pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons as Commons
import qs.Ui as Ui

Item {
  id: root

  property var shell: null
  property var manifest: null
  property bool opened: false
  property string status: "checking"
  property var profile: null

  readonly property string pluginId: manifest ? manifest.id : "io.github.i4w7w4a.water-click"
  readonly property color foreground: Commons.Color.popups.text

  function open(payloadJson) {
    opened = true
    if (!statusProcess.running) {
      status = "checking"
      profile = null
      statusProcess.running = true
    }
    Qt.callLater(function() {
      if (root.opened) closeButton.forceActiveFocus(Qt.TabFocusReason)
    })
  }

  function close() {
    opened = false
  }

  function dismiss() {
    if (!shell || !shell.hide(pluginId)) close()
  }

  function readStatus(exitCode, output) {
    status = "unavailable"
    if (exitCode !== 0) return

    const reply = output.trim()
    if (reply === "unknown request") {
      status = "unloaded"
      return
    }

    try {
      const result = JSON.parse(reply)
      if (result && result.plugin === "waterclick") {
        profile = result
        status = "loaded"
      }
    } catch (error) {
      // The compositor returned no Water Click status.
    }
  }

  Process {
    id: statusProcess
    command: ["hyprctl", "waterclick", "get"]
    stdout: StdioCollector { id: statusOutput }
    onExited: function(exitCode) { root.readStatus(exitCode, statusOutput.text) }
    onRunningChanged: if (!running && root.status === "checking") root.status = "unavailable"
  }

  Commons.Translations {
    id: strings
    directory: Qt.resolvedUrl("i18n")
  }

  Ui.OverlayWindow {
    shown: root.opened
    WlrLayershell.namespace: "omarchy-water-click"

    Rectangle {
      anchors.fill: parent
      color: Commons.Color.menu.scrim
      MouseArea { anchors.fill: parent; onClicked: root.dismiss() }
    }

    Ui.BorderSurface {
      id: card
      anchors.centerIn: parent
      width: Math.max(1, Math.min(Commons.Style.space(400), parent.width - Commons.Style.space(32)))
      height: form.implicitHeight + contentTopInset + contentBottomInset
      radius: Commons.Style.cornerRadius
      padding: Commons.Style.spacing.popupPadding
      color: Commons.Color.popups.background
      borderSpec: Commons.Border.surfaceSpec("popups", "border", Commons.Color.popups.border,
        Math.max(1, Commons.Style.space(2)))

      MouseArea { anchors.fill: parent }

      FocusScope {
        anchors.fill: parent
        anchors.topMargin: card.contentTopInset
        anchors.rightMargin: card.contentRightInset
        anchors.bottomMargin: card.contentBottomInset
        anchors.leftMargin: card.contentLeftInset
        focus: true
        Keys.onEscapePressed: root.dismiss()

        ColumnLayout {
          id: form
          anchors.fill: parent
          spacing: Commons.Style.spacing.labelGap

          RowLayout {
            Layout.fillWidth: true

            PanelText {
              text: strings.t("panel.title")
              font.pixelSize: Commons.Style.font.heading
              font.bold: true
            }

            Ui.PanelActionButton {
              id: closeButton
              iconText: "×"
              tooltipText: strings.t("panel.close")
              foreground: root.foreground
              focusable: true
              Accessible.role: Accessible.Button
              Accessible.name: tooltipText
              Accessible.onPressAction: root.dismiss()
              onClicked: root.dismiss()
            }
          }

          PanelText { text: strings.t("status." + root.status) }

          PanelText {
            visible: root.status === "loaded" && root.profile
              && Number.isFinite(root.profile.radius) && Number.isFinite(root.profile.duration_ms)
            text: visible ? strings.t("panel.profile")
              .arg(Commons.I18n.formatNumber(root.profile.radius))
              .arg(Commons.I18n.formatNumber(root.profile.duration_ms)) : ""
          }

          PanelText {
            visible: root.status === "unloaded"
            text: strings.t("panel.install")
          }
        }
      }
    }
  }

  component PanelText: Text {
    Layout.fillWidth: true
    color: root.foreground
    font.family: Commons.Style.font.family
    font.pixelSize: Commons.Style.font.body
    textFormat: Text.PlainText
    wrapMode: Text.Wrap
  }
}
