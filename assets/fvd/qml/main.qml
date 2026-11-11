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
	readonly property int deletableFoundations: hud === null || hud.deletableFoundations === undefined
	                                            ? 0 : hud.deletableFoundations
	readonly property var flareMarkers: hud === null || hud.flareMarkers === undefined ? [] : hud.flareMarkers
	readonly property var minimapFlares: hud === null || hud.minimapFlares === undefined ? [] : hud.minimapFlares
	readonly property real healthFraction: {
		var parts = hud !== null && hud.health ? String(hud.health).split("/") : []
		var current = parts.length === 2 ? Number(parts[0]) : 0
		var maximum = parts.length === 2 ? Number(parts[1]) : 0
		return maximum > 0 ? Math.max(0, Math.min(1, current / maximum)) : 0
	}
	readonly property var playerRoster: {
		var lines = lobby !== null && lobby.roles ? lobby.roles.split("\n") : []
		var players = []
		for (var i = 0; i < lines.length && players.length < 8; ++i) {
			var slot = lines[i].match(/\(Player ([1-8])\)/)
			if (slot) players.push({ number: Number(slot[1]), name: lines[i].split(" — ")[0] })
		}
		if (players.length === 0 && hud !== null) {
			players.push({ number: 1, name: "Hunter" })
			players.push({ number: 2, name: "Builder" })
		}
		players.sort(function(a, b) { return a.number - b.number })
		return players
	}
	readonly property var quickStatus: {
		if (ungarrisonErrorTimer.running && hud !== null && hud.ungarrisonError)
			return { text: hud.ungarrisonError, error: true }
		if (lobby !== null && lobby.builderDisconnected)
			return { text: "Builder disconnected — match continues", error: true }
		if (lobby !== null && lobby.departureNotice && lobby.departureNotice.length > 0)
			return { text: lobby.departureNotice, error: true }
		if (lobby !== null && lobby.orderFeedback && lobby.orderFeedback.length > 0)
			return { text: lobby.orderFeedback,
			         error: lobby.orderFeedback.indexOf(" rejected") !== -1 }
		if (placing)
			return { text: placementValid ? "Tower site ready · Click to build" : "Choose a clear tower site",
			         error: false }
		if (garrisoning) return { text: "Click a friendly tower to garrison", error: false }
		if (attacking) return { text: "Attack move · Click a destination", error: false }
		if (patrolling) return { text: "Patrol · Click the far end of the route", error: false }
		if (flaring) return { text: "Flare · Click the map", error: false }
		return { text: "Fast Villager Defense", error: false }
	}

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

	Rectangle {
		id: topBar
		objectName: "fvdTopBar"
		anchors.top: parent.top
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.margins: 6
		z: 10
		visible: root.hud !== null
		height: 46
		radius: 13
		color: "#50212b28"
		readonly property int contentVerticalOffset: -3
		border.width: 1
		border.color: "#7ccbd6d0"

		Image {
			anchors.left: parent.left
			anchors.leftMargin: 34
			anchors.verticalCenter: parent.verticalCenter
			anchors.verticalCenterOffset: 0
			width: 80
			source: "fvd-logo.png"
			sourceClipRect: Qt.rect(78, 179, 1615, 557)
			fillMode: Image.PreserveAspectFit
			mipmap: true
		}
		Rectangle {
			x: 146
			anchors.verticalCenter: parent.verticalCenter
			anchors.verticalCenterOffset: topBar.contentVerticalOffset
			width: 1
			height: 23
			color: "#80e1e7dd"
		}
		Text {
			id: statusLabel
			x: 165
			anchors.verticalCenter: parent.verticalCenter
			anchors.verticalCenterOffset: topBar.contentVerticalOffset
			width: Math.max(110, parent.width - 555)
			text: root.quickStatus.text
			elide: Text.ElideRight
			color: root.quickStatus.error ? "#ff7575"
			       : text === "Fast Villager Defense" ? "#f2f1ea" : "#f2d4a3"
			font.pixelSize: 15
			font.bold: true
		}
		Image {
			anchors.right: killsCount.left
			anchors.rightMargin: 24
			anchors.verticalCenter: parent.verticalCenter
			anchors.verticalCenterOffset: -1
			width: 40
			source: "kills-icon.png"
			fillMode: Image.PreserveAspectFit
			mipmap: false
			visible: !root.hunter
		}
		Text {
			id: killsCount
			anchors.right: timerSeparator.left
			anchors.rightMargin: 24
			anchors.verticalCenter: parent.verticalCenter
			anchors.verticalCenterOffset: topBar.contentVerticalOffset
			text: (root.hud !== null && root.hud.playerKillsAvailable ? root.hud.playerKills : "0")
			color: "#ffffff"
			font.pixelSize: 19
			font.bold: true
			visible: !root.hunter
		}
		MouseArea {
			id: killsTooltipArea
			anchors.right: timerSeparator.left
			anchors.rightMargin: 18
			anchors.verticalCenter: parent.verticalCenter
			width: 118
			height: parent.height
			hoverEnabled: true
			cursorShape: Qt.ArrowCursor
			visible: !root.hunter
		}
		Rectangle {
			anchors.top: parent.bottom
			anchors.right: killsTooltipArea.right
			anchors.topMargin: 5
			visible: !root.hunter && killsTooltipArea.containsMouse
			width: killsTooltipText.implicitWidth + 18
			height: killsTooltipText.implicitHeight + 12
			radius: 4
			color: "#e61b2520"
			border.width: 1
			border.color: "#c7d9d0"
			z: 1

			Text {
				id: killsTooltipText
				anchors.centerIn: parent
				text: "Enemy units killed by your towers. 100 Kills: Level up"
				color: "#f2f1ea"
				font.pixelSize: 13
				font.bold: true
			}
		}
		Rectangle {
			id: timerSeparator
			anchors.right: timerLabel.left
			anchors.rightMargin: 18
			anchors.verticalCenter: parent.verticalCenter
			anchors.verticalCenterOffset: topBar.contentVerticalOffset
			width: 1
			height: 23
			color: "#80e1e7dd"
		}
		Text {
			id: timerLabel
			anchors.right: parent.right
			anchors.rightMargin: 112
			anchors.verticalCenter: parent.verticalCenter
			anchors.verticalCenterOffset: topBar.contentVerticalOffset
			text: root.matchTimeRemaining >= 0
			      ? root.formatMatchTime(root.matchTimeRemaining) : "--:--"
			color: "#ffffff"
			font.pixelSize: 18
		}
		Rectangle {
			objectName: "fvdMenuButton"
			anchors.right: parent.right
			anchors.rightMargin: 8
			anchors.verticalCenter: parent.verticalCenter
			anchors.verticalCenterOffset: topBar.contentVerticalOffset
			width: 30
			height: 32
			visible: root.hud !== null
			color: "transparent"
			Text {
				anchors.centerIn: parent
				text: "☰"
				color: "#ffffff"
				font.pixelSize: 23
			}
			MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
		}
	}

	// Options (sound on/off). Clicks are routed by the presenter.
	Rectangle {
		id: optionsButton
		objectName: "fvdOptionsButton"
		anchors.top: parent.top
		anchors.right: parent.right
		anchors.topMargin: 11
		anchors.rightMargin: 50
		z: 11
		visible: root.hud !== null
		width: 28
		height: 28
		radius: 7
		color: root.optionsOpen ? "#6b8a7a" : "#282b3934"
		border.width: 1
		border.color: root.optionsOpen ? "#c8e6d6" : "transparent"

		Text {
			anchors.centerIn: parent
			color: "#ffffff"
			font.pixelSize: 22
			text: "⚙"
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
		radius: 11
		color: "#d1273530"
		border.width: 1
		border.color: "#9ad7e3d7"

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
					{ name: "fvdEffectsToggle", label: "Sound effects", on: root.effectsOn }
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
			Rectangle {
				objectName: "fvdLeaveButton"
				visible: root.lobby !== null && root.lobby.phase === "playing"
				width: 200
				height: visible ? 30 : 0
				radius: 5
				color: "#604d3e38"
				border.color: "#9ad7e3d7"
				Text {
					anchors.centerIn: parent
					text: "Leave match (F10)"
					color: "#f2e9e3"
					font.pixelSize: 12
				}
				MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
			}
		}
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
			ctx.font = "bold 11px sans-serif"
			ctx.textAlign = "center"
			ctx.textBaseline = "middle"
			for (var j = 0; j < sites.length; ++j) {
				var site = sites[j]
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
		anchors.top: topBar.bottom
		anchors.topMargin: 8
		anchors.horizontalCenter: parent.horizontalCenter
		visible: root.hunterCountdown > 0
		width: 250
		height: visible ? 74 : 0
		color: "#bd212e29"
		border.width: 1
		border.color: root.hunterCountdown <= 5 ? "#f4c993" : "#a4e0e8d7"
		radius: 10

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
		id: minimapPanel
		objectName: "fvdMinimapPanel"
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		anchors.rightMargin: 16
		anchors.bottomMargin: 16
		visible: root.hud !== null && root.minimapPixels > 0
		height: 164
		width: 236
		radius: 13
		color: "#50232f2a"
		border.width: 1
		border.color: "#a4e0e8d7"

		Item {
			id: minimapDiamond
			anchors.centerIn: parent
			// 2:1 diamond, centred in the full-height panel.
			height: Math.min((parent.width - 12) / 2, parent.height - 14)
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
		Text {
			anchors.top: parent.top
			anchors.right: parent.right
			anchors.topMargin: 7
			anchors.rightMargin: 9
			text: "N\n▲"
			color: "#f5f7ed"
			font.pixelSize: 11
			font.bold: true
			horizontalAlignment: Text.AlignHCenter
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
		anchors.left: parent.left
		anchors.right: minimapPanel.visible ? minimapPanel.left : parent.right
		anchors.bottom: parent.bottom
		anchors.leftMargin: 16
		anchors.rightMargin: 14
		anchors.bottomMargin: 16
		height: 164
		radius: 13
		color: "#5022302a"
		border.width: 1
		border.color: "#8bcbd9cc"

		Row {
			id: hudRow
			anchors.fill: parent
			anchors.margins: 11
			spacing: 8

			Item {
				id: selectionCard
				width: Math.max(178, Math.min(300, hudRow.width * 0.28))
				height: parent.height
				Rectangle {
					id: portrait
					anchors.left: parent.left
					anchors.verticalCenter: parent.verticalCenter
					width: Math.min(96, selectionCard.width * 0.38)
					height: parent.height - 10
					radius: 8
					clip: true
					color: "transparent"
					border.color: "#a8e1e9da"
					Image {
						id: portraitImage
						anchors.fill: parent
						anchors.margins: 2
						visible: root.hud !== null && root.hud.selectionKind !== ""
						source: root.hud === null || root.hud.selectionKind === "" ? ""
						        : root.hud.selectionKind === "horse" ? "horse_portrait.png" : "builder_portrait_cutout.png"
						fillMode: Image.PreserveAspectCrop
						layer.enabled: true
						layer.effect: ShaderEffect {
							property color teamColor: root.flagColor(root.hud === null ? 0 : root.hud.selectionOwner)
							property bool teamColorEnabled: root.hud !== null && root.hud.selectionOwner > 0
							property vector2d portraitSize: Qt.vector2d(portraitImage.width, portraitImage.height)
							fragmentShader: "qrc:/fvd/shaders/portrait.frag.qsb"
						}
					}
					Text {
						anchors.centerIn: parent
						visible: root.hud === null || root.hud.selectionKind === ""
						text: root.isTower || root.isFoundation ? "♜" : "✦"
						font.pixelSize: 42
						color: "#e4ece1"
					}
				}
				Column {
					anchors.left: portrait.right
					anchors.leftMargin: 9
					anchors.right: parent.right
					anchors.verticalCenter: parent.verticalCenter
					spacing: 7
					Text {
						width: parent.width
						text: root.hud === null ? "No selection" : root.hud.selection
						elide: Text.ElideRight
						font.family: "Georgia"
						font.bold: true
						font.pixelSize: 18
						color: "white"
					}
					Text {
						width: parent.width
						text: root.hud === null ? "" : root.hud.selectionSubtitle
						wrapMode: Text.WordWrap
						maximumLineCount: 2
						elide: Text.ElideRight
						font.pixelSize: 11
						color: "#e1e6dd"
					}
					Rectangle {
						width: parent.width
						visible: root.hud !== null && root.hud.selectionCount > 0
						height: visible ? 19 : 0
						radius: 6
						color: "#84432d29"
						border.color: "#7fffffff"
						Rectangle {
							width: parent.width * root.healthFraction
							height: parent.height
							radius: parent.radius
							color: "#4fbd4b"
						}
						Text {
							anchors.centerIn: parent
							text: root.hud === null ? "—" : root.hud.health
							font.pixelSize: 11
							color: "white"
							style: Text.Outline
							styleColor: "#77000000"
						}
					}
				}
			}
			Rectangle { width: 1; height: parent.height - 12; anchors.verticalCenter: parent.verticalCenter; color: "#60dbe5d8" }
			Column {
				id: statCard
				width: hudRow.width < 900 ? 112 : 175
				anchors.verticalCenter: parent.verticalCenter
				spacing: 8
				Text {
					text: "⚔  " + (root.isTower ? "Damage: " + root.hud.damage
					                 : root.canAttack ? "Hunter cavalry" : "Builder unit")
					font.pixelSize: 11; color: "#f1f3ec"
				}
				Text {
					text: "⬡  " + (root.isTower ? "Armour: " + root.hud.armor
					                 : root.canAttack ? root.stance : "Construction")
					font.pixelSize: 11; color: "#f1f3ec"
				}
				Text {
					width: parent.width; elide: Text.ElideRight
					text: "✦  " + (root.hud !== null && root.hud.killsLabel.length > 0
					                 ? root.hud.killsLabel + (root.hud.killsAvailable ? root.hud.kills : "—")
					                 : root.constructionStatus.length > 0 ? root.constructionStatus : "Ready")
					font.pixelSize: 11; color: "#f1f3ec"
				}
				Text {
					visible: root.queuedTowers > 0 || root.constructionPercent >= 0
					text: root.constructionPercent >= 0
					      ? "Building: " + root.constructionPercent + "%" : "Queued: " + root.queuedTowers
					font.pixelSize: 11; color: "#ccead0"
				}
			}
			Rectangle { width: 1; height: parent.height - 12; anchors.verticalCenter: parent.verticalCenter; color: "#60dbe5d8" }
			Item {
				id: actionsCard
				width: hudRow.width - selectionCard.width - statCard.width
				       - playersCard.width - 3 * hudRow.spacing - 3
				height: parent.height
				Text { text: "ACTIONS"; font.pixelSize: 10; font.letterSpacing: 1; color: "#cbd8cb" }
				Grid {
					id: actionGrid
					anchors.top: parent.top
					anchors.topMargin: 22
					width: parent.width
					spacing: 5
					columns: Math.max(2, Math.min(6, Math.floor((width + spacing) / (150 + spacing))))
					Repeater {
						model: [
							{ name: "fvdActionButton", label: root.canAttack ? "Attack move (A)" : root.placing ? "Cancel (Esc)" : "Build tower (B)", icon: root.canAttack ? "⚔" : "♜", show: root.canBuild || root.canAttack, active: root.attacking || root.placing },
							{ name: "fvdGarrisonButton", label: root.garrisoning ? "Choose tower" : "Garrison (T)", icon: "♜", show: root.canGarrison, active: root.garrisoning },
							{ name: "fvdUngarrisonButton", label: "Ungarrison (G)", icon: "⇧", show: root.canUngarrison, active: false },
							{ name: "fvdPatrolButton", label: "Patrol (P)", icon: "⟳", show: root.canAttack, active: root.patrolling },
							{ name: "fvdAggressiveButton", label: "Aggressive (Z)", icon: "⚔", show: root.canAttack, active: root.stance === "Aggressive" },
							{ name: "fvdDefensiveButton", label: "Defensive (X)", icon: "⬡", show: root.canAttack, active: root.stance === "Defensive" },
							{ name: "fvdObserveButton", label: "Observe (C)", icon: "◉", show: root.canAttack, active: root.stance === "Observe" },
							{ name: "fvdCancelButton", label: "Cancel (Esc)", icon: "×", show: root.canAttack && root.targeting, active: false },
							{ name: "fvdDeleteFoundationsButton", label: "Clear sites (" + root.deletableFoundations + ")", icon: "⌫", show: root.canBuild && root.deletableFoundations > 0, active: false },
							{ name: "fvdFlareButton", label: root.flaring ? "Place flare" : "Flare (F)", icon: "⚑", show: root.hud !== null && root.hud.controlled >= 2, active: root.flaring }
						]
						delegate: Rectangle {
							objectName: modelData.name
							visible: modelData.show
							width: Math.min(150, (actionGrid.width - (actionGrid.columns - 1) * actionGrid.spacing) / actionGrid.columns)
							height: 34
							radius: 7
							color: modelData.active ? "#a0587b78" : "#5b32403c"
							border.color: modelData.active ? "#d5f0eb" : "#8fa6b5a8"
							Text {
								anchors.left: parent.left
								anchors.leftMargin: 8
								anchors.verticalCenter: parent.verticalCenter
								text: modelData.icon
								font.pixelSize: 19; color: "#f3f7f2"
							}
							Text {
								anchors.left: parent.left
								anchors.leftMargin: 33
								anchors.right: parent.right
								anchors.rightMargin: 4
								anchors.verticalCenter: parent.verticalCenter
								text: modelData.label
								elide: Text.ElideRight
								font.pixelSize: 11; color: "#f5f7f2"
							}
							MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
						}
					}
				}
			}
			Rectangle { width: 1; height: parent.height - 12; anchors.verticalCenter: parent.verticalCenter; color: "#60dbe5d8" }
			Item {
				id: playersCard
				width: hudRow.width < 900 ? 126 : 170
				height: parent.height
				Text { text: "♟  Players"; font.family: "Georgia"; font.pixelSize: 14; color: "white" }
				Column {
					anchors.top: parent.top
					anchors.topMargin: 24
					spacing: 1
					Repeater {
						model: root.playerRoster.slice(0, 8)
						delegate: Row {
							spacing: 6
							Rectangle {
								width: 9; height: 9; radius: 2
								anchors.verticalCenter: parent.verticalCenter
								color: root.flagColor(modelData.number)
							}
							Text {
								width: playersCard.width - 22
								text: modelData.name + (root.hud !== null && modelData.number === root.hud.controlled ? " (You)" : "")
								elide: Text.ElideRight
								font.pixelSize: 10; color: "#d8ded6"
								style: Text.Outline; styleColor: "#99000000"
							}
						}
					}
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
