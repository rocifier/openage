// Copyright 2022-2025 the openage authors. See copying.md for legal info.

#include "render_stage.h"

#include <array>
#include <cmath>
#include <unordered_map>

#include <eigen3/Eigen/Dense>

#include "renderer/camera/camera.h"
#include "renderer/camera/frustum_3d.h"
#include "renderer/definitions.h"
#include "renderer/opengl/context.h"
#include "renderer/render_pass.h"
#include "renderer/render_target.h"
#include "renderer/resources/assets/asset_manager.h"
#include "renderer/resources/shader_source.h"
#include "renderer/resources/texture_info.h"
#include "renderer/shader_program.h"
#include "renderer/stages/world/object.h"
#include "renderer/stages/world/render_entity.h"
#include "renderer/texture.h"
#include "renderer/window.h"
#include "time/clock.h"
#include "util/stall_profiler.h"


namespace openage::renderer::world {

namespace {

// Render ground markers before the unit sprites and outline/occluder layers.
constexpr int64_t SELECTION_RING_LAYER_PRIORITY = -1;
constexpr int64_t OCCLUDER_LAYER_PRIORITY = renderer::LAYER_PRIORITY_MAX - 2;
constexpr int64_t OUTLINE_LAYER_PRIORITY = renderer::LAYER_PRIORITY_MAX - 1;

/** Player numbers are 1-based. Keep this palette aligned with the
 * garrison flag colours in assets/fvd/qml/main.qml. Both the unit outlines
 * and the sprite's HSL hue replacement use these same RGB values. */
Eigen::Vector4f get_player_outline_color(uint64_t player_id) {
	const auto rgba = [](float r, float g, float b) {
		return Eigen::Vector4f{r / 255.0f, g / 255.0f, b / 255.0f, 0.82f};
	};
	static const std::array<Eigen::Vector4f, 8> colors{
		rgba(214, 93, 82),   // 1: Hunter red
		rgba(67, 139, 229),  // 2: Builder blue
		rgba(85, 182, 111),  // 3: Builder green
		rgba(232, 209, 106), // 4: Builder yellow
		rgba(87, 198, 209),  // 5: Builder cyan
		rgba(170, 117, 217), // 6: Builder purple
		rgba(187, 187, 199), // 7: Builder grey
		rgba(236, 155, 76),  // 8: Builder orange
	};
	if (player_id == 0) {
		return rgba(149, 171, 183);
	}
	return colors[(player_id - 1) % colors.size()];
}

} // namespace

bool WorldRenderStage::ENABLE_FRUSTUM_CULLING = false;

WorldRenderStage::WorldRenderStage(const std::shared_ptr<Window> &window,
                                   const std::shared_ptr<renderer::Renderer> &renderer,
                                   const std::shared_ptr<renderer::camera::Camera> &camera,
                                   const util::Path &shaderdir,
                                   const std::shared_ptr<renderer::resources::AssetManager> &asset_manager,
                                   const std::shared_ptr<time::Clock> clock) :
	renderer{renderer},
	camera{camera},
	asset_manager{asset_manager},
	render_objects{},
	clock{clock},
	default_geometry{this->renderer->add_mesh_geometry(WorldObject::get_mesh())} {
	renderer::opengl::GlContext::check_error();

	auto size = window->get_physical_size();
	this->initialize_render_pass(size[0], size[1], shaderdir);
	this->init_uniform_ids();

	window->add_resize_callback([this](size_t width, size_t height, double scale) {
		auto size = Window::get_physical_size(width, height, scale);
		this->resize(size[0], size[1]);
	});

	log::log(INFO << "Created render stage 'World'");
}

std::shared_ptr<renderer::RenderPass> WorldRenderStage::get_render_pass() {
	return this->render_pass;
}

void WorldRenderStage::add_render_entity(const std::shared_ptr<RenderEntity> entity) {
	std::unique_lock lock{this->mutex};

	auto world_object = std::make_shared<WorldObject>(this->asset_manager);
	world_object->set_render_entity(entity);
	this->render_objects.push_back(world_object);
}

void WorldRenderStage::clear_entities() {
	std::unique_lock lock{this->mutex};
	this->render_objects.clear();
	this->selection_ring_ids.clear();
	this->selection_footprint_width = 0.0f;
	this->render_pass->clear_renderables();
}

void WorldRenderStage::pick_entity(
	const coord::input &position,
	const std::function<void(std::optional<uint32_t>)> &callback,
	const std::function<bool(uint32_t)> &filter) {
	util::StallProfiler profile{"world.pick_entity"};
	util::StallProfiler lock_profile{"world.pick_entity.lock_wait"};
	std::shared_lock lock{this->mutex};
	lock_profile.finish();
	util::StallProfiler scan_profile{"world.pick_entity.scan"};
	std::optional<uint32_t> selected_id;
	std::optional<WorldObject::HitTestResult> selected_hit;
	std::optional<DrawKey> selected_key;
	for (const auto &object : this->render_objects) {
		if (not object->is_active() or not object->is_pickable() or object->is_concealed()
		    or (filter and not filter(object->get_id()))) {
			continue;
		}
		auto hit = object->hit_test(position, this->camera);
		if (not hit.has_value()) {
			continue;
		}
		const auto key = this->make_draw_key(*object);
		if (not selected_hit.has_value()
		    or hit->layer_position > selected_hit->layer_position
		    or (hit->layer_position == selected_hit->layer_position
		        and (hit->depth < selected_hit->depth
		             or (hit->depth == selected_hit->depth
		                 and draw_before(*selected_key, key))))) {
			selected_id = object->get_id();
			selected_hit = hit;
			selected_key = key;
		}
	}
	scan_profile.finish();
	lock.unlock();
	callback(selected_id);
}

void WorldRenderStage::update(std::optional<time::time_t> presentation_time) {
	util::StallProfiler lock_profile{"world.update.lock_wait"};
	std::unique_lock lock{this->mutex};
	lock_profile.finish();
	auto current_time = presentation_time.value_or(this->clock->get_real_time());
	auto &camera_frustum = this->camera->get_frustum_2d();
	bool sprites_added = false;
	for (auto &obj : this->render_objects) {
		obj->fetch_updates(current_time);
		obj->set_concealed(this->shown_ids and not this->shown_ids->contains(obj->get_id()));
		obj->set_dimmed(this->dimmed_ids.contains(obj->get_id()));
		if (not obj->is_active()) {
			obj->update_uniforms(current_time);
			continue;
		}

		if (WorldRenderStage::ENABLE_FRUSTUM_CULLING
		    and not obj->is_visible(camera_frustum, current_time)) {
			continue;
		}

		if (obj->is_changed()) {
			if (obj->requires_renderable()) {
				auto layer_positions = obj->get_layer_positions(current_time);
				static const Eigen::Matrix4f model_matrix = obj->get_model_matrix();

				std::vector<std::shared_ptr<renderer::UniformInput>> transform_unifs;
				std::vector<std::shared_ptr<renderer::UniformInput>> occlusion_unifs;
				for (auto layer_pos : layer_positions) {
					// Set uniforms that don't change or are not changed often
					// A single texture serves every player; ownership only
					// changes these per-renderable shader uniforms.
					const auto owner = obj->get_player_id();
					auto layer_unifs = this->display_shader->new_uniform_input(
						"model",
						model_matrix,
						"flip_x",
						false,
						"flip_y",
						false,
						"team_color_enabled",
						owner != 0,
						"team_color",
						get_player_outline_color(owner));

					Renderable display_obj{
						layer_unifs,
						this->default_geometry,
						true,
						true,
					};
					this->render_pass->add_renderables(std::move(display_obj), layer_pos);
					transform_unifs.push_back(layer_unifs);

					const auto mode = obj->get_occlusion_mode();
					if (mode == occlusion_mode::NONE) {
						continue;
					}
					auto shader = mode == occlusion_mode::OUTLINED
					                ? this->outline_shader
					                : this->occluder_shader;
					auto occlusion_layer_unifs = shader->new_uniform_input(
						"model",
						model_matrix,
						"flip_x",
						false,
						"flip_y",
						false);
					if (mode == occlusion_mode::OUTLINED) {
						occlusion_layer_unifs->update(
							"outline_color", get_player_outline_color(obj->get_player_id()));
						Renderable outline_obj{
							occlusion_layer_unifs,
							this->default_geometry,
							true,
							true,
							false,
							true,
							depth_compare_t::GREATER,
						};
						this->render_pass->add_renderables(
							std::move(outline_obj), OUTLINE_LAYER_PRIORITY);
					}
					else {
						Renderable occluder_obj{
							occlusion_layer_unifs,
							this->default_geometry,
							false,
							true,
							true,
							false,
							depth_compare_t::LESS_EQUAL,
						};
						this->render_pass->add_renderables(
							std::move(occluder_obj), OCCLUDER_LAYER_PRIORITY);
					}
					occlusion_unifs.push_back(occlusion_layer_unifs);
				}

				obj->clear_requires_renderable();
				sprites_added = true;

				// update remaining uniforms for the object
				obj->set_uniforms(std::move(transform_unifs), std::move(occlusion_unifs));
			}
		}
		obj->update_uniforms(current_time);
	}

	if (sprites_added) {
		this->sort_back_to_front();
	}

	// A transient layer containing only the selected, actually visible units.
	// Use the same interpolated world positions as their sprite quads. Replacing
	// this layer never invalidates the long-lived sprite renderables.
	std::vector<Renderable> rings;
	rings.reserve(this->selection_ring_ids.size());
	if (this->selection_footprint_width > 0.0f) {
		for (const auto &obj : this->render_objects) {
			if (not this->selection_ring_ids.contains(obj->get_id())
			    or not obj->is_active() or obj->is_concealed()
			    or obj->get_occlusion_mode() != occlusion_mode::OUTLINED) {
				continue;
			}
			// Scale to 150% of the Selectable rectangle's half-width so the
			// stroke clears the unit's feet (Builder=0.5, Horse=0.8 tiles).
			const float radius = this->selection_footprint_width * 0.75f;
			auto uniforms = this->selection_ring_shader->new_uniform_input(
				"unit_position", obj->get_rendered_world_position(),
				"footprint_radius", radius);
			rings.push_back(Renderable{uniforms, this->default_geometry,
			                            true, false, false, true});
		}
	}
	this->render_pass->set_layer_renderables(
		SELECTION_RING_LAYER_PRIORITY, std::move(rings));
}

WorldRenderStage::DrawKey WorldRenderStage::make_draw_key(WorldObject &object) {
	const Eigen::Matrix4f &view = this->camera->get_view_matrix();
	const Eigen::Vector3f position = object.get_rendered_world_position();
	const Eigen::Vector4f in_view = view * Eigen::Vector4f{position.x(), position.y(), position.z(), 1.0f};
	// Quantised so equal depths tie exactly (a strict weak order).
	return DrawKey{std::llround(-in_view.z() * 10000.0f),
	               std::llround(in_view.x() * 10000.0f),
	               object.get_id()};
}

void WorldRenderStage::sort_back_to_front() {
	// The depth buffer settles opaque pixels, but soft sprite edges and
	// shadows are blended and write no depth, so they still depend on draw
	// order. Draw far to near, then left to right, so the picture never
	// depends on spawn order. Only done when sprites are added: moving units
	// keep their slot, and depth testing still covers their opaque pixels.
	std::unordered_map<const UniformInput *, DrawKey> keys{};
	keys.reserve(this->render_objects.size());
	for (const auto &obj : this->render_objects) {
		const auto key = this->make_draw_key(*obj);
		for (const auto &uniform : obj->get_layer_uniforms()) {
			keys.emplace(uniform.get(), key);
		}
	}
	const auto key_of = [&keys](const Renderable &renderable) {
		auto key = keys.find(renderable.uniform.get());
		return key == keys.end() ? DrawKey{} : key->second;
	};
	// Layers without sprites (rings, occluders, outlines) all tie and keep
	// their order: the sort is stable.
	this->render_pass->sort([&](const Renderable &a, const Renderable &b) {
		return draw_before(key_of(a), key_of(b));
	});
}

bool WorldRenderStage::draw_before(const DrawKey &a, const DrawKey &b) {
	if (a.depth != b.depth) {
		return a.depth > b.depth;
	}
	if (a.across != b.across) {
		return a.across < b.across;
	}
	return a.id < b.id;
}

void WorldRenderStage::set_shown(std::optional<std::unordered_set<uint32_t>> ids) {
	std::unique_lock lock{this->mutex};
	this->shown_ids = std::move(ids);
}

void WorldRenderStage::set_selected_unit_rings(
	const std::vector<uint32_t> &ids, float footprint_width) {
	std::unique_lock lock{this->mutex};
	this->selection_ring_ids.clear();
	this->selection_ring_ids.insert(ids.begin(), ids.end());
	this->selection_footprint_width = footprint_width;
}

void WorldRenderStage::set_dimmed(std::unordered_set<uint32_t> ids) {
	std::unique_lock lock{this->mutex};
	this->dimmed_ids = std::move(ids);
}

void WorldRenderStage::resize(size_t width, size_t height) {
	this->output_texture = renderer->add_texture(resources::Texture2dInfo(width, height, resources::pixel_format::rgba8));
	this->depth_texture = renderer->add_texture(resources::Texture2dInfo(width, height, resources::pixel_format::depth24));

	auto fbo = this->renderer->create_texture_target({this->output_texture, this->depth_texture});
	this->render_pass->set_target(fbo);
}

void WorldRenderStage::initialize_render_pass(size_t width,
                                              size_t height,
                                              const util::Path &shaderdir) {
	auto vert_shader_file = (shaderdir / "world2d.vert.glsl").open();
	auto vert_shader_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::vertex,
		vert_shader_file.read());
	vert_shader_file.close();

