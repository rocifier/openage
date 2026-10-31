// Copyright 2026 the openage authors. See copying.md for legal info.

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <vector>
#include <memory>
#include <unordered_map>

#include "assets/mod_manager.h"
#include "config.h"
#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "gamestate/api/ability.h"
#include "gamestate/api/animation.h"
#include "gamestate/api/property.h"
#include "gamestate/api/types.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/internal/commands/build_tower.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/entity_factory.h"
#include "gamestate/event/drag_select.h"
#include "gamestate/event/process_command.h"
#include "gamestate/event/send_command.h"
#include "gamestate/event/spawn_entity.h"
#include "gamestate/event/wait.h"
#include "gamestate/fvd.h"
#include "gamestate/fvd_test_support.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "gamestate/player.h"
#include "gamestate/simulation.h"
#include "gamestate/system/idle.h"
#include "gamestate/system/move.h"
#include "gamestate/terrain.h"
#include "gamestate/terrain_chunk.h"
#include "gamestate/terrain_factory.h"
#include "gamestate/terrain_tile.h"
#include "network/command_bridge.h"
#include "network/construction_presentation.h"
#include "network/match_connection_state.h"
#include "pathfinding/cost_field.h"
#include "pathfinding/definitions.h"
#include "pathfinding/grid.h"
#include "pathfinding/pathfinder.h"
#include "pathfinding/sector.h"
#include "renderer/resources/animation/angle_info.h"
#include "renderer/resources/animation/frame_info.h"
#include "renderer/resources/frame_timing.h"
#include "renderer/resources/parser/parse_sprite.h"
#include "renderer/resources/parser/parse_texture.h"
#include "renderer/resources/parser/parse_terrain.h"
#include "renderer/resources/texture_data.h"
#include "renderer/resources/texture_info.h"
#include "renderer/stages/world/render_entity.h"
#include "testing/testing.h"
#include "time/time_loop.h"
#include "util/fslike/directory.h"
#include "util/path.h"

