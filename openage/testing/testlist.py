# Copyright 2015-2026 the openage authors. See copying.md for legal info.

"""Lists of all possible tests; enter your tests here."""


def doctest_modules():
    """
    Yields the names of all Python modules that shall be tested during doctest.
    """

    yield "openage.util.math"
    yield "openage.util.strings"
    yield "openage.util.system"
    yield "openage.util.version"


def tests_py():
    """
    Yields tuples of (name, description, condition_function)
    for all Python test methods.

    If no description is required, just the name may be yielded.
    """

    yield ("openage.testing.doctest.test", "doctest on all modules from DOCTEST_MODULES")
    yield "openage.assets.test"
    yield "openage.cppinterface.exctranslate_tests.cpp_to_py"
    yield (
        "openage.cppinterface.exctranslate_tests.cpp_to_py_bounce",
        "translates the exception back and forth a few times",
    )
    yield ("openage.testing.misc_cpp.enum", "tests the interface for C++'s util::Enum class")
    yield ("openage.util.fslike.test.test", "test the filesystem abstraction subsystem")
    yield "openage.util.threading.test_concurrent_chain"
    yield "openage.nyan.tests.object_reference_members"
    yield (
        "openage.testing.network_two_process.test",
        "FVD movement synchronization across host and guest processes",
    )
    yield (
        "openage.testing.network_two_process.test_relay",
        "FVD gameplay frames through host, guest and relay processes",
    )
    yield "openage.convert.processor.conversion.de2.tests.diplomatic_effects"
    yield "openage.convert.processor.conversion.de2.tests.branching_upgrade_techs"


def demos_py():
    """
    Yields tuples of (name, description) for all Python demo methods.
    """

    yield (
        "openage.cppinterface.exctranslate_tests.cpp_to_py_demo",
        "translates a C++ exception and its causes to python",
    )
    yield ("openage.log.tests.demo", "demonstrates the translation of Python log messages")
    yield ("openage.convert.service.export.opus.demo.convert", "encodes an opus file from a wave file")
    yield ("openage.event.demo.curvepong", "play pong on steroids through future prediction")
    yield ("openage.gamestate.tests.simulation_demo", "showcases the game simulation")
    yield ("openage.pathfinding.tests.path_demo", "showcases the pathfinding system")
    yield ("openage.renderer.tests.renderer_demo", "showcases the renderer")
    yield ("openage.renderer.tests.renderer_stresstest", "stresstests for the renderer")
    yield ("openage.main.tests.engine_demo", "showcases the engine features")


def benchmark_py():
    """
    Yields tuples of (name, description) for python benchmark
    methods.
    """

    # TODO Add a real benchmark here, and remove this one
    yield ("openage.testing.benchmark.benchmark_test_function", "Benchmark yourself")


