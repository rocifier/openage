// Copyright 2026 the openage authors. See copying.md for legal info.

#include "game_bindings.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <ostream>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <eigen3/Eigen/Dense>
#include <QEvent>
#include <QWindow>

#include "config.h"
#include "coord/phys.h"
#include "coord/pixel.h"
#include "coord/scene.h"
#include "coord/tile.h"
#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "event/eventhandler.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/fvd.h"
#include "gamestate/fvd_test_support.h"
#include "gamestate/game.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "input/controller/game/binding_context.h"
#include "input/controller/game/command_dispatcher.h"
#include "input/controller/game/controller.h"
#include "input/event.h"
#include "network/command_bridge.h"
#include "network/messages.h"
#include "renderer/camera/camera.h"
#include "renderer/camera/definitions.h"
#include "renderer/gui/integration/public/gui_application_with_logger.h"
#include "renderer/renderer.h"
#include "renderer/window.h"
#include "testing/testing.h"
#include "time/time.h"
#include "time/time_loop.h"
#include "util/fslike/directory.h"
#include "util/path.h"


namespace openage::input::game::tests {

namespace {

using gamestate::entity_id_t;
using gamestate::player_id_t;
using gamestate::component::command::command_t;
using param_map = event::EventHandler::param_map;

constexpr size_t WINDOW_WIDTH = 512;
constexpr size_t WINDOW_HEIGHT = 768;
/** Shows neighbouring tiles, as double-click groups and drags need. */
constexpr float ZOOM = 1.0f;

constexpr std::string_view BUILDER = "fvd.data.entities.Builder";
constexpr std::string_view HORSE = "fvd.data.entities.Horse";
constexpr std::string_view FOUNDATION = "fvd.data.entities.Foundation";
constexpr std::string_view WOOD_TOWER = "fvd.data.entities.MedievalTowerLevel1";
constexpr std::string_view STONE_TOWER = "fvd.data.entities.MedievalTowerLevel2";
constexpr std::string_view HUNTER_TOWER = "fvd.data.entities.HunterTower";

/** A locally submitted command, decoded from its event parameters. */
struct LocalCommand {
	explicit LocalCommand(const param_map &params) :
		type{params.get("type", command_t::NONE)},
		ids{params.get("entity_ids", std::vector<entity_id_t>{})},
		owner{params.get<player_id_t>("owner", 0)},
		target{params.get("target", coord::phys3{0, 0, 0})},
		queue{params.get("queue", false)},
		build_line{params.get("build_line", std::vector<coord::phys3>{})},
		result_cb{params.get("result_cb", network::command_result_callback_t{})} {
		if (params.contains("target_entity")) {
			this->target_entity = params.get<entity_id_t>("target_entity", 0);
		}
		if (params.contains("stance")) {
			this->stance = params.get("stance", gamestate::HunterStance::AGGRESSIVE);
		}
	}

	command_t type;
	std::vector<entity_id_t> ids;
	player_id_t owner;
	coord::phys3 target;
	bool queue;
	std::vector<coord::phys3> build_line;
	network::command_result_callback_t result_cb;
	std::optional<entity_id_t> target_entity;
	std::optional<gamestate::HunterStance> stance;
};

/** A controller selection, comparable and printable in assertions. */
struct Selection {
	std::vector<entity_id_t> ids;
	SelectionKind kind;

	bool operator==(const Selection &other) const = default;
};

std::ostream &operator<<(std::ostream &stream, const Selection &selection) {
	stream << "kind " << static_cast<int>(selection.kind) << " of {";
	for (size_t i = 0; i < selection.ids.size(); ++i) {
		stream << (i == 0 ? "" : ", ") << selection.ids[i];
	}
	return stream << "}";
}

Event key_release(int key) {
	return Event{event_class::KEYBOARD, key, Qt::NoModifier, QEvent::KeyRelease};
}

/**
 * Game bindings for player 2 on an FVD simulation, viewed through the
 * camera of a hidden window.
 *
 * Commands are recorded instead of executed: local ones in \p submitted,
 * guest ones in \p sent. Guests play while \p replica is set. Snapshots
 * come from \p world, or from the simulation while it is unset. The
 * picker reports the first entity of \p under_cursor that passes its
 * filter, wherever the click is.
 */
class BindingHarness {
public:
	explicit BindingHarness(bool online_lobby = false) :
		root{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)},
		gui_app{std::make_shared<renderer::gui::GuiApplicationWithLogger>()} {
		renderer::window_settings settings{};
		settings.width = WINDOW_WIDTH;
		settings.height = WINDOW_HEIGHT;
		settings.vsync = false;
		this->window = renderer::Window::create("openage game bindings test", settings);
		this->window->get_qt_window()->setVisible(false);
		this->graphics = this->window->make_renderer();
		this->camera = std::make_shared<renderer::camera::Camera>(this->graphics, this->window->get_size());
		this->camera->set_zoom(ZOOM);

		this->time_loop = std::make_shared<time::TimeLoop>();
		auto cvars = std::make_shared<cvar::CVarManager>(this->root);
		this->simulation = std::make_shared<gamestate::GameSimulation>(this->root, cvars, this->time_loop);
		this->simulation->set_modpacks({"fvd"});
		this->simulation->initialize();
		this->state = this->simulation->get_game()->get_state();
		this->fvd = this->state->get_fvd();

		this->commands = std::make_shared<CommandDispatcher>(
			[this](network::CommandIntent intent) {
				this->sent.push_back(std::move(intent));
				return true;
			},
			[this]() { return this->replica; },
			[this]() {
				return this->world ? this->world : std::make_shared<const network::WorldState>(this->capture());
			},
			[this](const param_map &params) { this->submitted.push_back(params); });
		this->picker = [this](const coord::input &,
		                      const entity_pick_callback_t &callback,
		                      const entity_pick_filter_t &filter) {
			for (auto id : this->under_cursor) {
				if (not filter or filter(id)) {
					callback(id);
					return;
				}
			}
			callback(std::nullopt);
		};

		std::unordered_set<size_t> players;
		for (size_t id = 1; id <= this->fvd->get_player_count(); ++id) {
			players.insert(id);
		}
		this->controller = std::make_shared<Controller>(players, 2);
		this->controller->set_order_feedback([this]() { ++this->acknowledged; });
		this->ctx = std::make_shared<BindingContext>();
		this->bindings = std::make_shared<GameBindings>(
			this->time_loop, this->simulation, this->camera, this->picker, this->commands, [this]() { return this->spawn_allowed; });
		this->bindings->bind(this->ctx, this->controller, online_lobby);
	}

