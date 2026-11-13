pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons as Commons
import qs.Ui as Ui

Item {
  id: root

  property var shell: null
  property var manifest: null
  property var service: null
  property bool opened: false
  property string effectStatus: "checking"
  property string effectRequest: ""
  property string effectFeedback: ""
  property string appliedEffect: ""
  property string draftEffect: "water-short"

  readonly property string pluginId: manifest ? manifest.id : "io.github.i4w7w4a.water-click"
  readonly property color foreground: Commons.Color.popups.text
  readonly property var cursor: service || (shell ? shell.serviceFor(pluginId) : null)
  readonly property bool cursorReady: !!cursor && cursor.available
  readonly property string cursorStatus: cursor ? cursor.status : "unavailable"
  readonly property bool effectReady: effectStatus === "loaded"
  readonly property bool effectBusy: effectRequest !== ""
  readonly property var effects: ["water-short", "classic", "ring", "sparkle", "negative", "water", "prism"]

  onCursorChanged: if (opened && cursor) cursor.refresh()

  function open(payloadJson) {
    if (opened) return
    opened = true
    viewport.contentY = 0
    if (!effectBusy) {
      effectStatus = "checking"
      requestEffect("get", [])
    }
    if (cursor) cursor.refresh()
    Qt.callLater(function() {
      if (root.opened) closeButton.forceActiveFocus(Qt.TabFocusReason)
    })
  }

  function close() {
    effectPicker.close()
    shapePicker.close()
    sizePicker.close()
    if (cursor) cursor.rollback()
    opened = false
  }

  function dismiss() {
    if (!shell || !shell.hide(pluginId)) close()
  }

  function effectName(value) {
    return strings.t("effect.style." + value)
  }

  function requestEffect(action, args) {
    if (effectBusy) return
    effectRequest = action
    effectFeedback = ""
    effectProcess.exec(["timeout", "2s", "hyprctl", "waterclick", action].concat(args))
  }

  function readStatus(exitCode, reply, error) {
    if (reply === "unknown request" || error === "unknown request") {
      effectStatus = "unloaded"
      return
    }
    effectStatus = "unavailable"
    if (exitCode !== 0) return

    try {
      const result = JSON.parse(reply)
      if (!result || result.plugin !== "waterclick") return
      if (!effects.includes(result.style)) {
        effectStatus = "legacy"
        return
      }
      appliedEffect = result.style
      draftEffect = result.style
      effectStatus = "loaded"
    } catch (error) {
      // The compositor returned no Water Click status.
    }
  }

  function finishEffectRequest(exitCode, output, error) {
    const action = effectRequest
    effectRequest = ""
    const reply = output.trim()
    const stderr = error.trim()
    if (action === "get") {
      readStatus(exitCode, reply, stderr)
      return
    }
    if (action === "set") {
      if (exitCode === 0 && reply === "ok") {
        appliedEffect = draftEffect
      } else {
        effectFeedback = "apply-error"
      }
      return
    }
    if (action === "preview" && !(exitCode === 0 && reply === "ok")) {
      const reason = reply || stderr
      effectFeedback = reason === "error: animations disabled" ? "animations-disabled" : "preview-error"
    }
  }

  function previewEffect() {
    const screen = overlay.targetScreen
    if (!screen) return
    const center = effectPreview.mapToItem(overlay.contentItem, effectPreview.width / 2, effectPreview.height / 2)
    requestEffect("preview", [draftEffect, String(Math.round(screen.x + center.x)), String(Math.round(screen.y + center.y))])
  }

  Process {
    id: effectProcess
    stdout: StdioCollector { id: effectOutput }
    stderr: StdioCollector { id: effectError }
    onExited: function(exitCode) { root.finishEffectRequest(exitCode, effectOutput.text, effectError.text) }
  }

  Commons.Translations {
    id: strings
    directory: Qt.resolvedUrl("i18n")
  }

  Ui.OverlayWindow {
    id: overlay
    shown: root.opened
    WlrLayershell.namespace: "omarchy-water-click"

    property Item focusedControl: contentItem.Window.activeFocusItem
    onFocusedControlChanged: {
      if (!focusedControl) return
      let ancestor = focusedControl.parent
      while (ancestor && ancestor !== form) ancestor = ancestor.parent
      if (!ancestor) return
      const top = focusedControl.mapToItem(viewport.contentItem, 0, 0).y
      const bottom = top + focusedControl.height
      const maxY = Math.max(0, viewport.contentHeight - viewport.height)
      if (top < viewport.contentY) viewport.contentY = Math.max(0, top)
      else if (bottom > viewport.contentY + viewport.height) viewport.contentY = Math.min(maxY, bottom - viewport.height)
    }

    Rectangle {
      anchors.fill: parent
      color: Commons.Color.menu.scrim
      MouseArea { anchors.fill: parent; onClicked: root.dismiss() }
    }

    Ui.BorderSurface {
      id: card
      anchors.centerIn: parent
      width: Math.max(1, Math.min(Commons.Style.space(520), parent.width - Commons.Style.space(32)))
      height: Math.max(1, Math.min(parent.height - Commons.Style.space(32),
        header.implicitHeight + form.implicitHeight + panel.spacing + contentTopInset + contentBottomInset))
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
          id: panel
          anchors.fill: parent
          spacing: Commons.Style.spacing.panelGap

          RowLayout {
            id: header
            Layout.fillWidth: true

            PanelText {
              text: strings.t("panel.title")
              font.pixelSize: Commons.Style.font.heading
              font.bold: true
              Accessible.role: Accessible.Heading
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

          Flickable {
            id: viewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: form.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            Controls.ScrollBar.vertical: Controls.ScrollBar {}

            ColumnLayout {
              id: form
              width: viewport.width
              spacing: Commons.Style.spacing.panelGap

              ColumnLayout {
                Layout.fillWidth: true
                spacing: Commons.Style.spacing.lg

                PanelText {
                  text: strings.t("effect.title")
                  font.bold: true
                  Accessible.role: Accessible.Heading
                }

                PanelText {
                  visible: !root.effectReady
                  text: strings.t("status." + root.effectStatus)
                }

                PanelText {
                  visible: root.effectStatus === "unloaded" || root.effectStatus === "legacy"
                  text: strings.t("effect.install")
                  font.pixelSize: Commons.Style.font.bodySmall
                }

                PanelSelect {
                  id: effectPicker
                  visible: root.effectReady
                  enabled: !root.effectBusy
                  label: strings.t("effect.effect")
                  selection: root.draftEffect
                  options: root.effects.map(value => ({ value: value, label: root.effectName(value) }))
                  onSelected: function(selected) {
                    root.draftEffect = selected
                    root.effectFeedback = ""
                  }
                }

                RowLayout {
                  visible: root.effectReady
                  Layout.fillWidth: true
                  spacing: Commons.Style.spacing.lg

                  PreviewSurface {
                    id: effectPreview
                    Layout.fillWidth: true
                    Layout.preferredHeight: Commons.Style.space(118)
                    emphasized: true
                  }

                  ColumnLayout {
                    Layout.fillWidth: false
                    Layout.preferredWidth: Math.max(previewEffectButton.implicitWidth, applyEffectButton.implicitWidth)
                    spacing: Commons.Style.spacing.lg
                    PanelButton {
                      id: previewEffectButton
                      Layout.fillWidth: true
                      text: strings.t("effect.preview")
                      enabled: !root.effectBusy
                      onClicked: root.previewEffect()
                    }
                    PanelButton {
                      id: applyEffectButton
                      Layout.fillWidth: true
                      text: strings.t("panel.apply")
                      Accessible.name: strings.t("effect.apply")
                      enabled: root.effectReady && !root.effectBusy && root.draftEffect !== root.appliedEffect
                      onClicked: root.requestEffect("set", [root.draftEffect])
                    }
                  }
                }

                PanelText {
                  visible: root.effectReady
                  text: root.effectReady ? strings.t("effect.active").arg(root.effectName(root.appliedEffect)) : ""
                  font.pixelSize: Commons.Style.font.bodySmall
                }

                PanelText {
                  visible: root.effectReady
                  text: root.effectFeedback !== "" ? strings.t("effect." + root.effectFeedback)
                    : strings.t("effect.help").arg(root.effectName("classic")).arg(root.effectName("ring")).arg(root.effectName("sparkle"))
                  font.pixelSize: Commons.Style.font.bodySmall
                  color: root.effectFeedback !== "" ? Commons.Color.urgent : root.foreground
                }
              }

              Ui.PanelSeparator { Layout.fillWidth: true; foreground: root.foreground }

              ColumnLayout {
                Layout.fillWidth: true
                spacing: Commons.Style.spacing.lg

                PanelText {
                  text: strings.t("cursor.title")
                  font.bold: true
                  Accessible.role: Accessible.Heading
                }

                RowLayout {
                  Layout.fillWidth: true
                  visible: root.cursorReady
                  spacing: Commons.Style.spacing.lg

                  ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Commons.Style.spacing.lg

                    PanelSelect {
                      id: shapePicker
                      enabled: !!root.cursor && !root.cursor.busy
                      label: strings.t("cursor.shape")
                      selection: root.cursor ? root.cursor.shape : ""
                      options: root.cursor ? root.cursor.shapes.map(shape => ({
                        value: shape.value, label: strings.t("cursor.shape." + shape.value)
                      })) : []
                      onSelected: function(selected) { root.cursor.shape = selected }
                    }

                    PanelSelect {
                      id: sizePicker
                      enabled: !!root.cursor && !root.cursor.busy
                      label: strings.t("cursor.size")
                      selection: root.cursor ? String(root.cursor.size) : ""
                      options: root.cursor ? root.cursor.sizes.map(size => ({
                        value: String(size), label: strings.t("cursor.pixels").arg(Commons.I18n.formatNumber(size))
                      })) : []
                      onSelected: function(selected) { root.cursor.size = Number(selected) }
                    }
                  }

                  PreviewSurface {
                    Layout.preferredWidth: Commons.Style.space(112)
                    Layout.fillHeight: true
                    Layout.preferredHeight: Commons.Style.space(96)

                    Image {
                      anchors.centerIn: parent
                      source: root.cursor ? root.cursor.preview : ""
                      width: root.cursor ? root.cursor.size : 0
                      height: width
                      sourceSize.width: width
                      sourceSize.height: height
                      fillMode: Image.PreserveAspectFit
                      Accessible.role: Accessible.Graphic
                      Accessible.name: strings.t("cursor.preview")
                    }
                  }
                }

                RowLayout {
                  visible: root.cursorReady
                  Layout.fillWidth: true
                  spacing: Commons.Style.spacing.lg
                  Item { Layout.fillWidth: true }
                  PanelButton {
                    text: strings.t("cursor.restore")
                    enabled: !!root.cursor && root.cursor.configured && !root.cursor.busy
                    onClicked: root.cursor.restore()
                  }
                  PanelButton {
                    text: strings.t("panel.apply")
                    Accessible.name: strings.t("cursor.apply")
                    enabled: !!root.cursor && root.cursor.dirty && !root.cursor.busy
                    onClicked: root.cursor.apply()
                  }
                }

                PanelText {
                  text: root.cursorStatus === "ready" ? strings.t("cursor.help") : strings.t("cursor.status." + root.cursorStatus)
                  font.pixelSize: Commons.Style.font.bodySmall
                  color: root.cursorStatus === "error" ? Commons.Color.urgent : root.foreground
                }
              }
            }
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

  component PanelButton: Ui.Button {
    id: control
    bordered: true
    focusable: true
    foreground: root.foreground
    opacity: enabled ? 1 : 0.48
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: if (enabled) clicked()
  }

  component PanelSelect: Ui.Dropdown {
    id: control
    property string selection: ""
    signal selected(string choice)

    Layout.fillWidth: true
    value: selection
    Accessible.role: Accessible.ComboBox
    Accessible.name: label
    Accessible.description: currentLabel()
    Accessible.onPressAction: if (enabled) open()

    // The shell dropdown writes its own value before emitting changed.
    onChanged: function(choice) {
      selected(choice)
      value = Qt.binding(() => control.selection)
    }
  }

  component PreviewSurface: Ui.BorderSurface {
    id: surface

    property bool emphasized: false
    readonly property real pitch: Commons.Style.space(16)
    readonly property color gridColor: emphasized
      ? Commons.Util.alpha(Commons.Color.accent, 0.52)
      : Commons.Util.alpha(root.foreground, 0.16)

    radius: Commons.Style.cornerRadius
    color: Commons.Style.normalFillFor(root.foreground, Commons.Color.accent)
    borderSpec: Commons.Border.controlSpec("normal", root.foreground, Commons.Color.accent)

    Item {
      id: grid
      anchors.fill: parent
      anchors.margins: Commons.Style.spacing.hairline
      clip: true

      Repeater {
        model: Math.max(0, Math.ceil(grid.width / surface.pitch) - 1)
        Rectangle {
          required property int index
          x: (index + 1) * surface.pitch
          width: surface.emphasized && index % 4 === 0 ? 3 : 1
          height: grid.height
          color: surface.gridColor
        }
      }

      Repeater {
        model: Math.max(0, Math.ceil(grid.height / surface.pitch) - 1)
        Rectangle {
          required property int index
          y: (index + 1) * surface.pitch
          width: grid.width
          height: 1
          color: surface.gridColor
        }
      }
    }

    Rectangle {
      visible: surface.emphasized
      anchors.centerIn: parent
      width: Commons.Style.space(10)
      height: width
      radius: width / 2
      color: surface.color
      border.color: Commons.Color.accent
      border.width: Commons.Style.spacing.hairline
    }
  }
}
