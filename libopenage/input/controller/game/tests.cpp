// Copyright 2026 the openage authors. See copying.md for legal info.

#include "controller.h"

#include "input/input_context.h"
#include "input/input_manager.h"
#include "testing/testing.h"

namespace openage::input::game::tests {

void tower_line_axis_lock() {
	using coord::tile;
	TESTEQUALS(tower_line_sites({10, 20}, {10, 20}), (std::vector<tile>{{10, 20}}));
	TESTEQUALS(tower_line_sites({10, 20}, {14, 21}),
	           (std::vector<tile>{{10, 20}, {11, 20}, {12, 20}, {13, 20}, {14, 20}}));
	TESTEQUALS(tower_line_sites({10, 20}, {7, 19}),
	           (std::vector<tile>{{10, 20}, {9, 20}, {8, 20}, {7, 20}}));
	TESTEQUALS(tower_line_sites({10, 20}, {11, 16}),
	           (std::vector<tile>{{10, 20}, {10, 19}, {10, 18}, {10, 17}, {10, 16}}));
	TESTEQUALS(tower_line_sites({10, 20}, {11, 21}),
	           (std::vector<tile>{{10, 20}, {11, 20}}));
}

void horse_selection_priority() {
	// 5 stands for the Hunter tower; 100+ are horses.
	auto is_horse = [](gamestate::entity_id_t id) { return id >= 100; };
	using ids = std::vector<gamestate::entity_id_t>;
	TESTEQUALS(units_take_priority({100, 5, 101}, is_horse), (ids{100, 101}));
	TESTEQUALS(units_take_priority({5, 102}, is_horse), (ids{102}));
	TESTEQUALS(units_take_priority({5}, is_horse), (ids{5}));
	TESTEQUALS(units_take_priority({}, is_horse), ids{});
}


void fvd_structure_group_selection() {
	using namespace openage::network;
	WorldState world{};
	world.map_size = 32;
	const auto tower = [](gamestate::entity_id_t id, gamestate::player_id_t owner,
	                      coord::tile at, const std::string &definition) {
		EntityState entity{};
		entity.entity_id = id;
		entity.owner_id = owner;
		entity.definition = definition;
		entity.position = at.to_phys3_center();
		entity.health = 100;
		entity.maximum_health = 200;
		return entity;
	};
	world.entities = {
		tower(10, 2, {2, 2}, "fvd.data.entities.Tower"),
		tower(11, 3, {3, 3}, "fvd.data.entities.Tower"),
		tower(12, 3, {4, 4}, "fvd.data.entities.Tower"),
		tower(13, 3, {5, 5}, "fvd.data.entities.Tower"),
		tower(20, 2, {6, 6}, "fvd.data.entities.Foundation"),
		tower(21, 3, {7, 7}, "fvd.data.entities.Foundation"),
		tower(30, 2, {8, 8}, "fvd.data.entities.Tower"),
		tower(31, 2, {9, 9}, "fvd.data.entities.Tower"),
	};
	world.constructions.push_back(ConstructionState{});
	world.constructions.back().site_entity_id = 30;
	world.constructions.back().phase = ConstructionPhase::UNDER_CONSTRUCTION;
	world.constructions.push_back(ConstructionState{});
	world.constructions.back().site_entity_id = 31;
	world.constructions.back().phase = ConstructionPhase::UNDER_CONSTRUCTION;
	const auto on_screen = [](const EntityState &entity) { return entity.entity_id != 13; };
	const auto first = select_fvd_structure(world, 2, 10, {}, false, false, on_screen);
	TESTEQUALS(first.has_value(), true);
	TESTEQUALS(first->kind == SelectionKind::TOWER, true);
	const auto allied = select_fvd_structure(world, 2, 11,
		SelectionDetails{first->ids, first->kind}, true, false, on_screen);
	TESTEQUALS(allied.has_value(), true);
	TESTEQUALS(allied->kind == SelectionKind::GROUP_TOWERS, true);
	TESTEQUALS(allied->ids == (std::vector<gamestate::entity_id_t>{10, 11}), true);
	const auto repeated = select_fvd_structure(world, 2, 11,
		SelectionDetails{allied->ids, allied->kind}, true, false, on_screen);
	TESTEQUALS(repeated->ids == allied->ids, true);
	const auto same_owner = select_fvd_structure(world, 2, 11, {}, false, true, on_screen);
	TESTEQUALS(same_owner->ids == (std::vector<gamestate::entity_id_t>{11, 12}), true);
	const auto switched = select_fvd_structure(world, 2, 20,
		SelectionDetails{allied->ids, allied->kind}, true, false, on_screen);
	TESTEQUALS(switched->ids == (std::vector<gamestate::entity_id_t>{20}), true);
	TESTEQUALS(switched->kind == SelectionKind::FOUNDATION, true);
	const auto foundations = select_fvd_structure(world, 2, 21,
		SelectionDetails{switched->ids, switched->kind}, true, false, on_screen);
	TESTEQUALS(foundations->kind == SelectionKind::GROUP_FOUNDATIONS, true);
	const auto constructing = select_fvd_structure(world, 2, 30, {}, false, true, on_screen);
	TESTEQUALS(constructing->kind == SelectionKind::GROUP_CONSTRUCTING_TOWERS, true);
	TESTEQUALS(constructing->ids.size(), size_t{2});
	const auto unfinished_first = select_fvd_structure(world, 2, 30, {}, false, false, on_screen);
	const auto unfinished_pair = select_fvd_structure(world, 2, 31,
		SelectionDetails{unfinished_first->ids, unfinished_first->kind},
		true, false, on_screen);
	TESTEQUALS(unfinished_pair->kind == SelectionKind::GROUP_CONSTRUCTING_TOWERS, true);
	TESTEQUALS(unfinished_pair->ids.size(), size_t{2});

	// Hunter can inspect only visible towers, never multi-select or see fog memory.
	TeamFog hunter{};
	hunter.team = PlayerRole::HUNTER;
	hunter.visible.resize(fog_word_count(world.map_size));
	const auto index = 3 * world.map_size + 3;
	hunter.visible[index / 64] |= uint64_t{1} << (index % 64);
	world.fog.push_back(std::move(hunter));
	const auto inspect = select_fvd_structure(world, 1, 11, {}, true, true, on_screen);
	TESTEQUALS(inspect->ids == (std::vector<gamestate::entity_id_t>{11}), true);
	TESTEQUALS(inspect->kind == SelectionKind::TOWER, true);
	TESTEQUALS(select_fvd_structure(world, 1, 12, {}, false, false, on_screen).has_value(), false);
	TESTEQUALS(select_fvd_structure(world, 1, 20, {}, false, false, on_screen).has_value(), false);
	world.entities[1].remembered = true;
	TESTEQUALS(select_fvd_structure(world, 1, 11, {}, false, false, on_screen).has_value(), false);

	Controller controller{{1, 2, 3}, 2};
	controller.set_selected({10, 11}, SelectionKind::GROUP_TOWERS);
	TESTEQUALS(controller.get_selection_details().kind == SelectionKind::GROUP_TOWERS, true);
	controller.mark_structure_double_click();
	TESTEQUALS(controller.consume_structure_double_click_release(), true);
	TESTEQUALS(controller.consume_structure_double_click_release(), false);
}

void tower_placement_mode() {
	network::WorldState world{};
	world.entities = {
		{10, 2, "fvd.data.entities.Foundation", {5.5, 5.5, 0}},
		{11, 3, "fvd.data.entities.Foundation", {6.5, 6.5, 0}},
		{12, 2, "fvd.data.entities.Builder", {7.5, 7.5, 0}},
		{13, 2, "fvd.data.entities.Foundation", {6.5, 5.5, 0}},
	};
	TESTEQUALS(queue_foundation_target(world, 2, 10, {7, 7}),
	           std::optional<gamestate::entity_id_t>{10});
	TESTEQUALS(queue_foundation_target(world, 2, 10, {6, 5}),
	           std::optional<gamestate::entity_id_t>{13});
	TESTEQUALS(queue_foundation_target(world, 2, 13, {5, 5}),
	           std::optional<gamestate::entity_id_t>{10});
	TESTEQUALS(queue_foundation_target(world, 2, 10, {6, 6}),
	           std::optional<gamestate::entity_id_t>{10});
	TESTEQUALS(queue_foundation_target(world, 2, std::nullopt, {5, 5}),
	           std::optional<gamestate::entity_id_t>{10});
	TESTEQUALS(queue_foundation_target(world, 2, 12, {5, 5}),
	           std::optional<gamestate::entity_id_t>{10});
	TESTEQUALS(queue_foundation_target(world, 2, 11, {6, 6}).has_value(), false);
	TESTEQUALS(queue_foundation_target(world, 1, 10, {5, 5}).has_value(), false);
	TESTEQUALS(queue_foundation_target(world, 2, std::nullopt, {7, 7}).has_value(), false);
	world.reservations.push_back({{6, 5}, 12, 1});
	TESTEQUALS(queue_foundation_target(world, 2, 10, {6, 5}),
	           std::optional<gamestate::entity_id_t>{10});
	TESTEQUALS(queue_foundation_target(world, 2, 13, {6, 5}),
	           std::optional<gamestate::entity_id_t>{13});
	TESTEQUALS(queue_foundation_target(world, 2, std::nullopt, {6, 5}),
	           std::optional<gamestate::entity_id_t>{13});
	TESTEQUALS(queue_foundation_target(world, 2, 13, {5, 5}),
	           std::optional<gamestate::entity_id_t>{10});
	TESTEQUALS(queue_foundation_target(world, 2, 11, {6, 5}),
	           std::optional<gamestate::entity_id_t>{13});
	world.reservations.push_back({{5, 5}, 12, 2});
	TESTEQUALS(queue_foundation_target(world, 2, 10, {6, 5}),
	           std::optional<gamestate::entity_id_t>{13});
	world.reservations.clear();
	world.entities[0].remembered = true;
	TESTEQUALS(queue_foundation_target(world, 2, 10, {5, 5}).has_value(), false);

	// A real Qt double click is a distinct input class. It must reach the
	// game binding rather than disappearing at InputManager's global context.
	auto routing = std::make_shared<input::InputContext>("double_click_regression");
	input::setup_defaults(routing);
	const input::Event dbl{input::event_class::MOUSE_BUTTON_DBL,
	                       Qt::MouseButton::LeftButton,
	                       Qt::KeyboardModifier::NoModifier,
	                       QEvent::MouseButtonDblClick};
	TESTEQUALS(routing->is_bound(dbl), true);
	const auto &actions = routing->lookup(dbl);
	TESTEQUALS(actions.size(), size_t{2});
	TESTEQUALS(actions.front().action_type == input::input_action_t::GAME, true);

	fvd_structure_group_selection();
	Controller controller{{1, 2, 3}, 2};
	controller.set_drag_select_start({320, 180}, {10, 20});
	TESTEQUALS(controller.get_drag_select_start(), (coord::input{320, 180}));
	TESTEQUALS(controller.get_drag_world_start_optional().value(), (coord::tile{10, 20}));
	controller.reset_drag_select();
	TESTEQUALS(controller.get_drag_select_start_optional().has_value(), false);
	TESTEQUALS(controller.get_drag_world_start_optional().has_value(), false);

	TESTEQUALS(controller.begin_tower_placement(), false);
	TESTEQUALS(controller.is_tower_placement_active(), false);
	TESTEQUALS(controller.begin_garrison_mode(), false);

	controller.set_selected({42}, SelectionKind::TOWER);
	TESTEQUALS(controller.begin_tower_placement(), false);
	TESTEQUALS(controller.get_selection_details().kind == SelectionKind::TOWER, true);
	TESTEQUALS(controller.is_tower_placement_active(), false);

	controller.set_selected({42}, SelectionKind::BUILDER);
	TESTEQUALS(controller.begin_garrison_mode(), true);
	TESTEQUALS(controller.is_garrison_mode_active(), true);
	controller.cancel_garrison_mode();
	TESTEQUALS(controller.is_garrison_mode_active(), false);
	TESTEQUALS(controller.begin_tower_placement(), true);
	TESTEQUALS(controller.is_tower_placement_active(), true);
	TESTEQUALS(controller.get_selected() == (std::vector<gamestate::entity_id_t>{42}), true);
	controller.cancel_tower_placement();
	TESTEQUALS(controller.is_tower_placement_active(), false);
	TESTEQUALS(controller.get_selected() == (std::vector<gamestate::entity_id_t>{42}), true);
	TESTEQUALS(controller.get_selection_details().kind == SelectionKind::BUILDER, true);
	TESTEQUALS(controller.begin_tower_placement(), true);
	TESTEQUALS(controller.is_tower_placement_active(), true);
	controller.cancel_tower_placement();
	TESTEQUALS(controller.get_selection_details().kind == SelectionKind::BUILDER, true);

	controller.begin_tower_placement();
	controller.set_selected({55}, SelectionKind::TOWER);
	TESTEQUALS(controller.get_selection_details().kind == SelectionKind::TOWER, true);
	TESTEQUALS(controller.is_tower_placement_active(), false);
	TESTEQUALS(controller.begin_tower_placement(), false);
	controller.set_selected({42}, SelectionKind::BUILDER);

	controller.begin_tower_placement();
	controller.set_selected({});
	TESTEQUALS(controller.is_tower_placement_active(), false);

	controller.set_selected({42}, SelectionKind::BUILDER);
	controller.begin_tower_placement();
	controller.set_control(1);
	TESTEQUALS(controller.is_tower_placement_active(), false);
	TESTEQUALS(controller.begin_tower_placement(), false);

	controller.set_control(2);
	controller.set_selected({42, 43}, SelectionKind::BUILDER);
	TESTEQUALS(controller.begin_tower_placement(), false);
	TESTEQUALS(controller.get_selection_details().kind == SelectionKind::NONE, true);
	controller.set_selected({43}, SelectionKind::TOWER);
	TESTEQUALS(controller.begin_tower_placement(), false);
	TESTEQUALS(controller.is_tower_placement_active(), false);
	TESTEQUALS(controller.get_selected() == (std::vector<gamestate::entity_id_t>{43}), true);

	controller.set_control(1);
	controller.set_selected({101}, SelectionKind::HORSE);
	TESTEQUALS(controller.begin_attack_move(), true);
	TESTEQUALS(controller.is_attack_move_active(), true);
	controller.cancel_hunter_targeting();
	TESTEQUALS(controller.is_attack_move_active(), false);
	// Patrol and AttackMove share one targeting mode, so either replaces the other.
	TESTEQUALS(controller.begin_patrol(), true);
	TESTEQUALS(controller.is_patrol_active(), true);
	TESTEQUALS(controller.is_attack_move_active(), false);
	TESTEQUALS(controller.begin_attack_move(), true);
	TESTEQUALS(controller.is_patrol_active(), false);
	TESTEQUALS(controller.begin_patrol(), true);
	controller.cancel_hunter_targeting();
	TESTEQUALS(controller.is_patrol_active(), false);
	controller.set_selected({101, 102, 103}, SelectionKind::GROUP_HORSES);
	TESTEQUALS(controller.begin_attack_move(), true);
	// HUD buttons and shortcuts use one validated stance command path.
	int stance_commands = 0;
	auto last_stance = gamestate::HunterStance::OBSERVE;
	controller.set_hunter_stance_action([&](gamestate::HunterStance stance) {
		++stance_commands;
		last_stance = stance;
	});
	TESTEQUALS(controller.issue_hunter_stance(gamestate::HunterStance::AGGRESSIVE), true);
	TESTEQUALS(stance_commands, 1);
	TESTEQUALS(last_stance == gamestate::HunterStance::AGGRESSIVE, true);
	TESTEQUALS(controller.issue_hunter_stance(gamestate::HunterStance::DEFENSIVE), true);
	TESTEQUALS(controller.issue_hunter_stance(gamestate::HunterStance::OBSERVE), true);
	TESTEQUALS(stance_commands, 3);
	TESTEQUALS(last_stance == gamestate::HunterStance::OBSERVE, true);
	controller.set_control(2);
	TESTEQUALS(controller.issue_hunter_stance(gamestate::HunterStance::AGGRESSIVE), false);
	TESTEQUALS(stance_commands, 3);
	TESTEQUALS(controller.is_attack_move_active(), false);
	TESTEQUALS(controller.begin_attack_move(), false);
	TESTEQUALS(controller.begin_patrol(), false);
	TESTEQUALS(controller.is_hunter_selection(), false);

	// T after a click but before async type resolution should enter garrison
	// only after the correct Builder selection is applied.
	controller.set_selected({42}, SelectionKind::BUILDER);
	auto pending_builder = controller.begin_selection_update();
	TESTEQUALS(controller.begin_garrison_mode(), true);
	TESTEQUALS(controller.is_garrison_mode_active(), false);
	TESTEQUALS(controller.set_selected_if_current(
		pending_builder, {42}, SelectionKind::BUILDER), true);
	TESTEQUALS(controller.is_garrison_mode_active(), true);
	controller.cancel_garrison_mode();
	auto pending_tower = controller.begin_selection_update();
	TESTEQUALS(controller.begin_garrison_mode(), true);
	TESTEQUALS(controller.set_selected_if_current(
		pending_tower, {33}, SelectionKind::TOWER), true);
	TESTEQUALS(controller.is_garrison_mode_active(), false);
	TESTEQUALS(controller.get_selected().front(), gamestate::entity_id_t{33});
	TESTEQUALS(controller.get_selection_details().kind == SelectionKind::TOWER, true);
	bool deferred_ran = false;
	auto stale_generation = controller.begin_selection_update();
	controller.defer_until_selection_applied([&]() { deferred_ran = true; }) or TESTFAIL;
	auto current_generation = controller.begin_selection_update();
	TESTEQUALS(controller.set_selected_if_current(
		stale_generation, {101}, SelectionKind::HORSE), false);
	TESTEQUALS(controller.get_selected().front(), gamestate::entity_id_t{33});
	TESTEQUALS(deferred_ran, false);
	controller.defer_until_selection_applied([&]() {
		deferred_ran = controller.get_selected().size() == 2;
	}) or TESTFAIL;
	TESTEQUALS(controller.set_selected_if_current(
		current_generation, {101, 102}, SelectionKind::GROUP_HORSES), true);
	TESTEQUALS(deferred_ran, true);
	TESTEQUALS(controller.get_selected().size(), size_t{2});

	auto invalidated_generation = controller.begin_selection_update();
	controller.defer_until_selection_applied([&]() { deferred_ran = false; }) or TESTFAIL;
	controller.set_control(1);
	TESTEQUALS(controller.set_selected_if_current(
		invalidated_generation, {101}, SelectionKind::HORSE), false);
	TESTEQUALS(deferred_ran, true);

	// Online role draw locks the faction despite the solo test shortcuts.
	controller.lock_to_online_player(1);
	TESTEQUALS(controller.get_controlled(), size_t{1});
	controller.set_control(2);
	TESTEQUALS(controller.get_controlled(), size_t{1});
	controller.set_selected({101}, SelectionKind::HORSE);
	TESTEQUALS(controller.begin_attack_move(), true);
	controller.reset_online_player_slots(8);
	TESTEQUALS(controller.is_attack_move_active(), false);
	controller.set_control(2);
	TESTEQUALS(controller.get_controlled(), size_t{2});
}

} // namespace openage::input::game::tests