	~BindingHarness() {
		this->window->close();
	}

	BindingHarness(const BindingHarness &) = delete;
	BindingHarness &operator=(const BindingHarness &) = delete;

	bool send(event_class cl, int code, int mods, int state, const coord::input &at) {
		return this->controller->process(
			event_arguments{Event{cl, code, mods, state}, at, coord::input_delta{0, 0}}, this->ctx);
	}

	bool key(int key) {
		return this->send(event_class::KEYBOARD, key, Qt::NoModifier, QEvent::KeyRelease, {0, 0});
	}

	void left_drag(const coord::input &from, const coord::input &to, int mods = Qt::NoModifier) {
		TESTEQUALS(this->send(event_class::MOUSE_BUTTON, Qt::LeftButton, mods, QEvent::MouseButtonPress, from), true);
		TESTEQUALS(this->send(event_class::MOUSE_BUTTON, Qt::LeftButton, mods, QEvent::MouseButtonRelease, to), true);
	}

	void left_click(const coord::input &at, int mods = Qt::NoModifier) {
		this->left_drag(at, at, mods);
	}

	/** The second click of a double-click, as Qt reports it. */
	void double_click(const coord::input &at) {
		TESTEQUALS(this->send(event_class::MOUSE_BUTTON_DBL, Qt::LeftButton, Qt::NoModifier, QEvent::MouseButtonDblClick, at),
		           true);
		TESTEQUALS(this->send(event_class::MOUSE_BUTTON, Qt::LeftButton, Qt::NoModifier, QEvent::MouseButtonRelease, at),
		           true);
	}

	void right_click(const coord::input &at, int mods = Qt::NoModifier) {
		TESTEQUALS(this->send(event_class::MOUSE_BUTTON, Qt::RightButton, mods, QEvent::MouseButtonRelease, at), true);
	}

	/** Run the simulation events submitted by selections and spawns. */
	void settle() {
		this->simulation->get_event_loop()->reach_time(time::TIME_ZERO, this->state);
	}

	coord::input centre() const {
		const auto &size = this->camera->get_viewport_size();
		return coord::input{static_cast<coord::pixel_t>(size[0] / 2),
		                    static_cast<coord::pixel_t>(size[1] / 2)};
	}

	coord::input bottom_right() const {
		const auto &size = this->camera->get_viewport_size();
		return coord::input{static_cast<coord::pixel_t>(size[0] - 1),
		                    static_cast<coord::pixel_t>(size[1] - 1)};
	}

	/** Centre the camera on a tile and return the window pixel over it. */
	coord::input aim(const coord::tile &tile) {
		this->camera->look_at_coord(tile.to_phys3_center().to_scene3());
		const auto pixel = this->centre();
		if (pixel.to_phys3(this->camera).to_tile() != tile) {
			throw testing::TestError{MSG(err) << "Window centre is not over tile " << tile};
		}
		return pixel;
	}

	/** The window pixel over a tile's centre, which must be on screen. */
	coord::input pixel_of(const coord::tile &tile) const {
		const Eigen::Matrix4f matrix = this->camera->get_projection_matrix() * this->camera->get_view_matrix();
		const auto scene = tile.to_phys3_center().to_scene3().to_world_space();
		const Eigen::Vector4f clip = matrix * Eigen::Vector4f{scene.x(), scene.y(), scene.z(), 1.0f};
		const auto &size = this->camera->get_viewport_size();
		const coord::input pixel{
			static_cast<coord::pixel_t>((clip.x() / clip.w() * 0.5f + 0.5f) * size[0]),
			static_cast<coord::pixel_t>((0.5f - clip.y() / clip.w() * 0.5f) * size[1])};
		if (pixel.x < 0 or pixel.y < 0
		    or pixel.x >= static_cast<coord::pixel_t>(size[0])
		    or pixel.y >= static_cast<coord::pixel_t>(size[1])
		    or pixel.to_phys3(this->camera).to_tile() != tile) {
			throw testing::TestError{MSG(err) << "Tile " << tile << " is not on screen"};
		}
		return pixel;
	}

	network::WorldState capture() const {
		return network::capture_fvd_presentation_state(this->state, time::TIME_ZERO);
	}

	const network::EntityState &find(const network::WorldState &snapshot,
	                                 std::string_view definition,
	                                 player_id_t owner) const {
		const auto found = std::find_if(snapshot.entities.begin(), snapshot.entities.end(), [&](const network::EntityState &entity) {
			return entity.definition == definition
			       and entity.owner_id == owner;
		});
		if (found == snapshot.entities.end()) {
			throw testing::TestError{MSG(err) << "Missing " << definition << " of player " << owner};
		}
		return *found;
	}

