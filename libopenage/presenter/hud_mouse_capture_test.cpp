// Copyright 2026 the openage authors. See copying.md for legal info.

#include "hud_mouse_capture.h"

#include "testing/testing.h"

namespace openage::presenter::tests {

void hud_mouse_capture() {
	using Action = HudMouseCapture::Action;
	HudMouseCapture capture{};
	TESTEQUALS(capture.is_capturing(), false);
	auto click = [&](QEvent::Type type, Qt::MouseButton button,
	                 bool panel, Action action) {
		return capture.process(type, button, panel, action);
	};

	// Ordinary world clicks remain usable.
	TESTEQUALS(click(QEvent::MouseButtonPress, Qt::LeftButton, false, Action::NONE).consumed, false);
	TESTEQUALS(click(QEvent::MouseButtonRelease, Qt::LeftButton, false, Action::NONE).consumed, false);

	// Build Tower / Attack-move activates exactly once after matching press/release.
	auto press = click(QEvent::MouseButtonPress, Qt::LeftButton, true, Action::PRIMARY);
	TESTEQUALS(press.consumed, true);
	TESTEQUALS(press.activated_action == Action::NONE, true);
	TESTEQUALS(capture.is_capturing(), true);
	auto release = click(QEvent::MouseButtonRelease, Qt::LeftButton, true, Action::PRIMARY);
	TESTEQUALS(release.consumed, true);
	TESTEQUALS(release.activated_action == Action::PRIMARY, true);
	TESTEQUALS(capture.is_capturing(), false);
	TESTEQUALS(click(QEvent::MouseButtonRelease, Qt::LeftButton, true, Action::PRIMARY).activated_action == Action::NONE, true);

	// Every Hunter action keeps its own identity across the mouse gesture.
	for (auto action : {Action::PATROL, Action::CANCEL, Action::AGGRESSIVE,
	                    Action::DEFENSIVE, Action::OBSERVE}) {
		click(QEvent::MouseButtonPress, Qt::LeftButton, true, action);
		TESTEQUALS(click(QEvent::MouseButtonRelease, Qt::LeftButton, true, action).activated_action == action, true);
	}

	// A button outside the outer panel's reported geometry remains clickable.
	// Regression: only the first button was reachable when the panel hit-test
	// did not include newly shown controls below it.
	click(QEvent::MouseButtonPress, Qt::LeftButton, false, Action::PATROL);
	auto outside_panel = click(QEvent::MouseButtonRelease, Qt::LeftButton, false, Action::PATROL);
	TESTEQUALS(outside_panel.consumed, true);
	TESTEQUALS(outside_panel.activated_action == Action::PATROL, true);
	click(QEvent::MouseButtonPress, Qt::LeftButton, false, Action::AGGRESSIVE);
	TESTEQUALS(click(QEvent::MouseButtonRelease, Qt::LeftButton, false, Action::AGGRESSIVE).activated_action == Action::AGGRESSIVE, true);
	click(QEvent::MouseButtonPress, Qt::LeftButton, false, Action::CANCEL);
	TESTEQUALS(click(QEvent::MouseButtonRelease, Qt::LeftButton, false, Action::CANCEL).activated_action == Action::CANCEL, true);

	// Releasing over a different stance never changes the selected units.
	click(QEvent::MouseButtonPress, Qt::LeftButton, true, Action::AGGRESSIVE);
	TESTEQUALS(click(QEvent::MouseButtonRelease, Qt::LeftButton, true, Action::OBSERVE).activated_action == Action::NONE, true);
	click(QEvent::MouseButtonPress, Qt::LeftButton, true, Action::PATROL);
	TESTEQUALS(click(QEvent::MouseButtonRelease, Qt::LeftButton, true, Action::PRIMARY).activated_action == Action::NONE, true);

	// Panel background and right-click cancel never activate a button.
	click(QEvent::MouseButtonPress, Qt::LeftButton, true, Action::NONE);
	TESTEQUALS(click(QEvent::MouseButtonRelease, Qt::LeftButton, true, Action::AGGRESSIVE).activated_action == Action::NONE, true);
	click(QEvent::MouseButtonPress, Qt::RightButton, true, Action::AGGRESSIVE);
	auto right = click(QEvent::MouseButtonRelease, Qt::RightButton, true, Action::AGGRESSIVE);
	TESTEQUALS(right.consumed, true);
	TESTEQUALS(right.activated_action == Action::NONE, true);

	// A gesture beginning on the HUD cannot leak an order into the world.
	click(QEvent::MouseButtonPress, Qt::LeftButton, true, Action::AGGRESSIVE);
	TESTEQUALS(click(QEvent::MouseButtonRelease, Qt::LeftButton, false, Action::NONE).consumed, true);
	TESTEQUALS(capture.is_capturing(), false);
	TESTEQUALS(click(QEvent::MouseButtonRelease, Qt::LeftButton, true, Action::AGGRESSIVE).activated_action == Action::NONE, true);

	// A release inside the HUD without a HUD press must not activate it.
	click(QEvent::MouseButtonPress, Qt::LeftButton, false, Action::NONE);
	auto world_to_panel = click(QEvent::MouseButtonRelease, Qt::LeftButton, true, Action::AGGRESSIVE);
	TESTEQUALS(world_to_panel.consumed, false);
	TESTEQUALS(world_to_panel.activated_action == Action::NONE, true);
	TESTEQUALS(click(QEvent::MouseButtonRelease, Qt::LeftButton, true, Action::NONE).consumed, true);
	click(QEvent::MouseButtonPress, Qt::RightButton, false, Action::NONE);
	TESTEQUALS(click(QEvent::MouseButtonRelease, Qt::RightButton, true, Action::NONE).consumed, true);

	// Independent left/right capture does not steal or repeat the action.
	click(QEvent::MouseButtonPress, Qt::LeftButton, true, Action::DEFENSIVE);
	click(QEvent::MouseButtonPress, Qt::RightButton, true, Action::NONE);
	TESTEQUALS(capture.is_capturing(), true);
	auto left = click(QEvent::MouseButtonRelease, Qt::LeftButton, true, Action::DEFENSIVE);
	TESTEQUALS(left.activated_action == Action::DEFENSIVE, true);
	TESTEQUALS(capture.is_capturing(), true);
	TESTEQUALS(click(QEvent::MouseButtonRelease, Qt::RightButton, false, Action::NONE).consumed, true);
	TESTEQUALS(capture.is_capturing(), false);
}

} // namespace openage::presenter::tests
