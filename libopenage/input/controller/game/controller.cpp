// Copyright 2021-2023 the openage authors. See copying.md for legal info.

#include "controller.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <iterator>
#include <utility>

#include <QGuiApplication>
#include <QStyleHints>

#include "error/error.h"
#include "log/log.h"

#include "event/event_loop.h"
#include "event/evententity.h"
#include "event/state.h"
#include "gamestate/event/send_command.h"
#include "gamestate/event/spawn_entity.h"
#include "gamestate/fvd.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "input/controller/game/binding_context.h"
#include "input/controller/game/command_dispatcher.h"
#include "network/entity_kind.h"
#include "time/clock.h"
#include "time/time_loop.h"
#include "util/stall_profiler.h"

#include "coord/phys.h"
#include "renderer/camera/camera.h"
#include "renderer/stages/world/render_entity.h"


namespace openage::input::game {

Controller::Controller(const std::unordered_set<size_t> &controlled_factions,
                       size_t active_faction_id) :
	controlled_factions{controlled_factions},
	active_faction_id{active_faction_id} {}

void Controller::set_control(size_t faction_id) {
	std::unique_lock lock{this->mutex};

	if (this->controlled_factions.find(faction_id) != this->controlled_factions.end()) {
		this->active_faction_id = faction_id;
		this->selected.clear();
		this->selected_kind = SelectionKind::NONE;
		this->selected_render_entity.reset();
		this->selection_generation += 1;
		this->selection_pending = false;
		this->deferred_selection_action = {};
		this->placing_tower = false;
	this->garrison_mode = false;
		this->hunter_targeting = HunterTargeting::NONE;
	}
}

void Controller::lock_to_online_player(size_t faction_id) {
	std::unique_lock lock{this->mutex};
	this->controlled_factions = {faction_id};
	this->active_faction_id = faction_id;
	this->selected.clear();
	this->selected_kind = SelectionKind::NONE;
	this->selected_render_entity.reset();
	++this->selection_generation;
	this->selection_pending = false;
	this->deferred_selection_action = {};
	this->placing_tower = false;
	this->garrison_mode = false;
	this->hunter_targeting = HunterTargeting::NONE;
}

void Controller::reset_online_player_slots(size_t player_count) {
	std::unique_lock lock{this->mutex};
	this->controlled_factions.clear();
	for (size_t id = 1; id <= player_count; ++id) {
		this->controlled_factions.insert(id);
	}
	this->active_faction_id = 2;
	this->selected.clear();
	this->selected_kind = SelectionKind::NONE;
	this->selected_render_entity.reset();
	++this->selection_generation;
	this->selection_pending = false;
	this->deferred_selection_action = {};
	this->placing_tower = false;
	this->garrison_mode = false;
	this->hunter_targeting = HunterTargeting::NONE;
}

size_t Controller::get_controlled() const {
	std::unique_lock lock{this->mutex};

	return this->active_faction_id;
}

std::vector<gamestate::entity_id_t> Controller::get_selected() const {
	std::unique_lock lock{this->mutex};

	return this->selected;
}

void Controller::set_selected(
	const std::vector<gamestate::entity_id_t> ids,
	SelectionKind kind,
	const std::shared_ptr<renderer::world::RenderEntity> &render_entity) {
	std::unique_lock lock{this->mutex};

	log::log(DBG << "Selected " << ids.size() << " entities");

	this->selected = ids;
	this->selected_kind = (ids.size() == 1 or kind == SelectionKind::GROUP_HORSES
	                       or kind == SelectionKind::GROUP_TOWERS
	                       or kind == SelectionKind::GROUP_CONSTRUCTING_TOWERS
	                       or kind == SelectionKind::GROUP_FOUNDATIONS)
	                         ? kind : SelectionKind::NONE;
	this->selected_render_entity = ids.size() == 1 ? render_entity : nullptr;
	this->selection_generation += 1;
	this->selection_pending = false;
	this->deferred_selection_action = {};
	this->placing_tower = false;
	this->garrison_mode = false;
	this->hunter_targeting = HunterTargeting::NONE;
}

void Controller::mark_structure_double_click() {
	std::unique_lock lock{this->mutex};
	this->skip_double_click_release = true;
}

bool Controller::consume_structure_double_click_release() {
	std::unique_lock lock{this->mutex};
	return std::exchange(this->skip_double_click_release, false);
}

uint64_t Controller::begin_selection_update() {
	std::unique_lock lock{this->mutex};

	this->selection_generation += 1;
	this->selection_pending = true;
	this->deferred_selection_action = {};
	return this->selection_generation;
}

bool Controller::set_selected_if_current(
	uint64_t generation,
	const std::vector<gamestate::entity_id_t> ids,
	SelectionKind kind,
	const std::shared_ptr<renderer::world::RenderEntity> &render_entity) {
	std::function<void()> deferred;
	{
		std::unique_lock lock{this->mutex};
		if (not this->selection_pending or generation != this->selection_generation) {
			return false;
		}

		log::log(DBG << "Selected " << ids.size() << " entities");
		this->selected = ids;
		this->selected_kind = (ids.size() == 1 or kind == SelectionKind::GROUP_HORSES
		                       or kind == SelectionKind::GROUP_TOWERS
		                       or kind == SelectionKind::GROUP_CONSTRUCTING_TOWERS
		                       or kind == SelectionKind::GROUP_FOUNDATIONS)
		                         ? kind : SelectionKind::NONE;
		this->selected_render_entity = ids.size() == 1 ? render_entity : nullptr;
		this->selection_pending = false;
		this->placing_tower = false;
	this->garrison_mode = false;
		this->hunter_targeting = HunterTargeting::NONE;
		deferred = std::move(this->deferred_selection_action);
	}

	if (deferred) {
		deferred();
	}
	return true;
}

bool Controller::defer_until_selection_applied(std::function<void()> action) {
	std::unique_lock lock{this->mutex};
	if (not this->selection_pending) {
		return false;
	}
	this->deferred_selection_action = std::move(action);
	return true;
}

SelectionDetails Controller::get_selection_details() const {
	std::unique_lock lock{this->mutex};
	return {this->selected, this->selected_kind, this->selected_render_entity};
}

bool Controller::begin_garrison_mode() {
	std::unique_lock lock{this->mutex};
	// A click selection resolves on a later simulation-thread callback. T
	// pressed immediately after selecting the Builder must not be lost.
	if (this->selection_pending) {
		this->deferred_selection_action = [this]() { this->begin_garrison_mode(); };
		return true;
	}
	if (this->active_faction_id < 2 or this->selected.size() != 1
	    or this->selected_kind != SelectionKind::BUILDER) return false;
	this->placing_tower = false;
	this->hunter_targeting = HunterTargeting::NONE;
	this->garrison_mode = true;
	return true;
}

bool Controller::is_garrison_mode_active() const {
	std::unique_lock lock{this->mutex};
	return this->garrison_mode;
}

void Controller::cancel_garrison_mode() {
	std::unique_lock lock{this->mutex};
	this->garrison_mode = false;
}

bool Controller::begin_tower_placement() {
	std::unique_lock lock{this->mutex};
	if (this->active_faction_id < 2 or this->selected.size() != 1
	    or this->selected_kind != SelectionKind::BUILDER) {
		return false;
	}
	this->hunter_targeting = HunterTargeting::NONE;
	this->garrison_mode = false;
	this->placing_tower = true;
	return true;
}

void Controller::cancel_tower_placement() {
	std::unique_lock lock{this->mutex};
	this->placing_tower = false;
	this->garrison_mode = false;
}

bool Controller::is_tower_placement_active() const {
	std::unique_lock lock{this->mutex};
	return this->placing_tower;
}

bool Controller::begin_attack_move() {
	return this->begin_hunter_targeting(HunterTargeting::ATTACK_MOVE);
}

bool Controller::begin_patrol() {
	return this->begin_hunter_targeting(HunterTargeting::PATROL);
}

bool Controller::begin_hunter_targeting(HunterTargeting mode) {
	std::unique_lock lock{this->mutex};
	if (not this->is_hunter_selection()) {
		return false;
	}
	this->placing_tower = false;
	this->garrison_mode = false;
	this->hunter_targeting = mode;
	return true;
}

bool Controller::is_hunter_selection() const {
	std::unique_lock lock{this->mutex};
	return this->active_faction_id == 1 and not this->selected.empty()
	   and (this->selected_kind == SelectionKind::HORSE
	        or this->selected_kind == SelectionKind::GROUP_HORSES);
}

void Controller::set_hunter_stance_action(std::function<void(gamestate::HunterStance)> action) {
	std::unique_lock lock{this->mutex};
	this->hunter_stance_action = std::move(action);
}

bool Controller::issue_hunter_stance(gamestate::HunterStance stance) {
	std::function<void(gamestate::HunterStance)> action;
	{
		std::unique_lock lock{this->mutex};
		if (not this->is_hunter_selection() or not this->hunter_stance_action) {
			return false;
		}
		action = this->hunter_stance_action;
	}
	action(stance);
	return true;
}

void Controller::set_ungarrison_action(std::function<void()> action) {
	std::unique_lock lock{this->mutex};
	this->ungarrison_action = std::move(action);
}

bool Controller::issue_ungarrison() {
	std::function<void()> action;
	{
		std::unique_lock lock{this->mutex};
		if (this->active_faction_id < 2 or this->selected.size() != 1
		    or this->selected_kind != SelectionKind::TOWER or not this->ungarrison_action) {
			return false;
		}
		action = this->ungarrison_action;
	}
	// Run outside the lock: the action reads the selection again.
	action();
	return true;
}

void Controller::set_order_feedback(std::function<void()> feedback) {
	std::unique_lock lock{this->mutex};
	this->order_feedback = std::move(feedback);
}

void Controller::notify_order_issued() {
	std::function<void()> feedback;
	{
		std::unique_lock lock{this->mutex};
		feedback = this->order_feedback;
	}
	if (feedback) {
		feedback();
	}
}

void Controller::cancel_hunter_targeting() {
	std::unique_lock lock{this->mutex};
	this->hunter_targeting = HunterTargeting::NONE;
}

bool Controller::is_attack_move_active() const {
	std::unique_lock lock{this->mutex};
	return this->hunter_targeting == HunterTargeting::ATTACK_MOVE;
}

bool Controller::is_patrol_active() const {
	std::unique_lock lock{this->mutex};
	return this->hunter_targeting == HunterTargeting::PATROL;
}

bool Controller::process(const event_arguments &ev_args, const std::shared_ptr<BindingContext> &ctx) {
	util::StallProfiler profile{"input.game_controller.process"};
	util::StallProfiler lock_profile{"input.game_controller.lock_wait"};
	std::unique_lock lock{this->mutex};
	lock_profile.finish();

	if (not ctx->is_bound(ev_args.e)) {
		return false;
	}

	// TODO: check if action is allowed
	auto bind = ctx->lookup(ev_args.e);
	bind.transform(ev_args, this->shared_from_this());

	return true;
}

void Controller::set_drag_select_start(const coord::input &start,
                                       const coord::tile &world_start) {
	std::unique_lock lock{this->mutex};

	log::log(DBG << "Drag select start at " << start);
	this->drag_start = DragStart{start, world_start};
}


std::optional<coord::input> Controller::get_drag_select_start_optional() const {
	std::unique_lock lock{this->mutex};
	if (not this->drag_start) return std::nullopt;
	return this->drag_start->screen;
}

std::optional<coord::tile> Controller::get_drag_world_start_optional() const {
	std::unique_lock lock{this->mutex};
	if (not this->drag_start) return std::nullopt;
	return this->drag_start->world;
}

const coord::input Controller::get_drag_select_start() const {
	std::unique_lock lock{this->mutex};

	if (not this->drag_start.has_value()) {
		throw Error{ERR << "Drag select start not set."};
	}

	return this->drag_start->screen;
}

void Controller::reset_drag_select() {
	std::unique_lock lock{this->mutex};

	log::log(DBG << "Drag select start reset");
	this->drag_start = std::nullopt;
}

std::vector<coord::tile> tower_line_sites(const coord::tile &start,
                                          const coord::tile &end) {
	const auto ne_delta = end.ne - start.ne;
	const auto se_delta = end.se - start.se;
	const bool along_ne = std::abs(ne_delta) >= std::abs(se_delta);
	const auto delta = along_ne ? ne_delta : se_delta;
	const auto step = delta < 0 ? -1 : 1;
	std::vector<coord::tile> sites;
	sites.reserve(static_cast<size_t>(std::abs(delta)) + 1);
	for (auto i = 0; i <= std::abs(delta); ++i) {
		sites.emplace_back(start.ne + (along_ne ? i * step : 0),
		                   start.se + (along_ne ? 0 : i * step));
	}
	return sites;
}

std::vector<gamestate::entity_id_t> units_take_priority(
	const std::vector<gamestate::entity_id_t> &ids,
	const std::function<bool(gamestate::entity_id_t)> &is_unit) {
	std::vector<gamestate::entity_id_t> units;
	std::copy_if(ids.begin(), ids.end(), std::back_inserter(units), is_unit);
	return units.empty() ? ids : units;
}


namespace {

/** Selection kind for a single selected entity of the given kind. */
SelectionKind selection_kind_of(network::EntityKind kind) {
	switch (kind) {
	case network::EntityKind::BUILDER:
		return SelectionKind::BUILDER;
	case network::EntityKind::HORSE:
		return SelectionKind::HORSE;
	case network::EntityKind::WOOD_TOWER:
	case network::EntityKind::STONE_TOWER:
		return SelectionKind::TOWER;
	case network::EntityKind::HUNTER_TOWER:
		return SelectionKind::HUNTER_TOWER;
	case network::EntityKind::FOUNDATION:
		return SelectionKind::FOUNDATION;
	default:
		return SelectionKind::OTHER;
	}
}

SelectionKind structure_type(const network::WorldState &world,
                             const network::EntityState &entity) {
	const auto kind = entity.kind();
	if (kind == network::EntityKind::FOUNDATION) {
		return SelectionKind::FOUNDATION;
	}
	if (kind == network::EntityKind::HUNTER_TOWER) {
		return SelectionKind::HUNTER_TOWER;
	}
	if (not network::is_tower(kind)) {
		return SelectionKind::NONE;
	}
	const bool being_built = std::any_of(
		world.constructions.begin(), world.constructions.end(),
		[&](const network::ConstructionState &construction) {
			return construction.site_entity_id == entity.entity_id
			    and construction.phase == network::ConstructionPhase::UNDER_CONSTRUCTION;
		});
	return being_built ? SelectionKind::GROUP_CONSTRUCTING_TOWERS : SelectionKind::TOWER;
}

SelectionKind group_of(SelectionKind kind) {
	if (kind == SelectionKind::TOWER) return SelectionKind::GROUP_TOWERS;
	if (kind == SelectionKind::FOUNDATION) return SelectionKind::GROUP_FOUNDATIONS;
	return kind;
}

bool visible_structure(const network::WorldState &world,
                       gamestate::player_id_t viewer,
                       const network::EntityState &entity) {
	if (entity.remembered) return false;
	if (entity.owner_id != 0 and (entity.owner_id == 1) == (viewer == 1))
		return true;
	const auto side = viewer == 1 ? network::PlayerRole::HUNTER : network::PlayerRole::BUILDER;
	const auto fog = std::find_if(world.fog.begin(), world.fog.end(),
	                             [side](const network::TeamFog &entry) {
		                             return entry.team == side;
	                             });
	return fog != world.fog.end()
	    and network::fog_tile_set(fog->visible, world.map_size, entity.position.to_tile());
}

} // namespace

std::optional<StructureSelection> select_fvd_structure(
	const network::WorldState &world,
	gamestate::player_id_t viewer,
	gamestate::entity_id_t clicked,
	const SelectionDetails &previous,
	bool shift, bool double_click,
	const std::function<bool(const network::EntityState &)> &on_screen) {
	const auto find = [&](gamestate::entity_id_t id) {
		return std::find_if(world.entities.begin(), world.entities.end(),
			[id](const network::EntityState &entity) { return entity.entity_id == id; });
	};
	const auto source = find(clicked);
	if (source == world.entities.end() or not visible_structure(world, viewer, *source))
		return std::nullopt;
	const auto kind = structure_type(world, *source);
	if (kind == SelectionKind::NONE) return std::nullopt;
	const bool builder = viewer >= 2;
	if (builder and source->owner_id < 2) return std::nullopt;
	if (not builder and kind == SelectionKind::FOUNDATION) return std::nullopt;
	if (not builder) {
		return StructureSelection{{clicked},
			kind == SelectionKind::HUNTER_TOWER ? kind : SelectionKind::TOWER};
	}
	std::vector<gamestate::entity_id_t> selected{clicked};
	if (double_click) {
		for (const auto &entity : world.entities) {
			if (entity.entity_id != clicked and entity.owner_id == source->owner_id
			    and structure_type(world, entity) == kind
			    and visible_structure(world, viewer, entity) and on_screen(entity)) {
				selected.push_back(entity.entity_id);
			}
		}
	}
	else if (shift and (previous.kind == kind or previous.kind == group_of(kind)
	                     or (kind == SelectionKind::GROUP_CONSTRUCTING_TOWERS
	                         and previous.kind == SelectionKind::TOWER))) {
		const bool compatible = std::all_of(previous.ids.begin(), previous.ids.end(),
			[&](gamestate::entity_id_t id) {
				const auto entry = find(id);
				return entry != world.entities.end() and entry->owner_id >= 2
				    and structure_type(world, *entry) == kind
				    and visible_structure(world, viewer, *entry);
			});
		if (compatible) selected.insert(selected.end(), previous.ids.begin(), previous.ids.end());
	}
	std::sort(selected.begin(), selected.end());
	selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
	const auto result_kind = selected.size() > 1 ? group_of(kind)
	    : (kind == SelectionKind::GROUP_CONSTRUCTING_TOWERS ? SelectionKind::TOWER : kind);
	return StructureSelection{std::move(selected), result_kind};
}

namespace {

/** Resolve the selected entity type on the simulation thread. */
void apply_selection(const std::shared_ptr<Controller> &controller,
                     const std::shared_ptr<gamestate::GameState> &state,
                     uint64_t generation,
                     const std::vector<gamestate::entity_id_t> &ids) {
	if (not state->get_fvd()) {
		controller->set_selected_if_current(generation, ids);
		return;
	}
	auto is_horse = [&](gamestate::entity_id_t id) {
		auto found = state->get_game_entities().find(id);
		return found != state->get_game_entities().end()
		    and network::classify(found->second->get_definition()) == network::EntityKind::HORSE;
	};
	auto is_unit = [&](gamestate::entity_id_t id) {
		auto found = state->get_game_entities().find(id);
		return found != state->get_game_entities().end()
		    and network::is_unit(network::classify(found->second->get_definition()));
	};
	if (ids.size() > 1) {
		const auto chosen = units_take_priority(ids, is_unit);
		if (chosen.size() > 1) {
			bool all_horses = std::all_of(chosen.begin(), chosen.end(), is_horse);
			controller->set_selected_if_current(
				generation, chosen,
				all_horses ? SelectionKind::GROUP_HORSES : SelectionKind::NONE);
			return;
		}
		// One unit and some buildings: select that unit on its own below.
		apply_selection(controller, state, generation, chosen);
		return;
	}
	if (ids.empty()) {
		controller->set_selected_if_current(generation, {});
		return;
	}
	auto found = state->get_game_entities().find(ids.front());
	if (found == state->get_game_entities().end()) {
		controller->set_selected_if_current(generation, {});
		return;
	}
	const auto kind = selection_kind_of(network::classify(found->second->get_definition()));
	controller->set_selected_if_current(
		generation, ids, kind, found->second->get_render_entity());
}


/** Structure picks use authoritative recipient-visible snapshots, independent
 * of the host-local game.click_select ownership-only event. */
bool apply_structure_click(
	const std::shared_ptr<Controller> &controller,
	const CommandDispatcher &commands,
	const std::shared_ptr<renderer::camera::Camera> &camera,
	gamestate::entity_id_t clicked, uint64_t generation,
	const SelectionDetails &previous, bool shift, bool double_click) {
	const auto snapshot = commands.snapshot();
	const network::WorldState *world = snapshot.world.get();
	if (not world) return false;
	const auto viewer = static_cast<gamestate::player_id_t>(controller->get_controlled());
	const auto on_screen = [camera](const network::EntityState &entity) {
		const Eigen::Matrix4f matrix = camera->get_projection_matrix() * camera->get_view_matrix();
		const auto pos = entity.position.to_scene3().to_world_space();
		const Eigen::Vector4f clip = matrix * Eigen::Vector4f{pos.x(), pos.y(), pos.z(), 1.0f};
		return std::isfinite(clip.w()) and clip.w() > 0.0f
		    and std::isfinite(clip.x()) and std::isfinite(clip.y())
		    and std::abs(clip.x() / clip.w()) <= 1.0f
		    and std::abs(clip.y() / clip.w()) <= 1.0f;
	};
	const auto result = select_fvd_structure(
		*world, viewer, clicked, previous, shift, double_click, on_screen);
	if (not result) return false;
	controller->set_selected_if_current(generation, result->ids, result->kind);
	return true;
}

/** Resolve guest selections exclusively from the host's replicated world. */
void apply_guest_selection(
	const std::shared_ptr<Controller> &controller,
	const network::WorldState &world,
	uint64_t generation,
	const std::vector<gamestate::entity_id_t> &ids) {
	std::vector<gamestate::entity_id_t> valid;
	SelectionKind kind = SelectionKind::NONE;
	auto find = [&world](gamestate::entity_id_t id) {
		return std::find_if(world.entities.begin(), world.entities.end(),
			[id](const network::EntityState &entity) { return entity.entity_id == id; });
	};
	const auto candidates = units_take_priority(ids, [&](gamestate::entity_id_t id) {
		auto found = find(id);
		return found != world.entities.end() and found->owner_id == world.local_player_id
		    and network::is_unit(found->kind());
	});
	for (auto id : candidates) {
		auto found = find(id);
		if (found == world.entities.end()) {
			continue;
		}
		const bool own = found->owner_id == world.local_player_id;
		const bool allied_tower = world.local_player_id >= 2
		                       and found->owner_id >= 2
		                       and network::is_tower(found->kind());
		if (not own and not allied_tower) {
			continue;
		}
		const auto entity_selection = selection_kind_of(found->kind());
		if (entity_selection == SelectionKind::OTHER) {
			continue;
		}
		// Horses form groups; any other kind is selected on its own.
		if (entity_selection == SelectionKind::HORSE) {
			if (kind != SelectionKind::NONE and kind != SelectionKind::HORSE) {
				continue;
			}
		}
		else if (not valid.empty()) {
			continue;
		}
		kind = entity_selection;
		valid.push_back(id);
		if (kind != SelectionKind::HORSE) break;
	}
	if (kind == SelectionKind::HORSE and valid.size() > 1) {
		kind = SelectionKind::GROUP_HORSES;
	}
	controller->set_selected_if_current(generation, valid, kind);
}

} // namespace

std::optional<gamestate::entity_id_t> queue_foundation_target(
	const network::WorldState &world, gamestate::player_id_t owner,
	std::optional<gamestate::entity_id_t> picked, const coord::tile &clicked_tile) {
	auto matches = [owner](const network::EntityState &entity) {
		return owner >= 2 and entity.owner_id == owner and not entity.remembered
		    and entity.kind() == network::EntityKind::FOUNDATION;
	};
	auto ground = std::find_if(world.entities.begin(), world.entities.end(),
	                           [&](const auto &entity) {
		                           return matches(entity)
		                               and entity.position.to_tile() == clicked_tile;
	                           });
	auto sprite = world.entities.end();
	if (picked) {
		sprite = std::find_if(world.entities.begin(), world.entities.end(),
		                      [&](const auto &entity) {
			                      return entity.entity_id == *picked and matches(entity);
		                      });
	}
	if (ground != world.entities.end()) {
		if (sprite != world.entities.end() and sprite != ground) {
			auto reserved = [&world](const network::EntityState &entity) {
				const auto tile = entity.position.to_tile();
				return std::any_of(world.reservations.begin(), world.reservations.end(),
				                   [&tile](const auto &reservation) {
					                   return reservation.tile == tile;
				                   });
			};
			if (reserved(*ground) and not reserved(*sprite)) {
				return sprite->entity_id;
			}
		}
		return ground->entity_id;
	}
	if (sprite != world.entities.end()) {
		return sprite->entity_id;
	}
	return std::nullopt;
}

std::optional<TowerTarget> friendly_tower_target(
	const network::WorldState *world, gamestate::player_id_t owner,
	std::optional<gamestate::entity_id_t> picked, const coord::tile &clicked_tile,
	std::optional<gamestate::entity_id_t> excluded) {
	if (not world) {
		if (picked and picked != excluded) {
			return TowerTarget{*picked, std::nullopt};
		}
		return std::nullopt;
	}
	auto matches = [owner, excluded](const network::EntityState &entity) {
		return entity.entity_id != excluded and owner >= 2 and entity.owner_id >= 2
		    and network::is_tower(entity.kind());
	};
	auto found = world->entities.end();
	if (picked) {
		found = std::find_if(world->entities.begin(), world->entities.end(),
		                     [&](const network::EntityState &entity) {
			                     return entity.entity_id == *picked and matches(entity);
		                     });
	}
	if (found == world->entities.end()) {
		found = std::find_if(world->entities.begin(), world->entities.end(),
		                     [&](const network::EntityState &entity) {
			                     return matches(entity) and entity.position.to_tile() == clicked_tile;
		                     });
	}
	if (found == world->entities.end()) {
		return std::nullopt;
	}
	return TowerTarget{found->entity_id, found->position};
}

void setup_defaults(const std::shared_ptr<BindingContext> &ctx,
                    const std::shared_ptr<Controller> &controller,
                    const std::shared_ptr<time::TimeLoop> &time_loop,
                    const std::shared_ptr<openage::gamestate::GameSimulation> &simulation,
                    const std::shared_ptr<renderer::camera::Camera> &camera,
	                const entity_picker_t &pick_entity_filtered,
                    const std::function<bool(network::CommandIntent)> &send_guest_command,
                    const std::function<std::optional<network::WorldState>()> &guest_world,
                    bool online_lobby,
                    const std::function<bool()> &can_spawn_hunter) {
	auto pick_entity = [pick_entity_filtered](const coord::input &position,
	                                           const entity_pick_callback_t &callback) {
		pick_entity_filtered(position, callback, {});
	};
	const auto commands = CommandDispatcher::for_simulation(
		simulation, time_loop, send_guest_command, guest_world);
	auto create_entity_event = [simulation, time_loop, commands](
		const coord::phys3 &mouse_pos, bool append, const std::shared_ptr<Controller> &controller) {
		auto state = simulation->get_game()->get_state();
		if (state->get_fvd()) {
			auto selection = controller->get_selection_details();
			if (selection.kind != SelectionKind::BUILDER) {
				return;
			}
			const auto owner = static_cast<gamestate::player_id_t>(controller->get_controlled());
			const auto route = commands->route();
			// Stay in placement mode so an invalid click is correctable.
			if (route == CommandRoute::LOCAL
			    and not state->get_fvd()->can_place_tower(
			        state, owner, selection.ids, mouse_pos, time_loop->get_clock()->get_time())) {
				return;
			}
			network::CommandIntent intent{
				0, network::CommandKind::BUILD_TOWER, selection.ids, mouse_pos};
			intent.queue = append;
			if (commands->dispatch(route, intent, owner) and not append) {
				controller->cancel_tower_placement();
			}
			return;
		}

		event::EventHandler::param_map::map_t params{
			{"position", mouse_pos},
			{"owner", controller->get_controlled()},
			{"entity_ids", controller->get_selected()},
		};
		simulation->get_event_loop()->submit_event(
			"game.spawn_entity", simulation->get_spawner(), state,
			time_loop->get_clock()->get_time(), params);
	};

	binding_func_t hunter_route_event{[camera, commands](
		const event_arguments &args, const std::shared_ptr<Controller> controller) {
		const bool patrol = controller->is_patrol_active();
		if (not patrol and not controller->is_attack_move_active()) {
			return;
		}
		controller->cancel_hunter_targeting();
		controller->notify_order_issued();
		commands->dispatch(
			network::CommandIntent{
				0, patrol ? network::CommandKind::PATROL : network::CommandKind::ATTACK_MOVE,
				controller->get_selected(), args.mouse.to_phys3(camera)},
			static_cast<gamestate::player_id_t>(controller->get_controlled()));
	}};

	auto set_stance = [commands](
		const std::shared_ptr<Controller> &controller, gamestate::HunterStance stance) {
		if (not controller->is_hunter_selection()) {
			return;
		}
		network::CommandIntent intent{
			0, network::CommandKind::SET_STANCE, controller->get_selected()};
		intent.stance = stance;
		commands->dispatch(
			intent, static_cast<gamestate::player_id_t>(controller->get_controlled()));
	};

	auto fvd = simulation->get_game()->get_state()->get_fvd();
	if (fvd) {
		binding_func_t demolish_selected_tower{[commands](
		                                        const event_arguments &,
		                                        const std::shared_ptr<Controller> controller) {
			const auto selection = controller->get_selection_details();
			const auto owner = static_cast<gamestate::player_id_t>(controller->get_controlled());
			// Delete removes the selected tower (with its collapse animation)
			// or the selected foundation; the host checks ownership.
			if (owner < 2 or selection.ids.size() != 1
			    or (selection.kind != SelectionKind::TOWER and selection.kind != SelectionKind::FOUNDATION)) {
				return;
			}
			commands->dispatch(
				network::CommandIntent{0, network::CommandKind::DEMOLISH_TOWER, selection.ids},
				owner);
		}};
		ctx->bind(Event{event_class::KEYBOARD, Qt::Key_Delete,
		                Qt::KeyboardModifier::NoModifier, QEvent::KeyRelease},
		          binding_action{forward_action_t::CLEAR, demolish_selected_tower});
		std::weak_ptr<Controller> weak_controller = controller;
		controller->set_hunter_stance_action([weak_controller, set_stance](gamestate::HunterStance stance) {
			if (auto active = weak_controller.lock()) {
				set_stance(active, stance);
			}
		});
		binding_func_t start_garrison{[](const event_arguments &,
		                               const std::shared_ptr<Controller> ctl) {
			ctl->begin_garrison_mode();
		}};
		ctx->bind(Event{event_class::KEYBOARD, Qt::Key_T,
		                Qt::KeyboardModifier::NoModifier, QEvent::KeyRelease},
		          binding_action{forward_action_t::CLEAR, start_garrison});

		// Shared by the G hotkey and the HUD's Ungarrison button.
		auto ungarrison = [commands](const std::shared_ptr<Controller> &ctl) {
			const auto selection = ctl->get_selection_details();
			const auto owner = static_cast<gamestate::player_id_t>(ctl->get_controlled());
			if (owner < 2 or selection.ids.size() != 1
			    or selection.kind != SelectionKind::TOWER) return;
			commands->dispatch(
				network::CommandIntent{0, network::CommandKind::UNGARRISON, selection.ids},
				owner);
		};
		binding_func_t ungarrison_selected{[ungarrison](const event_arguments &,
		                                                const std::shared_ptr<Controller> ctl) {
			ungarrison(ctl);
		}};
		controller->set_ungarrison_action([weak_controller, ungarrison]() {
			if (auto active = weak_controller.lock()) {
				ungarrison(active);
			}
		});
		ctx->bind(Event{event_class::KEYBOARD, Qt::Key_G,
		                Qt::KeyboardModifier::NoModifier, QEvent::KeyRelease},
		          binding_action{forward_action_t::CLEAR, ungarrison_selected});

		binding_func_t spawn_hunter{[simulation, time_loop, can_spawn_hunter](
		                                const event_arguments &,
		                                const std::shared_ptr<Controller>) {
			if (can_spawn_hunter and not can_spawn_hunter()) {
				return;
			}
			auto state = simulation->get_game()->get_state();
			auto current_fvd = state->get_fvd();
			if (not current_fvd) {
				throw Error{MSG(err) << "F9 Hunter spawn requested outside an FVD game"};
			}
			current_fvd->request_immediate_hunter_spawn(
				state, time_loop->get_clock()->get_time());
		}};
		binding_func_t start_placement{[](const event_arguments &,
		                                 const std::shared_ptr<Controller> controller) {
			controller->begin_tower_placement();
		}};
		binding_func_t cancel_placement{[](const event_arguments &,
		                                  const std::shared_ptr<Controller> controller) {
			controller->cancel_tower_placement();
			controller->cancel_garrison_mode();
			controller->cancel_hunter_targeting();
		}};
		binding_func_t start_attack_move{[](const event_arguments &,
		                                    const std::shared_ptr<Controller> controller) {
			controller->begin_attack_move();
		}};
		binding_func_t start_patrol{[](const event_arguments &,
		                               const std::shared_ptr<Controller> controller) {
			controller->begin_patrol();
		}};
		ctx->bind(Event{event_class::KEYBOARD, Qt::Key_A,
		                Qt::KeyboardModifier::NoModifier, QEvent::KeyRelease},
		          binding_action{forward_action_t::CLEAR, start_attack_move});
		ctx->bind(Event{event_class::KEYBOARD, Qt::Key_P,
		                Qt::KeyboardModifier::NoModifier, QEvent::KeyRelease},
		          binding_action{forward_action_t::CLEAR, start_patrol});
		for (auto [key, stance] : {std::pair{Qt::Key_Z, gamestate::HunterStance::AGGRESSIVE},
		                           std::pair{Qt::Key_X, gamestate::HunterStance::DEFENSIVE},
		                           std::pair{Qt::Key_C, gamestate::HunterStance::OBSERVE}}) {
			binding_func_t choose_stance{[stance](const event_arguments &,
			                                     const std::shared_ptr<Controller> controller) {
				controller->issue_hunter_stance(stance);
			}};
			ctx->bind(Event{event_class::KEYBOARD, key,
			                Qt::KeyboardModifier::NoModifier, QEvent::KeyRelease},
			          binding_action{forward_action_t::CLEAR, choose_stance});
		}
		ctx->bind(Event{event_class::KEYBOARD, Qt::Key_B,
		                Qt::KeyboardModifier::NoModifier, QEvent::KeyRelease},
		          binding_action{forward_action_t::CLEAR, start_placement});
		ctx->bind(Event{event_class::KEYBOARD, Qt::Key_Escape,
		                Qt::KeyboardModifier::NoModifier, QEvent::KeyRelease},
		          binding_action{forward_action_t::CLEAR, cancel_placement});
		ctx->bind(Event{event_class::KEYBOARD, Qt::Key_F9,
		                Qt::KeyboardModifier::NoModifier, QEvent::KeyRelease},
		          binding_action{forward_action_t::CLEAR, spawn_hunter});

		if (not online_lobby) for (size_t id = 1; id <= fvd->get_player_count(); ++id) {
			binding_func_t switch_player{[id, camera, fvd](const event_arguments &,
			                                               const std::shared_ptr<Controller> controller) {
				controller->set_control(id);
				camera->look_at_coord(fvd->get_centre().to_scene3());
				log::log(INFO << "Controlling player " << id << (id == 1 ? " (Hunter)" : " (Builders)"));
			}};
			ctx->bind(Event{event_class::KEYBOARD, static_cast<int>(Qt::Key_0 + id),
			                Qt::KeyboardModifier::NoModifier, QEvent::KeyRelease},
			          binding_action{forward_action_t::CLEAR, switch_player});
		}
	}

	auto submit_order = [camera, pick_entity, commands](
		                    const event_arguments &args,
		                    const std::shared_ptr<Controller> controller) {
		if (controller->is_garrison_mode_active()) {
			controller->cancel_garrison_mode();
			return;
		}
		if (controller->is_tower_placement_active()) {
			controller->cancel_tower_placement();
			return;
		}
		if (controller->is_attack_move_active() or controller->is_patrol_active()) {
			controller->cancel_hunter_targeting();
			return;
		}
		auto mouse_pos = args.mouse.to_phys3(camera);
		auto selection = controller->get_selection_details();
		auto owner = static_cast<gamestate::player_id_t>(controller->get_controlled());
		// Treat right-clicking another owned tower as an ENTITY target, not
		// the ground underneath its sprite. Preserve the tower ID across the
		// network so a raised red arrow cannot affect movement or hopping.
		if (owner >= 2 and selection.kind == SelectionKind::TOWER
		    and selection.ids.size() == 1) {
			pick_entity(args.mouse, [commands, selection, mouse_pos, owner](
			    std::optional<gamestate::entity_id_t> picked) {
				const auto snapshot = commands->snapshot();
				const auto tower = friendly_tower_target(
					snapshot.world.get(), owner, picked, mouse_pos.to_tile(), selection.ids.front());
				network::CommandIntent intent{
					0, network::CommandKind::SET_UNGARRISON_POINT, selection.ids, mouse_pos};
				if (tower) {
					intent.target_entity = tower->id;
					if (tower->position) {
						intent.target = *tower->position;
						intent.target.up = coord::phys_t::from_int(0);
					}
				}
				commands->dispatch(snapshot.route, intent, owner);
			});
			return;
		}
		// Right-clicking with the Hunter tower selected moves its rally point.
		if (owner == 1 and selection.kind == SelectionKind::HUNTER_TOWER
		    and selection.ids.size() == 1) {
			commands->dispatch(
				network::CommandIntent{
					0, network::CommandKind::SET_RALLY_POINT, selection.ids, mouse_pos},
				owner);
			return;
		}
		// Acknowledge orders to the player's own units, as AoE2 does.
		if (not selection.ids.empty()
		    and ((owner == 1 and (selection.kind == SelectionKind::HORSE
		                          or selection.kind == SelectionKind::GROUP_HORSES))
		         or (owner >= 2 and selection.kind == SelectionKind::BUILDER))) {
			controller->notify_order_issued();
		}
		if (auto replica = commands->guest_world()) {
			if (selection.ids.empty()) return;
			const bool horses = owner == 1
				&& (selection.kind == SelectionKind::HORSE
				    or selection.kind == SelectionKind::GROUP_HORSES);
			const bool builder = owner >= 2 and selection.kind == SelectionKind::BUILDER;
			auto order = [commands, selection, mouse_pos,
			              replica = std::move(replica), owner, horses, builder](
				std::optional<gamestate::entity_id_t> hit) {
				network::CommandIntent intent{
					0, network::CommandKind::MOVE, selection.ids, mouse_pos};
				if (hit) {
					auto found = std::find_if(
						replica->entities.begin(), replica->entities.end(),
						[hit](const network::EntityState &entry) {
							return entry.entity_id == *hit;
						});
					if (found != replica->entities.end()) {
						if (horses and found->owner_id != owner
						    and (found->kind() == network::EntityKind::BUILDER
						         or network::is_tower(found->kind()))) {
							intent.kind = network::CommandKind::FOCUS_ATTACK;
							intent.target_entity = *hit;
						}
						else if (builder and found->owner_id == owner
						         and found->kind() == network::EntityKind::FOUNDATION) {
							intent.kind = network::CommandKind::PRIORITIZE_FOUNDATION;
							intent.target_entity = *hit;
						}
					}
				}
				commands->dispatch(CommandRoute::GUEST, intent, owner);
			};
			if (horses or builder) {
				pick_entity(args.mouse, std::move(order));
			}
			else {
				order(std::nullopt);
			}
			return;
		}

		// The render-thread pick reports only an entity ID. Target
		// ownership, type, and life are checked on the simulation thread.
		const bool hunter_selection = owner == 1 and not selection.ids.empty()
			and (selection.kind == SelectionKind::HORSE
			     or selection.kind == SelectionKind::GROUP_HORSES);
		const bool builder_selection = owner >= 2 and selection.ids.size() == 1
			and selection.kind == SelectionKind::BUILDER;
		auto send_order = [commands, mouse_pos, owner, ids = selection.ids,
		                   hunter_selection, builder_selection](
			std::optional<gamestate::entity_id_t> picked) {
			network::CommandIntent intent{
				0,
				picked and hunter_selection ? network::CommandKind::FOCUS_ATTACK
				                            : network::CommandKind::MOVE,
				ids, mouse_pos};
			if (picked and (hunter_selection or builder_selection)) {
				intent.target_entity = *picked;
			}
			commands->dispatch(CommandRoute::LOCAL, intent, owner);
		};
		if (hunter_selection or builder_selection) {
			pick_entity(args.mouse, send_order);
		}
		else {
			send_order(std::nullopt);
		}
	};
	binding_func_t move_entity{[submit_order](const event_arguments &args,
	                                            const std::shared_ptr<Controller> controller) {
		if (controller->defer_until_selection_applied(
			    [submit_order, args, controller]() { submit_order(args, controller); })) {
			return;
		}
		submit_order(args, controller);
	}};

	binding_action move_entity_action{forward_action_t::SEND, move_entity};
	Event ev_mouse_rmb{
		event_class::MOUSE_BUTTON,
		Qt::MouseButton::RightButton,
		Qt::KeyboardModifier::NoModifier,
		QEvent::MouseButtonRelease};

	ctx->bind(ev_mouse_rmb, move_entity_action);

	binding_func_t init_drag_selection{[camera](const event_arguments &args,
	                                       const std::shared_ptr<Controller> controller) {
		controller->set_drag_select_start(
			args.mouse, args.mouse.to_phys3(camera).to_tile());
	}};

	binding_action init_drag_selection_action{forward_action_t::CLEAR, init_drag_selection};
	Event ev_mouse_lmb_press{
		event_class::MOUSE_BUTTON,
		Qt::MouseButton::LeftButton,
		Qt::KeyboardModifier::NoModifier,
		QEvent::MouseButtonPress};

	ctx->bind(ev_mouse_lmb_press, init_drag_selection_action);
	ctx->bind(Event{event_class::MOUSE_BUTTON, Qt::MouseButton::LeftButton,
	                Qt::KeyboardModifier::ShiftModifier, QEvent::MouseButtonPress},
	          init_drag_selection_action);
	ctx->bind(Event{event_class::MOUSE_BUTTON_DBL, Qt::MouseButton::LeftButton,
	                Qt::KeyboardModifier::ShiftModifier, QEvent::MouseButtonDblClick},
	          init_drag_selection_action);

	struct ShiftClick {
		std::optional<gamestate::entity_id_t> picked;
		std::optional<gamestate::entity_id_t> foundation;
		gamestate::player_id_t owner;
		CommandRoute route;
	};

	auto resolve_queue_click = [pick_entity, pick_entity_filtered, commands](
		const coord::input &position,
		const coord::phys3 &mouse_pos,
		gamestate::player_id_t owner,
		const std::function<void(ShiftClick)> &callback) {
		const auto snapshot = commands->snapshot();
		const auto &world = snapshot.world;
		const auto route = snapshot.route;
		pick_entity(position,
		            [callback, world, route, owner, mouse_pos, position, pick_entity_filtered](
			            std::optional<gamestate::entity_id_t> picked) {
			auto finish = [callback, world, route, owner, mouse_pos, picked](
				std::optional<gamestate::entity_id_t> hit) {
				const auto foundation = world
					? queue_foundation_target(*world, owner, hit, mouse_pos.to_tile())
					: std::nullopt;
				callback(ShiftClick{picked, foundation, owner, route});
			};
			std::unordered_set<gamestate::entity_id_t> foundations;
			if (world and owner >= 2) {
				for (const auto &entity : world->entities) {
					if (entity.owner_id == owner and not entity.remembered
					    and entity.kind() == network::EntityKind::FOUNDATION) {
						foundations.insert(entity.entity_id);
					}
				}
			}
			// Queue targeting must not stop at an overlapping tower or unit.
			// Retain the unfiltered pick for non-Builder selection handling.
			if (not foundations.empty()) {
				pick_entity_filtered(position, finish,
				                     [foundations = std::move(foundations)](gamestate::entity_id_t id) {
					                     return foundations.contains(id);
				                     });
			}
			else {
				finish(picked);
			}
		});
	};

	auto queue_existing_foundation = [commands](
		const std::vector<gamestate::entity_id_t> &ids, const ShiftClick &click) {
		if (not click.foundation) {
			throw Error{ERR << "Foundation queue click has no foundation target"};
		}
		network::CommandIntent intent{0, network::CommandKind::PRIORITIZE_FOUNDATION, ids};
		intent.target_entity = *click.foundation;
		intent.queue = true;
		commands->dispatch(click.route, intent, click.owner, [](bool accepted) {
			if (not accepted) {
				log::log(WARN << "FVD foundation queue rejected: invalid Builder, unavailable site or queue full");
			}
		});
	};

	// Shift+right-click queues an existing foundation without entering
	// placement mode, matching the AoE2 queued-order gesture.
	binding_func_t queue_foundation_rmb{[resolve_queue_click, queue_existing_foundation, camera](
		const event_arguments &args, const std::shared_ptr<Controller> controller) {
		const auto owner = static_cast<gamestate::player_id_t>(controller->get_controlled());
		if (owner < 2) {
			return;
		}
		const auto mouse_pos = args.mouse.to_phys3(camera);
		std::weak_ptr<Controller> weak_controller = controller;
		resolve_queue_click(args.mouse, mouse_pos, owner,
		                    [weak_controller, queue_existing_foundation](ShiftClick click) {
			if (not click.foundation) {
				return;
			}
			auto controller = weak_controller.lock();
			if (not controller) {
				return;
			}
			auto action = [weak_controller, queue_existing_foundation, click]() {
				auto active = weak_controller.lock();
				if (not active or active->get_controlled() != click.owner) {
					return;
				}
				const auto selection = active->get_selection_details();
				if (selection.ids.size() != 1 or selection.kind != SelectionKind::BUILDER) {
					return;
				}
				queue_existing_foundation(selection.ids, click);
			};
			if (controller->defer_until_selection_applied(action)) {
				return;
			}
			action();
		});
	}};
	ctx->bind(Event{event_class::MOUSE_BUTTON, Qt::MouseButton::RightButton,
	                Qt::KeyboardModifier::ShiftModifier, QEvent::MouseButtonRelease},
	          binding_action{forward_action_t::SEND, queue_foundation_rmb});

	struct DragProjection {
		Eigen::Matrix4f matrix;
		util::Vector2s viewport;
		Eigen::Vector2f start;
		Eigen::Vector2f end;
	};

	auto process_drag_selection =
		[simulation, time_loop, camera, pick_entity, create_entity_event, hunter_route_event,
		 commands, queue_existing_foundation](const event_arguments &args,
	        const std::shared_ptr<Controller> &controller,
	        const std::optional<coord::input> &start,
	        const std::optional<coord::tile> &world_start,
	        const coord::phys3 &mouse_pos,
	        int drag_threshold,
	        const std::optional<DragProjection> &projection,
	        const std::optional<ShiftClick> &click) {
			const bool shift = (args.e.mod_code & Qt::ShiftModifier) != 0;
			const auto selection = controller->get_selection_details();
			auto pick_at_click = [pick_entity, click](
				const coord::input &position, const entity_pick_callback_t &callback) {
				if (click) {
					callback(click->picked);
				}
				else {
					pick_entity(position, callback);
				}
			};
			if (controller->is_garrison_mode_active()) {
				if (selection.kind != SelectionKind::BUILDER or selection.ids.size() != 1) {
					controller->cancel_garrison_mode();
					return;
				}
				const auto owner = static_cast<gamestate::player_id_t>(controller->get_controlled());
				const auto clicked_tile = mouse_pos.to_tile();
				// A thin tower silhouette may not produce a sprite pick.
				// Resolve the tile from the owner's authoritative presentation
				// snapshot instead, without relying on paused guest simulation.
				pick_at_click(args.mouse, [commands, controller, selection, owner, clicked_tile](
					std::optional<gamestate::entity_id_t> picked) {
					if (not controller->is_garrison_mode_active()
					    or controller->get_selected() != selection.ids) return;
					const auto snapshot = commands->snapshot();
					// Without a snapshot the pick is sent unchecked; the
					// simulation validates the target's type and owner.
					const auto tower = friendly_tower_target(
						snapshot.world.get(), owner, picked, clicked_tile);
					if (not tower) {
						log::log(INFO << "FVD garrison: click a friendly completed tower");
						return; // Keep T mode armed for another click or Esc.
					}
					controller->cancel_garrison_mode();
					network::CommandIntent intent{0, network::CommandKind::GARRISON, selection.ids};
					intent.target_entity = tower->id;
					commands->dispatch(snapshot.route, intent, owner, [](bool accepted) {
						if (not accepted) {
							log::log(WARN << "FVD garrison rejected: tower must be finished, empty and reachable");
						}
					});
				});
				return;
			}
			// Queueing an existing foundation leaves placement mode as it was.
			if (shift and click and click->foundation
			    and selection.kind == SelectionKind::BUILDER) {
				if (controller->get_controlled() == click->owner) {
					queue_existing_foundation(selection.ids, *click);
				}
				return;
			}
			if (shift and selection.kind == SelectionKind::BUILDER
			    and controller->begin_tower_placement()) {
				const auto drag_delta = start ? args.mouse - *start : coord::input_delta{0, 0};
				if (start and world_start
				    and (std::abs(drag_delta.x) >= drag_threshold
			               or std::abs(drag_delta.y) >= drag_threshold)) {
					const auto sites = tower_line_sites(*world_start,
					                                    mouse_pos.to_tile());
					// A single authoritative event preserves the line order even
					// when equal-time events execute newest-first.
					if (sites.size() > network::MAX_BUILD_LINE_SITES) {
						return;
					}
					std::vector<coord::phys3> line;
					line.reserve(sites.size());
					for (const auto &site : sites) {
						line.push_back(site.to_phys3_center());
					}
					network::CommandIntent intent{
						0, network::CommandKind::BUILD_TOWER, selection.ids, line.front()};
					intent.queue = true;
					intent.build_line = std::move(line);
					commands->dispatch(
						intent, static_cast<gamestate::player_id_t>(controller->get_controlled()));
				}
				else {
					if (not click) {
						throw Error{ERR << "Shift-click target was not captured"};
					}
					const auto current_selection = controller->get_selection_details();
					if (not controller->is_tower_placement_active()
					    or controller->get_controlled() != click->owner
					    or current_selection.ids != selection.ids
					    or current_selection.kind != SelectionKind::BUILDER) {
						return;
					}
					create_entity_event(mouse_pos, true, controller);
				}
				return;
			}
			if (controller->is_tower_placement_active()) {
				create_entity_event(mouse_pos, shift, controller);
				return;
			}
			if (controller->is_attack_move_active() or controller->is_patrol_active()) {
				hunter_route_event(args, controller);
				return;
			}
			auto selection_generation = controller->begin_selection_update();
			if (not start) {
				throw Error{ERR << "Drag select start not set."};
			}
			auto drag_start = *start;
			auto drag_delta = args.mouse - drag_start;
			if (auto replica = commands->guest_world()) {
				if (std::abs(drag_delta.x) < drag_threshold
				    and std::abs(drag_delta.y) < drag_threshold) {
					pick_at_click(args.mouse,
						[controller, commands, camera, selection, shift,
						 selection_generation, replica](
							std::optional<gamestate::entity_id_t> id) {
							if (id and apply_structure_click(controller, *commands, camera,
							    *id, selection_generation, selection, shift, false)) return;
							apply_guest_selection(controller, *replica, selection_generation,
								id ? std::vector<gamestate::entity_id_t>{*id}
								   : std::vector<gamestate::entity_id_t>{});
						});
				}
				else {
					// Drag-select locally owned Horses or a Builder from authoritative
					// positions, without querying the paused guest simulation.
					if (not projection) {
						throw Error{ERR << "Drag selection projection was not captured"};
					}
					std::vector<gamestate::entity_id_t> picked;
					for (const auto &entity : replica->entities) {
						if (entity.owner_id != replica->local_player_id
						    or not network::is_unit(entity.kind())) continue;
						auto scene = entity.position.to_scene3().to_world_space();
						Eigen::Vector4f clip = projection->matrix * Eigen::Vector4f{
							scene.x(), scene.y(), scene.z(), 1.0f};
						if (clip.w() <= 0) continue;
						double x = (clip.x() / clip.w() * 0.5 + 0.5) * projection->viewport[0];
						double y = (0.5 - clip.y() / clip.w() * 0.5) * projection->viewport[1];
						if (x >= std::min(drag_start.x, args.mouse.x)
						    and x <= std::max(drag_start.x, args.mouse.x)
						    and y >= std::min(drag_start.y, args.mouse.y)
						    and y <= std::max(drag_start.y, args.mouse.y)) {
							picked.push_back(entity.entity_id);
							// Builder controls currently accept exactly one unit.
							if (entity.kind() == network::EntityKind::BUILDER) break;
						}
					}
					apply_guest_selection(controller, *replica, selection_generation, picked);
				}
				return;
			}
			if (std::abs(drag_delta.x) < drag_threshold
			    and std::abs(drag_delta.y) < drag_threshold) {
				auto controlled = controller->get_controlled();
				pick_at_click(
					args.mouse,
					[simulation, time_loop, camera, controller, commands, selection,
						 shift, controlled, selection_generation](
						std::optional<gamestate::entity_id_t> id) {
						if (id and apply_structure_click(controller, *commands, camera,
						    *id, selection_generation, selection, shift, false)) return;
						std::vector<gamestate::entity_id_t> picked;
						if (id.has_value()) {
							picked.push_back(*id);
						}
						event::EventHandler::param_map::map_t params{
							{"controlled", controlled},
							{"entity_ids", picked},
							{"select_cb",
							 std::function<void(const std::vector<gamestate::entity_id_t>)>{
								 [controller, simulation, selection_generation](const std::vector<gamestate::entity_id_t> ids) {
									 apply_selection(controller, simulation->get_game()->get_state(),
									                 selection_generation, ids);
								 }}},
						};

						simulation->get_event_loop()->submit_event(
							"game.click_select",
							simulation->get_commander(),
							simulation->get_game()->get_state(),
							time_loop->get_clock()->get_time(),
							params);
					});
				return;
			}

			if (not projection) {
				throw Error{ERR << "Drag selection projection was not captured"};
			}
			event::EventHandler::param_map::map_t params{
				{"controlled", controller->get_controlled()},
				{"drag_start", projection->start},
				{"drag_end", projection->end},
				{"camera_matrix", projection->matrix},
				{"select_cb",
		         std::function<void(const std::vector<gamestate::entity_id_t> ids)>{
					 [controller, simulation, selection_generation](
						 const std::vector<gamestate::entity_id_t> ids) {
						 apply_selection(controller, simulation->get_game()->get_state(),
						                 selection_generation, ids);
					 }}},
			};

			simulation->get_event_loop()->submit_event(
				"game.drag_select",
				simulation->get_commander(),
				simulation->get_game()->get_state(),
				time_loop->get_clock()->get_time(),
				params);

		};

	binding_func_t drag_selection{
		[process_drag_selection, camera, resolve_queue_click](
			const event_arguments &args, const std::shared_ptr<Controller> controller) {
			if (controller->consume_structure_double_click_release()) {
				controller->reset_drag_select();
				return;
			}
			const auto start = controller->get_drag_select_start_optional();
			const auto world_start = controller->get_drag_world_start_optional();
			const auto mouse_pos = args.mouse.to_phys3(camera);
			controller->reset_drag_select();
			const bool shift = (args.e.mod_code & Qt::ShiftModifier) != 0;
			const auto drag_delta = start ? args.mouse - *start : coord::input_delta{0, 0};
			const auto threshold = QGuiApplication::styleHints()->startDragDistance();
			std::optional<DragProjection> projection;
			if (start and (std::abs(drag_delta.x) >= threshold
			               or std::abs(drag_delta.y) >= threshold)) {
				projection = DragProjection{
					camera->get_projection_matrix() * camera->get_view_matrix(),
					camera->get_viewport_size(),
					start->to_viewport(camera).to_ndc_space(camera),
					args.mouse.to_viewport(camera).to_ndc_space(camera)};
			}
			auto dispatch = [process_drag_selection, args, controller, start, world_start,
			                 mouse_pos, threshold, projection, shift](std::optional<ShiftClick> click) {
				std::weak_ptr<Controller> weak_controller = controller;
				auto action = [process_drag_selection, args, weak_controller, start, world_start,
				               mouse_pos, threshold, projection, click]() {
					if (auto active = weak_controller.lock()) {
						process_drag_selection(args, active, start, world_start, mouse_pos,
						                       threshold, projection, click);
					}
				};
				if (shift and controller->defer_until_selection_applied(action)) {
					return;
				}
				action();
			};
			if (not shift or (start and world_start
			                  and (std::abs(drag_delta.x) >= threshold
			                       or std::abs(drag_delta.y) >= threshold))) {
				dispatch(std::nullopt);
				return;
			}
			resolve_queue_click(args.mouse, mouse_pos,
			                    static_cast<gamestate::player_id_t>(controller->get_controlled()),
			                    [dispatch](ShiftClick click) { dispatch(click); });
		}};

	binding_action drag_selection_action{forward_action_t::CLEAR, drag_selection};
	Event ev_mouse_lmb_release{
		event_class::MOUSE_BUTTON,
		Qt::MouseButton::LeftButton,
		Qt::KeyboardModifier::NoModifier,
		QEvent::MouseButtonRelease};

	ctx->bind(ev_mouse_lmb_release, drag_selection_action);

	// Qt delivers a final left release after its double-click event. Process
	// the group here and suppress that release so it cannot undo the group.
	binding_func_t select_structure_group{
		[camera, pick_entity, commands](const event_arguments &args,
		                               const std::shared_ptr<Controller> controller) {
			controller->mark_structure_double_click();
			controller->reset_drag_select();
			if (controller->is_garrison_mode_active()
			    or controller->is_tower_placement_active()
			    or controller->is_attack_move_active()
			    or controller->is_patrol_active()) return;
			const auto previous = controller->get_selection_details();
			const auto generation = controller->begin_selection_update();
			pick_entity(args.mouse,
				[camera, controller, commands, previous, generation](
				    std::optional<gamestate::entity_id_t> id) {
					if (id and apply_structure_click(controller, *commands, camera,
					    *id, generation, previous, false, true)) return;
					// Ignore double-click on non-structures without losing the
					// selection created by the preceding single click.
					controller->set_selected_if_current(
					    generation, previous.ids, previous.kind, previous.render_entity);
				});
		}};
	ctx->bind(Event{event_class::MOUSE_BUTTON_DBL, Qt::MouseButton::LeftButton,
	                Qt::KeyboardModifier::NoModifier, QEvent::MouseButtonDblClick},
	          binding_action{forward_action_t::CLEAR, select_structure_group});
	// Shift+left release queues a click or a constrained multi-site drag.
	ctx->bind(Event{event_class::MOUSE_BUTTON, Qt::MouseButton::LeftButton,
	                Qt::KeyboardModifier::ShiftModifier, QEvent::MouseButtonRelease},
	          drag_selection_action);
}


} // namespace openage::input::game
