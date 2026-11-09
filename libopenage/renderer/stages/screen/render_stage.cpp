// Copyright 2022-2024 the openage authors. See copying.md for legal info.

#include "render_stage.h"

#include <algorithm>

#include "error/error.h"
#include "renderer/opengl/context.h"
#include "renderer/render_pass.h"
#include "renderer/render_target.h"
#include "renderer/renderer.h"
#include "renderer/resources/mesh_data.h"
#include "renderer/resources/shader_source.h"
#include "renderer/resources/texture_info.h"
#include "renderer/shader_program.h"
#include "renderer/texture.h"
#include "renderer/uniform_input.h"
#include "renderer/window.h"
#include "util/path.h"

namespace openage::renderer::screen {

ScreenRenderStage::ScreenRenderStage(const std::shared_ptr<Window> & /* window */,
                                     const std::shared_ptr<renderer::Renderer> &renderer,
                                     const util::Path &shaderdir) :
	renderer{renderer},
	render_targets{},
	pass_outputs{} {
	renderer::opengl::GlContext::check_error();

	this->initialize_render_pass(shaderdir);

	log::log(INFO << "Created render stage 'Screen'");
}

std::shared_ptr<renderer::RenderPass> ScreenRenderStage::get_render_pass() {
	return this->render_pass;
}

std::array<std::shared_ptr<renderer::RenderPass>, 3> ScreenRenderStage::get_blur_passes() {
	if (not this->blur_passes[0]) {
		throw Error{MSG(err) << "Screen blur passes need scene render targets"};
	}
	return this->blur_passes;
}

void ScreenRenderStage::set_render_targets(const std::vector<std::shared_ptr<renderer::RenderTarget>> &targets) {
	this->render_targets = targets;
	this->update_render_pass();
}

void ScreenRenderStage::initialize_render_pass(const util::Path &shaderdir) {
	auto vert_shader_file = (shaderdir / "final.vert.glsl").open();
	auto vert_shader_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::vertex,
		vert_shader_file.read());
	vert_shader_file.close();

	auto frag_shader_file = (shaderdir / "final.frag.glsl").open();
	auto frag_shader_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::fragment,
		frag_shader_file.read());
	frag_shader_file.close();

	this->blur_quad = renderer->add_mesh_geometry(renderer::resources::MeshData::make_quad());
	this->display_shader = renderer->add_shader({vert_shader_src, frag_shader_src});
	auto down_scene_file = (shaderdir / "glass_downsample_scene.frag.glsl").open();
	auto down_scene_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::fragment,
		down_scene_file.read());
	down_scene_file.close();
	this->downsample_scene_shader = renderer->add_shader({vert_shader_src, down_scene_src});
	auto down_file = (shaderdir / "glass_downsample.frag.glsl").open();
	auto down_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::fragment,
		down_file.read());
	down_file.close();
	this->downsample_shader = renderer->add_shader({vert_shader_src, down_src});
	auto up_file = (shaderdir / "glass_upsample.frag.glsl").open();
	auto up_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::fragment,
		up_file.read());
	up_file.close();
	this->upsample_shader = renderer->add_shader({vert_shader_src, up_src});

	auto glass_vert_file = (shaderdir / "glass.vert.glsl").open();
	auto glass_vert_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::vertex,
		glass_vert_file.read());
	glass_vert_file.close();
	auto glass_frag_file = (shaderdir / "glass.frag.glsl").open();
	auto glass_frag_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::fragment,
		glass_frag_file.read());
	glass_frag_file.close();
	this->glass_shader = renderer->add_shader({glass_vert_src, glass_frag_src});

	// nothing is drawn until render targets are added
	this->render_pass = renderer->add_render_pass({}, renderer->get_display_target());
}

void ScreenRenderStage::set_glass_panels(const std::array<GlassPanel, 4> &panels) {
	this->glass_panels = panels;
	bool active = false;
	for (size_t i = 0; i < this->glass_inputs.size(); ++i) {
		active |= panels[i].rect.z() > 0.0f and panels[i].rect.w() > 0.0f;
		if (this->glass_inputs[i]) {
			this->glass_inputs[i]->update("panel_rect", panels[i].rect);
			this->glass_inputs[i]->update("panel_radius", panels[i].radius);
		}
	}
	if (active != this->blur_active) {
		this->blur_active = active;
		this->update_blur_renderables();
	}
}

