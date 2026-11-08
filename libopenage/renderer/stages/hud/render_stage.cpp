// Copyright 2023-2024 the openage authors. See copying.md for legal info.

#include "render_stage.h"

#include "renderer/camera/camera.h"
#include "renderer/opengl/context.h"
#include "renderer/render_pass.h"
#include "renderer/render_target.h"
#include "renderer/resources/assets/asset_manager.h"
#include "renderer/resources/shader_source.h"
#include "renderer/resources/texture_info.h"
#include "renderer/shader_program.h"
#include "renderer/stages/hud/object.h"
#include "renderer/stages/world/render_entity.h"
#include "renderer/texture.h"
#include "renderer/window.h"
#include "time/clock.h"


namespace openage::renderer::hud {

HudRenderStage::HudRenderStage(const std::shared_ptr<Window> &window,
                               const std::shared_ptr<renderer::Renderer> &renderer,
                               const std::shared_ptr<renderer::camera::Camera> &camera,
                               const util::Path &shaderdir,
                               const std::shared_ptr<renderer::resources::AssetManager> &asset_manager,
                               const std::shared_ptr<time::Clock> clock) :
	renderer{renderer},
	camera{camera},
	asset_manager{asset_manager},
	drag_object{nullptr},
	health_objects{},
	selected_entities{},
	health_bar_vertical_offset{0.0f},
	clock{clock} {
	renderer::opengl::GlContext::check_error();

	auto size = window->get_physical_size();
	this->initialize_render_pass(size[0], size[1], shaderdir);

	window->add_resize_callback([this](size_t width, size_t height, double scale) {
		auto size = Window::get_physical_size(width, height, scale);
		this->resize(size[0], size[1]);
	});

	log::log(INFO << "Created render stage 'HUD'");
}

std::shared_ptr<renderer::RenderPass> HudRenderStage::get_render_pass() {
	return this->render_pass;
}

void HudRenderStage::add_drag_entity(const std::shared_ptr<DragRenderEntity> entity) {
	std::unique_lock lock{this->mutex};

	auto hud_object = std::make_shared<HudDragObject>(this->asset_manager);
	hud_object->set_render_entity(entity);
	hud_object->set_camera(this->camera);
	this->drag_object = hud_object;
}

void HudRenderStage::remove_drag_entity() {
	std::unique_lock lock{this->mutex};

	this->drag_object = nullptr;
	this->render_pass->clear_renderables();
	for (auto &health_object : this->health_objects) {
		health_object->require_renderable();
	}
}

void HudRenderStage::add_health_entity(
	const std::shared_ptr<renderer::world::RenderEntity> &render_entity) {
	std::unique_lock lock{this->mutex};

	this->health_objects.push_back(std::make_shared<HudHealthObject>(render_entity));
}

void HudRenderStage::set_selected(
	const std::vector<gamestate::entity_id_t> &entity_ids,
	float health_bar_vertical_offset) {
	std::unique_lock lock{this->mutex};

	this->selected_entities.clear();
	this->selected_entities.insert(entity_ids.begin(), entity_ids.end());
	this->health_bar_vertical_offset = health_bar_vertical_offset;
}

void HudRenderStage::update(std::optional<time::time_t> presentation_time) {
	std::unique_lock lock{this->mutex};
	auto current_time = this->clock->get_real_time();
	for (auto &health_object : this->health_objects) {
		if (health_object->requires_renderable()) {
			auto background_uniforms = this->health_bar_shader->new_uniform_input(
				"obj_world_position",
				Eigen::Vector3f{0.0f, 0.0f, 0.0f},
				"bar_size",
				Eigen::Vector2f{0.0f, 0.0f},
				"bar_offset",
				Eigen::Vector2f{0.0f, 0.0f},
				"in_col",
				Eigen::Vector4f{0.0f, 0.0f, 0.0f, 0.0f});
			auto fill_uniforms = this->health_bar_shader->new_uniform_input(
				"obj_world_position",
				Eigen::Vector3f{0.0f, 0.0f, 0.0f},
				"bar_size",
				Eigen::Vector2f{0.0f, 0.0f},
				"bar_offset",
				Eigen::Vector2f{0.0f, 0.0f},
				"in_col",
				Eigen::Vector4f{0.0f, 0.0f, 0.0f, 0.0f});

			this->render_pass->add_renderables(
				Renderable{background_uniforms, this->health_bar_geometry, true, false});
			this->render_pass->add_renderables(
				Renderable{fill_uniforms, this->health_bar_geometry, true, false});
			health_object->set_uniforms(background_uniforms, fill_uniforms);
			health_object->clear_requires_renderable();
		}

		if (health_object->requires_renderable()) {
			continue;
		}
		auto selected = this->selected_entities.contains(health_object->get_id());
		health_object->update_uniforms(
			presentation_time.value_or(current_time),
			selected,
			this->health_bar_vertical_offset);
	}

	if (this->drag_object) {
		this->drag_object->fetch_updates(current_time);
		if (this->drag_object->requires_renderable()) {
			auto geometry = this->renderer->add_mesh_geometry(resources::MeshData::make_quad());
			auto transform_unifs = this->drag_select_shader->new_uniform_input(
				"in_col",
				Eigen::Vector4f{0.0f, 0.0f, 0.0f, 0.2f});

			Renderable display_obj{
				transform_unifs,
				geometry,
				true,
				true,
			};

			this->render_pass->add_renderables(std::move(display_obj));
			this->drag_object->clear_requires_renderable();

			this->drag_object->set_uniforms(transform_unifs);
			this->drag_object->set_geometry(geometry);
		}
		this->drag_object->update_uniforms(current_time);
		this->drag_object->update_geometry(current_time);
	}
}

void HudRenderStage::resize(size_t width, size_t height) {
	this->output_texture = renderer->add_texture(resources::Texture2dInfo(width, height, resources::pixel_format::rgba8));
	this->depth_texture = renderer->add_texture(resources::Texture2dInfo(width, height, resources::pixel_format::depth24));

	auto fbo = this->renderer->create_texture_target({this->output_texture, this->depth_texture});
	this->render_pass->set_target(fbo);
}

void HudRenderStage::initialize_render_pass(size_t width,
                                            size_t height,
                                            const util::Path &shaderdir) {
	// Drag select shader
	auto vert_shader_file = (shaderdir / "hud_drag_select.vert.glsl").open();
	auto vert_shader_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::vertex,
		vert_shader_file.read());
	vert_shader_file.close();

