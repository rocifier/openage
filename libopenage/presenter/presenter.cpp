// Copyright 2019-2024 the openage authors. See copying.md for legal info.

#include "presenter.h"

#include <eigen3/Eigen/Dense>
#include <algorithm>
#include <chrono>
#include <exception>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QVariantList>
#include <QVariantMap>
#include <QQmlPropertyMap>
#include <QSettings>
#include <QVariant>
#include <cmath>
#include <iostream>
#include <unordered_set>
#include <stdexcept>
#include <utility>
#include <string>
#include <vector>

#include "error/error.h"
#include "gamestate/api/ability.h"
#include "gamestate/api/animation.h"
#include "gamestate/api/property.h"
#include "gamestate/api/types.h"
#include "gamestate/fvd.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "input/controller/camera/binding_context.h"
#include "input/controller/camera/controller.h"
#include "input/controller/game/binding_context.h"
#include "input/controller/game/controller.h"
#include "input/controller/hud/binding_context.h"
#include "input/controller/hud/controller.h"
#include "input/input_context.h"
#include "input/input_manager.h"
#include "log/log.h"
#include "network/construction_presentation.h"
#include "network/lobby_relay_client.h"
#include "network/order_presentation.h"
#include "network/replication.h"
#include "renderer/camera/camera.h"
#include "renderer/gui/gui.h"
#include "renderer/gui/guisys/public/gui_input.h"
#include "renderer/gui/integration/public/gui_application_with_logger.h"
#include "renderer/render_factory.h"
#include "renderer/render_pass.h"
#include "renderer/render_target.h"
#include "renderer/resources/assets/asset_manager.h"
#include "renderer/resources/shader_source.h"
#include "renderer/resources/texture_info.h"
#include "renderer/stages/camera/manager.h"
#include "renderer/stages/hud/render_stage.h"
#include "renderer/stages/screen/render_stage.h"
#include "renderer/stages/skybox/render_stage.h"
#include "renderer/stages/terrain/render_stage.h"
#include "renderer/stages/world/render_entity.h"
#include "renderer/stages/world/render_stage.h"
#include "time/clock.h"
#include "time/time_loop.h"
#include "util/path.h"
#include "util/stall_profiler.h"


namespace openage::presenter {

namespace {

constexpr double HORSE_DEATH_VISUAL_SECONDS = 3.1;

std::shared_ptr<QQmlPropertyMap> create_property_map() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
	return std::shared_ptr<QQmlPropertyMap>{QQmlPropertyMap::create()};
#else
	return std::make_shared<QQmlPropertyMap>();
#endif
}

} // namespace

Presenter::Presenter(const util::Path &root_dir,
                     const std::shared_ptr<gamestate::GameSimulation> &simulation,
                     const std::shared_ptr<time::TimeLoop> &time_loop) :
	root_dir{root_dir},
	render_passes{},
	simulation{simulation},
	time_loop{time_loop},
	game_controller{nullptr} {}

Presenter::~Presenter() = default;

void Presenter::set_fvd_lobby_enabled(bool enabled, std::chrono::milliseconds update_interval) {
	if (update_interval <= std::chrono::milliseconds::zero()) {
		throw Error{MSG(err) << "Network update interval must be positive"};
	}
	this->fvd_update_interval = update_interval;
	this->fvd_lobby_enabled = enabled;
	if (enabled) {
		this->fvd_lobby_client = std::make_unique<network::RelayLobbyClient>();
	}
}


void Presenter::run(const renderer::window_settings window_settings,
                    const std::function<void()> &on_initialized) {
	log::log(INFO << "Presenter: Launching subsystems...");

	this->init_graphics(window_settings);

	this->init_input();

	if (on_initialized) {
		on_initialized();
	}

	std::exception_ptr frame_error{};
	this->gui_app->run_event_loop([this, &frame_error]() {
		if (this->stop_requested) {
			return false;
		}

		try {
			{
				util::StallProfiler profile{"presenter.process_input"};
				this->window->process_events();
			}
			if (this->window->should_close()) {
				return false;
			}

			{
				util::StallProfiler profile{"presenter.render"};
				this->render();
			}

			{
				util::StallProfiler profile{"presenter.check_renderer_error"};
				this->renderer->check_error();
			}

			{
				util::StallProfiler profile{"presenter.window_present"};
				this->window->present();
			}
		}
		catch (...) {
			frame_error = std::current_exception();
			return false;
		}

		return true;
	});

	log::log(MSG(info) << "Presenter: Draw loop exited");
	this->window->close();

	if (frame_error) {
		std::rethrow_exception(frame_error);
	}
}

void Presenter::stop() {
	this->stop_requested = true;
}

void Presenter::set_simulation(const std::shared_ptr<gamestate::GameSimulation> &simulation) {
	this->simulation = simulation;
	auto render_factory = std::make_shared<renderer::RenderFactory>(
		this->terrain_renderer, this->world_renderer, this->hud_renderer);
	this->simulation->attach_renderer(render_factory);
}

void Presenter::set_time_loop(const std::shared_ptr<time::TimeLoop> &time_loop) {
	this->time_loop = time_loop;
}

void Presenter::set_fvd_replication(const std::shared_ptr<network::ReplicationTracker> &replication) {
	this->fvd_replication = replication;
}

std::shared_ptr<qtgui::GuiApplication> Presenter::init_window_system() {
	return std::make_shared<renderer::gui::GuiApplicationWithLogger>();
}

void Presenter::init_graphics(const renderer::window_settings &window_settings) {
	log::log(INFO << "Presenter: Initializing graphics subsystems...");

	// Start up rendering framework
	this->gui_app = this->init_window_system();

	// Window and renderer
	this->window = renderer::Window::create("openage presenter test", window_settings);
	this->renderer = this->window->make_renderer();

	// Asset mangement
	this->asset_manager = std::make_shared<renderer::resources::AssetManager>(
		this->renderer,
		this->root_dir / "assets" / "converted");
	auto missing_tex = this->root_dir / "assets" / "test" / "textures" / "test_missing.sprite";
	this->asset_manager->set_placeholder_animation(missing_tex);

	// Camera
	this->camera = std::make_shared<renderer::camera::Camera>(this->renderer, this->window->get_size());
	this->camera->look_at_coord(coord::scene3{10.0, 10.0, 0}); // Center camera on the map
	this->window->add_resize_callback([this](size_t w, size_t h, double /*scale*/) {
		this->camera->resize(w, h);
	});

	// Camera manager
	this->camera_manager = std::make_shared<renderer::camera::CameraManager>(this->camera);
	// TODO: Make boundaries dynamic based on map size.
	this->camera_manager->set_camera_boundaries(
		renderer::camera::CameraBoundaries{
			renderer::camera::X_BOUND_MIN,
			renderer::camera::X_BOUND_MAX,
			renderer::camera::Y_BOUND_MIN,
			renderer::camera::Y_BOUND_MAX,
			renderer::camera::Z_BOUND_MIN,
			renderer::camera::Z_BOUND_MAX});

	// Skybox
	this->skybox_renderer = std::make_shared<renderer::skybox::SkyboxRenderStage>(
		this->window,
		this->renderer,
		this->root_dir["assets"]["shaders"]);
	this->skybox_renderer->set_color(1.0f, 0.5f, 0.0f, 1.0f);
	this->render_passes.push_back(this->skybox_renderer->get_render_pass());

	// Terrain
	this->terrain_renderer = std::make_shared<renderer::terrain::TerrainRenderStage>(
		this->window,
		this->renderer,
		this->camera,
		this->root_dir["assets"]["shaders"],
		this->asset_manager,
		this->time_loop->get_clock());
	this->render_passes.push_back(this->terrain_renderer->get_render_pass());

	// Units/buildings
	this->world_renderer = std::make_shared<renderer::world::WorldRenderStage>(
		this->window,
		this->renderer,
		this->camera,
		this->root_dir["assets"]["shaders"],
		this->asset_manager,
		this->time_loop->get_clock());
	this->render_passes.push_back(this->world_renderer->get_render_pass());

	// HUD
	this->hud_renderer = std::make_shared<renderer::hud::HudRenderStage>(
		this->window,
		this->renderer,
		this->camera,
		this->root_dir["assets"]["shaders"],
		this->asset_manager,
		this->time_loop->get_clock());
	this->render_passes.push_back(this->hud_renderer->get_render_pass());

	this->init_gui();
	this->init_final_render_pass();

	if (this->simulation) {
		auto render_factory = std::make_shared<renderer::RenderFactory>(
			this->terrain_renderer, this->world_renderer, this->hud_renderer);
		this->simulation->attach_renderer(render_factory);
		auto fvd = this->simulation->get_game()->get_state()->get_fvd();
		if (fvd) {
			this->terrain_renderer->set_grid_enabled(true);
			this->camera->look_at_coord(fvd->get_centre().to_scene3());
			auto size = fvd->get_centre().ne.to_float() * 2;
			auto eye = this->camera->get_position();
			auto centre = fvd->get_centre().to_scene3().to_world_space();
			auto offset = eye - centre;
			this->camera_manager->set_camera_boundaries({
				offset.x(), offset.x() + size, offset.y(), offset.y(),
				offset.z() - size, offset.z()});
			this->skybox_renderer->set_color(0.08f, 0.12f, 0.08f, 1.0f);
		}
	}

	log::log(INFO << "Presenter: Graphics subsystems initialized");
}

void Presenter::init_gui() {
	log::log(INFO << "Presenter: Initializing GUI with Qt backend");

	//// -- gui initialization
	// TODO: Do not use test GUI
	util::Path qml_root = this->root_dir / "assets" / "test" / "qml";
	if (this->simulation and this->simulation->get_game()->get_state()->get_fvd()) {
		qml_root = this->root_dir / "assets" / "fvd" / "qml";
	}
	log::log(INFO << "Presenter: Setting QML root to " << qml_root.resolve_native_path());
	if (not qml_root.is_dir()) {
		throw Error{ERR << "could not find qml root folder " << qml_root};
	}

	util::Path qml_assets = this->root_dir / "assets";
	log::log(INFO << "Presenter: Setting QML asset path to " << qml_assets.resolve_native_path());
	if (not qml_assets.is_dir()) {
		throw Error{ERR << "could not find asset root folder " << qml_assets};
	}

	util::Path qml_root_file = qml_root / "main.qml";
	log::log(INFO << "Presenter: Setting QML root file to " << qml_root_file.resolve_native_path());
	if (not qml_root_file.is_file()) {
		throw Error{ERR << "could not find main.qml file " << qml_root_file};
	}

	// TODO: in order to support qml-mods, the fslike and filelike
	//       library has to be integrated into qt. For now,
	//       figure out the absolute paths here and pass them in.

	this->gui = std::make_shared<renderer::gui::GUI>(
		this->gui_app, // Qt application wrapper
		this->window,  // window for the gui
		qml_root_file, // entry qml file, absolute path.
		qml_root,      // directory to watch for qml file changes
		qml_assets,    // qml data: Engine *, the data directory, ...
		this->renderer // openage renderer
	);

	auto gui_pass = this->gui->get_render_pass();
	this->render_passes.push_back(gui_pass);
}

