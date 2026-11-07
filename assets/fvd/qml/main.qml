// Copyright 2026 the openage authors. See copying.md for legal info.

import QtQuick

Item {
	id: root

	property var hud: typeof fvdHud === "undefined" ? null : fvdHud
	property var lobby: typeof fvdLobby === "undefined" ? null : fvdLobby
	readonly property bool hunter: hud !== null && hud.controlled === 1
	readonly property bool placing: hud !== null && hud.placing
	readonly property bool placementValid: hud !== null && hud.placementValid
	readonly property var placementFootprint: hud === null ? [] : hud.placementFootprint
	readonly property var siteMarkers: hud === null ? [] : hud.siteMarkers
	readonly property var reservedFootprints: hud === null ? [] : hud.reservedFootprints
	readonly property int queuedTowers: hud === null ? 0 : hud.queuedTowers
	readonly property string constructionStatus: hud === null ? "" : hud.constructionStatus
	readonly property int constructionPercent: hud === null ? -1 : hud.constructionPercent
	readonly property bool canBuild: hud !== null && hud.canBuild
	readonly property bool canGarrison: hud !== null && hud.canGarrison === true
	readonly property bool garrisoning: hud !== null && hud.garrisoning === true
	readonly property bool canUngarrison: hud !== null && hud.canUngarrison === true
	readonly property bool isTower: hud !== null && hud.isTower
	readonly property bool isFoundation: hud !== null && hud.isFoundation === true
	readonly property bool canAttack: hud !== null && hud.canAttack
	readonly property bool attacking: hud !== null && hud.attacking
	readonly property bool patrolling: hud !== null && hud.patrolling
	readonly property bool targeting: attacking || patrolling
	readonly property string stance: hud === null ? "" : hud.stance
	readonly property int hunterCountdown: hud === null ? 0 : hud.hunterCountdown
	readonly property int matchTimeRemaining: hud === null || hud.matchTimeRemaining === undefined
	                                          ? -1 : hud.matchTimeRemaining
	readonly property var arrowStreaks: hud === null ? [] : hud.arrowStreaks
	readonly property var combatEffects: hud === null ? [] : hud.combatEffects
	readonly property var attackOrderMarkers: hud === null ? [] : hud.attackOrderMarkers
	readonly property var occupiedTowerFlags: hud === null ? [] : hud.occupiedTowerFlags
	readonly property int ungarrisonErrorSerial: hud === null ? 0 : hud.ungarrisonErrorSerial
	readonly property int unleashedSerial: hud === null || hud.unleashedSerial === undefined
	                                       ? 0 : hud.unleashedSerial
	readonly property string minimapImage: hud === null || hud.minimapImage === undefined ? "" : hud.minimapImage
	readonly property int minimapPixels: hud === null || hud.minimapPixels === undefined ? 0 : hud.minimapPixels
	readonly property var minimapView: hud === null || hud.minimapView === undefined ? [] : hud.minimapView
	readonly property bool optionsOpen: hud !== null && hud.optionsOpen === true
	readonly property bool flaring: hud !== null && hud.flaring === true
	readonly property bool towerLabelsOn: hud === null || hud.towerLabelsOn !== false
	readonly property int deletableFoundations: hud === null || hud.deletableFoundations === undefined
	                                            ? 0 : hud.deletableFoundations
	readonly property var flareMarkers: hud === null || hud.flareMarkers === undefined ? [] : hud.flareMarkers
	readonly property var minimapFlares: hud === null || hud.minimapFlares === undefined ? [] : hud.minimapFlares

	// Five-pointed star path, centred on (x, y).
	function starPath(ctx, x, y, outer, inner) {
		ctx.beginPath()
		for (var i = 0; i < 10; ++i) {
			var r = i % 2 === 0 ? outer : inner
			var a = -Math.PI / 2 + i * Math.PI / 5
			if (i === 0) ctx.moveTo(x + r * Math.cos(a), y + r * Math.sin(a))
			else ctx.lineTo(x + r * Math.cos(a), y + r * Math.sin(a))
		}
		ctx.closePath()
	}
	readonly property bool musicOn: hud === null || hud.musicOn !== false
	readonly property bool effectsOn: hud === null || hud.effectsOn !== false
	readonly property string audioNote: hud === null || hud.audioNote === undefined ? "" : hud.audioNote

	function flagColor(player) {
		// Keep player indices identical to the shader/outline palette
		// in renderer/stages/world/render_stage.cpp.
		var colors = ["#95abb7", "#d65d52", "#438be5", "#55b66f",
		              "#e8d16a", "#57c6d1", "#aa75d9", "#bbbbc7", "#ec9b4c"]
		return colors[player] || "#e6e1cd"
	}

	function formatMatchTime(totalSeconds) {
		var minutes = Math.floor(totalSeconds / 60)
		var seconds = totalSeconds % 60
		return ("0" + minutes).slice(-2) + ":" + ("0" + seconds).slice(-2)
	}

	// Options (sound on/off). Clicks are routed by the presenter using the
	// objectNames below, like the build panel buttons.
	Rectangle {
		id: optionsButton
		objectName: "fvdOptionsButton"
		anchors.top: parent.top
		anchors.right: parent.right
		anchors.margins: 8
		z: 11
		visible: root.hud !== null
		width: 92
		height: 28
		radius: 4
		color: root.optionsOpen ? "#ae6e30" : "#ee182824"
		border.width: 1
		border.color: "#b5d9c4"

		Text {
			anchors.centerIn: parent
			color: "#ffffff"
			font.bold: true
			font.pixelSize: 13
			text: "Options"
		}
	}

	Rectangle {
		id: optionsPanel
		objectName: "fvdOptionsPanel"
		anchors.top: optionsButton.bottom
		anchors.right: parent.right
		anchors.topMargin: 4
		anchors.rightMargin: 8
		z: 11
		visible: optionsButton.visible && root.optionsOpen
		width: 220
		height: optionsContent.implicitHeight + 20
		radius: 6
		color: "#f2182824"
		border.width: 1
		border.color: "#72918b"

		Column {
			id: optionsContent
			anchors.top: parent.top
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.margins: 10
			spacing: 8

			Text {
				color: "#ffffff"
				font.bold: true
				font.pixelSize: 15
				text: "Options"
			}

			Repeater {
				model: [
					{ name: "fvdMusicToggle", label: "Music", on: root.musicOn },
					{ name: "fvdEffectsToggle", label: "Sound effects", on: root.effectsOn },
					{ name: "fvdTowerLabelsToggle", label: "Builder tower labels", on: root.towerLabelsOn }
				]

				delegate: Rectangle {
					objectName: modelData.name
					width: 200
					height: 30
					radius: 4
					color: modelData.on ? "#3e7862" : "#4a3a30"
					border.width: 1
					border.color: "#b5d9c4"

					Text {
						anchors.left: parent.left
						anchors.leftMargin: 10
						anchors.verticalCenter: parent.verticalCenter
						color: "#ffffff"
						font.pixelSize: 13
						text: modelData.label
					}
					Text {
						anchors.right: parent.right
						anchors.rightMargin: 10
						anchors.verticalCenter: parent.verticalCenter
						color: modelData.on ? "#c8f0d8" : "#ffb6a8"
						font.bold: true
						font.pixelSize: 13
						text: modelData.on ? "ON" : "OFF"
					}
				}
			}

			Text {
				visible: text.length > 0
				width: 200
				wrapMode: Text.WordWrap
				color: "#f4c78c"
				font.pixelSize: 11
				text: root.audioNote
			}

			Text {
				width: 200
				wrapMode: Text.WordWrap
				color: "#b5d9c4"
				font.pixelSize: 11
				text: "M mutes everything during a match."
			}
		}
	}

	Text {
		anchors.top: parent.top
		anchors.right: optionsButton.visible ? optionsButton.left : parent.right
		anchors.topMargin: 13
		anchors.rightMargin: 13
		z: 10
		visible: root.lobby !== null && root.lobby.phase === "playing"
		text: "F10 — LEAVE MATCH"
		color: "#dce9df"
		font.pixelSize: 13
		font.bold: true
	}

	Text {
		anchors.top: parent.top
		anchors.right: parent.right
		anchors.topMargin: 44
		anchors.rightMargin: 13
		z: 10
		visible: root.lobby !== null && root.lobby.builderDisconnected
		text: "BUILDER DISCONNECTED — MATCH CONTINUES"
		color: "#f4c78c"
		font.pixelSize: 13
		font.bold: true
	}

	Text {
		anchors.top: parent.top
		anchors.right: parent.right
		anchors.topMargin: 65
		anchors.rightMargin: 13
		z: 10
		visible: root.lobby !== null && root.lobby.departureNotice.length > 0
		text: root.lobby !== null ? root.lobby.departureNotice : ""
		color: "#f4c78c"
		font.pixelSize: 13
		font.bold: true
	}

	// Terminal result comes exclusively from the replicated Hunter state.
	// UI never infers a winner from local fog, sprites or player disconnects.
	Rectangle {
		id: matchResultOverlay
		anchors.fill: parent
		z: 50
		visible: root.hud !== null && root.hud.matchEnded === true
		color: "#d9091616"

		Rectangle {
			anchors.centerIn: parent
			width: 420
			height: 210
			radius: 10
			color: "#f0213431"
			border.width: 2
			border.color: "#e2b778"

			Column {
				anchors.centerIn: parent
				spacing: 17
				Text {
					anchors.horizontalCenter: parent.horizontalCenter
					text: root.hud !== null ? root.hud.matchResultTitle : ""
					color: "#fff2d2"
					font.pixelSize: 36
					font.bold: true
				}
				Text {
					anchors.horizontalCenter: parent.horizontalCenter
					text: root.hud !== null ? root.hud.matchResultReason : ""
					color: "#e1e8e1"
					font.pixelSize: 17
				}
				Text {
					anchors.horizontalCenter: parent.horizontalCenter
					text: "F10 — Leave match"
					color: "#b4c6c0"
					font.pixelSize: 13
				}
			}
		}
	}

	// Pure presentation of host-authored foundations, towers and reserved
	// footprint cells. Never blocks world input or predicts a build outcome.
	Canvas {
		id: constructionSites
		anchors.fill: parent
		z: -1
		visible: root.siteMarkers.length > 0 || root.reservedFootprints.length > 0
		onVisibleChanged: requestPaint()
		Connections {
			target: root
			function onSiteMarkersChanged() { constructionSites.requestPaint() }
			function onTowerLabelsOnChanged() { constructionSites.requestPaint() }
			function onReservedFootprintsChanged() { constructionSites.requestPaint() }
		}
		onPaint: {
			var ctx = getContext("2d")
			ctx.clearRect(0, 0, width, height)
			var reserved = root.reservedFootprints
			ctx.lineWidth = 1
			ctx.fillStyle = "rgba(90, 175, 205, 0.12)"
			ctx.strokeStyle = "rgba(124, 201, 223, 0.42)"
			for (var i = 0; i < reserved.length; ++i) {
				var tile = reserved[i]
				ctx.beginPath()
				ctx.moveTo(tile.ax, tile.ay)
				ctx.lineTo(tile.bx, tile.by)
				ctx.lineTo(tile.cx, tile.cy)
				ctx.lineTo(tile.dx, tile.dy)
				ctx.closePath()
				ctx.fill()
				ctx.stroke()
			}
			var sites = root.siteMarkers
			var labels = root.towerLabelsOn
			ctx.font = "bold 11px sans-serif"
			ctx.textAlign = "center"
			ctx.textBaseline = "middle"
			for (var j = 0; j < sites.length; ++j) {
				var site = sites[j]
				var text = "P" + site.owner + " " + site.phase.toUpperCase()
				if (labels) {
				var labelWidth = ctx.measureText(text).width + 12
				var left = site.x - labelWidth / 2
				var top = site.y - 34
				var outline = site.phase === "Orphaned" ? "#a9b8b1"
				            : site.phase === "Queued" ? "#f1c66c"
				            : site.phase === "Planned" ? "#8cccf5"
				            : site.phase === "Building" ? "#92e1ae"
				            : "#cedbd1"
				ctx.fillStyle = "rgba(20, 34, 30, 0.9)"
				ctx.strokeStyle = outline
				ctx.lineWidth = 1
				ctx.fillRect(left, top, labelWidth, 18)
				ctx.strokeRect(left, top, labelWidth, 18)
				ctx.fillStyle = outline
				ctx.fillText(text, site.x, top + 9)
				}
				if (site.queueNumber > 0) {
					var number = String(site.queueNumber)
					ctx.font = "bold 10px sans-serif"
					ctx.fillStyle = "rgba(20, 34, 30, 0.95)"
					ctx.beginPath()
					ctx.arc(site.x, site.y - 8, 9, 0, Math.PI * 2)
					ctx.fill()
					ctx.strokeStyle = "#f1c66c"
					ctx.stroke()
					ctx.fillStyle = "#fff4d5"
					ctx.fillText(number, site.x, site.y - 8)
					ctx.font = "bold 11px sans-serif"
				}
			}
		}
	}

	// Tile-accurate 2D build preview, using the same camera projection as
	// world sprites. Mouse input still passes through to the game.
	Canvas {
		id: towerPreview
		anchors.fill: parent
		z: -1
		visible: root.placing && root.placementFootprint.length > 0
		onVisibleChanged: requestPaint()
		Connections {
			target: root
			function onPlacementFootprintChanged() { towerPreview.requestPaint() }
			function onPlacementValidChanged() { towerPreview.requestPaint() }
		}
		onPaint: {
			var ctx = getContext("2d")
			ctx.clearRect(0, 0, width, height)
			var tiles = root.placementFootprint
			ctx.lineWidth = 1.5
			ctx.fillStyle = root.placementValid ? "rgba(65, 216, 111, 0.24)" : "rgba(240, 71, 71, 0.30)"
			ctx.strokeStyle = root.placementValid ? "#74ed96" : "#ff6969"
			for (var i = 0; i < tiles.length; ++i) {
				var tile = tiles[i]
				ctx.beginPath()
				ctx.moveTo(tile.ax, tile.ay)
				ctx.lineTo(tile.bx, tile.by)
				ctx.lineTo(tile.cx, tile.cy)
				ctx.lineTo(tile.dx, tile.dy)
				ctx.closePath()
				ctx.fill()
				ctx.stroke()
			}
		}
	}

	// All viewers see the occupying Builder's player-colour flag above the
	// tower, even if an allied player constructed the tower. This is a
	// screen-space flag at the fixed tower-top height; no sprite changes.
	Canvas {
		id: towerFlags
		anchors.fill: parent
		z: 2
		visible: root.occupiedTowerFlags.length > 0
		onVisibleChanged: if (visible) requestPaint()
		Connections {
			target: root
			function onOccupiedTowerFlagsChanged() {
				if (towerFlags.visible) towerFlags.requestPaint()
			}
		}
		onPaint: {
			var ctx = getContext("2d")
			ctx.clearRect(0, 0, width, height)
			for (var i = 0; i < root.occupiedTowerFlags.length; ++i) {
				var flag = root.occupiedTowerFlags[i]
				var x = flag.x
				var y = flag.y
				var cloth = root.flagColor(flag.player)
				ctx.lineWidth = 2
				ctx.strokeStyle = "#eff0da"
				ctx.beginPath()
				ctx.moveTo(x, y + 9)
				ctx.lineTo(x, y - 24)
				ctx.stroke()
				ctx.fillStyle = cloth
				ctx.strokeStyle = "#18212c"
				ctx.lineWidth = 1.1
				ctx.beginPath()
				ctx.moveTo(x + 1, y - 24)
				ctx.lineTo(x + 26, y - 19)
				ctx.lineTo(x + 20, y - 11)
				ctx.lineTo(x + 1, y - 14)
				ctx.closePath()
				ctx.fill()
				ctx.stroke()
				ctx.fillStyle = "#101a25"
				ctx.font = "bold 10px sans-serif"
				ctx.fillText(String(flag.player), x + 8, y - 15)
			}
		}
	}

	Timer {
		id: ungarrisonErrorTimer
		interval: 3000
		repeat: false
	}
	onUngarrisonErrorSerialChanged: if (ungarrisonErrorSerial > 0) ungarrisonErrorTimer.restart()
	Rectangle {
		z: 15
		anchors.horizontalCenter: parent.horizontalCenter
		anchors.top: parent.top
		anchors.topMargin: 105
		width: ungarrisonErrorText.implicitWidth + 36
		height: 40
		radius: 6
		color: "#df342f29"
		border.color: "#ed9f8e"
		visible: ungarrisonErrorTimer.running
		Text {
			id: ungarrisonErrorText
			anchors.centerIn: parent
			font.pixelSize: 16
			font.bold: true
			color: "#fff1e7"
			text: root.hud === null ? "" : root.hud.ungarrisonError
		}
	}

	// Active orders come from the authoritative world snapshot. Target rings
	// follow replicated entities; ground rings use the authored destination.
	Canvas {
		id: attackOrders
		anchors.fill: parent
		z: -1
		visible: root.attackOrderMarkers.length > 0
		onVisibleChanged: if (visible) requestPaint()
		Connections {
			target: root
			function onAttackOrderMarkersChanged() {
				if (attackOrders.visible)
					attackOrders.requestPaint()
			}
		}
		onPaint: {
			var ctx = getContext("2d")
			ctx.clearRect(0, 0, width, height)
			var orders = root.attackOrderMarkers
			for (var i = 0; i < orders.length; ++i) {
				var order = orders[i]
				var color = order.focus ? "#ff766d"
				          : order.target ? "#f4bc63" : "#72dce8"
				ctx.globalAlpha = order.reached ? 0.38 : 0.7
				ctx.strokeStyle = color
				ctx.lineWidth = order.focus ? 2 : 1.5
				ctx.beginPath()
				ctx.moveTo(order.x0, order.y0)
				ctx.lineTo(order.x1, order.y1)
				ctx.stroke()
				var radius = order.target ? 11 : 8
				ctx.beginPath()
				ctx.arc(order.x1, order.y1, radius, 0, Math.PI * 2)
				ctx.stroke()
				ctx.beginPath()
				ctx.moveTo(order.x1 - radius - 4, order.y1)
				ctx.lineTo(order.x1 + radius + 4, order.y1)
				ctx.moveTo(order.x1, order.y1 - radius - 4)
				ctx.lineTo(order.x1, order.y1 + radius + 4)
				ctx.stroke()
				if (order.focus) {
					ctx.beginPath()
					ctx.arc(order.x1, order.y1, radius + 4, 0, Math.PI * 2)
					ctx.stroke()
				}
			}
			ctx.globalAlpha = 1
		}
	}

	// Screen-space display only. Simulation shot events determine impact
	// and damage; this Canvas never creates selectable world objects.
	Canvas {
		id: arrows
		anchors.fill: parent
		z: -1
		visible: root.arrowStreaks.length > 0 || root.combatEffects.length > 0
		onVisibleChanged: if (visible) requestPaint()
		Connections {
			target: root
			function onArrowStreaksChanged() {
				if (arrows.visible)
					arrows.requestPaint()
			}
			function onCombatEffectsChanged() {
				if (arrows.visible)
					arrows.requestPaint()
			}
		}
		onPaint: {
			var ctx = getContext("2d")
			ctx.clearRect(0, 0, width, height)
			var list = root.arrowStreaks
			for (var i = 0; i < list.length; ++i) {
				var shot = list[i]
				var dx = shot.x1 - shot.x0
				var dy = shot.y1 - shot.y0
				var len = Math.sqrt(dx * dx + dy * dy)
				if (shot.impact) {
					ctx.globalAlpha = shot.alpha
					ctx.strokeStyle = shot.hunter ? "#9ce6ed" : "#fff0ba"
					ctx.lineWidth = 2
					ctx.beginPath()
					ctx.arc(shot.x1, shot.y1, shot.hunter ? 5 : 4, 0, Math.PI * 2)
					ctx.stroke()
					continue
				}
				if (len < 0.5) continue
				var nx = dx / len
				var ny = dy / len
				ctx.globalAlpha = shot.alpha
				ctx.strokeStyle = shot.hunter ? "#9ce6ed" : "#f9d7a1"
				ctx.lineWidth = shot.hunter ? 2.5 : 2
				ctx.lineCap = "round"
				ctx.beginPath()
				ctx.moveTo(shot.x0, shot.y0)
				ctx.lineTo(shot.x1, shot.y1)
				ctx.stroke()
				// Two tiny fletching strokes distinguish the arrow head.
				ctx.lineWidth = 1
				ctx.beginPath()
				ctx.moveTo(shot.x1 - nx * 5 - ny * 2, shot.y1 - ny * 5 + nx * 2)
				ctx.lineTo(shot.x1, shot.y1)
				ctx.lineTo(shot.x1 - nx * 5 + ny * 2, shot.y1 - ny * 5 - nx * 2)
				ctx.stroke()
			}
			// Confirmed damage/death FX are generated only from the host's
			// health/removal state, never from a predicted projectile impact.
			var combat = root.combatEffects
			for (var j = 0; j < combat.length; ++j) {
				var fx = combat[j]
				ctx.globalAlpha = fx.alpha
				ctx.strokeStyle = fx.death ? "#f5b05e" : "#ffecb6"
				ctx.lineWidth = fx.death ? 3 : 2
				ctx.beginPath()
				ctx.arc(fx.x, fx.y, fx.radius, 0, Math.PI * 2)
				ctx.stroke()
				if (fx.death) {
					for (var k = 0; k < 6; ++k) {
						var a = Math.PI * k / 3
						var inner = fx.radius * 0.35
						var outer = fx.radius * 1.25
						ctx.beginPath()
						ctx.moveTo(fx.x + Math.cos(a) * inner, fx.y + Math.sin(a) * inner)
						ctx.lineTo(fx.x + Math.cos(a) * outer, fx.y + Math.sin(a) * outer)
						ctx.stroke()
					}
				}
			}
			ctx.globalAlpha = 1
		}
	}

	Rectangle {
		id: countdownBanner
		anchors.top: parent.top
		anchors.horizontalCenter: parent.horizontalCenter
		visible: root.hunterCountdown > 0
		width: 250
		height: visible ? 74 : 0
		color: "#ed182824"
		border.width: 1
		border.color: root.hunterCountdown <= 5 ? "#f4a861" : "#72918b"
		radius: 7

		Column {
			anchors.centerIn: parent
			spacing: 1

			Text {
				anchors.horizontalCenter: parent.horizontalCenter
				text: "HUNTER ARRIVES IN"
				color: "#e1e9e4"
				font.pixelSize: 12
				font.bold: true
			}

			Text {
				anchors.horizontalCenter: parent.horizontalCenter
				text: "00:" + ("0" + root.hunterCountdown).slice(-2)
				color: root.hunterCountdown <= 5 ? "#ffb877" : "#ffffff"
				font.pixelSize: 33
				font.bold: true
			}
		}
	}

	Rectangle {
		id: matchTimerBanner
		anchors.top: parent.top
		anchors.horizontalCenter: parent.horizontalCenter
		visible: root.matchTimeRemaining >= 0
		         && root.hunterCountdown <= 0
		         && !(root.hud !== null && root.hud.matchEnded === true)
		width: matchTimerLabel.implicitWidth + 28
		height: visible ? 32 : 0
		color: "#dc182824"
		border.width: 1
		border.color: root.hunter ? "#a66e62" : "#668e78"
		radius: 6

		Text {
			id: matchTimerLabel
			anchors.centerIn: parent
			text: (root.hunter ? "Kill all builders: " : "Survive: ")
			      + root.formatMatchTime(Math.max(0, root.matchTimeRemaining))
			      + " minutes"
			color: "#f2f5f2"
			font.pixelSize: 14
			font.bold: true
		}
	}

	Rectangle {
		anchors.top: root.hunterCountdown > 0
		             ? countdownBanner.bottom : matchTimerBanner.bottom
		anchors.topMargin: root.hunterCountdown > 0 || matchTimerBanner.visible ? 6 : 0
		anchors.horizontalCenter: parent.horizontalCenter
		width: help.width + 32
		height: help.height + 20
		color: "#d0203020"
		radius: 6

		Text {
			id: help
			anchors.centerIn: parent
			color: "#ffffff"
			font.pixelSize: 14
			text: root.placing
			    ? "Build: click to place | Shift+click to queue | Shift+drag: tower line | Esc cancels"
			    : root.attacking ? "Attack-move: click destination | Right-click or Esc cancels"
			    : root.patrolling ? "Patrol: click the far end of the route | Right-click or Esc cancels"
			    : root.canAttack
			    ? "Hunter: right-click enemy to focus attack | Right-click ground to move | A: Attack-move | P: Patrol\n"
			      + "Stance: Z Aggressive | X Defensive | C Observe"
			    : root.canBuild
			    ? "Builder: right-click foundation to resume | B: Build Tower | Shift+drag: line"
			    : root.isTower && root.hunter
			    ? "Hunter: selected enemy tower is inspection only"
			    : (root.isTower || root.isFoundation) && !root.hunter
			    ? "Builder: Shift+click allied same-type structures | Double-click to select owner's on-screen structures"
			    : "Drag to select units | Builders: Shift+click allied structures, double-click to group"
			horizontalAlignment: Text.AlignHCenter
		}
	}

	// Minimap, bottom left. The viewer's own fog and state only; the
	// presenter paints it top-down and this item turns it into an AoE2-style
	// diamond. Clicks are routed by the presenter ("fvdMinimapSurface").
	Rectangle {
		id: minimapPanel
		objectName: "fvdMinimapPanel"
		anchors.left: parent.left
		anchors.bottom: parent.bottom
		visible: root.hud !== null && root.minimapPixels > 0
		// Same height as the build panel beside it (which grows with its
		// buttons); the diamond is 2:1, so the width follows the height.
		height: buildPanel.height
		width: minimapDiamond.width + 16
		radius: 6
		color: "#ee182824"
		border.width: 1
		border.color: "#72918b"

		Item {
			id: minimapDiamond
			anchors.centerIn: parent
			// 2:1 diamond, centred in the full-height panel.
			height: Math.min(parent.height - 16, 200)
			width: height * 2
			// Side of the square image before it is turned into the diamond.
			readonly property real side: width / Math.SQRT2

			Item {
				id: minimapSurface
				objectName: "fvdMinimapSurface"
				width: Math.max(root.minimapPixels, 1)
				height: width
				x: (minimapDiamond.width - width) / 2
				y: (minimapDiamond.height - height) / 2
				readonly property real fit: minimapDiamond.side / width
				transform: [
					Rotation { origin.x: minimapSurface.width / 2; origin.y: minimapSurface.height / 2; angle: 45 },
					Scale {
						origin.x: minimapSurface.width / 2
						origin.y: minimapSurface.height / 2
						xScale: minimapSurface.fit
						yScale: minimapSurface.fit / 2
					}
				]

				Image {
					anchors.fill: parent
					source: root.minimapImage
					smooth: true
					cache: false
					asynchronous: false
				}

				Canvas {
					id: minimapView
					anchors.fill: parent
					onPaint: {
						var ctx = getContext("2d")
						ctx.reset()
						var v = root.minimapView
						if (v.length !== 8) return
						ctx.strokeStyle = "#ffffff"
						ctx.lineWidth = 2 / minimapSurface.fit
						ctx.beginPath()
						ctx.moveTo(v[0], v[1])
						for (var i = 2; i < 8; i += 2) ctx.lineTo(v[i], v[i + 1])
						ctx.closePath()
						ctx.stroke()
					}
					Connections {
						target: root
						function onMinimapViewChanged() { minimapView.requestPaint() }
					}
				}
			}
		}
	}

	// Flares on the minimap, drawn above the diamond so stars keep their shape.
	Canvas {
		id: minimapFlareLayer
		parent: minimapPanel
		anchors.fill: parent
		visible: root.minimapFlares.length > 0
		onPaint: {
			var ctx = getContext("2d")
			ctx.reset()
			for (var i = 0; i < root.minimapFlares.length; ++i) {
				var f = root.minimapFlares[i]
				var p = minimapSurface.mapToItem(minimapPanel, f.x, f.y)
				root.starPath(ctx, p.x, p.y, 7, 3)
				ctx.fillStyle = root.flagColor(f.player)
				ctx.fill()
				ctx.lineWidth = 1
				ctx.strokeStyle = "#ffffff"
				ctx.stroke()
			}
		}
		Connections {
			target: root
			function onMinimapFlaresChanged() { minimapFlareLayer.requestPaint() }
		}
	}

	Rectangle {
		id: buildPanel
		objectName: "fvdBuildPanel"
		anchors.left: minimapPanel.visible ? minimapPanel.right : parent.left
		anchors.bottom: parent.bottom
		width: 284
		// Always the size of its tallest layout (measured: a Builder with a
		// tower selected while placing, or the Hunter targeting with
		// horses), so the HUD never jumps. Grows only if text wraps further.
		height: Math.max(panelContent.implicitHeight + 24, 360)
		radius: 6
		color: "#ee182824"
		border.width: 1
		border.color: "#72918b"

		Column {
			id: panelContent
			anchors.top: parent.top
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.margins: 12
			spacing: 8

			Text {
				color: "#ffffff"
				font.bold: true
				font.pixelSize: 16
				text: root.hud === null ? "FVD" : "Player " + root.hud.controlled
			      + (root.hunter ? " — Hunter" : " — Builder")
			}
			Text {
				// Long hints ("Builder — click a friendly completed tower to
				// garrison (Esc cancels)") wrap inside the panel.
				width: parent.width
				wrapMode: Text.WordWrap
				color: "#ffffff"
				font.bold: true
				font.pixelSize: 14
				text: root.hud === null ? "No unit selected" : root.hud.selection
			}
			Text {
				visible: root.isTower || root.canBuild || root.canAttack
				color: "#d6e6db"
				font.pixelSize: 13
				text: "HP: " + (root.hud === null ? "—" : root.hud.health)
			}
			Text {
				visible: root.canAttack && root.stance.length > 0
				color: "#d6e6db"
				font.pixelSize: 13
				text: "Stance: " + root.stance
			}
			Text {
				visible: root.isTower
				color: "#d6e6db"
				font.pixelSize: 13
				text: "Damage: " + (root.hud === null ? "Not defined" : root.hud.damage)
			}
			Text {
				visible: root.isTower
				color: "#d6e6db"
				font.pixelSize: 13
				text: "Armour: " + (root.hud === null ? "Not defined" : root.hud.armor)
			}
			Text {
				visible: root.hud !== null && root.hud.killsLabel.length > 0
				color: "#d6e6db"
				font.pixelSize: 13
				text: root.hud === null ? "" : root.hud.killsLabel + (!root.hud.killsAvailable ? "—" : root.hud.kills)
			}
			Text {
				width: parent.width
				wrapMode: Text.WordWrap
				visible: root.constructionStatus.length > 0
				color: "#b5d9c4"
				font.pixelSize: 12
				text: root.constructionStatus
			}
			Rectangle {
				visible: root.constructionPercent >= 0
				width: parent.width
				height: visible ? 6 : 0
				radius: 3
				color: "#344b40"
				Rectangle {
					width: parent.width * Math.max(0, Math.min(100, root.constructionPercent)) / 100
					height: parent.height
					radius: parent.radius
					color: "#84ddb2"
				}
			}
			Text {
				visible: root.lobby !== null && root.lobby.orderFeedback.length > 0
				width: parent.width
				wrapMode: Text.WordWrap
				color: root.lobby !== null && root.lobby.orderFeedback.indexOf("rejected") >= 0
				       ? "#ffb6a8" : "#b5d9c4"
				font.pixelSize: 12
				text: root.lobby === null ? "" : root.lobby.orderFeedback
				      + (root.lobby.pendingOrders > 0 ? " (" + root.lobby.pendingOrders + " awaiting host)" : "")
			}
			Text {
				width: parent.width
				wrapMode: Text.WordWrap
				visible: !root.hunter && (root.placing || root.queuedTowers > 0)
				color: root.placing && !root.placementValid ? "#ffb6a8" : "#b5d9c4"
				font.pixelSize: 12
				text: "Queued towers: " + root.queuedTowers +
				      (root.placing ? (root.placementValid ? " | Site ready" : " | Site blocked") : "")
			}
			Rectangle {
				id: actionButton
				objectName: "fvdActionButton"
				visible: root.canBuild || root.canAttack
				width: 252
				height: 32
				radius: 4
				color: root.attacking || root.placing ? "#ae6e30" : "#3e7862"
				border.width: 1
				border.color: "#b5d9c4"

				Text {
					anchors.centerIn: parent
					color: "#ffffff"
					font.bold: true
					font.pixelSize: 14
					text: root.canAttack ? "Attack-move (A)" : root.placing ? "Cancel (Esc)" : "Build Tower (B)"
				}

				MouseArea {
					anchors.fill: parent
					cursorShape: Qt.PointingHandCursor
				}
			}
			// Clicks are routed by the presenter using these objectNames.
			Rectangle {
				objectName: "fvdGarrisonButton"
				visible: root.canGarrison
				width: 252
				height: 32
				radius: 4
				color: root.garrisoning ? "#ae6e30" : "#3e7862"
				border.width: 1
				border.color: "#b5d9c4"

				Text {
					anchors.centerIn: parent
					color: "#ffffff"
					font.bold: true
					font.pixelSize: 14
					text: root.garrisoning ? "Click a tower (Esc cancels)" : "Garrison (T)"
				}

				MouseArea {
					anchors.fill: parent
					cursorShape: Qt.PointingHandCursor
				}
			}
			Rectangle {
				objectName: "fvdUngarrisonButton"
				visible: root.canUngarrison
				width: 252
				height: 32
				radius: 4
				color: "#3e7862"
				border.width: 1
				border.color: "#b5d9c4"

				Text {
					anchors.centerIn: parent
					color: "#ffffff"
					font.bold: true
					font.pixelSize: 14
					text: "Ungarrison (G)"
				}

				MouseArea {
					anchors.fill: parent
					cursorShape: Qt.PointingHandCursor
				}
			}
			Rectangle {
				objectName: "fvdPatrolButton"
				visible: root.canAttack
				width: 252
				height: 32
				radius: 4
				color: root.patrolling ? "#ae6e30" : "#3e7862"
				border.width: 1
				border.color: "#b5d9c4"

				Text {
					anchors.centerIn: parent
					color: "#ffffff"
					font.bold: true
					font.pixelSize: 14
					text: "Patrol (P)"
				}

				MouseArea {
					anchors.fill: parent
					cursorShape: Qt.PointingHandCursor
				}
			}
			Row {
				visible: root.canAttack
				spacing: 6

				Repeater {
					model: [
						{ name: "Aggressive", hotkey: "Z" },
						{ name: "Defensive", hotkey: "X" },
						{ name: "Observe", hotkey: "C" }
					]

					delegate: Rectangle {
						objectName: "fvd" + modelData.name + "Button"
						width: 80
						height: 32
						radius: 4
						color: root.stance === modelData.name ? "#ae6e30" : "#3e7862"
						border.width: 1
						border.color: "#b5d9c4"

						Text {
							anchors.centerIn: parent
							color: "#ffffff"
							font.bold: true
							font.pixelSize: 11
							text: modelData.name + " (" + modelData.hotkey + ")"
						}

						MouseArea {
							anchors.fill: parent
							cursorShape: Qt.PointingHandCursor
						}
					}
				}
			}
			Rectangle {
				objectName: "fvdDeleteFoundationsButton"
				// Only while this Builder has foundations to remove.
				visible: root.canBuild && root.deletableFoundations > 0
				width: 252
				height: 32
				radius: 4
				color: "#7a3b31"
				border.width: 1
				border.color: "#e3b0a5"

				Text {
					anchors.centerIn: parent
					color: "#ffffff"
					font.bold: true
					font.pixelSize: 14
					text: "Delete all foundations (" + root.deletableFoundations + ")"
				}
			}
			Rectangle {
				objectName: "fvdFlareButton"
				visible: root.hud !== null && root.hud.controlled >= 2
				width: 252
				height: 32
				radius: 4
				color: root.flaring ? "#ae6e30" : "#3e7862"
				border.width: 1
				border.color: "#b5d9c4"

				Text {
					anchors.centerIn: parent
					color: "#ffffff"
					font.bold: true
					font.pixelSize: 14
					text: root.flaring ? "Click the map to flare (Esc cancels)" : "Flare (F)"
				}
			}
			Rectangle {
				objectName: "fvdCancelButton"
				visible: root.canAttack && root.targeting
				width: 252
				height: 32
				radius: 4
				color: "#674a32"
				border.width: 1
				border.color: "#b5d9c4"

				Text {
					anchors.centerIn: parent
					color: "#ffffff"
					font.bold: true
					font.pixelSize: 14
					text: "Cancel targeting (Esc)"
				}

				MouseArea {
					anchors.fill: parent
					cursorShape: Qt.PointingHandCursor
				}
			}
		}
	}

	// Builder flares: a pulsing star in the flaring player's colour with an
	// expanding ring, fading out over five seconds.
	Canvas {
		id: flareLayer
		anchors.fill: parent
		z: 8
		visible: root.flareMarkers.length > 0
		onPaint: {
			var ctx = getContext("2d")
			ctx.reset()
			for (var i = 0; i < root.flareMarkers.length; ++i) {
				var f = root.flareMarkers[i]
				var fade = Math.max(0, 1 - f.age / 5)
				var pulse = 1 + 0.18 * Math.sin(f.age * 9)
				var color = root.flagColor(f.player)
				ctx.globalAlpha = fade * 0.8
				ctx.strokeStyle = color
				ctx.lineWidth = 3
				ctx.beginPath()
				ctx.arc(f.x, f.y, 14 + (f.age % 1.25) * 40, 0, Math.PI * 2)
				ctx.stroke()
				ctx.globalAlpha = fade
				root.starPath(ctx, f.x, f.y - 18, 20 * pulse, 8.5 * pulse)
				ctx.fillStyle = color
				ctx.fill()
				ctx.lineWidth = 2
				ctx.strokeStyle = "#ffffff"
				ctx.stroke()
			}
			ctx.globalAlpha = 1
		}
		Connections {
			target: root
			function onFlareMarkersChanged() { flareLayer.requestPaint() }
		}
	}

	// Shown to everyone the moment the Hunter's horses start spawning.
	Item {
		id: unleashedBanner
		// Half the window wide, in the upper part of the screen.
		anchors.horizontalCenter: parent.horizontalCenter
		y: parent.height * 0.26 - height / 2
		z: 9
		width: parent.width * 0.5
		height: unleashedText.implicitHeight + 32
		opacity: 0
		visible: opacity > 0

		Rectangle {
			anchors.fill: parent
			radius: 8
			color: "#e6261a14"
			border.width: 2
			border.color: "#e0763c"
		}

		Text {
			id: unleashedText
			anchors.centerIn: parent
			text: "HUNTER UNLEASHED"
			color: "#ffd2a6"
			style: Text.Outline
			styleColor: "#3a0f02"
			// Text fills most of the banner's width.
			font.pixelSize: Math.min(root.height * 0.08, root.width * 0.034)
			font.weight: Font.Black
			font.letterSpacing: 6
		}

		SequentialAnimation {
			id: unleashedAnimation
			NumberAnimation { target: unleashedBanner; property: "opacity"; to: 1; duration: 180 }
			NumberAnimation { target: unleashedBanner; property: "scale"; from: 1.25; to: 1; duration: 0 }
			PauseAnimation { duration: 3000 }
			NumberAnimation { target: unleashedBanner; property: "opacity"; to: 0; duration: 900 }
		}

		Connections {
			target: root
			function onUnleashedSerialChanged() {
				if (root.unleashedSerial > 0) {
					unleashedAnimation.restart()
				}
			}
		}
	}

	// The startup menu takes over input until the relay pre-match flow is ready.
	LobbyOverlay {
		anchors.fill: parent
	}
}
