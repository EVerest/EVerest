#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import asyncio
import socket
import struct
from datetime import datetime, timezone

import pytest
import yaml

from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule

PORT = 15502
BASE = 40000
MODEL_1 = BASE + 2
MODEL_701 = MODEL_1 + 68
END_MARKER = MODEL_701 + 155

# Model 701 point addresses
W = MODEL_701 + 10
A = MODEL_701 + 14
LNV = MODEL_701 + 16
HZ = MODEL_701 + 17
TOT_WH_INJ = MODEL_701 + 19
TOT_WH_ABS = MODEL_701 + 23
W_L1 = MODEL_701 + 41
W_L3 = MODEL_701 + 87
A_SF = MODEL_701 + 113

ILLEGAL_FUNCTION = 0x01
ILLEGAL_DATA_ADDRESS = 0x02

POWERMETER = {
    "timestamp": datetime.now(timezone.utc).isoformat(),
    "energy_Wh_import": {"total": 123456.0, "L1": 41152.0, "L2": 41152.0, "L3": 41152.0},
    "energy_Wh_export": {"total": 10.0},
    "power_W": {"total": 7200.0, "L1": 2400.0, "L2": 2400.0, "L3": 2400.0},
    "current_A": {"L1": 10.0, "L2": 10.5, "L3": 11.0},
    "voltage_V": {"L1": 230.0, "L2": 231.0, "L3": 232.0},
    "frequency_Hz": {"L1": 50.02},
}


class ModbusError(Exception):
    def __init__(self, code: int):
        super().__init__(f"Modbus exception {code}")
        self.code = code


class ModbusTcpClient:
    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.transaction = 0

    def _request(self, pdu: bytes) -> bytes:
        self.transaction = (self.transaction + 1) & 0xFFFF
        self.sock.sendall(struct.pack(">HHHB", self.transaction, 0, len(pdu) + 1, 1) + pdu)
        header = self._recv(7)
        _, _, length, _ = struct.unpack(">HHHB", header)
        response = self._recv(length - 1)
        if response[0] & 0x80:
            raise ModbusError(response[1])
        return response

    def _recv(self, count: int) -> bytes:
        data = b""
        while len(data) < count:
            chunk = self.sock.recv(count - len(data))
            if not chunk:
                raise ConnectionError("server closed the connection")
            data += chunk
        return data

    def read(self, address: int, count: int) -> list:
        response = self._request(struct.pack(">BHH", 0x03, address, count))
        return list(struct.unpack(f">{count}H", response[2:2 + 2 * count]))

    def write(self, address: int, value: int) -> None:
        self._request(struct.pack(">BHH", 0x06, address, value))

    def int16(self, address: int) -> int:
        return struct.unpack(">h", struct.pack(">H", self.read(address, 1)[0]))[0]

    def uint32(self, address: int) -> int:
        hi, lo = self.read(address, 2)
        return (hi << 16) | lo

    def uint64(self, address: int) -> int:
        value = 0
        for word in self.read(address, 4):
            value = (value << 16) | word
        return value

    def string(self, address: int, length: int) -> str:
        raw = b"".join(struct.pack(">H", word) for word in self.read(address, length))
        return raw.rstrip(b"\0").decode()


async def connect(timeout: float = 10.0) -> ModbusTcpClient:
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    while True:
        try:
            sock = socket.create_connection(("127.0.0.1", PORT), timeout=2)
            return ModbusTcpClient(sock)
        except OSError:
            if loop.time() > deadline:
                raise
            await asyncio.sleep(0.2)


async def eventually(condition, timeout: float = 10.0):
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    while not condition():
        if loop.time() > deadline:
            raise TimeoutError("condition not reached")
        await asyncio.sleep(0.2)


def interface_commands(everest_core: EverestCore, interface: str) -> list:
    path = everest_core.prefix_path / "share/everest/interfaces" / f"{interface}.yaml"
    return list((yaml.safe_load(path.read_text()).get("cmds") or {}).keys())


async def start(everest_core: EverestCore) -> ProbeModule:
    everest_core.start(standalone_module="probe")
    probe = ProbeModule(everest_core.get_runtime_session())
    # The probe must implement every evse_manager command; the server calls none of them.
    for command in interface_commands(everest_core, "evse_manager"):
        probe.implement_command("evse_manager", command, lambda _args: None)
    probe.start()
    await probe.wait_to_be_ready()
    return probe


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="sunspec_modbus_server")
@pytest.mark.everest_core_config("config-test-sunspec-modbus-server.yaml")
async def test_discovers_sunspec_device_with_common_and_701_models(everest_core: EverestCore):
    await start(everest_core)
    client = await connect()

    assert client.read(BASE, 2) == [0x5375, 0x6E53]
    assert client.read(MODEL_1, 2) == [1, 66]
    assert client.string(MODEL_1 + 2, 16) == "Example Inc"
    assert client.string(MODEL_1 + 50, 16) == "SN-TEST"
    assert client.read(MODEL_701, 3) == [701, 153, 2]
    assert client.read(END_MARKER, 2) == [0xFFFF, 0]


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="sunspec_modbus_server")
@pytest.mark.everest_core_config("config-test-sunspec-modbus-server.yaml")
async def test_reports_evse_powermeter_in_model_701(everest_core: EverestCore):
    probe = await start(everest_core)
    client = await connect()
    assert client.read(W, 1) == [0x8000]

    probe.publish_variable("evse_manager", "powermeter", POWERMETER)

    await eventually(lambda: client.int16(W) == -7200)
    assert client.int16(W_L1) == -2400
    assert client.int16(W_L3) == -2400
    assert client.int16(A_SF) == -2
    assert client.int16(A) == 3150
    assert client.read(LNV, 1) == [2310]
    assert client.uint32(HZ) == 5002
    assert client.uint64(TOT_WH_ABS) == 123456
    assert client.uint64(TOT_WH_INJ) == 10


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="sunspec_modbus_server")
@pytest.mark.everest_core_config("config-test-sunspec-modbus-server.yaml")
async def test_rejects_writes_and_reads_outside_the_map(everest_core: EverestCore):
    await start(everest_core)
    client = await connect()

    with pytest.raises(ModbusError) as write_error:
        client.write(W, 1)
    assert write_error.value.code == ILLEGAL_FUNCTION

    with pytest.raises(ModbusError) as read_error:
        client.read(END_MARKER, 3)
    assert read_error.value.code == ILLEGAL_DATA_ADDRESS

    assert client.read(BASE, 2) == [0x5375, 0x6E53]


@pytest.mark.asyncio
@pytest.mark.xdist_group(name="sunspec_modbus_server")
@pytest.mark.everest_core_config("config-test-sunspec-modbus-server.yaml")
async def test_keeps_serving_after_clients_disconnect(everest_core: EverestCore):
    await start(everest_core)

    for _ in range(10):
        client = await connect()
        assert client.read(BASE, 2) == [0x5375, 0x6E53]
        client.sock.close()

    client = await connect()
    assert client.read(MODEL_701, 2) == [701, 153]