std::string Presenter::fvd_lobby_action_at(const QPointF &position) const {
	if (not this->gui) return {};
	for (const auto &[item, action] : {
		std::pair{"fvdLobbyHostButton", "host"},
		std::pair{"fvdLobbyRefreshButton", "refresh"},
		std::pair{"fvdLobbyJoinButton", "join"},
		std::pair{"fvdLobbyStartButton", "start"},
		std::pair{"fvdLobbyBackButton", "disconnect"},
		std::pair{"fvdLobbyEndButton", "disconnect"},
	}) {
		if (this->gui->contains_item(QString::fromUtf8(item), position)) {
			return action;
		}
	}
	return {};
}

void Presenter::handle_fvd_lobby_action(const std::string &action) {
	if (not this->fvd_lobby_state or not this->fvd_lobby_client) return;
	if (this->fvd_reset_pending) return; // New match world is not ready yet.
	if (action == "disconnect") {
		const bool reset_match = this->fvd_gameplay_active
			|| this->fvd_bootstrap_supplied || bool(this->fvd_replication);
		this->fvd_lobby_local_error.clear();
		this->fvd_gameplay_active = false;
		this->fvd_assigned_player = 0;
		this->fvd_replication.reset();
		this->fvd_guest_visuals_initialized = false;
		this->fvd_guest_visual_revision = 0;
		this->fvd_guest_clock = GuestClock{};
		this->fvd_guest_snapshot_generation = 0;
		this->fvd_last_guest_arrow_id = 0;
		this->fvd_guest_late_arrows.clear();
		this->fvd_previous_combat_world.reset();
		this->clear_fvd_combat_visuals();
		for (auto &[id, visual] : this->fvd_guest_entities) visual->deactivate(0);
		this->fvd_guest_entities.clear();
		this->fvd_guest_sprite_paths.clear();
		if (this->fvd_lobby_hosting and this->time_loop) {
			this->time_loop->get_clock()->pause();
		}
		this->fvd_bootstrap_requested = false;
		this->fvd_bootstrap_supplied = false;
		this->fvd_participant_slots_configured = false;
		this->fvd_capture_recipient = 0;
		this->fvd_bootstrapped_recipients.clear();
		this->fvd_capture_cursor = 0;
		this->fvd_recorded_departures.clear();
		this->fvd_update_requested = false;
		this->fvd_next_world_revision = 2;
		this->fvd_last_update_request = {};
		this->fvd_lobby_hosting = false;
		if (this->simulation) {
			this->simulation->cancel_network_bootstrap();
		}
		this->fvd_lobby_client->close();
		if (this->game_controller) {
			auto fvd = this->simulation->get_game()->get_state()->get_fvd();
			if (fvd) this->game_controller->reset_online_player_slots(fvd->get_player_count());
		}
		if (reset_match and this->simulation and this->time_loop) {
			// Shutdown relay and clear the old render stages before the
			// simulation owner thread builds a clean game at time zero.
			auto clock = this->time_loop->get_clock();
			clock->pause();
			clock->reset_paused();
			this->world_renderer->clear_entities();
			this->terrain_renderer->clear_entities();
			this->simulation->request_match_reset();
			this->fvd_reset_pending = true;
			this->fvd_lobby_local_error = "Preparing a fresh FVD match...";
		}
		return;
	}
	if (action == "start") {
		this->fvd_lobby_client->request_start();
		return;
	}
	if (action != "host" and action != "join" and action != "refresh") return;
	// A lost match has an old clock, entity world and session identity.
	// Never reuse that simulation for a freshly authenticated room.
	if (this->fvd_bootstrap_supplied or this->fvd_replication) {
		this->fvd_lobby_local_error =
			"Reset the previous match with F10 before hosting or joining again.";
		return;
	}
	this->fvd_lobby_local_error.clear();
	this->fvd_gameplay_active = false;
	this->fvd_assigned_player = 0;
	this->fvd_replication.reset();
	this->fvd_guest_visuals_initialized = false;
	this->fvd_guest_visual_revision = 0;
	this->fvd_guest_clock = GuestClock{};
	this->fvd_guest_snapshot_generation = 0;
	this->fvd_last_guest_arrow_id = 0;
	this->fvd_guest_late_arrows.clear();
	this->fvd_previous_combat_world.reset();
	this->clear_fvd_combat_visuals();
	for (auto &[id, visual] : this->fvd_guest_entities) visual->deactivate(0);
	this->fvd_guest_entities.clear();
	this->fvd_guest_sprite_paths.clear();
	if (this->fvd_lobby_hosting and this->time_loop) {
		this->time_loop->get_clock()->pause();
	}
	this->fvd_bootstrap_requested = false;
	this->fvd_bootstrap_supplied = false;
		this->fvd_participant_slots_configured = false;
		this->fvd_capture_recipient = 0;
		this->fvd_bootstrapped_recipients.clear();
		this->fvd_capture_cursor = 0;
		this->fvd_recorded_departures.clear();
	this->fvd_update_requested = false;
	this->fvd_next_world_revision = 2;
	this->fvd_last_update_request = {};
	if (this->simulation) {
		this->simulation->cancel_network_bootstrap();
	}
	if (this->game_controller) {
		auto fvd = this->simulation->get_game()->get_state()->get_fvd();
		if (fvd) this->game_controller->reset_online_player_slots(fvd->get_player_count());
	}
	try {
		auto val = [&](const char *name) {
			return this->fvd_lobby_state->value(name).toString().trimmed();
		};
		bool ok_port = false;
		const auto port = val("port").toUInt(&ok_port);
		if (not ok_port or port == 0 or port > 65535) {
			throw std::invalid_argument{"Relay port must be a valid positive number"};
		}
		network::RelayLobbyConfig config{};
		config.address = val("address").toStdString();
		config.port = static_cast<uint16_t>(port);
		config.token_hex = val("token").toStdString();
		config.name = val("name").toStdString();
		if (action == "refresh") {
			this->fvd_lobby_hosting = false;
			this->fvd_lobby_suggested_room_id = 0;
			this->fvd_lobby_state->insert("roomId", QString{});
			this->fvd_lobby_client->refresh(std::move(config));
			return;
		}
		config.hosting = action == "host";
		if (config.hosting) {
			bool valid_count = false;
			auto count = val("maxPlayers").toUInt(&valid_count);
			if (not valid_count or count < 2 or count > 8) {
				throw std::invalid_argument{"Maximum players must be between 2 and 8"};
			}
			config.player_capacity = count;
			config.room_name = val("roomName").toStdString();
		}
		else {
			bool ok_room = false;
			config.room_id = val("roomId").toULongLong(&ok_room);
			if (not ok_room or config.room_id == 0) {
				throw std::invalid_argument{"Room ID must be a valid positive number"};
			}
		}
		this->fvd_lobby_hosting = config.hosting;
		this->fvd_lobby_client->connect(std::move(config));
	}
	catch (const std::exception &error) {
		this->fvd_lobby_local_error = error.what();
	}
}

