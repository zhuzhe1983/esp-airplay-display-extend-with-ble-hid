#!/usr/bin/env python3
"""Save Wi-Fi settings over the device's USB Serial/JTAG port."""
import argparse
import getpass
import time
import urllib.parse

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--ssid", required=True)
    parser.add_argument("--name", default="P4 Display")
    args = parser.parse_args()
    password = getpass.getpass("Wi-Fi password (empty for open network): ")
    payload = urllib.parse.urlencode({"ssid": args.ssid, "password": password, "name": args.name})
    # Configure idle lines before opening so macOS does not put P4 into reset.
    port = serial.Serial(port=None, baudrate=115200, timeout=0.2, exclusive=True)
    port.dtr = False
    port.rts = False
    port.port = args.port
    try:
        port.open()
        deadline = time.monotonic() + 35
        received = bytearray()
        while time.monotonic() < deadline:
            port.write(b"\nP4-PING\n")
            until = time.monotonic() + 1
            while time.monotonic() < until:
                received.extend(port.read(min(max(port.in_waiting, 1), 4096)))
                if b"P4_PONG" in received:
                    break
            if b"P4_PONG" in received:
                break
        else:
            parser.error("Device configuration interface is not ready.")
        port.write(("\nP4-WIFI " + payload + "\n").encode())
        port.flush()
        received = bytearray()
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            received.extend(port.read(min(max(port.in_waiting, 1), 4096)))
            if b"P4_WIFI_OK" in received:
                print("Settings saved. The device is restarting.")
                return 0
            if b"P4_WIFI_ERROR" in received:
                parser.error("The device rejected the settings; check SSID, password length and name.")
        parser.error("No acknowledgement; confirm the firmware has USB provisioning enabled and has finished booting.")
    finally:
        port.close()


if __name__ == "__main__":
    raise SystemExit(main())
