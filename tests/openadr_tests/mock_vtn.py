# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Minimal OpenADR 3.0 VTN for testing the RsOpenLEADR module.

Implements only what the openleadr-rs client uses: the OAuth client credentials
token endpoint, reading a program and listing events with pagination.
"""

import base64
import json
import secrets
import threading
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Dict, List, Optional
from urllib.parse import parse_qs, urlparse

PROGRAM_ID = "program-1"
CLIENT_ID = "ven-1"
CLIENT_SECRET = "ven-1-secret"


def iso(dt: datetime) -> str:
    return dt.astimezone(timezone.utc).isoformat()


def iso_duration(delta: timedelta) -> str:
    """ISO 8601 duration in the form openleadr-vtn uses, e.g. P0Y0M0DT0H20M0S."""
    minutes, seconds = divmod(int(delta.total_seconds()), 60)
    hours, minutes = divmod(minutes, 60)
    return f"P0Y0M0DT{hours}H{minutes}M{seconds}S"


def event(
    name: str,
    start: datetime,
    duration: timedelta,
    payload_type: str = "IMPORT_CAPACITY_LIMIT",
    value: float = 7.0,
    units: Optional[str] = "KW",
    priority: int = 0,
) -> dict:
    """An OpenADR event with a single interval and a single payload."""
    descriptor = {"payloadType": payload_type}
    if units is not None:
        descriptor["units"] = units
    return {
        "id": name,
        "createdDateTime": iso(start),
        "modificationDateTime": iso(start),
        "objectType": "EVENT",
        "programID": PROGRAM_ID,
        "eventName": name,
        "priority": priority,
        "targets": [{"type": "GROUP", "values": ["group-1"]}],
        "payloadDescriptors": [descriptor],
        "intervalPeriod": {"start": iso(start), "duration": iso_duration(duration)},
        "intervals": [{"id": 0, "payloads": [{"type": payload_type, "values": [value]}]}],
    }


@dataclass
class Request:
    method: str
    path: str
    query: Dict[str, List[str]]
    authorization: Optional[str]


@dataclass
class MockVtnState:
    events: List[dict] = field(default_factory=list)
    available: bool = True
    requests: List[Request] = field(default_factory=list)
    tokens: set = field(default_factory=set)
    lock: threading.Lock = field(default_factory=threading.Lock)


class MockVtn:
    def __init__(self):
        self.state = MockVtnState()
        self._server = ThreadingHTTPServer(("127.0.0.1", 0), _make_handler(self.state))
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)

    @property
    def url(self) -> str:
        return f"http://127.0.0.1:{self._server.server_address[1]}"

    def start(self):
        self._thread.start()

    def stop(self):
        self._server.shutdown()
        self._server.server_close()

    def set_events(self, events: List[dict]):
        with self.state.lock:
            self.state.events = list(events)

    def set_available(self, available: bool):
        """An unavailable VTN answers every request with 503."""
        with self.state.lock:
            self.state.available = available

    def requests(self, path_prefix: str = "") -> List[Request]:
        with self.state.lock:
            return [r for r in self.state.requests if r.path.startswith(path_prefix)]


def _make_handler(state: MockVtnState):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format, *args):
            pass

        def _record(self) -> Request:
            url = urlparse(self.path)
            request = Request(self.command, url.path, parse_qs(url.query), self.headers.get("Authorization"))
            with state.lock:
                state.requests.append(request)
            return request

        def _send_json(self, status: int, body):
            data = json.dumps(body).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def _problem(self, status: int, title: str):
            self._send_json(status, {"type": "about:blank", "title": title, "status": status})

        def _authorized(self, request: Request) -> bool:
            if not request.authorization or not request.authorization.startswith("Bearer "):
                return False
            with state.lock:
                return request.authorization[len("Bearer "):] in state.tokens

        def do_POST(self):
            request = self._record()
            self.rfile.read(int(self.headers.get("Content-Length", 0)))
            with state.lock:
                available = state.available
            if not available:
                return self._send_json(503, {"error": "temporarily_unavailable"})
            if request.path != "/auth/token":
                return self._problem(404, "Not Found")

            expected = base64.b64encode(f"{CLIENT_ID}:{CLIENT_SECRET}".encode()).decode()
            if request.authorization != f"Basic {expected}":
                return self._send_json(401, {"error": "invalid_client"})
            token = secrets.token_hex(16)
            with state.lock:
                state.tokens.add(token)
            self._send_json(200, {"access_token": token, "token_type": "Bearer", "expires_in": 3600})

        def do_GET(self):
            request = self._record()
            with state.lock:
                available = state.available
                events = list(state.events)
            if not available:
                return self._problem(503, "Service Unavailable")
            if not self._authorized(request):
                return self._problem(401, "Unauthorized")

            if request.path == f"/programs/{PROGRAM_ID}":
                return self._send_json(200, {
                    "id": PROGRAM_ID,
                    "createdDateTime": "2024-07-25T08:31:10.776+00:00",
                    "modificationDateTime": "2024-07-25T08:31:10.776+00:00",
                    "objectType": "PROGRAM",
                    "programName": PROGRAM_ID,
                    "targets": [{"type": "GROUP", "values": ["group-1"]}],
                })
            if request.path == "/events":
                skip = int(request.query.get("skip", ["0"])[0])
                limit = int(request.query.get("limit", ["50"])[0])
                return self._send_json(200, events[skip:skip + limit])
            return self._problem(404, "Not Found")

    return Handler
