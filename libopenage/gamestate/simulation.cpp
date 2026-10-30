// Copyright 2013-2024 the openage authors. See copying.md for legal info.

#include "simulation.h"

#include <algorithm>
#include <limits>

#include <utility>
#include "error/error.h"

#include <chrono>
#include <thread>

#include "assets/mod_manager.h"
#include "event/event_loop.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/types.h"
#include "gamestate/game_entity.h"
#include "gamestate/entity_factory.h"
#include "gamestate/event/drag_select.h"
#include "gamestate/event/process_command.h"
#include "gamestate/event/send_command.h"
#include "gamestate/event/spawn_entity.h"
#include "gamestate/event/wait.h"
#include "gamestate/terrain_factory.h"
#include "time/clock.h"
#include "time/time_loop.h"

// TODO
#include "gamestate/fvd.h"
#include "network/command_bridge.h"
#include "network/match_connection_state.h"
#include "gamestate/game.h"
#include "gamestate/game_state.h"

namespace openage::gamestate {

size_t prune_unassigned_fvd_builders(
	const std::shared_ptr<GameState> &state,
	size_t admitted_players,
	const time::time_t &at) {
	if (not state or not state->get_fvd()
	    or admitted_players < 2
	    or admitted_players > state->get_fvd()->get_player_count()) {
		throw Error{MSG(err) << "Invalid FVD participant count for Builder pruning"};
	}
	std::vector<entity_id_t> unused{};
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() != "fvd.data.entities.Builder") continue;
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (ownership and ownership->get_owners().get(at) > admitted_players) {
			unused.push_back(id);
		}
	}
	for (auto id : unused) {
		state->remove_game_entity(id, at);
	}
	return unused.size();
}

GameSimulation::GameSimulation(const util::Path &root_dir,
                               const std::shared_ptr<cvar::CVarManager> &cvar_manager,
                               const std::shared_ptr<openage::time::TimeLoop> time_loop) :
	running{false},
	root_dir{root_dir},
	cvar_manager{cvar_manager},
	time_loop{time_loop},
	event_loop{std::make_shared<openage::event::EventLoop>()},
	entity_factory{std::make_shared<gamestate::EntityFactory>()},
	terrain_factory{std::make_shared<gamestate::TerrainFactory>()},
	mod_manager{std::make_shared<assets::ModManager>(this->root_dir / "assets" / "converted")},
	spawner{std::make_shared<gamestate::event::Spawner>(this->event_loop)},
	commander{std::make_shared<gamestate::event::Commander>(this->event_loop)} {
	auto mods = mod_manager->enumerate_modpacks(root_dir / "assets" / "converted");
	for (const auto &mod : mods) {
		this->mod_manager->register_modpack(mod);
	}
	for (const auto &mod : mod_manager->enumerate_modpacks(root_dir / "assets")) {
		this->mod_manager->register_modpack(mod);
	}

	log::log(MSG(info) << "Created game simulation");
}


