// Copyright 2026 the openage authors. See copying.md for legal info.
// Native-only C++ test dispatcher: no Python, Cython or generated test list.
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

#include "config.h"
#include "error/error.h"
#include "log/log.h"
#include "testing/native_test_runner.h"
#include "testing/testing.h"

namespace openage::cvar::tests { void native_config(); }
namespace openage::coord::tests { void coord(); }
namespace openage::datastructure::tests { void concurrent_queue(); void constexpr_map(); void pairing_heap(); }
namespace openage::job::tests { void test_job_manager(); }
namespace openage::network::tests { void command_params(); void entity_kind(); void messages(); void stance_messages(); void fog_messages(); void protocol(); void handshake(); void lobby(); void lobby_rematch_room_visibility(); void lobby_relay_client(); void lobby_latency(); void lobby_capacity(); void authority_succession(); void session(); void rehost_ticket(); void command_mailbox(); void connection_mailbox(); void match_disconnect_coordinator(); void replication(); void world_delta(); void world_delta_flares(); void transport(); void transport_high_fd(); void fvd_loopback(); void fvd_multi_session(); void fvd_process_worker(); void relay(); void relay_registry(); void relay_multi_host_lobby(); void relay_service(); void relay_process_worker(); }
namespace openage::path::tests { void path_node(); void flow_field(); }
namespace openage::renderer::tests { void font(); void font_manager(); }
#if WITH_OPENGL
namespace openage::renderer::opengl::tests { void uniform_buffer_padding(); }
#endif
namespace openage::renderer::resources::tests { void alpha_mask(); void texture_metadata(); void palette_colors(); }
namespace openage::rng::tests { void run(); }
namespace openage::util::tests { void constinit_vector(); void enum_(); void fixed_point(); void init(); void matrix(); void quaternion(); void vector(); void siphash(); void array_conversion(); }
namespace openage::curve::tests { void container(); void curve_types(); }
namespace openage::event::tests { void eventtrigger(); }
namespace openage::testing::tests { void comparison(); }
namespace openage::input::game::tests { void command_dispatcher(); void tower_line_axis_lock(); void tower_placement_mode(); void horse_selection_priority(); void tower_click_target(); }
namespace openage::presenter::tests { void hud_mouse_capture(); void fvd_combat_sounds(); void fvd_minimap(); void fvd_hunter_unleashed(); void fvd_construction_sounds(); void guest_interpolation(); void guest_clock_jitter(); void guest_response_latency(); void guest_clock_catchup(); }
namespace openage::audio::tests { void audio_settings(); void audio_mixer_effects(); void audio_mixer_volume(); void audio_mixer_music(); void audio_decode_wav(); void fvd_audio_files(); void fvd_audio_placement(); void fvd_audio_throttle(); void fvd_audio_bank(); }
namespace openage::gamestate::tests { void fvd_network_builder_count(); void fvd_scenario(); void fvd_construction(); void fvd_presentation_snapshot(); void fvd_foundation_push_and_timeout(); void fvd_build_queue(); void fvd_build_line_order(); void fvd_match_end_garrison_and_hunter_stop(); void fvd_builder_flare(); void fvd_builder_delete_all_foundations(); void fvd_build_double_row(); void fvd_animation_phase(); void fvd_retask_foundation(); void fvd_queue_existing_foundations(); void fvd_builder_demolition(); void fvd_builder_delete_foundation(); void fvd_match_outcome_rules(); void fvd_garrison(); void fvd_allied_garrison(); void fvd_garrison_hop(); void fvd_garrison_all_sides(); void fvd_hunter_spawning(); void fvd_horse_spawn_blocked(); void fvd_hunter_combat(); void fvd_combat_balance(); void fvd_hunter_attack_move_regressions(); void fvd_hunter_builder_aggro(); void fvd_hunter_focus_attack(); void fvd_first_builder_attack_order(); void fvd_hunter_stances(); void fvd_hunter_patrol_stances(); void fvd_fog_of_war(); void fvd_hunter_tower(); void fvd_hunter_rally_point(); void activity_demo(); }
namespace openage::renderer::world::tests { void structure_shadow_depth(); void adjacent_tower_draw_order(); void terrain_fog_soft_edges(); void selection_ground_rings(); void foundation_picking(); void foundation_mouse_input(); }
namespace openage::console::tests { void render(); void interactive(); }
namespace openage::error { void demo(); }
namespace openage::input::tests { void action_demo(); }
namespace openage::log::tests { void demo(); }
namespace openage::test { void benchmark(); }