namespace openage::gamestate::tests {

/**
 * FVD's base scenario always starts with seven Builders; the authoritative
 * multiplayer simulation must remove unadmitted ones before it snapshots.
 */
void fvd_network_builder_count() {
	auto root = util::Path{
		std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	for (size_t admitted : {size_t{2}, size_t{3}, size_t{8}}) {
		auto loop = std::make_shared<openage::event::EventLoop>();
		loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
		loop->add_event_handler(std::make_shared<event::WaitHandler>());
		auto entities = std::make_shared<EntityFactory>();
		auto terrain = std::make_shared<TerrainFactory>();
		Game game{loop, mods, entities, terrain};
		auto state = game.get_state();
		TESTEQUALS(state->get_fvd()->get_player_count(), size_t{8});
		auto count_builders = [&]() {
			size_t count{};
			for (const auto &[id, entity] : state->get_game_entities()) {
				(void)id;
				if (entity->get_definition() == "fvd.data.entities.Builder") {
					++count;
				}
			}
			return count;
		};
		TESTEQUALS(count_builders(), size_t{7});
		TESTTHROWS(prune_unassigned_fvd_builders(state, 1, time::TIME_ZERO));
		TESTTHROWS(prune_unassigned_fvd_builders(state, 9, time::TIME_ZERO));
		TESTEQUALS(count_builders(), size_t{7});
		TESTEQUALS(prune_unassigned_fvd_builders(
			state, admitted, time::TIME_ZERO), size_t{8} - admitted);
		TESTEQUALS(count_builders(), admitted - 1);
		TESTEQUALS(prune_unassigned_fvd_builders(
			state, admitted, time::TIME_ZERO), size_t{0});
		// Test that real Position::Segmented future angle jumps are exported
		// only once per time (rather than both same-time left/right keys).
		if (admitted == 2) {
			for (const auto &[id, entity] : state->get_game_entities()) {
				(void)id;
				if (entity->get_definition() != "fvd.data.entities.Builder") continue;
				auto owner = std::dynamic_pointer_cast<component::Ownership>(
					entity->get_component(component::component_t::OWNERSHIP));
				if (owner->get_owners().get(time::TIME_ZERO) != player_id_t{2}) continue;
				auto position = std::dynamic_pointer_cast<component::Position>(
					entity->get_component(component::component_t::POSITION));
				position->set_angle(time::time_t::from_int(1),
				                    coord::phys_angle_t::from_int(90));
				position->set_angle(time::time_t::from_int(2),
				                    coord::phys_angle_t::from_int(180));
				break;
			}
		}
		network::MatchConnectionState connections{8};
		auto captured = network::capture_fvd_state(
			state, connections, time::TIME_ZERO, 0, 1, 2);
		size_t replicated_builders{};
		for (const auto &entity : captured.entities) {
			if (entity.definition != "fvd.data.entities.Builder") continue;
			++replicated_builders;
			TESTEQUALS(entity.owner_id >= 2
			           and entity.owner_id <= admitted, true);
		}
		// Two players -> one Builder, three -> two, eight -> seven.
		TESTEQUALS(replicated_builders, admitted - 1);
		if (admitted == 2) {
			auto builder = std::find_if(captured.entities.begin(), captured.entities.end(),
				[](const auto &entry) { return entry.owner_id == 2; });
			TESTEQUALS(builder != captured.entities.end(), true);
			TESTEQUALS(builder->angle, coord::phys_angle_t::from_int(315));
			TESTEQUALS(builder->facing_keys.size(), size_t{2});
			TESTEQUALS(builder->facing_keys[0].time, time::time_t::from_int(1));
			TESTEQUALS(builder->facing_keys[0].angle, coord::phys_angle_t::from_int(90));
			TESTEQUALS(builder->facing_keys[1].time, time::time_t::from_int(2));
			TESTEQUALS(builder->facing_keys[1].angle, coord::phys_angle_t::from_int(180));
			auto played = network::capture_fvd_state(
				state, connections, time::time_t::from_double(1.5), 1, 2, 2);
			auto played_builder = std::find_if(played.entities.begin(), played.entities.end(),
				[](const auto &entry) { return entry.owner_id == 2; });
			TESTEQUALS(played_builder != played.entities.end(), true);
			TESTEQUALS(played_builder->angle, coord::phys_angle_t::from_int(90));
			TESTEQUALS(played_builder->facing_keys.size(), size_t{1});
			TESTEQUALS(played_builder->facing_keys[0].time, time::time_t::from_int(2));
			// The actual guest render-entity curve holds the host's facing
			// through each turn, without interpolating an invented rotation.
			renderer::world::RenderEntity proxy{};
			proxy.update_facing(builder->angle, time::TIME_ZERO, {
				{builder->facing_keys[0].time, builder->facing_keys[0].angle},
				{builder->facing_keys[1].time, builder->facing_keys[1].angle},
			});
			{
				auto lock = proxy.get_read_lock();
				TESTEQUALS(proxy.get_angle().get(time::time_t::from_double(0.5)),
				           coord::phys_angle_t::from_int(315));
				TESTEQUALS(proxy.get_angle().get(time::time_t::from_double(1.5)),
				           coord::phys_angle_t::from_int(90));
				TESTEQUALS(proxy.get_angle().get(time::time_t::from_double(2.5)),
				           coord::phys_angle_t::from_int(180));
			}
			proxy.update_facing(played_builder->angle,
				time::time_t::from_double(1.5), {
					{played_builder->facing_keys[0].time,
					 played_builder->facing_keys[0].angle},
				});
			{
				auto lock = proxy.get_read_lock();
				TESTEQUALS(proxy.get_angle().get(time::time_t::from_double(1.75)),
				           coord::phys_angle_t::from_int(90));
				TESTEQUALS(proxy.get_angle().get(time::time_t::from_int(2)),
				           coord::phys_angle_t::from_int(180));
			}
		}
	}
}

void fvd_scenario() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto graphics = root / "assets" / "fvd" / "graphics";
	auto plains_graphics = renderer::resources::parser::parse_terrain_file(graphics / "grass.terrain");
	auto builder_graphics = renderer::resources::parser::parse_sprite_file(graphics / "builder.sprite");
	auto foundation_graphics = renderer::resources::parser::parse_sprite_file(graphics / "foundation.sprite");
	auto tower_graphics = renderer::resources::parser::parse_sprite_file(graphics / "tower.sprite");
	auto tower_demolition_graphics = renderer::resources::parser::parse_sprite_file(
		graphics / "fvd_stone_tower_demolition.sprite");
	TESTEQUALS(plains_graphics.get_texture_count(), 1);
	TESTEQUALS(builder_graphics.get_texture_count(), 1);
	auto animated_idle = renderer::resources::parser::parse_sprite_file(graphics / "orc_idle.sprite");
	auto animated_walk = renderer::resources::parser::parse_sprite_file(graphics / "orc_walk.sprite");
	auto horse_idle1 = renderer::resources::parser::parse_sprite_file(
		graphics / "ethereal-hunter-horse" / "ethereal-hunter-horse-idle1.sprite");
	auto horse_idle2 = renderer::resources::parser::parse_sprite_file(
		graphics / "ethereal-hunter-horse" / "ethereal-hunter-horse-idle2.sprite");
	auto horse_gallop = renderer::resources::parser::parse_sprite_file(
		graphics / "ethereal-hunter-horse" / "ethereal-hunter-horse-gallop.sprite");
	auto horse_attack = renderer::resources::parser::parse_sprite_file(
		graphics / "ethereal-hunter-horse" / "ethereal-hunter-horse-attack.sprite");
	auto horse_death = renderer::resources::parser::parse_sprite_file(
		graphics / "ethereal-hunter-horse" / "ethereal-hunter-horse-death.sprite");
	TESTEQUALS(animated_idle.get_texture_count(), size_t{16});
	TESTEQUALS(animated_walk.get_texture_count(), size_t{16});
	TESTEQUALS(horse_idle1.get_texture_count(), size_t{2});
	TESTEQUALS(horse_idle2.get_texture_count(), size_t{1});
	TESTEQUALS(horse_gallop.get_texture_count(), size_t{1});
	TESTEQUALS(horse_attack.get_texture_count(), size_t{1});
	TESTEQUALS(horse_death.get_texture_count(), size_t{4});
	const auto &idle_layer = animated_idle.get_layer(0);
	const auto &walk_layer = animated_walk.get_layer(0);
	const auto &horse_idle1_layer = horse_idle1.get_layer(0);
	const auto &horse_idle2_layer = horse_idle2.get_layer(0);
	const auto &horse_gallop_layer = horse_gallop.get_layer(0);
	const auto &horse_attack_layer = horse_attack.get_layer(0);
	const auto &horse_death_layer = horse_death.get_layer(0);
	const auto &tower_layer = tower_graphics.get_layer(0);
	const auto &tower_demolition_layer = tower_demolition_graphics.get_layer(0);
	TESTEQUALS(idle_layer.get_display_mode(),
	           renderer::resources::display_mode::LOOP);
	TESTEQUALS(walk_layer.get_display_mode(),
	           renderer::resources::display_mode::LOOP);
	TESTEQUALS(horse_idle1_layer.get_display_mode(),
	           renderer::resources::display_mode::LOOP);
	TESTEQUALS(horse_idle2_layer.get_display_mode(),
	           renderer::resources::display_mode::LOOP);
	TESTEQUALS(horse_gallop_layer.get_display_mode(),
	           renderer::resources::display_mode::LOOP);
	TESTEQUALS(horse_attack_layer.get_display_mode(),
	           renderer::resources::display_mode::LOOP);
	TESTEQUALS(horse_death_layer.get_display_mode(),
	           renderer::resources::display_mode::ONCE);
	TESTEQUALS(tower_layer.get_display_mode(),
	           renderer::resources::display_mode::LOOP);
	TESTEQUALS(tower_demolition_layer.get_display_mode(),
	           renderer::resources::display_mode::ONCE);
	TESTEQUALS(tower_demolition_layer.get_time_per_frame(), 0.125f);
	TESTEQUALS(tower_demolition_layer.get_frame_timing()->size(), size_t{14});
	TESTEQUALS(horse_idle1_layer.get_angle_count(), size_t{16});
	TESTEQUALS(horse_idle2_layer.get_angle_count(), size_t{16});
	TESTEQUALS(horse_gallop_layer.get_angle_count(), size_t{16});
	TESTEQUALS(horse_attack_layer.get_angle_count(), size_t{16});
	TESTEQUALS(horse_death_layer.get_angle_count(), size_t{16});
	// A frame duration is measured in seconds, not milliseconds.
	TESTEQUALS(idle_layer.get_time_per_frame(), 0.125f);
	TESTEQUALS(walk_layer.get_time_per_frame(), 0.07f);
	TESTEQUALS(horse_gallop_layer.get_time_per_frame(), 0.08f);
	TESTEQUALS(horse_idle1_layer.get_time_per_frame(), 0.15f);
	TESTEQUALS(horse_idle2_layer.get_time_per_frame(), 0.4f);
	TESTEQUALS(horse_attack_layer.get_time_per_frame(), 0.0375f);
	TESTEQUALS(horse_death_layer.get_time_per_frame(), 0.05f);
	TESTEQUALS(tower_layer.get_time_per_frame(), 0.125f);
	const auto idle_frames = idle_layer.get_frame_timing();
	const auto walk_frames = walk_layer.get_frame_timing();
	TESTEQUALS(idle_frames->size(), size_t{16});
	TESTEQUALS(walk_frames->size(), size_t{20});
	TESTEQUALS(horse_gallop_layer.get_frame_timing()->size(), size_t{12});
	TESTEQUALS(horse_idle1_layer.get_frame_timing()->size(), size_t{24});
	TESTEQUALS(horse_idle2_layer.get_frame_timing()->size(), size_t{12});
	TESTEQUALS(horse_attack_layer.get_frame_timing()->size(), size_t{16});
	TESTEQUALS(horse_death_layer.get_frame_timing()->size(), size_t{60});
	TESTEQUALS(tower_layer.get_frame_timing()->size(), size_t{16});
	const auto &tower_angle = tower_layer.get_direction_angle(0.0f);
	TESTEQUALS(tower_angle->get_frame_count(), size_t{16});
	for (size_t frame = 0; frame < 16; ++frame) {
		TESTEQUALS(tower_angle->get_frame(frame)->get_texture_idx(), size_t{0});
		TESTEQUALS(tower_angle->get_frame(frame)->get_subtexture_idx(), frame);
	}
	const std::array<std::pair<float, size_t>, 16> horse_direction_rows{{
		{0.0f, 15},
		{22.0f, 14},
		{45.0f, 13},
		{67.0f, 12},
		{90.0f, 11},
		{112.0f, 10},
		{135.0f, 9},
		{157.0f, 8},
		{180.0f, 7},
		{202.0f, 6},
		{225.0f, 5},
		{247.0f, 4},
		{270.0f, 3},
		{292.0f, 2},
		{315.0f, 1},
		{337.0f, 0},
	}};
	for (const auto &[angle, row] : horse_direction_rows) {
		const auto &idle1_angle = horse_idle1_layer.get_direction_angle(angle);
		const auto &idle2_angle = horse_idle2_layer.get_direction_angle(angle);
		const auto &gallop_angle = horse_gallop_layer.get_direction_angle(angle);
		const auto &attack_angle = horse_attack_layer.get_direction_angle(angle);
		const auto &death_angle = horse_death_layer.get_direction_angle(angle);
		TESTEQUALS(idle1_angle->get_frame_count(), size_t{24});
		TESTEQUALS(idle2_angle->get_frame_count(), size_t{12});
		const auto idle1_texture = row < 8 ? size_t{0} : size_t{1};
		const auto idle1_row = row % 8;
		for (size_t frame = 0; frame < 24; ++frame) {
			TESTEQUALS(idle1_angle->get_frame(frame)->get_texture_idx(), idle1_texture);
			TESTEQUALS(idle1_angle->get_frame(frame)->get_subtexture_idx(), idle1_row * 24 + frame);
		}
		for (size_t frame = 0; frame < 12; ++frame) {
			TESTEQUALS(idle2_angle->get_frame(frame)->get_texture_idx(), size_t{0});
			TESTEQUALS(idle2_angle->get_frame(frame)->get_subtexture_idx(), row * 12 + frame);
		}
		TESTEQUALS(gallop_angle->get_frame_count(), size_t{12});
		TESTEQUALS(gallop_angle->get_frame(0)->get_subtexture_idx(), row * 12);
		TESTEQUALS(gallop_angle->get_frame(11)->get_subtexture_idx(), row * 12 + 11);
		TESTEQUALS(attack_angle->get_frame_count(), size_t{16});
		TESTEQUALS(attack_angle->get_frame(0)->get_subtexture_idx(), row * 16);
		TESTEQUALS(attack_angle->get_frame(15)->get_subtexture_idx(), row * 16 + 15);
		TESTEQUALS(death_angle->get_frame_count(), size_t{60});
		const auto death_texture = row / 4;
		const auto death_row = row % 4;
		for (size_t frame = 0; frame < 60; ++frame) {
			TESTEQUALS(death_angle->get_frame(frame)->get_texture_idx(), death_texture);
			TESTEQUALS(death_angle->get_frame(frame)->get_subtexture_idx(),
			           death_row * 60 + frame);
		}
	}
	TESTEQUALS(idle_frames->get_frame(time::time_t::from_double(0.26),
	                                  time::TIME_ZERO), size_t{2});
	TESTEQUALS(idle_frames->get_frame(time::time_t::from_double(1.95),
	                                  time::TIME_ZERO), size_t{15});
	TESTEQUALS(idle_frames->get_frame(time::time_t::from_double(2.05),
	                                  time::TIME_ZERO), size_t{0});
	TESTEQUALS(walk_frames->get_frame(time::time_t::from_double(0.16),
	                                  time::TIME_ZERO), size_t{2});
	TESTEQUALS(walk_frames->get_frame(time::time_t::from_double(1.38),
	                                  time::TIME_ZERO), size_t{19});
	TESTEQUALS(walk_frames->get_frame(time::time_t::from_double(1.45),
	                                  time::TIME_ZERO), size_t{0});
	const auto horse_idle1_frames = horse_idle1_layer.get_frame_timing();
	const auto horse_idle2_frames = horse_idle2_layer.get_frame_timing();
	TESTEQUALS(horse_idle1_frames->get_frame(time::time_t::from_double(0.91),
	                                        time::TIME_ZERO), size_t{6});
	TESTEQUALS(horse_idle1_frames->get_frame(time::time_t::from_double(5.9),
	                                        time::TIME_ZERO), size_t{15});
	TESTEQUALS(horse_idle2_frames->get_frame(time::time_t::from_double(0.9),
	                                        time::TIME_ZERO), size_t{2});
	TESTEQUALS(horse_idle2_frames->get_frame(time::time_t::from_double(5.9),
	                                        time::TIME_ZERO), size_t{2});
	// Phase is relative to the host's start time, not packet arrival.
	TESTEQUALS(walk_frames->get_frame(time::time_t::from_double(10.16),
	                                  time::time_t::from_int(10)), size_t{2});
	auto idle_sheet = renderer::resources::parser::parse_texture_file(
		graphics / "orc" / "x256p_Spritesheets" / "Idle_Unarmed"
		         / "Idle_Unarmed_Body_180.texture");
	auto walk_sheet = renderer::resources::parser::parse_texture_file(
		graphics / "orc" / "x256p_Spritesheets" / "Walk_Unarmed"
		         / "Walk_Unarmed_Body_180.texture");
	TESTEQUALS(idle_sheet.get_size(), (std::pair<int32_t, int32_t>{1024, 1024}));
	TESTEQUALS(walk_sheet.get_size(), (std::pair<int32_t, int32_t>{1280, 1024}));
	TESTEQUALS(idle_sheet.get_subtex_count(), size_t{16});
	TESTEQUALS(walk_sheet.get_subtex_count(), size_t{20});
	auto horse_sheet = renderer::resources::parser::parse_texture_file(
		graphics / "ethereal-hunter-horse" / "ethereal-hunter-horse-gallop.texture");
	TESTEQUALS(horse_sheet.get_size(), (std::pair<int32_t, int32_t>{3072, 4096}));
	TESTEQUALS(horse_sheet.get_subtex_count(), size_t{192});
	auto horse_attack_sheet = renderer::resources::parser::parse_texture_file(
		graphics / "ethereal-hunter-horse" / "ethereal-hunter-horse-attack.texture");
	TESTEQUALS(horse_attack_sheet.get_size(), (std::pair<int32_t, int32_t>{4096, 4096}));
	TESTEQUALS(horse_attack_sheet.get_subtex_count(), size_t{256});
	for (const auto &death_name : {"ethereal-hunter-horse-death_page_00.texture",
	                               "ethereal-hunter-horse-death_page_01.texture",
	                               "ethereal-hunter-horse-death_page_02.texture",
	                               "ethereal-hunter-horse-death_page_03.texture"}) {
		auto death_sheet = renderer::resources::parser::parse_texture_file(
			graphics / "ethereal-hunter-horse" / death_name);
		TESTEQUALS(death_sheet.get_size(), (std::pair<int32_t, int32_t>{4096, 4096}));
		TESTEQUALS(death_sheet.get_subtex_count(), size_t{240});
		TESTEQUALS(death_sheet.get_subtex_info(0).get_anchor_pos()[0], 128);
		TESTEQUALS(death_sheet.get_subtex_info(0).get_anchor_pos()[1], 160);
		TESTEQUALS(death_sheet.get_subtex_info(239).get_anchor_pos()[0], 128);
		TESTEQUALS(death_sheet.get_subtex_info(239).get_anchor_pos()[1], 160);
	}
	auto tower_sheet = renderer::resources::parser::parse_texture_file(
		graphics / "medieval-tower" / "medieval_tower.texture");
	TESTEQUALS(tower_sheet.get_size(), (std::pair<int32_t, int32_t>{4096, 1024}));
	TESTEQUALS(tower_sheet.get_subtex_count(), size_t{16});
	TESTEQUALS(tower_sheet.get_subtex_info(0).get_size()[0], uint32_t{512});
	TESTEQUALS(tower_sheet.get_subtex_info(0).get_size()[1], uint32_t{512});
	TESTEQUALS(tower_sheet.get_subtex_info(0).get_anchor_pos()[0], 318);
	TESTEQUALS(tower_sheet.get_subtex_info(0).get_anchor_pos()[1], 403);
	TESTEQUALS(tower_sheet.get_subtex_info(15).get_size()[0], uint32_t{512});
	TESTEQUALS(tower_sheet.get_subtex_info(15).get_size()[1], uint32_t{512});
	TESTEQUALS(tower_sheet.get_subtex_info(15).get_anchor_pos()[0], 318);
	TESTEQUALS(tower_sheet.get_subtex_info(15).get_anchor_pos()[1], 403);
	auto tower_demolition_sheet = renderer::resources::parser::parse_texture_file(
		graphics / "fvd_stone_tower_demolition.texture");
	TESTEQUALS(tower_demolition_sheet.get_size(),
	           (std::pair<int32_t, int32_t>{1024, 1024}));
	TESTEQUALS(tower_demolition_sheet.get_subtex_count(), size_t{14});
	TESTEQUALS(tower_demolition_sheet.get_subtex_info(13).get_anchor_pos()[0], 128);
	TESTEQUALS(tower_demolition_sheet.get_subtex_info(13).get_anchor_pos()[1], 206);
	for (const auto &idle_name : {"ethereal-hunter-horse-idle1_page_00.texture",
	                              "ethereal-hunter-horse-idle1_page_01.texture",
	                              "ethereal-hunter-horse-idle2.texture"}) {
		auto idle_sheet = renderer::resources::parser::parse_texture_file(
			graphics / "ethereal-hunter-horse" / idle_name);
		TESTEQUALS(idle_sheet.get_size(), (std::pair<int32_t, int32_t>{3072, 4096}));
		TESTEQUALS(idle_sheet.get_subtex_count(), size_t{192});
	}
	TESTEQUALS(foundation_graphics.get_texture_count(), 1);
	TESTEQUALS(tower_graphics.get_texture_count(), 1);
	TESTEQUALS(tower_graphics.get_scalefactor(), 0.5f);
	TESTEQUALS(horse_death.get_scalefactor(), 2.0f);
	for (const auto &info : {plains_graphics.get_texture(0),
	                        builder_graphics.get_texture(0),
	                        horse_idle1.get_texture(0),
	                        horse_idle1.get_texture(1),
	                        horse_idle2.get_texture(0),
	                        horse_attack.get_texture(0),
	                        horse_death.get_texture(0),
	                        horse_death.get_texture(1),
	                        horse_death.get_texture(2),
	                        horse_death.get_texture(3),
	                        foundation_graphics.get_texture(0),
	                        tower_graphics.get_texture(0)}) {
		auto texture = renderer::resources::Texture2dData{info->get_image_path().value()};
		TESTEQUALS(info->get_cbits(), false);
		TESTEQUALS(texture.get_info().get_size().first, info->get_size().first);
		TESTEQUALS(texture.get_info().get_size().second, info->get_size().second);
	}
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();

	// The guest's replicated units do not all exist locally (Horses spawn
	// only on the authoritative host). Resolve the same namespaced sprite
	// references used by Idle::idle for all four FVD entity definitions.
	const std::array<std::pair<std::string, size_t>, 3> idle_resources{{
		{"fvd.data.entities.Builder", 16},
		{"fvd.data.entities.Tower", 1},
		{"fvd.data.entities.Foundation", 1},
	}};
	for (const auto &[definition, texture_count] : idle_resources) {
		const auto idle = state->get_db_view()->get_object(definition + ".Idle");
		const auto animated = api::APIAbility::get_property(
			idle, api::ability_property_t::ANIMATED);
		const auto animations = api::APIAbilityProperty::get_animations(animated);
		const auto paths = api::APIAnimation::get_animation_paths(animations);
		TESTEQUALS(paths.size(), size_t{1});
		const auto sprite = root / "assets" / "converted" / paths.front();
		TESTEQUALS(sprite.is_file(), true);
		const auto parsed = renderer::resources::parser::parse_sprite_file(sprite);
		TESTEQUALS(parsed.get_texture_count(), texture_count);
	}
	const auto horse_idle = state->get_db_view()->get_object(
		"fvd.data.entities.Horse.Idle");
	const auto horse_idle_animated = api::APIAbility::get_property(
		horse_idle, api::ability_property_t::ANIMATED);
	const auto horse_idle_animations = api::APIAbilityProperty::get_animations(
		horse_idle_animated);
	auto horse_idle_paths = api::APIAnimation::get_animation_paths(
		horse_idle_animations);
	std::sort(horse_idle_paths.begin(), horse_idle_paths.end());
	TESTEQUALS(horse_idle_paths.size(), size_t{2});
	TESTEQUALS(horse_idle_paths[0].ends_with("ethereal-hunter-horse-idle1.sprite"), true);
	TESTEQUALS(horse_idle_paths[1].ends_with("ethereal-hunter-horse-idle2.sprite"), true);
	TESTEQUALS(renderer::resources::parser::parse_sprite_file(
		           root / "assets" / "converted" / horse_idle_paths[0])
	             .get_texture_count(),
	           size_t{2});
	TESTEQUALS(renderer::resources::parser::parse_sprite_file(
		           root / "assets" / "converted" / horse_idle_paths[1])
	             .get_texture_count(),
	           size_t{1});
	const auto horse_move = state->get_db_view()->get_object(
		"fvd.data.entities.Horse.Move");
	const auto horse_move_animated = api::APIAbility::get_property(
		horse_move, api::ability_property_t::ANIMATED);
	const auto horse_move_animations = api::APIAbilityProperty::get_animations(
		horse_move_animated);
	const auto horse_move_paths = api::APIAnimation::get_animation_paths(
		horse_move_animations);
	TESTEQUALS(horse_move_paths.size(), size_t{1});
	const auto horse_move_sprite = root / "assets" / "converted" / horse_move_paths.front();
	TESTEQUALS(horse_move_sprite.is_file(), true);
	const auto parsed_horse_move = renderer::resources::parser::parse_sprite_file(
		horse_move_sprite);
	const auto &horse_move_layer = parsed_horse_move.get_layer(0);
	TESTEQUALS(horse_move_layer.get_display_mode(),
	           renderer::resources::display_mode::LOOP);
	TESTEQUALS(horse_move_layer.get_angle_count(), size_t{16});
	TESTEQUALS(horse_move_layer.get_frame_timing()->size(), size_t{12});
	const auto horse_attack_ability = state->get_db_view()->get_object(
		"fvd.data.entities.Horse.Attack");
	const auto horse_attack_animated = api::APIAbility::get_property(
		horse_attack_ability, api::ability_property_t::ANIMATED);
	const auto horse_attack_animations = api::APIAbilityProperty::get_animations(
		horse_attack_animated);
	const auto horse_attack_paths = api::APIAnimation::get_animation_paths(
		horse_attack_animations);
	TESTEQUALS(horse_attack_paths.size(), size_t{1});
	TESTEQUALS(horse_attack_paths.front().ends_with(
		           "ethereal-hunter-horse-attack.sprite"),
	           true);
	const auto horse_death_ability = state->get_db_view()->get_object(
		"fvd.data.entities.Horse.Death");
	const auto horse_death_animated = api::APIAbility::get_property(
		horse_death_ability, api::ability_property_t::ANIMATED);
	const auto horse_death_animations = api::APIAbilityProperty::get_animations(
		horse_death_animated);
	const auto horse_death_paths = api::APIAnimation::get_animation_paths(
		horse_death_animations);
	TESTEQUALS(horse_death_paths.size(), size_t{1});
	TESTEQUALS(horse_death_paths.front().ends_with(
		           "ethereal-hunter-horse-death.sprite"),
	           true);

	auto fvd = state->get_fvd();
	TESTEQUALS(fvd->get_player_count(), 8);
	TESTEQUALS(fvd->get_centre(), (coord::phys3{64, 64, 0}));
	TESTEQUALS(state->get_map()->get_size(), (util::Vector2s{128, 128}));
	TESTEQUALS(state->get_map()->get_terrain()->get_chunks().size(), 64);
	size_t tile_count = 0;
	for (const auto &chunk : state->get_map()->get_terrain()->get_chunks()) {
		for (const auto &tile : chunk->get_tiles()) {
			TESTEQUALS(tile.elevation, terrain_elevation_t::zero());
			TESTEQUALS(tile.terrain.get_name(), "fvd.data.terrain.Plains");
			if (not (root / "assets" / "converted" / tile.terrain_asset_path).is_file()) {
				TESTFAILMSG("Plains graphics cannot be resolved");
			}
			++tile_count;
		}
	}
	TESTEQUALS(tile_count, 128 * 128);
	TESTEQUALS(state->get_player(1)->get_setup(), "fvd.data.scenario.Hunter");
	// Seven Builders on a ring outside the Hunter tower's reach, which
	// stands alone at the centre.
	TESTEQUALS(state->get_game_entities().size(), 8);
	std::array<size_t, 9> counts{};
	std::array<entity_id_t, 9> builders{};
	for (const auto &[id, entity] : state->get_game_entities()) {
		auto owner = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP))->get_owners().get(time::TIME_ZERO);
		auto at_start = std::dynamic_pointer_cast<component::Position>(
			entity->get_component(component::component_t::POSITION))->get_positions().get(time::TIME_ZERO);
		if (entity->get_definition() == "fvd.data.entities.HunterTower") {
			TESTEQUALS(owner, player_id_t{1});
			TESTEQUALS(fvd->get_hunter_tower() == id, true);
			TESTEQUALS(at_start, (coord::phys3{64, 64, 0}));
			continue;
		}
		TESTEQUALS((at_start - fvd->get_centre()).length() > 15.0, true);
		TESTEQUALS(entity->get_definition(), "fvd.data.entities.Builder");
		counts.at(owner)++;
		builders.at(owner) = id;
	}
	TESTEQUALS(counts[0], 0);
	TESTEQUALS(counts[1], 0);
	for (size_t id = 2; id <= 8; ++id) {
		TESTEQUALS(counts[id], 1);
		TESTEQUALS(state->get_player(id)->get_setup(), "fvd.data.scenario.Builders");
	}
	auto builder_entity = state->get_game_entity(builders[2]);
	auto builder_render = std::make_shared<renderer::world::RenderEntity>();
	builder_entity->set_render_entity(builder_render);
	system::Idle::idle(builder_entity, time::TIME_ZERO);
	TESTEQUALS(builder_entity->get_animation_action(), AnimationAction::IDLE);
	TESTEQUALS(builder_entity->get_animation_started_at(), time::TIME_ZERO);
	{
		auto lock = builder_render->get_read_lock();
		auto position = std::dynamic_pointer_cast<component::Position>(
			builder_entity->get_component(component::component_t::POSITION));
		TESTEQUALS(builder_render->get_position().get(time::TIME_ZERO),
		           position->get_positions().get(time::TIME_ZERO).to_scene3());
	}
	TESTEQUALS(builder_render->has_health(), true);
	TESTEQUALS(builder_render->get_health_at(time::TIME_ZERO), 25);
	TESTEQUALS(builder_render->get_max_health(), 25);
	std::vector<entity_id_t> selection;
	event::ClickSelectHandler click_selector;
	auto click = [&](player_id_t controlled, const std::vector<entity_id_t> &candidates) {
		click_selector.invoke(*loop, nullptr, state, time::TIME_ZERO,
		                      {{"controlled", size_t{controlled}},
		                       {"entity_ids", candidates},
		                       {"select_cb", std::function<void(const std::vector<entity_id_t>)>{
		                                         [&](const std::vector<entity_id_t> ids) {
			                                         selection = ids;
		                                         }}}});
	};
	click(2, {builders[2]});
	TESTEQUALS(selection.size(), 1);
	TESTEQUALS(selection.front(), builders[2]);
	click(2, {builders[3]});
	TESTEQUALS(selection.empty(), true);
	click(2, {});
	TESTEQUALS(selection.empty(), true);

	event::DragSelectHandler selector;
	Eigen::Matrix4f selection_matrix = Eigen::Matrix4f::Identity();
	selection_matrix(0, 0) = 0.01f;
	selection_matrix(1, 2) = 0.01f;
	selector.invoke(*loop, nullptr, state, time::TIME_ZERO,
	                {{"controlled", size_t{2}},
	                 {"drag_start", Eigen::Vector2f{-1, -1}},
	                 {"drag_end", Eigen::Vector2f{1, 1}},
	                 {"camera_matrix", selection_matrix},
	                 {"select_cb", std::function<void(const std::vector<entity_id_t>)>{
	                                   [&](const std::vector<entity_id_t> ids) {
		                                   selection = ids;
	                                   }}}});
	TESTEQUALS(selection.size(), 1);
	TESTEQUALS(selection.front(), builders[2]);
	auto selection_position = std::dynamic_pointer_cast<component::Position>(
		builder_entity->get_component(component::component_t::POSITION));
	auto selection_world = selection_position->get_positions()
	                           .get(time::TIME_ZERO)
	                           .to_scene3()
	                           .to_world_space();
	const Eigen::Vector4f selection_clip = selection_matrix * Eigen::Vector4f{
		selection_world.x(), selection_world.y(), selection_world.z(), 1};
	const Eigen::Vector2f selection_edge{selection_clip.x(), selection_clip.y()};
	const Eigen::Vector2f selection_margin{0.05f, 0.05f};
	// std::any preserves Eigen expression types, while the handler expects
	// concrete vectors in its parameter map.
	const Eigen::Vector2f selection_drag_start =
		selection_edge - selection_margin;
	const Eigen::Vector2f selection_drag_end =
		selection_edge + selection_margin;
	selector.invoke(*loop, nullptr, state, time::TIME_ZERO,
	                {{"controlled", size_t{2}},
	                 {"drag_start", selection_drag_start},
	                 {"drag_end", selection_drag_end},
	                 {"camera_matrix", selection_matrix},
	                 {"select_cb", std::function<void(const std::vector<entity_id_t>)>{
	                                   [&](const std::vector<entity_id_t> ids) {
		                                   selection = ids;
	                                   }}}});
	TESTEQUALS(selection.size(), size_t{1});
	TESTEQUALS(selection.front(), builders[2]);
	// Even when the cursor ends exactly on the projected unit,
	// rounding must not sporadically exclude it.
	selector.invoke(*loop, nullptr, state, time::TIME_ZERO,
	                {{"controlled", size_t{2}},
	                 {"drag_start", selection_edge},
	                 {"drag_end", selection_drag_end},
	                 {"camera_matrix", selection_matrix},
	                 {"select_cb", std::function<void(const std::vector<entity_id_t>)>{
	                                   [&](const std::vector<entity_id_t> ids) {
		                                   selection = ids;
	                                   }}}});
	TESTEQUALS(selection.size(), size_t{1});
	TESTEQUALS(selection.front(), builders[2]);
	// Perspective camera: clip coordinates must be divided by w to
	// match the NDC bounds supplied by the input controller.
	Eigen::Matrix4f perspective_matrix = selection_matrix;
	perspective_matrix(3, 3) = 2.0f;
	const Eigen::Vector2f ndc_position = selection_edge / 2.0f;
	const Eigen::Vector2f perspective_drag_start =
		ndc_position - selection_margin;
	const Eigen::Vector2f perspective_drag_end =
		ndc_position + selection_margin;
	selector.invoke(*loop, nullptr, state, time::TIME_ZERO,
	                {{"controlled", size_t{2}},
	                 {"drag_start", perspective_drag_start},
	                 {"drag_end", perspective_drag_end},
	                 {"camera_matrix", perspective_matrix},
	                 {"select_cb", std::function<void(const std::vector<entity_id_t>)>{
	                                   [&](const std::vector<entity_id_t> ids) {
		                                   selection = ids;
	                                   }}}});
	TESTEQUALS(selection.size(), size_t{1});
	TESTEQUALS(selection.front(), builders[2]);

	auto place = [&](player_id_t owner, const std::vector<entity_id_t> &selected, coord::phys3 pos) {
		return fvd->request_build(state, owner, selected, pos, time::TIME_ZERO);
	};
	TESTEQUALS(place(1, {builders[2]}, {10, 10, 0}), false);
	TESTEQUALS(place(2, {}, {10, 10, 0}), false);
	TESTEQUALS(place(2, {builders[2], builders[3]}, {10, 10, 0}), false);
	TESTEQUALS(place(2, {builders[3]}, {10, 10, 0}), false);
	TESTEQUALS(place(2, {9999}, {10, 10, 0}), false);
	TESTEQUALS(place(9, {builders[2]}, {10, 10, 0}), false);
	TESTEQUALS(place(2, {builders[2]}, {-1, 10, 0}), false);
	TESTEQUALS(place(2, {builders[2]}, {128, 10, 0}), false);
	TESTEQUALS(place(2, {builders[2]}, {10, 128, 0}), false);
	TESTEQUALS(place(2, {builders[2]}, {10, 10, 1}), false);

	auto builder_pos = std::dynamic_pointer_cast<component::Position>(
		builder_entity->get_component(component::component_t::POSITION));
	TESTEQUALS(place(2, {builders[2]}, builder_pos->get_positions().get(time::TIME_ZERO)), false);
	TESTEQUALS(state->get_game_entities().size(), 8);
	TESTEQUALS(place(2, {builders[2]}, {10, 10, 0}), true);
	TESTEQUALS(fvd->is_tile_reserved(coord::tile{10, 10}), true);
	TESTEQUALS(fvd->is_tile_reserved(coord::tile{12, 12}), false);
	auto order = fvd->get_build_order(builders[2]);
	TESTEQUALS(order != nullptr, true);
	TESTEQUALS(order->get_type() == component::command::command_t::BUILD_TOWER, true);
	TESTEQUALS(order->get_owner(), player_id_t{2});
	TESTEQUALS(order->get_builder(), builders[2]);
	TESTEQUALS(order->get_target(), (coord::tile{10, 10}));
	TESTEQUALS(state->get_game_entities().size(), 9);
	entity_id_t first_foundation = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() != "fvd.data.entities.Foundation") {
			continue;
		}
		first_foundation = id;
		TESTEQUALS(entity->has_component(component::component_t::SELECTABLE), true);
		auto position = std::dynamic_pointer_cast<component::Position>(
			entity->get_component(component::component_t::POSITION));
		TESTEQUALS(position->get_positions().get(time::TIME_ZERO).to_tile(), (coord::tile{10, 10}));
		auto render = std::make_shared<renderer::world::RenderEntity>();
		entity->set_render_entity(render);
		system::Idle::idle(entity, time::TIME_ZERO);
		auto lock = render->get_read_lock();
		TESTEQUALS(render->get_animation_paths().get(time::TIME_ZERO).front().ends_with("foundation.sprite"), true);
	}
	TESTEQUALS(first_foundation != 0, true);

	// A conflicting reservation must not overwrite or cancel the first order.
	TESTEQUALS(place(3, {builders[3]}, {10, 10, 0}), false);
	TESTEQUALS(place(2, {builders[2]}, {10, 10, 0}), false);
	TESTEQUALS(fvd->get_build_order(builders[2])->get_id(), order->get_id());
	TESTEQUALS(fvd->get_build_order(builders[3]) == nullptr, true);
	TESTEQUALS(place(2, {builders[2]}, {12, 12, 0}), true);
	TESTEQUALS(fvd->is_tile_reserved(coord::tile{10, 10}), false);
	TESTEQUALS(fvd->is_tile_reserved(coord::tile{12, 12}), true);
	TESTEQUALS(fvd->get_build_order(builders[2])->get_id() != order->get_id(), true);
	TESTEQUALS(state->get_game_entities().contains(first_foundation), true);

	// An orphaned foundation blocks direct placement by another Builder.
	TESTEQUALS(place(3, {builders[3]}, {10, 10, 0}), false);
	TESTEQUALS(fvd->get_build_order(builders[3]) == nullptr, true);
	TESTEQUALS(state->get_game_entities().size(), 10);

	// Dead Builders cannot initiate new construction.
	auto live = std::dynamic_pointer_cast<component::Live>(
		state->get_game_entity(builders[8])->get_component(component::component_t::LIVE));
	live->set_attribute(time::TIME_ZERO, "fvd.data.entities.Health", 0);
	TESTEQUALS(place(8, {builders[8]}, {127.5, 127.5, 0}), false);

	event::SendCommandHandler commander;
	commander.invoke(*loop, nullptr, state, time::TIME_ZERO,
	                 {{"type", component::command::command_t::BUILD_TOWER},
	                  {"owner", player_id_t{4}},
	                  {"entity_ids", std::vector<entity_id_t>{builders[4]}},
	                  {"target", coord::phys3{20, 20, 0}}});
	TESTEQUALS(fvd->is_tile_reserved(coord::tile{20, 20}), true);
	TESTEQUALS(fvd->get_build_order(builders[4])->get_owner(), player_id_t{4});
	// Spoofed MOVE must not cancel another player's pending build.
	commander.invoke(*loop, nullptr, state, time::TIME_ZERO,
	                 {{"type", component::command::command_t::MOVE},
	                  {"owner", player_id_t{3}},
	                  {"entity_ids", std::vector<entity_id_t>{builders[4]}},
	                  {"target", coord::phys3{30, 30, 0}}});
	TESTEQUALS(fvd->is_tile_reserved(coord::tile{20, 20}), true);
	commander.invoke(*loop, nullptr, state, time::TIME_ZERO,
	                 {{"type", component::command::command_t::MOVE},
	                  {"owner", player_id_t{4}},
	                  {"entity_ids", std::vector<entity_id_t>{builders[4]}},
	                  {"target", coord::phys3{30, 30, 0}}});
	TESTEQUALS(fvd->is_tile_reserved(coord::tile{20, 20}), false);
	TESTEQUALS(fvd->get_build_order(builders[4]) == nullptr, true);

	TESTEQUALS(fvd->cancel_build(state, builders[2], time::TIME_ZERO), true);
	TESTEQUALS(fvd->is_tile_reserved(coord::tile{12, 12}), false);
	TESTEQUALS(fvd->cancel_build(state, builders[2], time::TIME_ZERO), false);
	TESTEQUALS(place(5, {builders[5]}, {12, 12, 0}), false);
	TESTEQUALS(fvd->cancel_build(state, builders[3], time::TIME_ZERO), false);
	TESTEQUALS(fvd->cancel_build(state, builders[5], time::TIME_ZERO), false);
	TESTEQUALS(state->get_game_entities().size(), 11);

	event::SpawnEntityHandler spawner{loop, entities};
	TESTTHROWS(spawner.invoke(*loop, nullptr, state, time::TIME_ZERO,
	                          {{"position", coord::phys3{10, 10, 0}},
	                           {"owner", player_id_t{2}},
	                           {"entity_ids", std::vector<entity_id_t>{builders[2]}}}));

	auto destination = coord::phys3{80, 80, 0};
	auto duration = system::Move::move_default(
		state->get_game_entity(builders[2]), state, destination, time::TIME_ZERO);
	TESTEQUALS(builder_pos->get_positions().get(duration), destination);
	TESTEQUALS(state->get_game_entity(builders[2])->get_animation_action(),
	           AnimationAction::MOVE);
	TESTEQUALS(state->get_game_entity(builders[2])->get_animation_started_at(),
	           time::TIME_ZERO);
}

