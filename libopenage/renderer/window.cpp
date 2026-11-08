// Copyright 2015-2024 the openage authors. See copying.md for legal info.

#include "window.h"

#include <cmath>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QWheelEvent>
#include <QWindow>

#include "log/log.h"
#include "log/message.h"
#include "opengl/window.h"
#include "window_event_handler.h"


namespace openage::renderer {

std::shared_ptr<Window> Window::create(const std::string &title,
                                       window_settings settings) {
	// currently we only have a functional GL window
	// TODO: support other renderer windows
	//       and add some selection mechanism.
	return std::make_shared<opengl::GlWindow>(title, settings);
}


Window::Window(size_t width, size_t height) :
	size{width, height}, event_handler{std::make_shared<WindowEventHandler>()} {}


const util::Vector2s &Window::get_size() const {
	return this->size;
}

util::Vector2s Window::get_physical_size() const {
	return Window::get_physical_size(this->size[0], this->size[1], this->scale_dpr);
}

util::Vector2s Window::get_physical_size(size_t width, size_t height, double scale) {
	return {
		static_cast<size_t>(std::lround(static_cast<double>(width) * scale)),
		static_cast<size_t>(std::lround(static_cast<double>(height) * scale))};
}

double Window::get_scale() const {
	return this->scale_dpr;
}

double Window::get_refresh_rate() const {
	if (this->window and this->window->screen()) {
		const auto refresh_rate = this->window->screen()->refreshRate();
		if (refresh_rate >= 1.0 and refresh_rate <= 1000.0) {
			return refresh_rate;
		}
	}
	return 60.0;
}

bool Window::should_close() const {
	return this->should_be_closed;
}

void Window::add_mouse_button_callback(const mouse_button_cb_t &cb) {
	this->on_mouse_button.push_back(cb);
}

void Window::add_mouse_move_callback(const mouse_move_cb_t &cb) {
	this->on_mouse_move.push_back(cb);
}

void Window::add_mouse_wheel_callback(const mouse_wheel_cb_t &cb) {
	this->on_mouse_wheel.push_back(cb);
}

void Window::add_key_callback(const key_cb_t &cb) {
	this->on_key.push_back(cb);
}

void Window::add_resize_callback(const resize_cb_t &cb) {
	this->on_resize.push_back(cb);
}

const std::shared_ptr<QWindow> &Window::get_qt_window() const {
	return this->window;
}

void Window::process_events() {
	auto events = this->event_handler->pop_events();

	for (auto &&event : events) {
		switch (event->type()) {
		case QEvent::Resize: {
			auto ev = std::dynamic_pointer_cast<QResizeEvent>(event);
			size_t width = ev->size().width();
			size_t height = ev->size().height();
			log::log(MSG(dbg) << "Window resized to: " << width << "x" << height);

			this->size = {width, height};
			for (auto &cb : this->on_resize) {
				cb(width, height, this->scale_dpr);
			}
		} break;
		case QEvent::DevicePixelRatioChange: {
			this->scale_dpr = this->window->devicePixelRatio();
			log::log(MSG(dbg) << "Window device pixel ratio changed to: " << this->scale_dpr);

			for (auto &cb : this->on_resize) {
				cb(this->size[0], this->size[1], this->scale_dpr);
			}
		} break;
		case QEvent::Close: {
			this->should_be_closed = true;
		} break;

		case QEvent::KeyPress:
		case QEvent::KeyRelease: {
			auto const ev = std::dynamic_pointer_cast<QKeyEvent>(event);
			for (auto &cb : this->on_key) {
				cb(*ev);
			}
		} break;
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		case QEvent::MouseButtonDblClick: {
			auto const ev = std::dynamic_pointer_cast<QMouseEvent>(event);
			for (auto &cb : this->on_mouse_button) {
				cb(*ev);
			}
		} break;
		case QEvent::MouseMove: {
			auto const ev = std::dynamic_pointer_cast<QMouseEvent>(event);
			for (auto &cb : this->on_mouse_move) {
				cb(*ev);
			}
		} break;
		case QEvent::Wheel: {
			auto const ev = std::dynamic_pointer_cast<QWheelEvent>(event);
			for (auto &cb : this->on_mouse_wheel) {
				cb(*ev);
			}
		} break;
		default:
			break;
		}
	}
}

void Window::update() {
	this->process_events();
	this->present();
}

void Window::close() {
	this->window->close();
}


} // namespace openage::renderer
