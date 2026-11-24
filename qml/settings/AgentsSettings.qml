import QtQuick
import QtQuick.Controls

// Agents section of Settings. There is no built-in pipeline: the user creates
// agents, gives each its own instructions, and selects which of them take part
// in a run. Several active agents collaborate on the open document; a
// document-scoped agent can work on its own document elsewhere.
Column {
  id: root
  objectName: "agentsRoot"

  width: parent ? parent.width : 0
  spacing: 16

  readonly property var registry: typeof agentManager !== "undefined" ? agentManager : null
  property string editingId: ""
  property string formName: ""
  property string formInstructions: ""
  property string formDocument: ""

  function resetForm() {
    root.editingId = ""
    root.formName = ""
    root.formInstructions = ""
    root.formDocument = ""
  }

  function editAgent(agent) {
    root.editingId = agent.id
    root.formName = agent.name || ""
    root.formInstructions = agent.instructions || ""
    root.formDocument = agent.document || ""
  }

  function saveAgent() {
    if (!root.registry || !root.formName.trim())
      return
    if (root.editingId) {
      root.registry.updateAgent(root.editingId, root.formName,
                                root.formInstructions, root.formDocument)
    } else {
      root.registry.addAgent(root.formName, root.formInstructions,
                             root.formDocument)
    }
    resetForm()
  }

  Text {
    width: parent.width
    text: "Agents are theirs own workers. Give each one instructions and choose " +
          "which agents run on your next prompt — there is no fixed sequence."
    color: Theme.textMuted
    font.pixelSize: 12
    wrapMode: Text.Wrap
  }

  Rectangle {
    width: parent.width
    implicitHeight: formColumn.implicitHeight + 24
    radius: 8
    color: Theme.surfaceAlt
    border.width: 1
    border.color: Theme.border

    Column {
      id: formColumn
      anchors {
        left: parent.left
        right: parent.right
        margins: 12
      }
      spacing: 8

      Text {
        text: root.editingId ? "Edit agent" : "New agent"
        color: Theme.text
        font.pixelSize: 13
        font.bold: true
      }

      TextField {
        id: nameField
        objectName: "agentNameField"
        width: parent.width
        placeholderText: "Name (e.g. Researcher)"
        text: root.formName
        onTextChanged: root.formName = text
      }

      TextArea {
        id: instructionsField
        objectName: "agentInstructionsField"
        width: parent.width
        height: 88
        placeholderText: "Instructions — what should this agent do?"
        text: root.formInstructions
        onTextChanged: root.formInstructions = text
        wrapMode: TextArea.Wrap
      }

      TextField {
        id: documentField
        objectName: "agentDocumentField"
        width: parent.width
        placeholderText: "Document (optional — leave blank for the open document)"
        text: root.formDocument
        onTextChanged: root.formDocument = text
      }

      Row {
        spacing: 8

        Button {
          objectName: "saveAgentButton"
          text: root.editingId ? "Save agent" : "Add agent"
          enabled: !!root.registry && !!root.formName.trim()
          onClicked: root.saveAgent()
        }

        Button {
          text: "Cancel"
          visible: !!root.editingId
          onClicked: root.resetForm()
        }
      }
    }
  }

  Text {
    width: parent.width
    text: "Selected agents" + (root.registry && root.registry.activeIds.length
                               ? " (" + root.registry.activeIds.length + ")"
                               : "")
    color: Theme.text
    font.pixelSize: 13
    font.bold: true
  }

  ListView {
    id: agentsList
    objectName: "agentsList"
    width: parent.width
    height: Math.max(80, Math.min(360, contentHeight))
    interactive: false
    spacing: 8
    model: root.registry ? root.registry.agents : []

    delegate: Rectangle {
      id: agentRow
      width: agentsList.width
      implicitHeight: agentColumn.implicitHeight + 20
      radius: 8
      color: Theme.surfaceAlt
      border.width: 1
      border.color: Theme.border

      property var agent: modelData
      property bool isActive: root.registry && root.registry.activeIds.indexOf(agent.id) >= 0

      Column {
        id: agentColumn
        anchors {
          left: parent.left
          right: parent.right
          margins: 10
        }
        spacing: 6

        Row {
          width: parent.width
          spacing: 8

          Text {
            width: parent.width - controlsRow.width - 8
            text: agent.name || "Agent"
            color: Theme.text
            font.pixelSize: 13
            font.bold: true
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
          }

          Row {
            id: controlsRow
            spacing: 6

            CheckBox {
              objectName: "agentEnabledBox"
              text: "Enabled"
              checked: !!agent.enabled
              onToggled: root.registry.setAgentEnabled(agent.id, checked)
            }

            CheckBox {
              objectName: "agentActiveBox"
              text: "In run"
              checked: agentRow.isActive
              enabled: !!agent.enabled
              onToggled: root.registry.setAgentActive(agent.id, checked)
            }
          }
        }

        Text {
          width: parent.width
          text: agent.instructions ? agent.instructions : "No instructions — follows your prompt."
          color: Theme.textMuted
          font.pixelSize: 11
          wrapMode: Text.Wrap
          maximumLineCount: 3
          elide: Text.ElideRight
        }

        Text {
          visible: !!agent.document
          width: parent.width
          text: "Document: " + (agent.document || "")
          color: Theme.textFaint
          font.pixelSize: 10
        }

        Row {
          spacing: 6
          Button {
            text: "Edit"
            onClicked: root.editAgent(agent)
          }
          Button {
            text: "Delete"
            onClicked: root.registry.removeAgent(agent.id)
          }
        }
      }
    }
  }

  Text {
    visible: !!root.registry && !!root.registry.error
    width: parent.width
    text: root.registry ? root.registry.error : ""
    color: Theme.danger
    font.pixelSize: 11
    wrapMode: Text.Wrap
  }
}