void GameSimulation::run() {
	{
		std::shared_lock lock{this->mutex};
		if (not this->game) {
			throw Error{MSG(err) << "Cannot run an uninitialized game simulation"};
		}
	}

	log::log(MSG(info) << "Game simulation started");

	while (this->running.load()) {
		bool reset = false;
		{
			std::lock_guard reset_lock{this->reset_mutex};
			reset = std::exchange(this->reset_requested, false);
		}
		if (reset) {
			if (this->time_loop->get_clock()->get_state() != time::ClockState::PAUSED
			    or this->time_loop->get_clock()->get_time() != time::TIME_ZERO) {
				throw Error{MSG(err) << "Cannot recreate FVD match without paused time zero"};
			}
			{
				std::unique_lock game_lock{this->mutex};
				// GameState and the event loop are owned exclusively by this
				// thread. The presenter clears GPU draw calls beforehand and
				// reattaches renderer resources after the replacement is ready.
				this->event_loop = std::make_shared<openage::event::EventLoop>();
				this->entity_factory = std::make_shared<gamestate::EntityFactory>();
				this->terrain_factory = std::make_shared<gamestate::TerrainFactory>();
				this->spawner = std::make_shared<gamestate::event::Spawner>(this->event_loop);
				this->commander = std::make_shared<gamestate::event::Commander>(this->event_loop);
				this->init_event_handlers();
				this->game = std::make_shared<gamestate::Game>(
					this->event_loop, this->mod_manager,
					this->entity_factory, this->terrain_factory);
				if (auto fvd = this->game->get_state()->get_fvd()) {
					fvd->start_hunter_wave(
						this->game->get_state(), time::TIME_ZERO);
				}
				{
					std::lock_guard presentation_lock{this->presentation_mutex};
					this->presentation_requested = true;
				}
				this->update_fvd_presentation_state(time::TIME_ZERO);
			}
			{
				std::lock_guard reset_lock{this->reset_mutex};
				this->reset_completed = true;
			}
			continue;
		}
		time::time_t current_time = this->time_loop->get_clock()->get_time();
		// The base FVD scenario creates all seven Builders. A smaller online
		// room must not leave unattended, attackable Builder units in unused
		// player slots. This mutation occurs only on the simulation owner
		// thread and only once before recipient snapshots are captured.
		size_t prune_above = 0;
		{
			std::lock_guard lock{this->bootstrap_mutex};
			if (this->network_prune_unassigned_builders) {
				prune_above = this->network_admitted_players;
				this->network_prune_unassigned_builders = false;
			}
		}
		if (prune_above > 0) {
			const auto removed = prune_unassigned_fvd_builders(
				this->game->get_state(), prune_above, current_time);
			log::log(INFO << "[FVD/match] starting_builders_pruned"
			              << " participants=" << prune_above
			              << " expected_builders=" << prune_above - 1
			              << " removed=" << removed);
		}
		// A Hunter may remain in the match after every Builder disconnects.
		// There is then no live recipient whose projectile receipt must
		// be preserved. Continue bounded local visual retention instead of
		// accumulating an indefinite unacknowledged network history.
		bool no_live_builders = false;
		{
			std::lock_guard lock{this->bootstrap_mutex};
			if (this->network_admitted_players > 1) {
				no_live_builders = true;
				for (player_id_t player = 2;
				     player <= this->network_admitted_players; ++player) {
					if (std::find(this->network_disconnected_players.begin(),
					              this->network_disconnected_players.end(), player)
					    == this->network_disconnected_players.end()) {
						no_live_builders = false;
						break;
					}
				}
			}
		}
		if (no_live_builders) {
			this->game->get_state()->get_fvd()->prune_delivered_projectiles(
				current_time, std::numeric_limits<uint64_t>::max());
		}
		// Only the simulation owner thread may submit authenticated network
		// commands into the mutable game event loop.
		std::deque<NetworkCommandRequest> incoming;
		{
			std::lock_guard lock{this->network_command_mutex};
			incoming.swap(this->network_commands);
		}
		for (auto &request : incoming) {
			// Connection teardown and F10 can race an already-drained command.
			// Authorize again at the simulation owner-thread boundary, before
			// mutating the authoritative world.
			if (request.session_is_active and not request.session_is_active()) {
				request.callback(false);
				continue;
			}
			network::submit_authenticated_command(
				request.command, this->event_loop, this->commander,
				this->game->get_state(), current_time, std::move(request.callback));
		}
		this->event_loop->reach_time(current_time, this->game->get_state());
		// Evaluate wins only on the simulation owner thread after all due
		// combat/build events. The first result becomes immutable and appears
		// in both the host presentation and subsequent recipient captures.
		if (auto fvd = this->game->get_state()->get_fvd()) {
			std::vector<player_id_t> departed{};
			{
				std::lock_guard lock{this->bootstrap_mutex};
				departed = this->network_disconnected_players;
			}
			fvd->evaluate_match_outcome(this->game->get_state(), current_time, departed);
		}
		this->update_fvd_presentation_state(current_time);
		// Capture only on the simulation owner thread: entity and event state
		// must not be enumerated concurrently from the Qt/render thread.
		std::optional<BootstrapRequest> bootstrap{};
		{
			std::lock_guard lock{this->bootstrap_mutex};
			bootstrap = std::exchange(this->bootstrap_requested, std::nullopt);
		}
		if (bootstrap) {
			auto fvd = this->game->get_state()->get_fvd();
			if (not fvd) {
				throw Error{MSG(err) << "Network bootstrap requires an FVD scenario"};
			}
			network::MatchConnectionState connections{fvd->get_player_count()};
			auto world = network::capture_fvd_state(
				this->game->get_state(), connections, current_time, 0, 1, bootstrap->recipient);
			// Fail at the first snapshot instead of shipping ghost starting
			// Builders if the lobby admission/pruning handshake regresses.
			size_t expected_builders{};
			{
				std::lock_guard lock{this->bootstrap_mutex};
				expected_builders = this->network_admitted_players - 1;
			}
			const auto actual_builders = std::count_if(
				world.entities.begin(), world.entities.end(),
				[](const network::EntityState &entity) {
					return entity.definition == "fvd.data.entities.Builder";
				});
			if (actual_builders != expected_builders) {
				throw Error{MSG(err) << "FVD initial world has " << actual_builders
				                     << " Builders but expected " << expected_builders
				                     << " for the admitted lobby"};
			}
			std::lock_guard lock{this->bootstrap_mutex};
			if (this->network_admitted_players > 0) {
				for (auto &player : world.players) {
					player.connected = player.player_id <= this->network_admitted_players
						&& std::find(this->network_disconnected_players.begin(),
						             this->network_disconnected_players.end(), player.player_id)
						   == this->network_disconnected_players.end();
				}
			}
			if (bootstrap->generation == this->bootstrap_generation) {
				this->bootstrap_ready = std::move(world);
			}
		}
		// Later authoritative captures use the same simulation-thread boundary
		// as bootstrap, so entity/event data is never enumerated by the
		// presenter or relay workers.
		std::optional<UpdateRequest> update{};
		{
			std::lock_guard lock{this->bootstrap_mutex};
			update = std::exchange(this->update_requested, std::nullopt);
		}
		if (update) {
			auto fvd = this->game->get_state()->get_fvd();
			if (not fvd) {
				throw Error{MSG(err) << "Network update requires an FVD scenario"};
			}
			network::MatchConnectionState connections{fvd->get_player_count()};
			fvd->prune_delivered_projectiles(current_time, update->delivered_to_all);
			auto world = network::capture_fvd_state(
				this->game->get_state(), connections, current_time,
				0, update->revision, update->recipient,
				update->delivered_projectile_id);
			std::lock_guard lock{this->bootstrap_mutex};
			if (this->network_admitted_players > 0) {
				for (auto &player : world.players) {
					player.connected = player.player_id <= this->network_admitted_players
						&& std::find(this->network_disconnected_players.begin(),
						             this->network_disconnected_players.end(), player.player_id)
						   == this->network_disconnected_players.end();
				}
			}
			if (update->generation == this->bootstrap_generation) {
				this->update_ready = std::move(world);
			}
		}
		if (this->time_loop->get_clock()->get_state() != time::ClockState::RUNNING) {
			std::this_thread::sleep_for(std::chrono::milliseconds{10});
		}
	}
	log::log(MSG(info) << "Game simulation loop exited");
}


