// Copyright 2021-2023 the openage authors. See copying.md for legal info.

#include "controller.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <iterator>
#include <utility>

#include "error/error.h"
#include "log/log.h"

#include "input/controller/game/binding_context.h"
#include "network/entity_kind.h"
#include "util/stall_profiler.h"

#include "coord/phys.h"
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


} // namespace openage::input::game
