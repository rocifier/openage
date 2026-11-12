// Copyright 2026 the openage authors. See copying.md for legal info.

#include "renderer/stages/world/render_stage.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

#include <QCoreApplication>
#include <QMouseEvent>
#include <QWindow>

#include "config.h"
#include "coord/phys.h"
#include "coord/scene.h"
#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "gamestate/component/internal/commands/build_tower.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/entity_factory.h"
#include "gamestate/event/process_command.h"
#include "gamestate/event/send_command.h"
#include "gamestate/event/wait.h"
#include "gamestate/fvd.h"
#include "gamestate/fvd_test_support.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "gamestate/terrain_factory.h"
#include "input/controller/game/binding_context.h"
#include "input/controller/game/controller.h"
#include "input/controller/hud/binding_context.h"
#include "input/controller/hud/controller.h"
#include "input/input_context.h"
#include "input/input_manager.h"
#include "network/command_bridge.h"
#include "network/construction_presentation.h"
#include "network/match_connection_state.h"
#include "renderer/camera/camera.h"
#include "renderer/gui/integration/public/gui_application_with_logger.h"
#include "renderer/render_factory.h"
#include "renderer/render_pass.h"
#include "renderer/render_target.h"
#include "renderer/renderer.h"
#include "renderer/resources/assets/asset_manager.h"
#include "renderer/resources/texture_data.h"
#include "renderer/stages/camera/manager.h"
#include "renderer/stages/terrain/render_entity.h"
#include "renderer/stages/terrain/render_stage.h"
#include "renderer/stages/world/render_entity.h"
#include "renderer/window.h"
#include "testing/testing.h"
#include "time/clock.h"
#include "time/time.h"
#include "time/time_loop.h"
#include "util/fslike/directory.h"
#include "util/path.h"