/** A real authoritative Builder shelters inside a completed tower and exits unharmed. */
void fvd_garrison() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	tests::use_centre_test_layout(state);
	auto t = [](double seconds) { return time::time_t::from_double(seconds); };

	entity_id_t builder = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() != "fvd.data.entities.Builder") continue;
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (ownership->get_owners().get(t(0)) == player_id_t{2}) builder = id;
	}
	TESTEQUALS(builder != 0, true);
	TESTEQUALS(fvd->request_build(state, 2, {builder}, {61, 66, 0}, t(0)), true);
	loop->reach_time(t(0.2), state);
	entity_id_t tower = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() != "fvd.data.entities.Tower") continue;
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (ownership->get_owners().get(t(0.2)) == player_id_t{2}) tower = id;
	}
	TESTEQUALS(tower != 0, true);
	TESTEQUALS(fvd->request_garrison(state, 1, {builder}, tower, t(0.2)), false);
	TESTEQUALS(fvd->request_garrison(state, 3, {builder}, tower, t(0.2)), false);
	TESTEQUALS(fvd->request_garrison(state, 2, {builder}, builder, t(0.2)), false);
	TESTEQUALS(fvd->request_garrison(state, 2, {builder}, tower, t(0.2)), true);
	TESTEQUALS(fvd->request_garrison(state, 2, {builder}, tower, t(0.2)), false);
	loop->reach_time(t(0.25), state);
	TESTEQUALS(fvd->is_garrisoned(builder), true);
	TESTEQUALS(fvd->can_command_units(state, 2, {builder}, t(0.25)), false);
	TESTEQUALS(fvd->can_see(state, FVDTeam::HUNTER, builder, t(0.25)), false);
	TESTEQUALS(fvd->can_place_tower(state, 2, {builder}, {70, 70, 0}, t(0.25)), false);
	const auto snapshot = network::capture_fvd_presentation_state(state, t(0.25));
	TESTEQUALS(std::none_of(snapshot.entities.begin(), snapshot.entities.end(),
	                        [builder](const auto &item) { return item.entity_id == builder; }), true);

	auto occupant = state->get_game_entity(builder);
	auto live = std::dynamic_pointer_cast<component::Live>(
		occupant->get_component(component::component_t::LIVE));
	const auto hp = live->get_attribute(t(0.25), "fvd.data.entities.Health")->get(t(0.25));
	fvd->hunter_tower_hit(state, builder, 100, t(0.25));
	TESTEQUALS(live->get_attribute(t(0.25), "fvd.data.entities.Health")->get(t(0.25)), hp);
	TESTEQUALS(fvd->ungarrison_builder(state, 3, {tower}, t(0.25)), false);
	TESTEQUALS(fvd->ungarrison_builder(state, 2, {tower}, t(0.25)), true);
	TESTEQUALS(fvd->is_garrisoned(builder), false);
	TESTEQUALS(fvd->can_command_units(state, 2, {builder}, t(0.25)), true);
	TESTEQUALS(fvd->ungarrison_builder(state, 2, {tower}, t(0.25)), false);
	TESTEQUALS(state->get_game_entities().contains(builder), true);
}

void fvd_construction() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	tests::use_centre_test_layout(state);

	std::array<entity_id_t, 9> builders{};
	for (const auto &[id, entity] : state->get_game_entities()) {
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		builders.at(ownership->get_owners().get(time::TIME_ZERO)) = id;
	}
	auto t = [](double seconds) {
		return time::time_t::from_double(seconds);
	};
	auto build = [&](player_id_t owner, const coord::phys3 &tile, double seconds) {
		return fvd->request_build(state, owner, {builders.at(owner)}, tile, t(seconds));
	};

	// Start a route that crosses the future tower tile. The refresh must
	// replace its remaining position curve once construction blocks the tile.
	event::SendCommandHandler route_commander;
	route_commander.invoke(*loop, nullptr, state, time::TIME_ZERO,
	                       {{"type", component::command::command_t::MOVE},
	                        {"owner", player_id_t{3}},
	                        {"entity_ids", std::vector<entity_id_t>{builders[3]}},
	                        {"target", coord::phys3{60.5, 67.5, 0}}});
	loop->reach_time(time::TIME_ZERO, state);
	auto routed_position = std::dynamic_pointer_cast<component::Position>(
		state->get_game_entity(builders[3])->get_component(component::component_t::POSITION));
	TESTEQUALS(routed_position->get_positions().get(t(0.5)).to_tile(), (coord::tile{61, 66}));

	// An adjacent Builder starts building at once. Its footprint becomes
	// impassable immediately while health rises over the configured duration.
	TESTEQUALS(build(2, {61, 66, 0}, 0), true);
	TESTEQUALS(fvd->is_tile_reserved({61, 66}), true);
	TESTEQUALS(state->get_game_entities().size(), 8);
	bool planned = false;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.Foundation") {
			planned = true;
		}
	}
	TESTEQUALS(planned, true);
	auto construction_planned = fvd->get_construction_state();
	TESTEQUALS(construction_planned.size(), size_t{1});
	TESTEQUALS(construction_planned.front().builder_id, builders[2]);
	TESTEQUALS(construction_planned.front().phase == FVDConstructionPhase::PLANNED, true);
	TESTEQUALS(construction_planned.front().progress_percent(t(0)), 0);
	loop->reach_time(t(0.05), state);
	TESTEQUALS(state->get_game_entities().size(), 8);
	auto constructing = fvd->get_construction_state();
	TESTEQUALS(constructing.size(), size_t{1});
	TESTEQUALS(constructing.front().phase == FVDConstructionPhase::UNDER_CONSTRUCTION, true);
	const auto &progress = constructing.front();
	TESTEQUALS(progress.progress_percent(progress.phase_start_time), 0);
	TESTEQUALS(progress.progress_percent(progress.phase_end_time), 100);
	TESTEQUALS(progress.progress_percent(progress.phase_end_time + t(1)), 100);
	TESTEQUALS(progress.progress_percent(progress.phase_start_time - t(1)), 0);
	TESTEQUALS(progress.progress_percent(t(0.05)) > 0, true);
	TESTEQUALS(progress.progress_percent(t(0.05)) < 100, true);
	for (const auto &[id, entity] : state->get_game_entities()) {
		TESTNOTEQUALS(entity->get_definition(), "fvd.data.entities.Foundation");
	}
	auto land_grid = state->get_map()->get_pathfinder()->get_grid(
		state->get_map()->get_grid_id("fvd.data.terrain.Land"));
	auto sector_size = land_grid->get_sector_size();
	auto tower_sector = land_grid->get_sector(61 / sector_size, 66 / sector_size);
	auto tower_local = coord::tile{61, 66}
	                 - tower_sector->get_position().to_tile(sector_size);
	TESTEQUALS(tower_sector->get_cost_field()->get_cost(tower_local), path::COST_IMPASSABLE);
	entity_id_t first_tower = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() != "fvd.data.entities.Tower") {
			continue;
		}
		first_tower = id;
		auto tower_live = std::dynamic_pointer_cast<component::Live>(
			entity->get_component(component::component_t::LIVE));
		auto mid_health = tower_live->get_attribute(
			 t(0.05), "fvd.data.entities.Health")->get(t(0.05));
		TESTEQUALS(mid_health > 0 and mid_health < 100, true);
	}
	TESTEQUALS(first_tower != 0, true);
	fvd->refresh_paths(state, t(0.05));
	TESTEQUALS((routed_position->get_positions().get(t(0.5)).to_tile() != coord::tile{61, 66}), true);
	loop->reach_time(t(0.1), state);
	TESTEQUALS(state->get_game_entities().size(), 8);
	TESTEQUALS(fvd->is_tile_reserved({61, 66}), false);
	TESTEQUALS(fvd->get_build_order(builders[2]) == nullptr, true);
	TESTEQUALS(fvd->get_construction_state().empty(), true);
	bool completed = false;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.Tower") {
			auto pos = std::dynamic_pointer_cast<component::Position>(
				entity->get_component(component::component_t::POSITION));
			if (pos->get_positions().get(t(0.1)).to_tile() == coord::tile{61, 66}) {
				auto ownership = std::dynamic_pointer_cast<component::Ownership>(
					entity->get_component(component::component_t::OWNERSHIP));
				TESTEQUALS(ownership->get_owners().get(t(0.1)), player_id_t{2});
				auto tower_live = std::dynamic_pointer_cast<component::Live>(
					entity->get_component(component::component_t::LIVE));
				TESTEQUALS(tower_live->get_attribute(t(0.1), "fvd.data.entities.Health")->get(t(0.1)), 100);
				completed = true;
			}
		}
	}
	TESTEQUALS(completed, true);

	// Distant construction must travel first and then finish.
	TESTEQUALS(build(3, {75, 75, 0}, 4), true);
	auto moving = state->get_game_entity(builders[3]);
	auto position = std::dynamic_pointer_cast<component::Position>(
		moving->get_component(component::component_t::POSITION));
	auto start = position->get_positions().get(t(4));
	TESTEQUALS(position->get_positions().get(t(5)) != start, true);
	loop->reach_time(t(5), state);
	TESTEQUALS(state->get_game_entities().size(), 9);
	loop->reach_time(t(30), state);
	TESTEQUALS(fvd->is_tile_reserved({75, 75}), false);
	TESTEQUALS(state->get_game_entities().size(), 9);

	// Issuing a different command cancels the work, preserves its foundation,
	// truncates the old movement curve, and makes scheduled completion a no-op.
	TESTEQUALS(build(4, {90, 90, 0}, 31), true);
	event::SendCommandHandler commander;
	commander.invoke(*loop, nullptr, state, t(32),
	                 {{"type", component::command::command_t::MOVE},
	                  {"owner", player_id_t{4}},
	                  {"entity_ids", std::vector<entity_id_t>{builders[4]}},
	                  {"target", coord::phys3{70, 70, 0}}});
	TESTEQUALS(fvd->is_tile_reserved({90, 90}), false);
	TESTEQUALS(fvd->get_build_order(builders[4]) == nullptr, true);
	loop->reach_time(t(100), state);
	TESTEQUALS(state->get_game_entities().size(), 10);

	// A Builder who dies during travel does not complete construction.
	TESTEQUALS(build(5, {95, 95, 0}, 101), true);
	auto victim = state->get_game_entity(builders[5]);
	auto live = std::dynamic_pointer_cast<component::Live>(
		victim->get_component(component::component_t::LIVE));
	live->set_attribute(t(102), "fvd.data.entities.Health", 0);
	loop->reach_time(t(150), state);
	TESTEQUALS(fvd->is_tile_reserved({95, 95}), false);
	TESTEQUALS(fvd->get_build_order(builders[5]) == nullptr, true);
	TESTEQUALS(state->get_game_entities().size(), 11);

	// A newer order supersedes a previous construction. Its old event
	// cannot spawn a tower even once the previous deadline passes.
	TESTEQUALS(build(6, {83, 83, 0}, 151), true);
	auto original = fvd->get_build_order(builders[6])->get_id();
	TESTEQUALS(build(6, {84, 84, 0}, 152), true);
	TESTEQUALS(fvd->get_build_order(builders[6])->get_id() != original, true);
	TESTEQUALS(fvd->is_tile_reserved({83, 83}), false);
	loop->reach_time(t(210), state);
	TESTEQUALS(fvd->is_tile_reserved({84, 84}), false);

	TESTEQUALS(state->get_game_entities().size(), 13);

	// Fully surrounded sites have no reachable, vacant adjacent stand tile.
	const std::array<coord::tile_delta, 8> neighbors{{
		{-1, 0}, {1, 0}, {0, -1}, {0, 1},
		{-1, -1}, {-1, 1}, {1, -1}, {1, 1},
	}};
	for (const auto &delta : neighbors) {
		coord::tile tile{110 + delta.ne, 110 + delta.se};
		entities->spawn_game_entity(loop, state, 2, "fvd.data.entities.Tower",
		                           tile.to_phys3_center(), t(211));
	}
	TESTEQUALS(build(7, {110, 110, 0}, 211), false);
	TESTEQUALS(fvd->is_tile_reserved({110, 110}), false);
	TESTEQUALS(state->get_game_entities().size(), 21);

	// A dead Builder with a construction already underway cannot finish it.
	TESTEQUALS(build(7, {66, 66, 0}, 212), true);
	auto final_builder = state->get_game_entity(builders[7]);
	auto final_live = std::dynamic_pointer_cast<component::Live>(
		final_builder->get_component(component::component_t::LIVE));
	loop->reach_time(t(212.05), state);
	final_live->set_attribute(t(212.05), "fvd.data.entities.Health", 0);
	loop->reach_time(t(220), state);
	TESTEQUALS(fvd->is_tile_reserved({66, 66}), false);
	TESTEQUALS(state->get_game_entities().size(), 22);
	auto canceled_sector = land_grid->get_sector(66 / sector_size, 66 / sector_size);
	auto canceled_local = coord::tile{66, 66}
	                   - canceled_sector->get_position().to_tile(sector_size);
	TESTEQUALS(canceled_sector->get_cost_field()->get_cost(canceled_local), path::cost_t{1});

	// Destruction replaces the tower with nonblocking demolition rubble and
	// restores the terrain's original path cost.
	TESTEQUALS(fvd->destroy_tower(state, first_tower, t(221)), true);
	TESTEQUALS(state->get_game_entities().size(), 22);
	TESTEQUALS(tower_sector->get_cost_field()->get_cost(tower_local), path::cost_t{1});
	TESTEQUALS(fvd->destroy_tower(state, first_tower, t(222)), false);
}

/**
 * Delete demolishes one owned tower through the authoritative command path.
 * This also covers construction cancellation, pathfinding and replication.
 */
void fvd_builder_demolition() {
	auto root = util::Path{
		std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	tests::use_centre_test_layout(state);
	auto at = [](double seconds) { return time::time_t::from_double(seconds); };
	entity_id_t builder_id = 0;
	bool found_builder = false;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() != "fvd.data.entities.Builder") continue;
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (ownership->get_owners().get(time::TIME_ZERO) == player_id_t{2}) {
			builder_id = id;
			found_builder = true;
			break;
		}
	}
	TESTEQUALS(found_builder, true);
	const coord::phys3 site{61, 66, 0};
	auto tower_at_site = [&](const time::time_t &time) {
		entity_id_t tower_id = 0;
		for (const auto &[id, entity] : state->get_game_entities()) {
			if (entity->get_definition() != "fvd.data.entities.Tower") continue;
			auto position = std::dynamic_pointer_cast<component::Position>(
				entity->get_component(component::component_t::POSITION));
			if (position->get_positions().get(time).to_tile() == site.to_tile()) {
				tower_id = id;
			}
		}
		return tower_id;
	};
	auto land_grid = state->get_map()->get_pathfinder()->get_grid(
		state->get_map()->get_grid_id("fvd.data.terrain.Land"));
	auto sector_size = land_grid->get_sector_size();
	auto sector = land_grid->get_sector(61 / sector_size, 66 / sector_size);
	auto local = site.to_tile() - sector->get_position().to_tile(sector_size);
	auto cost = [&]() { return sector->get_cost_field()->get_cost(local); };
	event::SendCommandHandler commander;
	auto demolish = [&](player_id_t owner, const std::vector<entity_id_t> &ids,
	                    const time::time_t &time) {
		bool accepted = false;
		commander.invoke(*loop, nullptr, state, time,
		                 {{"type", component::command::command_t::DEMOLISH_TOWER},
		                  {"owner", owner},
		                  {"entity_ids", ids},
		                  {"result_cb", std::function<void(bool)>{[&accepted](bool result) {
			                  accepted = result;
		                  }}}});
		return accepted;
	};

	TESTEQUALS(fvd->request_build(state, 2, {builder_id}, site, time::TIME_ZERO), true);
	loop->reach_time(at(0.1), state);
	auto tower = tower_at_site(at(0.1));
	TESTEQUALS(tower != entity_id_t{0}, true);
	TESTEQUALS(cost(), path::COST_IMPASSABLE);
	TESTEQUALS(demolish(1, {tower}, at(0.11)), false); // Hunter role
	TESTEQUALS(demolish(3, {tower}, at(0.11)), false); // Foreign Builder
	TESTEQUALS(demolish(2, {builder_id}, at(0.11)), false); // Not a tower
	TESTEQUALS(demolish(2, {}, at(0.11)), false);
	TESTEQUALS(demolish(2, {tower, tower}, at(0.11)), false);
	TESTEQUALS(demolish(2, {tower + 100000}, at(0.11)), false);
	TESTEQUALS(state->get_game_entities().contains(tower), true);
	TESTEQUALS(demolish(2, {tower}, at(0.12)), true);
	TESTEQUALS(state->get_game_entities().contains(tower), false);
	TESTEQUALS(cost(), path::cost_t{1});
	entity_id_t demolition = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.TowerDemolition") {
			demolition = id;
		}
	}
	TESTNOTEQUALS(demolition, entity_id_t{0});
	auto demolition_entity = state->get_game_entity(demolition);
	TESTEQUALS(demolition_entity->get_animation_action(), AnimationAction::IDLE);
	TESTEQUALS(demolition_entity->get_animation_started_at(), at(0.12));
	TESTEQUALS(demolish(2, {tower}, at(0.13)), false);
	auto snapshot = network::capture_fvd_presentation_state(state, at(0.13));
	TESTEQUALS(std::none_of(snapshot.entities.begin(), snapshot.entities.end(),
	                       [tower](const auto &entry) { return entry.entity_id == tower; }), true);
	TESTEQUALS(std::any_of(snapshot.entities.begin(), snapshot.entities.end(),
	                      [demolition](const auto &entry) {
		                      return entry.entity_id == demolition
		                             and entry.definition == "fvd.data.entities.TowerDemolition";
	                      }), true);

	// Delete during construction must not leave a foundation, reservation
	// or scheduled completion event able to resurrect the tower.
	TESTEQUALS(fvd->request_build(state, 2, {builder_id}, site, at(0.2)), true);
	TESTEQUALS(state->get_game_entities().contains(demolition), false);
	loop->reach_time(at(0.25), state);
	auto unfinished = tower_at_site(at(0.25));
	TESTEQUALS(unfinished != entity_id_t{0}, true);
	TESTEQUALS(fvd->get_build_order(builder_id) != nullptr, true);
	TESTEQUALS(fvd->is_tile_reserved(site.to_tile()), true);
	TESTEQUALS(demolish(2, {unfinished}, at(0.26)), true);
	TESTEQUALS(state->get_game_entities().contains(unfinished), false);
	TESTEQUALS(fvd->get_build_order(builder_id) == nullptr, true);
	TESTEQUALS(fvd->is_tile_reserved(site.to_tile()), false);
	TESTEQUALS(cost(), path::cost_t{1});
	loop->reach_time(at(1), state);
	TESTEQUALS(tower_at_site(at(1)), entity_id_t{0});
	entity_id_t unfinished_demolition = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		TESTNOTEQUALS(entity->get_definition(), "fvd.data.entities.Foundation");
		if (entity->get_definition() == "fvd.data.entities.TowerDemolition") {
			unfinished_demolition = id;
		}
	}
	TESTNOTEQUALS(unfinished_demolition, entity_id_t{0});
	loop->reach_time(at(30.27), state);
	TESTEQUALS(state->get_game_entities().contains(unfinished_demolition), false);
}

