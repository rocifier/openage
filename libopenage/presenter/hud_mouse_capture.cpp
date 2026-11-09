// Copyright 2026 the openage authors. See copying.md for legal info.

#include "hud_mouse_capture.h"

namespace openage::presenter {

HudMouseCapture::Result HudMouseCapture::process(
	QEvent::Type type, Qt::MouseButton button,
	bool over_hud, Action hovered_action) {
	// Buttons can extend beyond the reported panel rectangle while Qt Quick
	// recalculates its Column height. They are still part of the HUD.
	const bool over_surface = over_hud or hovered_action != Action::NONE;
	const auto mask = static_cast<unsigned int>(button);
	const bool captured = (this->captured_buttons & mask) != 0;
	const bool world_left_release = type == QEvent::MouseButtonRelease
	                                and button == Qt::LeftButton
	                                and this->world_left_pressed;
	Action activated_action = Action::NONE;

	if (type == QEvent::MouseButtonPress or type == QEvent::MouseButtonDblClick) {
		if (over_surface) {
			this->captured_buttons |= mask;
		}
		if (button == Qt::LeftButton) {
			this->pressed_action = over_surface ? hovered_action : Action::NONE;
			this->world_left_pressed = not over_surface;
		}
	}
	else if (type == QEvent::MouseButtonRelease) {
		if (button == Qt::LeftButton) {
			if (captured and over_surface and this->pressed_action != Action::NONE
			    and hovered_action == this->pressed_action) {
				activated_action = hovered_action;
			}
			this->pressed_action = Action::NONE;
			this->world_left_pressed = false;
		}
		this->captured_buttons &= ~mask;
	}
	return {(over_surface and not world_left_release) or captured, activated_action};
}

bool HudMouseCapture::is_capturing() const {
	return this->captured_buttons != 0;
}

} // namespace openage::presenter
