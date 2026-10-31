// Copyright 2026 the openage authors. See copying.md for legal info.

import QtQuick

Item {
    id: menu
    anchors.fill: parent
    property var lobby: typeof fvdLobby === "undefined" ? null : fvdLobby
    // Once a synchronized session enters gameplay, the entire overlay
    // disappears. The FVD HUD and world renderer remain visible beneath it.
    visible: lobby !== null && lobby.enabled && phase !== "playing"
    z: 100

    readonly property string phase: lobby ? lobby.phase : "menu"
    readonly property bool matchSetup: phase === "assigned" || phase === "bootstrap" || phase === "streaming"
    readonly property int revealSeconds: lobby ? lobby.roleSeconds : 0
    readonly property bool revealingRoles: matchSetup && revealSeconds > 0

    Rectangle {
        anchors.fill: parent
        // Keep the world visible around the five-second role reveal.
        color: menu.revealingRoles ? "#88121e1a" : "#f0121e1a"
    }

    Rectangle {
        id: panel
        objectName: "fvdLobbyPanel"
        visible: !menu.matchSetup
        width: Math.min(parent.width - 32, 540)
        height: Math.min(parent.height - 32, settings.implicitHeight + 46)
        anchors.centerIn: parent
        clip: true
        radius: 12
        color: "#f421302b"
        border.width: 1
        border.color: "#72918b"

        Flickable {
            anchors.fill: parent
            anchors.margins: 23
            contentWidth: width
            contentHeight: settings.implicitHeight
            flickableDirection: Flickable.VerticalFlick
            clip: true

            Column {
                id: settings
                width: parent.width
                spacing: 9

                Text {
                    width: parent.width
                    text: "FAST VILLAGER DEFENSE"
                    horizontalAlignment: Text.AlignHCenter
                    color: "#f4ebca"
                    font.pixelSize: 22
                    font.bold: true
                }

                Text {
                    width: parent.width
                    text: "MULTIPLAYER LOBBY"
                    horizontalAlignment: Text.AlignHCenter
                    color: "#9ccab3"
                    font.pixelSize: 12
                    font.letterSpacing: 2
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "Host a public game or refresh the relay room list and join by room ID. Enter an access token only when the relay runs in private-alpha mode."
                    color: "#bdd0c5"
                    font.pixelSize: 12
                }

                Item { width: 1; height: 3 }

                Text { text: "Player name"; color: "#dfe9e3"; font.pixelSize: 12 }
                Rectangle {
                    width: parent.width; height: 36; color: "#17221e"; radius: 4
                    border.color: "#688478"; border.width: 1
                    TextInput {
                        anchors.fill: parent; anchors.margins: 9
                        color: "white"; selectionColor: "#5c997f"
                        font.pixelSize: 14; text: menu.lobby ? menu.lobby.name : "Player"
                        onTextEdited: if (menu.lobby) menu.lobby.name = text
                    }
                }

                Text { text: "Relay address and port"; color: "#dfe9e3"; font.pixelSize: 12 }
                Row {
                    width: parent.width; spacing: 8
                    Rectangle {
                        width: parent.width * 0.69; height: 36; color: "#17221e"; radius: 4
                        border.color: "#688478"; border.width: 1
                        TextInput {
                            anchors.fill: parent; anchors.margins: 9
                            text: menu.lobby ? menu.lobby.address : "127.0.0.1"; color: "white"; font.pixelSize: 14
                            onTextEdited: if (menu.lobby) menu.lobby.address = text
                        }
                    }
                    Rectangle {
                        width: parent.width * 0.31 - 8; height: 36
                        color: "#17221e"; radius: 4; border.color: "#688478"; border.width: 1
                        TextInput {
                            anchors.fill: parent; anchors.margins: 9
                            text: menu.lobby ? menu.lobby.port : "32456"; color: "white"; font.pixelSize: 14
                            inputMethodHints: Qt.ImhDigitsOnly
                            onTextEdited: if (menu.lobby) menu.lobby.port = text
                        }
                    }
                }

                Text { text: "Game name"; color: "#dfe9e3"; font.pixelSize: 12 }
                Rectangle {
                    width: parent.width; height: 36
                    color: "#17221e"; radius: 4; border.color: "#688478"; border.width: 1
                    TextInput {
                        anchors.fill: parent; anchors.margins: 9
                        text: menu.lobby ? menu.lobby.roomName : "FVD Game"; color: "white"; font.pixelSize: 14
                        onTextEdited: if (menu.lobby) menu.lobby.roomName = text
                    }
                }

                Text { text: "Maximum players when hosting (2–8)"; color: "#dfe9e3"; font.pixelSize: 12 }
                Rectangle {
                    width: parent.width; height: 36
                    color: "#17221e"; radius: 4; border.color: "#688478"; border.width: 1
                    TextInput {
                        anchors.fill: parent; anchors.margins: 9
                        text: menu.lobby ? menu.lobby.maxPlayers : "2"; color: "white"; font.pixelSize: 14
                        inputMethodHints: Qt.ImhDigitsOnly
                        onTextEdited: if (menu.lobby) menu.lobby.maxPlayers = text
                    }
                }

                Text { text: "Room ID to join"; color: "#dfe9e3"; font.pixelSize: 12 }
                Rectangle {
                    width: parent.width; height: 36
                    color: "#17221e"; radius: 4; border.color: "#688478"; border.width: 1
                    TextInput {
                        anchors.fill: parent; anchors.margins: 9
                        text: menu.lobby ? menu.lobby.roomId : ""
                        color: "white"; font.pixelSize: 14
                        inputMethodHints: Qt.ImhDigitsOnly
                        onTextEdited: if (menu.lobby) menu.lobby.roomId = text
                    }
                }

                Text { text: "Private-alpha player token (optional)"; color: "#dfe9e3"; font.pixelSize: 12 }
                Rectangle {
                    width: parent.width; height: 36
                    color: "#17221e"; radius: 4; border.color: "#688478"; border.width: 1
                    TextInput {
                        anchors.fill: parent; anchors.margins: 9
                        text: menu.lobby ? menu.lobby.token : ""; color: "white"; font.pixelSize: 13
                        echoMode: TextInput.Password
                        onTextEdited: if (menu.lobby) menu.lobby.token = text
                    }
                }

                Row {
                    width: parent.width; spacing: 8
                    Rectangle {
                        objectName: "fvdLobbyHostButton"
                        width: (settings.width - 8) / 2; height: 41; radius: 5
                        color: "#426c5a"; border.color: "#9bcdb8"
                        Text {
                            anchors.centerIn: parent
                            text: "HOST GAME"; color: "white"; font.bold: true
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
                    }
                    Rectangle {
                        objectName: "fvdLobbyRefreshButton"
                        width: (settings.width - 8) / 2; height: 41; radius: 5
                        color: "#4b5f69"; border.color: "#9bbdd1"
                        Text {
                            anchors.centerIn: parent
                            text: "REFRESH GAMES"; color: "white"; font.bold: true
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
                    }
                }

                Rectangle {
                    objectName: "fvdLobbyJoinButton"
                    width: parent.width; height: 41; radius: 5
                    color: "#365168"; border.color: "#9bbdd1"
                    Text {
                        anchors.centerIn: parent
                        text: "JOIN ROOM"; color: "white"; font.bold: true
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
                }

                Rectangle {
                    width: parent.width
                    height: 96
                    radius: 5
                    color: "#17221e"
                    border.color: "#546d60"
                    clip: true
                    Column {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 5
                        Text {
                            color: "#bbd8c8"
                            text: "AVAILABLE GAMES"
                            font.bold: true
                            font.pixelSize: 11
                        }
                        Text {
                            width: parent.width
                            color: "#f4deb3"
                            font.pixelSize: 12
                            textFormat: Text.PlainText
                            text: menu.lobby && menu.lobby.rooms.length > 0
                                  ? menu.lobby.rooms : "Refresh to look for waiting games."
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 125
                    radius: 5
                    color: "#17221e"
                    border.color: "#546d60"
                    Column {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 7
                        Text {
                            width: parent.width
                            color: "#bbd8c8"
                            text: "STATUS - " + (menu.lobby ? menu.lobby.phase.toUpperCase() : "MENU")
                            font.bold: true
                            font.pixelSize: 11
                        }
                        Text {
                            width: parent.width
                            color: "#ecf3ed"
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            text: menu.lobby ? menu.lobby.status : ""
                        }
                        Text {
                            width: parent.width
                            color: "#f4deb3"
                            font.pixelSize: 12
                            textFormat: Text.PlainText
                            text: menu.lobby ? menu.lobby.participants : ""
                        }
                    }
                }

                Row {
                    width: parent.width; spacing: 10
                    Rectangle {
                        id: startControl
                        objectName: "fvdLobbyStartButton"
                        width: (settings.width - 10) / 2; height: 40; radius: 5
                        color: menu.lobby && menu.lobby.canStart ? "#527e59" : "#384842"
                        border.color: "#799883"
                        Text {
                            anchors.centerIn: parent
                            text: "START GAME"; color: "white"; font.bold: true
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
                    }
                    Rectangle {
                        objectName: "fvdLobbyBackButton"
                        width: (settings.width - 10) / 2; height: 40; radius: 5
                        color: "#594b3d"; border.color: "#a69a79"
                        Text {
                            anchors.centerIn: parent
                            text: "DISCONNECT"; color: "white"; font.bold: true
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
                    }
                }

                Text {
                    width: parent.width
                    color: "#b7b7ae"; font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: "Local developer play: run game --solo\nStart Game draws and broadcasts the roles, then synchronizes the initial world."
                }
            }
        }
    }

    // Shown to every participant for five seconds after the same
    // authoritative START_MATCH is received. C++ owns the monotonic timer;
    // QML only displays it, so reloading the overlay cannot restart it.
    Rectangle {
        id: reveal
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 580)
        height: Math.min(parent.height - 32, 390)
        radius: 12
        color: "#f321322c"
        border.width: 2
        border.color: "#d4ad6e"
        visible: menu.revealingRoles

        Column {
            anchors.fill: parent
            anchors.margins: 30
            spacing: 15

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "ROLES ASSIGNED"
                color: "#f4dfad"
                font.pixelSize: 23
                font.bold: true
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "YOU ARE THE"
                color: "#d3e0d4"
                font.pixelSize: 14
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: menu.lobby ? menu.lobby.localRole : ""
                color: menu.lobby && menu.lobby.localRole === "HUNTER"
                    ? "#ffb877" : "#88dfb3"
                font.pixelSize: 37
                font.bold: true
            }
            Rectangle {
                width: parent.width
                height: 1
                color: "#5e7669"
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: menu.lobby ? menu.lobby.roles : ""
                color: "#ebf3e6"
                font.pixelSize: 16
                lineHeight: 1.6
                wrapMode: Text.WordWrap
            }
            Item { width: 1; height: 4 }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "ROLE REVEAL  ·  " + menu.revealSeconds
                color: "#b9cdbd"
                font.pixelSize: 15
                font.bold: true
            }
        }
    }

    // The existing network stack now streams validated world-state deltas
    // over the relay, but gameplay inputs and guest world rendering still
    // need integration. Keep this boundary visible; do not simulate
    // independent local worlds or drop the overlay early.
    Rectangle {
        id: prepared
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 550)
        height: 270
        radius: 12
        visible: menu.matchSetup && !menu.revealingRoles
        color: "#f321322c"
        border.width: 1
        border.color: "#739888"

        Column {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 14
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: menu.phase === "streaming" ? "WORLD STATE STREAMING"
                    : menu.phase === "bootstrap" ? "MATCH SYNCHRONIZED" : "PREPARING MATCH"
                color: "#e8e4c9"
                font.bold: true
                font.pixelSize: 22
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "The five-second role reveal is complete."
                color: "#e4f0e8"
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: menu.lobby ? menu.lobby.status : ""
                color: "#c1d0c3"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "World revision: " + (menu.lobby ? menu.lobby.worldRevision : 0)
                color: "#f4dfad"
                font.pixelSize: 13
                visible: menu.phase === "streaming"
            }
            Rectangle {
                objectName: "fvdLobbyEndButton"
                width: 230; height: 38
                anchors.horizontalCenter: parent.horizontalCenter
                radius: 5
                color: "#594b3d"
                border.color: "#a69a79"
                Text {
                    anchors.centerIn: parent
                    text: "LEAVE MATCH SETUP"
                    font.bold: true
                    color: "white"
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
            }
        }
    }
}