void fvd_builder_delete_foundation() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	tests::use_centre_test_layout(state);
	auto at = [](double seconds) { return time::time_t::from_double(seconds); };
	auto alive = [&](entity_id_t id) { return state->get_game_entities().contains(id); };
	std::optional<entity_id_t> found_builder{};
	for (const auto &[id, entity] : state->get_game_entities()) {
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (entity->get_definition() == "fvd.data.entities.Builder"
		    and ownership->get_owners().get(time::TIME_ZERO) == player_id_t{2}) {
			found_builder = id;
		}
	}
	TESTEQUALS(found_builder.has_value(), true);
	const auto builder = *found_builder;
	auto foundation_at = [&](coord::tile site) {
		for (const auto &[id, entity] : state->get_game_entities()) {
			if (entity->get_definition() != "fvd.data.entities.Foundation") continue;
			auto position = std::dynamic_pointer_cast<component::Position>(
				entity->get_component(component::component_t::POSITION));
			if (position->get_positions().get(at(0.01)).to_tile() == site) return id;
		}
		return entity_id_t{0};
	};

	// The Builder walks to the first of three sites; two wait in its queue.
	const coord::tile first{66, 72}, second{68, 72}, third{70, 72};
	TESTEQUALS(fvd->request_build(state, 2, {builder}, first.to_phys3_center(), at(0)), true);
	TESTEQUALS(fvd->queue_build(state, 2, {builder}, second.to_phys3_center(), at(0)), true);
	TESTEQUALS(fvd->queue_build(state, 2, {builder}, third.to_phys3_center(), at(0)), true);
	TESTEQUALS(fvd->get_queued_build_count(builder), size_t{2});
	const auto first_site = foundation_at(first);
	const auto second_site = foundation_at(second);
	const auto third_site = foundation_at(third);
	TESTEQUALS(first_site != 0 and second_site != 0 and third_site != 0, true);

	// Only the owner may remove them.
	TESTEQUALS(fvd->delete_owned_foundation(state, 1, second_site, at(0.01)), false);
	TESTEQUALS(fvd->delete_owned_foundation(state, 3, second_site, at(0.01)), false);
	TESTEQUALS(fvd->delete_owned_foundation(state, 2, builder, at(0.01)), false);

	// A queued site leaves the queue, frees its tiles and lowers the count.
	TESTEQUALS(fvd->delete_owned_foundation(state, 2, second_site, at(0.01)), true);
	TESTEQUALS(alive(second_site), false);
	TESTEQUALS(fvd->get_queued_build_count(builder), size_t{1});
	TESTEQUALS(fvd->is_tile_reserved(second), false);

	// The site being walked to: the Builder moves on to the next one.
	TESTEQUALS(fvd->delete_owned_foundation(state, 2, first_site, at(0.02)), true);
	TESTEQUALS(alive(first_site), false);
	TESTEQUALS(fvd->is_tile_reserved(first), false);
	TESTEQUALS(fvd->get_build_order(builder) != nullptr, true);
	TESTEQUALS(fvd->get_build_order(builder)->get_target(), third);
	TESTEQUALS(fvd->get_queued_build_count(builder), size_t{0});

	// An abandoned foundation goes through the same Delete command as towers.
	TESTEQUALS(fvd->cancel_build(state, builder, at(0.03)), true);
	TESTEQUALS(alive(third_site), true);
	TESTEQUALS(fvd->demolish_owned_tower(state, 3, {third_site}, at(0.04)), false);
	TESTEQUALS(fvd->demolish_owned_tower(state, 2, {third_site}, at(0.04)), true);
	TESTEQUALS(alive(third_site), false);
	TESTEQUALS(foundation_at(third), entity_id_t{0});
	loop->reach_time(at(5), state);
	TESTEQUALS(fvd->get_build_order(builder) == nullptr, true);
}

void fvd_match_outcome_rules() {
	auto at = [](int seconds) { return time::time_t::from_int(seconds); };
	const auto deadline = at(FVD_MATCH_DURATION_SECONDS);
	const auto smallest_time_step = time::time_t::from_raw_value(1);
	TESTEQUALS(match_end_countdown(std::nullopt, at(0)).has_value(), false);
	TESTEQUALS(match_end_countdown(deadline, at(0)).value(), 45 * 60);
	TESTEQUALS(match_end_countdown(deadline, at(2699)).value(), 1);
	TESTEQUALS(match_end_countdown(deadline, deadline - smallest_time_step).value(), 1);
	TESTEQUALS(match_end_countdown(deadline, at(2700)).value(), 0);
	TESTEQUALS(match_end_countdown(deadline, deadline + smallest_time_step).value(), 0);
	TESTEQUALS(match_end_countdown(deadline, at(2701)).value(), 0);
	// Builders can only win when the 45-minute timer expires.
	TESTEQUALS(decide_fvd_match_outcome(true, at(2699), deadline).has_value(), false);
	TESTEQUALS(decide_fvd_match_outcome(true, at(2700), deadline)->winner,
	           network::MatchWinner::BUILDERS);
	TESTEQUALS(decide_fvd_match_outcome(true, at(2700), deadline)->reason,
	           network::MatchEndReason::TIME_LIMIT);
	TESTEQUALS(decide_fvd_match_outcome(true, at(2701), deadline)->reason,
	           network::MatchEndReason::TIME_LIMIT);
	TESTEQUALS(decide_fvd_match_outcome(true, at(2700), std::nullopt).has_value(), false);
	// Eliminating every Builder wins for the Hunter, even at the timer deadline.
	TESTEQUALS(decide_fvd_match_outcome(false, at(2700), deadline)->winner,
	           network::MatchWinner::HUNTER);
	TESTEQUALS(decide_fvd_match_outcome(false, at(10), deadline)->reason,
	           network::MatchEndReason::ALL_BUILDERS_DEAD);

	// Exercise the actual authoritative FVD state and recipient capture,
	// not merely the rule helper. Disconnections count as dead without
	// requiring mutation of game entities or destruction of their towers.
	auto root = util::Path{
		std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	TESTEQUALS(fvd->get_match_outcome().has_value(), false);
	TESTEQUALS(fvd->get_match_end_deadline().has_value(), false);
	fvd->start_hunter_wave(state, at(0));
	TESTEQUALS(fvd->get_match_end_deadline().value(), deadline);
	fvd->evaluate_match_outcome(state, at(19));
	TESTEQUALS(fvd->get_match_outcome().has_value(), false);
	// A fully disconnected team is eliminated, but its structures remain.
	const auto entity_count = state->get_game_entities().size();
	fvd->evaluate_match_outcome(state, at(20), {2, 3, 4, 5, 6, 7, 8});
	TESTEQUALS(fvd->get_match_outcome()->winner, network::MatchWinner::HUNTER);
	TESTEQUALS(fvd->get_match_outcome()->ended_at, at(20));
	TESTEQUALS(state->get_game_entities().size(), entity_count);
	bool accepted_after_end = true;
	std::function<void(bool)> on_order = [&](bool accepted) {
		accepted_after_end = accepted;
	};
	event::SendCommandHandler sender{};
	sender.invoke(*loop, nullptr, state, at(21), {
		{"type", component::command::command_t::MOVE},
		{"owner", player_id_t{2}},
		{"entity_ids", std::vector<entity_id_t>{}},
		{"result_cb", on_order},
	});
	TESTEQUALS(accepted_after_end, false);
	fvd->evaluate_match_outcome(state, at(2700));
	TESTEQUALS(fvd->get_match_outcome()->reason,
	           network::MatchEndReason::ALL_BUILDERS_DEAD);
	auto state_on_wire = network::capture_fvd_presentation_state(state, at(20));
	TESTEQUALS(state_on_wire.match_outcome, fvd->get_match_outcome());
}

void fvd_hunter_spawning() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	auto at = [](double seconds) {
		return time::time_t::from_double(seconds);
	};
	auto count_horses = [&]() {
		size_t horses = 0;
		for (const auto &[id, entity] : state->get_game_entities()) {
			if (entity->get_definition() == "fvd.data.entities.Horse") {
				++horses;
			}
		}
		return horses;
	};

	// The HUD countdown uses the match's actual first-spawn deadline.
	TESTEQUALS(fvd->get_hunter_spawn_countdown(at(0)).has_value(), false);
	// Preparing the map does not start the Hunter wave.
	loop->reach_time(at(0), state);
	TESTEQUALS(count_horses(), size_t{0});
	fvd->start_hunter_wave(state, at(0));
	fvd->start_hunter_wave(state, at(0));
	TESTEQUALS(fvd->get_first_hunter_spawn_time().value(), at(20));
	TESTEQUALS(fvd->get_hunter_spawn_countdown(at(0)).value(), 20);
	TESTEQUALS(fvd->get_hunter_spawn_countdown(at(0.01)).value(), 20);
	TESTEQUALS(fvd->get_hunter_spawn_countdown(at(1)).value(), 19);
	TESTEQUALS(fvd->get_hunter_spawn_countdown(at(19)).value(), 1);
	TESTEQUALS(fvd->get_hunter_spawn_countdown(at(19.999)).value(), 1);
	TESTEQUALS(fvd->get_hunter_spawn_countdown(at(20)).has_value(), false);
	TESTEQUALS(fvd->get_hunter_spawn_countdown(at(21)).has_value(), false);

	loop->reach_time(at(19.999), state);
	TESTEQUALS(count_horses(), size_t{0});
	loop->reach_time(at(20), state);
	TESTEQUALS(count_horses(), size_t{1});
	TESTEQUALS(fvd->get_first_hunter_spawn_time().has_value(), false);
	loop->reach_time(at(20.999), state);
	TESTEQUALS(count_horses(), size_t{1});
	loop->reach_time(at(21), state);
	TESTEQUALS(count_horses(), size_t{2});
	loop->reach_time(at(22), state);
	TESTEQUALS(count_horses(), size_t{3});

	// Horses never receive an automatic Move order. The only displacement
	// comes from nearby bodies making room for subsequent spawns.
	loop->reach_time(at(23), state);
	TESTEQUALS(count_horses(), size_t{4});
	loop->reach_time(at(23.4), state);
	std::vector<coord::phys3> horse_positions;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() != "fvd.data.entities.Horse") {
			continue;
		}
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		auto position = std::dynamic_pointer_cast<component::Position>(
			entity->get_component(component::component_t::POSITION));
		auto move = std::dynamic_pointer_cast<component::Move>(
			entity->get_component(component::component_t::MOVE));
		TESTEQUALS(ownership->get_owners().get(at(23.4)), player_id_t{1});
		TESTEQUALS(entity->has_component(component::component_t::SELECTABLE), true);
		TESTEQUALS(move->get_active_target().has_value(), false);
		auto at_end = position->get_positions().get(at(23.4));
		auto displacement = at_end - fvd->get_centre();
		TESTEQUALS(displacement.length() < 5.0, true);
		horse_positions.push_back(at_end);
	}
	TESTEQUALS(horse_positions.size(), size_t{4});
	for (size_t i = 0; i < horse_positions.size(); ++i) {
		for (size_t j = i + 1; j < horse_positions.size(); ++j) {
			auto delta = horse_positions[i] - horse_positions[j];
			// 0.38 + 0.38 radii, with a little numerical tolerance.
			TESTEQUALS(delta.length() > 0.68, true);
		}
	}

	// A single command must wake every recently spawned horse, including
	// horses whose spawn-separation movement has only just completed.
	loop->reach_time(at(39), state);
	std::vector<entity_id_t> wave;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.Horse") {
			wave.push_back(id);
		}
	}
	TESTEQUALS(wave.size(), size_t{20});
	event::SendCommandHandler commander;
	bool accepted = false;
	commander.invoke(*loop, nullptr, state, at(39),
	                 {{"type", component::command::command_t::MOVE},
	                  {"owner", player_id_t{1}},
	                  {"entity_ids", wave},
	                  {"target", coord::phys3{70, 70, 0}},
	                  {"result_cb", std::function<void(bool)>{[&accepted](bool result) {
		                  accepted = result;
	                  }}}});
	TESTEQUALS(accepted, true);
	loop->reach_time(at(39), state);
	size_t moving = 0;
	for (auto id : wave) {
		auto entity = state->get_game_entity(id);
		auto move = std::dynamic_pointer_cast<component::Move>(
			entity->get_component(component::component_t::MOVE));
		if (move->get_active_target().has_value()) {
			TESTEQUALS(*move->get_active_target(), coord::phys3(70, 70, 0));
			++moving;
		}
	}
	TESTEQUALS(moving, wave.size());

	// The debug shortcut replaces the pending first event, starts the regular
	// interval from now, and cannot be used again after the wave has begun.
	auto immediate_loop = std::make_shared<openage::event::EventLoop>();
	immediate_loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	immediate_loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto immediate_entities = std::make_shared<EntityFactory>();
	auto immediate_terrain = std::make_shared<TerrainFactory>();
	Game immediate_game{immediate_loop, mods, immediate_entities, immediate_terrain};
	auto immediate_state = immediate_game.get_state();
	auto immediate_fvd = immediate_state->get_fvd();
	auto count_immediate_horses = [&]() {
		return static_cast<size_t>(std::count_if(
			immediate_state->get_game_entities().begin(),
			immediate_state->get_game_entities().end(),
			[](const auto &entry) {
				return entry.second->get_definition() == "fvd.data.entities.Horse";
			}));
	};
	immediate_fvd->start_hunter_wave(immediate_state, at(0));
	immediate_fvd->request_immediate_hunter_spawn(immediate_state, at(5));
	immediate_fvd->request_immediate_hunter_spawn(immediate_state, at(5));
	immediate_loop->reach_time(at(5), immediate_state);
	TESTEQUALS(count_immediate_horses(), size_t{1});
	TESTEQUALS(immediate_fvd->get_hunter_spawn_countdown(at(5)).has_value(), false);
	immediate_loop->reach_time(at(5.999), immediate_state);
	TESTEQUALS(count_immediate_horses(), size_t{1});
	immediate_loop->reach_time(at(6), immediate_state);
	TESTEQUALS(count_immediate_horses(), size_t{2});
	immediate_loop->reach_time(at(20), immediate_state);
	TESTEQUALS(count_immediate_horses(), size_t{16});
}

void fvd_horse_spawn_blocked() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	tests::use_centre_test_layout(state);
	auto t = [](double seconds) {
		return time::time_t::from_double(seconds);
	};
	entity_id_t builder = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (ownership->get_owners().get(time::TIME_ZERO) == player_id_t{5}) {
			builder = id;
			break;
		}
	}
	TESTEQUALS(builder != 0, true);
	// Construct a path-blocking tower directly on the future spawn site.
	TESTEQUALS(fvd->request_build(state, 5, {builder}, fvd->get_centre(), t(0)), true);
	loop->reach_time(t(1), state);
	TESTEQUALS(fvd->is_tile_reserved({64, 64}), false);

	fvd->start_hunter_wave(state, t(0));
	loop->reach_time(t(20), state);
	std::shared_ptr<GameEntity> horse;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.Horse") {
			horse = entity;
		}
	}
	TESTEQUALS(horse != nullptr, true);
	loop->reach_time(t(20.4), state);
	auto position = std::dynamic_pointer_cast<component::Position>(
		horse->get_component(component::component_t::POSITION));
	// A blocked centre never vetoes the scheduled spawn: contact shoves
	// the new horse into nearby accessible terrain instead.
	TESTEQUALS(position->get_positions().get(t(20.4)) != fvd->get_centre(), true);
}

void fvd_hunter_combat() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	tests::use_centre_test_layout(state);
	auto at = [](double seconds) { return time::time_t::from_double(seconds); };

	entity_id_t builder = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		auto owner = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (owner->get_owners().get(time::TIME_ZERO) == player_id_t{5}) {
			builder = id;
			break;
		}
	}
	TESTEQUALS(builder != 0, true);
	TESTEQUALS(fvd->request_build(state, 5, {builder}, {68, 69, 0}, at(0)), true);
	loop->reach_time(at(15), state);
	entity_id_t tower = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.Tower") {
			tower = id;
		}
	}
	TESTEQUALS(tower != 0, true);
	auto tower_entity = state->get_game_entity(tower);
	auto tower_health = std::dynamic_pointer_cast<component::Live>(
		tower_entity->get_component(component::component_t::LIVE));
	TESTEQUALS(tower_health->get_attribute(at(15), "fvd.data.entities.Health")->get(at(15)), int64_t{100});
	TESTEQUALS(fvd->get_kills(5), uint64_t{0});

	// The host validates an entire Hunter group; mixed player ownership
	// rejects the command without moving any of the horses.
	auto one = entities->spawn_game_entity(loop, state, 1, "fvd.data.entities.Horse",
	                                      {71, 69, 0}, at(15));
	auto two = entities->spawn_game_entity(loop, state, 1, "fvd.data.entities.Horse",
	                                      {72, 69, 0}, at(15));
	TESTEQUALS(fvd->issue_attack_move(state, 2, {one->get_id()}, {73, 69, 0}, at(15)), false);
	TESTEQUALS(fvd->issue_attack_move(state, 1, {one->get_id(), builder}, {73, 69, 0}, at(15)), false);
	TESTEQUALS(fvd->can_command_units(state, 1, {one->get_id(), two->get_id()}, at(15)), true);
	TESTEQUALS(fvd->can_command_units(state, 5, {one->get_id()}, at(15)), false);
	TESTEQUALS(fvd->issue_attack_move(state, 1, {one->get_id(), two->get_id()},
	                                  {70, 69, 0}, at(15)), true);
	auto horse_pos = std::dynamic_pointer_cast<component::Position>(
		one->get_component(component::component_t::POSITION));
	TESTEQUALS(horse_pos->get_positions().get(at(15.25)).ne != coord::phys_t::from_int(71), true);

	// Completed towers automatically fire at Hunter units. The visual
	// streak exists during the flight, but cannot deal damage itself.
	fvd->combat_tick(state, at(15));
	TESTEQUALS(fvd->get_visible_arrows(at(14.9)).empty(), true);
	auto trails = fvd->get_visible_arrows(at(15.1));
	auto tower_trail = std::find_if(trails.begin(), trails.end(),
	                              [](const auto &trail) { return trail.owner == 5; });
	tower_trail != trails.end() or TESTFAIL;
	TESTEQUALS(tower_trail->progress > 0.0, true);
	TESTEQUALS(tower_trail->progress < 1.0, true);
	TESTEQUALS(tower_trail->head != tower_trail->tail, true);
	TESTEQUALS(tower_trail->impact, false);
	TESTEQUALS(std::any_of(trails.begin(), trails.end(),
	                       [](const auto &trail) { return trail.owner == 1; }), true);
	// Both Tower and Hunter arcs retain a brief cosmetic impact flash.
	auto at_tower_impact = fvd->get_visible_arrows(at(15.2));
	TESTEQUALS(std::any_of(at_tower_impact.begin(), at_tower_impact.end(),
	                       [](const auto &trail) { return trail.owner == 5 and trail.impact; }),
	           true);
	loop->reach_time(at(15.2), state);
	auto horse_live = std::dynamic_pointer_cast<component::Live>(
		one->get_component(component::component_t::LIVE));
	TESTEQUALS(horse_live->get_attribute(at(15.2), "fvd.data.entities.Health")->get(at(15.2)), int64_t{88});

	// An already-lethal in-flight shot resolves once; extra hits against a
	// corpse cannot award another kill. Normal tower volleys are much weaker
	// after the balance adjustment; simulate a lethal hit explicitly here,
	// and verify sustained group combat in fvd_combat_balance.
	fvd->arrow_hit(state, one->get_id(), 5, tower, 100, at(16));
	TESTEQUALS(state->get_game_entities().contains(one->get_id()), false);
	auto kills = fvd->get_kills(5);
	TESTEQUALS(kills >= 1, true);
	fvd->arrow_hit(state, one->get_id(), 5, 0, 100, at(32));
	TESTEQUALS(fvd->get_kills(5), kills);
	TESTEQUALS(fvd->get_kills(4), uint64_t{0});

	// A horse inside firing range damages a tower, with armour
	// reducing but not preventing all damage.
	auto melee = entities->spawn_game_entity(loop, state, 1, "fvd.data.entities.Horse", {67.5, 69.5, 0}, at(33));
	TESTEQUALS(fvd->issue_attack_move(state, 1, {melee->get_id()},
	                                  {67, 70, 0}, at(33)), true);
	const auto tower_hp_before_melee = tower_health->get_attribute(
		at(33), "fvd.data.entities.Health")->get(at(33));
	fvd->combat_tick(state, at(33));
	TESTEQUALS(melee->get_animation_action(), AnimationAction::ATTACK);
	TESTEQUALS(melee->get_animation_started_at(), at(33));
	// One immutable Hunter projectile is published at animation start.
	// The host still schedules the sole authoritative HP mutation at
	// precisely three quarters of the 0.6s attack period.
	const auto projectiles = fvd->get_arrow_state(at(33));
	auto horse_shot = std::find_if(projectiles.begin(), projectiles.end(),
	                               [](const auto &shot) { return shot.owner == 1; });
	horse_shot != projectiles.end() or TESTFAIL;
	TESTEQUALS(horse_shot->target_id, tower);
	TESTEQUALS(horse_shot->launch_time, at(33));
	// The cooldown is quantized to fixed-point time *before* the
	// 75% hit delay is calculated. Independently converting 33.45
	// differs by a raw tick on some platforms.
	const auto expected_hit_delay = time::time_t::from_double(
		time::time_t::from_double(0.6).to_double() * 0.75);
	TESTEQUALS(horse_shot->impact_time, at(33) + expected_hit_delay);
	TESTEQUALS(std::any_of(projectiles.begin(), projectiles.end(),
	                       [&](const auto &shot) { return shot.arrow_id == horse_shot->arrow_id
	                              and shot.destination.up > coord::phys_t::from_int(0); }), true);
	loop->reach_time(at(33.449), state);
	TESTEQUALS(tower_health->get_attribute(
		at(33.449), "fvd.data.entities.Health")->get(at(33.449)),
	           tower_hp_before_melee);
	loop->reach_time(at(33.45), state);
	TESTEQUALS(tower_health->get_attribute(
		at(33.45), "fvd.data.entities.Health")->get(at(33.45)) < tower_hp_before_melee,
	           true);

	// A normal Move command interrupts an active ranged engagement.
	event::SendCommandHandler commander;
	bool accepted = false;
	commander.invoke(*loop, nullptr, state, at(33.5),
	                 {{"type", component::command::command_t::MOVE},
	                  {"owner", player_id_t{1}},
	                  {"entity_ids", std::vector<entity_id_t>{melee->get_id()}},
	                  {"target", coord::phys3{80, 80, 0}},
	                  {"result_cb", std::function<void(bool)>{[&accepted](bool result) {
		                  accepted = result;
	                  }}}});
	TESTEQUALS(accepted, true);
	TESTEQUALS(melee->get_animation_action(), AnimationAction::MOVE);
	auto melee_move = std::dynamic_pointer_cast<component::Move>(
		melee->get_component(component::component_t::MOVE));
	TESTEQUALS(melee_move->get_active_target().has_value(), true);
	if (melee_move->get_active_target()) {
		TESTEQUALS(*melee_move->get_active_target(), coord::phys3(80, 80, 0));
	}

	// Destroying the tower does not erase its kills. The last arrow still
	// awards its owner if it lands after the tower was destroyed.
	auto last_victim = entities->spawn_game_entity(
		loop, state, 1, "fvd.data.entities.Horse", {90, 90, 0}, at(34));
	TESTEQUALS(fvd->destroy_tower(state, tower, at(34)), true);
	TESTEQUALS(state->get_game_entities().contains(tower), false);
	fvd->arrow_hit(state, last_victim->get_id(), 5, tower, 100, at(34.1));
	TESTEQUALS(state->get_game_entities().contains(last_victim->get_id()), false);
	TESTEQUALS(fvd->get_tower_kills(tower, 5).value(), kills + 1);
	TESTEQUALS(fvd->get_kills(5), kills + 1);
	TESTEQUALS(fvd->get_tower_kills(tower, 4).has_value(), false);
}