namespace openage::renderer::world::tests {

namespace {

constexpr size_t TEST_TARGET_WIDTH = 512;
constexpr size_t TEST_TARGET_HEIGHT = 768;
constexpr size_t SAMPLE_FRAME_X = 100;
constexpr size_t SAMPLE_FRAME_Y = 300;
constexpr size_t SAMPLE_SIZE = 10;
constexpr size_t FRONT_FRAME_X = 139;
constexpr size_t FRONT_FRAME_Y = 69;

const uint8_t *pixel(const resources::Texture2dData &image,
                     size_t x,
                     size_t y) {
	return image.get_data() + y * image.get_info().get_row_size() + x * 4;
}

} // namespace

void structure_shadow_depth() {
	auto root = util::Path{
		std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto gui_app = std::make_shared<gui::GuiApplicationWithLogger>();
	window_settings settings{};
	settings.width = TEST_TARGET_WIDTH;
	settings.height = TEST_TARGET_HEIGHT;
	settings.vsync = false;
	auto window = Window::create("openage structure shadow test", settings);
	window->get_qt_window()->setVisible(false);
	auto graphics_renderer = window->make_renderer();
	auto camera = std::make_shared<renderer::camera::Camera>(
		graphics_renderer, window->get_size());
	// Match the tower's 0.5 animation scale so one frame pixel maps to one
	// framebuffer pixel. The centered anchor puts its 512px frame at (0, 0).
	camera->set_zoom(0.5f);
	camera->look_at_coord(coord::scene3{0, 0, 0});
	auto camera_manager = std::make_shared<renderer::camera::CameraManager>(camera);
	camera_manager->update();
	auto assets = std::make_shared<resources::AssetManager>(
		graphics_renderer, root / "assets" / "fvd" / "graphics");
	auto clock = std::make_shared<time::Clock>();
	auto stage = std::make_shared<WorldRenderStage>(
		window, graphics_renderer, camera, root / "assets" / "shaders", assets, clock);

	auto render = [&](bool rear_enabled, bool front_enabled, bool team_coloured = false) {
		stage->clear_entities();
		auto add_tower = [&](uint32_t id, const coord::phys3 &position) {
			auto entity = std::make_shared<RenderEntity>();
			entity->update(id, position, "stone_tower.sprite", time::TIME_ZERO);
			if (team_coloured) {
				// Exercise the player-owned world2d shader uniform path.
				// Existing neutral art must still render with no palette copies.
				entity->set_occlusion_mode(occlusion_mode::OCCLUDER, 2);
			}
			stage->add_render_entity(entity);
		};
		if (rear_enabled) {
			add_tower(1, {0, 0, 0});
		}
		if (front_enabled) {
			// Increasing SE by one tile projects towards screen bottom-right.
			add_tower(2, {0, 1, 0});
		}
		stage->update(time::TIME_ZERO);
		graphics_renderer->render(stage->get_render_pass());
		return stage->get_render_pass()->get_target()->into_data();
	};

	auto rear = render(true, false);
	auto combined = render(true, true);
	auto owned = render(true, false, true);
	TESTEQUALS(owned.get_info().get_row_size(), rear.get_info().get_row_size());
	auto luminance_on_white = [](const uint8_t *value) {
		const auto red = static_cast<double>(value[0]) + 255.0 - value[3];
		const auto green = static_cast<double>(value[1]) + 255.0 - value[3];
		const auto blue = static_cast<double>(value[2]) + 255.0 - value[3];
		return 0.2126 * red + 0.7152 * green + 0.0722 * blue;
	};
	double rear_luminance = 0.0;
	double combined_luminance = 0.0;
	// Sample the front frame at (100, 300), translated by its one-tile
	// bottom-right displacement into the shared framebuffer.
	const auto sample_x = FRONT_FRAME_X + SAMPLE_FRAME_X;
	const auto sample_y = FRONT_FRAME_Y + SAMPLE_FRAME_Y;
	for (size_t y = sample_y; y < sample_y + SAMPLE_SIZE; ++y) {
		for (size_t x = sample_x; x < sample_x + SAMPLE_SIZE; ++x) {
			// Texture targets are read from OpenGL's bottom-left origin, while
			// frame offsets use the asset's top-left origin.
			const auto target_y = TEST_TARGET_HEIGHT - 1 - y;
			rear_luminance += luminance_on_white(pixel(rear, x, target_y));
			combined_luminance += luminance_on_white(pixel(combined, x, target_y));
		}
	}
	const auto sample_pixels = static_cast<double>(SAMPLE_SIZE * SAMPLE_SIZE);
	rear_luminance /= sample_pixels;
	combined_luminance /= sample_pixels;
	TESTNOTEQUALS(rear_luminance, 0.0);
	const auto darkening = (rear_luminance - combined_luminance) / rear_luminance;
	if (darkening > 0.20) {
		TESTFAILMSG(__FILE__ << ":" << __LINE__
		                     << ": front tower shadow darkened the rear sample by "
		                     << darkening * 100.0 << "%");
	}

	// Render the smaller Builder at native texture resolution so this test
	// measures team-colour coverage independently of its gameplay scale.
	camera->set_zoom(0.1375f);
	camera_manager->update();
	auto render_builder = [&](uint64_t owner) {
		stage->clear_entities();
		auto entity = std::make_shared<RenderEntity>();
		entity->update(1, coord::phys3{0, 0, 0}, "builder.sprite", time::TIME_ZERO);
		entity->set_occlusion_mode(occlusion_mode::NONE, owner);
		stage->add_render_entity(entity);
		stage->update(time::TIME_ZERO);
		graphics_renderer->render(stage->get_render_pass());
		return stage->get_render_pass()->get_target()->into_data();
	};
	auto blue_builder = render_builder(2);
	auto green_builder = render_builder(3);
	size_t recoloured_pixels = 0;
	for (size_t y = 0; y < TEST_TARGET_HEIGHT; ++y) {
		for (size_t x = 0; x < TEST_TARGET_WIDTH; ++x) {
			const auto blue = pixel(blue_builder, x, y);
			const auto green = pixel(green_builder, x, y);
			if (blue[0] == green[0]
			    and blue[1] == green[1]
			    and blue[2] == green[2]) {
				continue;
			}
			++recoloured_pixels;
		}
	}
	TESTEQUALS(recoloured_pixels > 800, true);
	window->close();
}

// Render the shader itself, not a CPU approximation of its blend. Existing
// per-tile fog data must produce dark interiors and soft tile-edge transitions.
void terrain_fog_soft_edges() {
	auto root = util::Path{
		std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto gui_app = std::make_shared<gui::GuiApplicationWithLogger>();
	window_settings settings{};
	settings.width = TEST_TARGET_WIDTH;
	settings.height = TEST_TARGET_HEIGHT;
	settings.vsync = false;
	auto window = Window::create("openage softened terrain fog test", settings);
	window->get_qt_window()->setVisible(false);
	auto graphics_renderer = window->make_renderer();
	auto camera = std::make_shared<renderer::camera::Camera>(
		graphics_renderer, window->get_size());
	camera->set_zoom(1.0f);
	camera->look_at_coord(coord::scene3{1.5, 1.5, 0});
	auto camera_manager = std::make_shared<renderer::camera::CameraManager>(camera);
	camera_manager->update();
	auto assets = std::make_shared<resources::AssetManager>(
		graphics_renderer, root / "assets" / "fvd" / "graphics");
	auto clock = std::make_shared<time::Clock>();
	auto stage = std::make_shared<terrain::TerrainRenderStage>(
		window, graphics_renderer, camera, root / "assets" / "shaders", assets, clock);

	terrain::RenderEntity::tiles_t tiles{};
	for (int i = 0; i < 9; ++i) {
		tiles.emplace_back(terrain::RenderEntity::terrain_elevation_t{0}, "grass.terrain");
	}
	auto entity = std::make_shared<terrain::RenderEntity>();
	entity->update(util::Vector2s{3, 3}, tiles, time::TIME_ZERO);
	stage->add_render_entity(entity, util::Vector2s{3, 3}, coord::scene2_delta{0, 0});

	auto render = [&](const std::vector<uint8_t> &fog_levels, float edge_width) {
		stage->set_fog(3, fog_levels);
		stage->set_fog_edge_width(edge_width);
		stage->update();
		graphics_renderer->render(stage->get_render_pass());
		return stage->get_render_pass()->get_target()->into_data();
	};
	const std::vector<uint8_t> all_visible(9, 255);
	// Fog rows are NE (height), columns SE (width): first SE column visible.
	const std::vector<uint8_t> revealed{
		255, 0, 0,
		255, 0, 0,
		255, 0, 0,
	};
	auto full = render(all_visible, 0.55f);
	auto soft = render(revealed, 0.55f);
	auto hard = render(revealed, 0.0f);
	auto sample = [camera](const resources::Texture2dData &image,
	                       float se, float ne) {
		const Eigen::Vector3f world = coord::scene3{ne, se, 0}.to_world_space();
		const auto matrix = camera->get_projection_matrix() * camera->get_view_matrix();
		const Eigen::Vector4f projected = matrix * Eigen::Vector4f{
			world.x(), world.y(), world.z(), 1.0f};
		const auto x = static_cast<size_t>(std::clamp(
			std::lround((projected.x() / projected.w() * 0.5 + 0.5) * TEST_TARGET_WIDTH),
			0l, static_cast<long>(TEST_TARGET_WIDTH - 1)));
		const auto y = static_cast<size_t>(std::clamp(
			std::lround((projected.y() / projected.w() * 0.5 + 0.5) * TEST_TARGET_HEIGHT),
			0l, static_cast<long>(TEST_TARGET_HEIGHT - 1)));
		const uint8_t *rgba = pixel(image, x, y);
		return 0.2126 * rgba[0] + 0.7152 * rgba[1] + 0.0722 * rgba[2];
	};
	const double full_center = sample(full, 0.5f, 1.5f);
	const double full_edge = sample(full, 1.0f, 1.5f);
	const double full_hidden = sample(full, 1.5f, 1.5f);
	TESTEQUALS(full_center > 0.0, true);
	TESTEQUALS(full_edge > 0.0, true);
	TESTEQUALS(full_hidden > 0.0, true);
	// Fog softness doesn't wash out well inside visible and unseen tiles.
	TESTEQUALS(sample(soft, 0.5f, 1.5f) > full_center * 0.95, true);
	TESTEQUALS(sample(soft, 1.5f, 1.5f) < full_hidden * 0.05, true);
	// Between different tile centres, the new shader renders a partial fade.
	TESTEQUALS(sample(soft, 1.0f, 1.5f) > full_edge * 0.20, true);
	TESTEQUALS(sample(soft, 1.0f, 1.5f) < full_edge * 0.85, true);
	// A width of zero restores the earlier hard tile boundary.
	TESTEQUALS(sample(hard, 0.88f, 1.5f) > full_edge * 0.95, true);
	TESTEQUALS(sample(hard, 1.12f, 1.5f) < full_edge * 0.05, true);
	window->close();
}

// Selection rings must appear under selected movable units and disappear
// immediately on deselection or when the unit is hidden by fog.
void selection_ground_rings() {
	auto root = util::Path{
		std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto gui_app = std::make_shared<gui::GuiApplicationWithLogger>();
	window_settings settings{};
	settings.width = TEST_TARGET_WIDTH;
	settings.height = TEST_TARGET_HEIGHT;
	settings.vsync = false;
	auto window = Window::create("openage selection ring test", settings);
	window->get_qt_window()->setVisible(false);
	auto graphics_renderer = window->make_renderer();
	auto camera = std::make_shared<renderer::camera::Camera>(
		graphics_renderer, window->get_size());
	camera->set_zoom(0.5f);
	camera->look_at_coord(coord::scene3{0, 0, 0});
	auto camera_manager = std::make_shared<renderer::camera::CameraManager>(camera);
	camera_manager->update();
	auto assets = std::make_shared<resources::AssetManager>(
		graphics_renderer, root / "assets" / "fvd" / "graphics");
	auto clock = std::make_shared<time::Clock>();
	auto stage = std::make_shared<WorldRenderStage>(
		window, graphics_renderer, camera, root / "assets" / "shaders", assets, clock);

	auto builder = std::make_shared<RenderEntity>();
	builder->update(17, coord::phys3{0, 0, 0}, "builder.sprite", time::TIME_ZERO);
	builder->set_occlusion_mode(occlusion_mode::OUTLINED, 2);
	stage->add_render_entity(builder);
	auto draw = [&] {
		stage->update(time::TIME_ZERO);
		graphics_renderer->render(stage->get_render_pass());
		return stage->get_render_pass()->get_target()->into_data();
	};
	stage->set_selected_unit_rings({}, 0.0f);
	auto plain = draw();
	stage->set_selected_unit_rings({17}, 0.5f);
	auto ringed = draw();
	size_t ring_pixels = 0;
	for (size_t y = 0; y < TEST_TARGET_HEIGHT; ++y) {
		for (size_t x = 0; x < TEST_TARGET_WIDTH; ++x) {
			const auto *a = pixel(plain, x, y);
			const auto *b = pixel(ringed, x, y);
			if (a[0] != b[0] or a[1] != b[1]
			    or a[2] != b[2] or a[3] != b[3]) {
				++ring_pixels;
			}
		}
	}
	TESTEQUALS(ring_pixels > size_t{40}, true);

	// No stale renderable persists after the selection layer is replaced.
	stage->set_selected_unit_rings({}, 0.0f);
	auto deselected = draw();
	for (size_t y = 0; y < TEST_TARGET_HEIGHT; y += 7) {
		for (size_t x = 0; x < TEST_TARGET_WIDTH; x += 7) {
			const auto *a = pixel(plain, x, y);
			const auto *b = pixel(deselected, x, y);
			TESTEQUALS(a[0] == b[0] and a[1] == b[1]
			           and a[2] == b[2] and a[3] == b[3], true);
		}
	}

	stage->set_selected_unit_rings({17}, 0.5f);
	stage->set_shown(std::unordered_set<uint32_t>{});
	auto hidden = draw();
	size_t shown_pixels = 0;
	for (size_t y = 0; y < TEST_TARGET_HEIGHT; ++y) {
		for (size_t x = 0; x < TEST_TARGET_WIDTH; ++x) {
			if (pixel(hidden, x, y)[3] > 5) ++shown_pixels;
		}
	}
	TESTEQUALS(shown_pixels == size_t{0}, true);
	window->close();
}

void foundation_picking() {
	auto root = util::Path{
		std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto gui_app = std::make_shared<gui::GuiApplicationWithLogger>();
	window_settings settings{};
	settings.width = TEST_TARGET_WIDTH;
	settings.height = TEST_TARGET_HEIGHT;
	settings.vsync = false;
	auto window = Window::create("openage foundation picking test", settings);
	window->get_qt_window()->setVisible(false);
	auto graphics_renderer = window->make_renderer();
	auto camera = std::make_shared<renderer::camera::Camera>(graphics_renderer, window->get_size());
	camera->set_zoom(0.32f);
	camera->look_at_coord(coord::scene3{0, 0, 0});
	auto camera_manager = std::make_shared<renderer::camera::CameraManager>(camera);
	camera_manager->update();
	auto assets = std::make_shared<resources::AssetManager>(
		graphics_renderer, root / "assets" / "fvd" / "graphics");
	auto clock = std::make_shared<time::Clock>();
	auto stage = std::make_shared<WorldRenderStage>(
		window, graphics_renderer, camera, root / "assets" / "shaders", assets, clock);
	auto entity = std::make_shared<RenderEntity>();
	entity->update(17, coord::phys3{0, 0, 0}, "foundation.sprite", time::TIME_ZERO);
	stage->add_render_entity(entity);
	stage->update(time::TIME_ZERO);
	graphics_renderer->render(stage->get_render_pass());
	const auto image = stage->get_render_pass()->get_target()->into_data();

	size_t visible = 0;
	size_t picked = 0;
	std::optional<coord::input> visible_pixel;
	for (size_t y = 0; y < TEST_TARGET_HEIGHT; y += 3) {
		for (size_t x = 0; x < TEST_TARGET_WIDTH; x += 3) {
			if (pixel(image, x, y)[3] < 250) continue;
			++visible;
			const coord::input point{
				static_cast<coord::pixel_t>(x),
				static_cast<coord::pixel_t>(TEST_TARGET_HEIGHT - 1 - y)};
			stage->pick_entity(point, [&](std::optional<uint32_t> id) {
				if (id == uint32_t{17}) {
					++picked;
					visible_pixel = point;
				}
			});
		}
	}
	TESTEQUALS(visible > size_t{1000}, true);
	if (picked * 100 < visible * 99) {
		TESTFAILMSG("Visible foundation pixels picked: " << picked << " of " << visible);
	}
	TESTEQUALS(visible_pixel.has_value(), true);
	std::optional<uint32_t> filtered;
	stage->pick_entity(*visible_pixel, [&](std::optional<uint32_t> id) { filtered = id; },
	                   [](uint32_t id) { return id == 17; });
	TESTEQUALS(filtered, std::optional<uint32_t>{17});
	stage->pick_entity(*visible_pixel, [&](std::optional<uint32_t> id) { filtered = id; },
	                   [](uint32_t id) { return id != 17; });
	TESTEQUALS(filtered.has_value(), false);
	stage->set_shown(std::unordered_set<uint32_t>{});
	stage->update(time::TIME_ZERO);
	stage->pick_entity(*visible_pixel, [&](std::optional<uint32_t> id) { filtered = id; },
	                   [](uint32_t id) { return id == 17; });
	TESTEQUALS(filtered.has_value(), false);
	window->close();
}

void foundation_mouse_input() {
	auto root = util::Path{
		std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto gui_app = std::make_shared<gui::GuiApplicationWithLogger>();
	window_settings settings{};
	settings.width = TEST_TARGET_WIDTH;
	settings.height = TEST_TARGET_HEIGHT;
	settings.vsync = false;
	auto window = Window::create("openage foundation mouse input test", settings);
	window->get_qt_window()->setVisible(false);
	auto graphics_renderer = window->make_renderer();
	auto camera = std::make_shared<renderer::camera::Camera>(graphics_renderer, window->get_size());
	camera->set_zoom(0.32f);
	camera->look_at_coord(coord::tile{57, 66}.to_phys3_center().to_scene3());
	auto camera_manager = std::make_shared<renderer::camera::CameraManager>(camera);
	camera_manager->update();
	auto assets = std::make_shared<resources::AssetManager>(
		graphics_renderer, root / "assets" / "converted");
	auto clock = std::make_shared<time::TimeLoop>();
	auto stage = std::make_shared<WorldRenderStage>(
		window, graphics_renderer, camera, root / "assets" / "shaders",
		assets, clock->get_clock());
	auto terrain = std::make_shared<terrain::TerrainRenderStage>(
		window, graphics_renderer, camera, root / "assets" / "shaders",
		assets, clock->get_clock());
	auto cvars = std::make_shared<cvar::CVarManager>(root);
	auto simulation = std::make_shared<gamestate::GameSimulation>(root, cvars, clock);
	simulation->set_modpacks({"fvd"});
	simulation->initialize();
	auto initial = simulation->get_fvd_presentation_state();
	auto builder = std::find_if(initial->entities.begin(), initial->entities.end(),
	                            [](const auto &entity) {
		                            return entity.owner_id == 2
		                                and entity.definition == "fvd.data.entities.Builder";
	                            });
	TESTEQUALS(builder != initial->entities.end(), true);
	const auto builder_id = builder->entity_id;
	auto state = simulation->get_game()->get_state();
	auto fvd = state->get_fvd();
	gamestate::tests::use_centre_test_layout(state);
	simulation->attach_renderer(std::make_shared<renderer::RenderFactory>(terrain, stage));

	auto foundation_at = [&](const coord::tile &tile) {
		for (const auto &[id, entity] : state->get_game_entities()) {
			if (entity->get_definition() != "fvd.data.entities.Foundation") continue;
			auto position = std::dynamic_pointer_cast<gamestate::component::Position>(
				entity->get_component(gamestate::component::component_t::POSITION));
			if (position->get_positions().get(time::TIME_ZERO).to_tile() == tile) return id;
		}
		throw testing::TestError{MSG(err) << "Missing foundation at " << tile};
	};
	const std::vector<gamestate::entity_id_t> selected{builder_id};
	TESTEQUALS(fvd->queue_build(state, 2, selected, {61, 66, 0}, time::TIME_ZERO), true);
	simulation->get_event_loop()->reach_time(time::TIME_ZERO, state);
	TESTEQUALS(fvd->cancel_build(state, builder_id, time::TIME_ZERO), true);
	const auto recreated = foundation_at({61, 66});
	TESTEQUALS(fvd->queue_build(state, 2, selected, {57, 66, 0}, time::TIME_ZERO), true);
	TESTEQUALS(fvd->queue_build(state, 2, selected, {54, 66, 0}, time::TIME_ZERO), true);
	TESTEQUALS(fvd->queue_build(state, 2, selected, {51, 66, 0}, time::TIME_ZERO), true);
	const auto planned = foundation_at({57, 66});
	const auto overlapping = foundation_at({54, 66});
	const auto waiting = foundation_at({51, 66});
	const std::vector<coord::tile> crowded_sites{{36, 66}, {37, 66}, {38, 66}, {39, 66}};
	std::vector<gamestate::entity_id_t> crowded_foundations;
	for (const auto &site : crowded_sites) {
		TESTEQUALS(fvd->queue_build(state, 2, selected, site.to_phys3_center(), time::TIME_ZERO), true);
		crowded_foundations.push_back(foundation_at(site));
	}
	TESTEQUALS(fvd->cancel_build(state, builder_id, time::TIME_ZERO), true);
	TESTEQUALS(fvd->queue_build(state, 2, selected, {48, 66, 0}, time::TIME_ZERO), true);
	const auto active_order = fvd->get_build_order(builder_id)->get_id();

	// Publish a completed local snapshot without depending on thread timing.
	network::AuthenticatedCommand barrier{};
	barrier.identity.player_id = 2;
	barrier.intent.kind = network::CommandKind::DELETE_FOUNDATIONS;
	TESTEQUALS(simulation->submit_network_command(
		barrier, [&](bool accepted) {
			TESTEQUALS(accepted, false);
			simulation->stop();
		}), true);
	simulation->run();
	const auto world = simulation->get_fvd_presentation_state();
	TESTEQUALS(world != nullptr, true);
	TESTEQUALS(std::any_of(world->entities.begin(), world->entities.end(),
	                       [planned](const auto &entity) {
		                       return entity.entity_id == planned and entity.owner_id == 2
		                           and entity.definition == "fvd.data.entities.Foundation";
	                       }), true);
	TESTEQUALS(std::any_of(world->entities.begin(), world->entities.end(),
	                       [recreated](const auto &entity) {
		                       return entity.entity_id == recreated and entity.owner_id == 2
		                           and entity.definition == "fvd.data.entities.Foundation";
	                       }), true);

	stage->update(time::TIME_ZERO);
	auto pixel_for = [&](gamestate::entity_id_t id) {
		for (size_t y = 0; y < TEST_TARGET_HEIGHT; y += 2) {
			for (size_t x = 0; x < TEST_TARGET_WIDTH; x += 2) {
				coord::input point{static_cast<coord::pixel_t>(x),
				                   static_cast<coord::pixel_t>(y)};
				std::optional<uint32_t> picked;
				stage->pick_entity(point, [&](std::optional<uint32_t> value) {
					picked = value;
				});
				if (picked == id) return point;
			}
		}
		throw testing::TestError{MSG(err) << "Entity is not pickable: " << id};
	};

	auto controller = std::make_shared<input::game::Controller>(
		std::unordered_set<size_t>{1, 2}, 2);
	auto bindings = std::make_shared<input::game::BindingContext>();
	input::game::entity_picker_t picker = [stage](
		const coord::input &point, const input::game::entity_pick_callback_t &callback,
		const input::game::entity_pick_filter_t &filter) {
		std::function<bool(uint32_t)> render_filter;
		if (filter) {
			render_filter = [filter](uint32_t id) { return filter(id); };
		}
		stage->pick_entity(point, [&](std::optional<uint32_t> id) {
			callback(id);
		}, render_filter);
	};
	input::game::setup_defaults(bindings, controller, clock, simulation, camera, picker);
	input::InputManager manager;
	auto context = manager.get_global_context();
	input::setup_defaults(context);
	context->set_game_bindings(bindings);
	context->set_hud_bindings(std::make_shared<input::hud::BindingContext>());
	manager.set_game_controller(controller);
	manager.set_hud_controller(std::make_shared<input::hud::Controller>());
	auto click = [&](input::InputManager &input, const coord::input &point,
	                 Qt::KeyboardModifiers modifiers,
	                 bool process_events = true,
	                 QEvent::Type press_type = QEvent::MouseButtonPress,
	                 Qt::MouseButton button = Qt::LeftButton) {
		const QPointF position{static_cast<qreal>(point.x), static_cast<qreal>(point.y)};
		QMouseEvent press{press_type, position, position,
		                  button, button, modifiers};
		QMouseEvent release{QEvent::MouseButtonRelease, position, position,
		                    button, Qt::NoButton, modifiers};
		TESTEQUALS(input.process(press), true);
		TESTEQUALS(input.process(release), true);
		if (process_events) {
			simulation->get_event_loop()->reach_time(time::TIME_ZERO, state);
		}
	};

	const auto planned_pixel = pixel_for(planned);
	click(manager, planned_pixel, Qt::NoModifier);
	TESTEQUALS(controller->get_selected(), (std::vector<gamestate::entity_id_t>{planned}));
	TESTEQUALS(controller->get_selection_details().kind == input::game::SelectionKind::FOUNDATION, true);
	controller->set_selected(selected, input::game::SelectionKind::BUILDER);
	click(manager, planned_pixel, Qt::ShiftModifier);
	TESTEQUALS(fvd->get_queued_build_count(builder_id), size_t{1});
	TESTEQUALS(controller->is_tower_placement_active(), false);
	TESTEQUALS(fvd->get_build_order(builder_id)->get_id(), active_order);
	TESTEQUALS(foundation_at({57, 66}), planned);

	camera->look_at_coord(coord::tile{61, 66}.to_phys3_center().to_scene3());
	camera_manager->update();
	stage->update(time::TIME_ZERO);
	click(manager, pixel_for(recreated), Qt::ShiftModifier);
	TESTEQUALS(fvd->get_queued_build_count(builder_id), size_t{2});
	TESTEQUALS(fvd->get_build_order(builder_id)->get_id(), active_order);
	TESTEQUALS(foundation_at({61, 66}), recreated);

	controller->cancel_tower_placement();
	click(manager, pixel_for(recreated), Qt::NoModifier);
	camera->look_at_coord(coord::tile{57, 66}.to_phys3_center().to_scene3());
	camera_manager->update();
	stage->update(time::TIME_ZERO);
	click(manager, pixel_for(planned), Qt::ShiftModifier);
	auto grouped = std::vector<gamestate::entity_id_t>{planned, recreated};
	std::sort(grouped.begin(), grouped.end());
	TESTEQUALS(controller->get_selected(), grouped);
	TESTEQUALS(controller->get_selection_details().kind == input::game::SelectionKind::GROUP_FOUNDATIONS, true);
	controller->set_selected(selected, input::game::SelectionKind::BUILDER);

	// A Builder can stand on a walkable foundation. Click where its sprite
	// overlaps the foundation but projects outside the foundation's tile.
	auto builder_position = std::dynamic_pointer_cast<gamestate::component::Position>(
		state->get_game_entity(builder_id)->get_component(gamestate::component::component_t::POSITION));
	builder_position->set_position(time::TIME_ZERO, coord::phys3{54.75, 66.75, 0});
	state->get_game_entity(builder_id)->render_update(
		time::TIME_ZERO, "fvd/data/../../../fvd/graphics/builder.sprite");
	camera->look_at_coord(coord::tile{54, 66}.to_phys3_center().to_scene3());
	camera_manager->update();
	stage->set_shown(std::unordered_set<uint32_t>{static_cast<uint32_t>(overlapping)});
	stage->update(time::TIME_ZERO);
	std::vector<coord::input> foundation_pixels;
	for (size_t y = 0; y < TEST_TARGET_HEIGHT; y += 2) {
		for (size_t x = 0; x < TEST_TARGET_WIDTH; x += 2) {
			coord::input point{static_cast<coord::pixel_t>(x),
			                   static_cast<coord::pixel_t>(y)};
			std::optional<uint32_t> picked;
			stage->pick_entity(point, [&](std::optional<uint32_t> id) { picked = id; });
			if (picked == overlapping) foundation_pixels.push_back(point);
		}
	}
	stage->set_shown(std::unordered_set<uint32_t>{
		static_cast<uint32_t>(overlapping), static_cast<uint32_t>(builder_id)});
	stage->update(time::TIME_ZERO);
	std::optional<coord::input> overlap;
	size_t obscured = 0;
	for (const auto &point : foundation_pixels) {
		std::optional<uint32_t> picked;
		stage->pick_entity(point, [&](std::optional<uint32_t> id) { picked = id; });
		if (picked == builder_id) {
			++obscured;
			if (point.to_phys3(camera).to_tile() != coord::tile{54, 66}) {
				overlap = point;
				break;
			}
		}
	}
	if (not overlap) {
		TESTFAILMSG("Foundation pixels: " << foundation_pixels.size()
		            << ", obscured by Builder: " << obscured);
	}
	controller->cancel_tower_placement();
	click(manager, pixel_for(overlapping), Qt::NoModifier);
	TESTEQUALS(controller->get_selected(), (std::vector<gamestate::entity_id_t>{overlapping}));
	click(manager, *overlap, Qt::NoModifier);
	TESTEQUALS(controller->get_selected(), selected);
	click(manager, *overlap, Qt::ShiftModifier);
	TESTEQUALS(fvd->is_tile_reserved({54, 66}), true);
	TESTEQUALS(foundation_at({54, 66}), overlapping);
	TESTEQUALS(fvd->get_queued_build_count(builder_id), size_t{3});

	// A deferred Shift-drag for a non-Builder selection still uses the
	// selection rectangle and camera from the original gesture.
	controller->set_selected({});
	camera->look_at_coord(builder_position->get_positions().get(time::TIME_ZERO).to_scene3());
	camera_manager->update();
	stage->update(time::TIME_ZERO);
	const auto pending_drag = controller->begin_selection_update();
	const QPointF drag_start{160, 288};
	const QPointF drag_end{352, 480};
	QMouseEvent drag_press{QEvent::MouseButtonPress, drag_start, drag_start,
	                       Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier};
	QMouseEvent drag_release{QEvent::MouseButtonRelease, drag_end, drag_end,
	                         Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier};
	TESTEQUALS(manager.process(drag_press), true);
	TESTEQUALS(manager.process(drag_release), true);
	camera->look_at_coord(coord::tile{20, 20}.to_phys3_center().to_scene3());
	camera_manager->update();
	stage->update(time::TIME_ZERO);
	TESTEQUALS(controller->set_selected_if_current(
		pending_drag, {planned}, input::game::SelectionKind::FOUNDATION), true);
	simulation->get_event_loop()->reach_time(time::TIME_ZERO, state);
	TESTEQUALS(controller->get_selected(), selected);

	// Input can arrive while the simulation has not yet resolved the preceding
	// Builder selection, especially when it is processing many build orders.
	controller->set_selected({});
	camera->look_at_coord(builder_position->get_positions().get(time::TIME_ZERO).to_scene3());
	camera_manager->update();
	stage->update(time::TIME_ZERO);
	click(manager, pixel_for(builder_id), Qt::NoModifier, false);
	stage->set_shown(std::nullopt);
	camera->look_at_coord(coord::tile{51, 66}.to_phys3_center().to_scene3());
	camera_manager->update();
	stage->update(time::TIME_ZERO);
	click(manager, pixel_for(waiting), Qt::ShiftModifier, false);
	camera->look_at_coord(coord::tile{57, 66}.to_phys3_center().to_scene3());
	camera_manager->update();
	stage->update(time::TIME_ZERO);
	simulation->get_event_loop()->reach_time(time::TIME_ZERO, state);
	TESTEQUALS(controller->get_selected(), selected);
	// The selection callback submits its order to the next inbox drain.
	simulation->get_event_loop()->reach_time(time::TIME_ZERO, state);
	TESTEQUALS(fvd->get_queued_build_count(builder_id), size_t{4});
	TESTEQUALS(fvd->get_build_order(builder_id)->get_id(), active_order);

	// Rapid clicks on adjacent sites must not target a neighbor's overlapping sprite.
	camera->set_zoom(2.0f);
	camera->look_at_coord(coord::tile{38, 66}.to_phys3_center().to_scene3());
	camera_manager->update();
	std::unordered_set<uint32_t> crowded_visible;
	for (auto id : crowded_foundations) crowded_visible.insert(static_cast<uint32_t>(id));
	stage->set_shown(std::move(crowded_visible));
	stage->update(time::TIME_ZERO);
	auto center_pixel = [&](const coord::tile &tile) {
		const auto scene = tile.to_phys3_center().to_scene3().to_world_space();
		const Eigen::Matrix4f matrix = camera->get_projection_matrix() * camera->get_view_matrix();
		const Eigen::Vector4f clip = matrix * Eigen::Vector4f{scene.x(), scene.y(), scene.z(), 1.0f};
		return coord::input{
			static_cast<coord::pixel_t>(std::lround((clip.x() / clip.w() * 0.5f + 0.5f) * TEST_TARGET_WIDTH)),
			static_cast<coord::pixel_t>(std::lround((0.5f - clip.y() / clip.w() * 0.5f) * TEST_TARGET_HEIGHT))};
	};
	std::vector<coord::input> crowded_pixels;
	std::optional<size_t> conflicting;
	std::optional<gamestate::entity_id_t> conflicting_pick;
	for (size_t i = 0; i < crowded_sites.size(); ++i) {
		auto point = center_pixel(crowded_sites[i]);
		TESTEQUALS(point.to_phys3(camera).to_tile(), crowded_sites[i]);
		for (coord::pixel_t dy = -20; not conflicting and dy <= 20; dy += 2) {
			for (coord::pixel_t dx = -40; dx <= 40; dx += 2) {
				const coord::input candidate{point.x + dx, point.y + dy};
				if (candidate.to_phys3(camera).to_tile() != crowded_sites[i]) continue;
				std::optional<uint32_t> picked;
				stage->pick_entity(candidate, [&](std::optional<uint32_t> id) { picked = id; });
				if (not picked or *picked == crowded_foundations[i]) continue;
				std::optional<uint32_t> target;
				stage->pick_entity(candidate, [&](std::optional<uint32_t> id) { target = id; },
				                   [&](uint32_t id) { return id == crowded_foundations[i]; });
				if (target) {
					point = candidate;
					conflicting = i;
					conflicting_pick = *picked;
					break;
				}
			}
		}
		crowded_pixels.push_back(point);
	}
	if (not conflicting) {
		TESTFAILMSG("Adjacent foundation sprites did not overlap within a site");
	}
	std::vector<size_t> clicked{*conflicting};
	for (size_t i = 0; i < crowded_sites.size() and clicked.size() < 3; ++i) {
		if (i != *conflicting and crowded_foundations[i] != *conflicting_pick) clicked.push_back(i);
	}
	TESTEQUALS(clicked.size(), size_t{3});
	for (size_t i = 0; i < clicked.size(); ++i) {
		const auto press = i == 1 ? QEvent::MouseButtonDblClick : QEvent::MouseButtonPress;
		click(manager, crowded_pixels[clicked[i]], Qt::ShiftModifier, false, press);
	}
	simulation->get_event_loop()->reach_time(time::TIME_ZERO, state);
	TESTEQUALS(fvd->get_queued_build_count(builder_id), size_t{7});
	const auto presentation = network::make_construction_presentation(
		network::capture_fvd_presentation_state(state, time::TIME_ZERO));
	for (size_t i = 0; i < crowded_sites.size(); ++i) {
		const bool expected = std::find(clicked.begin(), clicked.end(), i) != clicked.end();
		TESTEQUALS(fvd->is_tile_reserved(crowded_sites[i]), expected);
		auto site = std::find_if(presentation.sites.begin(), presentation.sites.end(),
		                         [&](const auto &item) {
			                         return item.entity_id == crowded_foundations[i];
		                         });
		TESTEQUALS(site != presentation.sites.end(), true);
		TESTEQUALS(site->site, crowded_sites[i]);
		TESTEQUALS(site->phase == (expected ? network::SitePresentationPhase::QUEUED
		                                   : network::SitePresentationPhase::ORPHANED), true);
		TESTEQUALS(site->queue_position > 1, expected);
	}
	stage->set_shown(std::nullopt);
	camera->set_zoom(0.32f);

	// A guest uses the same filtered picking and sends an authenticated
	// targeted order instead of mutating its local simulation.
	TESTEQUALS(fvd->cancel_build(state, builder_id, time::TIME_ZERO), true);
	TESTEQUALS(fvd->queue_build(state, 2, selected, {42, 66, 0}, time::TIME_ZERO), true);
	const auto guest_active_order = fvd->get_build_order(builder_id)->get_id();
	network::MatchConnectionState connections{fvd->get_player_count()};
	const network::SessionIdentity identity{123, 2};
	connections.bind_remote(identity);
	auto replica = network::capture_fvd_state(
		state, connections, time::TIME_ZERO, 0, 1, 2);
	auto guest = std::make_shared<input::game::Controller>(
		std::unordered_set<size_t>{2}, 2);
	guest->set_selected(selected, input::game::SelectionKind::BUILDER);
	auto guest_bindings = std::make_shared<input::game::BindingContext>();
	std::optional<network::CommandIntent> sent;
	std::optional<bool> accepted;
	input::game::setup_defaults(
		guest_bindings, guest, clock, simulation, camera, picker,
		[&](network::CommandIntent intent) {
			intent.command_id = 1;
			sent = network::decode_command_intent(network::encode_command_intent(intent));
			network::submit_authenticated_command(
				network::AuthenticatedCommand{identity, *sent}, connections,
				simulation->get_event_loop(), simulation->get_commander(),
				state, time::TIME_ZERO, [&](bool result) { accepted = result; });
			return true;
		},
		[&]() -> std::optional<network::WorldState> { return replica; });
	input::InputManager guest_input;
	auto guest_context = guest_input.get_global_context();
	input::setup_defaults(guest_context);
	guest_context->set_game_bindings(guest_bindings);
	guest_context->set_hud_bindings(std::make_shared<input::hud::BindingContext>());
	guest_input.set_game_controller(guest);
	guest_input.set_hud_controller(std::make_shared<input::hud::Controller>());
	guest->set_selected({});
	camera->look_at_coord(builder_position->get_positions().get(time::TIME_ZERO).to_scene3());
	camera_manager->update();
	stage->update(time::TIME_ZERO);
	const auto pending_guest_drag = guest->begin_selection_update();
	TESTEQUALS(guest_input.process(drag_press), true);
	TESTEQUALS(guest_input.process(drag_release), true);
	camera->look_at_coord(coord::tile{20, 20}.to_phys3_center().to_scene3());
	camera_manager->update();
	stage->update(time::TIME_ZERO);
	TESTEQUALS(guest->set_selected_if_current(
		pending_guest_drag, {planned}, input::game::SelectionKind::FOUNDATION), true);
	TESTEQUALS(guest->get_selected(), selected);

	camera->look_at_coord(coord::tile{54, 66}.to_phys3_center().to_scene3());
	camera_manager->update();
	stage->update(time::TIME_ZERO);
	const auto pending_guest = guest->begin_selection_update();
	click(guest_input, *overlap, Qt::ShiftModifier, false);
	camera->look_at_coord(coord::tile{57, 66}.to_phys3_center().to_scene3());
	camera_manager->update();
	stage->update(time::TIME_ZERO);
	TESTEQUALS(guest->set_selected_if_current(
		pending_guest, selected, input::game::SelectionKind::BUILDER), true);
	simulation->get_event_loop()->reach_time(time::TIME_ZERO, state);
	TESTEQUALS(sent.has_value(), true);
	TESTEQUALS(sent->kind == network::CommandKind::PRIORITIZE_FOUNDATION, true);
	TESTEQUALS(sent->target_entity, std::optional<gamestate::entity_id_t>{overlapping});
	TESTEQUALS(sent->queue, true);
	TESTEQUALS(accepted, std::optional<bool>{true});
	TESTEQUALS(fvd->get_queued_build_count(builder_id), size_t{1});
	TESTEQUALS(fvd->get_build_order(builder_id)->get_id(), guest_active_order);

	sent.reset();
	accepted.reset();
	camera->look_at_coord(coord::tile{61, 66}.to_phys3_center().to_scene3());
	camera_manager->update();
	stage->update(time::TIME_ZERO);
	click(guest_input, pixel_for(recreated), Qt::ShiftModifier);
	TESTEQUALS(sent.has_value(), true);
	TESTEQUALS(sent->target_entity, std::optional<gamestate::entity_id_t>{recreated});
	TESTEQUALS(sent->queue, true);
	TESTEQUALS(accepted, std::optional<bool>{true});
	TESTEQUALS(fvd->get_queued_build_count(builder_id), size_t{2});

	// Guests must send the site under the cursor, not the overlapping sprite.
	sent.reset();
	accepted.reset();
	camera->set_zoom(2.0f);
	camera->look_at_coord(coord::tile{38, 66}.to_phys3_center().to_scene3());
	camera_manager->update();
	std::unordered_set<uint32_t> guest_visible;
	for (auto id : crowded_foundations) guest_visible.insert(static_cast<uint32_t>(id));
	stage->set_shown(std::move(guest_visible));
	stage->update(time::TIME_ZERO);
	click(guest_input, crowded_pixels[*conflicting], Qt::ShiftModifier, false,
	      QEvent::MouseButtonDblClick);
	TESTEQUALS(sent.has_value(), true);
	TESTEQUALS(sent->target_entity,
	           std::optional<gamestate::entity_id_t>{crowded_foundations[*conflicting]});
	TESTEQUALS(sent->queue, true);
	simulation->get_event_loop()->reach_time(time::TIME_ZERO, state);
	TESTEQUALS(accepted, std::optional<bool>{true});
	TESTEQUALS(fvd->get_queued_build_count(builder_id), size_t{3});
	TESTEQUALS(fvd->is_tile_reserved(crowded_sites[*conflicting]), true);

	// The same sprite remains selectable when it overlaps a reserved site.
	// Shift-clicking it must not retry the already queued ground tile.
	replica = network::capture_fvd_state(
		state, connections, time::TIME_ZERO, 0, 1, 2);
	guest->cancel_tower_placement();
	click(guest_input, crowded_pixels[*conflicting], Qt::NoModifier, false);
	TESTEQUALS(guest->get_selected(),
	           (std::vector<gamestate::entity_id_t>{*conflicting_pick}));
	TESTEQUALS(guest->get_selection_details().kind == input::game::SelectionKind::FOUNDATION, true);
	guest->set_selected(selected, input::game::SelectionKind::BUILDER);
	sent.reset();
	accepted.reset();
	click(guest_input, crowded_pixels[*conflicting], Qt::ShiftModifier);
	TESTEQUALS(sent.has_value(), true);
	TESTEQUALS(sent->target_entity, conflicting_pick);
	TESTEQUALS(accepted, std::optional<bool>{true});
	TESTEQUALS(fvd->get_queued_build_count(builder_id), size_t{4});
	auto guest_site = std::find(crowded_foundations.begin(), crowded_foundations.end(),
	                            *conflicting_pick);
	TESTEQUALS(guest_site != crowded_foundations.end(), true);
	TESTEQUALS(fvd->is_tile_reserved(
		crowded_sites[static_cast<size_t>(guest_site - crowded_foundations.begin())]), true);
	stage->set_shown(std::nullopt);

	// Empty-ground Shift-clicks and Shift-drags remain construction commands,
	// including when Qt reports the drag's press as a double-click.
	TESTEQUALS(fvd->cancel_build(state, builder_id, time::TIME_ZERO), true);
	controller->set_selected(selected, input::game::SelectionKind::BUILDER);
	camera->look_at_coord(coord::tile{30, 66}.to_phys3_center().to_scene3());
	camera_manager->update();
	stage->set_shown(std::unordered_set<uint32_t>{});
	stage->update(time::TIME_ZERO);
	click(manager, center_pixel({30, 66}), Qt::ShiftModifier);
	TESTEQUALS(fvd->get_build_order(builder_id) != nullptr, true);
	TESTEQUALS(fvd->get_build_order(builder_id)->get_target(), (coord::tile{30, 66}));
	foundation_at({30, 66});

	camera->look_at_coord(coord::tile{27, 66}.to_phys3_center().to_scene3());
	camera_manager->update();
	stage->update(time::TIME_ZERO);
	const auto line_start = center_pixel({27, 66});
	const auto line_end = center_pixel({25, 66});
	const QPointF line_start_pos{static_cast<qreal>(line_start.x), static_cast<qreal>(line_start.y)};
	const QPointF line_end_pos{static_cast<qreal>(line_end.x), static_cast<qreal>(line_end.y)};
	QMouseEvent line_press{QEvent::MouseButtonDblClick, line_start_pos, line_start_pos,
	                       Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier};
	QMouseEvent line_release{QEvent::MouseButtonRelease, line_end_pos, line_end_pos,
	                         Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier};
	TESTEQUALS(manager.process(line_press), true);
	TESTEQUALS(manager.process(line_release), true);
	simulation->get_event_loop()->reach_time(time::TIME_ZERO, state);
	TESTEQUALS(fvd->get_queued_build_count(builder_id), size_t{3});
	for (coord::tile_t ne = 25; ne <= 27; ++ne) {
		TESTEQUALS(fvd->is_tile_reserved({ne, 66}), true);
		foundation_at({ne, 66});
	}
	stage->set_shown(std::nullopt);
	camera->set_zoom(0.32f);

	// An idle Builder starts the existing site rather than creating another.
	TESTEQUALS(fvd->cancel_build(state, builder_id, time::TIME_ZERO), true);
	controller->set_selected(selected, input::game::SelectionKind::BUILDER);
	camera->look_at_coord(coord::tile{61, 66}.to_phys3_center().to_scene3());
	camera_manager->update();
	stage->update(time::TIME_ZERO);
	click(manager, pixel_for(recreated), Qt::ShiftModifier);
	TESTEQUALS(fvd->get_build_order(builder_id) != nullptr, true);
	TESTEQUALS(fvd->get_build_order(builder_id)->get_target(), (coord::tile{61, 66}));
	TESTEQUALS(fvd->get_queued_build_count(builder_id), size_t{0});
	TESTEQUALS(foundation_at({61, 66}), recreated);

	// Exercise the same overlap with a fresh local presentation snapshot.
	stage->clear_entities();
	auto solo = std::make_shared<gamestate::GameSimulation>(root, cvars, clock);
	solo->set_modpacks({"fvd"});
	solo->initialize();
	auto solo_state = solo->get_game()->get_state();
	auto solo_fvd = solo_state->get_fvd();
	gamestate::tests::use_centre_test_layout(solo_state);
	solo->attach_renderer(std::make_shared<renderer::RenderFactory>(terrain, stage));
	auto solo_world = solo->get_fvd_presentation_state();
	auto solo_builder = std::find_if(solo_world->entities.begin(), solo_world->entities.end(),
	                                 [](const auto &entity) {
		                                 return entity.owner_id == 2
		                                     and entity.definition == "fvd.data.entities.Builder";
	                                 });
	TESTEQUALS(solo_builder != solo_world->entities.end(), true);
	const std::vector<gamestate::entity_id_t> solo_selected{solo_builder->entity_id};
	std::vector<gamestate::entity_id_t> solo_foundations;
	for (const auto &site : crowded_sites) {
		TESTEQUALS(solo_fvd->queue_build(
			solo_state, 2, solo_selected, site.to_phys3_center(), time::TIME_ZERO), true);
		auto foundation = std::find_if(
			solo_state->get_game_entities().begin(), solo_state->get_game_entities().end(),
			[&](const auto &entry) {
				if (entry.second->get_definition() != "fvd.data.entities.Foundation") return false;
				auto position = std::dynamic_pointer_cast<gamestate::component::Position>(
					entry.second->get_component(gamestate::component::component_t::POSITION));
				return position->get_positions().get(time::TIME_ZERO).to_tile() == site;
			});
		TESTEQUALS(foundation != solo_state->get_game_entities().end(), true);
		solo_foundations.push_back(foundation->first);
	}
	TESTEQUALS(solo_fvd->cancel_build(solo_state, solo_selected.front(), time::TIME_ZERO), true);
	camera->set_zoom(2.0f);
	camera->look_at_coord(coord::tile{38, 66}.to_phys3_center().to_scene3());
	camera_manager->update();
	std::unordered_set<uint32_t> solo_visible;
	for (auto id : solo_foundations) solo_visible.insert(static_cast<uint32_t>(id));
	stage->set_shown(std::move(solo_visible));
	stage->update(time::TIME_ZERO);
	std::optional<coord::input> solo_overlap;
	std::optional<size_t> solo_ground;
	std::optional<gamestate::entity_id_t> solo_sprite;
	for (size_t i = 0; i < crowded_sites.size() and not solo_overlap; ++i) {
		const auto center = center_pixel(crowded_sites[i]);
		for (coord::pixel_t dy = -20; dy <= 20 and not solo_overlap; dy += 2) {
			for (coord::pixel_t dx = -40; dx <= 40; dx += 2) {
				const coord::input candidate{center.x + dx, center.y + dy};
				if (candidate.to_phys3(camera).to_tile() != crowded_sites[i]) continue;
				std::optional<uint32_t> hit;
				stage->pick_entity(candidate, [&](std::optional<uint32_t> id) { hit = id; });
				if (not hit or *hit == solo_foundations[i]) continue;
				std::optional<uint32_t> ground_hit;
				stage->pick_entity(candidate, [&](std::optional<uint32_t> id) { ground_hit = id; },
				                   [&](uint32_t id) { return id == solo_foundations[i]; });
				if (ground_hit) {
					solo_overlap = candidate;
					solo_ground = i;
					solo_sprite = *hit;
					break;
				}
			}
		}
	}
	if (not solo_overlap or not solo_ground or not solo_sprite) {
		TESTFAILMSG("Local foundation sprites did not overlap within a site");
	}
	TESTEQUALS(solo_fvd->queue_foundation(
		solo_state, 2, solo_selected, solo_foundations[*solo_ground], time::TIME_ZERO), true);
	const auto solo_active_order = solo_fvd->get_build_order(solo_selected.front())->get_id();
	solo->get_fvd_presentation_state();
	TESTEQUALS(solo->submit_network_command(
		barrier, [&](bool result) {
			TESTEQUALS(result, false);
			solo->stop();
		}), true);
	solo->run();
	solo_world = solo->get_fvd_presentation_state();
	TESTEQUALS(std::any_of(solo_world->reservations.begin(), solo_world->reservations.end(),
	                       [&](const auto &reservation) {
		                       return reservation.tile == crowded_sites[*solo_ground];
	                       }), true);
	auto solo_controller = std::make_shared<input::game::Controller>(
		std::unordered_set<size_t>{1, 2}, 2);
	auto solo_bindings = std::make_shared<input::game::BindingContext>();
	input::game::setup_defaults(solo_bindings, solo_controller, clock, solo, camera, picker);
	input::InputManager solo_input;
	auto solo_context = solo_input.get_global_context();
	input::setup_defaults(solo_context);
	solo_context->set_game_bindings(solo_bindings);
	solo_context->set_hud_bindings(std::make_shared<input::hud::BindingContext>());
	solo_input.set_game_controller(solo_controller);
	solo_input.set_hud_controller(std::make_shared<input::hud::Controller>());
	size_t handled_mouse_events{};
	window->add_mouse_button_callback([&](const QMouseEvent &event) {
		if (solo_input.process(event)) {
			++handled_mouse_events;
		}
	});
	auto solo_click = [&](Qt::KeyboardModifiers modifiers) {
		const QPointF position{static_cast<qreal>(solo_overlap->x),
		                       static_cast<qreal>(solo_overlap->y)};
		QMouseEvent press{QEvent::MouseButtonPress, position, position,
		                  Qt::LeftButton, Qt::LeftButton, modifiers};
		QMouseEvent release{QEvent::MouseButtonRelease, position, position,
		                    Qt::LeftButton, Qt::NoButton, modifiers};
		handled_mouse_events = 0;
		QCoreApplication::sendEvent(window->get_qt_window().get(), &press);
		QCoreApplication::sendEvent(window->get_qt_window().get(), &release);
		window->process_events();
		TESTEQUALS(handled_mouse_events, size_t{2});
		solo->get_event_loop()->reach_time(time::TIME_ZERO, solo_state);
	};
	solo_click(Qt::NoModifier);
	TESTEQUALS(solo_controller->get_selected(),
	           (std::vector<gamestate::entity_id_t>{*solo_sprite}));
	TESTEQUALS(solo_controller->get_selection_details().kind == input::game::SelectionKind::FOUNDATION, true);
	solo_controller->set_selected(solo_selected, input::game::SelectionKind::BUILDER);
	solo_click(Qt::ShiftModifier);
	TESTEQUALS(solo_fvd->get_build_order(solo_selected.front())->get_id(), solo_active_order);
	TESTEQUALS(solo_fvd->get_queued_build_count(solo_selected.front()), size_t{1});
	auto solo_site = std::find(solo_foundations.begin(), solo_foundations.end(), *solo_sprite);
	TESTEQUALS(solo_site != solo_foundations.end(), true);
	TESTEQUALS(solo_fvd->is_tile_reserved(
		crowded_sites[static_cast<size_t>(solo_site - solo_foundations.begin())]), true);
	auto solo_presentation = network::make_construction_presentation(
		network::capture_fvd_presentation_state(solo_state, time::TIME_ZERO));
	auto queued = std::find_if(solo_presentation.sites.begin(), solo_presentation.sites.end(),
	                           [&](const auto &site) { return site.entity_id == *solo_sprite; });
	TESTEQUALS(queued != solo_presentation.sites.end(), true);
	TESTEQUALS(queued->phase == network::SitePresentationPhase::QUEUED, true);
	TESTEQUALS(queued->queue_position, uint32_t{2});

	// Foundation sprites extend beyond their ground tiles and can be
	// covered by a neighboring tower as well as by the selected Builder.
	stage->clear_entities();
	auto mixed_loop = std::make_shared<event::EventLoop>();
	mixed_loop->add_event_handler(std::make_shared<gamestate::event::ProcessCommandHandler>());
	mixed_loop->add_event_handler(std::make_shared<gamestate::event::SendCommandHandler>());
	mixed_loop->add_event_handler(std::make_shared<gamestate::event::WaitHandler>());
	auto mixed_commander = std::make_shared<gamestate::event::Commander>(mixed_loop);
	auto factory = std::make_shared<gamestate::EntityFactory>();
	gamestate::Game mixed_game{
		mixed_loop, state->get_mod_manager(), factory, std::make_shared<gamestate::TerrainFactory>()};
	auto mixed_state = mixed_game.get_state();
	auto mixed_fvd = mixed_state->get_fvd();
	gamestate::tests::use_centre_test_layout(mixed_state);
	auto render_factory = std::make_shared<renderer::RenderFactory>(terrain, stage);
	mixed_game.attach_renderer(render_factory);
	factory->attach_renderer(render_factory);
	auto mixed_world = network::capture_fvd_presentation_state(mixed_state, time::TIME_ZERO);
	auto mixed_builder = std::find_if(mixed_world.entities.begin(), mixed_world.entities.end(),
	                                  [](const auto &entity) {
		                                  return entity.owner_id == 2
		                                      and entity.definition == "fvd.data.entities.Builder";
	                                  });
	TESTEQUALS(mixed_builder != mixed_world.entities.end(), true);
	const std::vector<gamestate::entity_id_t> mixed_selected{mixed_builder->entity_id};
	const std::vector<coord::tile_delta> neighbors{
		{-1, -1}, {-1, 0}, {-1, 1}, {0, -1},
		{0, 1}, {1, -1}, {1, 0}, {1, 1}};
	std::vector<coord::tile> mixed_sites;
	std::vector<gamestate::entity_id_t> mixed_foundations;
	std::vector<gamestate::entity_id_t> mixed_towers;
	for (size_t i = 0; i < neighbors.size(); ++i) {
		const coord::tile tower_tile{
			30 + static_cast<coord::tile_t>(i) * 8, 90};
		auto tower = factory->spawn_game_entity(
			mixed_loop, mixed_state, 2, "fvd.data.entities.MedievalTowerLevel1",
			tower_tile.to_phys3_center(), time::TIME_ZERO);
		mixed_towers.push_back(tower->get_id());
		const auto site = tower_tile + neighbors[i];
		mixed_sites.push_back(site);
		TESTEQUALS(mixed_fvd->queue_build(
			mixed_state, 2, mixed_selected, site.to_phys3_center(), time::TIME_ZERO), true);
		auto foundation = std::find_if(
			mixed_state->get_game_entities().begin(), mixed_state->get_game_entities().end(),
			[&](const auto &entry) {
				if (entry.second->get_definition() != "fvd.data.entities.Foundation") return false;
				auto position = std::dynamic_pointer_cast<gamestate::component::Position>(
					entry.second->get_component(gamestate::component::component_t::POSITION));
				return position->get_positions().get(time::TIME_ZERO).to_tile() == site;
			});
		TESTEQUALS(foundation != mixed_state->get_game_entities().end(), true);
		mixed_foundations.push_back(foundation->first);
	}
	TESTEQUALS(mixed_fvd->cancel_build(mixed_state, mixed_selected.front(), time::TIME_ZERO), true);
	TESTEQUALS(mixed_fvd->queue_build(
		mixed_state, 2, mixed_selected, {48, 66, 0}, time::TIME_ZERO), true);
	const auto mixed_order = mixed_fvd->get_build_order(mixed_selected.front())->get_id();
	mixed_world = network::capture_fvd_presentation_state(mixed_state, time::TIME_ZERO);
	auto mixed_controller = std::make_shared<input::game::Controller>(
		std::unordered_set<size_t>{1, 2}, 2);
	auto mixed_bindings = std::make_shared<input::game::BindingContext>();
	std::optional<network::CommandIntent> mixed_sent;
	network::MatchConnectionState mixed_connections{mixed_fvd->get_player_count()};
	mixed_connections.bind_remote(identity);
	input::game::setup_defaults(
		mixed_bindings, mixed_controller, clock, simulation, camera, picker,
		[&](network::CommandIntent intent) {
			mixed_sent = intent;
			network::submit_authenticated_command(
				network::AuthenticatedCommand{identity, intent}, mixed_connections,
				mixed_loop, mixed_commander,
				mixed_state, time::TIME_ZERO, [&](bool result) { TESTEQUALS(result, true); });
			return true;
		},
		[&]() -> std::optional<network::WorldState> { return mixed_world; });
	input::InputManager mixed_input;
	auto mixed_context = mixed_input.get_global_context();
	input::setup_defaults(mixed_context);
	mixed_context->set_game_bindings(mixed_bindings);
	mixed_context->set_hud_bindings(std::make_shared<input::hud::BindingContext>());
	mixed_input.set_game_controller(mixed_controller);
	mixed_input.set_hud_controller(std::make_shared<input::hud::Controller>());
	mixed_controller->set_selected(mixed_selected, input::game::SelectionKind::BUILDER);
	size_t tower_obscured{};
	const auto entity_count = mixed_state->get_game_entities().size();
	auto mixed_builder_position = std::dynamic_pointer_cast<gamestate::component::Position>(
		mixed_state->get_game_entity(mixed_selected.front())->get_component(
			gamestate::component::component_t::POSITION));
	for (size_t i = 0; i < mixed_sites.size(); ++i) {
		mixed_builder_position->set_position(
			time::TIME_ZERO, (mixed_sites[i] + neighbors[i]).to_phys3_center());
		mixed_state->get_game_entity(mixed_selected.front())->render_update(
			time::TIME_ZERO, "fvd/data/../../../fvd/graphics/builder.sprite");
		camera->look_at_coord(mixed_sites[i].to_phys3_center().to_scene3());
		camera_manager->update();
		stage->set_shown(std::unordered_set<uint32_t>{
			static_cast<uint32_t>(mixed_foundations[i]), static_cast<uint32_t>(mixed_towers[i]),
			static_cast<uint32_t>(mixed_selected.front())});
		stage->update(time::TIME_ZERO);
		std::optional<coord::input> candidate;
		bool covered{};
		for (size_t y = 0; y < TEST_TARGET_HEIGHT and not covered; y += 2) {
			for (size_t x = 0; x < TEST_TARGET_WIDTH; x += 2) {
				const coord::input point{
					static_cast<coord::pixel_t>(x), static_cast<coord::pixel_t>(y)};
				if (point.to_phys3(camera).to_tile() == mixed_sites[i]) continue;
				std::optional<uint32_t> foundation_hit;
				stage->pick_entity(point, [&](std::optional<uint32_t> id) { foundation_hit = id; },
				                   [&](uint32_t id) { return id == mixed_foundations[i]; });
				if (not foundation_hit) continue;
				std::optional<uint32_t> hit;
				stage->pick_entity(point, [&](std::optional<uint32_t> id) { hit = id; });
				candidate = point;
				if (hit == mixed_towers[i]) {
					covered = true;
					++tower_obscured;
					break;
				}
			}
		}
		TESTEQUALS(candidate.has_value(), true);
		mixed_sent.reset();
		// Alternate buttons; neither may enter placement mode.
		const bool right = i % 2 == 1;
		click(mixed_input, *candidate, Qt::ShiftModifier, false, QEvent::MouseButtonPress,
		      right ? Qt::RightButton : Qt::LeftButton);
		TESTEQUALS(mixed_sent.has_value(), true);
		TESTEQUALS(mixed_sent->kind == network::CommandKind::PRIORITIZE_FOUNDATION, true);
		TESTEQUALS(mixed_sent->target_entity, mixed_foundations[i]);
		TESTEQUALS(mixed_sent->queue, true);
		TESTEQUALS(mixed_controller->is_tower_placement_active(), false);
		mixed_loop->reach_time(time::TIME_ZERO, mixed_state);
		TESTEQUALS(mixed_fvd->is_tile_reserved(mixed_sites[i]), true);
		TESTEQUALS(mixed_fvd->get_queued_build_count(mixed_selected.front()), i + 1);
		TESTEQUALS(mixed_fvd->get_build_order(mixed_selected.front())->get_id(), mixed_order);
		TESTEQUALS(mixed_state->get_game_entities().size(), entity_count);
		TESTEQUALS(mixed_controller->get_selected(), mixed_selected);
		mixed_world = network::capture_fvd_presentation_state(mixed_state, time::TIME_ZERO);
	}
	TESTEQUALS(tower_obscured > 0, true);

	// Shift+right-click on ground without a foundation is not an order.
	const coord::input empty_ground{0, 0};
	TESTEQUALS(std::find(mixed_sites.begin(), mixed_sites.end(),
	                     empty_ground.to_phys3(camera).to_tile())
	               == mixed_sites.end(),
	           true);
	mixed_controller->cancel_tower_placement();
	mixed_sent.reset();
	click(mixed_input, empty_ground, Qt::ShiftModifier, false, QEvent::MouseButtonPress,
	      Qt::RightButton);
	TESTEQUALS(mixed_sent.has_value(), false);
	TESTEQUALS(mixed_controller->is_tower_placement_active(), false);
	TESTEQUALS(mixed_controller->get_selected(), mixed_selected);
	window->close();
}

// Adjacent towers must overlap like AoE2 buildings: whatever is nearer the
// camera is in front, whatever order the towers were built in.
void adjacent_tower_draw_order() {
	auto root = util::Path{
		std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto gui_app = std::make_shared<gui::GuiApplicationWithLogger>();
	window_settings settings{};
	settings.width = TEST_TARGET_WIDTH;
	settings.height = TEST_TARGET_HEIGHT;
	settings.vsync = false;
	auto window = Window::create("openage tower draw order test", settings);
	window->get_qt_window()->setVisible(false);
	auto graphics_renderer = window->make_renderer();
	auto camera = std::make_shared<renderer::camera::Camera>(graphics_renderer, window->get_size());
	camera->set_zoom(1.0f);
	camera->look_at_coord(coord::scene3{1, 1, 0});
	auto camera_manager = std::make_shared<renderer::camera::CameraManager>(camera);
	camera_manager->update();
	auto assets = std::make_shared<resources::AssetManager>(
		graphics_renderer, root / "assets" / "fvd" / "graphics");
	auto clock = std::make_shared<time::Clock>();
	auto stage = std::make_shared<WorldRenderStage>(
		window, graphics_renderer, camera, root / "assets" / "shaders", assets, clock);

	using positions_t = std::vector<coord::phys3>;
	auto render_positions = [&](const positions_t &positions) {
		stage->clear_entities();
		uint32_t id = 1;
		for (const auto &position : positions) {
			auto entity = std::make_shared<RenderEntity>();
			entity->update(id++, position, "stone_tower.sprite", time::TIME_ZERO);
			entity->set_occlusion_mode(occlusion_mode::OCCLUDER, 2);
			stage->add_render_entity(entity);
		}
		stage->update(time::TIME_ZERO);
		graphics_renderer->render(stage->get_render_pass());
		return stage->get_render_pass()->get_target()->into_data();
	};
	using tiles_t = std::vector<std::pair<int, int>>;
	auto render = [&](const tiles_t &tiles) {
		positions_t positions;
		positions.reserve(tiles.size());
		for (auto [ne, se] : tiles) {
			positions.emplace_back(
				coord::phys_t::from_int(ne) + coord::phys_t{0.5},
				coord::phys_t::from_int(se) + coord::phys_t{0.5}, 0);
		}
		return render_positions(positions);
	};
	auto same = [](const resources::Texture2dData &a, const resources::Texture2dData &b) {
		for (size_t y = 0; y < TEST_TARGET_HEIGHT; ++y) {
			for (size_t x = 0; x < TEST_TARGET_WIDTH; ++x) {
				if (std::memcmp(pixel(a, x, y), pixel(b, x, y), 4) != 0) return false;
			}
		}
		return true;
	};

	// A 3x3 block and a row of equal-depth towers, built in both orders.
	tiles_t grid{{0, 0}, {0, 1}, {0, 2}, {1, 0}, {1, 1}, {1, 2}, {2, 0}, {2, 1}, {2, 2}};
	tiles_t row{{0, 0}, {1, 1}, {2, 2}};
	for (const auto &tiles : {grid, row}) {
		auto reversed = tiles;
		std::reverse(reversed.begin(), reversed.end());
		same(render(tiles), render(reversed)) or TESTFAILMSG("tower overlap depends on build order");
	}

	// The nearer tower (higher SE) is drawn completely over the rear one,
	// even when it was built first.
	const auto front_alone = render({{0, 1}});
	const auto both = render({{0, 1}, {0, 0}});
	size_t front_pixels = 0;
	size_t covered = 0;
	for (size_t y = 0; y < TEST_TARGET_HEIGHT; ++y) {
		for (size_t x = 0; x < TEST_TARGET_WIDTH; ++x) {
			const auto *front = pixel(front_alone, x, y);
			if (front[3] < 250) continue;
			++front_pixels;
			if (std::memcmp(front, pixel(both, x, y), 4) == 0) ++covered;
		}
	}
	(front_pixels > 10000) or TESTFAIL;
	if (covered < front_pixels * 99 / 100) {
		TESTFAILMSG(__FILE__ << ":" << __LINE__ << ": rear tower drawn over the front one ("
		                     << front_pixels - covered << " of " << front_pixels << " pixels)");
	}

	// Equal-depth towers are ordered left to right. Picking must use that same
	// tie-break instead of whichever tower happened to be built last.
	const coord::phys3 pick_rear_position{coord::phys_t{0.5}, coord::phys_t{0.5}, 0};
	const coord::phys3 pick_front_position{coord::phys_t{0.75}, coord::phys_t{0.75}, 0};
	const auto pick_rear = render_positions({pick_rear_position});
	const auto pick_front = render_positions({pick_front_position});
	std::optional<coord::input> overlap;
	for (size_t y = 0; y < TEST_TARGET_HEIGHT and not overlap.has_value(); ++y) {
		for (size_t x = 0; x < TEST_TARGET_WIDTH; ++x) {
			if (pixel(pick_front, x, y)[3] >= 250 and pixel(pick_rear, x, y)[3] >= 250) {
				overlap = coord::input{
					static_cast<coord::pixel_t>(x),
					static_cast<coord::pixel_t>(TEST_TARGET_HEIGHT - 1 - y)};
				break;
			}
		}
	}
	overlap.has_value() or TESTFAILMSG("adjacent towers have no opaque overlap to pick");
	for (const auto &positions : {positions_t{pick_front_position, pick_rear_position},
	                              positions_t{pick_rear_position, pick_front_position}}) {
		render_positions(positions);
		std::optional<uint32_t> picked;
		stage->pick_entity(*overlap, [&](std::optional<uint32_t> id) {
			picked = id;
		});
		const uint32_t front_id = positions.front() == pick_front_position ? 1 : 2;
		TESTEQUALS(picked.value_or(0), front_id);
	}
	window->close();
}

} // namespace openage::renderer::world::tests
