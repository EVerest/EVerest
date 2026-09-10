# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Unit tests for the MQTT connect probe on `EverestTestController`.

`BrokerNotConnectedError` used to say only that a CONNACK never arrived
within the budget. That sentence is true of a broker that is merely slow, of
a broker that hangs up on every session, and of a broker that refuses the
session outright, so it could not be used to choose between raising the
budget and fixing the broker.

These tests enumerate the failure modes the harness can actually meet and
assert that each one produces a *different*, correct verdict. They run
against real sockets rather than a mocked paho client: the thing under test
is what paho and the kernel do to a session, which a mock cannot reproduce.
"""

import os
import socket
import sys
import threading
import time
import types
from unittest.mock import MagicMock

import pytest

# Stub the heavy everest_core import; the controller only needs the type.
_stub_everest_core_mod = types.ModuleType(
    "everest.testing.core_utils.everest_core"
)


class _StubEverestCore:  # pragma: no cover - placeholder only
    pass


_stub_everest_core_mod.EverestCore = _StubEverestCore  # type: ignore[attr-defined]
sys.modules.setdefault(
    "everest.testing.core_utils.everest_core", _stub_everest_core_mod
)

_PKG_SRC = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "src"))
if _PKG_SRC not in sys.path:
    sys.path.insert(0, _PKG_SRC)

from everest.testing.core_utils.controller.everest_test_controller import (  # noqa: E402
    BrokerConnectProbe,
    EverestTestController,
    _first_reason_code,
    _reason_is_failure,
)
from everest.testing.core_utils.sim_registry import (  # noqa: E402
    BrokerNotConnectedError,
)

# Short enough to keep the suite quick, long enough that paho's network
# thread gets a scheduling slice on a loaded machine.
_BUDGET_S = 1.5
# Post-timeout grace used in these tests. Production waits far longer; every
# arm here only needs the window to exist, not to be that wide.
_GRACE_S = 0.6
# A broker that answers this far after the budget is late, not silent.
_SLOW_DELAY_S = _BUDGET_S + 0.15

# CONNACK, remaining length 2, session-present 0, return code 5 (not authorized).
_CONNACK_REFUSED = bytes([0x20, 0x02, 0x00, 0x05])
# The same packet with return code 0: session accepted.
_CONNACK_ACCEPTED = bytes([0x20, 0x02, 0x00, 0x00])


class _FakeBroker:
    """A listening socket that mistreats clients in one specific way."""

    def __init__(self, behavior: str):
        self._behavior = behavior
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind(("127.0.0.1", 0))
        self._sock.listen(8)
        self.port = self._sock.getsockname()[1]
        self._held = []
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def _serve(self):
        self._sock.settimeout(0.2)
        while not self._stop.is_set():
            try:
                conn, _ = self._sock.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            if self._behavior == "silent":
                # Accept and never answer. Holding the reference keeps the
                # socket open, which is the whole point of this arm.
                self._held.append(conn)
            elif self._behavior == "hangup":
                conn.close()
            elif self._behavior == "refuse":
                self._respond(conn, _CONNACK_REFUSED)
            elif self._behavior == "accept":
                self._respond(conn, _CONNACK_ACCEPTED)
            elif self._behavior == "slow":
                # Answers, but only after the client's budget has expired.
                time.sleep(_SLOW_DELAY_S)
                self._respond(conn, _CONNACK_ACCEPTED)

    def _respond(self, conn, connack):
        try:
            conn.recv(4096)
            conn.sendall(connack)
        except OSError:
            pass
        self._held.append(conn)

    def close(self):
        self._stop.set()
        self._thread.join(timeout=2)
        for conn in self._held:
            conn.close()
        self._sock.close()


@pytest.fixture
def broker_factory():
    brokers = []

    def make(behavior: str) -> _FakeBroker:
        broker = _FakeBroker(behavior)
        brokers.append(broker)
        return broker

    yield make
    for broker in brokers:
        broker.close()


@pytest.fixture(autouse=True)
def _short_connack_grace(monkeypatch):
    import everest.testing.core_utils.controller.everest_test_controller as mod

    monkeypatch.setattr(mod, "_BROKER_CONNACK_GRACE_S", _GRACE_S)


def _controller(monkeypatch, port: int) -> EverestTestController:
    monkeypatch.setenv("MQTT_SERVER_ADDRESS", "127.0.0.1")
    monkeypatch.setenv("MQTT_SERVER_PORT", str(port))
    core = MagicMock()
    core.everest_uuid = "probe-test-uuid"
    core.mqtt_external_prefix = "ext/"
    return EverestTestController(everest_core=core)


def _start_expecting_failure(controller) -> BrokerConnectProbe:
    with pytest.raises(BrokerNotConnectedError) as excinfo:
        controller.start(broker_connect_timeout=_BUDGET_S)
    probe = excinfo.value.probe
    # The message has to carry the evidence, not just the probe object: CI
    # reads the rendered exception out of a JUnit artifact, not a debugger.
    assert probe.describe() in str(excinfo.value)
    assert probe.verdict() in str(excinfo.value)
    return probe


# ---------------------------------------------------------------------------
# The three failure modes, each with a distinct verdict
# ---------------------------------------------------------------------------


def test_silent_broker_reports_a_socket_that_stayed_open(monkeypatch, broker_factory):
    """Accepted, never answered: the budget really was the binding constraint."""
    broker = broker_factory("silent")
    controller = _controller(monkeypatch, broker.port)
    try:
        probe = _start_expecting_failure(controller)
    finally:
        controller._destroy_mqtt_client()

    assert probe.peer == str(("127.0.0.1", broker.port))
    assert probe.tcp_connect_s is not None and probe.tcp_connect_s < _BUDGET_S
    assert probe.socket_at_timeout == "open"
    assert probe.socket_closes == 0
    assert probe.connect_failures == 0
    assert probe.connack_refusals == []
    assert probe.connack_late is False
    assert "stayed open" in probe.verdict()


def test_a_late_connack_is_reported_as_late_not_absent(monkeypatch, broker_factory):
    """Answered after the budget: the budget was short, the broker was fine.

    This is the arm the probe used to get wrong. Verified against a real
    frozen mosquitto 2.0.10: the probe said the broker "never answered
    CONNECT" in the same session the broker logged `New client connected`.
    A reader who believes that hunts for a broken broker.
    """
    broker = broker_factory("slow")
    controller = _controller(monkeypatch, broker.port)
    try:
        probe = _start_expecting_failure(controller)
    finally:
        controller._destroy_mqtt_client()

    assert probe.connack_late is True
    assert "LATE" in probe.verdict()
    assert "never answered" not in probe.verdict()


def test_the_wait_is_measured_rather_than_assumed(monkeypatch, broker_factory):
    """`waited_s` must be a measurement, because nothing else can supply it.

    It is the only signal in the probe no broker log can produce: a broker
    cannot see that the *test process* was descheduled. Reporting the
    budget back instead leaves every other assertion in this file green.
    """
    broker = broker_factory("silent")
    controller = _controller(monkeypatch, broker.port)
    started = time.monotonic()
    try:
        probe = _start_expecting_failure(controller)
        elapsed = time.monotonic() - started
    finally:
        controller._destroy_mqtt_client()

    # A wait that returns at exactly its budget was not timed, it was recited.
    assert probe.waited_s > _BUDGET_S
    assert probe.waited_s <= elapsed
    # And it is the wait, not the wait plus the grace that follows it.
    assert probe.waited_s < _BUDGET_S + _GRACE_S


def test_hangup_broker_reports_churn_not_a_short_budget(monkeypatch, broker_factory):
    """Accepted then dropped: raising the budget would change nothing."""
    broker = broker_factory("hangup")
    controller = _controller(monkeypatch, broker.port)
    try:
        probe = _start_expecting_failure(controller)
    finally:
        controller._destroy_mqtt_client()

    assert probe.socket_closes > 0
    assert "not serving this session" in probe.verdict()
    assert "stayed open" not in probe.verdict()


def test_refusing_broker_is_not_reported_as_connected(monkeypatch, broker_factory):
    """A CONNACK carrying a failure code must not count as a session.

    This is the arm the old code got wrong: `on_connect` set the connected
    event unconditionally, so a refused session looked established and the
    subsequent silence was blamed on the modules.
    """
    broker = broker_factory("refuse")
    controller = _controller(monkeypatch, broker.port)
    try:
        probe = _start_expecting_failure(controller)
    finally:
        controller._destroy_mqtt_client()

    assert not controller._connected.is_set()
    assert probe.connack_refusals, "the refusal reason code was not recorded"
    assert "REFUSED" in probe.verdict()


def test_no_listener_fails_before_the_wait(monkeypatch):
    """Nothing listening fails inside connect(), with a different exception.

    This is what makes the other three arms readable: a `BrokerNotConnected`
    error proves the TCP session was established, because an unreachable
    broker never reaches the wait at all.
    """
    spare = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    spare.bind(("127.0.0.1", 0))
    dead_port = spare.getsockname()[1]
    spare.close()

    controller = _controller(monkeypatch, dead_port)
    with pytest.raises(OSError) as excinfo:
        controller.start(broker_connect_timeout=_BUDGET_S)
    assert not isinstance(excinfo.value, BrokerNotConnectedError)


# ---------------------------------------------------------------------------
# Reason-code plumbing
# ---------------------------------------------------------------------------


@pytest.mark.parametrize("args, expected", [
    (("client", None, {"session present": 0}, 0), 0),
    (("client", None, {"session present": 0}, 5), 5),
    (("client", None, 3), 3),
    (("client", None), None),
])
def test_first_reason_code_skips_the_flags_argument(args, expected):
    assert _first_reason_code(args) == expected


def test_reason_is_failure_treats_nonzero_connack_as_refusal():
    assert not _reason_is_failure(0)
    assert _reason_is_failure(5)

    class _V2Code:
        def __init__(self, failed):
            self.is_failure = failed

    assert not _reason_is_failure(_V2Code(False))
    assert _reason_is_failure(_V2Code(True))


def test_suback_grant_of_qos1_is_not_a_refusal(monkeypatch):
    """Guard against reusing the CONNACK rule for SUBACK grants.

    A v1 SUBACK reports the granted QoS, so 1 and 2 are successes; only
    0x80 is a refusal. Collapsing the two rules would report every QoS 1
    subscription as refused and send the next reader hunting a broker bug.
    """
    controller = _controller(monkeypatch, 1883)
    registry = MagicMock()
    controller._registry = registry

    controller._on_broker_suback(None, None, 7, [1])
    registry.note_suback.assert_called_once_with(7, True)

    registry.reset_mock()
    controller._on_broker_suback(None, None, 8, [0x80])
    registry.note_suback.assert_called_once_with(8, False)


# ---------------------------------------------------------------------------
# Verdict arms that no fake broker can stage
# ---------------------------------------------------------------------------


def _probe(**overrides) -> BrokerConnectProbe:
    fields = dict(host="mqtt-server", port=1883, budget_s=10.0)
    fields.update(overrides)
    return BrokerConnectProbe(**fields)


def test_a_wait_that_overran_its_budget_blames_the_host():
    """`wait(10)` returning after 40s is a starved waiter, not a slow broker.

    Without the measured elapsed time these two are the same error text,
    and the fix for one (more CPU) is useless against the other.
    """
    probe = _probe(waited_s=41.0, socket_opens=1)
    assert "descheduled" in probe.verdict()


def test_counters_that_do_not_separate_the_causes_say_so():
    """The probe must be able to decline to answer.

    Several sockets opened with none closed is not a shape any of the named
    arms describes. Reporting the nearest arm anyway is how an instrument
    starts confirming whatever the reader already believed.
    """
    probe = _probe(waited_s=10.01, socket_opens=3)
    assert probe.verdict().startswith("undetermined")


# ---------------------------------------------------------------------------
# The census: one failure has to be readable without the other 969
# ---------------------------------------------------------------------------


def test_a_first_session_failure_makes_no_claim_about_history():
    probe = _probe(waited_s=10.01, socket_opens=1, session_ordinal=1)
    assert "already established" not in probe.verdict()


def test_a_late_failure_says_the_broker_stopped_serving_mid_run():
    """The shape that mattered in CI: 723 sessions worked, then none did.

    Reading that off one error message is the difference between "raise the
    budget" and "find what broke the broker at minute 24". Aggregating a
    thousand log lines to see it is not a diagnostic anyone repeats.
    """
    probe = _probe(waited_s=10.01, socket_opens=1, session_ordinal=181,
                   sessions_established_before=180,
                   since_last_established_s=94.0)
    verdict = probe.verdict()
    assert "already established 180 session(s)" in verdict
    assert "stopped serving part way through" in verdict
    assert "94s ago" in verdict


def test_a_broker_that_worked_then_stopped_is_reported_as_such(
        monkeypatch, broker_factory):
    """End to end over real sockets: one good session, then a dead broker.

    This is the CI shape in miniature, and it is the only arm that proves
    the established-session counter is fed by an actual CONNACK rather than
    by the attempt.
    """
    import everest.testing.core_utils.controller.everest_test_controller as mod

    monkeypatch.setattr(mod, "_SESSIONS", mod._SessionCensus())
    # start() continues into registry setup once the session is up; that is
    # not what this test is about.
    monkeypatch.setattr(mod, "SimRegistry", MagicMock())

    good = broker_factory("accept")
    working = _controller(monkeypatch, good.port)
    try:
        working.start(broker_connect_timeout=_BUDGET_S)
        assert working._connected.is_set()
    finally:
        working._destroy_mqtt_client()

    dead = broker_factory("silent")
    failing = _controller(monkeypatch, dead.port)
    try:
        probe = _start_expecting_failure(failing)
    finally:
        failing._destroy_mqtt_client()

    assert probe.session_ordinal == 2
    assert probe.sessions_established_before == 1
    assert "stopped serving part way through" in probe.verdict()


def test_a_reconnect_is_not_counted_as_a_second_session(monkeypatch):
    """A flapping session must count once, or the census flatters the broker."""
    import everest.testing.core_utils.controller.everest_test_controller as mod

    census = mod._SessionCensus()
    monkeypatch.setattr(mod, "_SESSIONS", census)
    controller = _controller(monkeypatch, 1883)

    controller._on_broker_connect(None, None, {}, 0, None)
    controller._on_broker_connect(None, None, {}, 0, None)

    ordinal, established, _since = census.attempt()
    assert (ordinal, established) == (1, 1)