void Presenter::refresh_fvd_lobby_state() {
	if (not this->fvd_lobby_enabled or not this->fvd_lobby_state
	    or not this->fvd_lobby_client) return;
	if (this->fvd_reset_pending) {
		if (not this->simulation->consume_match_reset_completed()) return;
		// Only this GUI/render thread creates OpenGL resources. The game
		// replacement was created separately on the simulation owner thread.
		auto factory = std::make_shared<renderer::RenderFactory>(
			this->terrain_renderer, this->world_renderer, this->hud_renderer);
		this->simulation->attach_renderer(factory);
		this->fvd_reset_pending = false;
		this->fvd_lobby_local_error.clear();
		this->fvd_lobby_suggested_room_id = 0;
		this->game_controller->reset_online_player_slots(
			this->simulation->get_game()->get_state()->get_fvd()->get_player_count());
	}
	const auto state = this->fvd_lobby_client->status();
	const bool actual_authority = this->fvd_lobby_client->is_authority();
	if (actual_authority != this->fvd_lobby_hosting) {
		// A Builder who created the lobby becomes an ordinary guest, while
		// the randomly drawn Hunter becomes the sole simulation authority.
		// This transition takes place before the first world snapshot.
		if (this->fvd_bootstrap_supplied or this->fvd_gameplay_active) {
			this->fvd_lobby_local_error =
				"Unsafe late Hunter authority transfer; reset the match with F10.";
			return;
		}
		this->fvd_lobby_hosting = actual_authority;
		this->fvd_bootstrap_requested = false;
		this->fvd_bootstrap_supplied = false;
		this->fvd_participant_slots_configured = false;
		this->fvd_bootstrapped_recipients.clear();
		this->fvd_recorded_departures.clear();
		this->fvd_capture_cursor = 0;
		this->fvd_capture_recipient = 0;
		this->fvd_update_requested = false;
		this->fvd_next_world_revision = 2;
		this->fvd_replication.reset();
		if (this->time_loop) this->time_loop->get_clock()->pause();
		if (this->simulation) this->simulation->cancel_network_bootstrap();
	}
	// Snapshot capture belongs to the simulation thread. The presenter only
	// requests it and passes the immutable result to the relay worker.
	if (state.phase == "assigned" and this->simulation and not this->fvd_bootstrap_supplied) {
		try {
			if (this->fvd_lobby_hosting) {
				// Prune unoccupied starting Builders in EVERY network match,
				// including the legacy two-peer Hunter-creator path. Only
				// applying this for multi_host left all seven default
				// Builders alive whenever the lobby creator drew Hunter.
				if (not this->fvd_participant_slots_configured) {
					const auto participant_count = state.match_player_count != 0
						? state.match_player_count
						: (state.local_player_id != 0 and state.remote_player_id != 0
						       ? size_t{2} : size_t{0});
					if (participant_count < 2 or participant_count > network::MAX_STATE_PLAYERS) {
						throw Error{MSG(err) << "Cannot start network match without valid player count"};
					}
					this->simulation->configure_network_participants(participant_count);
					this->fvd_participant_slots_configured = true;
					log::log(INFO << "[FVD/match] starting_builders_prune_scheduled"
					              << " participants=" << participant_count
					              << " builders=" << participant_count - 1);
				}
				// One request at a time across all recipient-private streams.
				if (not this->fvd_bootstrap_requested) {
					auto recipients = state.multi_host ? state.remote_player_ids
					                                   : std::vector<gamestate::player_id_t>{
					                                       state.remote_player_id};
					for (auto id : recipients) {
						if (id == 0
						    or std::find(this->fvd_bootstrapped_recipients.begin(),
						                 this->fvd_bootstrapped_recipients.end(), id)
						               != this->fvd_bootstrapped_recipients.end()) continue;
						this->simulation->request_network_bootstrap(id);
						this->fvd_capture_recipient = id;
						this->fvd_bootstrap_requested = true;
						break;
					}
				}
				if (this->fvd_bootstrap_requested) {
					auto world = this->simulation->take_network_bootstrap();
					if (world) {
						this->fvd_lobby_client->provide_host_snapshot(std::move(*world));
						this->fvd_bootstrapped_recipients.push_back(this->fvd_capture_recipient);
						this->fvd_bootstrap_requested = false;
					}
				}
				if (state.multi_host) {
					this->fvd_bootstrap_supplied = std::all_of(
						state.remote_player_ids.begin(), state.remote_player_ids.end(),
						[&](gamestate::player_id_t id) {
							return std::find(this->fvd_bootstrapped_recipients.begin(),
							                 this->fvd_bootstrapped_recipients.end(), id)
							       != this->fvd_bootstrapped_recipients.end();
						});
				}
				else this->fvd_bootstrap_supplied =
					this->fvd_bootstrapped_recipients.size() == 1;
			}
		}
		catch (const std::exception &error) {
			this->fvd_lobby_local_error = error.what();
		}
	}
	// Lock the local controller to the one player chosen by StartMatch.
	if (state.local_player_id != 0 and this->game_controller
	    and state.local_player_id != this->fvd_assigned_player) {
		this->game_controller->lock_to_online_player(state.local_player_id);
		this->fvd_assigned_player = state.local_player_id;
	}
	if (not this->fvd_lobby_hosting and not this->fvd_replication) {
		this->fvd_replication = this->fvd_lobby_client->get_replication();
	}
	const bool ready_to_play = state.phase == "playing" and this->fvd_assigned_player != 0
		&& (this->fvd_lobby_hosting or (this->fvd_replication
			&& this->fvd_replication->has_state()));
	if (ready_to_play and not this->fvd_gameplay_active) {
		// The initial graphics setup centres everyone on the Hunter's tower,
		// but each Builder starts somewhere else on the spawn ring. Once the
		// authenticated role and initial world are ready, centre only this
		// client's view on its own authoritative Builder. Do this once per
		// match, never continuously follow a moving unit.
		if (this->camera and this->simulation) {
			if (this->fvd_assigned_player == 1) {
				// Also restore the Hunter's existing centre view after an F10
				// reset, even if the previous match ended elsewhere on the map.
				if (auto fvd = this->simulation->get_game()->get_state()->get_fvd()) {
					this->camera->look_at_coord(fvd->get_centre().to_scene3());
				}
			}
			else {
				const auto world = this->fvd_replication
					? this->fvd_replication->get_world_state()
					: std::optional<network::WorldState>{};
				const auto local_world = this->fvd_replication
					? nullptr : this->simulation->get_fvd_presentation_state();
				const network::WorldState *initial = world ? &*world : local_world.get();
				if (initial) {
					const auto builder = std::find_if(
						initial->entities.begin(), initial->entities.end(),
						[&](const network::EntityState &entity) {
							return entity.owner_id == this->fvd_assigned_player
							    and entity.definition == "fvd.data.entities.Builder"
							    and not entity.remembered;
						});
					if (builder != initial->entities.end()) {
						this->camera->look_at_coord(builder->position.to_scene3());
					}
					else {
						log::log(WARN << "[FVD/camera] missing starting Builder for player="
						              << this->fvd_assigned_player);
					}
				}
			}
		}
		this->fvd_gameplay_active = true;
		if (this->fvd_lobby_hosting and this->time_loop) {
			// Only the authority advances game rules.
			this->time_loop->get_clock()->resume();
		}
	}
	if ((state.phase == "error" or state.phase == "menu")
	    and this->fvd_gameplay_active) {
		this->fvd_gameplay_active = false;
		if (this->fvd_lobby_hosting and this->time_loop) {
			this->time_loop->get_clock()->pause();
		}
	}
	if (this->fvd_gameplay_active and this->fvd_lobby_hosting) {
		for (auto player : state.disconnected_players) {
			if (std::find(this->fvd_recorded_departures.begin(),
			              this->fvd_recorded_departures.end(), player)
			    == this->fvd_recorded_departures.end()) {
				this->simulation->report_network_participant_disconnect(player);
				this->fvd_recorded_departures.push_back(player);
			}
		}
		for (auto &command : this->fvd_lobby_client->take_host_commands()) {
			const auto command_id = command.intent.command_id;
			const auto identity = command.identity;
			const auto generation = this->fvd_lobby_client->get_session_generation();
			auto reply = [this, command_id, identity, generation](bool accepted) {
				network::CommandResult result{};
				result.command_id = command_id;
				result.status = accepted ? network::CommandStatus::ACCEPTED
				                         : network::CommandStatus::REJECTED;
				result.world_revision = this->fvd_lobby_client->status().snapshot_revision;
				result.simulation_time = this->time_loop->get_clock()->get_time();
				if (not accepted) result.reason = "Host rejected command (ownership or target)";
				// A callback from a prior match, or a Builder who already left,
				// must never deliver a result into a new authenticated session.
				this->fvd_lobby_client->send_command_result(
					identity, generation, std::move(result));
			};
			auto live_session = [this, identity, generation]() {
				return this->fvd_lobby_client->accepts_host_command(identity, generation);
			};
			if (not this->simulation->submit_network_command(
				std::move(command), reply, std::move(live_session))) reply(false);
		}
	}
	if (this->fvd_gameplay_active and not this->fvd_lobby_hosting) {
		for (const auto &decision : this->fvd_lobby_client->take_command_results()) {
			if (decision.status == network::CommandStatus::REJECTED) {
				log::log(WARN << "FVD host rejected command #"
				              << decision.command_id << ": " << decision.reason);
			}
		}
	}
	// Capture live authoritative world states on the simulation owner thread;
	// the relay worker transmits recipient deltas while the host simulation
	// is running. Guest simulation remains paused, using replicated visuals.
	if (this->simulation and this->fvd_lobby_hosting
	    and this->fvd_bootstrap_supplied
	    and (state.phase == "bootstrap" or state.phase == "streaming"
	     or state.phase == "playing")
	    and state.role_seconds_remaining == 0 and state.remote_player_id != 0) {
		try {
			if (this->fvd_update_requested) {
				auto world = this->simulation->take_network_update();
				if (world) {
					// A Builder can leave while their capture is in flight.
					if (not state.multi_host
					    or std::find(state.remote_player_ids.begin(),
					                 state.remote_player_ids.end(), this->fvd_capture_recipient)
					       != state.remote_player_ids.end()) {
						this->fvd_lobby_client->provide_host_update(std::move(*world));
					}
					this->fvd_update_requested = false;
				}
			}
			if (not this->fvd_update_requested) {
				const auto now = std::chrono::steady_clock::now();
				auto interval = this->fvd_update_interval;
				if (state.multi_host and not state.remote_player_ids.empty()) {
					interval = std::max(std::chrono::milliseconds{10},
						std::chrono::duration_cast<std::chrono::milliseconds>(
							this->fvd_update_interval / state.remote_player_ids.size()));
				}
				if (now - this->fvd_last_update_request >= interval) {
					auto recipient = state.remote_player_id;
					if (state.multi_host) {
						recipient = state.remote_player_ids[
							this->fvd_capture_cursor++ % state.remote_player_ids.size()];
					}
					uint64_t min_sent = this->fvd_lobby_client->delivered_projectile_id(recipient);
					if (state.multi_host) {
						for (auto id : state.remote_player_ids) {
							min_sent = std::min(
								min_sent, this->fvd_lobby_client->delivered_projectile_id(id));
						}
					}
					this->simulation->request_network_update(
						recipient, this->fvd_next_world_revision++,
						this->fvd_lobby_client->delivered_projectile_id(recipient),
						min_sent);
					this->fvd_capture_recipient = recipient;
					this->fvd_update_requested = true;
					this->fvd_last_update_request = now;
				}
			}
		}
		catch (const std::exception &error) {
			this->fvd_lobby_local_error = error.what();
		}
	}
	auto update = [&](const char *key, const QVariant &value) {
		if (this->fvd_lobby_state->value(key) != value) {
			this->fvd_lobby_state->insert(key, value);
		}
	};
	update("phase", QString::fromStdString(
		this->fvd_lobby_local_error.empty() ? state.phase : "error"));
	update("status", QString::fromStdString(
		this->fvd_lobby_local_error.empty() ? state.message : this->fvd_lobby_local_error));
	update("participants", QString::fromStdString(state.participants));
	update("rooms", QString::fromStdString(state.rooms));
	if (state.suggested_room_id != this->fvd_lobby_suggested_room_id) {
		this->fvd_lobby_suggested_room_id = state.suggested_room_id;
		update("roomId", state.suggested_room_id == 0
		                   ? QString{}
		                   : QString::number(state.suggested_room_id));
	}
	update("canStart", state.can_start);
	update("roles", QString::fromStdString(state.roles));
	update("localRole", QString::fromStdString(state.local_role));
	update("roleSeconds", static_cast<int>(state.role_seconds_remaining));
	update("worldRevision", static_cast<qulonglong>(state.snapshot_revision));
	update("orderFeedback", QString::fromStdString(state.order_feedback));
	update("departureNotice", QString::fromStdString(state.departure_notice));
	update("pendingOrders", static_cast<int>(state.pending_order_count));
	update("builderDisconnected",
	       not state.connected and state.local_role == "HUNTER"
	       and state.remote_player_id == 0 and state.phase == "playing");
}

network::WorldState Presenter::fvd_local_view(const network::WorldState &world, size_t controlled) {
	const auto role = controlled == 1 ? network::PlayerRole::HUNTER : network::PlayerRole::BUILDER;
	auto fog = std::find_if(world.fog.begin(), world.fog.end(), [&](const auto &entry) {
		return entry.team == role;
	});
	if (fog == world.fog.end()) {
		throw Error{MSG(err) << "FVD presentation snapshot lacks the viewer's fog"};
	}
	auto own_side = [&](gamestate::player_id_t owner) {
		return owner != 0 and (owner == 1) == (controlled == 1);
	};
	auto visible = [&](const coord::phys3 &position) {
		return network::fog_tile_set(fog->visible, world.map_size, position.to_tile());
	};
	const std::unordered_set<gamestate::entity_id_t> remembered{
		fog->remembered.begin(), fog->remembered.end()};

	network::WorldState view = world;
	std::erase_if(view.entities, [&](const network::EntityState &entity) {
		// Only the Hunter sees its rally point marker.
		if (entity.definition == "fvd.data.entities.HunterRallyPoint") {
			return role != network::PlayerRole::HUNTER;
		}
		return not own_side(entity.owner_id) and not visible(entity.position)
		       and not remembered.contains(entity.entity_id);
	});
	// Draw only what this snapshot shows. An entity the simulation created
	// after the snapshot appears once the next snapshot includes it.
	std::unordered_set<uint32_t> shown;
	for (const auto &entity : view.entities) {
		shown.insert(static_cast<uint32_t>(entity.entity_id));
	}
	for (const auto &effect : this->fvd_combat_visuals) {
		if (effect.render_entity) shown.insert(effect.render_entity->get_id());
	}
	this->world_renderer->set_shown(std::move(shown));
	// A guest only knows a remembered building's last place, not its state.
	for (auto &entity : view.entities) {
		if (not own_side(entity.owner_id) and not visible(entity.position)) {
			entity = network::EntityState{entity.entity_id, entity.owner_id, entity.definition, entity.position};
			entity.remembered = true;
		}
	}
	if (role == network::PlayerRole::HUNTER) {
		std::unordered_set<gamestate::entity_id_t> shown;
		for (const auto &entity : view.entities) {
			shown.insert(entity.entity_id);
		}
		// Plans and orders must only reference entities this view shows, or
		// the construction and order presentations reject the whole snapshot.
		std::erase_if(view.reservations, [&](const auto &reservation) {
			return not network::fog_tile_set(fog->visible, world.map_size, reservation.tile)
			       or not shown.contains(reservation.builder_id);
		});
		std::erase_if(view.constructions, [&](const auto &construction) {
			return not shown.contains(construction.site_entity_id)
			       or not shown.contains(construction.builder_id);
		});
		std::erase_if(view.attack_orders, [&](const auto &order) {
			return not shown.contains(order.horse_id)
			       or (order.target_id and not shown.contains(*order.target_id));
		});
	}
	else {
		view.attack_orders.clear();
	}
	view.fog = {*fog};
	view.fog.front().remembered.clear();
	view.local_player_id = controlled;
	return view;
}