	entity_id_t entity(std::string_view definition, player_id_t owner) const {
		return this->find(this->capture(), definition, owner).entity_id;
	}

	coord::tile tile_of(entity_id_t id) const {
		const auto snapshot = this->capture();
		for (const auto &entity : snapshot.entities) {
			if (entity.entity_id == id) {
				return entity.position.to_tile();
			}
		}
		throw testing::TestError{MSG(err) << "Missing entity " << id};
	}

	entity_id_t foundation_at(const coord::tile &tile) const {
		const auto snapshot = this->capture();
		for (const auto &entity : snapshot.entities) {
			if (entity.definition == FOUNDATION and entity.position.to_tile() == tile) {
				return entity.entity_id;
			}
		}
		throw testing::TestError{MSG(err) << "Missing foundation at " << tile};
	}

	size_t count(std::string_view definition) const {
		const auto snapshot = this->capture();
		return std::count_if(snapshot.entities.begin(), snapshot.entities.end(), [definition](const network::EntityState &entity) {
			return entity.definition == definition;
		});
	}

	Selection selection() const {
		const auto details = this->controller->get_selection_details();
		return Selection{details.ids, details.kind};
	}

	/** The one command submitted locally since the last call. */
	LocalCommand take_local() {
		if (this->submitted.size() != 1 or not this->sent.empty()) {
			throw testing::TestError{MSG(err) << "Expected one local command, got "
			                                  << this->submitted.size() << " local and "
			                                  << this->sent.size() << " guest commands"};
		}
		LocalCommand command{this->submitted.front()};
		this->submitted.clear();
		return command;
	}

	/** The one command sent to the host since the last call. */
	network::CommandIntent take_guest() {
		if (this->sent.size() != 1 or not this->submitted.empty()) {
			throw testing::TestError{MSG(err) << "Expected one guest command, got "
			                                  << this->sent.size() << " guest and "
			                                  << this->submitted.size() << " local commands"};
		}
		auto intent = std::move(this->sent.front());
		this->sent.clear();
		return intent;
	}

	bool idle() const {
		return this->submitted.empty() and this->sent.empty();
	}

	util::Path root;
	std::shared_ptr<renderer::gui::GuiApplicationWithLogger> gui_app;
	std::shared_ptr<renderer::Window> window;
	std::shared_ptr<renderer::Renderer> graphics;
	std::shared_ptr<renderer::camera::Camera> camera;
	std::shared_ptr<time::TimeLoop> time_loop;
	std::shared_ptr<gamestate::GameSimulation> simulation;
	std::shared_ptr<gamestate::GameState> state;
	std::shared_ptr<gamestate::FVD> fvd;

	std::vector<param_map> submitted;
	std::vector<network::CommandIntent> sent;
	std::optional<network::WorldState> replica;
	std::shared_ptr<const network::WorldState> world;
	std::vector<entity_id_t> under_cursor;
	bool spawn_allowed = true;
	size_t acknowledged = 0;

	std::shared_ptr<CommandDispatcher> commands;
	entity_picker_t picker;
	std::shared_ptr<Controller> controller;
	std::shared_ptr<BindingContext> ctx;
	std::shared_ptr<GameBindings> bindings;
};

network::EntityState world_entity(entity_id_t id,
                                  player_id_t owner,
                                  std::string_view definition,
                                  const coord::tile &tile) {
	network::EntityState entity{};
	entity.entity_id = id;
	entity.owner_id = owner;
	entity.definition = definition;
	entity.position = tile.to_phys3_center();
	return entity;
}

} // namespace


