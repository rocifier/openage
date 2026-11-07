// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <QEvent>

namespace openage::presenter {

/**
 * Track mouse gestures begun over the FVD HUD.
 *
 * A panel click must not turn into a world selection/order if the pointer
 * leaves the panel before its button is released.
 */
class HudMouseCapture {
public:
	enum class Action {
		NONE,
		PRIMARY,
		PATROL,
		CANCEL,
		AGGRESSIVE,
		DEFENSIVE,
		OBSERVE,
		OPTIONS,
		TOGGLE_MUSIC,
		TOGGLE_EFFECTS,
		FLARE,
		TOGGLE_TOWER_LABELS,
		DELETE_FOUNDATIONS,
		GARRISON,
		UNGARRISON,
	};

	struct Result {
		bool consumed;
		Action activated_action;
	};

	/**
	 * Consume HUD gestures and activate a left-clicked action on release.
	 * Both press and release must occur within the same action button.
	 */
	Result process(QEvent::Type type, Qt::MouseButton button,
	               bool over_hud, Action hovered_action);

	/** Whether any HUD mouse gesture is currently active. */
	bool is_capturing() const;

private:
	unsigned int captured_buttons{0};
	Action pressed_action{Action::NONE};
};

} // namespace openage::presenter