/**
 * Real combat timeline plus the ideal stationary-volley bounds used for
 * tuning FVD. Travel, repositioning and focus changes can lengthen combat.
 */
void fvd_combat_balance() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	tests::use_centre_test_layout(state);
	auto at = [](double seconds) { return time::time_t::from_double(seconds); };

	auto rules = state->get_db_view()->get_object("fvd.data.scenario.Scenario");
	const int64_t melee_damage = rules.get_int("horse_melee_damage");
	TESTEQUALS(fvd->get_tower_damage(), int64_t{2});
	TESTEQUALS(melee_damage, int64_t{3});
	TESTEQUALS(fvd->get_tower_armor(), int64_t{1});
	// Existing reloads: towers 0.8s (+0.2s flight), horses 0.6s.
	// Compute stationary group DPS in the same integer-volley model as
	// combat_tick, rather than assuming fractional damage per hit.
	for (int64_t tower_count : {3, 4}) {
		const int64_t total_damage = tower_count * fvd->get_tower_damage();
		const int64_t volleys = (90 + total_damage - 1) / total_damage;
		const double lethal_time = (volleys - 1) * 0.8 + 0.2;
		TESTEQUALS(lethal_time >= 8.0 and lethal_time <= 12.0, true);
		// Three focused towers: 11.4s; four focused towers: 9.0s.
		TESTEQUALS_FLOAT(lethal_time, tower_count == 3 ? 11.4 : 9.0, 0.001);
	}
	// Three horses striking through tower armor should take 17 melee
	// volleys, 0.6s apart, to remove 100 HP (~9.6s).
	const int64_t horse_hit = std::max<int64_t>(1, melee_damage - fvd->get_tower_armor());
	const int64_t melee_volleys = (100 + 3 * horse_hit - 1) / (3 * horse_hit);
	TESTEQUALS(melee_volleys, int64_t{17});
	TESTEQUALS_FLOAT((melee_volleys - 1) * 0.6 + 0.45, 10.05, 0.001);

	// Create a fully built tower via the same construction pipeline as
	// gameplay, then focus three real, immediately adjacent Hunter horses.
	entity_id_t builder = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		auto owner = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (owner->get_owners().get(time::TIME_ZERO) == player_id_t{5}) {
			builder = id;
			break;
		}
	}
	TESTEQUALS(builder != 0, true);
	TESTEQUALS(fvd->request_build(state, 5, {builder}, {68, 69, 0}, at(0)), true);
	loop->reach_time(at(15), state);
	entity_id_t tower = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.Tower") {
			tower = id;
			break;
		}
	}
	TESTEQUALS(tower != 0, true);
	auto tower_live = std::dynamic_pointer_cast<component::Live>(
		state->get_game_entity(tower)->get_component(component::component_t::LIVE));
	TESTEQUALS(tower_live->get_attribute(
		at(15), "fvd.data.entities.Health")->get(at(15)), int64_t{100});

	std::vector<entity_id_t> attackers{};
	for (const coord::phys3 location : {
	         coord::phys3{69, 69, 0}, coord::phys3{69, 70, 0},
	         coord::phys3{68, 70, 0}}) {
		auto horse = entities->spawn_game_entity(
			loop, state, 1, "fvd.data.entities.Horse", location, at(15));
		auto live = std::dynamic_pointer_cast<component::Live>(
			horse->get_component(component::component_t::LIVE));
		TESTEQUALS(live->get_attribute(
			at(15), "fvd.data.entities.Health")->get(at(15)), int64_t{90});
		attackers.push_back(horse->get_id());
	}
	TESTEQUALS(fvd->issue_focus_attack(state, 1, attackers, tower, at(15)), true);
	double killed_after = -1.0;
	for (size_t tick = 0; tick <= 65; ++tick) {
		const double elapsed = static_cast<double>(tick) * 0.2;
		const auto now = at(15.0 + elapsed);
		loop->reach_time(now, state);
		fvd->combat_tick(state, now);
		if (not state->get_game_entities().contains(tower)) {
			killed_after = elapsed;
			break;
		}
	}
	// The test exercises the actual scheduler, damage and tower destruction,
	// not just the ideal math above. A little tick rounding is acceptable.
	TESTEQUALS(killed_after >= 8.0 and killed_after <= 12.0, true);
	TESTEQUALS(state->get_game_entities().contains(tower), false);
}

void fvd_hunter_attack_move_regressions() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	tests::use_centre_test_layout(state);
	auto at = [](double seconds) { return time::time_t::from_double(seconds); };

	entity_id_t builder = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (ownership->get_owners().get(time::TIME_ZERO) == player_id_t{5}) {
			builder = id;
			break;
		}
	}
	TESTEQUALS(builder != 0, true);
	TESTEQUALS(fvd->request_build(state, 5, {builder}, {68, 69, 0}, at(0)), true);
	loop->reach_time(at(15), state);
	entity_id_t tower = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.Tower") {
			tower = id;
			break;
		}
	}
	TESTEQUALS(tower != 0, true);
	auto tower_entity = state->get_game_entity(tower);
	auto health = std::dynamic_pointer_cast<component::Live>(
		tower_entity->get_component(component::component_t::LIVE));

	// Tower priority must beat even a closer Builder in firing range.
	auto protected_builder = entities->spawn_game_entity(
		loop, state, 2, "fvd.data.entities.Builder", {72, 69, 0}, at(15));
	auto protected_health = std::dynamic_pointer_cast<component::Live>(
		protected_builder->get_component(component::component_t::LIVE));
	auto protected_hp = protected_health->get_attribute(
		at(15), "fvd.data.entities.Health")->get(at(15));

	// Notice a tower within four tiles and attack it instead of
	// following an attack-move path that leads away from the tower.
	auto approaching = entities->spawn_game_entity(
		loop, state, 1, "fvd.data.entities.Horse", {71, 69, 0}, at(15));
	TESTEQUALS(fvd->issue_attack_move(state, 1, {approaching->get_id()},
	                                  {78, 69, 0}, at(15)), true);
	auto approach_position = std::dynamic_pointer_cast<component::Position>(
		approaching->get_component(component::component_t::POSITION));
	auto tower_position = std::dynamic_pointer_cast<component::Position>(
		tower_entity->get_component(component::component_t::POSITION));
	auto tower_at = tower_position->get_positions().get(at(15));
	auto before = approach_position->get_positions().get(at(15)) - tower_at;
	fvd->combat_tick(state, at(15));
	TESTEQUALS(protected_health->get_attribute(
		at(15), "fvd.data.entities.Health")->get(at(15)), protected_hp);
	auto after = approach_position->get_positions().get(at(15.2)) - tower_at;
	TESTEQUALS(after == before, true);
	TESTEQUALS(approaching->get_animation_action(), AnimationAction::ATTACK);
	fvd->combat_tick(state, at(16));
	TESTEQUALS(health->get_attribute(
		at(16), "fvd.data.entities.Health")->get(at(16)), int64_t{100});
	loop->reach_time(at(16.45), state);
	TESTEQUALS(health->get_attribute(
		at(16.45), "fvd.data.entities.Health")->get(at(16.45)) < 100, true);

	// Reaching the destination doesn't remove tower awareness.
	// Remove the Builder used for the priority assertion so its new
	// AttackMove aggro does not replace the tower-awareness scenario.
	state->remove_game_entity(protected_builder->get_id(), at(19));
	auto finished = entities->spawn_game_entity(
		loop, state, 1, "fvd.data.entities.Horse", {74, 69, 0}, at(20));
	TESTEQUALS(fvd->issue_attack_move(state, 1, {finished->get_id()},
	                                  {71, 69, 0}, at(20)), true);
	fvd->combat_tick(state, at(20));
	auto finished_position = std::dynamic_pointer_cast<component::Position>(
		finished->get_component(component::component_t::POSITION));
	auto at_destination = finished_position->get_positions().get(at(21.2)) - tower_at;
	fvd->combat_tick(state, at(21.2));
	auto after_destination = finished_position->get_positions().get(at(21.5)) - tower_at;
	TESTEQUALS(after_destination == at_destination, true);
	TESTEQUALS(finished->get_animation_action(), AnimationAction::ATTACK);

	// A horse killed between selection and command is pruned, rather
	// than invalidating all surviving Hunter horses in that selection.
	auto dead = entities->spawn_game_entity(
		loop, state, 1, "fvd.data.entities.Horse", {95, 95, 0}, at(30));
	auto survivor = entities->spawn_game_entity(
		loop, state, 1, "fvd.data.entities.Horse", {96, 95, 0}, at(30));
	fvd->arrow_hit(state, dead->get_id(), 5, 0, 100, at(30));
	TESTEQUALS(state->get_game_entities().contains(dead->get_id()), false);
	TESTEQUALS(fvd->can_command_units(
		state, 1, {dead->get_id(), survivor->get_id()}, at(30)), true);
	TESTEQUALS(fvd->issue_attack_move(state, 1, {dead->get_id(), survivor->get_id()},
	                                  {97, 95, 0}, at(30)), true);
	auto surviving_move = std::dynamic_pointer_cast<component::Move>(
		survivor->get_component(component::component_t::MOVE));
	TESTEQUALS(surviving_move->get_active_target().has_value(), true);
	TESTEQUALS(fvd->can_command_units(state, 1, {dead->get_id()}, at(30)), false);
	TESTEQUALS(fvd->issue_attack_move(state, 1, {dead->get_id()},
	                                  {98, 95, 0}, at(30)), false);
	TESTEQUALS(fvd->issue_attack_move(state, 1, {survivor->get_id(), builder},
	                                  {98, 95, 0}, at(30)), false);
	TESTEQUALS(fvd->issue_attack_move(state, 1, {survivor->get_id(), entity_id_t{999999}},
	                                  {98, 95, 0}, at(30)), false);
	TESTEQUALS(fvd->issue_attack_move(state, 1, {survivor->get_id(), survivor->get_id()},
	                                  {98, 95, 0}, at(30)), false);
	TESTEQUALS(fvd->issue_attack_move(state, 2, {survivor->get_id()},
	                                  {98, 95, 0}, at(30)), false);

	// Both Hunter movement orders go through the same stale-selection
	// validation at the authoritative command handler.
	event::SendCommandHandler commander;
	bool accepted = false;
	commander.invoke(*loop, nullptr, state, at(30),
	                 {{"type", component::command::command_t::MOVE},
	                  {"owner", player_id_t{1}},
	                  {"entity_ids", std::vector<entity_id_t>{dead->get_id(), survivor->get_id()}},
	                  {"target", coord::phys3{98, 95, 0}},
	                  {"result_cb", std::function<void(bool)>{[&accepted](bool result) {
		                  accepted = result;
	                  }}}});
	TESTEQUALS(accepted, true);
	// A-click directly on an occupied tower tile must not be rejected by
	// ground pathfinding. Its centre is (68.5, 69.5), not (68, 69);
	// test exactly four tiles away from the actual centre, not tile origin.
	auto clicked_horse = entities->spawn_game_entity(
		loop, state, 1, "fvd.data.entities.Horse", {64.5, 69.5, 0}, at(32));
	TESTEQUALS(fvd->issue_attack_move(
		state, 1, {clicked_horse->get_id()}, {68, 69, 0}, at(32)), true);
	auto clicked_pos = std::dynamic_pointer_cast<component::Position>(
		clicked_horse->get_component(component::component_t::POSITION));
	const auto actual_tower_centre = std::dynamic_pointer_cast<component::Position>(
		tower_entity->get_component(component::component_t::POSITION))
		->get_positions().get(at(32));
	TESTEQUALS(actual_tower_centre, (coord::phys3{68.5, 69.5, 0}));
	fvd->combat_tick(state, at(32));
	TESTEQUALS(clicked_horse->get_animation_action(), AnimationAction::ATTACK);
	TESTEQUALS(clicked_pos->get_positions().get(at(32.2)), (coord::phys3{64.5, 69.5, 0}));

	// A missed sprite pick still produces a ground Move command. Clicking
	// the tower's actual tile must resolve to the same ranged focus order.
	auto missed_pick_horse = entities->spawn_game_entity(
		loop, state, 1, "fvd.data.entities.Horse", {68.5, 65.5, 0}, at(34));
	bool clicked_accepted = false;
	event::SendCommandHandler tower_click;
	tower_click.invoke(*loop, nullptr, state, at(34),
	                   {{"type", component::command::command_t::MOVE},
	                    {"owner", player_id_t{1}},
	                    {"entity_ids", std::vector<entity_id_t>{missed_pick_horse->get_id()}},
	                    {"target", coord::phys3{68, 69, 0}},
	                    {"result_cb", std::function<void(bool)>{[&clicked_accepted](bool accepted) {
		                    clicked_accepted = accepted;
	                    }}}});
	TESTEQUALS(clicked_accepted, true);
	auto missed_pick_pos = std::dynamic_pointer_cast<component::Position>(
		missed_pick_horse->get_component(component::component_t::POSITION));
	fvd->combat_tick(state, at(34));
	TESTEQUALS(missed_pick_horse->get_animation_action(), AnimationAction::ATTACK);
	TESTEQUALS(missed_pick_pos->get_positions().get(at(34.2)), (coord::phys3{68.5, 65.5, 0}));

}

void fvd_hunter_builder_aggro() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	auto at = [](double seconds) { return time::time_t::from_double(seconds); };

	// A Builder inside four-tile firing range must be attacked even
	// when the Hunter clicked a destination in the opposite direction.
	auto builder = entities->spawn_game_entity(
		loop, state, 2, "fvd.data.entities.Builder", {105, 105, 0}, at(0));
	auto horse = entities->spawn_game_entity(
		loop, state, 1, "fvd.data.entities.Horse", {102.5, 105, 0}, at(0));
	auto builder_health = std::dynamic_pointer_cast<component::Live>(
		builder->get_component(component::component_t::LIVE));
	auto hp = builder_health->get_attribute(
		at(0), "fvd.data.entities.Health")->get(at(0));
	TESTEQUALS(fvd->issue_attack_move(state, 1, {horse->get_id()},
	                                  {99, 105, 0}, at(0)), true);
	auto builder_position = std::dynamic_pointer_cast<component::Position>(
		builder->get_component(component::component_t::POSITION));
	auto horse_position = std::dynamic_pointer_cast<component::Position>(
		horse->get_component(component::component_t::POSITION));
	auto builder_at = builder_position->get_positions().get(at(0));
	auto before = horse_position->get_positions().get(at(0)) - builder_at;
	fvd->combat_tick(state, at(0));
	auto after = horse_position->get_positions().get(at(0.25)) - builder_at;
	TESTEQUALS(after == before, true);
	TESTEQUALS(horse->get_animation_action(), AnimationAction::ATTACK);
	fvd->combat_tick(state, at(1.2));
	TESTEQUALS(horse->get_animation_started_at(), at(1.2));
	TESTEQUALS(builder_health->get_attribute(
		at(1.2), "fvd.data.entities.Health")->get(at(1.2)), hp);
	loop->reach_time(at(1.65), state);
	TESTEQUALS(builder_health->get_attribute(
		at(1.65), "fvd.data.entities.Health")->get(at(1.65)) < hp, true);

	// Detection continues after arrival when a Builder enters the
	// four-tile radius; it is not limited to passing enemies en route.
	auto finished = entities->spawn_game_entity(
		loop, state, 1, "fvd.data.entities.Horse", {110, 110, 0}, at(3));
	TESTEQUALS(fvd->issue_attack_move(state, 1, {finished->get_id()},
	                                  {111, 110, 0}, at(3)), true);
	fvd->combat_tick(state, at(4));
	auto finished_position = std::dynamic_pointer_cast<component::Position>(
		finished->get_component(component::component_t::POSITION));
	auto destination = finished_position->get_positions().get(at(4));
	TESTEQUALS(destination.ne == coord::phys_t::from_int(111), true);
	auto late_builder = entities->spawn_game_entity(
		loop, state, 3, "fvd.data.entities.Builder", {113, 110, 0}, at(4.1));
	auto late_position = std::dynamic_pointer_cast<component::Position>(
		late_builder->get_component(component::component_t::POSITION));
	auto late_at = late_position->get_positions().get(at(4.1));
	fvd->combat_tick(state, at(4.1));
	auto late_before = finished_position->get_positions().get(at(4.1)) - late_at;
	auto late_after = finished_position->get_positions().get(at(4.35)) - late_at;
	TESTEQUALS(late_after == late_before, true);
	TESTEQUALS(finished->get_animation_action(), AnimationAction::ATTACK);
	auto late_health = std::dynamic_pointer_cast<component::Live>(
		late_builder->get_component(component::component_t::LIVE));
	auto late_hp = late_health->get_attribute(
		at(4.1), "fvd.data.entities.Health")->get(at(4.1));
	fvd->combat_tick(state, at(5.1));
	TESTEQUALS(late_health->get_attribute(
		at(5.1), "fvd.data.entities.Health")->get(at(5.1)), late_hp);
	loop->reach_time(at(5.55), state);
	TESTEQUALS(late_health->get_attribute(
		at(5.55), "fvd.data.entities.Health")->get(at(5.55)) < late_hp, true);

	// Exactly four tiles away: attack from the current position.
	auto ranged_builder = entities->spawn_game_entity(
		loop, state, 2, "fvd.data.entities.Builder", {114, 120, 0}, at(7));
	auto ranged_horse = entities->spawn_game_entity(
		loop, state, 1, "fvd.data.entities.Horse", {110, 120, 0}, at(7));
	TESTEQUALS(fvd->issue_attack_move(state, 1, {ranged_horse->get_id()},
	                                  {106, 120, 0}, at(7)), true);
	auto ranged_position = std::dynamic_pointer_cast<component::Position>(
		ranged_horse->get_component(component::component_t::POSITION));
	fvd->combat_tick(state, at(7));
	TESTEQUALS(ranged_horse->get_animation_action(), AnimationAction::ATTACK);
	TESTEQUALS(ranged_position->get_positions().get(at(7.2)),
	           (coord::phys3{110, 120, 0}));
	auto ranged_health = std::dynamic_pointer_cast<component::Live>(
		ranged_builder->get_component(component::component_t::LIVE));
	const auto ranged_hp = ranged_health->get_attribute(
		at(7), "fvd.data.entities.Health")->get(at(7));
	loop->reach_time(at(7.45), state);
	TESTEQUALS(ranged_health->get_attribute(
		at(7.45), "fvd.data.entities.Health")->get(at(7.45)) < ranged_hp, true);

	// Five tiles away: focus order approaches a reachable ranged position.
	auto distant_builder = entities->spawn_game_entity(
		loop, state, 2, "fvd.data.entities.Builder", {116, 124, 0}, at(9));
	auto distant_horse = entities->spawn_game_entity(
		loop, state, 1, "fvd.data.entities.Horse", {111, 124, 0}, at(9));
	TESTEQUALS(fvd->issue_focus_attack(
		state, 1, {distant_horse->get_id()}, distant_builder->get_id(), at(9)), true);
	auto distant_move = std::dynamic_pointer_cast<component::Move>(
		distant_horse->get_component(component::component_t::MOVE));
	TESTEQUALS(distant_move->get_active_target().has_value(), true);
	auto distant_position = std::dynamic_pointer_cast<component::Position>(
		distant_horse->get_component(component::component_t::POSITION));
	const auto before_approach = distant_position->get_positions().get(at(9));
	fvd->combat_tick(state, at(9));
	const auto after_approach = distant_position->get_positions().get(at(9.2));
	TESTEQUALS(after_approach.ne > before_approach.ne, true);
}


