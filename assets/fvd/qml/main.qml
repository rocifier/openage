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

	function formatMatchTime(totalSeconds) {
		var minutes = Math.floor(totalSeconds / 60)
		var seconds = totalSeconds % 60
		return ("0" + minutes).slice(-2) + ":" + ("0" + seconds).slice(-2)
	}

	Text {
		anchors.top: parent.top
		anchors.right: parent.right
		anchors.margins: 13
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
		anchors.topMargin: 35
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
		anchors.topMargin: 56
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
				var text = "P" + site.owner + " " + site.phase.toUpperCase()
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
			    : (root.isTower || root.isFoundation) && !root.hunter
			    ? "Builder: press Delete to remove your selected tower or foundation"
			    : "Drag to select your units | Right-click to move"
			horizontalAlignment: Text.AlignHCenter
		}
	}

	Rectangle {
		id: buildPanel
		objectName: "fvdBuildPanel"
		anchors.left: parent.left
		anchors.bottom: parent.bottom
		width: 284
		height: panelContent.implicitHeight + 24
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

	// The startup menu takes over input until the relay pre-match flow is ready.
	LobbyOverlay {
		anchors.fill: parent
	}
}
