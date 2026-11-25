import QtQuick
import QtQuick.Controls
import QtCharts
import Templnote

// Chart block. Data comes from either an inline CSV snippet or a Table block
// elsewhere in the same document, so a single table can feed several charts.
Item {
  id: root

  property string blockId: ""
  property int blockType: 16
  property string content: ""
  property int headingLevel: 0
  property int numberIndex: 1
  property string alignment: "left"
  property bool isChecked: false
  property bool showPlaceholder: false
  property var blockMetadata: null
  property bool isBlockHovered: false
  property var model: null
  property var searchHighlights: []
  property bool searchNavigationActive: false

  readonly property string editorText: root.content
  readonly property bool isFocused: csvArea.activeFocus || root.activeFocus
  readonly property real firstLineHeight: 32

  signal enterPressed()
  signal backspacePressed()
  signal undoRequested()
  signal redoRequested()
  signal moveFocusRequested(int direction, real xPosition)
  signal caretMoved(int position, real cursorGlobalY, real cursorHeight)
  signal commandMenuRequested(string query, real xPosition, real yPosition, real cursorHeight)
  signal commandMenuDismissed()
  signal commandMenuAccepted()
  signal commandMenuNavigated(int direction)
  signal selectionChanged(bool hasSelection, real xPosition, real yPosition, real cursorHeight)
  signal focusRequested(int position)
  signal searchNavigated(int direction)
  signal searchDismissed()
  signal searchEditStarted()

  implicitWidth: parent ? parent.width : 600
  implicitHeight: editing ? 280 : 250

  property bool editing: false
  property var draftMeta: ({})
  property string draftCsv: ""

  readonly property var chartMeta: {
    const meta = root.blockMetadata
    return (meta && meta.chart) ? meta.chart : {}
  }
  readonly property string chartType: chartMeta.type || "line"
  readonly property string chartTitle: chartMeta.title || "Chart"

  function parseCsv(text) {
    const lines = String(text || "").trim().split(/\r?\n/)
    if (lines.length <= 1)
      return []
    lines.shift()
    const result = []
    lines.forEach(function(line) {
      const parts = line.split(",")
      if (parts.length < 2)
        return
      result.push({label: parts[0].trim(), value: Number(parts[1]) || 0})
    })
    return result
  }

  function tableSeries() {
    const id = chartMeta.dataBlockId
    if (!root.model || !id || typeof root.model.indexOfBlock !== "function")
      return []
    const row = root.model.indexOfBlock(id)
    if (row < 0)
      return []
    const meta = root.model.data(root.model.index(row, 0), 262) // MetadataRole
    const table = meta && meta.table
    if (!table || !table.columns || table.columns.length < 2 || !table.rows)
      return []
    const labelCol = table.columns[0].id
    let valueCol = chartMeta.valueColumn || table.columns[1].id
    if (!table.columns.some(function(c) { return c.id === valueCol }))
      valueCol = table.columns[1].id
    const series = []
    table.rows.forEach(function(r) {
      const cells = r.cells || {}
      series.push({
        label: String(cells[labelCol] || ""),
        value: Number(cells[valueCol]) || 0
      })
    })
    return series
  }

  readonly property var chartData: {
    const id = chartMeta.dataBlockId
    if (id && id.length > 0)
      return tableSeries()
    return parseCsv(root.content)
  }
  readonly property var chartLabels: {
    const out = []
    root.chartData.forEach(function(d) { out.push(d.label) })
    return out
  }
  readonly property var chartValues: {
    const out = []
    root.chartData.forEach(function(d) { out.push(d.value) })
    return out
  }
  readonly property real maxValue: {
    let max = 1
    root.chartValues.forEach(function(v) { if (v > max) max = v })
    return max
  }

  function focusEditor(position) {
    const meta = {}
    const source = root.blockMetadata || {}
    Object.keys(source).forEach(function(k) { meta[k] = source[k] })
    draftMeta = meta
    draftCsv = root.content
    csvArea.text = draftCsv
    editorButton.checked = true
    editing = true
    csvArea.forceActiveFocus()
  }

  function refreshFromModel() {
    if (!editing)
      draftCsv = root.content
  }

  function commit() {
    const meta = {}
    Object.keys(draftMeta).forEach(function(k) { meta[k] = draftMeta[k] })
    if (!meta.chart || typeof meta.chart !== "object")
      meta.chart = {}
    else
      meta.chart = JSON.parse(JSON.stringify(meta.chart))
    meta.chart.type = typeCombo.currentText
    meta.chart.title = titleField.text
    meta.chart.dataBlockId = linkField.text.trim()
    meta.chart.csv = csvArea.text

    if (root.model)
      root.model.setBlockMetadataWithText(root.blockId, meta, csvArea.text)
    editing = false
  }

  Rectangle {
    anchors.fill: parent
    radius: 8
    color: Theme.surfaceAlt
    border.width: 1
    border.color: Theme.border
  }

  Row {
    id: toolbar
    anchors { top: parent.top; left: parent.left; margins: 8 }
    spacing: 8
    Text {
      anchors.verticalCenter: parent.verticalCenter
      text: "CHART"
      color: Theme.textFaint
      font.pixelSize: 10
      font.bold: true
      font.letterSpacing: 1.2
    }
    Button {
      id: editorButton
      width: 24
      height: 20
      padding: 0
      flat: true
      text: editing ? "✓" : "✎"
      font.pixelSize: 12
      Accessible.name: editing ? "Apply chart" : "Edit chart"
      onClicked: {
        if (editing) {
          commit()
          checked = false
        } else {
          focusEditor(-1)
        }
      }
    }
  }

  Loader {
    id: chartLoader
    anchors {
      top: toolbar.bottom
      left: parent.left
      right: parent.right
      bottom: parent.bottom
      margins: 8
    }
    visible: !editing
    sourceComponent: {
      if (chartType === "bar") return barChartComponent
      if (chartType === "pie") return pieChartComponent
      if (chartType === "scatter") return scatterChartComponent
      return lineChartComponent
    }
  }

  Component {
    id: lineChartComponent
    ChartView {
      title: root.chartTitle
      antialiasing: true
      legend.visible: root.chartData.length > 0
      ValueAxis {
        id: axisX
        min: 0
        max: Math.max(1, root.chartData.length - 1)
        tickCount: Math.min(8, Math.max(2, root.chartData.length))
        gridVisible: false
        labelsVisible: false
      }
      ValueAxis {
        id: axisY
        min: 0
        max: root.maxValue * 1.15 + 1
      }
      LineSeries {
        id: lineSeries
        name: "Values"
        axisX: axisX
        axisY: axisY
        function refreshSeries() {
          clear()
          for (var i = 0; i < root.chartData.length; ++i)
            append(i, root.chartData[i].value)
        }
        Component.onCompleted: refreshSeries()
        Connections {
          target: root
          function onChartDataChanged() { lineSeries.refreshSeries() }
        }
      }
    }
  }

  Component {
    id: scatterChartComponent
    ChartView {
      title: root.chartTitle
      antialiasing: true
      legend.visible: false
      ValueAxis { id: axisX; min: 0; max: Math.max(1, root.chartData.length - 1); labelsVisible: false; gridVisible: false }
      ValueAxis { id: axisY; min: 0; max: root.maxValue * 1.15 + 1 }
      ScatterSeries {
        id: scatterSeries
        axisX: axisX
        axisY: axisY
        function refreshSeries() {
          clear()
          for (var i = 0; i < root.chartData.length; ++i)
            append(i, root.chartData[i].value)
        }
        Component.onCompleted: refreshSeries()
        Connections {
          target: root
          function onChartDataChanged() { scatterSeries.refreshSeries() }
        }
      }
    }
  }

  Component {
    id: barChartComponent
    ChartView {
      title: root.chartTitle
      antialiasing: true
      legend.visible: false
      BarSeries {
        axisX: BarCategoryAxis { categories: root.chartLabels }
        BarSet {
          label: "Values"
          values: root.chartValues
        }
      }
    }
  }

  Component {
    id: pieChartComponent
    ChartView {
      title: root.chartTitle
      antialiasing: true
      legend.visible: true
      PieSeries {
        id: pieSeries
        function refreshSeries() {
          clear()
          for (var i = 0; i < root.chartData.length; ++i)
            append(root.chartData[i].label, root.chartData[i].value)
        }
        Component.onCompleted: refreshSeries()
        Connections {
          target: root
          function onChartDataChanged() { pieSeries.refreshSeries() }
        }
      }
    }
  }

  Column {
    id: editorPane
    anchors {
      top: toolbar.bottom
      left: parent.left
      right: parent.right
      bottom: parent.bottom
      margins: 10
      topMargin: 34
    }
    visible: editing
    spacing: 6

    Row {
      spacing: 8
      Text { anchors.verticalCenter: parent.verticalCenter; text: "Type"; color: Theme.textMuted; font.pixelSize: 11 }
      ComboBox {
        id: typeCombo
        width: 110
        model: ["line", "bar", "pie", "scatter"]
        Component.onCompleted: currentIndex = Math.max(0, model.indexOf(root.chartType))
      }
      Text { anchors.verticalCenter: parent.verticalCenter; text: "Title"; color: Theme.textMuted; font.pixelSize: 11 }
      TextField {
        id: titleField
        width: 160
        text: root.chartTitle
        placeholderText: "Chart title"
      }
    }

    TextField {
      id: linkField
      width: parent.width
      placeholderText: "Optional Table block id (leave empty to use CSV below)"
      text: chartMeta.dataBlockId || ""
    }

    TextArea {
      id: csvArea
      width: parent.width
      height: 130
      font.family: "monospace"
      font.pixelSize: 12
      wrapMode: TextEdit.Wrap
      selectByMouse: true
      placeholderText: "Label,Value\nA,4\nB,7"
      Keys.onEscapePressed: editing = false
    }

    Text {
      width: parent.width
      text: "CSV first row is a header. Values are read from the second column."
      color: Theme.textFaint
      font.pixelSize: 10
      wrapMode: Text.Wrap
    }
  }
}