namespace {
struct Test {
	std::string_view name;
	void (*function)();
	bool requires_assets;
	bool opt_in;
};

// All retained C++ tests, demos and benchmarks live in this C++ table.
// No Python testlist, Cython bridge, dispatcher or generated C++ source.
// Asset-bound and explicit worker/slow/interactive tests are opt-in.
constexpr Test native_test_entries[] = {
	{"openage::cvar::tests::native_config", &openage::cvar::tests::native_config, false, false},
	{"openage::coord::tests::coord", &openage::coord::tests::coord, false, false},
	{"openage::datastructure::tests::concurrent_queue", &openage::datastructure::tests::concurrent_queue, false, false},
	{"openage::datastructure::tests::constexpr_map", &openage::datastructure::tests::constexpr_map, false, false},
	{"openage::datastructure::tests::pairing_heap", &openage::datastructure::tests::pairing_heap, false, false},
	{"openage::job::tests::test_job_manager", &openage::job::tests::test_job_manager, false, false},
	{"openage::network::tests::command_params", &openage::network::tests::command_params, false, false},
	{"openage::network::tests::entity_kind", &openage::network::tests::entity_kind, false, false},
	{"openage::network::tests::messages", &openage::network::tests::messages, false, false},
	{"openage::network::tests::stance_messages", &openage::network::tests::stance_messages, false, false},
	{"openage::network::tests::fog_messages", &openage::network::tests::fog_messages, false, false},
	{"openage::network::tests::protocol", &openage::network::tests::protocol, false, false},
	{"openage::network::tests::handshake", &openage::network::tests::handshake, false, false},
	{"openage::network::tests::lobby", &openage::network::tests::lobby, false, false},
	{"openage::network::tests::lobby_rematch_room_visibility", &openage::network::tests::lobby_rematch_room_visibility, false, false},
	{"openage::network::tests::lobby_relay_client", &openage::network::tests::lobby_relay_client, false, false},
	{"openage::network::tests::lobby_latency", &openage::network::tests::lobby_latency, false, true},
	{"openage::network::tests::lobby_capacity", &openage::network::tests::lobby_capacity, false, false},
	{"openage::network::tests::authority_succession", &openage::network::tests::authority_succession, false, false},
	{"openage::network::tests::session", &openage::network::tests::session, false, false},
	{"openage::network::tests::rehost_ticket", &openage::network::tests::rehost_ticket, false, false},
	{"openage::network::tests::command_mailbox", &openage::network::tests::command_mailbox, false, false},
	{"openage::network::tests::connection_mailbox", &openage::network::tests::connection_mailbox, false, false},
	{"openage::network::tests::match_disconnect_coordinator", &openage::network::tests::match_disconnect_coordinator, true, false},
	{"openage::network::tests::replication", &openage::network::tests::replication, false, false},
	{"openage::network::tests::world_delta", &openage::network::tests::world_delta, false, false},
	{"openage::network::tests::world_delta_flares", &openage::network::tests::world_delta_flares, false, false},
	{"openage::network::tests::transport", &openage::network::tests::transport, false, false},
	{"openage::network::tests::transport_high_fd", &openage::network::tests::transport_high_fd, false, false},
	{"openage::network::tests::fvd_loopback", &openage::network::tests::fvd_loopback, false, false},
	{"openage::network::tests::fvd_multi_session", &openage::network::tests::fvd_multi_session, true, false},
	{"openage::network::tests::fvd_process_worker", &openage::network::tests::fvd_process_worker, false, true},
	{"openage::network::tests::relay", &openage::network::tests::relay, false, false},
	{"openage::network::tests::relay_registry", &openage::network::tests::relay_registry, false, false},
	{"openage::network::tests::relay_multi_host_lobby", &openage::network::tests::relay_multi_host_lobby, false, false},
	{"openage::network::tests::relay_service", &openage::network::tests::relay_service, false, false},
	{"openage::network::tests::relay_process_worker", &openage::network::tests::relay_process_worker, false, true},
	{"openage::path::tests::path_node", &openage::path::tests::path_node, false, false},
	{"openage::path::tests::flow_field", &openage::path::tests::flow_field, false, false},
	{"openage::renderer::tests::font", &openage::renderer::tests::font, false, false},
	{"openage::renderer::tests::font_manager", &openage::renderer::tests::font_manager, false, false},
#if WITH_OPENGL
	{"openage::renderer::opengl::tests::uniform_buffer_padding", &openage::renderer::opengl::tests::uniform_buffer_padding, true, false},
#endif
	{"openage::renderer::resources::tests::alpha_mask", &openage::renderer::resources::tests::alpha_mask, false, false},
	{"openage::renderer::resources::tests::texture_metadata", &openage::renderer::resources::tests::texture_metadata, false, false},
	{"openage::renderer::resources::tests::palette_colors", &openage::renderer::resources::tests::palette_colors, false, false},
	{"openage::rng::tests::run", &openage::rng::tests::run, false, false},
	{"openage::util::tests::constinit_vector", &openage::util::tests::constinit_vector, false, false},
	{"openage::util::tests::enum_", &openage::util::tests::enum_, false, false},
	{"openage::util::tests::fixed_point", &openage::util::tests::fixed_point, false, false},
	{"openage::util::tests::init", &openage::util::tests::init, false, false},
	{"openage::util::tests::matrix", &openage::util::tests::matrix, false, false},
	{"openage::util::tests::quaternion", &openage::util::tests::quaternion, false, false},
	{"openage::util::tests::vector", &openage::util::tests::vector, false, false},
	{"openage::util::tests::siphash", &openage::util::tests::siphash, false, false},
	{"openage::util::tests::array_conversion", &openage::util::tests::array_conversion, false, false},
	{"openage::curve::tests::container", &openage::curve::tests::container, false, false},
	{"openage::curve::tests::curve_types", &openage::curve::tests::curve_types, false, false},
	{"openage::event::tests::eventtrigger", &openage::event::tests::eventtrigger, false, false},
	{"openage::testing::tests::comparison", &openage::testing::tests::comparison, false, false},
	{"openage::input::game::tests::tower_line_axis_lock", &openage::input::game::tests::tower_line_axis_lock, false, false},
	{"openage::input::game::tests::tower_placement_mode", &openage::input::game::tests::tower_placement_mode, false, false},
	{"openage::input::game::tests::horse_selection_priority", &openage::input::game::tests::horse_selection_priority, false, false},
	{"openage::input::game::tests::tower_click_target", &openage::input::game::tests::tower_click_target, false, false},
	{"openage::input::game::tests::command_dispatcher", &openage::input::game::tests::command_dispatcher, false, false},
	{"openage::presenter::tests::hud_mouse_capture", &openage::presenter::tests::hud_mouse_capture, false, false},
	{"openage::presenter::tests::fvd_combat_sounds", &openage::presenter::tests::fvd_combat_sounds, false, false},
	{"openage::presenter::tests::fvd_minimap", &openage::presenter::tests::fvd_minimap, false, false},
	{"openage::presenter::tests::fvd_hunter_unleashed", &openage::presenter::tests::fvd_hunter_unleashed, false, false},
	{"openage::presenter::tests::fvd_construction_sounds", &openage::presenter::tests::fvd_construction_sounds, false, false},
	{"openage::audio::tests::audio_settings", &openage::audio::tests::audio_settings, false, false},
	{"openage::audio::tests::audio_mixer_effects", &openage::audio::tests::audio_mixer_effects, false, false},
	{"openage::audio::tests::audio_mixer_volume", &openage::audio::tests::audio_mixer_volume, false, false},
	{"openage::audio::tests::audio_mixer_music", &openage::audio::tests::audio_mixer_music, false, false},
	{"openage::audio::tests::audio_decode_wav", &openage::audio::tests::audio_decode_wav, false, false},
	{"openage::audio::tests::fvd_audio_files", &openage::audio::tests::fvd_audio_files, false, false},
	{"openage::audio::tests::fvd_audio_placement", &openage::audio::tests::fvd_audio_placement, false, false},
	{"openage::audio::tests::fvd_audio_throttle", &openage::audio::tests::fvd_audio_throttle, false, false},
	{"openage::audio::tests::fvd_audio_bank", &openage::audio::tests::fvd_audio_bank, false, false},
	{"openage::presenter::tests::guest_interpolation", &openage::presenter::tests::guest_interpolation, false, false},
	{"openage::presenter::tests::guest_clock_jitter", &openage::presenter::tests::guest_clock_jitter, false, false},
	{"openage::presenter::tests::guest_response_latency", &openage::presenter::tests::guest_response_latency, false, false},
	{"openage::presenter::tests::guest_clock_catchup", &openage::presenter::tests::guest_clock_catchup, false, false},
	{"openage::gamestate::tests::fvd_network_builder_count", &openage::gamestate::tests::fvd_network_builder_count, true, false},
	{"openage::gamestate::tests::fvd_scenario", &openage::gamestate::tests::fvd_scenario, true, false},
	{"openage::renderer::world::tests::structure_shadow_depth", &openage::renderer::world::tests::structure_shadow_depth, true, false},
	{"openage::renderer::world::tests::adjacent_tower_draw_order", &openage::renderer::world::tests::adjacent_tower_draw_order, true, false},
	{"openage::renderer::world::tests::terrain_fog_soft_edges", &openage::renderer::world::tests::terrain_fog_soft_edges, true, false},
	{"openage::renderer::world::tests::selection_ground_rings", &openage::renderer::world::tests::selection_ground_rings, true, false},
	{"openage::renderer::world::tests::foundation_picking", &openage::renderer::world::tests::foundation_picking, true, false},
	{"openage::renderer::world::tests::foundation_mouse_input", &openage::renderer::world::tests::foundation_mouse_input, true, false},
	{"openage::gamestate::tests::fvd_construction", &openage::gamestate::tests::fvd_construction, true, false},
	{"openage::gamestate::tests::fvd_presentation_snapshot", &openage::gamestate::tests::fvd_presentation_snapshot, true, false},
	{"openage::gamestate::tests::fvd_foundation_push_and_timeout", &openage::gamestate::tests::fvd_foundation_push_and_timeout, true, false},
	{"openage::gamestate::tests::fvd_build_queue", &openage::gamestate::tests::fvd_build_queue, true, false},
	{"openage::gamestate::tests::fvd_build_line_order", &openage::gamestate::tests::fvd_build_line_order, true, false},
	{"openage::gamestate::tests::fvd_match_end_garrison_and_hunter_stop", &openage::gamestate::tests::fvd_match_end_garrison_and_hunter_stop, true, false},
	{"openage::gamestate::tests::fvd_builder_flare", &openage::gamestate::tests::fvd_builder_flare, true, false},
	{"openage::gamestate::tests::fvd_builder_delete_all_foundations", &openage::gamestate::tests::fvd_builder_delete_all_foundations, true, false},
	{"openage::gamestate::tests::fvd_build_double_row", &openage::gamestate::tests::fvd_build_double_row, true, false},
	{"openage::gamestate::tests::fvd_animation_phase", &openage::gamestate::tests::fvd_animation_phase, false, false},
	{"openage::gamestate::tests::fvd_retask_foundation", &openage::gamestate::tests::fvd_retask_foundation, true, false},
	{"openage::gamestate::tests::fvd_queue_existing_foundations", &openage::gamestate::tests::fvd_queue_existing_foundations, true, false},
	{"openage::gamestate::tests::fvd_builder_demolition", &openage::gamestate::tests::fvd_builder_demolition, true, false},
	{"openage::gamestate::tests::fvd_builder_delete_foundation", &openage::gamestate::tests::fvd_builder_delete_foundation, true, false},
	{"openage::gamestate::tests::fvd_match_outcome_rules", &openage::gamestate::tests::fvd_match_outcome_rules, true, false},
	{"openage::gamestate::tests::fvd_garrison", &openage::gamestate::tests::fvd_garrison, true, false},
	{"openage::gamestate::tests::fvd_allied_garrison", &openage::gamestate::tests::fvd_allied_garrison, true, false},
	{"openage::gamestate::tests::fvd_garrison_hop", &openage::gamestate::tests::fvd_garrison_hop, true, false},
	{"openage::gamestate::tests::fvd_garrison_all_sides", &openage::gamestate::tests::fvd_garrison_all_sides, true, false},
	{"openage::gamestate::tests::fvd_hunter_spawning", &openage::gamestate::tests::fvd_hunter_spawning, true, false},
	{"openage::gamestate::tests::fvd_horse_spawn_blocked", &openage::gamestate::tests::fvd_horse_spawn_blocked, true, false},
	{"openage::gamestate::tests::fvd_hunter_combat", &openage::gamestate::tests::fvd_hunter_combat, true, false},
	{"openage::gamestate::tests::fvd_combat_balance", &openage::gamestate::tests::fvd_combat_balance, true, false},
	{"openage::gamestate::tests::fvd_hunter_attack_move_regressions", &openage::gamestate::tests::fvd_hunter_attack_move_regressions, true, false},
	{"openage::gamestate::tests::fvd_hunter_builder_aggro", &openage::gamestate::tests::fvd_hunter_builder_aggro, true, false},
	{"openage::gamestate::tests::fvd_hunter_focus_attack", &openage::gamestate::tests::fvd_hunter_focus_attack, true, false},
	{"openage::gamestate::tests::fvd_first_builder_attack_order", &openage::gamestate::tests::fvd_first_builder_attack_order, true, false},
	{"openage::gamestate::tests::fvd_hunter_stances", &openage::gamestate::tests::fvd_hunter_stances, true, false},
	{"openage::gamestate::tests::fvd_hunter_patrol_stances", &openage::gamestate::tests::fvd_hunter_patrol_stances, true, false},
	{"openage::gamestate::tests::fvd_fog_of_war", &openage::gamestate::tests::fvd_fog_of_war, true, false},
	{"openage::gamestate::tests::fvd_hunter_tower", &openage::gamestate::tests::fvd_hunter_tower, true, false},
	{"openage::gamestate::tests::fvd_hunter_rally_point", &openage::gamestate::tests::fvd_hunter_rally_point, true, false},
	{"openage::console::tests::render", &openage::console::tests::render, false, true},
	{"openage::console::tests::interactive", &openage::console::tests::interactive, false, true},
	{"openage::error::demo", &openage::error::demo, false, true},
	{"openage::gamestate::tests::activity_demo", &openage::gamestate::tests::activity_demo, false, true},
	{"openage::input::tests::action_demo", &openage::input::tests::action_demo, false, true},
	{"openage::log::tests::demo", &openage::log::tests::demo, false, true},
	{"openage::test::benchmark", &openage::test::benchmark, false, true},
};

void help() {
	std::cout << "Usage: openage_native_cpp_tests [--list | --all [--have-assets] | --exact NAME | --prefix PREFIX]\n"
	             "  --list             List registered native C++ tests\n"
	             "  --exact NAME       Run one fully qualified test\n"
	             "  --prefix PREFIX    Run all tests starting with PREFIX\n"
	             "  --all              Run non-opt-in tests (asset-bound tests need --have-assets)\n";
}

void diagnostic_failure() {
	// Exercise the failure path without registering a permanently failing test.
	TESTFAILMSG(__FILE__ << ":" << __LINE__ << ": native failure diagnostic");
}

int run(const Test &test) {
	std::cout << "[ RUN      ] " << test.name << std::endl;
	try {
		test.function();
		std::cout << "[       OK ] " << test.name << '\n';
		return 0;
	}
	catch (const openage::error::Error &error) {
		std::cerr << "[  FAILED  ] " << test.name << ": " << error.what() << '\n';
		std::cerr << error << '\n'; // preserved C++ exception and source location
	}
	catch (const std::exception &error) {
		std::cerr << "[  FAILED  ] " << test.name << ": " << error.what() << '\n';
	}
	catch (...) {
		std::cerr << "[  FAILED  ] " << test.name << ": unknown C++ exception\n";
	}
	return 1;
}
} // namespace