void game_bindings_construction() {
	BindingHarness h;
	gamestate::tests::use_centre_test_layout(h.state);
	const auto builder = h.entity(BUILDER, 2);
	const std::vector<entity_id_t> selected{builder};

	TESTTHROWS((GameBindings{nullptr, h.simulation, h.camera, h.picker, h.commands}));
	TESTTHROWS((GameBindings{h.time_loop, nullptr, h.camera, h.picker, h.commands}));
	TESTTHROWS((GameBindings{h.time_loop, h.simulation, nullptr, h.picker, h.commands}));
	TESTTHROWS((GameBindings{h.time_loop, h.simulation, h.camera, {}, h.commands}));
	TESTTHROWS((GameBindings{h.time_loop, h.simulation, h.camera, h.picker, nullptr}));

	// Online lobbies assign players, so number keys must not switch them.
	auto lobby_ctx = std::make_shared<BindingContext>();
	h.bindings->bind(lobby_ctx, std::make_shared<Controller>(std::unordered_set<size_t>{2}, 2), true);
	TESTEQUALS(lobby_ctx->is_bound(key_release(Qt::Key_1)), false);
	TESTEQUALS(lobby_ctx->is_bound(key_release(Qt::Key_B)), true);
	TESTEQUALS(h.ctx->is_bound(key_release(Qt::Key_1)), true);

	// B arms placement only for a selected Builder.
	TESTEQUALS(h.key(Qt::Key_B), true);
	TESTEQUALS(h.controller->is_tower_placement_active(), false);
	h.controller->set_selected(selected, SelectionKind::BUILDER);
	TESTEQUALS(h.key(Qt::Key_B), true);
	TESTEQUALS(h.controller->is_tower_placement_active(), true);

	// An invalid site keeps placement armed so the click can be corrected.
	const coord::tile foundation_tile{51, 66};
	TESTEQUALS(h.fvd->queue_build(h.state, 2, selected, foundation_tile.to_phys3_center(), time::TIME_ZERO), true);
	h.settle();
	const auto foundation = h.foundation_at(foundation_tile);
	const auto occupied = h.aim(foundation_tile);
	TESTEQUALS(h.fvd->can_place_tower(h.state, 2, selected, occupied.to_phys3(h.camera), time::TIME_ZERO), false);
	h.left_click(occupied);
	TESTEQUALS(h.idle(), true);
	TESTEQUALS(h.controller->is_tower_placement_active(), true);

	// A valid site is built and placement ends.
	const auto site = h.aim({57, 66});
	const auto site_pos = site.to_phys3(h.camera);
	TESTEQUALS(h.fvd->can_place_tower(h.state, 2, selected, site_pos, time::TIME_ZERO), true);
	h.left_click(site);
	auto build = h.take_local();
	TESTEQUALS(build.type, command_t::BUILD_TOWER);
	TESTEQUALS(build.ids, selected);
	TESTEQUALS(build.owner, player_id_t{2});
	TESTEQUALS(build.target, site_pos);
	TESTEQUALS(build.queue, false);
	TESTEQUALS(build.build_line.empty(), true);
	TESTEQUALS(h.controller->is_tower_placement_active(), false);

	// Shift-click appends a site, entering and staying in placement mode.
	h.left_click(site, Qt::ShiftModifier);
	build = h.take_local();
	TESTEQUALS(build.type, command_t::BUILD_TOWER);
	TESTEQUALS(build.target, site_pos);
	TESTEQUALS(build.queue, true);
	TESTEQUALS(h.controller->is_tower_placement_active(), true);
	TESTEQUALS(h.key(Qt::Key_Escape), true);
	TESTEQUALS(h.controller->is_tower_placement_active(), false);

	// Shift-drag queues the axis-locked line as one ordered command.
	const auto line_end = h.pixel_of({60, 66});
	std::vector<coord::phys3> line;
	for (const auto &tile : tower_line_sites({57, 66}, {60, 66})) {
		line.push_back(tile.to_phys3_center());
	}
	TESTEQUALS(line.size(), size_t{4});
	h.left_drag(site, line_end, Qt::ShiftModifier);
	build = h.take_local();
	TESTEQUALS(build.type, command_t::BUILD_TOWER);
	TESTEQUALS(build.queue, true);
	TESTEQUALS(build.target, line.front());
	TESTEQUALS(build.build_line, line);
	TESTEQUALS(h.key(Qt::Key_Escape), true);

	// A line longer than one command may carry is ignored.
	h.camera->set_zoom(renderer::camera::DEFAULT_MAX_ZOOM_OUT);
	const coord::input far_start{0, 0};
	const auto far_end = h.bottom_right();
	TESTEQUALS(tower_line_sites(far_start.to_phys3(h.camera).to_tile(),
	                            far_end.to_phys3(h.camera).to_tile())
	                   .size()
	               > network::MAX_BUILD_LINE_SITES,
	           true);
	h.left_drag(far_start, far_end, Qt::ShiftModifier);
	TESTEQUALS(h.idle(), true);
	TESTEQUALS(h.key(Qt::Key_Escape), true);
	h.camera->set_zoom(ZOOM);
	TESTEQUALS(h.aim({57, 66}), site);

	// Shift+right or Shift+left click queues an owned foundation, even
	// behind the Builder, without entering placement mode.
	h.under_cursor = {builder, foundation};
	for (const auto right : {true, false}) {
		if (right) {
			h.right_click(site, Qt::ShiftModifier);
		}
		else {
			h.left_click(site, Qt::ShiftModifier);
		}
		const auto queued = h.take_local();
		TESTEQUALS(queued.type, command_t::MOVE);
		TESTEQUALS(queued.ids, selected);
		TESTEQUALS(queued.target_entity, std::optional<entity_id_t>{foundation});
		TESTEQUALS(queued.queue, true);
		TESTEQUALS(static_cast<bool>(queued.result_cb), true);
		queued.result_cb(false);
		TESTEQUALS(h.controller->is_tower_placement_active(), false);
	}

	// Queueing a foundation leaves an armed placement mode armed.
	TESTEQUALS(h.key(Qt::Key_B), true);
	h.left_click(site, Qt::ShiftModifier);
	TESTEQUALS(h.take_local().target_entity, std::optional<entity_id_t>{foundation});
	TESTEQUALS(h.controller->is_tower_placement_active(), true);
	TESTEQUALS(h.key(Qt::Key_Escape), true);

	// A queue click right after a selection click waits for that selection.
	for (const auto right : {true, false}) {
		const auto generation = h.controller->begin_selection_update();
		if (right) {
			h.right_click(site, Qt::ShiftModifier);
		}
		else {
			h.left_click(site, Qt::ShiftModifier);
		}
		TESTEQUALS(h.idle(), true);
		TESTEQUALS(h.controller->set_selected_if_current(generation, selected, SelectionKind::BUILDER), true);
		TESTEQUALS(h.take_local().target_entity, std::optional<entity_id_t>{foundation});
	}

	// Without a foundation under the cursor Shift+right-click is no order.
	h.under_cursor.clear();
	h.right_click(site, Qt::ShiftModifier);
	TESTEQUALS(h.idle(), true);

	// Right-click moves the Builder, targeting the entity under the cursor.
	h.right_click(site);
	auto order = h.take_local();
	TESTEQUALS(order.type, command_t::MOVE);
	TESTEQUALS(order.ids, selected);
	TESTEQUALS(order.target, site_pos);
	TESTEQUALS(order.target_entity.has_value(), false);
	TESTEQUALS(order.queue, false);
	h.under_cursor = {foundation};
	h.right_click(site);
	order = h.take_local();
	TESTEQUALS(order.type, command_t::MOVE);
	TESTEQUALS(order.target_entity, std::optional<entity_id_t>{foundation});
	TESTEQUALS(h.acknowledged, size_t{2});

	// An order right after a selection click waits for that selection.
	auto generation = h.controller->begin_selection_update();
	h.right_click(site);
	TESTEQUALS(h.idle(), true);
	TESTEQUALS(h.controller->set_selected_if_current(generation, selected, SelectionKind::BUILDER), true);
	TESTEQUALS(h.take_local().type, command_t::MOVE);

	// Right-click and Escape cancel placement and garrison modes.
	TESTEQUALS(h.key(Qt::Key_B), true);
	h.right_click(site);
	TESTEQUALS(h.controller->is_tower_placement_active(), false);
	TESTEQUALS(h.key(Qt::Key_T), true);
	TESTEQUALS(h.controller->is_garrison_mode_active(), true);
	h.right_click(site);
	TESTEQUALS(h.controller->is_garrison_mode_active(), false);
	TESTEQUALS(h.key(Qt::Key_T), true);
	TESTEQUALS(h.key(Qt::Key_Escape), true);
	TESTEQUALS(h.controller->is_garrison_mode_active(), false);
	TESTEQUALS(h.idle(), true);

	// Hunter controls do nothing for a Builder.
	TESTEQUALS(h.key(Qt::Key_A), true);
	TESTEQUALS(h.controller->is_attack_move_active(), false);
	TESTEQUALS(h.key(Qt::Key_Z), true);
	TESTEQUALS(h.key(Qt::Key_Delete), true);
	TESTEQUALS(h.key(Qt::Key_G), true);
	TESTEQUALS(h.idle(), true);

	// Delete demolishes one selected foundation or tower.
	h.controller->set_selected({foundation}, SelectionKind::FOUNDATION);
	TESTEQUALS(h.key(Qt::Key_Delete), true);
	auto demolish = h.take_local();
	TESTEQUALS(demolish.type, command_t::DEMOLISH_TOWER);
	TESTEQUALS(demolish.ids, (std::vector<entity_id_t>{foundation}));
	h.controller->set_selected({foundation, builder}, SelectionKind::GROUP_FOUNDATIONS);
	TESTEQUALS(h.key(Qt::Key_Delete), true);
	TESTEQUALS(h.idle(), true);

	// T, then a click on a friendly tower or its tile garrisons the Builder.
	const coord::tile ally_tile{60, 66};
	auto snapshot = h.capture();
	const auto ally_tower = world_entity(90001, 3, WOOD_TOWER, ally_tile);
	const auto own_tower = world_entity(90002, 2, STONE_TOWER, {62, 66});
	snapshot.entities.push_back(ally_tower);
	snapshot.entities.push_back(own_tower);
	h.world = std::make_shared<const network::WorldState>(std::move(snapshot));
	h.controller->set_selected(selected, SelectionKind::BUILDER);
	TESTEQUALS(h.key(Qt::Key_T), true);
	h.under_cursor.clear();
	h.left_click(site);
	TESTEQUALS(h.idle(), true);
	TESTEQUALS(h.controller->is_garrison_mode_active(), true);
	h.under_cursor = {ally_tower.entity_id};
	h.left_click(site);
	auto garrison = h.take_local();
	TESTEQUALS(garrison.type, command_t::GARRISON);
	TESTEQUALS(garrison.ids, selected);
	TESTEQUALS(garrison.target_entity, std::optional<entity_id_t>{ally_tower.entity_id});
	garrison.result_cb(false);
	TESTEQUALS(h.controller->is_garrison_mode_active(), false);
	TESTEQUALS(h.key(Qt::Key_T), true);
	h.under_cursor.clear();
	h.left_click(h.aim(ally_tile));
	TESTEQUALS(h.take_local().target_entity, std::optional<entity_id_t>{ally_tower.entity_id});
	const auto ground = h.aim({57, 66});

	// T needs a single selected Builder.
	h.controller->set_selected({foundation}, SelectionKind::FOUNDATION);
	TESTEQUALS(h.key(Qt::Key_T), true);
	TESTEQUALS(h.controller->is_garrison_mode_active(), false);

	// A selected tower ungarrisons with G or the HUD button, and right-click
	// sets its ungarrison point, snapping to another friendly tower.
	h.controller->set_selected({own_tower.entity_id}, SelectionKind::TOWER);
	TESTEQUALS(h.key(Qt::Key_G), true);
	auto ungarrison = h.take_local();
	TESTEQUALS(ungarrison.type, command_t::UNGARRISON);
	TESTEQUALS(ungarrison.ids, (std::vector<entity_id_t>{own_tower.entity_id}));
	TESTEQUALS(h.controller->issue_ungarrison(), true);
	TESTEQUALS(h.take_local().type, command_t::UNGARRISON);
	TESTEQUALS(h.key(Qt::Key_Delete), true);
	TESTEQUALS(h.take_local().type, command_t::DEMOLISH_TOWER);

	h.under_cursor = {own_tower.entity_id};
	h.right_click(ground);
	auto point = h.take_local();
	TESTEQUALS(point.type, command_t::SET_UNGARRISON_POINT);
	TESTEQUALS(point.target, site_pos);
	TESTEQUALS(point.target_entity.has_value(), false);
	h.under_cursor = {ally_tower.entity_id};
	h.right_click(ground);
	point = h.take_local();
	TESTEQUALS(point.type, command_t::SET_UNGARRISON_POINT);
	TESTEQUALS(point.target_entity, std::optional<entity_id_t>{ally_tower.entity_id});
	TESTEQUALS(point.target, ally_tile.to_phys3_center());
	TESTEQUALS(h.acknowledged, size_t{3});
}