void ScreenRenderStage::update_blur_renderables() {
	if (not this->blur_passes[0]) {
		return;
	}
	for (size_t i = 0; i < this->blur_passes.size(); ++i) {
		this->blur_passes[i]->clear_renderables();
		if (this->blur_active) {
			this->blur_passes[i]->add_renderables(renderer::Renderable{
				this->blur_inputs[i],
				this->blur_quad,
				false,
				false,
			});
		}
	}
}

void ScreenRenderStage::update_render_pass() {
	auto quad = renderer->add_mesh_geometry(renderer::resources::MeshData::make_quad());

	std::vector<renderer::Renderable> output_layers{};
	output_layers.reserve(this->render_targets.size() + this->glass_inputs.size());
	std::array<std::shared_ptr<renderer::Texture2d>, 4> glass_sources{};
	Eigen::Vector2f viewport_size{};
	if (this->render_targets.size() >= 5) {
		for (size_t i = 0; i < glass_sources.size(); ++i) {
			for (const auto &tex : this->render_targets[i]->get_texture_targets()) {
				if (tex->get_info().get_format() == renderer::resources::pixel_format::rgba8) {
					glass_sources[i] = tex;
					break;
				}
			}
			if (not glass_sources[i]) {
				throw Error{MSG(err) << "Screen glass blur needs four color render targets"};
			}
		}
		const auto source_size = glass_sources[0]->get_info().get_size();
		viewport_size = Eigen::Vector2f{
			static_cast<float>(source_size.first),
			static_cast<float>(source_size.second),
		};
		const auto half_width = std::max(1, source_size.first / 2);
		const auto half_height = std::max(1, source_size.second / 2);
		const auto quarter_width = std::max(1, half_width / 2);
		const auto quarter_height = std::max(1, half_height / 2);
		this->blur_textures[0] = this->renderer->add_texture(renderer::resources::Texture2dInfo(
			half_width, half_height, renderer::resources::pixel_format::rgba8));
		this->blur_textures[1] = this->renderer->add_texture(renderer::resources::Texture2dInfo(
			quarter_width, quarter_height, renderer::resources::pixel_format::rgba8));
		this->blur_textures[2] = this->renderer->add_texture(renderer::resources::Texture2dInfo(
			half_width, half_height, renderer::resources::pixel_format::rgba8));
		this->blur_inputs[0] = this->downsample_scene_shader->new_uniform_input(
			"skybox", glass_sources[0],
			"terrain", glass_sources[1],
			"world", glass_sources[2],
			"hud", glass_sources[3]);
		this->blur_inputs[1] = this->downsample_shader->new_uniform_input("tex", this->blur_textures[0]);
		this->blur_inputs[2] = this->upsample_shader->new_uniform_input("tex", this->blur_textures[1]);
		for (size_t i = 0; i < this->blur_passes.size(); ++i) {
			auto blur_target = this->renderer->create_texture_target({this->blur_textures[i]});
			if (this->blur_passes[i]) {
				this->blur_passes[i]->set_target(blur_target);
			}
			else {
				this->blur_passes[i] = this->renderer->add_render_pass({}, blur_target);
			}
		}
		this->update_blur_renderables();
	}
	for (size_t target_index = 0; target_index < this->render_targets.size(); ++target_index) {
		auto &target = this->render_targets[target_index];
		auto textures = target->get_texture_targets();
		auto texture_unif = this->display_shader->create_empty_input();

		// TODO: Dirty hack that only selects color textures
		// use this->pass_outputs in the future to assign output
		// textures we want to render
		for (const auto &tex : textures) {
			auto format = tex->get_info().get_format();
			if (format == renderer::resources::pixel_format::rgba8) {
				texture_unif->update("tex", tex);
				break;
			}
		}
		renderer::Renderable display_obj{
			texture_unif,
			quad,
			true,
			false,
		};
		output_layers.push_back(display_obj);
		if (target_index == 3 and this->render_targets.size() >= 5) {
			for (size_t panel_index = 0; panel_index < this->glass_inputs.size(); ++panel_index) {
				const auto &panel = this->glass_panels[panel_index];
				auto input = this->glass_shader->new_uniform_input(
					"blurred_scene", this->blur_textures[2],
					"viewport_size", viewport_size,
					"panel_rect", panel.rect,
					"panel_radius", panel.radius);
				this->glass_inputs[panel_index] = input;
				output_layers.push_back(renderer::Renderable{input, quad, true, false});
			}
		}
	}
	this->render_pass->clear_renderables();
	this->render_pass->add_renderables(std::move(output_layers));
}

} // namespace openage::renderer::screen
