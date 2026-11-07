# Copyright 2026 the openage authors. See copying.md for legal info.

"""Command-line entry point for the FVD room relay service."""

from __future__ import annotations

import typing

from ..log import info

if typing.TYPE_CHECKING:
    from argparse import ArgumentParser, Namespace
    from collections.abc import Callable


def init_subparser(cli: ArgumentParser) -> None:
    """Initialize the relay service subparser."""
    cli.set_defaults(entrypoint=main)
    cli.add_argument(
        "--listen",
        default="0.0.0.0",
        help="IPv4, IPv6 or DNS address on which the relay accepts connections",
    )
    cli.add_argument(
        "--port",
        type=int,
        default=40000,
        help="TCP port on which the relay accepts connections",
    )
    cli.add_argument(
        "--player-token",
        action="append",
        default=[],
        metavar="TOKEN",
        help=(
            "authorize one reusable 64-character player token; repeat for each "
            "alpha player (omitting this option enables public access)"
        ),
    )
    cli.add_argument(
        "--max-rooms",
        type=int,
        default=64,
        help="maximum number of advertised rooms waiting for a guest",
    )
    cli.add_argument(
        "--max-connections",
        type=int,
        default=128,
        help="maximum number of concurrent relay requests and matches",
    )


def _parse_token(value: str, error: Callable[[str], typing.NoReturn]) -> bytes:
    """Parse and validate one authorized player token."""
    try:
        token = bytes.fromhex(value)
    except ValueError:
        error("relay player tokens must be hexadecimal")
    if len(token) != 32:
        error("relay player tokens must contain exactly 32 bytes")
    if not any(token):
        error("relay player tokens must not be all zero")
    return token


def main(args: Namespace, error: Callable[[str], typing.NoReturn]) -> int:
    """Run a persistent public or token-restricted relay."""
    if args.port <= 0 or args.port > 65535:
        error("relay port must be between 1 and 65535")
    if args.max_rooms <= 0 or args.max_rooms > 4096:
        error("relay max rooms must be between 1 and 4096")
    if args.max_connections <= 0:
        error("relay max connections must be positive")

    tokens = [_parse_token(value, error) for value in args.player_token]
    if len(tokens) != len(set(tokens)):
        error("relay player tokens must be unique")

    from ..cppinterface.setup import setup as cpp_interface_setup
    from .main_cpp import run_relay, stop_relay

    cpp_interface_setup(args)
    access = f"{len(tokens)} authorized player token(s)" if tokens else "public access"
    info("starting FVD room relay with %s", access)
    result: list[int] = []
    failure: list[BaseException] = []

    def serve() -> None:
        try:
            result.append(
                run_relay(
                    args.listen,
                    args.port,
                    tokens,
                    args.max_rooms,
                    args.max_connections,
                )
            )
        except BaseException as exc:  # Propagate the worker failure on the CLI thread.
            failure.append(exc)

    from threading import Thread

    worker = Thread(target=serve, name="fvd-relay")
    worker.start()
    try:
        while worker.is_alive():
            worker.join(0.25)
    except KeyboardInterrupt:
        while worker.is_alive():
            stop_relay()
            worker.join(0.05)
        return 130

    if failure:
        raise failure[0]
    return result[0]
