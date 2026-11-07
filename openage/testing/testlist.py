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
    yield ("openage.game.placeholder_sounds.test", "FVD placeholder sounds are generated once")
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