void fvd_hunter_focus_attack() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	tests::use_centre_test_layout(state);
	auto at = [](double sec) { return time::time_t::from_double(sec); };

	entity_id_t owner_builder = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (ownership->get_owners().get(time::TIME_ZERO) == player_id_t{5}) {
			owner_builder = id;
			break;
		}
	}
	TESTEQUALS(owner_builder != 0, true);
	TESTEQUALS(fvd->request_build(state, 5, {owner_builder}, {68, 69, 0}, at(0)), true);
	loop->reach_time(at(15), state);
	entity_id_t tower = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.Tower") {
			tower = id;
			break;
		}
	}
	TESTEQUALS(tower != 0, true);
	auto tower_entity = state->get_game_entity(tower);
	auto tower_live = std::dynamic_pointer_cast<component::Live>(
		tower_entity->get_component(component::component_t::LIVE));
	auto hp_tower = tower_live->get_attribute(
		at(15), "fvd.data.entities.Health")->get(at(15));

	// Tower and Builder are equally close to the Hunter horse.
	// Focus-attack must pursue the specifically chosen Builder.
	auto builder = entities->spawn_game_entity(
		loop, state, 2, "fvd.data.entities.Builder", {72, 69, 0}, at(15));
	auto horse = entities->spawn_game_entity(
		loop, state, 1, "fvd.data.entities.Horse", {70, 69, 0}, at(15));
	auto builder_live = std::dynamic_pointer_cast<component::Live>(
		builder->get_component(component::component_t::LIVE));
	auto hp_builder = builder_live->get_attribute(
		at(15), "fvd.data.entities.Health")->get(at(15));

	// Command authentication and target validation happen before altering
	// any existing orders. Unknown, friendly or foreign targets fail.
	TESTEQUALS(fvd->issue_focus_attack(
		state, 2, {horse->get_id()}, builder->get_id(), at(15)), false);
	TESTEQUALS(fvd->issue_focus_attack(
		state, 1, {horse->get_id()}, horse->get_id(), at(15)), false);
	TESTEQUALS(fvd->issue_focus_attack(
		state, 1, {horse->get_id()}, entity_id_t{999999}, at(15)), false);
	TESTEQUALS(fvd->issue_focus_attack(
		state, 1, {horse->get_id(), owner_builder}, builder->get_id(), at(15)), false);

	// Exercise the same command entry point used by right-click picking.
	event::SendCommandHandler command;
	bool accepted = false;
	command.invoke(*loop, nullptr, state, at(15),
	               {{"type", component::command::command_t::FOCUS_ATTACK},
	                {"owner", player_id_t{1}},
	                {"target_entity", builder->get_id()},
	                {"entity_ids", std::vector<entity_id_t>{horse->get_id()}},
	                {"result_cb", std::function<void(bool)>{[&accepted](bool result) {
		                accepted = result;
	                }}}});
	TESTEQUALS(accepted, true);
	auto horse_move = std::dynamic_pointer_cast<component::Move>(
		horse->get_component(component::component_t::MOVE));
	TESTEQUALS(horse_move->get_active_target().has_value(), false);
	fvd->combat_tick(state, at(15));
	fvd->combat_tick(state, at(16));
	TESTEQUALS(builder_live->get_attribute(
		at(16), "fvd.data.entities.Health")->get(at(16)), hp_builder);
	loop->reach_time(at(16.45), state);
	TESTEQUALS(builder_live->get_attribute(
		at(16.45), "fvd.data.entities.Health")->get(at(16.45)) < hp_builder, true);
	TESTEQUALS(tower_live->get_attribute(
		at(16), "fvd.data.entities.Health")->get(at(16)), hp_tower);

	// An explicit target remains the priority for its entire lifetime.
	for (int i = 17; i <= 25 and state->get_game_entities().contains(builder->get_id()); ++i) {
		loop->reach_time(at(i), state);
		fvd->combat_tick(state, at(i));
	}
	TESTEQUALS(state->get_game_entities().contains(builder->get_id()), false);
	fvd->combat_tick(state, at(26));
	TESTEQUALS(horse_move->get_active_target().has_value(), false);
	TESTEQUALS(tower_live->get_attribute(
		at(26), "fvd.data.entities.Health")->get(at(26)), hp_tower);

	// A fresh focus order can be replaced by an ordinary Move, so no
	// stale focus remains to override subsequent human commands.
	auto other_builder = entities->spawn_game_entity(
		loop, state, 3, "fvd.data.entities.Builder", {71, 69, 0}, at(27));
	TESTEQUALS(fvd->issue_focus_attack(
		state, 1, {horse->get_id()}, other_builder->get_id(), at(27)), true);
	// Confirm that an attack really launches before we cancel it; the
	// immutable projectile remains cosmetic, while its pending HP event
	// must be invalidated by the replacement Move command.
	fvd->combat_tick(state, at(27));
	auto launched = fvd->get_arrow_state(at(27));
	auto cancelled_shot = std::find_if(
		launched.begin(), launched.end(), [&](const auto &shot) {
			return shot.owner == 1 and shot.target_id == other_builder->get_id()
			       and shot.launch_time == at(27);
		});
	cancelled_shot != launched.end() or TESTFAIL;
	accepted = false;
	command.invoke(*loop, nullptr, state, at(27.1),
	               {{"type", component::command::command_t::MOVE},
	                {"owner", player_id_t{1}},
	                {"target", coord::phys3{75, 69, 0}},
	                {"entity_ids", std::vector<entity_id_t>{horse->get_id()}},
	                {"result_cb", std::function<void(bool)>{[&accepted](bool result) {
		                accepted = result;
	                }}}});
	TESTEQUALS(accepted, true);
	auto other_live = std::dynamic_pointer_cast<component::Live>(
		other_builder->get_component(component::component_t::LIVE));
	auto other_hp = other_live->get_attribute(
		at(27), "fvd.data.entities.Health")->get(at(27));
	loop->reach_time(at(27.6), state);
	TESTEQUALS(other_live->get_attribute(
		at(27.6), "fvd.data.entities.Health")->get(at(27.6)), other_hp);
	fvd->combat_tick(state, at(28));
	TESTEQUALS(other_live->get_attribute(
		at(28), "fvd.data.entities.Health")->get(at(28)), other_hp);
}

void fvd_presentation_snapshot() {
	auto root = util::Path{
		std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto cvars = std::make_shared<cvar::CVarManager>(root);
	auto clock = std::make_shared<time::TimeLoop>();
	GameSimulation simulation{root, cvars, clock};
	simulation.set_modpacks({"fvd"});
	simulation.initialize();
	auto initial = simulation.get_fvd_presentation_state();
	TESTEQUALS(initial != nullptr, true);
	// Seven Builders and the Hunter tower.
	TESTEQUALS(initial->entities.size(), size_t{8});
	TESTEQUALS(network::make_construction_presentation(*initial).sites.empty(), true);
	auto builder = std::find_if(
		initial->entities.begin(), initial->entities.end(), [](const auto &entity) { return entity.owner_id == 2; });
	TESTEQUALS(builder != initial->entities.end(), true);

	auto state = simulation.get_game()->get_state();
	auto fvd = state->get_fvd();
	tests::use_centre_test_layout(state);
	network::AuthenticatedCommand command{};
	command.identity.player_id = 2;
	command.intent.command_id = 1;
	command.intent.kind = network::CommandKind::BUILD_TOWER;
	command.intent.entity_ids = {builder->entity_id};
	command.intent.target = {61, 66, 0};
	bool accepted = false;
	auto on_build = [&](bool result) {
		accepted = result;
		TESTEQUALS(fvd->queue_build(state, 2, {builder->entity_id}, {57, 66, 0}, time::TIME_ZERO), true);
		TESTEQUALS(fvd->queue_build(state, 2, {builder->entity_id}, {54, 66, 0}, time::TIME_ZERO), true);
		// A reader during command execution keeps the last completed world.
		TESTEQUALS(simulation.get_fvd_presentation_state() == initial, true);
		simulation.stop();
	};
	TESTEQUALS(simulation.submit_network_command(command, on_build), true);
	simulation.run();
	TESTEQUALS(accepted, true);
	auto building = simulation.get_fvd_presentation_state();
	TESTEQUALS(building != nullptr, true);
	TESTEQUALS(building == initial, false);
	const auto view = network::make_construction_presentation(*building);
	TESTEQUALS(view.sites.size(), size_t{3});
	TESTEQUALS(std::count_if(view.sites.begin(), view.sites.end(), [](const auto &site) {
				   return site.phase == network::SitePresentationPhase::BUILDING;
			   }),
	           1);
	TESTEQUALS(std::count_if(view.sites.begin(), view.sites.end(), [](const auto &site) {
				   return site.phase == network::SitePresentationPhase::QUEUED;
			   }),
	           2);
	std::vector<int> numbers;
	for (const auto &site : view.sites) {
		numbers.push_back(static_cast<int>(site.queue_position));
	}
	std::sort(numbers.begin(), numbers.end());
	TESTEQUALS(numbers, (std::vector<int>{1, 2, 3}));
	// Replacement and cancellation cannot change a snapshot held by the HUD.
	TESTEQUALS(network::make_construction_presentation(*initial).sites.empty(), true);
	TESTEQUALS(fvd->cancel_build(state, builder->entity_id, time::TIME_ZERO), true);
	const auto canceled = network::capture_fvd_presentation_state(state, time::TIME_ZERO);
	const auto abandoned = network::make_construction_presentation(canceled);
	TESTEQUALS(abandoned.sites.size(), size_t{3});
	TESTEQUALS(abandoned.reservations.empty(), true);
	TESTEQUALS(std::all_of(abandoned.sites.begin(), abandoned.sites.end(), [](const auto &site) {
				   return site.phase == network::SitePresentationPhase::ORPHANED
				       and site.queue_position == 0;
			   }), true);
	TESTEQUALS(network::make_construction_presentation(*building).sites.size(), size_t{3});
}

void fvd_build_queue() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	tests::use_centre_test_layout(state);
	auto at = [](double sec) { return time::time_t::from_double(sec); };
	std::optional<entity_id_t> builder{};
	for (const auto &[id, entity] : state->get_game_entities()) {
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (ownership->get_owners().get(time::TIME_ZERO) == player_id_t{2}) {
			builder = id;
			break;
		}
	}
	builder.has_value() or TESTFAIL;
	const std::vector<entity_id_t> selected{*builder};
	TESTEQUALS(fvd->can_place_tower(state, 2, selected, {61, 66, 0}, at(0)), true);
	TESTEQUALS(fvd->can_place_tower(state, 1, selected, {61, 66, 0}, at(0)), false);
	auto queue = [&](const coord::phys3 &site, double when) {
		bool accepted = false;
		event::SendCommandHandler commander;
		commander.invoke(*loop, nullptr, state, at(when),
		                 {{"type", component::command::command_t::BUILD_TOWER},
		                  {"owner", player_id_t{2}}, {"queue", true},
		                  {"entity_ids", selected}, {"target", site},
		                  {"result_cb", std::function<void(bool)>{[&accepted](bool value) {
			                  accepted = value;
		                  }}}});
		return accepted;
	};
	TESTEQUALS(queue({61, 66, 0}, 0), true);
	TESTEQUALS(queue({57, 66, 0}, 0), true);
	TESTEQUALS(queue({54, 66, 0}, 0), true);
	TESTEQUALS(fvd->get_queued_build_count(*builder), size_t{2});
	TESTEQUALS(fvd->is_tile_reserved({54, 66}), true);
	TESTEQUALS(queue({57, 66, 0}, 0), false);
	TESTEQUALS(fvd->can_place_tower(state, 2, selected, {57, 66, 0}, at(0)), false);
	const auto queued_world = network::capture_fvd_presentation_state(state, at(0));
	TESTEQUALS(network::make_construction_presentation(queued_world).sites.size(), size_t{3});
	loop->reach_time(at(0.1), state);
	TESTEQUALS(fvd->get_queued_build_count(*builder), size_t{1});
	TESTEQUALS(fvd->get_build_order(*builder)->get_target(), (coord::tile{57, 66}));
	const auto advanced_world = network::capture_fvd_presentation_state(state, at(0.1));
	const auto advanced_view = network::make_construction_presentation(advanced_world);
	TESTEQUALS(advanced_view.sites.size(), size_t{3});
	TESTEQUALS(std::count_if(advanced_view.sites.begin(), advanced_view.sites.end(), [](const auto &site) {
				   return site.phase == network::SitePresentationPhase::COMPLETE;
			   }),
	           1);
	loop->reach_time(at(40), state);
	TESTEQUALS(fvd->get_build_order(*builder) == nullptr, true);
	TESTEQUALS(fvd->get_queued_build_count(*builder), size_t{0});
	size_t towers = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.Tower") {
			++towers;
		}
	}
	TESTEQUALS(towers, size_t{3});
	const auto completed_world = network::capture_fvd_presentation_state(state, at(40));
	const auto completed_view = network::make_construction_presentation(completed_world);
	TESTEQUALS(completed_view.sites.size(), size_t{3});
	TESTEQUALS(completed_view.reservations.empty(), true);
	TESTEQUALS(std::all_of(completed_view.sites.begin(), completed_view.sites.end(), [](const auto &site) {
				   return site.phase == network::SitePresentationPhase::COMPLETE;
			   }),
	           true);
	TESTEQUALS(network::make_construction_presentation(queued_world).sites.size(), size_t{3});
	TESTEQUALS(fvd->is_tile_reserved({57, 66}), false);
	TESTEQUALS(fvd->is_tile_reserved({54, 66}), false);
	TESTEQUALS(queue({45, 66, 0}, 41), true);
	TESTEQUALS(queue({42, 66, 0}, 41), true);
	TESTEQUALS(fvd->cancel_build(state, *builder, at(41.1)), true);
	TESTEQUALS(fvd->is_tile_reserved({45, 66}), false);
	TESTEQUALS(fvd->is_tile_reserved({42, 66}), false);
	TESTEQUALS(fvd->get_queued_build_count(*builder), size_t{0});
	const auto orphaned_world = network::capture_fvd_presentation_state(state, at(41.1));
	const auto orphaned_view = network::make_construction_presentation(orphaned_world);
	TESTEQUALS(std::count_if(orphaned_view.sites.begin(), orphaned_view.sites.end(), [](const auto &site) {
				   return site.phase == network::SitePresentationPhase::ORPHANED;
			   }), size_t{2});
	TESTEQUALS(fvd->can_place_tower(state, 2, selected, {42, 66, 0}, at(41.1)), false);
	auto orphan = std::find_if(orphaned_view.sites.begin(), orphaned_view.sites.end(),
	                          [](const auto &site) { return site.site == coord::tile{42, 66}; });
	TESTEQUALS(orphan != orphaned_view.sites.end(), true);
	TESTEQUALS(fvd->prioritize_foundation(state, 2, selected, orphan->entity_id, at(41.1)), true);
	TESTEQUALS(fvd->get_build_order(*builder)->get_target(), (coord::tile{42, 66}));
	TESTEQUALS(fvd->is_tile_reserved({42, 66}), true);
}

void fvd_animation_phase() {
	TESTTHROWS(renderer::resources::select_animation_variant(
		77, time::TIME_ZERO, 0, 0));
	std::array<bool, 2> selected_variants{};
	for (uint64_t cycle = 0; cycle < 64; ++cycle) {
		const auto selected = renderer::resources::select_animation_variant(
			77, time::time_t::from_int(20), cycle, selected_variants.size());
		TESTEQUALS(selected,
		           renderer::resources::select_animation_variant(
			           77, time::time_t::from_int(20), cycle,
			           selected_variants.size()));
		selected_variants.at(selected) = true;
	}
	TESTEQUALS(std::all_of(selected_variants.begin(), selected_variants.end(),
	                       [](bool selected) { return selected; }),
	           true);
	renderer::world::RenderEntity variant_entity{};
	const std::vector<std::string> idle_variants{
		"fvd/idle1.sprite", "fvd/idle2.sprite"};
	variant_entity.update_sample(77, coord::phys3{20, 20, 0}, idle_variants,
	                             time::time_t::from_int(20), std::nullopt, {},
	                             time::time_t::from_int(20));
	{
		auto lock = variant_entity.get_read_lock();
		const auto variants = variant_entity.get_animation_paths().get_time(
			time::time_t::from_int(20));
		TESTEQUALS(variants.first, time::time_t::from_int(20));
		TESTEQUALS(variants.second, idle_variants);
	}

	// An entity spawned after match start begins its very first idle
	// animation at spawn time, not when the match clock was zero.
	GameEntity spawned{77};
	spawned.render_update(time::time_t::from_int(20),
	                     "fvd/orc_idle.sprite", AnimationAction::IDLE);
	TESTEQUALS(spawned.get_animation_started_at(), time::time_t::from_int(20));
	spawned.render_update(time::time_t::from_int(21),
	                     "fvd/orc_idle.sprite", AnimationAction::IDLE);
	TESTEQUALS(spawned.get_animation_started_at(), time::time_t::from_int(20));
	spawned.render_update(time::time_t::from_double(21.5),
	                     "fvd/orc_idle_alternate.sprite", AnimationAction::IDLE);
	TESTEQUALS(spawned.get_animation_started_at(), time::time_t::from_double(21.5));
	spawned.render_update(time::time_t::from_int(22),
	                     "fvd/orc_walk.sprite", AnimationAction::MOVE);
	TESTEQUALS(spawned.get_animation_started_at(), time::time_t::from_int(22));
	spawned.render_update(time::time_t::from_double(22.5),
	                     "fvd/horse_attack.sprite", AnimationAction::ATTACK);
	TESTEQUALS(spawned.get_animation_started_at(), time::time_t::from_double(22.5));

	// The renderer must retain absolute host animation start times even
	// when it receives the first snapshot partway through the cycle.
	renderer::world::RenderEntity entity{};
	const auto at = [](double value) { return time::time_t::from_double(value); };
	entity.update_sample(77, coord::phys3{20, 20, 0},
	                     "fvd/orc_idle.sprite", at(5), std::nullopt,
	                     {}, at(1.25));
	{
		auto lock = entity.get_read_lock();
		const auto phase = entity.get_animation_paths().get_time(at(5));
		TESTEQUALS(phase.first, at(1.25));
		TESTEQUALS(phase.second, (std::vector<std::string>{"fvd/orc_idle.sprite"}));
	}
	entity.update_sample(77, coord::phys3{21, 20, 0},
	                     "fvd/orc_idle.sprite", at(5.25), at(5),
	                     {}, at(1.25));
	{
		auto lock = entity.get_read_lock();
		TESTEQUALS(entity.get_animation_paths().get_time(at(5.25)).first, at(1.25));
	}
	entity.update_sample(77, coord::phys3{22, 20, 0},
	                     "fvd/orc_walk.sprite", at(5.75), at(5.25),
	                     {}, at(5.5));
	{
		auto lock = entity.get_read_lock();
		const auto phase = entity.get_animation_paths().get_time(at(5.75));
		TESTEQUALS(phase.first, at(5.5));
		TESTEQUALS(phase.second, (std::vector<std::string>{"fvd/orc_walk.sprite"}));
	}
	entity.update_sample(77, coord::phys3{23, 20, 0},
	                     "fvd/orc_idle.sprite", at(6.5), std::nullopt,
	                     {}, at(6.25));
	{
		auto lock = entity.get_read_lock();
		TESTEQUALS(entity.get_animation_paths().get_time(at(6.5)).first, at(6.25));
	}
}

