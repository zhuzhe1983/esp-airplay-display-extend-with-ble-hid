#!/usr/bin/env python3
"""Measure Mac -> P4 goodput through bounded, acknowledged RTSP OPTIONS bodies.

This includes TCP, Wi-Fi and the receiver's control parser, not AVC decoding.
It is a lower bound on network capacity, not an iperf or PHY rate measurement.
No pairing, SETUP, credentials or firmware changes are sent.
"""
import argparse
import concurrent.futures
import json
import plistlib
import socket
import statistics
import struct
import sys
import time
from pathlib import Path


class Connection:
    def __init__(self, host, port):
        self.socket = socket.create_connection((host, port), timeout=5)
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.pending = b""

    def close(self):
        self.socket.close()

    def response(self):
        while b"\r\n\r\n" not in self.pending:
            if len(self.pending) > 8192:
                raise ValueError("Response header too large")
            self.read()
        header, self.pending = self.pending.split(b"\r\n\r\n", 1)
        if header.split(b"\r\n", 1)[0].split()[1:2] != [b"200"]:
            raise ValueError(f"Probe rejected: {header[:80]!r}")
        lengths = [line.split(b":", 1)[1] for line in header.split(b"\r\n")
                   if line.lower().startswith(b"content-length:")]
        if len(lengths) != 1:
            raise ValueError("Response must have one Content-Length")
        length = int(lengths[0])
        if not 0 <= length <= 65536:
            raise ValueError("Response body too large")
        while len(self.pending) < length:
            self.read()
        body, self.pending = self.pending[:length], self.pending[length:]
        return body

    def read(self):
        chunk = self.socket.recv(8192)
        if not chunk:
            raise ConnectionError("Receiver closed the probe connection")
        self.pending += chunk

    def tcp_info(self):
        if sys.platform != "darwin":
            return {}
        try:
            # Public macOS netinet/tcp.h, TCP_CONNECTION_INFO. Newer kernels
            # may append fields; only parse the stable prefix when available.
            data = self.socket.getsockopt(socket.IPPROTO_TCP, 0x106, 256)
            fields = ["options", "flags", "rto_ms", "mss", "ssthresh", "cwnd_bytes",
                      "send_window_bytes", "send_buffer_bytes", "recv_window_bytes",
                      "rtt_current_ms", "rtt_average_ms", "rtt_variance_ms", "tfo_flags"]
            result = dict(zip(fields, struct.unpack_from("=13I", data, 4)))
            counters = ["tx_packets", "tx_bytes", "tx_retransmitted_bytes", "rx_packets",
                        "rx_bytes", "rx_out_of_order_bytes", "tx_retransmitted_packets"]
            count = min(len(counters), max(0, (len(data) - 56) // 8))
            result.update(zip(counters, struct.unpack_from(f"={count}Q", data, 56)))
            return result
        except (OSError, struct.error):
            return {}


def measure(host, port, duration, stream):
    connection = Connection(host, port)
    body = b"Z" * (60 * 1024)
    confirmed = 0
    started = time.perf_counter()
    error = None
    try:
        try:
            while time.perf_counter() - started < duration:
                batch = [f"OPTIONS * RTSP/1.0\r\nCSeq: {confirmed + i + 1}\r\n"
                         f"Content-Length: {len(body)}\r\n\r\n".encode() + body for i in range(4)]
                connection.socket.sendall(b"".join(batch))
                for _ in batch:
                    connection.response()
                    confirmed += 1
        except (OSError, ConnectionError, ValueError) as exc:
            error = str(exc)
        ended = time.perf_counter()
        seconds = ended - started
        return {"stream": stream, "completed": error is None, "error": error, "seconds": round(seconds, 3),
                "confirmed_payload_bytes": confirmed * len(body),
                "payload_mbps": round(confirmed * len(body) * 8 / seconds / 1e6, 3),
                "tcp_info": connection.tcp_info()}
    finally:
        connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=7000)
    parser.add_argument("--seconds", type=float, default=5)
    parser.add_argument("--streams", type=int, choices=[1, 2], default=1)
    parser.add_argument("--expect-name")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 30:
        parser.error("--seconds must be between 1 and 30")
    connection = Connection(args.host, args.port)
    try:
        connection.socket.sendall(b"GET /info RTSP/1.0\r\nCSeq: 0\r\n\r\n")
        descriptor = plistlib.loads(connection.response())
        name = descriptor["name"]
        if args.expect_name and name != args.expect_name:
            raise ValueError(f"Unexpected receiver {name!r}; expected {args.expect_name!r}")
        identity = {"name": name, "device_id": descriptor["deviceID"],
                    "address": connection.socket.getpeername()[0]}
        samples = []
        for sequence in range(1, 11):
            started = time.perf_counter()
            connection.socket.sendall(f"OPTIONS * RTSP/1.0\r\nCSeq: {sequence}\r\n\r\n".encode())
            connection.response()
            samples.append((time.perf_counter() - started) * 1000)
    finally:
        connection.close()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.streams) as pool:
        rows = list(pool.map(lambda i: measure(args.host, args.port, args.seconds, i + 1),
                             range(args.streams)))
    completed = all(row["completed"] for row in rows)
    result = {"receiver": identity, "method": "acknowledged 60 KiB RTSP OPTIONS bodies, four per batch",
              "direction": "Mac to P4", "includes_control_parser": True,
              "control_rtt_ms": {"min": round(min(samples), 2), "median": round(statistics.median(samples), 2),
                                 "max": round(max(samples), 2)},
              "streams": rows, "all_streams_completed": completed,
              "sum_stream_mbps": round(sum(row["payload_mbps"] for row in rows), 3) if completed else None}
    text = json.dumps(result, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n")
    print(text)


if __name__ == "__main__":
    main()
