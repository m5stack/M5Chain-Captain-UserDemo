#!/usr/bin/env python3

from __future__ import annotations

import argparse
import asyncio
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any
from uuid import uuid4

import uvicorn
import zmq
import zmq.asyncio
from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles


DEFAULT_ZMQ_ENDPOINT = "tcp://127.0.0.1:5557"
DEFAULT_HTTP_HOST = "127.0.0.1"
DEFAULT_HTTP_PORT = 8765
REQUEST_TIMEOUT_SECONDS = 0.6
WEB_ROOT = Path(__file__).resolve().parent / "web"


class SimulatorUnavailable(RuntimeError):
    pass


class ZmqBridge:
    def __init__(self, endpoint: str) -> None:
        self._endpoint = endpoint
        self._context = zmq.asyncio.Context()
        self._socket = self._new_socket()
        self._lock = asyncio.Lock()

    def _new_socket(self) -> zmq.asyncio.Socket:
        socket = self._context.socket(zmq.REQ)
        socket.setsockopt(zmq.LINGER, 0)
        socket.setsockopt(zmq.IMMEDIATE, 1)
        socket.connect(self._endpoint)
        return socket

    def _reset_socket(self) -> None:
        self._socket.close(linger=0)
        self._socket = self._new_socket()

    async def forward(self, message: dict[str, Any]) -> dict[str, Any]:
        envelope = dict(message)
        envelope["version"] = 1
        envelope["id"] = str(uuid4())

        async with self._lock:
            try:
                await asyncio.wait_for(
                    self._socket.send_json(envelope), REQUEST_TIMEOUT_SECONDS
                )
                reply = await asyncio.wait_for(
                    self._socket.recv_json(), REQUEST_TIMEOUT_SECONDS
                )
            except (asyncio.TimeoutError, zmq.ZMQError, ValueError) as error:
                self._reset_socket()
                raise SimulatorUnavailable from error

        if not isinstance(reply, dict):
            raise SimulatorUnavailable
        return reply

    def close(self) -> None:
        self._socket.close(linger=0)
        self._context.term()


def create_app(endpoint: str = DEFAULT_ZMQ_ENDPOINT) -> FastAPI:
    bridge = ZmqBridge(endpoint)

    @asynccontextmanager
    async def lifespan(_: FastAPI):
        yield
        bridge.close()

    app = FastAPI(title="Chain Captain Debugger", lifespan=lifespan)
    app.mount("/static", StaticFiles(directory=WEB_ROOT), name="static")

    @app.get("/", include_in_schema=False)
    async def index() -> FileResponse:
        return FileResponse(WEB_ROOT / "index.html")

    @app.get("/api/status")
    async def status() -> dict[str, Any]:
        try:
            reply = await bridge.forward({"type": "ping"})
        except SimulatorUnavailable as error:
            raise HTTPException(status_code=503, detail="simulator_offline") from error
        return reply

    @app.post("/api/message")
    async def message(request: Request) -> dict[str, Any]:
        try:
            payload = await request.json()
        except ValueError as error:
            raise HTTPException(status_code=400, detail="invalid_json") from error
        if not isinstance(payload, dict):
            raise HTTPException(status_code=400, detail="invalid_message")

        try:
            reply = await bridge.forward(payload)
        except SimulatorUnavailable as error:
            raise HTTPException(status_code=503, detail="simulator_offline") from error
        return reply

    return app


def main() -> None:
    parser = argparse.ArgumentParser(description="Run the Chain Captain web debugger")
    parser.add_argument("--host", default=DEFAULT_HTTP_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_HTTP_PORT)
    parser.add_argument("--endpoint", default=DEFAULT_ZMQ_ENDPOINT)
    args = parser.parse_args()
    uvicorn.run(create_app(args.endpoint), host=args.host, port=args.port)


if __name__ == "__main__":
    main()
