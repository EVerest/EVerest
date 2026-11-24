#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import asyncio
import socket
import struct
import threading
from typing import Dict, List, Optional

import pytest

from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule

INVERTER_PORT = 15503
RESERVE_W = 1000
FALLBACK_W = 500


def int16(value: int) -> int:
    return value & 0xFFFF


class FakeInverter:
    """Minimal SunSpec device: a Modbus TCP server answering function code 3."""

    def __init__(self, base: int, model_id: int, w: int, w_sf: int):
        self.registers: Dict[int, int] = {}
        self.listener: Optional[socket.socket] = None
        self.clients: List[socket.socket] = []
        lengths = {701: 153, 101: 50, 102: 50, 103: 50}
        offsets = {701: (10, 116), 101: (14, 15), 102: (14, 15), 103: (14, 15)}

        self._put(base, [0x5375, 0x6E53])
        self._put(base + 2, [1, 66] + [0] * 66)
        model = base + 2 + 68
        self._put(model, [model_id, lengths[model_id]] + [0] * lengths[model_id])
        self._put(model + 2 + lengths[model_id], [0xFFFF, 0])
        self.w_address = model + offsets[model_id][0]
        self.w_sf_address = model + offsets[model_id][1]
        self.set_power(w, w_sf)

    def _put(self, address: int, values: List[int]) -> None:
        for i, value in enumerate(values):
            self.registers[address + i] = value

    def set_power(self, w: int, w_sf: int) -> None:
        self.registers[self.w_address] = int16(w)
        self.registers[self.w_sf_address] = int16(w_sf)

    def start(self) -> None:
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", INVERTER_PORT))
        self.listener.listen(4)
        threading.Thread(target=self._accept, args=(self.listener,), daemon=True).start()

    def stop(self) -> None:
        if self.listener is not None:
            # shutdown() wakes the thread blocked in accept(); close() alone does not.
            try:
                self.listener.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.listener.close()
            self.listener = None
        for client in self.clients:
            try:
                client.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            client.close()
        self.clients = []

    def _accept(self, listener: socket.socket) -> None:
        while True:
            try:
                client, _ = listener.accept()
            except OSError:
                return
            self.clients.append(client)
            threading.Thread(target=self._serve, args=(client,), daemon=True).start()

    def _serve(self, client: socket.socket) -> None:
        try:
            while True:
                header = self._recv(client, 7)
                transaction, protocol, length, unit = struct.unpack(">HHHB", header)
                pdu = self._recv(client, length - 1)
                response = self._handle(pdu)
                client.sendall(struct.pack(">HHHB", transaction, protocol, len(response) + 1, unit) + response)
        except (OSError, ConnectionError):
            return

    @staticmethod
    def _recv(client: socket.socket, count: int) -> bytes:
        data = b""
        while len(data) < count:
            chunk = client.recv(count - len(data))
            if not chunk:
                raise ConnectionError("closed")
            data += chunk
        return data

    def _handle(self, pdu: bytes) -> bytes:
        function = pdu[0]
        if function != 0x03:
            return bytes([function | 0x80, 0x01])
        address, count = struct.unpack(">HH", pdu[1:5])
        try:
            values = [self.registers[address + i] for i in range(count)]
        except KeyError:
            return bytes([0x83, 0x02])
        return bytes([0x03, 2 * count]) + struct.pack(f">{count}H", *values)


class LimitRecorder:
    def __init__(self):
        self.limits: List[float] = []

    def __call__(self, args: dict):
        entry = args["value"]["schedule_import"][0]
        self.limits.append(entry["limits_to_leaves"]["total_power_W"]["value"])
        return None

    def last(self) -> Optional[float]:
        return self.limits[-1] if self.limits else None


def is_close(value: Optional[float], expected: float) -> bool:
    return value is not None and abs(value - expected) < 0.5


async def eventually(condition, timeout: float = 10.0):
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    while not condition():
        if loop.time() > deadline:
            raise TimeoutError("condition not reached")
        await asyncio.sleep(0.1)


async def start(everest_core: EverestCore) -> LimitRecorder:
    everest_core.start(standalone_module="probe")
    probe = ProbeModule(everest_core.get_runtime_session())
    recorder = LimitRecorder()
    probe.implement_command("energy_node", "set_external_limits", recorder)
    probe.start()
    await probe.wait_to_be_ready()
    return recorder


@pytest.fixture
def inverter():
    created: List[FakeInverter] = []

    def make(base: int, model_id: int, w: int, w_sf: int = 0) -> FakeInverter:
        fake = FakeInverter(base, model_id, w, w_sf)
        fake.start()
        created.append(fake)
        return fake

    yield make
    for fake in created:
        fake.stop()


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="sunspec_solar_limit")
@pytest.mark.everest_core_config("config-test-sunspec-solar-limit.yaml")
async def test_limits_charging_to_production_minus_reserve(everest_core: EverestCore, inverter):
    fake = inverter(base=40000, model_id=103, w=5000)
    recorder = await start(everest_core)

    await eventually(lambda: is_close(recorder.last(), 5000 - RESERVE_W))

    fake.set_power(25000, -1)
    await eventually(lambda: is_close(recorder.last(), 2500 - RESERVE_W))

    fake.set_power(-30, 0)
    await eventually(lambda: is_close(recorder.last(), 0))


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="sunspec_solar_limit")
@pytest.mark.everest_core_config("config-test-sunspec-solar-limit.yaml")
async def test_reads_model_701_at_base_zero(everest_core: EverestCore, inverter):
    inverter(base=0, model_id=701, w=3000)
    recorder = await start(everest_core)

    await eventually(lambda: is_close(recorder.last(), 3000 - RESERVE_W))


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="sunspec_solar_limit")
@pytest.mark.everest_core_config("config-test-sunspec-solar-limit.yaml")
async def test_applies_fallback_without_inverter(everest_core: EverestCore):
    recorder = await start(everest_core)

    await eventually(lambda: is_close(recorder.last(), FALLBACK_W))


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="sunspec_solar_limit")
@pytest.mark.everest_core_config("config-test-sunspec-solar-limit.yaml")
async def test_falls_back_when_inverter_stops_and_recovers(everest_core: EverestCore, inverter):
    fake = inverter(base=40000, model_id=103, w=5000)
    recorder = await start(everest_core)
    await eventually(lambda: is_close(recorder.last(), 5000 - RESERVE_W))

    fake.stop()
    await eventually(lambda: is_close(recorder.last(), FALLBACK_W))

    fake.set_power(4000, 0)
    fake.start()
    await eventually(lambda: is_close(recorder.last(), 4000 - RESERVE_W))
