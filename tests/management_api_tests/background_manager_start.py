#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Shared helper for driving EverestCore.start() in a background thread for --into-idle tests."""

import logging
import threading
from contextlib import contextmanager
from typing import Iterator, List, Tuple

from everest.testing.core_utils.everest_core import EverestCore

# How long to wait for the starter thread to notice a forced unblock before giving up.
_UNBLOCK_GRACE_S = 5.0


@contextmanager
def background_manager_start(
    everest_core: EverestCore, startup_timeout_s: float = 120.0
) -> Iterator[Tuple[threading.Thread, List[BaseException]]]:
    """Runs everest_core.start() on a background thread; guarantees it is joined on exit.

    Yields (thread, start_exception); the caller joins and asserts on them itself, so a
    startup failure is reported at the natural point in the test.

    Should the caller not get that far, the thread is still waiting for
    ALL_MODULES_STARTED in a poll loop that never notices the manager dying at teardown,
    so joining alone would block for the full `startup_timeout_s`. Closing the
    status-fifo listener wakes that wait and fails the next fifo read, which unblocks
    start() with an exception like any other startup failure.
    """
    start_exception: List[BaseException] = []

    def start_core() -> None:
        try:
            everest_core.start(startup_timeout_s=startup_timeout_s)
        except BaseException as exc:  # recorded for the caller to assert on
            start_exception.append(exc)

    thread = threading.Thread(target=start_core)
    thread.start()
    try:
        yield thread, start_exception
    finally:
        thread.join(timeout=_UNBLOCK_GRACE_S)
        if thread.is_alive():
            logging.error(
                "everest_core.start() background thread still running while its test is "
                "tearing down; closing the status-fifo listener to unblock it instead of "
                "waiting out the full startup budget")
            everest_core.status_listener.close()
            thread.join(timeout=_UNBLOCK_GRACE_S)
            if thread.is_alive():
                logging.error(
                    "everest_core.start() background thread is still running even after "
                    "closing the status-fifo listener; leaving it running")