int openage::testing::native_test_main(int argc, char **argv) {
	// Register the global C++ log sink before constructing MSG(err).
	// Without a sink, MessageBuilder suppresses text formatting and even
	// deliberate assertion failures lose the expected diagnostic message.
	openage::log::set_level(openage::log::level::warn);
	if (argc == 2 && std::string_view{argv[1]} == "--verify-error-report") {
		return run({"native::failure_diagnostic", diagnostic_failure, false, true});
	}
	if (argc == 2 && std::string_view{argv[1]} == "--help") {
		help();
		return 0;
	}
	if (argc == 2 && std::string_view{argv[1]} == "--list") {
		for (const auto &test : native_test_entries) {
			std::cout << test.name << '\n';
		}
		return 0;
	}
	const bool all = (argc == 2 || argc == 3) &&
		std::string_view{argv[1]} == "--all" &&
		(argc == 2 || std::string_view{argv[2]} == "--have-assets");
	const bool have_assets = all && argc == 3;
	const bool exact = argc == 3 && std::string_view{argv[1]} == "--exact";
	const bool prefix = argc == 3 && std::string_view{argv[1]} == "--prefix";
	if (!all && !exact && !prefix) {
		help();
		return 2;
	}

	const std::string_view wanted = all ? std::string_view{} : argv[2];
	const std::string normalized = !all && !wanted.starts_with("openage::")
		? std::string{"openage::"} + std::string{wanted}
		: std::string{wanted};
	int failed = 0;
	int matched = 0;
	for (const auto &test : native_test_entries) {
		const bool selected = (all && !test.opt_in &&
			(have_assets || !test.requires_assets)) ||
			(exact && test.name == normalized) ||
			(prefix && test.name.starts_with(normalized));
		if (!selected) {
			continue;
		}
		++matched;
		failed += run(test);
	}
	if (matched == 0) {
		std::cerr << "No registered native C++ test matched: " << wanted << '\n';
		return 2;
	}
	std::cout << "Native C++ tests: " << matched - failed << "/" << matched
	          << " passed\n";
	return failed ? 1 : 0;
}
