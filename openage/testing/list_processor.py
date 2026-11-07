# Copyright 2015-2026 the openage authors. See copying.md for legal info.

"""Processes the raw test lists from the testlist module."""

from collections import OrderedDict
from importlib import import_module
from typing import Callable

from ..util.strings import lstrip_once


def list_targets(test_lister: Callable, demo_lister: Callable, benchmark_lister: Callable):
    """
    Yields tuples of (testname, type, description, condition_function)
    for given test and demo listers.

    A processing step between the raw lists in testlist, and get_all_tests().
    """

    def default_cond(_):
        """default condition test to enable a test"""
        return True

    for test in test_lister():
        if not isinstance(test, tuple):
            test = (test,)

        if not test:
            raise ValueError("empty test definition encountered")

        if len(test) == 3:
            condfun = test[2]
        else:
            condfun = default_cond

        if len(test) >= 2:
            desc = test[1]
        else:
            desc = ""

        name = test[0]

        yield name, "test", desc, condfun

    for demo in demo_lister():
        name, desc = demo
        yield name, "demo", desc, default_cond

    for benchmark in benchmark_lister():
        name, desc = benchmark
        yield name, "benchmark", desc, default_cond


def list_targets_py():
    """Invokes list_targets() with the py-specific listers."""
    from .testlist import benchmark_py, demos_py, tests_py

    yield from list_targets(tests_py, demos_py, benchmark_py)


def get_all_targets() -> OrderedDict:
    """
    Reads pure Python tests, demos and benchmarks only. Native C++ tests use openage_native_cpp_tests.

    returns an OrderedDict of
    {(testname, type): conditionfun, lang, description, testfun}.

    type is in {'demo', 'test'},
    lang is 'py',
    conditionfun is a callable which determines if the test is
        to be run in the given environment
    description is a str, and
    testfun is callable and takes 0 args for tests / list(str) for demos.
    """
    result = OrderedDict()

    for name, type_, description, conditionfun in list_targets_py():
        modulename, objectname = name.rsplit(".", maxsplit=1)

        try:
            module = import_module(modulename)
            func = getattr(module, objectname)
        except Exception as exc:
            raise ValueError("no such function: " + name) from exc

        try:
            name = lstrip_once(name, "openage.")
        except ValueError as exc:
            raise ValueError("Unexpected Python test/demo name") from exc

        result[name, type_] = conditionfun, "py", description, func

    return result