	auto frag_shader_file = (shaderdir / "hud_drag_select.frag.glsl").open();
	auto frag_shader_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::fragment,
		frag_shader_file.read());
	frag_shader_file.close();

	this->drag_select_shader = this->renderer->add_shader({vert_shader_src, frag_shader_src});

	// Health bar shader
	vert_shader_file = (shaderdir / "hud_health_bar.vert.glsl").open();
	vert_shader_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::vertex,
		vert_shader_file.read());
	vert_shader_file.close();

	frag_shader_file = (shaderdir / "hud_health_bar.frag.glsl").open();
	frag_shader_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::fragment,
		frag_shader_file.read());
	frag_shader_file.close();

	this->health_bar_shader = this->renderer->add_shader({vert_shader_src, frag_shader_src});
	this->health_bar_shader->bind_uniform_buffer("camera", this->camera->get_uniform_buffer());
	this->health_bar_geometry = this->renderer->add_mesh_geometry(resources::MeshData::make_quad());

	// Texture targets
	this->output_texture = renderer->add_texture(resources::Texture2dInfo(width, height, resources::pixel_format::rgba8));
	this->depth_texture = renderer->add_texture(resources::Texture2dInfo(width, height, resources::pixel_format::depth24));

	auto fbo = this->renderer->create_texture_target({this->output_texture, this->depth_texture});
	this->render_pass = this->renderer->add_render_pass({}, fbo);
}

} // namespace openage::renderer::hud