void fvd_build_line_order() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	std::optional<entity_id_t> builder{};
	for (const auto &[id, entity] : state->get_game_entities()) {
		auto owner = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (owner->get_owners().get(time::TIME_ZERO) == player_id_t{2}) {
			builder = id;
			break;
		}
	}
	builder.has_value() or TESTFAIL;
	event::SendCommandHandler command;
	const std::vector<entity_id_t> selected{*builder};
	auto submit_line = [&](const std::vector<coord::phys3> &line) {
		bool accepted = false;
		command.invoke(*loop, nullptr, state, time::TIME_ZERO,
		               {{"type", component::command::command_t::BUILD_TOWER},
		                {"owner", player_id_t{2}}, {"queue", true},
		                {"entity_ids", selected}, {"target", line.front()},
		                {"build_line", line},
		                {"result_cb", std::function<void(bool)>{[&](bool result) {
			                accepted = result;
		                }}}});
		return accepted;
	};
	const std::vector<coord::phys3> sites{
		{55.5, 71.5, 0}, {56.5, 71.5, 0}, {57.5, 71.5, 0}, {58.5, 71.5, 0}};
	TESTEQUALS(submit_line(sites), true);
	TESTEQUALS(fvd->get_build_order(*builder)->get_target(), (coord::tile{55, 71}));
	TESTEQUALS(fvd->get_queued_build_count(*builder), size_t{3});
	const auto first_world = network::capture_fvd_presentation_state(state, time::TIME_ZERO);
	const auto first_view = network::make_construction_presentation(first_world);
	TESTEQUALS(first_view.sites.size(), size_t{4});
	for (size_t i = 0; i < sites.size(); ++i) {
		const auto tile = sites[i].to_tile();
		auto found = std::find_if(first_view.sites.begin(), first_view.sites.end(),
		                          [tile](const auto &site) { return site.site == tile; });
		TESTEQUALS(found != first_view.sites.end(), true);
		TESTEQUALS(found->queue_position, static_cast<uint32_t>(i + 1));
		TESTEQUALS(found->phase, i == 0
		    ? network::SitePresentationPhase::PLANNED
		    : network::SitePresentationPhase::QUEUED);
	}
	// Invalid diagonal input must not mutate an existing build queue.
	TESTEQUALS(submit_line({{52.5, 78.5, 0}, {53.5, 79.5, 0}}), false);
	TESTEQUALS(fvd->get_build_order(*builder)->get_target(), (coord::tile{55, 71}));
	TESTEQUALS(fvd->get_queued_build_count(*builder), size_t{3});
	TESTEQUALS(fvd->cancel_build(state, *builder, time::TIME_ZERO), true);
	TESTEQUALS(submit_line({{61.5, 77.5, 0}, {60.5, 77.5, 0}, {59.5, 77.5, 0}}), true);
	TESTEQUALS(fvd->get_build_order(*builder)->get_target(), (coord::tile{61, 77}));
	TESTEQUALS(fvd->get_queued_build_count(*builder), size_t{2});
}

void fvd_retask_foundation() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	auto at = [](double seconds) { return time::time_t::from_double(seconds); };

	std::array<entity_id_t, 9> builders{};
	for (const auto &[id, entity] : state->get_game_entities()) {
		auto owner = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		builders.at(owner->get_owners().get(time::TIME_ZERO)) = id;
	}
	auto builder = builders.at(2);
	const std::vector<entity_id_t> selected{builder};
	TESTEQUALS(fvd->queue_build(state, 2, selected, {61, 66, 0}, at(0)), true);
	TESTEQUALS(fvd->queue_build(state, 2, selected, {57, 66, 0}, at(0)), true);
	TESTEQUALS(fvd->queue_build(state, 2, selected, {54, 66, 0}, at(0)), true);
	TESTEQUALS(fvd->get_queued_build_count(builder), size_t{2});

	auto foundations = [&]() {
		std::unordered_map<coord::tile, entity_id_t> ids;
		for (const auto &[id, entity] : state->get_game_entities()) {
			if (entity->get_definition() == "fvd.data.entities.Foundation") {
				auto position = std::dynamic_pointer_cast<component::Position>(
					entity->get_component(component::component_t::POSITION));
				ids[position->get_positions().get(at(0)).to_tile()] = id;
			}
		}
		return ids;
	};
	auto markers = foundations();
	TESTEQUALS(markers.size(), size_t{3});
	TESTEQUALS(markers.contains(coord::tile{61, 66}), true);
	TESTEQUALS(markers.contains(coord::tile{57, 66}), true);
	TESTEQUALS(markers.contains(coord::tile{54, 66}), true);
	auto chosen = markers.at(coord::tile{54, 66});
	TESTEQUALS(fvd->prioritize_foundation(state, 3, {builders[3]}, chosen, at(0)), false);
	TESTEQUALS(fvd->prioritize_foundation(state, 2, selected, entity_id_t{999999}, at(0)), false);

	// Use the same right-click Move command path as local game input.
	bool accepted = false;
	event::SendCommandHandler command;
	command.invoke(*loop, nullptr, state, at(0),
	               {{"type", component::command::command_t::MOVE},
	                {"owner", player_id_t{2}}, {"entity_ids", selected},
	                {"target", coord::phys3{54, 66, 0}},
	                {"target_entity", chosen},
	                {"result_cb", std::function<void(bool)>{[&accepted](bool result) {
		                accepted = result;
	                }}}});
	TESTEQUALS(accepted, true);
	TESTEQUALS(fvd->get_queued_build_count(builder), size_t{0});
	TESTEQUALS(fvd->get_build_order(builder)->get_target(), (coord::tile{54, 66}));
	markers = foundations();
	TESTEQUALS(markers.size(), size_t{3});
	TESTEQUALS(markers.at(coord::tile{54, 66}), chosen);
	TESTEQUALS(markers.contains(coord::tile{61, 66}), true);
	TESTEQUALS(markers.contains(coord::tile{57, 66}), true);
	TESTEQUALS(fvd->is_tile_reserved({61, 66}), false);
	TESTEQUALS(fvd->is_tile_reserved({57, 66}), false);
	TESTEQUALS(fvd->is_tile_reserved({54, 66}), true);

	// Clicking the active foundation retains its order ID and progress,
	// discarding the remaining queue without restarting construction.
	auto order_id = fvd->get_build_order(builder)->get_id();
	TESTEQUALS(fvd->queue_build(state, 2, selected, {48, 66, 0}, at(0)), true);
	TESTEQUALS(foundations().size(), size_t{4});
	TESTEQUALS(fvd->prioritize_foundation(state, 2, selected, chosen, at(0)), true);
	TESTEQUALS(fvd->get_build_order(builder)->get_id(), order_id);
	TESTEQUALS(fvd->get_queued_build_count(builder), size_t{0});
	TESTEQUALS(foundations().size(), size_t{4});
	TESTEQUALS(fvd->is_tile_reserved({48, 66}), false);

	loop->reach_time(at(35), state);
	TESTEQUALS(fvd->get_build_order(builder) == nullptr, true);
	TESTEQUALS(state->get_game_entities().contains(chosen), false);
	bool completed = false;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.Tower") {
			auto position = std::dynamic_pointer_cast<component::Position>(
				entity->get_component(component::component_t::POSITION));
			completed = completed
				or position->get_positions().get(at(35)).to_tile() == coord::tile{54, 66};
		}
	}
	TESTEQUALS(completed, true);

	// Ordinary movement abandons both active and queued work, preserving sites.
	TESTEQUALS(fvd->queue_build(state, 2, selected, {45, 66, 0}, at(36)), true);
	TESTEQUALS(fvd->queue_build(state, 2, selected, {42, 66, 0}, at(36)), true);
	TESTEQUALS(foundations().size(), size_t{5});
	command.invoke(*loop, nullptr, state, at(36),
	               {{"type", component::command::command_t::MOVE},
	                {"owner", player_id_t{2}}, {"entity_ids", selected},
	                {"target", coord::phys3{40, 70, 0}}});
	TESTEQUALS(foundations().size(), size_t{5});
	TESTEQUALS(fvd->get_queued_build_count(builder), size_t{0});
	const auto abandoned = network::make_construction_presentation(
		network::capture_fvd_presentation_state(state, at(36)));
	TESTEQUALS(std::count_if(abandoned.sites.begin(), abandoned.sites.end(), [](const auto &site) {
				   return site.phase == network::SitePresentationPhase::ORPHANED;
			   }), size_t{5});
	TESTEQUALS(fvd->is_tile_reserved({45, 66}), false);
	TESTEQUALS(fvd->is_tile_reserved({42, 66}), false);

	// A non-Shift placement replaces the entire queue, not the foundations.
	TESTEQUALS(fvd->request_build(state, 2, selected, {39, 66, 0}, at(36)), true);
	TESTEQUALS(fvd->queue_build(state, 2, selected, {36, 66, 0}, at(36)), true);
	TESTEQUALS(fvd->get_queued_build_count(builder), size_t{1});
	TESTEQUALS(fvd->request_build(state, 2, selected, {33, 66, 0}, at(36)), true);
	TESTEQUALS(fvd->get_queued_build_count(builder), size_t{0});
	TESTEQUALS(fvd->get_build_order(builder)->get_target(), (coord::tile{33, 66}));
	TESTEQUALS(fvd->is_tile_reserved({39, 66}), false);
	TESTEQUALS(fvd->is_tile_reserved({36, 66}), false);
	TESTEQUALS(foundations().contains(coord::tile{39, 66}), true);
	TESTEQUALS(foundations().contains(coord::tile{36, 66}), true);
}

void fvd_hunter_stances() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	auto at = [](double seconds) { return time::time_t::from_double(seconds); };
	auto spawn = [&](player_id_t owner, const char *definition, coord::phys3 position) {
		return entities->spawn_game_entity(
			loop, state, owner, std::string{"fvd.data.entities."} + definition, position, at(0));
	};
	auto position_of = [&](const std::shared_ptr<GameEntity> &entity, double seconds) {
		return std::dynamic_pointer_cast<component::Position>(
			entity->get_component(component::component_t::POSITION))->get_positions().get(at(seconds));
	};
	auto health_of = [&](const std::shared_ptr<GameEntity> &entity, double seconds) {
		return std::dynamic_pointer_cast<component::Live>(
			entity->get_component(component::component_t::LIVE))
		    ->get_attribute(at(seconds), "fvd.data.entities.Health")->get(at(seconds));
	};
	auto has_order = [&](const std::shared_ptr<GameEntity> &horse) {
		const auto orders = fvd->get_attack_order_state();
		return std::any_of(orders.begin(), orders.end(), [&](const auto &order) {
			return order.horse_id == horse->get_id();
		});
	};

	// Separate the scenarios so that no sight radius or chase overlaps another.
	auto observer = spawn(1, "Horse", {12, 12, 0});
	auto observed = spawn(2, "Builder", {14, 12, 0});
	auto aggressor = spawn(1, "Horse", {30, 12, 0});
	auto prey = spawn(2, "Builder", {36, 12, 0});
	auto far_sighted = spawn(1, "Horse", {30, 30, 0});
	auto distant = spawn(2, "Builder", {39, 30, 0});
	auto guard = spawn(1, "Horse", {12, 40, 0});
	auto outside = spawn(2, "Builder", {17, 40, 0});
	auto leashed = spawn(1, "Horse", {12, 90, 0});
	auto runner = spawn(2, "Builder", {15, 90, 0});
	auto arriving = spawn(1, "Horse", {80, 12, 0});
	auto awaited = spawn(2, "Builder", {89, 12, 0});
	auto arriving_observer = spawn(1, "Horse", {80, 40, 0});
	auto unnoticed = spawn(2, "Builder", {89, 40, 0});
	auto patroller = spawn(1, "Horse", {100, 100, 0});
	auto ambusher = spawn(1, "Horse", {100, 120, 0});
	auto ambushed = spawn(2, "Builder", {103, 122, 0});

	// Every horse starts in Observe, and only the Hunter may change a
	// stance for a complete, valid selection of its own horses.
	fvd->get_hunter_stance(aggressor->get_id()) == HunterStance::OBSERVE or TESTFAIL;
	TESTEQUALS(fvd->set_hunter_stance(
		state, 2, {aggressor->get_id()}, HunterStance::AGGRESSIVE, at(0)), false);
	TESTEQUALS(fvd->set_hunter_stance(
		state, 1, {aggressor->get_id(), prey->get_id()}, HunterStance::AGGRESSIVE, at(0)), false);
	TESTEQUALS(fvd->set_hunter_stance(
		state, 1, {aggressor->get_id(), aggressor->get_id()}, HunterStance::AGGRESSIVE, at(0)), false);
	fvd->get_hunter_stance(aggressor->get_id()) == HunterStance::OBSERVE or TESTFAIL;
	TESTEQUALS(fvd->set_hunter_stance(
		state, 1, {aggressor->get_id(), far_sighted->get_id(), arriving->get_id()},
		HunterStance::AGGRESSIVE, at(0)), true);
	fvd->get_hunter_stance(far_sighted->get_id()) == HunterStance::AGGRESSIVE or TESTFAIL;
	TESTEQUALS(fvd->set_hunter_stance(
		state, 1, {guard->get_id(), leashed->get_id()}, HunterStance::DEFENSIVE, at(0)), true);

	// An arrived AttackMove continues under a non-Observe stance, but keeps
	// its short alert radius under Observe.
	TESTEQUALS(fvd->issue_attack_move(state, 1, {arriving->get_id()}, {83, 12, 0}, at(0)), true);
	TESTEQUALS(fvd->issue_attack_move(
		state, 1, {arriving_observer->get_id()}, {83, 40, 0}, at(0)), true);

	// Patrol accepts only a valid Hunter order for reachable map positions.
	TESTEQUALS(fvd->issue_patrol(state, 2, {patroller->get_id()}, {106, 100, 0}, at(0)), false);
	TESTEQUALS(fvd->issue_patrol(state, 1, {patroller->get_id()}, {200, 100, 0}, at(0)), false);
	TESTEQUALS(fvd->issue_patrol(
		state, 1, {patroller->get_id(), observed->get_id()}, {106, 100, 0}, at(0)), false);
	TESTEQUALS(has_order(patroller), false);
	TESTEQUALS(fvd->issue_patrol(state, 1, {patroller->get_id()}, {106, 100, 0}, at(0)), true);
	TESTEQUALS(fvd->set_hunter_stance(
		state, 1, {ambusher->get_id()}, HunterStance::DEFENSIVE, at(0)), true);
	TESTEQUALS(fvd->issue_patrol(state, 1, {ambusher->get_id()}, {106, 120, 0}, at(0)), true);

	// Drive the authoritative 0.2-second combat tick and track each loop.
	double now = 0;
	double leashed_furthest = 0;
	bool patrol_out = false;
	bool patrol_back = false;
	bool patrol_out_again = false;
	bool ambush_resumed = false;
	auto run_until = [&](double until) {
		while (now + 0.1 < until) {
			now += 0.2;
			loop->reach_time(at(now), state);
			fvd->combat_tick(state, at(now));
			leashed_furthest = std::max(
				leashed_furthest, (position_of(leashed, now) - coord::phys3{12, 90, 0}).length());
			const auto patrol_ne = position_of(patroller, now).ne.to_double();
			patrol_out = patrol_out or patrol_ne >= 105.8;
			patrol_back = patrol_back or (patrol_out and patrol_ne <= 100.2);
			patrol_out_again = patrol_out_again or (patrol_back and patrol_ne >= 102.0);
			ambush_resumed = ambush_resumed
			              or (not state->get_game_entities().contains(ambushed->get_id())
			                  and position_of(ambusher, now).ne.to_double() >= 105.8);
		}
	};

	run_until(1.0);
	// Observe never starts a fight, even with an enemy two tiles away.
	TESTEQUALS(has_order(observer), false);
	TESTEQUALS(health_of(observed, now), int64_t{25});
	// Aggressive acts on anything its seven-tile vision reveals; nine is out of sight.
	TESTEQUALS(has_order(aggressor), true);
	TESTEQUALS(position_of(aggressor, now).ne.to_double() > 30.0, true);
	TESTEQUALS(has_order(far_sighted), false);
	TESTEQUALS(position_of(far_sighted, now) == (coord::phys3{30, 30, 0}), true);
	// Defensive ignores an enemy just beyond its firing range.
	TESTEQUALS(has_order(guard), false);
	TESTEQUALS(position_of(guard, now) == (coord::phys3{12, 40, 0}), true);
	TESTEQUALS(has_order(leashed), true);

	// Once struck, the Defensive horse's target flees beyond its leash.
	TESTEQUALS(health_of(runner, now) < 25, true);
	system::Move::move_default(runner, state, {32, 90, 0}, at(now));

	run_until(4.0);
	const auto prey_hp = health_of(prey, now);
	TESTEQUALS(prey_hp < 25, true);
	// Switching to Observe ends the stance's engagement before its next hit.
	TESTEQUALS(fvd->set_hunter_stance(
		state, 1, {aggressor->get_id()}, HunterStance::OBSERVE, at(now)), true);
	TESTEQUALS(has_order(aggressor), false);

	run_until(10.0);
	TESTEQUALS(health_of(prey, now), prey_hp);
	TESTEQUALS(has_order(aggressor), false);
	// The leashed horse chased, gave up and returned to its post, idle.
	TESTEQUALS(leashed_furthest > 0.25, true);
	TESTEQUALS((position_of(leashed, now) - coord::phys3{12, 90, 0}).length() < 0.2, true);
	TESTEQUALS(has_order(leashed), false);
	TESTEQUALS(state->get_game_entities().contains(runner->get_id()), true);
	// The arrived Aggressive horse found an enemy six tiles from its
	// destination; the Observe horse kept waiting at its own.
	TESTEQUALS(health_of(awaited, now) < 25, true);
	TESTEQUALS(health_of(unnoticed, now), int64_t{25});
	TESTEQUALS(has_order(arriving_observer), true);
	TESTEQUALS((position_of(arriving_observer, now) - coord::phys3{83, 40, 0}).length() < 0.2, true);
	// Patrol loops between its start and the clicked point.
	TESTEQUALS(patrol_out and patrol_back and patrol_out_again, true);
	TESTEQUALS(has_order(patroller), true);

	// A Defensive patrol fights an enemy beside its route, then resumes.
	run_until(16.0);
	TESTEQUALS(state->get_game_entities().contains(ambushed->get_id()), false);
	TESTEQUALS(ambush_resumed, true);
	TESTEQUALS(has_order(ambusher), true);
}

void fvd_hunter_patrol_stances() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	auto at = [](double seconds) { return time::time_t::from_double(seconds); };
	auto spawn = [&](player_id_t owner, const char *definition, coord::phys3 position) {
		return entities->spawn_game_entity(
			loop, state, owner, std::string{"fvd.data.entities."} + definition, position, at(0));
	};
	auto position_of = [&](const std::shared_ptr<GameEntity> &entity, double seconds) {
		return std::dynamic_pointer_cast<component::Position>(
			entity->get_component(component::component_t::POSITION))->get_positions().get(at(seconds));
	};
	auto health_of = [&](const std::shared_ptr<GameEntity> &entity, double seconds) {
		return std::dynamic_pointer_cast<component::Live>(
			entity->get_component(component::component_t::LIVE))
		    ->get_attribute(at(seconds), "fvd.data.entities.Health")->get(at(seconds));
	};
	auto has_order = [&](const std::shared_ptr<GameEntity> &horse) {
		const auto orders = fvd->get_attack_order_state();
		return std::any_of(orders.begin(), orders.end(), [&](const auto &order) {
			return order.horse_id == horse->get_id();
		});
	};

	// Three patrols far enough apart that no sight radius overlaps another.
	auto watcher = spawn(1, "Horse", {20, 20, 0});
	auto ignored = spawn(2, "Builder", {23, 22, 0});
	auto guard = spawn(1, "Horse", {20, 60, 0});
	auto fleeing = spawn(2, "Builder", {23, 63, 0});
	auto hunter = spawn(1, "Horse", {20, 100, 0});
	auto quarry = spawn(2, "Builder", {23, 106, 0});
	TESTEQUALS(fvd->set_hunter_stance(state, 1, {guard->get_id()}, HunterStance::DEFENSIVE, at(0)), true);
	TESTEQUALS(fvd->set_hunter_stance(state, 1, {hunter->get_id()}, HunterStance::AGGRESSIVE, at(0)), true);
	TESTEQUALS(fvd->issue_patrol(state, 1, {watcher->get_id()}, {26, 20, 0}, at(0)), true);
	TESTEQUALS(fvd->issue_patrol(state, 1, {guard->get_id()}, {26, 60, 0}, at(0)), true);
	TESTEQUALS(fvd->issue_patrol(state, 1, {hunter->get_id()}, {26, 100, 0}, at(0)), true);

	double now = 0;
	double guard_furthest = 0;
	bool guard_resumed = false;
	auto run_until = [&](double until, const std::function<void()> &each_tick = {}) {
		while (now + 0.1 < until) {
			now += 0.2;
			loop->reach_time(at(now), state);
			fvd->combat_tick(state, at(now));
			const auto guard_at = position_of(guard, now);
			guard_furthest = std::max(guard_furthest, std::abs(guard_at.se.to_double() - 60.0));
			guard_resumed = guard_resumed
			             or (guard_furthest > 1.0 and std::abs(guard_at.se.to_double() - 60.0) < 0.2
			                 and guard_at.ne.to_double() >= 25.8);
			if (each_tick) {
				each_tick();
			}
		}
	};

	// The Aggressive patrol sees a Builder six tiles off its route and
	// leaves the route for it; a Defensive one would ignore it.
	run_until(1.0);
	TESTEQUALS(has_order(hunter), true);
	TESTEQUALS(position_of(hunter, now).se.to_double() > 100.5, true);

	// Its quarry escapes beyond sight, so it gives up but keeps patrolling.
	std::dynamic_pointer_cast<component::Position>(
		quarry->get_component(component::component_t::POSITION))->set_position(at(now), {23, 125, 0});
	run_until(1.4);
	TESTEQUALS(has_order(hunter), true);
	fvd->get_hunter_stance(hunter->get_id()) == HunterStance::AGGRESSIVE or TESTFAIL;
	TESTEQUALS(fvd->get_attack_order_state().empty(), false);
	for (const auto &order : fvd->get_attack_order_state()) {
		if (order.horse_id == hunter->get_id()) {
			TESTEQUALS(order.target_id.has_value(), false);
		}
	}

	// The Defensive patrol has engaged the Builder near its route by now;
	// the Builder runs, and the horse chases until the six-tile leash.
	TESTEQUALS(health_of(fleeing, now) < 25, true);
	system::Move::move_default(fleeing, state, {23, 80, 0}, at(now));

	run_until(16.0);
	// Observe on patrol never attacks, even two tiles from its route.
	TESTEQUALS(health_of(ignored, now), int64_t{25});
	TESTEQUALS(has_order(watcher), true);
	// The Defensive horse chased, gave up within the stationary six-tile
	// leash and went back to patrolling the original route; its target escaped.
	TESTEQUALS(guard_furthest > 1.0 and guard_furthest < 7.0, true);
	TESTEQUALS(guard_resumed, true);
	TESTEQUALS(has_order(guard), true);
	TESTEQUALS(state->get_game_entities().contains(fleeing->get_id()), true);
	// The Aggressive horse went back to its route and is still patrolling;
	// it never caught its quarry.
	TESTEQUALS(std::abs(position_of(hunter, now).se.to_double() - 100.0) < 0.5, true);
	TESTEQUALS(has_order(hunter), true);
	TESTEQUALS(health_of(quarry, now), int64_t{25});
}