void Presenter::update_fvd_fog(const network::WorldState *world) {
	std::unordered_set<uint32_t> dimmed;
	if (world) {
		for (const auto &entity : world->entities) {
			if (entity.remembered) {
				dimmed.insert(static_cast<uint32_t>(entity.entity_id));
			}
		}
	}
	this->world_renderer->set_dimmed(std::move(dimmed));
	if (not world or world->fog.size() != 1) {
		if (not this->fvd_fog_levels.empty()) {
			this->fvd_fog_levels.clear();
			this->terrain_renderer->set_fog(1, {});
		}
		return;
	}
	const auto &fog = world->fog.front();
	const size_t tiles = static_cast<size_t>(world->map_size) * world->map_size;
	std::vector<uint8_t> levels(tiles, 0);
	for (size_t i = 0; i < tiles; ++i) {
		const bool explored = (fog.explored[i / 64] >> (i % 64)) & 1;
		const bool visible = (fog.visible[i / 64] >> (i % 64)) & 1;
		levels[i] = visible ? 255 : explored ? 128 : 0;
	}
	if (levels != this->fvd_fog_levels) {
		this->terrain_renderer->set_fog(world->map_size, levels);
		this->fvd_fog_levels = std::move(levels);
	}
}

void Presenter::record_confirmed_combat_effects(
	const network::WorldState &world,
	bool reset,
	const time::time_t &presentation_time) {
	if (reset) {
		this->fvd_previous_combat_world.reset();
		this->clear_fvd_combat_visuals();
	}
	// A lethal state transition can be captured twice at the same simulation
	// time. Diff every confirmed state; identical states produce no effects.
	if (this->fvd_previous_combat_world) {
		const auto confirmed = network::confirmed_combat_effects(
			*this->fvd_previous_combat_world, world);
		const auto now = std::chrono::steady_clock::now();
		for (const auto &effect : confirmed) {
			if (this->fvd_combat_visuals.size() >= 512) break;
			std::shared_ptr<renderer::world::RenderEntity> render_entity{};
			if (effect.death
			    and effect.definition == "fvd.data.entities.Horse"
			    and this->world_renderer) {
				const auto &paths = this->resolve_fvd_sprite_paths(
					"fvd.data.entities.Horse.Death");
				render_entity = std::make_shared<renderer::world::RenderEntity>(false);
				this->world_renderer->add_render_entity(render_entity);
				render_entity->update_sample(
					effect.target_id, effect.position, paths, presentation_time,
					std::nullopt, {}, presentation_time);
				render_entity->update_facing(
					effect.angle, presentation_time, {});
				render_entity->set_occlusion_mode(
					renderer::world::occlusion_mode::OUTLINED, effect.owner_id);
			}
			this->fvd_combat_visuals.push_back({
				effect.position, effect.death, now, presentation_time, std::move(render_entity)});
		}
	}
	this->fvd_previous_combat_world = world;
}

void Presenter::clear_fvd_combat_visuals() {
	for (auto &effect : this->fvd_combat_visuals) {
		if (effect.render_entity) effect.render_entity->deactivate(0);
	}
	this->fvd_combat_visuals.clear();
}

const std::vector<std::string> &Presenter::resolve_fvd_sprite_paths(
	const std::string &ability) {
	auto path = this->fvd_guest_sprite_paths.find(ability);
	if (path != this->fvd_guest_sprite_paths.end()) return path->second;

	// The source attribute is relative to its nyan namespace. The API returns
	// the asset-root-relative path expected by AssetManager.
	auto view = this->simulation->get_game()->get_state()->get_db_view();
	const auto ability_object = view->get_object(ability);
	const auto animated = gamestate::api::APIAbility::get_property(
		ability_object, gamestate::api::ability_property_t::ANIMATED);
	const auto animations = gamestate::api::APIAbilityProperty::get_animations(animated);
	auto paths = gamestate::api::APIAnimation::get_animation_paths(animations);
	if (paths.empty()) {
		throw Error{MSG(err) << "FVD entity has no animation: " << ability};
	}
	std::sort(paths.begin(), paths.end());
	for (const auto &sprite_path : paths) {
		const auto resolved = this->root_dir / "assets" / "converted" / sprite_path;
		if (not resolved.is_file()) {
			throw Error{MSG(err) << "FVD entity sprite not found: " << resolved};
		}
	}
	return this->fvd_guest_sprite_paths.emplace(ability, std::move(paths)).first->second;
}

void Presenter::refresh_fvd_guest_visuals() {
	if (not this->fvd_lobby_enabled or this->fvd_lobby_hosting
	    or not this->fvd_replication or not this->world_renderer) return;
	auto replicated = this->fvd_replication->get_presentation_state();
	if (not replicated) {
		return;
	}
	const auto *world = &replicated->world;
	const bool reset = replicated->snapshot_generation != this->fvd_guest_snapshot_generation;
	if (not reset and world->world_revision == this->fvd_guest_visual_revision) {
		return;
	}
	const auto received_at = std::chrono::steady_clock::now();
	const auto previous_time = this->fvd_guest_clock.update(
		world->simulation_time, received_at, reset);
	const auto presentation_time = this->fvd_guest_clock.get_time(received_at);
	this->record_confirmed_combat_effects(*world, reset, presentation_time);
	if (reset) this->fvd_guest_late_arrows.clear();
	for (const auto &shot : world->arrows) {
		if (shot.arrow_id <= this->fvd_last_guest_arrow_id) continue;
		this->fvd_last_guest_arrow_id = shot.arrow_id;
		// A launch fully elapsed between recipient captures. Replay the
		// shot's visual just once, never its authoritative impact damage.
		if (shot.impact_time <= presentation_time
		    and this->fvd_guest_late_arrows.size() < network::MAX_STATE_ARROWS) {
			this->fvd_guest_late_arrows.push_back({shot, received_at});
		}
	}

	// The guest's paused local simulation loads terrain/assets only.
	// Turn off its independent units before creating authoritative proxies.
	if (not this->fvd_guest_visuals_initialized) {
		for (const auto &[id, entity] :
		     this->simulation->get_game()->get_state()->get_game_entities()) {
			if (auto visual = entity->get_render_entity()) visual->deactivate(0);
		}
		// Local presentation filters the shared world stage to its fog-visible
		// entity IDs. Guest snapshots are already filtered by the host, so drop
		// that filter before the first replicated proxy is rendered.
		this->world_renderer->set_shown(std::nullopt);
		this->fvd_guest_visuals_initialized = true;
	}
	std::unordered_set<gamestate::entity_id_t> present;
	for (const auto &entity : world->entities) {
		// Only render known FVD types; do not accept arbitrary sprite
		// filenames supplied in network messages.
		if (entity.definition != "fvd.data.entities.Builder"
		    and entity.definition != "fvd.data.entities.Horse"
		    and entity.definition != "fvd.data.entities.Tower"
		    and entity.definition != "fvd.data.entities.HunterTower"
		    and entity.definition != "fvd.data.entities.HunterRallyPoint"
		    and entity.definition != "fvd.data.entities.TowerDemolition"
		    and entity.definition != "fvd.data.entities.Foundation") {
			continue;
		}
		const bool walking = entity.animation_action == gamestate::AnimationAction::MOVE;
		const bool attacking = entity.animation_action == gamestate::AnimationAction::ATTACK;
		if (walking and entity.definition != "fvd.data.entities.Builder"
		    and entity.definition != "fvd.data.entities.Horse") {
			throw Error{MSG(err) << "FVD replica has an invalid moving entity"};
		}
		if (attacking and entity.definition != "fvd.data.entities.Horse") {
			throw Error{MSG(err) << "FVD replica has an invalid attacking entity"};
		}
		const bool movable = entity.definition == "fvd.data.entities.Builder"
		                     or entity.definition == "fvd.data.entities.Horse";
		const std::string cache_key = entity.definition
		                              + (attacking ? ".Attack"
		                                           : walking ? ".Move" : ".Idle");
		const auto &paths = this->resolve_fvd_sprite_paths(cache_key);
		present.insert(entity.entity_id);
		auto &visual = this->fvd_guest_entities[entity.entity_id];
		const bool new_entity = not visual;
		if (not visual) {
			// Clicks pass through the rally point marker, as on the host.
			const bool pickable = entity.definition != "fvd.data.entities.HunterRallyPoint";
			visual = std::make_shared<renderer::world::RenderEntity>(pickable);
			this->world_renderer->add_render_entity(visual);
			// Replicated proxies do not pass through the simulation's normal
			// health-bar registration path.
			if (this->hud_renderer) this->hud_renderer->add_health_entity(visual);
		}
		visual->set_occlusion_mode(
			movable ? renderer::world::occlusion_mode::OUTLINED
			        : renderer::world::occlusion_mode::OCCLUDER,
			entity.owner_id);
		std::vector<std::pair<time::time_t, coord::phys3>> future_motion{};
		future_motion.reserve(entity.motion_keys.size());
		for (const auto &key : entity.motion_keys) {
			// The renderer follows the host's actual event-scheduled path.
			// Ordinary position samples remain authoritative corrections.
			if (key.time > world->simulation_time) {
				future_motion.emplace_back(key.time, key.position);
			}
		}
		visual->update_sample(
			entity.entity_id, entity.position, paths, world->simulation_time,
			new_entity or not previous_time ? std::nullopt
			                                : std::make_optional(presentation_time),
			future_motion, entity.animation_started_at);
		std::vector<std::pair<time::time_t, coord::phys_angle_t>> turns;
		turns.reserve(entity.facing_keys.size());
		for (const auto &key : entity.facing_keys) {
			turns.emplace_back(key.time, key.angle);
		}
		visual->update_facing(entity.angle, world->simulation_time, turns);
		if (entity.health and entity.maximum_health and *entity.maximum_health > 0) {
			curve::Segmented<int64_t> health{nullptr, 0, "", nullptr, *entity.health};
			visual->update_health(health, *entity.maximum_health, world->simulation_time);
		}
	}
	for (auto it = this->fvd_guest_entities.begin();
	     it != this->fvd_guest_entities.end();) {
		if (not present.contains(it->first)) {
			it->second->deactivate(0);
			it = this->fvd_guest_entities.erase(it);
		}
		else ++it;
	}
	this->fvd_guest_visual_revision = world->world_revision;
	this->fvd_guest_snapshot_generation = replicated->snapshot_generation;
}