void game_bindings_hunter() {
	BindingHarness h;
	const auto hunter_tower = h.entity(HUNTER_TOWER, 1);
	const auto builder = h.entity(BUILDER, 2);

	// Number keys switch the controlled player and centre the camera.
	TESTEQUALS(h.key(Qt::Key_1), true);
	TESTEQUALS(h.controller->get_controlled(), size_t{1});
	const auto looked_at = h.centre().to_phys3(h.camera).to_tile();
	const auto centre = h.fvd->get_centre().to_tile();
	TESTEQUALS(std::abs(looked_at.ne - centre.ne) <= 1 and std::abs(looked_at.se - centre.se) <= 1, true);

	// Clicking the Hunter tower selects it from the snapshot right away.
	auto ground = h.centre();
	auto ground_pos = ground.to_phys3(h.camera);
	h.under_cursor = {hunter_tower};
	h.left_click(ground);
	TESTEQUALS(h.selection(), (Selection{{hunter_tower}, SelectionKind::HUNTER_TOWER}));

	// Its right-click moves the rally point; the Hunter cannot demolish.
	h.under_cursor.clear();
	h.right_click(ground);
	const auto rally = h.take_local();
	TESTEQUALS(rally.type, command_t::SET_RALLY_POINT);
	TESTEQUALS(rally.ids, (std::vector<entity_id_t>{hunter_tower}));
	TESTEQUALS(rally.owner, player_id_t{1});
	TESTEQUALS(rally.target, ground_pos);
	TESTEQUALS(h.key(Qt::Key_Delete), true);
	TESTEQUALS(h.key(Qt::Key_G), true);
	h.right_click(ground, Qt::ShiftModifier);
	TESTEQUALS(h.idle(), true);
	TESTEQUALS(h.acknowledged, size_t{0});

	// F9 spawns the first horse at once, but only on the authoritative simulation.
	h.fvd->start_hunter_wave(h.state, time::TIME_ZERO);
	TESTEQUALS(h.count(HORSE), size_t{0});
	h.spawn_allowed = false;
	TESTEQUALS(h.key(Qt::Key_F9), true);
	h.settle();
	TESTEQUALS(h.count(HORSE), size_t{0});
	h.spawn_allowed = true;
	TESTEQUALS(h.key(Qt::Key_F9), true);
	h.settle();
	TESTEQUALS(h.count(HORSE), size_t{1});
	TESTEQUALS(h.fvd->get_first_hunter_spawn_time().has_value(), false);
	const auto horse = h.entity(HORSE, 1);
	const std::vector<entity_id_t> horses{horse};

	// Clicking a horse resolves its selection on the simulation thread.
	ground = h.aim(h.tile_of(horse));
	ground_pos = ground.to_phys3(h.camera);
	h.under_cursor = {horse};
	h.left_click(ground);
	h.settle();
	TESTEQUALS(h.selection(), (Selection{horses, SelectionKind::HORSE}));

	// Dragging prefers units over the Hunter's buildings.
	h.under_cursor.clear();
	h.left_click(ground);
	h.settle();
	TESTEQUALS(h.selection(), (Selection{{}, SelectionKind::NONE}));
	h.left_drag({0, 0}, h.bottom_right());
	h.settle();
	TESTEQUALS(h.selection(), (Selection{horses, SelectionKind::HORSE}));

	// Z, X and C and the HUD buttons set the selected horses' stance.
	for (const auto &[key, stance] : {std::pair{Qt::Key_Z, gamestate::HunterStance::AGGRESSIVE},
	                                  std::pair{Qt::Key_X, gamestate::HunterStance::DEFENSIVE},
	                                  std::pair{Qt::Key_C, gamestate::HunterStance::OBSERVE}}) {
		TESTEQUALS(h.key(key), true);
		const auto command = h.take_local();
		TESTEQUALS(command.type, command_t::SET_STANCE);
		TESTEQUALS(command.ids, horses);
		TESTEQUALS(command.stance, stance);
	}
	TESTEQUALS(h.controller->issue_hunter_stance(gamestate::HunterStance::DEFENSIVE), true);
	TESTEQUALS(h.take_local().stance, gamestate::HunterStance::DEFENSIVE);

	// A or P, then a click sends the horses there once.
	for (const auto &[key, type] : {std::pair{Qt::Key_A, command_t::ATTACK_MOVE},
	                                std::pair{Qt::Key_P, command_t::PATROL}}) {
		TESTEQUALS(h.key(key), true);
		TESTEQUALS(h.controller->is_attack_move_active() or h.controller->is_patrol_active(), true);
		h.left_click(ground);
		const auto route = h.take_local();
		TESTEQUALS(route.type, type);
		TESTEQUALS(route.ids, horses);
		TESTEQUALS(route.target, ground_pos);
		TESTEQUALS(h.controller->is_attack_move_active() or h.controller->is_patrol_active(), false);
	}
	TESTEQUALS(h.acknowledged, size_t{2});
	// Escape and right-click cancel targeting without an order.
	TESTEQUALS(h.key(Qt::Key_A), true);
	TESTEQUALS(h.key(Qt::Key_Escape), true);
	TESTEQUALS(h.controller->is_attack_move_active(), false);
	TESTEQUALS(h.key(Qt::Key_P), true);
	h.right_click(ground);
	TESTEQUALS(h.controller->is_patrol_active(), false);
	TESTEQUALS(h.idle(), true);

	// Right-click focus-attacks the entity under the cursor, or moves.
	h.under_cursor = {builder};
	h.right_click(ground);
	auto order = h.take_local();
	TESTEQUALS(order.type, command_t::FOCUS_ATTACK);
	TESTEQUALS(order.ids, horses);
	TESTEQUALS(order.target_entity, std::optional<entity_id_t>{builder});
	h.under_cursor.clear();
	h.right_click(ground);
	order = h.take_local();
	TESTEQUALS(order.type, command_t::MOVE);
	TESTEQUALS(order.target, ground_pos);
	TESTEQUALS(order.target_entity.has_value(), false);
	TESTEQUALS(h.acknowledged, size_t{4});

	// Number keys switch back to a Builder.
	TESTEQUALS(h.key(Qt::Key_2), true);
	TESTEQUALS(h.controller->get_controlled(), size_t{2});
	TESTEQUALS(h.selection(), (Selection{{}, SelectionKind::NONE}));
}

