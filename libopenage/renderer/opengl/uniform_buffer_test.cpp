// Copyright 2026 the openage authors. See copying.md for legal info.

#include "renderer/opengl/uniform_buffer.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <epoxy/gl.h>
#include <QWindow>

#include "renderer/gui/integration/public/gui_application_with_logger.h"
#include "renderer/opengl/uniform_input.h"
#include "renderer/renderer.h"
#include "renderer/resources/buffer_info.h"
#include "renderer/resources/shader_source.h"
#include "renderer/uniform_buffer.h"
#include "renderer/window.h"
#include "testing/testing.h"


namespace openage::renderer::opengl::tests {

void uniform_buffer_padding() {
	auto gui_app = std::make_shared<gui::GuiApplicationWithLogger>();
	window_settings settings{};
	settings.width = 64;
	settings.height = 64;
	settings.vsync = false;
	auto window = Window::create("openage uniform buffer test", settings);
	window->get_qt_window()->setVisible(false);
	auto graphics_renderer = window->make_renderer();

	const resources::UniformBufferInfo info{
		resources::ubo_layout_t::STD140,
		{{"head", resources::ubo_input_t::F32},
		 {"position", resources::ubo_input_t::V2F32},
		 {"tail", resources::ubo_input_t::F32}}};

	auto verify = [](const std::shared_ptr<UniformBuffer> &buffer) {
		auto gl_buffer = std::dynamic_pointer_cast<GlUniformBuffer>(buffer);
		auto input = buffer->create_empty_input();
		auto gl_input = std::dynamic_pointer_cast<GlUniformBufferInput>(input);
		TESTEQUALS(gl_buffer != nullptr, true);
		TESTEQUALS(gl_input != nullptr, true);
		for (const auto &uniform : gl_buffer->get_uniforms()) {
			TESTEQUALS(uniform.offset <= gl_input->update_data.size(), true);
			TESTEQUALS(uniform.size <= gl_input->update_data.size() - uniform.offset, true);
		}

		input->update("tail", 7.5f, "position", Eigen::Vector2f{2.25f, 3.5f}, "head", 1.25f);
		buffer->update_uniforms(input);
		gl_buffer->bind();
		GLint buffer_size{};
		glGetBufferParameteriv(GL_UNIFORM_BUFFER, GL_BUFFER_SIZE, &buffer_size);
		TESTEQUALS(buffer_size >= 20, true);
		std::vector<uint8_t> uploaded(static_cast<size_t>(buffer_size));
		glGetBufferSubData(GL_UNIFORM_BUFFER, 0,
		                   static_cast<GLsizeiptr>(uploaded.size()), uploaded.data());
		float head{};
		float position[2]{};
		float tail{};
		std::memcpy(&head, uploaded.data(), sizeof(head));
		std::memcpy(position, uploaded.data() + 8, sizeof(position));
		std::memcpy(&tail, uploaded.data() + 16, sizeof(tail));
		TESTEQUALS(head, 1.25f);
		TESTEQUALS(position[0], 2.25f);
		TESTEQUALS(position[1], 3.5f);
		TESTEQUALS(tail, 7.5f);
	};
	verify(graphics_renderer->add_uniform_buffer(info));

	auto shader = graphics_renderer->add_shader({
		{resources::shader_lang_t::glsl, resources::shader_stage_t::vertex,
		 std::string{R"(#version 330 core
layout(std140) uniform Padded {
    float head;
    vec2 position;
    float tail;
};
void main() {
    gl_Position = vec4(position + vec2(head, tail), 0.0, 1.0);
})"}},
		{resources::shader_lang_t::glsl, resources::shader_stage_t::fragment,
		 std::string{R"(#version 330 core
out vec4 color;
void main() {
    color = vec4(1.0);
})"}},
	});
	verify(graphics_renderer->add_uniform_buffer(shader, "Padded"));
	window->close();
}

} // namespace openage::renderer::opengl::tests