void Presenter::init_input() {
	log::log(INFO << "Presenter: Initializing input subsystem...");

	this->input_manager = std::make_shared<input::InputManager>();

	this->window->add_key_callback([&](const QKeyEvent &ev) {
		// Hidden multiplayer lobby can be reopened without leaving the
		// current process. A restarted match still needs fresh simulation.
		if (this->fvd_gameplay_active and ev.type() == QEvent::KeyRelease
		    and ev.key() == Qt::Key_F10) {
			this->handle_fvd_lobby_action("disconnect");
			return;
		}
		if (this->fvd_lobby_enabled and not this->fvd_gameplay_active) {
			// GuiRendererImpl already forwards every key to its QQuickWindow.
			// Forwarding here too makes each character appear twice in TextInput.
			// Do not dispatch lobby keystrokes to gameplay bindings either.
			return;
		}
		this->input_manager->process(ev);
	});
	this->window->add_mouse_button_callback([&](const QMouseEvent &ev) {
		if (this->fvd_lobby_enabled and not this->fvd_gameplay_active) {
			// GuiRendererImpl forwards mouse buttons to QQuickWindow already.
			// Only perform presenter-owned button hit testing once here.
			if (ev.button() == Qt::LeftButton) {
				const auto action = this->fvd_lobby_action_at(ev.position());
				if (ev.type() == QEvent::MouseButtonPress) {
					this->fvd_lobby_pressed = action;
				}
				else if (ev.type() == QEvent::MouseButtonRelease) {
					if (not action.empty() and action == this->fvd_lobby_pressed) {
						this->handle_fvd_lobby_action(action);
					}
					this->fvd_lobby_pressed.clear();
				}
			}
			return;
		}
		using HudAction = HudMouseCapture::Action;
		auto hovered_action = HudAction::NONE;
		// Hit-test buttons independently of the panel bounds. Qt Quick can
		// place later Column children outside the parent's stale geometry
		// during a visibility/layout update; those buttons must still work.
		if (this->fvd_hud_state and this->gui) {
			for (const auto &[name, action] : {
			         std::pair{"fvdActionButton", HudAction::PRIMARY},
			         std::pair{"fvdPatrolButton", HudAction::PATROL},
			         std::pair{"fvdCancelButton", HudAction::CANCEL},
			         std::pair{"fvdAggressiveButton", HudAction::AGGRESSIVE},
			         std::pair{"fvdDefensiveButton", HudAction::DEFENSIVE},
			         std::pair{"fvdObserveButton", HudAction::OBSERVE}}) {
				if (this->gui->contains_item(name, ev.position())) {
					hovered_action = action;
					break;
				}
			}
		}
		const bool over_panel = this->fvd_hud_state and this->gui
		                        and this->gui->contains_item("fvdBuildPanel", ev.position());
		const bool over_hud = over_panel or hovered_action != HudAction::NONE;
		const auto hud_input = this->fvd_hud_capture.process(
			ev.type(), ev.button(), over_hud, hovered_action);
		// An offscreen Qt Quick view cannot be relied upon to deliver a
		// MouseArea click. Deliver the panel's action once at the presenter
		// boundary while preventing that gesture from selecting the world.
		if (this->gui) {
			this->gui->get_input_handler()->process(
				std::shared_ptr<QEvent>{ev.clone()});
		}
		if (this->game_controller) {
			switch (hud_input.activated_action) {
			case HudAction::PRIMARY:
				if (this->game_controller->get_controlled() == 1) {
					this->game_controller->begin_attack_move();
				}
				else if (this->game_controller->is_tower_placement_active()) {
					this->game_controller->cancel_tower_placement();
				}
				else {
					this->game_controller->begin_tower_placement();
				}
				break;
			case HudAction::PATROL:
				this->game_controller->begin_patrol();
				break;
			case HudAction::CANCEL:
				this->game_controller->cancel_hunter_targeting();
				break;
			case HudAction::AGGRESSIVE:
				this->game_controller->issue_hunter_stance(gamestate::HunterStance::AGGRESSIVE);
				break;
			case HudAction::DEFENSIVE:
				this->game_controller->issue_hunter_stance(gamestate::HunterStance::DEFENSIVE);
				break;
			case HudAction::OBSERVE:
				this->game_controller->issue_hunter_stance(gamestate::HunterStance::OBSERVE);
				break;
			case HudAction::NONE:
				break;
			}
		}
		if (hud_input.consumed) {
			if (this->game_controller) {
				this->game_controller->reset_drag_select();
			}
			return;
		}
		this->input_manager->process(ev);
	});
	this->window->add_mouse_move_callback([&](const QMouseEvent &ev) {
		if (this->fvd_lobby_enabled and not this->fvd_gameplay_active) {
			if (this->gui) {
				this->gui->get_input_handler()->process(
					std::shared_ptr<QEvent>{ev.clone()});
			}
			return;
		}
		this->placement_cursor = coord::input{
			static_cast<coord::pixel_t>(ev.position().x()),
			static_cast<coord::pixel_t>(ev.position().y())};
		this->input_manager->set_mouse(ev.position().x(), ev.position().y());
		if (this->gui) {
			this->gui->get_input_handler()->process(
				std::shared_ptr<QEvent>{ev.clone()});
		}
		const bool over_hud = this->fvd_hud_state and this->gui
		                      and this->gui->contains_item("fvdBuildPanel", ev.position());
		if (not over_hud and not this->fvd_hud_capture.is_capturing()) {
			this->input_manager->process(ev);
		}
	});
	this->window->add_mouse_wheel_callback([&](const QWheelEvent &ev) {
		if (this->fvd_lobby_enabled and not this->fvd_gameplay_active) {
			// Scroll forwarding is also owned by GuiRendererImpl.
			return;
		}
		if (this->gui) {
			this->gui->get_input_handler()->process(
				std::shared_ptr<QEvent>{ev.clone()});
		}
		if (this->fvd_hud_state and this->gui
		    and this->gui->contains_item("fvdBuildPanel", ev.position())) {
			return;
		}
		this->input_manager->process(ev);
	});

	auto input_ctx = this->input_manager->get_global_context();
	input::setup_defaults(input_ctx);

	// setup simulation controls
	if (this->simulation) {
		log::log(INFO << "Loading game simulation controls");

		// TODO: Remove hardcoding
		auto game_controller = std::make_shared<input::game::Controller>(
			std::unordered_set<size_t>{0, 1, 2, 3}, 0);
		auto fvd = this->simulation->get_game()->get_state()->get_fvd();
		if (fvd) {
			std::unordered_set<size_t> players;
			if (this->fvd_replication) {
				auto assigned = this->fvd_replication->get_local_player_id();
				if (assigned > fvd->get_player_count()) {
					throw Error{MSG(err) << "FVD assigned player is outside the current match"};
				}
				players.insert(assigned);
			}
			else {
				for (size_t id = 1; id <= fvd->get_player_count(); ++id) {
					players.insert(id);
				}
			}
			for (size_t id : players) {
				input_ctx->bind(
					input::Event{input::event_class::KEYBOARD, static_cast<int>(Qt::Key_0 + id),
					             Qt::KeyboardModifier::NoModifier, QEvent::KeyRelease},
					input::input_action{input::input_action_t::GAME});
			}
			game_controller = std::make_shared<input::game::Controller>(
				players, this->fvd_replication
					? this->fvd_replication->get_local_player_id()
					: 2);

			// The global input context must forward placement keys to
			// the FVD-specific game binding context.
			for (auto key : {Qt::Key_B, Qt::Key_A, Qt::Key_P, Qt::Key_Z, Qt::Key_X, Qt::Key_C,
			                 Qt::Key_Delete, Qt::Key_Escape, Qt::Key_F9}) {
				input_ctx->bind(
					input::Event{input::event_class::KEYBOARD, key,
					             Qt::KeyboardModifier::NoModifier, QEvent::KeyRelease},
					input::input_action{input::input_action_t::GAME});
			}
		}
		auto engine_context = std::make_shared<input::game::BindingContext>();
		input::game::setup_defaults(
			engine_context, game_controller, this->time_loop, this->simulation, this->camera,
			[this](
				const coord::input &position,
				const std::function<void(std::optional<gamestate::entity_id_t>)> &callback) {
				this->world_renderer->pick_entity(
					position,
					[callback](std::optional<uint32_t> id) {
						if (id.has_value()) {
							callback(static_cast<gamestate::entity_id_t>(*id));
							return;
						}
						callback(std::nullopt);
					});
			},
			[this](network::CommandIntent intent) {
				if (not this->fvd_lobby_enabled or this->fvd_lobby_hosting)
					return false; // Authority uses local simulation controls.
				if (this->fvd_gameplay_active and this->fvd_lobby_client) {
					if (not this->fvd_lobby_client->send_guest_command(std::move(intent))) {
						log::log(WARN << "FVD guest command queue rejected an input");
					}
				}
				return true; // Never issue guest orders to local simulation.
			},
			[this]() -> std::optional<network::WorldState> {
				if (this->fvd_lobby_hosting or not this->fvd_gameplay_active
				    or not this->fvd_lobby_client) return std::nullopt;
				auto tracker = this->fvd_lobby_client->get_replication();
				return tracker ? tracker->get_world_state() : std::nullopt;
			},
			this->fvd_lobby_enabled,
			[this]() {
				return not this->fvd_lobby_enabled or this->fvd_lobby_hosting;
			});
		this->input_manager->set_game_controller(game_controller);
		this->game_controller = game_controller;
		input_ctx->set_game_bindings(engine_context);
		if (fvd and this->gui) {
			this->fvd_hud_state = create_property_map();
			this->fvd_hud_state->insert("controlled", static_cast<int>(game_controller->get_controlled()));
			this->fvd_hud_state->insert("selection", QString{"No unit selected"});
			this->fvd_hud_state->insert("isTower", false);
			this->fvd_hud_state->insert("isFoundation", false);
			this->fvd_hud_state->insert("health", QString{"—"});
			this->fvd_hud_state->insert("damage", QString{"Not defined"});
			this->fvd_hud_state->insert("armor", QString{"Not defined"});
			this->fvd_hud_state->insert("canBuild", false);
			this->fvd_hud_state->insert("placementFootprint", QVariantList{});
			this->fvd_hud_state->insert("siteMarkers", QVariantList{});
			this->fvd_hud_state->insert("reservedFootprints", QVariantList{});
			this->fvd_hud_state->insert("placementValid", false);
			this->fvd_hud_state->insert("queuedTowers", 0);
			this->fvd_hud_state->insert("constructionStatus", QString{});
			this->fvd_hud_state->insert("constructionPercent", -1);
			this->fvd_hud_state->insert("canAttack", false);
			this->fvd_hud_state->insert("attacking", false);
			this->fvd_hud_state->insert("patrolling", false);
			this->fvd_hud_state->insert("stance", QString{});
			this->fvd_hud_state->insert("placing", false);
			this->fvd_hud_state->insert("kills", 0);
			this->fvd_hud_state->insert("killsAvailable", !this->fvd_replication);
			this->fvd_hud_state->insert("killsLabel", QString{});
			this->fvd_hud_state->insert("arrowStreaks", QVariantList{});
			this->fvd_hud_state->insert("combatEffects", QVariantList{});
			this->fvd_hud_state->insert("attackOrderMarkers", QVariantList{});
			this->fvd_hud_state->insert("hunterCountdown", 0);
			this->fvd_hud_state->insert("matchTimeRemaining", -1);
			this->fvd_hud_state->insert("matchEnded", false);
			this->fvd_hud_state->insert("matchResultTitle", QString{});
			this->fvd_hud_state->insert("matchResultReason", QString{});
			this->gui->set_context_property("fvdHud", this->fvd_hud_state.get());
			if (this->fvd_lobby_enabled) {
				this->fvd_lobby_state = create_property_map();
				// Store lobby preferences per OS user, independently of the
				// working directory or installed asset location. QML edits the
				// property map; the valueChanged signal is emitted for those
				// edits, not for our C++ status/room-ID updates.
				QSettings saved{QSettings::IniFormat, QSettings::UserScope,
				                QStringLiteral("openage"), QStringLiteral("fvd")};
				for (const auto &[key, default_value] : {
					std::pair{"address", "127.0.0.1"},
					std::pair{"port", "32456"},
					std::pair{"roomName", "FVD Game"},
					std::pair{"maxPlayers", "2"},
					std::pair{"roomId", ""},
					std::pair{"name", "Player"},
				}) {
					this->fvd_lobby_state->insert(
						key, saved.value(QStringLiteral("lobby/") + QString::fromUtf8(key),
						                 QString::fromUtf8(default_value)).toString());
				}
				// This optional access credential must not be persisted as
				// unencrypted preferences (QSettings is not a password vault).
				this->fvd_lobby_state->insert("token", QString{});
				for (const auto &[key, value] : {
					std::pair{"phase", "menu"},
					std::pair{"status", "Host a game or refresh the relay room list."},
					std::pair{"participants", ""},
					std::pair{"rooms", ""},
				}) {
					this->fvd_lobby_state->insert(key, QString::fromUtf8(value));
				}
				QObject::connect(this->fvd_lobby_state.get(), &QQmlPropertyMap::valueChanged,
				                 this->fvd_lobby_state.get(),
				                 [](const QString &key, const QVariant &value) {
					// Only user-editable, non-sensitive fields are retained.
					// In particular, never store a private relay access token.
					if (key != QStringLiteral("address")
					    and key != QStringLiteral("port")
					    and key != QStringLiteral("roomName")
					    and key != QStringLiteral("maxPlayers")
					    and key != QStringLiteral("roomId")
					    and key != QStringLiteral("name")) return;
					QSettings settings{QSettings::IniFormat, QSettings::UserScope,
					                   QStringLiteral("openage"), QStringLiteral("fvd")};
					settings.setValue(QStringLiteral("lobby/") + key,
					                  value.toString().left(256));
				});
				this->fvd_lobby_state->insert("canStart", false);
				this->fvd_lobby_state->insert("roles", QString{});
				this->fvd_lobby_state->insert("localRole", QString{});
				this->fvd_lobby_state->insert("roleSeconds", 0);
				this->fvd_lobby_state->insert("worldRevision", static_cast<qulonglong>(0));
				this->fvd_lobby_state->insert("orderFeedback", QString{});
				this->fvd_lobby_state->insert("departureNotice", QString{});
				this->fvd_lobby_state->insert("pendingOrders", 0);
				this->fvd_lobby_state->insert("builderDisconnected", false);
				this->fvd_lobby_state->insert("enabled", true);
				this->gui->set_context_property("fvdLobby", this->fvd_lobby_state.get());
			}
		}
	}

	// attach GUI if it's initialized
	if (this->gui) {
		log::log(INFO << "Loading GUI controls");
		this->input_manager->set_gui(this->gui->get_input_handler());
	}

	// setup camera controls
	if (this->camera) {
		log::log(INFO << "Loading camera controls");
		auto camera_controller = std::make_shared<input::camera::Controller>();
		auto camera_context = std::make_shared<input::camera::BindingContext>();
		input::camera::setup_defaults(camera_context, this->camera, this->camera_manager);
		this->input_manager->set_camera_controller(camera_controller);
		input_ctx->set_camera_bindings(camera_context);
	}

	// setup HUD controls
	if (this->hud_renderer) {
		log::log(INFO << "Loading HUD controls");
		auto hud_controller = std::make_shared<input::hud::Controller>();
		auto hud_context = std::make_shared<input::hud::BindingContext>();
		input::hud::setup_defaults(hud_context, this->hud_renderer);
		this->input_manager->set_hud_controller(hud_controller);
		input_ctx->set_hud_bindings(hud_context);
	}

	log::log(INFO << "Presenter: Input subsystem initialized");
}