void GameSimulation::initialize() {
	std::unique_lock lock{this->mutex};
	if (this->game) {
		throw Error{MSG(err) << "Game simulation is already initialized"};
	}

	this->init_event_handlers();

	this->game = std::make_shared<gamestate::Game>(event_loop,
	                                               this->mod_manager,
	                                               this->entity_factory,
	                                               this->terrain_factory);

	if (auto fvd = this->game->get_state()->get_fvd()) {
		fvd->start_hunter_wave(this->game->get_state(),
		                       this->time_loop->get_clock()->get_time());
	}
	this->update_fvd_presentation_state(this->time_loop->get_clock()->get_time());
	this->running.store(true);
	log::log(MSG(info) << "Game simulation initialized");
}


void GameSimulation::stop() {
	if (this->running.exchange(false)) {
		log::log(MSG(info) << "Game simulation stopped");
	}
}


const util::Path &GameSimulation::get_root_dir() {
	std::shared_lock lock{this->mutex};

	return this->root_dir;
}

const std::shared_ptr<cvar::CVarManager> GameSimulation::get_cvar_manager() {
	std::shared_lock lock{this->mutex};

	return this->cvar_manager;
}

const std::shared_ptr<gamestate::Game> GameSimulation::get_game() {
	std::shared_lock lock{this->mutex};

	return this->game;
}