void game_bindings_selection() {
	BindingHarness h;
	gamestate::tests::use_centre_test_layout(h.state);
	const auto builder = h.entity(BUILDER, 2);
	const std::vector<entity_id_t> selected{builder};
	TESTEQUALS(h.fvd->queue_build(h.state, 2, selected, coord::tile{57, 66}.to_phys3_center(), time::TIME_ZERO), true);
	TESTEQUALS(h.fvd->queue_build(h.state, 2, selected, coord::tile{54, 66}.to_phys3_center(), time::TIME_ZERO), true);
	h.settle();
	const auto first = h.foundation_at({57, 66});
	const auto second = h.foundation_at({54, 66});
	const auto foundations = std::minmax(first, second);
	const std::vector<entity_id_t> both{foundations.first, foundations.second};
	const auto click = h.aim({57, 66});

	// A unit click resolves on the simulation thread; empty ground deselects.
	h.under_cursor = {builder};
	h.left_click(click);
	h.settle();
	TESTEQUALS(h.selection(), (Selection{selected, SelectionKind::BUILDER}));
	h.under_cursor.clear();
	h.left_click(click);
	h.settle();
	TESTEQUALS(h.selection(), (Selection{{}, SelectionKind::NONE}));

	// Structures resolve from the snapshot; Shift adds the same kind.
	h.under_cursor = {first};
	h.left_click(click);
	TESTEQUALS(h.selection(), (Selection{{first}, SelectionKind::FOUNDATION}));
	h.under_cursor = {second};
	h.left_click(h.pixel_of({54, 66}), Qt::ShiftModifier);
	TESTEQUALS(h.idle(), true);
	TESTEQUALS(h.selection(), (Selection{both, SelectionKind::GROUP_FOUNDATIONS}));

	// A double-click selects the owner's on-screen structures of that kind,
	// and the release Qt sends after it cannot undo that.
	h.under_cursor = {first};
	h.left_click(click);
	TESTEQUALS(h.selection(), (Selection{{first}, SelectionKind::FOUNDATION}));
	h.double_click(click);
	TESTEQUALS(h.selection(), (Selection{both, SelectionKind::GROUP_FOUNDATIONS}));

	// Double-clicking a unit keeps the selection.
	h.controller->set_selected({first}, SelectionKind::FOUNDATION);
	h.under_cursor = {builder};
	h.double_click(click);
	TESTEQUALS(h.selection(), (Selection{{first}, SelectionKind::FOUNDATION}));

	// A double-click during placement neither selects nor places.
	h.controller->set_selected(selected, SelectionKind::BUILDER);
	TESTEQUALS(h.key(Qt::Key_B), true);
	h.under_cursor = {first};
	h.double_click(click);
	TESTEQUALS(h.idle(), true);
	TESTEQUALS(h.controller->is_tower_placement_active(), true);
	TESTEQUALS(h.selection(), (Selection{selected, SelectionKind::BUILDER}));
	TESTEQUALS(h.key(Qt::Key_Escape), true);

	// Dragging selects the player's units in the box, before buildings.
	h.under_cursor.clear();
	h.left_click(h.aim(h.tile_of(builder)));
	h.settle();
	TESTEQUALS(h.selection(), (Selection{{}, SelectionKind::NONE}));
	h.left_drag({0, 0}, h.bottom_right());
	h.settle();
	TESTEQUALS(h.selection(), (Selection{selected, SelectionKind::BUILDER}));
}