void Presenter::init_final_render_pass() {
	// Final output to window
	this->screen_renderer = std::make_shared<renderer::screen::ScreenRenderStage>(
		this->window,
		this->renderer,
		this->root_dir["assets"]["shaders"]);
	std::vector<std::shared_ptr<renderer::RenderTarget>> targets{};
	for (auto pass : this->render_passes) {
		targets.push_back(pass->get_target());
	}
	this->screen_renderer->set_render_targets(targets);
	this->render_passes.push_back(this->screen_renderer->get_render_pass());

	// Update final render pass if the textures are reassigned on resize
	// TODO: This REQUIRES that all other render passes have already been
	//       resized
	this->window->add_resize_callback([this](size_t, size_t, double /*scale*/) {
		// Acquire the render targets for all previous passes
		std::vector<std::shared_ptr<renderer::RenderTarget>> targets{};
		for (size_t i = 0; i < this->render_passes.size() - 1; ++i) {
			targets.push_back(this->render_passes[i]->get_target());
		}
		this->screen_renderer->set_render_targets(targets);
	});
}

void Presenter::render() {
	this->refresh_fvd_lobby_state();
	if (this->fvd_reset_pending) return;
	this->refresh_fvd_guest_visuals();
	const std::optional<time::time_t> guest_time = this->fvd_replication ? std::make_optional(this->fvd_guest_clock.get_time(std::chrono::steady_clock::now())) : std::nullopt;
	// TODO: Pass current time to update() instead of fetching it in renderer
	{
		util::StallProfiler profile{"render.camera_update"};
		this->camera_manager->update();
	}
	{
		util::StallProfiler profile{"render.terrain_update"};
		this->terrain_renderer->update();
	}
	{
		util::StallProfiler profile{"render.world_update"};
		this->world_renderer->update(guest_time);
	}
	if (this->game_controller) {
		auto selected = this->game_controller->get_selection_details();
		if (this->fvd_hud_state
		    and (selected.kind == input::game::SelectionKind::TOWER
		         or selected.kind == input::game::SelectionKind::FOUNDATION)
		    and selected.ids.size() == 1) {
			// A demolished (or combat-destroyed) tower can remain selected
			// until the next click. Clear it only after authoritative state
			// confirms removal, never when sending an unacknowledged order.
			auto world = this->fvd_replication
				? this->fvd_replication->get_world_state()
				: std::optional<network::WorldState>{};
			const auto local = this->fvd_replication
				? nullptr : this->simulation->get_fvd_presentation_state();
			const network::WorldState *authoritative = world ? &*world : local.get();
			if (authoritative and std::none_of(
			        authoritative->entities.begin(), authoritative->entities.end(),
			        [&](const network::EntityState &entity) {
				        return entity.entity_id == selected.ids.front();
			        })) {
				this->game_controller->set_selected({});
				selected = this->game_controller->get_selection_details();
			}
		}
		this->hud_renderer->set_selected(selected.ids);
		if (this->fvd_hud_state) {
			auto controlled = this->game_controller->get_controlled();
			auto tower = selected.kind == input::game::SelectionKind::TOWER;
			auto builder = selected.kind == input::game::SelectionKind::BUILDER;
			auto hunter_horse = selected.kind == input::game::SelectionKind::HORSE
			                 or selected.kind == input::game::SelectionKind::GROUP_HORSES;
			QString selection_label{"No unit selected"};
			if (selected.ids.size() > 1) {
				selection_label = QString{"%1 units selected"}.arg(static_cast<int>(selected.ids.size()));
			}
			else if (builder) {
				selection_label = QString{"Builder"};
			}
			else if (tower) {
				selection_label = QString{"Tower"};
			}
			else if (selected.kind == input::game::SelectionKind::FOUNDATION) {
				selection_label = QString{"Foundation"};
			}
			else if (selected.kind == input::game::SelectionKind::HUNTER_TOWER) {
				selection_label = QString{"Hunter tower — right-click to set rally point"};
			}
			else if (hunter_horse) {
				selection_label = QString{"Horse"};
			}
			else if (not selected.ids.empty()) {
				selection_label = QString{"Unit selected"};
			}

			QString health{"—"};
			if (selected.render_entity and selected.render_entity->is_active()
			    and selected.render_entity->has_health()) {
				auto current = selected.render_entity->get_health_at(
					this->time_loop->get_clock()->get_time());
				health = QString{"%1 / %2"}
				             .arg(static_cast<qlonglong>(current))
				             .arg(selected.render_entity->get_max_health());
			}
			// Guest render entities are lightweight proxies; health is supplied
			// by the authoritative state rather than the paused local world.
			if (this->fvd_replication and selected.ids.size() == 1) {
				auto replicated = this->fvd_replication->get_world_state();
				if (replicated) {
					auto id = selected.ids.front();
					auto entity = std::find_if(
						replicated->entities.begin(), replicated->entities.end(),
						[id](const network::EntityState &entry) {
							return entry.entity_id == id;
						});
					if (entity != replicated->entities.end()
					    and entity->health and entity->maximum_health) {
						health = QString{"%1 / %2"}
						             .arg(static_cast<qlonglong>(*entity->health))
						             .arg(static_cast<qlonglong>(*entity->maximum_health));
					}
				}
			}
			this->fvd_hud_state->insert("controlled", static_cast<int>(controlled));
			this->fvd_hud_state->insert("selection", selection_label);
			this->fvd_hud_state->insert("isTower", tower);
			this->fvd_hud_state->insert("isFoundation",
				selected.kind == input::game::SelectionKind::FOUNDATION);
			this->fvd_hud_state->insert("health", health);
			this->fvd_hud_state->insert("canBuild", controlled >= 2 and builder);
			this->fvd_hud_state->insert("canAttack", controlled == 1 and hunter_horse);
			this->fvd_hud_state->insert("attacking", this->game_controller->is_attack_move_active());
			this->fvd_hud_state->insert("patrolling", this->game_controller->is_patrol_active());
			const auto drag_start = this->game_controller->get_drag_select_start_optional();
			const bool preview_line = controlled >= 2 and builder and drag_start.has_value()
				// Shift-drag can begin without pressing B first.
				and (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
			auto fvd = this->simulation->get_game()->get_state()->get_fvd();
			if (fvd) {
				auto now = this->time_loop->get_clock()->get_time();
				auto replica_for_hud = this->fvd_replication ? this->fvd_replication->get_world_state() : std::nullopt;
				const auto local_hud = this->fvd_replication ? nullptr : this->simulation->get_fvd_presentation_state();
				// Show the local player only what their side can see, exactly as
				// a guest of the host would; hidden entities are not drawn.
				std::optional<network::WorldState> local_view{};
				if (local_hud) {
					local_view = this->fvd_local_view(*local_hud, controlled);
				}
				const network::WorldState *construction_world = replica_for_hud ? &*replica_for_hud
				                                              : local_view ? &*local_view : nullptr;
				const auto result = construction_world
					? construction_world->match_outcome : std::nullopt;
				const bool game_over = result.has_value();
				QString result_title{};
				QString result_reason{};
				if (game_over) {
					const bool hunter_won = result->winner == network::MatchWinner::HUNTER;
					const bool local_hunter = controlled == 1;
					result_title = hunter_won == local_hunter
						? QString{"VICTORY"} : QString{"DEFEAT"};
					switch (result->reason) {
					case network::MatchEndReason::ALL_BUILDERS_DEAD:
						result_reason = QString{"All Builders have been eliminated"};
						break;
					case network::MatchEndReason::TIME_LIMIT:
						result_reason = QString{"Builders survived for 45 minutes"};
						break;
					}
				}
				this->fvd_hud_state->insert("matchEnded", game_over);
				this->fvd_hud_state->insert("matchResultTitle", result_title);
				this->fvd_hud_state->insert("matchResultReason", result_reason);
				if (not this->fvd_replication and local_view) {
					this->record_confirmed_combat_effects(
						*local_view,
						false,
						this->time_loop->get_clock()->get_real_time());
				}
				this->update_fvd_fog(construction_world);
				// The Hunter's own horse stances arrive in the same snapshot locally
				// and on guests; a group with different stances reads as Mixed.
				QString stance_label{};
				if (controlled == 1 and hunter_horse and construction_world) {
					std::optional<gamestate::HunterStance> common{};
					bool mixed = false;
					for (auto id : selected.ids) {
						auto entity = std::find_if(
							construction_world->entities.begin(), construction_world->entities.end(),
							[id](const network::EntityState &entry) {
								return entry.entity_id == id;
							});
						if (entity == construction_world->entities.end() or not entity->hunter_stance) {
							continue;
						}
						mixed = mixed or (common and *common != *entity->hunter_stance);
						common = entity->hunter_stance;
					}
					if (mixed) {
						stance_label = QString{"Mixed"};
					}
					else if (common) {
						switch (*common) {
						case gamestate::HunterStance::OBSERVE:
							stance_label = QString{"Observe"};
							break;
						case gamestate::HunterStance::AGGRESSIVE:
							stance_label = QString{"Aggressive"};
							break;
						case gamestate::HunterStance::DEFENSIVE:
							stance_label = QString{"Defensive"};
							break;
						}
					}
				}
				this->fvd_hud_state->insert("stance", stance_label);
				std::vector<gamestate::FVD::ArrowTrail> arrows{};
				if (this->fvd_replication) {
					// Replicate each launch once. The guest animates the flight and
					// hides it at impact_time without an arrow removal packet.
					auto replicated = this->fvd_replication->get_world_state();
					if (replicated) {
						now = *guest_time;
						arrows.reserve(replicated->arrows.size()
						               + this->fvd_guest_late_arrows.size());
						auto add_visual_arc = [&](const network::ArrowState &arrow,
						                          double progress) {
							auto position_at = [&](double fraction) {
								auto lerp = [fraction](coord::phys_t a, coord::phys_t b) {
									return coord::phys_t::from_double(
										a.to_double() + (b.to_double() - a.to_double()) * fraction);
								};
								return coord::phys3{
									lerp(arrow.start.ne, arrow.destination.ne),
									lerp(arrow.start.se, arrow.destination.se),
									lerp(arrow.start.up, arrow.destination.up)
										+ coord::phys_t::from_double(2.0 * fraction * (1.0 - fraction))};
							};
							arrows.push_back({
								position_at(std::max(0.0, progress - 0.22)),
								position_at(progress), progress, arrow.owner_id});
						};
						for (const auto &arrow : replicated->arrows) {
							if (now >= arrow.impact_time
							    and now < arrow.impact_time + time::time_t::from_double(0.12)) {
								arrows.push_back({arrow.destination, arrow.destination,
								                  1.0, arrow.owner_id, true});
								continue;
							}
							if (not network::arrow_visible_at(arrow, now)) continue;
							const auto duration = (arrow.impact_time - arrow.launch_time).to_double();
							if (duration > 0.0) {
								const auto progress = std::clamp(
									(now - arrow.launch_time).to_double() / duration, 0.0, 1.0);
								add_visual_arc(arrow, progress);
							}
						}
						// A late launch record is rendered on its original
						// 0.2s flight curve, shifted to the first receipt
						// time so slow recipients can still see the shot.
						// This does NOT alter the authoritative hit time.
						const auto received_now = std::chrono::steady_clock::now();
						std::erase_if(this->fvd_guest_late_arrows,
							[&](const LateGuestArrow &late) {
								const auto duration =
									(late.shot.impact_time - late.shot.launch_time).to_double();
								const auto elapsed = std::chrono::duration<double>(
									received_now - late.first_seen).count();
								return duration <= 0.0 or elapsed >= duration + 0.12;
							});
						for (const auto &late : this->fvd_guest_late_arrows) {
							const auto duration =
								(late.shot.impact_time - late.shot.launch_time).to_double();
							const auto elapsed = std::chrono::duration<double>(
								received_now - late.first_seen).count();
							if (elapsed >= duration) {
								arrows.push_back({late.shot.destination, late.shot.destination,
								                  1.0, late.shot.owner_id, true});
							}
							else {
								add_visual_arc(late.shot, std::clamp(elapsed / duration, 0.0, 1.0));
							}
						}
					}
				}
				else {
					arrows = fvd->get_visible_arrows(now);
				}
				QVariantList arrow_streaks;
				arrow_streaks.reserve(arrows.size());
				if (not arrows.empty()) {
					const Eigen::Matrix4f camera_matrix = this->camera->get_projection_matrix()
					                                     * this->camera->get_view_matrix();
					const auto viewport = this->camera->get_viewport_size();
					const auto screen_width = static_cast<float>(viewport[0]);
					const auto screen_height = static_cast<float>(viewport[1]);
					auto project = [&](const coord::phys3 &position) {
						const auto world = position.to_scene3().to_world_space();
						const Eigen::Vector4f clip = camera_matrix
						                           * Eigen::Vector4f{world.x(), world.y(), world.z(), 1.0f};
						return Eigen::Vector2f{
							(clip.x() / clip.w() * 0.5f + 0.5f) * screen_width,
							(0.5f - clip.y() / clip.w() * 0.5f) * screen_height};
					};
					for (const auto &arrow : arrows) {
						auto tail = project(arrow.tail);
						auto head = project(arrow.head);
						// Keep QML Canvas work bounded to visible shots.
						if (not std::isfinite(head.x()) or not std::isfinite(head.y())
						    or (head.x() < -100 and tail.x() < -100)
						    or (head.x() > screen_width + 100 and tail.x() > screen_width + 100)
						    or (head.y() < -100 and tail.y() < -100)
						    or (head.y() > screen_height + 100 and tail.y() > screen_height + 100)) {
							continue;
						}
						arrow_streaks.push_back(QVariantMap{
							{"x0", tail.x()}, {"y0", tail.y()},
							{"x1", head.x()}, {"y1", head.y()},
							{"alpha", arrow.impact ? 0.9 : std::min(1.0, (1.0 - arrow.progress) * 4.0)},
							{"hunter", arrow.owner == 1},
							{"impact", arrow.impact}});
					}
				}
				// No Canvas repaint when nothing is on screen.
				if (not arrow_streaks.empty()
				    or not this->fvd_hud_state->value("arrowStreaks").toList().empty()) {
					this->fvd_hud_state->insert("arrowStreaks", arrow_streaks);
				}
				// Use the same projection as projectiles, but emit flashes
				// only after confirmed HP changes / authoritative removals.
				const auto visual_now = std::chrono::steady_clock::now();
				const auto visual_presentation_time = guest_time.value_or(
					this->time_loop->get_clock()->get_real_time());
				std::erase_if(this->fvd_combat_visuals, [&](CombatVisual &fx) {
					const auto elapsed = fx.render_entity
					                       ? (visual_presentation_time - fx.presentation_started).to_double()
					                       : std::chrono::duration<double>(visual_now - fx.started).count();
					const auto duration = fx.render_entity ? HORSE_DEATH_VISUAL_SECONDS
					                                       : fx.death ? 0.72 : 0.36;
					if (elapsed < duration) return false;
					if (fx.render_entity) fx.render_entity->deactivate(0);
					return true;
				});
				QVariantList combat_effects;
				if (not this->fvd_combat_visuals.empty()) {
					const auto fx_matrix = this->camera->get_projection_matrix()
					                     * this->camera->get_view_matrix();
					const auto fx_viewport = this->camera->get_viewport_size();
					for (const auto &fx : this->fvd_combat_visuals) {
						const auto elapsed = std::chrono::duration<double>(
							visual_now - fx.started).count();
						const auto duration = fx.death ? 0.72 : 0.36;
						if (elapsed >= duration) continue;
						auto world_pos = fx.position;
						world_pos.up += coord::phys_t::from_double(fx.death ? 0.5 : 0.8);
						const auto world_point = world_pos.to_scene3().to_world_space();
						const auto clip = fx_matrix * Eigen::Vector4f{
							world_point.x(), world_point.y(), world_point.z(), 1.0f};
						if (clip.w() <= 0) continue;
						const float x = (clip.x() / clip.w() * 0.5f + 0.5f) * fx_viewport[0];
						const float y = (0.5f - clip.y() / clip.w() * 0.5f) * fx_viewport[1];
						if (x < -40 or y < -40 or x > fx_viewport[0] + 40
						    or y > fx_viewport[1] + 40) continue;
						const double progress = std::clamp(elapsed / duration, 0.0, 1.0);
						combat_effects.push_back(QVariantMap{
							{"x", x}, {"y", y}, {"death", fx.death},
							{"alpha", 1.0 - progress},
							{"radius", (fx.death ? 19.0 : 9.0) * (0.55 + progress)}});
					}
				}
				if (not combat_effects.empty()
				    or not this->fvd_hud_state->value("combatEffects").toList().empty()) {
					this->fvd_hud_state->insert("combatEffects", combat_effects);
				}
				// World-space tile projection is shared by placement and
				// authoritative construction/reservation overlays.
				const Eigen::Matrix4f camera_matrix =
					this->camera->get_projection_matrix() * this->camera->get_view_matrix();
				const auto viewport = this->camera->get_viewport_size();
				auto project_position = [&](const coord::phys3 &position) {
					const auto world = position.to_scene3().to_world_space();
					const Eigen::Vector4f clip = camera_matrix
						* Eigen::Vector4f{world.x(), world.y(), world.z(), 1.0f};
					return Eigen::Vector2f{
						(clip.x() / clip.w() * 0.5f + 0.5f) * viewport[0],
						(0.5f - clip.y() / clip.w() * 0.5f) * viewport[1]};
				};
				auto project_tile = [&](double ne, double se) {
					return project_position({
						coord::phys_t::from_double(ne),
						coord::phys_t::from_double(se), 0});
				};
				QVariantList attack_order_markers;
				if (construction_world
				    and (not this->fvd_lobby_enabled or this->fvd_gameplay_active)) {
					for (const auto &order : network::make_attack_order_presentation(
					         *construction_world, guest_time.value_or(now))) {
						const auto horse = project_position(order.horse_position);
						const auto marker = project_position(order.marker_position);
						if ((horse.x() < -90 or horse.y() < -90
						     or horse.x() > viewport[0] + 90
						     or horse.y() > viewport[1] + 90)
						    and (marker.x() < -90 or marker.y() < -90
						         or marker.x() > viewport[0] + 90
						         or marker.y() > viewport[1] + 90)) {
							continue;
						}
						attack_order_markers.push_back(QVariantMap{
							{"x0", horse.x()}, {"y0", horse.y()},
							{"x1", marker.x()}, {"y1", marker.y()},
							{"target", order.target_id.has_value()},
							{"focus", order.focus_target},
							{"reached", order.reached_destination},
						});
					}
				}
				if (not attack_order_markers.isEmpty()
				    or not this->fvd_hud_state->value("attackOrderMarkers").toList().empty()) {
					this->fvd_hud_state->insert("attackOrderMarkers", attack_order_markers);
				}
				QVariantList footprint;
				bool valid = false;
				if ((this->game_controller->is_tower_placement_active() or preview_line)
				    and controlled >= 2 and builder
				    and this->placement_cursor and selected.ids.size() == 1) {
					auto mouse = this->placement_cursor->to_phys3(this->camera);
					auto sites = preview_line
						? input::game::tower_line_sites(
							drag_start->to_phys3(this->camera).to_tile(), mouse.to_tile())
						: std::vector<coord::tile>{mouse.to_tile()};
					valid = construction_world != nullptr
					    and std::any_of(construction_world->entities.begin(),
					                    construction_world->entities.end(),
					                    [&](const network::EntityState &entry) {
						                    return entry.entity_id == selected.ids.front()
						                        and entry.owner_id == controlled
						                        and entry.definition == "fvd.data.entities.Builder";
					                    });
					for (const auto &site : sites) {
						for (const auto &tile : fvd->get_tower_footprint(site)) {
							if (construction_world
							    and (tile.ne < 0 or tile.se < 0
							         or tile.ne >= construction_world->map_size
							         or tile.se >= construction_world->map_size
							         or std::any_of(construction_world->reservations.begin(),
							                        construction_world->reservations.end(),
							                        [&](const network::TileReservation &reserved) {
								                        return reserved.tile == tile;
							                        })
							         or std::any_of(construction_world->entities.begin(),
							                        construction_world->entities.end(),
							                        [&](const network::EntityState &entry) {
								                        return entry.definition != "fvd.data.entities.TowerDemolition"
								                            and entry.position.to_tile() == tile;
							                        }))) {
								valid = false;
							}
							const auto a = project_tile(tile.ne, tile.se);
							const auto b = project_tile(tile.ne + 1.0, tile.se);
							const auto c = project_tile(tile.ne + 1.0, tile.se + 1.0);
							const auto d = project_tile(tile.ne, tile.se + 1.0);
							footprint.push_back(QVariantMap{
								{"ax", a.x()}, {"ay", a.y()},
								{"bx", b.x()}, {"by", b.y()},
								{"cx", c.x()}, {"cy", c.y()},
								{"dx", d.x()}, {"dy", d.y()}});
						}
					}
				}
				if (not footprint.isEmpty()
				    or not this->fvd_hud_state->value("placementFootprint").toList().empty()) {
					this->fvd_hud_state->insert("placementFootprint", footprint);
				}
				this->fvd_hud_state->insert("placementValid", valid);
				// Both the Hunter and every Builder see the same authoritative
				// world sites. Local snapshots and guest replication each contain
				// a complete construction transition from the simulation thread.
				network::ConstructionPresentation construction_view{};
				if (construction_world) {
					construction_view = network::make_construction_presentation(*construction_world);
				}
				QVariantList reserved_footprints;
				for (const auto &reserved : construction_view.reservations) {
					const auto &tile = reserved.tile;
					const auto center = project_tile(tile.ne + 0.5, tile.se + 0.5);
					if (center.x() < -90 or center.y() < -90
					    or center.x() > viewport[0] + 90
					    or center.y() > viewport[1] + 90) continue;
					const auto a = project_tile(tile.ne, tile.se);
					const auto b = project_tile(tile.ne + 1.0, tile.se);
					const auto c = project_tile(tile.ne + 1.0, tile.se + 1.0);
					const auto d = project_tile(tile.ne, tile.se + 1.0);
					reserved_footprints.push_back(QVariantMap{
						{"ax", a.x()}, {"ay", a.y()},
						{"bx", b.x()}, {"by", b.y()},
						{"cx", c.x()}, {"cy", c.y()},
						{"dx", d.x()}, {"dy", d.y()}});
				}
				QVariantList site_markers;
				for (const auto &site : construction_view.sites) {
					const auto center = project_tile(site.site.ne + 0.5, site.site.se + 0.5);
					if (center.x() < -90 or center.y() < -90
					    or center.x() > viewport[0] + 90
					    or center.y() > viewport[1] + 90) continue;
					QString phase{};
					switch (site.phase) {
					case network::SitePresentationPhase::QUEUED:
						phase = "Queued";
						break;
					case network::SitePresentationPhase::ORPHANED:
						phase = "Orphaned";
						break;
					case network::SitePresentationPhase::PLANNED:
						phase = "Planned";
						break;
					case network::SitePresentationPhase::BUILDING:
						phase = "Building";
						break;
					case network::SitePresentationPhase::COMPLETE:
						phase = "Tower";
						break;
					}
					site_markers.push_back(QVariantMap{
						{"x", center.x()}, {"y", center.y()},
						{"owner", static_cast<int>(site.owner_id)}, {"phase", phase},
						{"queueNumber", site.owner_id == controlled
						    ? static_cast<int>(site.queue_position) : 0}});
				}
				if (not reserved_footprints.isEmpty()
				    or not this->fvd_hud_state->value("reservedFootprints").toList().empty()) {
					this->fvd_hud_state->insert("reservedFootprints", reserved_footprints);
				}
				if (not site_markers.isEmpty()
				    or not this->fvd_hud_state->value("siteMarkers").toList().empty()) {
					this->fvd_hud_state->insert("siteMarkers", site_markers);
				}
				QString construction_status{};
				int construction_percent = -1;
				int queued_towers = 0;
				if (builder and controlled >= 2 and selected.ids.size() == 1) {
					if (construction_world) {
						auto builder_id = selected.ids.front();
						for (const auto &site : construction_view.sites) {
							if (site.phase == network::SitePresentationPhase::QUEUED
							    and site.builder_id == builder_id
							    and site.owner_id == controlled) ++queued_towers;
						}
						for (const auto &order : construction_world->constructions) {
							if (order.builder_id != builder_id
							    or order.owner_id != controlled) continue;
							if (order.phase == network::ConstructionPhase::PLANNED) {
								if (construction_status.isEmpty())
									construction_status = QString{"Walking to tower site"};
							}
							else {
								construction_percent = order.progress_percent(now);
								construction_status = QString{"Building: %1%"}
									.arg(construction_percent);
							}
						}
					}
				}
				this->fvd_hud_state->insert("queuedTowers", queued_towers);
				this->fvd_hud_state->insert("constructionStatus", construction_status);
				this->fvd_hud_state->insert("constructionPercent", construction_percent);
				auto countdown = replica_for_hud
					? gamestate::hunter_spawn_countdown(
						replica_for_hud->first_hunter_spawn_time, *guest_time)
					: fvd->get_hunter_spawn_countdown(now);
				this->fvd_hud_state->insert("hunterCountdown",
					countdown.value_or(0));
				std::optional<time::time_t> match_deadline{};
				if (construction_world) {
					// Network matches start at simulation time zero. Authority
					// handoff pauses and preserves that shared clock.
					match_deadline = this->fvd_replication
						? std::make_optional(time::time_t::from_int(
						      gamestate::FVD_MATCH_DURATION_SECONDS))
						: fvd->get_match_end_deadline();
				}
				const auto match_time = guest_time.value_or(now);
				const auto match_countdown = gamestate::match_end_countdown(
					match_deadline, match_time);
				this->fvd_hud_state->insert(
					"matchTimeRemaining",
					game_over ? -1 : match_countdown.value_or(-1));
				QString kills_label{};
				std::optional<uint64_t> visible_kills{};
				if (builder and selected.ids.size() == 1 and controlled >= 2) {
					kills_label = QString{"Total kills: "};
					if (this->fvd_replication) {
						visible_kills = this->fvd_replication->get_local_kills();
					}
					else {
						visible_kills = fvd->get_kills(controlled);
					}
				}
				else if (tower and selected.ids.size() == 1 and controlled >= 2) {
					kills_label = QString{"Tower kills: "};
					if (this->fvd_replication) {
						visible_kills = this->fvd_replication->get_tower_kills(
							selected.ids.front());
					}
					else {
						visible_kills = fvd->get_tower_kills(selected.ids.front(), controlled);
					}
				}
				this->fvd_hud_state->insert("killsLabel", kills_label);
				this->fvd_hud_state->insert("killsAvailable", visible_kills.has_value());
				this->fvd_hud_state->insert("kills",
					static_cast<qulonglong>(visible_kills.value_or(0)));
				this->fvd_hud_state->insert("damage",
					static_cast<qlonglong>(fvd->get_tower_damage()));
				this->fvd_hud_state->insert("armor",
					static_cast<qlonglong>(fvd->get_tower_armor()));
			}
			this->fvd_hud_state->insert(
				"placing", this->game_controller->is_tower_placement_active() or preview_line);
		}
	}
	{
		util::StallProfiler profile{"render.hud_update"};
		this->hud_renderer->update(guest_time);
	}
	{
		util::StallProfiler profile{"render.gui"};
		this->gui->render();
	}

	{
		util::StallProfiler profile{"render.passes"};
		for (auto &pass : this->render_passes) {
			this->renderer->render(pass);
		}
	}
}

} // namespace openage::presenter