const std::shared_ptr<openage::event::EventLoop> GameSimulation::get_event_loop() {
	std::shared_lock lock{this->mutex};

	return this->event_loop;
}

const std::shared_ptr<gamestate::event::Spawner> GameSimulation::get_spawner() {
	std::shared_lock lock{this->mutex};

	return this->spawner;
}

const std::shared_ptr<gamestate::event::Commander> GameSimulation::get_commander() {
	std::shared_lock lock{this->mutex};

	return this->commander;
}

void GameSimulation::attach_renderer(const std::shared_ptr<renderer::RenderFactory> &render_factory) {
	std::unique_lock lock{this->mutex};

	this->game->attach_renderer(render_factory);
	this->entity_factory->attach_renderer(render_factory);
	this->terrain_factory->attach_renderer(render_factory);
}

void GameSimulation::configure_network_participants(size_t admitted_players) {
	if (admitted_players < 2 or admitted_players > network::MAX_STATE_PLAYERS) {
		throw Error{MSG(err) << "FVD network match needs 2–8 admitted players"};
	}
	std::lock_guard lock{this->bootstrap_mutex};
	if (this->network_admitted_players != 0
	    and this->network_admitted_players != admitted_players) {
		throw Error{MSG(err) << "Cannot change admitted player slots during a match"};
	}
	this->network_admitted_players = admitted_players;
	this->network_prune_unassigned_builders = true;
}

void GameSimulation::report_network_participant_disconnect(player_id_t player) {
	std::lock_guard lock{this->bootstrap_mutex};
	if (this->network_admitted_players == 0 or player == 0
	    or player > this->network_admitted_players) return;
	if (std::find(this->network_disconnected_players.begin(),
	              this->network_disconnected_players.end(), player)
	    == this->network_disconnected_players.end()) {
		this->network_disconnected_players.push_back(player);
	}
}

void GameSimulation::request_network_bootstrap(player_id_t recipient) {
	if (recipient == 0 or recipient > network::MAX_STATE_PLAYERS) {
		throw Error{MSG(err) << "Invalid FVD bootstrap recipient"};
	}
	if (this->time_loop->get_clock()->get_state() != time::ClockState::PAUSED) {
		throw Error{MSG(err) << "FVD bootstrap requires a paused authoritative simulation"};
	}
	std::lock_guard lock{this->bootstrap_mutex};
	if (this->network_admitted_players < 2
	    or recipient > this->network_admitted_players) {
		throw Error{MSG(err) << "Cannot snapshot a network match before its"
		                     << " admitted participant count is configured"};
	}
	if (this->bootstrap_requested or this->bootstrap_ready) {
		throw Error{MSG(err) << "FVD bootstrap capture already pending"};
	}
	this->bootstrap_requested = BootstrapRequest{recipient, this->bootstrap_generation};
}

std::optional<network::WorldState> GameSimulation::take_network_bootstrap() {
	std::lock_guard lock{this->bootstrap_mutex};
	return std::exchange(this->bootstrap_ready, std::nullopt);
}

void GameSimulation::request_network_update(
	player_id_t recipient, uint64_t revision,
	uint64_t delivered_projectile_id, uint64_t delivered_to_all) {
	if (recipient == 0 or recipient > network::MAX_STATE_PLAYERS or revision < 2) {
		throw Error{MSG(err) << "Invalid FVD recipient or world revision"};
	}
	std::lock_guard lock{this->bootstrap_mutex};
	if (this->update_requested or this->update_ready) {
		throw Error{MSG(err) << "FVD world update capture already pending"};
	}
	this->update_requested = UpdateRequest{
		recipient, revision, this->bootstrap_generation,
		delivered_projectile_id, delivered_to_all};
}

