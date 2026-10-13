# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Resource lifetime of EverestCore and the harness's MQTT clients.

A pytest worker runs hundreds of tests in one process. Anything a test leaves
open accumulates, and paho's network loop cannot service a socket whose
descriptor is 1024 or above, so a leak eventually surfaces as a broker that
"never acknowledged" the connection. The collector is disabled while counting:
a descriptor that only a garbage collection releases is still a leak.
"""

import gc
import importlib.util
import os
import socket
import sys
import textwrap
import types

import pytest

_PKG_SRC = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "src"))
if _PKG_SRC not in sys.path:
    sys.path.insert(0, _PKG_SRC)

# everest_core imports RuntimeSession from libeverestpy, which only its
# get_runtime_session() uses.
if importlib.util.find_spec("everest.framework") is None:
    _framework = types.ModuleType("everest.framework")
    _framework.RuntimeSession = object  # type: ignore[attr-defined]
    sys.modules["everest.framework"] = _framework

# Other test modules register a stub under the canonical name, so load the real
# module under its own. The package is resolved rather than src/ assumed: the
# integration image carries only the tests and the installed wheel.
_CORE_UTILS = importlib.util.find_spec("everest.testing.core_utils").submodule_search_locations[0]
_spec = importlib.util.spec_from_file_location(
    "everest.testing.core_utils._everest_core_under_test",
    os.path.join(_CORE_UTILS, "everest_core.py"),
)
everest_core = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(everest_core)

from everest.testing.core_utils.common import close_mqtt_client  # noqa: E402

_FAKE_MANAGER = textwrap.dedent("""\
    #!{python}
    import signal, sys, time
    status = open(sys.argv[sys.argv.index("--status-fifo") + 1], "w", buffering=1)

    def on_sigint(*_):
        status.write("SIGINT_RECEIVED\\n")
        sys.exit(0)

    signal.signal(signal.SIGINT, on_sigint)
    sys.stderr.write("fake manager up\\n")
    sys.stderr.flush()
    status.write("ALL_MODULES_STARTED\\n")
    while True:
        time.sleep(1)
""")

_CONFIG = textwrap.dedent("""\
    active_modules:
      car_simulator:
        module: EvSimulator
        config_module:
          connector_id: 1
      yeti_driver:
        module: YetiSimulator
        config_module:
          connector_id: 1
""")


@pytest.fixture
def prefix(tmp_path):
    manager = tmp_path / "prefix" / "bin" / "manager"
    manager.parent.mkdir(parents=True)
    manager.write_text(_FAKE_MANAGER.format(python=sys.executable))
    manager.chmod(0o755)
    return tmp_path / "prefix"


@pytest.fixture
def config(tmp_path):
    path = tmp_path / "everest-config.yaml"
    path.write_text(_CONFIG)
    return path


@pytest.fixture
def make_core(tmp_path, prefix, config):
    runs = iter(range(1000))

    def make():
        run_dir = tmp_path / f"run{next(runs)}"
        run_dir.mkdir()
        return everest_core.EverestCore(prefix_path=prefix, config_path=config, tmp_path=run_dir)

    return make


@pytest.fixture
def no_gc():
    gc.disable()
    yield
    gc.enable()


def _fd_count() -> int:
    return len(os.listdir("/proc/self/fd"))


@pytest.mark.usefixtures("no_gc")
def test_stop_releases_every_descriptor_the_core_opened(make_core):
    before = _fd_count()

    for _ in range(5):
        core = make_core()
        core.start()
        core.stop()

    assert _fd_count() == before


@pytest.mark.usefixtures("no_gc")
def test_a_core_that_never_started_releases_its_status_fifo(make_core):
    before = _fd_count()

    make_core().stop()

    assert _fd_count() == before


@pytest.mark.usefixtures("no_gc")
def test_restart_after_stop_keeps_descriptors_flat(make_core):
    core = make_core()
    before = _fd_count()

    for _ in range(3):
        core.start()
        core.stop()

    # The status fifo is reopened on restart, so the core holds one fewer
    # descriptor after stop than before its first start.
    assert _fd_count() == before - 1


def test_a_second_stop_leaves_other_descriptors_alone(make_core):
    # Test fixtures stop the core once through the controller and once
    # directly. The fifo's descriptor number is free after the first stop and
    # the next open reuses it.
    core = make_core()
    core.start()
    core.stop()
    read_end, write_end = os.pipe()
    try:
        core.stop()
        os.fstat(read_end)
        os.fstat(write_end)
    finally:
        os.close(read_end)
        os.close(write_end)


def test_status_written_before_exit_stays_readable_after_stop(make_core):
    # Lifecycle tests assert on what the manager reported while shutting down.
    core = make_core()
    core.start()
    core.stop()

    core.wait_for_manager_status(everest_core.ManagerStatusFifo.SIGINT_RECEIVED, timeout_s=1.0)


@pytest.mark.usefixtures("no_gc")
def test_close_mqtt_client_releases_its_sockets():
    mqtt = pytest.importorskip("paho.mqtt.client")
    from paho.mqtt import __version__ as paho_version

    listener = socket.create_server(("127.0.0.1", 0))
    try:
        before = _fd_count()
        if paho_version < "2.0":
            client = mqtt.Client("fd-test")
        else:
            client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2, client_id="fd-test")
        client.connect(*listener.getsockname())
        client.loop_start()
        broker_side, _ = listener.accept()

        close_mqtt_client(client)
        broker_side.close()

        # `client` is still referenced, as it is when a callback cycle holds it.
        assert _fd_count() == before
    finally:
        listener.close()


def test_stop_closes_the_probe_modules_left_open(make_core, monkeypatch):
    # Tests create ProbeModules freely and never close them; each holds its own
    # MQTT connection and threads.
    closed = []
    probe_module = types.ModuleType("everest.testing.core_utils.probe_module")
    probe_module.close_all = lambda: closed.append(True)
    monkeypatch.setitem(sys.modules, probe_module.__name__, probe_module)

    core = make_core()
    core.start()
    core.stop()

    assert closed == [True]


class _FakeModule:
    def __init__(self, module_id, session):
        self.closed = 0

    def say_hello(self):
        return None

    def shutdown_handler(self, handler):
        pass

    def close(self):
        self.closed += 1


def _load_probe_module(monkeypatch):
    framework = types.ModuleType("everest.framework")
    framework.Module = _FakeModule
    framework.RuntimeSession = object
    framework.error = types.ModuleType("everest.framework.error")
    framework.error.Error = object  # probe_module annotates with it; before Python 3.14 that runs at import
    monkeypatch.setitem(sys.modules, "everest.framework", framework)
    spec = importlib.util.spec_from_file_location(
        "everest.testing.core_utils._probe_module_under_test", os.path.join(_CORE_UTILS, "probe_module.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_close_all_closes_each_open_probe_once(monkeypatch):
    probe_module = _load_probe_module(monkeypatch)
    first = probe_module.ProbeModule(session=None)
    second = probe_module.ProbeModule(session=None)
    modules = [first._mod, second._mod]

    probe_module.close_all()
    probe_module.close_all()
    first.close()

    assert [m.closed for m in modules] == [1, 1]