void fvd_fog_of_war() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	tests::use_centre_test_layout(state);
	auto at = [](double seconds) { return time::time_t::from_double(seconds); };
	auto spawn = [&](player_id_t owner, const char *definition, coord::phys3 position, double seconds) {
		return entities->spawn_game_entity(
			loop, state, owner, std::string{"fvd.data.entities."} + definition, position, at(seconds));
	};
	auto teleport = [&](const std::shared_ptr<GameEntity> &entity, coord::phys3 position, double seconds) {
		std::dynamic_pointer_cast<component::Position>(
			entity->get_component(component::component_t::POSITION))->set_position(at(seconds), position);
	};
	auto has_order = [&](const std::shared_ptr<GameEntity> &horse) {
		const auto orders = fvd->get_attack_order_state();
		return std::any_of(orders.begin(), orders.end(), [&](const auto &order) {
			return order.horse_id == horse->get_id();
		});
	};

	TESTEQUALS(FVD::team_of(1) == FVDTeam::HUNTER, true);
	TESTEQUALS(FVD::team_of(2) == FVDTeam::BUILDERS, true);
	TESTEQUALS(FVD::team_of(8) == FVDTeam::BUILDERS, true);
	TESTEQUALS(fvd->tile_visible(state, FVDTeam::BUILDERS, {-1, 4}, at(0)), false);

	// Vision ranges come from each type's LineOfSight: Builders see six
	// tiles, horses seven. Distances below are to tile centres.
	auto scout = spawn(2, "Builder", {10.5, 10.5, 0}, 0);
	auto near_horse = spawn(1, "Horse", {15.5, 10.5, 0}, 0);
	auto far_horse = spawn(1, "Horse", {17.5, 10.5, 0}, 0);
	auto hidden_builder = spawn(3, "Builder", {30.5, 10.5, 0}, 0);
	TESTEQUALS(fvd->can_see(state, FVDTeam::BUILDERS, near_horse->get_id(), at(0)), true);
	TESTEQUALS(fvd->can_see(state, FVDTeam::BUILDERS, far_horse->get_id(), at(0)), false);
	TESTEQUALS(fvd->can_see(state, FVDTeam::HUNTER, scout->get_id(), at(0)), true);
	TESTEQUALS(fvd->can_see(state, FVDTeam::HUNTER, hidden_builder->get_id(), at(0)), false);
	// A side always sees its own units, wherever they are.
	TESTEQUALS(fvd->can_see(state, FVDTeam::BUILDERS, hidden_builder->get_id(), at(0)), true);
	TESTEQUALS(fvd->can_see(state, FVDTeam::HUNTER, far_horse->get_id(), at(0)), true);

	// Nothing hidden may be targeted.
	TESTEQUALS(fvd->issue_focus_attack(state, 1, {near_horse->get_id()},
	                                   hidden_builder->get_id(), at(0)), false);
	TESTEQUALS(has_order(near_horse), false);
	TESTEQUALS(fvd->issue_focus_attack(state, 1, {near_horse->get_id()}, scout->get_id(), at(0)), true);
	fvd->update_vision(state, at(0));
	auto builders_view = fvd->get_vision(FVDTeam::BUILDERS);
	TESTEQUALS(network::fog_tile_set(builders_view.visible, 128, {8, 8}), true);
	TESTEQUALS(network::fog_tile_set(builders_view.explored, 128, {8, 8}), true);

	// The scout escapes beyond every horse's sight: the focus attack ends,
	// and the Builders keep the ground they explored, now unseen.
	teleport(scout, {10.5, 60.5, 0}, 0.1);
	fvd->combat_tick(state, at(0.2));
	TESTEQUALS(has_order(near_horse), false);
	builders_view = fvd->get_vision(FVDTeam::BUILDERS);
	TESTEQUALS(network::fog_tile_set(builders_view.visible, 128, {8, 8}), false);
	TESTEQUALS(network::fog_tile_set(builders_view.explored, 128, {8, 8}), true);
	TESTEQUALS(network::fog_tile_set(builders_view.explored, 128, {120, 120}), false);

	// The Hunter remembers an enemy tower it has seen until it sees the
	// tower's site again without it.
	entity_id_t builder = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (entity->get_definition() == "fvd.data.entities.Builder"
		    and ownership->get_owners().get(time::TIME_ZERO) == player_id_t{5}) {
			builder = id;
		}
	}
	TESTEQUALS(fvd->request_build(state, 5, {builder}, {68, 69, 0}, at(1)), true);
	loop->reach_time(at(15), state);
	entity_id_t tower = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.Tower") {
			tower = id;
		}
	}
	TESTEQUALS(tower != 0, true);
	auto remembers_tower = [&]() {
		const auto view = fvd->get_vision(FVDTeam::HUNTER);
		return std::any_of(view.remembered.begin(), view.remembered.end(), [&](const auto &building) {
			return building.entity_id == tower;
		});
	};
	auto watcher = spawn(1, "Horse", {72.5, 69.5, 0}, 15);
	fvd->update_vision(state, at(15));
	TESTEQUALS(remembers_tower(), true);
	teleport(watcher, {110.5, 110.5, 0}, 15.1);
	fvd->update_vision(state, at(15.2));
	TESTEQUALS(remembers_tower(), true);

	// Each recipient receives only its own side's view.
	network::MatchConnectionState connections{fvd->get_player_count()};
	auto hunter_world = network::decode_world_state(network::encode_world_state(
		network::capture_fvd_state(state, connections, at(15.2), 0, 1, 1)));
	TESTEQUALS(hunter_world.fog.size(), size_t{1});
	(hunter_world.fog.front().team == network::PlayerRole::HUNTER) or TESTFAIL;
	auto sent_tower = std::find_if(hunter_world.entities.begin(), hunter_world.entities.end(),
	                               [&](const auto &entity) { return entity.entity_id == tower; });
	sent_tower != hunter_world.entities.end() or TESTFAIL;
	TESTEQUALS(sent_tower->remembered, true);
	TESTEQUALS(sent_tower->health.has_value(), false);
	auto sent_hidden = std::find_if(hunter_world.entities.begin(), hunter_world.entities.end(),
	                                [&](const auto &entity) { return entity.entity_id == hidden_builder->get_id(); });
	TESTEQUALS(sent_hidden == hunter_world.entities.end(), true);
	TESTEQUALS(fvd->issue_attack_move(state, 1, {watcher->get_id()}, {112, 110, 0}, at(15.2)), true);
	auto builder_world = network::decode_world_state(network::encode_world_state(
		network::capture_fvd_state(state, connections, at(15.2), 0, 1, 2)));
	(builder_world.fog.front().team == network::PlayerRole::BUILDER) or TESTFAIL;
	TESTEQUALS(builder_world.attack_orders.empty(), true);
	auto sent_watcher = std::find_if(builder_world.entities.begin(), builder_world.entities.end(),
	                                 [&](const auto &entity) { return entity.entity_id == watcher->get_id(); });
	TESTEQUALS(sent_watcher == builder_world.entities.end(), true);

	// Destroyed while unseen, the tower stays remembered until seen gone.
	TESTEQUALS(fvd->destroy_tower(state, tower, at(15.3)), true);
	fvd->update_vision(state, at(15.4));
	TESTEQUALS(remembers_tower(), true);
	fvd->cancel_attack_move(watcher->get_id());
	teleport(watcher, {72.5, 69.5, 0}, 15.5);
	fvd->update_vision(state, at(15.6));
	TESTEQUALS(remembers_tower(), false);
}

void fvd_hunter_tower() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	auto at = [](double seconds) { return time::time_t::from_double(seconds); };
	auto alive = [&](entity_id_t id) { return state->get_game_entities().contains(id); };
	auto spawn = [&](player_id_t owner, const char *definition, coord::phys3 position, double seconds) {
		return entities->spawn_game_entity(
			loop, state, owner, std::string{"fvd.data.entities."} + definition, position, at(seconds));
	};
	auto position_of_entity = [&](entity_id_t id, double seconds) {
		return std::dynamic_pointer_cast<component::Position>(
			state->get_game_entity(id)->get_component(component::component_t::POSITION))
		    ->get_positions().get(at(seconds));
	};
	TESTEQUALS(fvd->get_hunter_tower().has_value(), true);
	const auto central = *fvd->get_hunter_tower();
	TESTEQUALS(alive(central), true);


	// The first horse, and the tower's first shot, are due 20 seconds in.
	fvd->start_hunter_wave(state, time::TIME_ZERO);
	const auto centre = position_of_entity(central, 0);

	// A Builder tower well inside range, which the Hunter tower must ignore.
	auto mason = spawn(2, "Builder", {72.5, 66.5, 0}, 0);
	TESTEQUALS(fvd->request_build(state, 2, {mason->get_id()}, {72, 64, 0}, at(0)), true);
	loop->reach_time(at(10), state);
	entity_id_t tower = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.Tower") {
			tower = id;
		}
	}
	TESTEQUALS(tower != 0, true);
	std::dynamic_pointer_cast<component::Position>(
		mason->get_component(component::component_t::POSITION))->set_position(at(10), {110.5, 110.5, 0});

	// Builders six tiles away, just inside the 10-tile range and just outside it.
	auto near = spawn(3, "Builder", centre + coord::phys3_delta{-6, 0, 0}, 10);
	auto edge = spawn(4, "Builder", centre + coord::phys3_delta{0, 9.9, 0}, 10);
	auto outside = spawn(5, "Builder", centre + coord::phys3_delta{0, -10.1, 0}, 10);
	// Enough health to count individual 100-damage hits.
	auto near_live = std::dynamic_pointer_cast<component::Live>(
		near->get_component(component::component_t::LIVE));
	near_live->set_attribute(at(10), "fvd.data.entities.Health", 200);
	auto near_health = [&](double seconds) {
		return near_live->get_attribute(at(seconds), "fvd.data.entities.Health")->get(at(seconds));
	};
	auto hunter_tower_shots = [&](double seconds) {
		size_t shots = 0;
		for (const auto &shot : fvd->get_arrow_state(at(seconds))) {
			shots += shot.owner == player_id_t{1} and shot.start.up.to_double() > 1.0;
		}
		return shots;
	};
	const auto kills = fvd->get_kills(3);

	// Silent through the Builders' head start, even with targets in range.
	loop->reach_time(at(19.999), state);
	TESTEQUALS(hunter_tower_shots(19.999), size_t{0});
	TESTEQUALS(near_health(19.999), int64_t{200});
	TESTEQUALS(alive(edge->get_id()) and alive(outside->get_id()), true);

	// From 20 seconds it fires once a second at the nearest Builder unit,
	// each hit taking exactly 100 health.
	loop->reach_time(at(20.5), state);
	TESTEQUALS(hunter_tower_shots(20.5), size_t{1});
	TESTEQUALS(near_health(20.5), int64_t{100});
	loop->reach_time(at(20.9), state);
	TESTEQUALS(near_health(20.9), int64_t{100});
	// Combat ticks every 0.2 seconds, so later shots may land a tick late.
	loop->reach_time(at(21.9), state);
	TESTEQUALS(alive(near->get_id()), false);
	TESTEQUALS(alive(edge->get_id()), true);
	loop->reach_time(at(23.1), state);
	TESTEQUALS(alive(edge->get_id()), false);
	loop->reach_time(at(25), state);
	// Beyond 10 tiles is safe, towers are never targeted, and the Hunter
	// tower earns no kill credit.
	TESTEQUALS(alive(outside->get_id()), true);
	TESTEQUALS(alive(tower), true);
	TESTEQUALS(fvd->get_kills(3), kills);

	// Nothing can damage it: horses never target their own side's tower.
	auto horse = spawn(1, "Horse", {70.5, 64.5, 0}, 25);
	TESTEQUALS(fvd->issue_focus_attack(state, 1, {horse->get_id()}, central, at(25)), false);
	// Builders cannot demolish it either.
	for (player_id_t builder_player = 2; builder_player <= 3; ++builder_player) {
		TESTEQUALS(fvd->demolish_owned_tower(state, builder_player, {central}, at(25)), false);
	}
	TESTEQUALS(alive(central), true);
	auto health = std::dynamic_pointer_cast<component::Live>(
		state->get_game_entity(central)->get_component(component::component_t::LIVE));
	TESTEQUALS(health->get_attribute(at(25), "fvd.data.entities.Health")->get(at(25)), int64_t{1000});
	// It gives the Hunter vision across its 15-tile reach and no further.
	TESTEQUALS(fvd->tile_visible(state, FVDTeam::HUNTER, {64, 78}, at(25)), true);
	TESTEQUALS(fvd->tile_visible(state, FVDTeam::HUNTER, {64, 81}, at(25)), false);

	// Horses appear on the free tile beside the tower, which is solid.
	const auto spawn_point = fvd->get_horse_spawn_point();
	TESTEQUALS(spawn_point, (coord::phys3{64.5, 65.5, 0}));
	auto grid = state->get_map()->get_pathfinder()->get_grid(
		state->get_map()->get_grid_id("fvd.data.terrain.Land"));
	auto cost_at = [&](coord::tile tile) {
		auto sector = grid->get_sector(tile.ne / grid->get_sector_size(), tile.se / grid->get_sector_size());
		return sector->get_cost_field()->get_cost(tile - sector->get_position().to_tile(grid->get_sector_size()));
	};
	TESTEQUALS(cost_at({64, 64}), path::COST_IMPASSABLE);
	TESTEQUALS(cost_at(spawn_point.to_tile()) < path::COST_IMPASSABLE, true);
	// The first horse appeared on that tile at 20 seconds.
	bool spawned_beside = false;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.Horse") {
			auto position = std::dynamic_pointer_cast<component::Position>(
				entity->get_component(component::component_t::POSITION))->get_positions().get(at(20));
			spawned_beside = spawned_beside or position.to_tile() == spawn_point.to_tile();
		}
	}
	TESTEQUALS(spawned_beside, true);
}

void fvd_hunter_rally_point() {
	auto root = util::Path{std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets" / "converted");
	mods->register_modpack(root / "assets" / "converted" / "engine" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<openage::event::EventLoop>();
	loop->add_event_handler(std::make_shared<event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<event::WaitHandler>());
	auto entities = std::make_shared<EntityFactory>();
	auto terrain = std::make_shared<TerrainFactory>();
	Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	auto fvd = state->get_fvd();
	auto at = [](double seconds) { return time::time_t::from_double(seconds); };
	auto alive = [&](entity_id_t id) { return state->get_game_entities().contains(id); };
	auto spawn = [&](player_id_t owner, const char *definition, coord::phys3 position, double seconds) {
		return entities->spawn_game_entity(
			loop, state, owner, std::string{"fvd.data.entities."} + definition, position, at(seconds));
	};
	auto position_of = [&](entity_id_t id, double seconds) {
		return std::dynamic_pointer_cast<component::Position>(
			state->get_game_entity(id)->get_component(component::component_t::POSITION))
		    ->get_positions().get(at(seconds));
	};
	auto markers = [&]() {
		size_t count = 0;
		for (const auto &[id, entity] : state->get_game_entities()) {
			count += entity->get_definition() == fvd->get_hunter_rally_marker_definition();
		}
		return count;
	};
	const auto central = *fvd->get_hunter_tower();
	auto horse = spawn(1, "Horse", {70.5, 70.5, 0}, 0);
	const coord::phys3 rally{80.5, 64.5, 0};

	// Only the Hunter may set it, through its tower alone, on reachable ground.
	TESTEQUALS(fvd->set_hunter_rally_point(state, 2, {central}, rally, at(0)), false);
	TESTEQUALS(fvd->set_hunter_rally_point(state, 1, {horse->get_id()}, rally, at(0)), false);
	TESTEQUALS(fvd->set_hunter_rally_point(state, 1, {central, horse->get_id()}, rally, at(0)), false);
	TESTEQUALS(fvd->set_hunter_rally_point(state, 1, {central}, {200, 5, 0}, at(0)), false);
	TESTEQUALS(fvd->set_hunter_rally_point(state, 1, {central}, {80.5, 64.5, 1}, at(0)), false);
	TESTEQUALS(fvd->get_hunter_rally_point().has_value(), false);
	TESTEQUALS(markers(), size_t{0});

	// Moving it leaves one marker, in the new place. It cannot be selected.
	TESTEQUALS(fvd->set_hunter_rally_point(state, 1, {central}, {50.5, 64.5, 0}, at(0)), true);
	const auto first_marker = *fvd->get_hunter_rally_marker();
	TESTEQUALS(fvd->set_hunter_rally_point(state, 1, {central}, rally, at(1)), true);
	TESTEQUALS(alive(first_marker), false);
	const auto marker = *fvd->get_hunter_rally_marker();
	TESTEQUALS(markers(), size_t{1});
	TESTEQUALS(position_of(marker, 1), rally);
	TESTEQUALS(*fvd->get_hunter_rally_point(), rally);
	TESTEQUALS(state->get_game_entity(marker)->has_component(component::component_t::SELECTABLE), false);

	// A box selection that also caught the tower still orders the horses;
	// the tower on its own cannot be ordered about.
	TESTEQUALS(fvd->can_command_units(state, 1, {horse->get_id(), central}, at(1)), true);
	TESTEQUALS(fvd->can_command_units(state, 1, {central}, at(1)), false);
	TESTEQUALS(fvd->issue_attack_move(state, 1, {central, horse->get_id()}, {70.5, 80.5, 0}, at(1)), true);

	// New horses ride from beside the tower to the rally point.
	fvd->start_hunter_wave(state, at(1));
	loop->reach_time(at(21), state);
	entity_id_t first_spawn = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.Horse" and id != horse->get_id()
		    and (first_spawn == 0 or id < first_spawn)) {
			first_spawn = id;
		}
	}
	TESTEQUALS(first_spawn != 0, true);
	TESTEQUALS((position_of(first_spawn, 21) - fvd->get_horse_spawn_point()).length() < 1.0, true);
	loop->reach_time(at(40), state);
	TESTEQUALS((position_of(first_spawn, 40) - rally).length() < 2.5, true);

	// Only the Hunter is sent the marker, even with a Builder standing beside it.
	auto scout = spawn(2, "Builder", rally + coord::phys3_delta{1, 1, 0}, 40);
	fvd->update_vision(state, at(40));
	TESTEQUALS(fvd->can_see(state, FVDTeam::BUILDERS, marker, at(40)), true);
	network::MatchConnectionState connections{fvd->get_player_count()};
	auto sent_marker = [&](player_id_t recipient) {
		auto world = network::capture_fvd_state(state, connections, at(40), 0, 1, recipient);
		return std::any_of(world.entities.begin(), world.entities.end(),
		                   [&](const auto &entity) { return entity.entity_id == marker; });
	};
	TESTEQUALS(sent_marker(1), true);
	TESTEQUALS(sent_marker(2), false);
	TESTEQUALS(alive(scout->get_id()), true);

	// Builders wall in the centre. A new horse cannot reach the rally point,
	// so it rides as close as it can get rather than standing at the spawn.
	entity_id_t newest_before = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		newest_before = std::max(newest_before, id);
	}
	const nyan::fqon_t land{"fvd.data.terrain.Land"};
	for (coord::tile_t ne = 59; ne <= 69; ++ne) {
		for (coord::tile_t se = 59; se <= 70; ++se) {
			if (ne == 59 or ne == 69 or se == 59 or se == 70) {
				state->get_map()->set_path_cost(land, {ne, se}, path::COST_IMPASSABLE, at(40.5));
			}
		}
	}
	state->get_map()->rebuild_path_grid(land);
	loop->reach_time(at(47), state);
	entity_id_t walled_in = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == "fvd.data.entities.Horse" and id > newest_before
		    and (walled_in == 0 or id < walled_in)) {
			walled_in = id;
		}
	}
	TESTEQUALS(walled_in != 0, true);
	const auto stopped = position_of(walled_in, 47);
	// The reachable tile nearest the rally point is (68, 64), against the wall.
	TESTEQUALS(stopped.ne.to_double() > 67.0 and stopped.ne.to_double() < 69.0, true);
	TESTEQUALS(std::abs(stopped.se.to_double() - 64.5) < 1.5, true);
	// Ordering it out of the walled-in centre is refused, not a crash: the
	// pathfinder used to throw when no portal route reached the target.
	TESTEQUALS(fvd->issue_attack_move(state, 1, {walled_in}, {100.5, 100.5, 0}, at(47)), false);
	// Setting a rally point behind the wall is allowed, as in AoE2.
	TESTEQUALS(fvd->set_hunter_rally_point(state, 1, {central}, {90.5, 90.5, 0}, at(47)), true);
}
}