	auto frag_shader_file = (shaderdir / "world2d.frag.glsl").open();
	auto frag_shader_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::fragment,
		frag_shader_file.read());
	frag_shader_file.close();

	auto occluder_frag_shader_file = (shaderdir / "world2d_occluder.frag.glsl").open();
	auto occluder_frag_shader_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::fragment,
		occluder_frag_shader_file.read());
	occluder_frag_shader_file.close();

	auto outline_frag_shader_file = (shaderdir / "world2d_outline.frag.glsl").open();
	auto outline_frag_shader_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::fragment,
		outline_frag_shader_file.read());
	outline_frag_shader_file.close();

	this->output_texture = renderer->add_texture(resources::Texture2dInfo(width, height, resources::pixel_format::rgba8));
	this->depth_texture = renderer->add_texture(resources::Texture2dInfo(width, height, resources::pixel_format::depth24));

	this->display_shader = this->renderer->add_shader({vert_shader_src, frag_shader_src});
	this->display_shader->bind_uniform_buffer("camera", this->camera->get_uniform_buffer());
	this->occluder_shader = this->renderer->add_shader({vert_shader_src, occluder_frag_shader_src});
	this->occluder_shader->bind_uniform_buffer("camera", this->camera->get_uniform_buffer());
	this->outline_shader = this->renderer->add_shader({vert_shader_src, outline_frag_shader_src});
	this->outline_shader->bind_uniform_buffer("camera", this->camera->get_uniform_buffer());

	// Selection circles are analytic ellipses in screen pixels; they share
	// the camera uniforms and quad geometry but need no sprite textures.
	auto ring_vert_file = (shaderdir / "world_selection_ring.vert.glsl").open();
	auto ring_vert_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::vertex,
		ring_vert_file.read());
	ring_vert_file.close();
	auto ring_frag_file = (shaderdir / "world_selection_ring.frag.glsl").open();
	auto ring_frag_src = renderer::resources::ShaderSource(
		resources::shader_lang_t::glsl,
		resources::shader_stage_t::fragment,
		ring_frag_file.read());
	ring_frag_file.close();
	this->selection_ring_shader = this->renderer->add_shader({ring_vert_src, ring_frag_src});
	this->selection_ring_shader->bind_uniform_buffer("camera", this->camera->get_uniform_buffer());

	auto fbo = this->renderer->create_texture_target({this->output_texture, this->depth_texture});
	this->render_pass = this->renderer->add_render_pass({}, fbo);
	this->render_pass->add_layer(SELECTION_RING_LAYER_PRIORITY, false);
	this->render_pass->add_layer(OCCLUDER_LAYER_PRIORITY, true);
	this->render_pass->add_layer(OUTLINE_LAYER_PRIORITY, false);
}

void WorldRenderStage::init_uniform_ids() {
	WorldObject::obj_world_position = this->display_shader->get_uniform_id("obj_world_position");
	WorldObject::flip_x = this->display_shader->get_uniform_id("flip_x");
	WorldObject::flip_y = this->display_shader->get_uniform_id("flip_y");
	WorldObject::tex = this->display_shader->get_uniform_id("tex");
	WorldObject::cbits = this->display_shader->get_uniform_id("cbits");
	WorldObject::tile_params = this->display_shader->get_uniform_id("tile_params");
	WorldObject::scale = this->display_shader->get_uniform_id("scale");
	WorldObject::subtex_size = this->display_shader->get_uniform_id("subtex_size");
	WorldObject::anchor_offset = this->display_shader->get_uniform_id("anchor_offset");
}

} // namespace openage::renderer::world