def tests_cpp():
    """
    Yields tuples of (name, description, condition_function)
    for all C++ test methods.

    If no description is required, just the name may be yielded.
    """

    yield "openage::coord::tests::coord"
    yield "openage::datastructure::tests::concurrent_queue"
    yield "openage::datastructure::tests::constexpr_map"
    yield "openage::datastructure::tests::pairing_heap"
    yield "openage::job::tests::test_job_manager"
    yield "openage::network::tests::messages"
    yield "openage::network::tests::stance_messages", "FVD Hunter stance and patrol wire format"
    yield "openage::network::tests::fog_messages", "FVD fog of war and remembered building wire format"
    yield "openage::network::tests::protocol"
    yield "openage::network::tests::handshake"
    yield "openage::network::tests::lobby"
    yield "openage::network::tests::lobby_relay_client"
    yield (
        "openage::network::tests::lobby_latency",
        "Five-player relay command and recipient-update benchmark (run separately with optional shaping)",
        lambda _env: False,  # Opt-in: slower than the ordinary correctness suite.
    )
    yield (
        "openage::network::tests::lobby_capacity",
        "Full 4/8-player relay, Hunter rehost, contested Builder orders, "
        "disconnect continuation and match termination",
    )
    yield (
        "openage::network::tests::authority_succession",
        "FVD Hunter takeover epoch fencing and complete-checkpoint safety requirements",
    )
    yield "openage::network::tests::session"
    yield "openage::network::tests::rehost_ticket", "Single-use Hunter rehost proof and old-slot binding"
    yield "openage::network::tests::command_mailbox"
    yield "openage::network::tests::connection_mailbox"
    yield (
        "openage::network::tests::match_disconnect_coordinator",
        "FVD mid-match disconnect, authoritative revision and stale-command rejection",
        lambda env: env["has_assets"],
    )
    yield "openage::network::tests::replication"
    yield "openage::network::tests::world_delta"
    yield "openage::network::tests::transport"
    yield (
        "openage::network::tests::transport_high_fd",
        "TCP poll read with POSIX descriptor beyond select FD_SETSIZE (skips if OS limit is too low)",
    )
    yield (
        "openage::network::tests::fvd_loopback",
        "FVD command authority and state synchronization over loopback TCP",
    )
    yield (
        "openage::network::tests::fvd_multi_session",
        "FVD 3/4/8-player TCP sessions, simultaneous Builder commands and disconnect continuation",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::network::tests::fvd_process_worker",
        "Internal worker for the FVD two-process network test",
        lambda _env: False,
    )
    yield "openage::network::tests::relay", "FVD relay admission and forwarding"
    yield (
        "openage::network::tests::relay_registry",
        "FVD concurrent relay pair admission, isolation and cancellation",
    )
    yield (
        "openage::network::tests::relay_multi_host_lobby",
        "FVD live three/eight-player relay lobby roster and immutable role assignment",
    )
    yield (
        "openage::network::tests::relay_service",
        "FVD relay room discovery, private access and frame forwarding",
    )
    yield (
        "openage::network::tests::relay_process_worker",
        "Internal worker for the FVD three-process relay test",
        lambda _env: False,
    )
    yield "openage::path::tests::path_node", "pathfinding"
    yield "openage::path::tests::flow_field", "pathfinding"
    yield "openage::pyinterface::tests::pyobject"
    yield "openage::pyinterface::tests::err_py_to_cpp"
    yield "openage::renderer::tests::font"
    yield "openage::renderer::tests::font_manager"
    yield "openage::renderer::resources::tests::alpha_mask"
    yield "openage::renderer::resources::tests::texture_metadata"
    yield "openage::renderer::resources::tests::palette_colors"
    yield "openage::rng::tests::run"
    yield "openage::util::tests::constinit_vector"
    yield "openage::util::tests::enum_"
    yield "openage::util::tests::fixed_point"
    yield "openage::util::tests::init"
    yield "openage::util::tests::matrix"
    yield "openage::util::tests::quaternion"
    yield "openage::util::tests::vector"
    yield "openage::util::tests::siphash"
    yield "openage::util::tests::array_conversion"
    yield "openage::curve::tests::container"
    yield "openage::curve::tests::curve_types"
    yield "openage::event::tests::eventtrigger"
    yield "openage::testing::tests::comparison", "Const and non-printable comparison assertions"
    yield "openage::input::game::tests::tower_line_axis_lock"
    yield "openage::input::game::tests::tower_placement_mode"
    yield (
        "openage::input::game::tests::horse_selection_priority",
        "Box selections keep horses and leave out the Hunter tower",
    )
    yield "openage::presenter::tests::hud_mouse_capture"
    yield "openage::presenter::tests::guest_interpolation"
    yield "openage::presenter::tests::guest_clock_jitter"
    yield "openage::presenter::tests::guest_response_latency"
    yield "openage::presenter::tests::guest_clock_catchup"
    yield (
        "openage::gamestate::tests::fvd_network_builder_count",
        "FVD network starting Builders: one for 2 people, two for 3, seven for 8",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_scenario",
        "FVD plains, starting players and tower placement",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::renderer::world::tests::structure_shadow_depth",
        "FVD adjacent tower shadows stay behind the rear structure",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_construction",
        "FVD Builder movement, timed construction and cancellation",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_presentation_snapshot",
        "FVD immutable HUD snapshots across construction transitions",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_build_queue",
        "FVD FIFO tower construction and reserved queued sites",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_build_line_order",
        "FVD line placement queues valid tower foundations in order",
        lambda env: env["has_assets"],
    )
    yield "openage::gamestate::tests::fvd_animation_phase"
    yield (
        "openage::gamestate::tests::fvd_retask_foundation",
        "FVD persistent queued foundations and Builder right-click retasking",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_builder_demolition",
        "FVD owned tower demolition, rubble lifetime and rebuild cleanup",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_builder_delete_foundation",
        "FVD Delete on queued, walked-to and abandoned foundations",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_match_outcome_rules",
        "FVD Hunter/Builder victory, 45-minute timeout and irreversible outcomes",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_garrison",
        "FVD Builder garrison validation, concealment, immunity and ejection",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_garrison_hop",
        "FVD adjacent tower-to-tower shelter transfer and point marker cleanup",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_garrison_all_sides",
        "FVD immediate shelter entry from all eight tower faces/corners",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_hunter_spawning",
        "FVD Hunter horse spawn cadence and idle behavior",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_horse_spawn_blocked",
        "FVD horse spawn with blocked centre",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_hunter_combat",
        "FVD Hunter AttackMove, tower damage and lethal kill credit",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_combat_balance",
        "FVD approximately ten-second 3/4-tower and three-horse focused combat",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_hunter_attack_move_regressions",
        "FVD stale Hunter selections and three-tile tower pursuit",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_hunter_builder_aggro",
        "FVD Builder detection, pursuit and tower targeting priority",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_hunter_focus_attack",
        "FVD Hunter focus attacks and explicit target override",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_hunter_stances",
        "FVD Hunter Observe/Aggressive/Defensive stances and Patrol",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_hunter_patrol_stances",
        "FVD Patrol behaviour under Observe, Defensive and Aggressive stances",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_fog_of_war",
        "FVD per-side vision, hidden targets, explored memory and filtered replication",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_hunter_tower",
        "FVD invulnerable central Hunter tower: 20-second gate, range, Builder-only targets and damage",
        lambda env: env["has_assets"],
    )
    yield (
        "openage::gamestate::tests::fvd_hunter_rally_point",
        "FVD Hunter tower rally point, Hunter-only marker and mixed horse/tower selections",
        lambda env: env["has_assets"],
    )


def demos_cpp():
    """
    Yields tuples of (name, description) for all C++ demo methods.
    """

    yield ("openage::console::tests::render", "prints a few test lines to a buffer, and renders it to stdout")
    yield (
        "openage::console::tests::interactive",
        "showcases console as an interactive terminal on your current tty",
    )
    yield ("openage::error::demo", "showcases the openage exceptions, including backtraces")
    yield ("openage::gamestate::tests::activity_demo", "showcases the activity system in the gamestate")
    yield ("openage::input::tests::action_demo", "showcases the low-level input system")
    yield ("openage::log::tests::demo", "showcases the logging system")
    yield ("openage::pyinterface::tests::err_py_to_cpp_demo", "translates a Python exception to C++")
    yield ("openage::pyinterface::tests::pyobject_demo", "a tiny interactive interpreter using PyObjectRef")


def benchmark_cpp():
    """
    Yields tuples of (name, description) for C++ benchmark
    methods.
    """

    # TODO Add a real benchmark here!
    yield ("openage::test::benchmark", "Test the benchmark")