void game_bindings_guest() {
	BindingHarness h;
	const coord::tile home{40, 40};
	const auto click = h.aim(home);
	const auto click_pos = click.to_phys3(h.camera);

	const network::EntityState builder = world_entity(501, 2, BUILDER, home);
	const network::EntityState ally_builder = world_entity(502, 3, BUILDER, {41, 40});
	const network::EntityState foundation = world_entity(601, 2, FOUNDATION, {43, 40});
	const network::EntityState ally_tower = world_entity(701, 3, WOOD_TOWER, {37, 40});
	const network::EntityState own_tower = world_entity(702, 2, STONE_TOWER, {40, 43});
	const network::EntityState horse = world_entity(801, 1, HORSE, {41, 41});
	const network::EntityState second_horse = world_entity(802, 1, HORSE, {39, 41});
	network::WorldState world{};
	world.local_player_id = 2;
	world.entities = {builder, ally_builder, foundation, ally_tower, own_tower, horse, second_horse};
	h.replica = world;

	// Clicks select from the host's replicated world: own units and allied towers.
	h.under_cursor = {builder.entity_id};
	h.left_click(click);
	TESTEQUALS(h.selection(), (Selection{{builder.entity_id}, SelectionKind::BUILDER}));
	h.under_cursor = {ally_builder.entity_id};
	h.left_click(click);
	TESTEQUALS(h.selection(), (Selection{{}, SelectionKind::NONE}));
	h.under_cursor = {ally_tower.entity_id};
	h.left_click(click);
	TESTEQUALS(h.selection(), (Selection{{ally_tower.entity_id}, SelectionKind::TOWER}));

	// Dragging selects the guest's own Builder on screen.
	h.under_cursor.clear();
	h.left_drag({0, 0}, h.bottom_right());
	TESTEQUALS(h.selection(), (Selection{{builder.entity_id}, SelectionKind::BUILDER}));

	// Builder right-clicks prioritise an owned foundation and otherwise move.
	h.under_cursor = {foundation.entity_id};
	h.right_click(click);
	auto intent = h.take_guest();
	TESTEQUALS(intent.kind, network::CommandKind::PRIORITIZE_FOUNDATION);
	TESTEQUALS(intent.entity_ids, (std::vector<entity_id_t>{builder.entity_id}));
	TESTEQUALS(intent.target_entity, std::optional<entity_id_t>{foundation.entity_id});
	TESTEQUALS(intent.queue, false);
	h.under_cursor = {ally_tower.entity_id};
	h.right_click(click);
	intent = h.take_guest();
	TESTEQUALS(intent.kind, network::CommandKind::MOVE);
	TESTEQUALS(intent.target, click_pos);
	TESTEQUALS(intent.target_entity.has_value(), false);
	TESTEQUALS(h.acknowledged, size_t{2});

	// Shift-clicks queue the foundation through the host.
	h.under_cursor = {foundation.entity_id};
	h.right_click(click, Qt::ShiftModifier);
	intent = h.take_guest();
	TESTEQUALS(intent.kind, network::CommandKind::PRIORITIZE_FOUNDATION);
	TESTEQUALS(intent.target_entity, std::optional<entity_id_t>{foundation.entity_id});
	TESTEQUALS(intent.queue, true);
	h.left_click(click, Qt::ShiftModifier);
	TESTEQUALS(h.take_guest().queue, true);
	TESTEQUALS(h.controller->is_tower_placement_active(), false);

	// The host validates tower sites, so placement ends once one is sent.
	TESTEQUALS(h.key(Qt::Key_B), true);
	h.left_click(click);
	intent = h.take_guest();
	TESTEQUALS(intent.kind, network::CommandKind::BUILD_TOWER);
	TESTEQUALS(intent.target, click_pos);
	TESTEQUALS(intent.queue, false);
	TESTEQUALS(h.controller->is_tower_placement_active(), false);

	// T, then a click on an allied tower garrisons the Builder there.
	TESTEQUALS(h.key(Qt::Key_T), true);
	h.under_cursor = {ally_tower.entity_id};
	h.left_click(click);
	intent = h.take_guest();
	TESTEQUALS(intent.kind, network::CommandKind::GARRISON);
	TESTEQUALS(intent.target_entity, std::optional<entity_id_t>{ally_tower.entity_id});

	// A selected tower's commands also go to the host.
	h.controller->set_selected({own_tower.entity_id}, SelectionKind::TOWER);
	h.right_click(click);
	intent = h.take_guest();
	TESTEQUALS(intent.kind, network::CommandKind::SET_UNGARRISON_POINT);
	TESTEQUALS(intent.target_entity, std::optional<entity_id_t>{ally_tower.entity_id});
	TESTEQUALS(intent.target, ally_tower.position);
	TESTEQUALS(h.key(Qt::Key_G), true);
	TESTEQUALS(h.take_guest().kind, network::CommandKind::UNGARRISON);
	TESTEQUALS(h.key(Qt::Key_Delete), true);
	TESTEQUALS(h.take_guest().kind, network::CommandKind::DEMOLISH_TOWER);

	// The Hunter selects its horses as a group.
	world.local_player_id = 1;
	h.replica = world;
	h.controller->set_control(1);
	h.under_cursor = {horse.entity_id};
	h.left_click(h.pixel_of(horse.position.to_tile()));
	TESTEQUALS(h.selection(), (Selection{{horse.entity_id}, SelectionKind::HORSE}));
	h.under_cursor.clear();
	h.left_drag({0, 0}, h.bottom_right());
	const std::vector<entity_id_t> horses{horse.entity_id, second_horse.entity_id};
	TESTEQUALS(h.selection(), (Selection{horses, SelectionKind::GROUP_HORSES}));

	// Horses focus-attack Builders and towers, and move onto anything else.
	for (const auto target : {builder.entity_id, own_tower.entity_id}) {
		h.under_cursor = {target};
		h.right_click(click);
		intent = h.take_guest();
		TESTEQUALS(intent.kind, network::CommandKind::FOCUS_ATTACK);
		TESTEQUALS(intent.entity_ids, horses);
		TESTEQUALS(intent.target_entity, std::optional<entity_id_t>{target});
	}
	h.under_cursor = {second_horse.entity_id};
	h.right_click(click);
	intent = h.take_guest();
	TESTEQUALS(intent.kind, network::CommandKind::MOVE);
	TESTEQUALS(intent.target_entity.has_value(), false);

	TESTEQUALS(h.key(Qt::Key_A), true);
	h.left_click(click);
	TESTEQUALS(h.take_guest().kind, network::CommandKind::ATTACK_MOVE);
	TESTEQUALS(h.key(Qt::Key_X), true);
	intent = h.take_guest();
	TESTEQUALS(intent.kind, network::CommandKind::SET_STANCE);
	TESTEQUALS(intent.stance, gamestate::HunterStance::DEFENSIVE);

	// Without a selection a right-click is no order.
	h.controller->set_selected({});
	h.right_click(click);
	TESTEQUALS(h.idle(), true);
}

} // namespace openage::input::game::tests