std::optional<network::WorldState> GameSimulation::take_network_update() {
	std::lock_guard lock{this->bootstrap_mutex};
	return std::exchange(this->update_ready, std::nullopt);
}

std::shared_ptr<const network::WorldState> GameSimulation::get_fvd_presentation_state() {
	std::lock_guard lock{this->presentation_mutex};
	this->presentation_requested = true;
	return this->presentation_state;
}

void GameSimulation::update_fvd_presentation_state(const time::time_t &time) {
	{
		std::lock_guard lock{this->presentation_mutex};
		if (not std::exchange(this->presentation_requested, false)) {
			return;
		}
	}
	std::shared_ptr<const network::WorldState> snapshot{};
	if (this->game->get_state()->get_fvd()) {
		snapshot = std::make_shared<const network::WorldState>(
			network::capture_fvd_presentation_state(this->game->get_state(), time));
	}
	std::lock_guard lock{this->presentation_mutex};
	this->presentation_state = std::move(snapshot);
}

bool GameSimulation::submit_network_command(
	network::AuthenticatedCommand command, std::function<void(bool)> callback,
	std::function<bool()> session_is_active) {
	if (not callback) return false;
	std::lock_guard lock{this->network_command_mutex};
	if (this->network_commands.size() >= 256) return false;
	this->network_commands.push_back({
		std::move(command), std::move(callback), std::move(session_is_active)});
	return true;
}

void GameSimulation::request_match_reset() {
	if (this->time_loop->get_clock()->get_state() != time::ClockState::PAUSED
	    or this->time_loop->get_clock()->get_time() != time::TIME_ZERO) {
		throw Error{MSG(err) << "FVD match reset requires a paused clock at zero"};
	}
	std::lock_guard lock{this->reset_mutex};
	if (this->reset_requested or this->reset_completed) {
		throw Error{MSG(err) << "FVD match reset already pending"};
	}
	this->reset_requested = true;
}

bool GameSimulation::consume_match_reset_completed() {
	std::lock_guard lock{this->reset_mutex};
	return std::exchange(this->reset_completed, false);
}

void GameSimulation::cancel_network_bootstrap() {
	std::lock_guard lock{this->bootstrap_mutex};
	++this->bootstrap_generation;
	this->bootstrap_requested.reset();
	this->bootstrap_ready.reset();
	this->update_requested.reset();
	this->update_ready.reset();
	this->network_admitted_players = 0;
	this->network_prune_unassigned_builders = false;
	this->network_disconnected_players.clear();
	// Commands are validated against the guest session and must not survive
	// leaving the current match.
	std::lock_guard commands_lock{this->network_command_mutex};
	this->network_commands.clear();
}

void GameSimulation::set_modpacks(const std::vector<std::string> &modpacks) {
	std::unique_lock lock{this->mutex};

	std::vector<std::string> mods{"engine"};
	mods.insert(mods.end(), modpacks.begin(), modpacks.end());

	this->mod_manager->activate_modpacks(mods);

	// TODO: Prevent setting modpacks if a game is already running
}

void GameSimulation::init_event_handlers() {
	auto click_select_handler = std::make_shared<gamestate::event::ClickSelectHandler>();
	auto drag_select_handler = std::make_shared<gamestate::event::DragSelectHandler>();
	auto spawn_handler = std::make_shared<gamestate::event::SpawnEntityHandler>(this->event_loop,
	                                                                            this->entity_factory);
	auto command_handler = std::make_shared<gamestate::event::SendCommandHandler>();
	auto manager_handler = std::make_shared<gamestate::event::ProcessCommandHandler>();
	auto wait_handler = std::make_shared<gamestate::event::WaitHandler>();
	this->event_loop->add_event_handler(click_select_handler);
	this->event_loop->add_event_handler(drag_select_handler);
	this->event_loop->add_event_handler(spawn_handler);
	this->event_loop->add_event_handler(command_handler);
	this->event_loop->add_event_handler(manager_handler);
	this->event_loop->add_event_handler(wait_handler);
}

} // namespace openage::gamestate
