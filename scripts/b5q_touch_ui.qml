import QtQuick
import QtQuick.Window

Window {
    id: root
    visible: true
    visibility: Window.FullScreen
    color: "#10131c"
    title: "B5Q Touch Debugger"

    property int stepIdx: 0
    property int totalSteps: 7
    property string stepTitle: "步骤 1/7：单点轻触"
    property string stepDesc: "轻点红色圆心一次后立刻抬手"
    property string stepHint: "音量+ = 下一步   |   音量- = 重做本步"
    property string drawMode: "tap_center"
    property int evCount: 0
    property int downCount: 0
    property int upCount: 0
    property bool activeTouch: false
    property real normX: -1
    property real normY: -1
    property string lastLine: "等待触摸..."
    property bool finished: false

    Timer {
        interval: 80
        running: true
        repeat: true
        onTriggered: {
            var xhr = new XMLHttpRequest();
            xhr.open("GET", "file:///tmp/touch_ui_state.json?t=" + Date.now());
            xhr.onreadystatechange = function() {
                if (xhr.readyState === XMLHttpRequest.DONE && xhr.responseText.length > 0) {
                    try {
                        var st = JSON.parse(xhr.responseText);
                        root.stepIdx = st.step_idx;
                        root.totalSteps = st.total_steps;
                        root.stepTitle = st.title;
                        root.stepDesc = st.desc;
                        root.drawMode = st.draw;
                        root.evCount = st.ev_count;
                        root.downCount = st.down_count;
                        root.upCount = st.up_count;
                        root.activeTouch = st.active_touch;
                        root.normX = st.norm_x;
                        root.normY = st.norm_y;
                        root.lastLine = st.last_line;
                        root.finished = st.finished;
                        if (st.quit) {
                            Qt.quit();
                        }
                    } catch (e) {}
                }
            };
            xhr.send();
        }
    }

    // Top Header Banner
    Rectangle {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 118
        color: "#1e2638"

        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 6

            Text {
                text: root.finished ? "全部 7 步采集完成！" : root.stepTitle
                color: root.finished ? "#66ff88" : "#ffffff"
                font.pixelSize: 22
                font.bold: true
            }
            Text {
                text: root.finished ? "正在保存数据并恢复桌面..." : root.stepDesc
                color: "#7be3ff"
                font.pixelSize: 16
            }
            Text {
                text: root.stepHint
                color: "#ffd066"
                font.pixelSize: 14
                font.bold: true
            }
        }
    }

    // Main Target Canvas
    Item {
        id: canvas
        anchors.top: header.bottom
        anchors.bottom: footer.top
        anchors.left: parent.left
        anchors.right: parent.right

        // Step 1: Center Single Tap
        Item {
            anchors.fill: parent
            visible: !root.finished && root.drawMode === "tap_center"
            Rectangle {
                width: 84; height: 84; radius: 42
                anchors.centerIn: parent
                color: "transparent"
                border.color: "#ff4444"
                border.width: 5
            }
            Rectangle { width: 120; height: 2; color: "#ff6666"; anchors.centerIn: parent }
            Rectangle { width: 2; height: 120; color: "#ff6666"; anchors.centerIn: parent }
            Text {
                text: "轻点一次立刻抬手"
                color: "#ffaaaa"
                font.pixelSize: 16
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.verticalCenter
                anchors.topMargin: 56
            }
        }

        // Step 2: 4 Corners
        Item {
            anchors.fill: parent
            visible: !root.finished && root.drawMode === "tap_corners"
            Repeater {
                model: [
                    { fx: 0.14, fy: 0.10, lbl: "1" },
                    { fx: 0.86, fy: 0.10, lbl: "2" },
                    { fx: 0.14, fy: 0.90, lbl: "3" },
                    { fx: 0.86, fy: 0.90, lbl: "4" }
                ]
                Rectangle {
                    width: 66; height: 66; radius: 33
                    x: canvas.width * modelData.fx - width / 2
                    y: canvas.height * modelData.fy - height / 2
                    color: "#1a3b5c"
                    border.color: "#55bbff"
                    border.width: 4
                    Text {
                        anchors.centerIn: parent
                        text: modelData.lbl
                        color: "#ffffff"
                        font.pixelSize: 24
                        font.bold: true
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                text: "按 1 → 2 → 3 → 4 顺序依次轻点"
                color: "#99ddff"
                font.pixelSize: 17
            }
        }

        // Step 3: Long Press
        Item {
            anchors.fill: parent
            visible: !root.finished && root.drawMode === "long_press"
            Rectangle {
                width: 108; height: 108; radius: 54
                anchors.centerIn: parent
                color: "#3d3108"
                border.color: "#ffd43b"
                border.width: 6
                Rectangle {
                    width: 44; height: 44; radius: 22
                    anchors.centerIn: parent
                    color: "#ffd43b"
                }
            }
            Text {
                text: "按住静止 2 秒后再抬手"
                color: "#ffe077"
                font.pixelSize: 17
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.verticalCenter
                anchors.topMargin: 70
            }
        }

        // Step 4: Double Tap
        Item {
            anchors.fill: parent
            visible: !root.finished && root.drawMode === "double_tap"
            Rectangle {
                width: 96; height: 96; radius: 48
                anchors.centerIn: parent
                color: "transparent"
                border.color: "#22e5e5"
                border.width: 5
                Rectangle {
                    width: 44; height: 44; radius: 22
                    anchors.centerIn: parent
                    color: "transparent"
                    border.color: "#22e5e5"
                    border.width: 4
                }
            }
            Text {
                text: "快速连续点击 2 次"
                color: "#88ffff"
                font.pixelSize: 17
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.verticalCenter
                anchors.topMargin: 64
            }
        }

        // Step 5: Swipe Lines (Horizontal + Vertical)
        Item {
            anchors.fill: parent
            visible: !root.finished && root.drawMode === "swipe_lines"
            // Horizontal A
            Rectangle {
                x: canvas.width * 0.15
                y: canvas.height * 0.28 - 4
                width: canvas.width * 0.70
                height: 8
                radius: 4
                color: "#38e070"
            }
            Rectangle {
                width: 30; height: 30; radius: 15
                x: canvas.width * 0.15 - 15
                y: canvas.height * 0.28 - 15
                color: "#38e070"
            }
            Rectangle {
                width: 30; height: 30; radius: 15
                x: canvas.width * 0.85 - 15
                y: canvas.height * 0.28 - 15
                color: "transparent"
                border.color: "#ffffff"
                border.width: 4
            }
            Text {
                x: canvas.width * 0.16
                y: canvas.height * 0.28 - 44
                text: "线 A：从左向右划 →"
                color: "#77ff99"
                font.pixelSize: 16
                font.bold: true
            }

            // Vertical B
            Rectangle {
                x: canvas.width * 0.50 - 4
                y: canvas.height * 0.44
                width: 8
                height: canvas.height * 0.42
                radius: 4
                color: "#44b8ff"
            }
            Rectangle {
                width: 30; height: 30; radius: 15
                x: canvas.width * 0.50 - 15
                y: canvas.height * 0.44 - 15
                color: "#44b8ff"
            }
            Rectangle {
                width: 30; height: 30; radius: 15
                x: canvas.width * 0.50 - 15
                y: canvas.height * 0.86 - 15
                color: "transparent"
                border.color: "#ffffff"
                border.width: 4
            }
            Text {
                x: canvas.width * 0.56
                y: canvas.height * 0.62
                text: "线 B：\n从上向下划 ↓"
                color: "#88d4ff"
                font.pixelSize: 16
                font.bold: true
            }
        }

        // Step 6: Large Area / Flat Thumb
        Item {
            anchors.fill: parent
            visible: !root.finished && root.drawMode === "area_palm"
            Rectangle {
                width: canvas.width * 0.58
                height: canvas.width * 0.58
                anchors.centerIn: parent
                radius: 20
                color: "#4f1863"
                border.color: "#d955ff"
                border.width: 6
                Text {
                    anchors.centerIn: parent
                    text: "大拇指指腹\n平贴按压 1 秒"
                    horizontalAlignment: Text.AlignHCenter
                    color: "#f2bbff"
                    font.pixelSize: 18
                    font.bold: true
                }
            }
        }

        // Step 7: Two Finger Pinch
        Item {
            anchors.fill: parent
            visible: !root.finished && root.drawMode === "two_finger"
            Rectangle {
                width: 78; height: 78; radius: 39
                x: canvas.width * 0.26 - width / 2
                y: canvas.height * 0.36 - height / 2
                color: "#4a2708"
                border.color: "#ff9933"
                border.width: 5
                Text { anchors.centerIn: parent; text: "指1"; color: "#ffffff"; font.pixelSize: 20; font.bold: true }
            }
            Rectangle {
                width: 78; height: 78; radius: 39
                x: canvas.width * 0.74 - width / 2
                y: canvas.height * 0.64 - height / 2
                color: "#4a2708"
                border.color: "#ff9933"
                border.width: 5
                Text { anchors.centerIn: parent; text: "指2"; color: "#ffffff"; font.pixelSize: 20; font.bold: true }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: canvas.height * 0.78
                text: "两指同时按住向中间捏合后同时抬手"
                color: "#ffbb77"
                font.pixelSize: 16
                font.bold: true
            }
        }
    }

    // Live indicator of where kernel evdev currently thinks the finger is!
    Rectangle {
        visible: root.normX >= 0 && root.normY >= 0
        width: 28; height: 28; radius: 14
        x: root.width * root.normX - width / 2
        y: root.height * root.normY - height / 2
        color: root.activeTouch ? "#88ff4444" : "#8844ff88"
        border.color: root.activeTouch ? "#ff2222" : "#22ff66"
        border.width: 3
    }

    // Bottom Live Telemetry Footer
    Rectangle {
        id: footer
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 102
        color: "#181d2b"

        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 4

            Row {
                spacing: 14
                Text {
                    text: "帧数: " + root.evCount
                    color: "#77ff88"
                    font.pixelSize: 15
                    font.bold: true
                }
                Text {
                    text: "按下: " + root.downCount
                    color: "#ff9966"
                    font.pixelSize: 15
                    font.bold: true
                }
                Text {
                    text: "抬起: " + root.upCount
                    color: "#66ccff"
                    font.pixelSize: 15
                    font.bold: true
                }
                Text {
                    text: root.activeTouch ? "[当前状态: 按住中!]" : "[当前状态: 已松开]"
                    color: root.activeTouch ? "#ff4444" : "#44ff88"
                    font.pixelSize: 15
                    font.bold: true
                }
            }
            Text {
                text: root.lastLine
                color: "#d0d8e8"
                font.pixelSize: 13
                elide: Text.ElideRight
                width: parent.width
            }
        }
    }
}
