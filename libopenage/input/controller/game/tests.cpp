// Copyright 2026 the openage authors. See copying.md for legal info.

#include "controller.h"

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

void tower_placement_mode() {
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
	bool deferred_ran = false;
	auto stale_generation = controller.begin_selection_update();
	controller.defer_until_selection_applied([&]() { deferred_ran = true; }) or TESTFAIL;
	auto current_generation = controller.begin_selection_update();
	TESTEQUALS(controller.set_selected_if_current(
		stale_generation, {101}, SelectionKind::HORSE), false);
	TESTEQUALS(controller.get_selected().front(), gamestate::entity_id_t{42});
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
