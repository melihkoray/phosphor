#!/usr/bin/env python3
"""Hardware regression for Phosphor's /update and /assets authorization.

Reinstalls --firmware and reboots the device once. Use a known-good image for
this device; no asset pack is written. The token is read from an environment
variable and is never printed. Only Python's standard library is required.
"""

import argparse
import hashlib
import http.client
import json
import os
from pathlib import Path
import socket
import time
from urllib.parse import urlsplit


BOUNDARY = "phosphor-ota-regression-boundary"


def multipart(filename, data, field="firmware"):
    prefix = (
        f"--{BOUNDARY}\r\n"
        f'Content-Disposition: form-data; name="{field}"; filename="{filename}"\r\n'
        "Content-Type: application/octet-stream\r\n\r\n"
    ).encode("ascii")
    suffix = f"\r\n--{BOUNDARY}--\r\n".encode("ascii")
    return prefix, data, suffix


class Device:
    def __init__(self, url, token):
        parsed = urlsplit(url)
        if (parsed.scheme != "http" or not parsed.hostname or parsed.username
                or parsed.path not in ("", "/") or parsed.query or parsed.fragment):
            raise ValueError("use a device URL such as http://192.168.1.14")
        if not token or "\r" in token or "\n" in token:
            raise ValueError("the token environment variable must contain a nonempty token")
        token.encode("ascii")
        self.host, self.port = parsed.hostname, parsed.port or 80
        self.authority, self.token = parsed.netloc, token

    def request(self, method, path, body=None, headers=None, timeout=10):
        client = http.client.HTTPConnection(self.host, self.port, timeout=timeout)
        try:
            client.request(method, path, body=body, headers=headers or {})
            response = client.getresponse()
            return response.status, response.read()
        finally:
            client.close()

    def status(self, timeout=3):
        code, body = self.request("GET", "/status", timeout=timeout)
        if code != 200:
            raise RuntimeError(f"/status returned HTTP {code}")
        value = json.loads(body)
        if not isinstance(value.get("uptime_s"), int):
            raise RuntimeError("/status has no uptime_s")
        return value

    def wait_ready(self, seconds=20):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                return self.status(timeout=2)
            except (OSError, http.client.HTTPException, ValueError, RuntimeError):
                time.sleep(0.25)
        raise RuntimeError("device did not become ready")

    def wait_settled(self):
        # A reboot must make uptime fall below every negative-test baseline.
        # Allow the board to finish booting before testing that invariant.
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            status = self.wait_ready()
            if status["uptime_s"] >= 30:
                return status
            time.sleep(0.5)
        raise RuntimeError("device did not reach 30 seconds of stable uptime")

    def abort_upload(self, firmware):
        # A valid header reaches UPLOAD_FILE_START. Less than one flash sector
        # is sent, and the declared multipart body is deliberately left short.
        prefix, _, suffix = multipart("interrupted.bin", b"")
        partial = firmware[:2048]
        length = len(prefix) + len(firmware) + len(suffix)
        headers = (
            "POST /update HTTP/1.1\r\n"
            f"Host: {self.authority}\r\n"
            f"X-Token: {self.token}\r\n"
            f"Content-Type: multipart/form-data; boundary={BOUNDARY}\r\n"
            f"Content-Length: {length}\r\nConnection: close\r\n\r\n"
        ).encode("ascii")
        with socket.create_connection((self.host, self.port), timeout=10) as connection:
            connection.sendall(headers + prefix + partial)
            connection.shutdown(socket.SHUT_WR)
            # Wait for the server to notice the truncated body. A normal
            # request handler is not expected for an aborted multipart body.
            connection.settimeout(10)
            while connection.recv(4096):
                pass


def run(device, firmware, results):
    start = device.wait_settled()
    if not isinstance(start.get("fw"), str) or not start["fw"]:
        raise RuntimeError("/status has no firmware version")
    code, settings = device.request("GET", "/api/settings")
    if code != 200:
        raise RuntimeError("cannot save settings before the test")
    results["firmware_before"] = start.get("fw")
    results["checks"] = []

    def check(label, route, token, expected, file_data=None):
        before = device.status()
        headers = {} if token is None else {"X-Token": token}
        body = b""
        if file_data is not None:
            field = "firmware" if route == "/update" else "assets"
            body = b"".join(multipart("test.bin", file_data, field))
            headers["Content-Type"] = f"multipart/form-data; boundary={BOUNDARY}"
        code, _ = device.request("POST", route, body, headers)
        # restartSoon() waits 300 ms. Check after that interval as well as the
        # response code, so an accidental restart cannot pass unnoticed.
        time.sleep(0.6)
        after = device.wait_ready()
        no_restart = (after["uptime_s"] >= before["uptime_s"]
                      and after.get("fw") == start["fw"])
        passed = code == expected and no_restart
        results["checks"].append({"name": label, "status": code, "expected": expected,
                                  "no_restart": no_restart,
                                  "passed": passed})
        print(f"{'PASS' if passed else 'FAIL'} {label}: HTTP {code}", flush=True)
        if not passed:
            raise RuntimeError(f"{label}: expected HTTP {expected} without a restart")

    wrong_token = "wrong-token" if device.token != "wrong-token" else "another-wrong-token"

    def unauthorized(label):
        for route in ("/update", "/assets"):
            for token, name in ((None, "missing token"), (wrong_token, "wrong token")):
                check(f"{label}: {route}, {name}, no file", route, token, 401)

    unauthorized("initial")
    for route in ("/update", "/assets"):
        check(f"{route}, missing token, multipart", route, None, 401, b"invalid")
        check(f"{route}, wrong token, multipart", route, wrong_token, 401, b"invalid")
        check(f"{route}, valid token, no file", route, device.token, 400)

    # Populate the old global authorization flag, then send independent
    # requests with no file (therefore no UPLOAD_FILE_START callback).
    check("authorized invalid firmware", "/update", device.token, 500, b"invalid firmware")
    unauthorized("after rejected firmware")

    before_abort = device.status()
    device.abort_upload(firmware)
    after_abort = device.wait_ready()
    if (after_abort["uptime_s"] < before_abort["uptime_s"]
            or after_abort.get("fw") != start["fw"]):
        raise RuntimeError("aborted upload restarted the device")
    results["checks"].append({"name": "aborted firmware upload", "passed": True})
    print("PASS aborted firmware upload: device remains available", flush=True)
    unauthorized("after aborted firmware")
    check("after abort: valid token, no file", "/update", device.token, 400)

    before_retry = device.status()
    body = b"".join(multipart("firmware.bin", firmware))
    code, _ = device.request("POST", "/update", body, {
        "X-Token": device.token,
        "Content-Type": f"multipart/form-data; boundary={BOUNDARY}",
    }, timeout=120)
    if code != 200:
        raise RuntimeError(f"valid retry returned HTTP {code}")
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        after = device.wait_ready()
        if after["uptime_s"] < before_retry["uptime_s"]:
            break
        time.sleep(0.5)
    else:
        raise RuntimeError("valid retry did not restart the device")
    results["firmware_after"] = after.get("fw")
    if after.get("fw") != start["fw"]:
        raise RuntimeError("the retry booted a different firmware version")
    results["checks"].append({"name": "successful firmware retry and reboot", "passed": True})
    print("PASS successful firmware retry and reboot", flush=True)
    device.wait_settled()
    unauthorized("after successful update")
    code, after_settings = device.request("GET", "/api/settings")
    if code != 200 or after_settings != settings:
        raise RuntimeError("saved settings changed")
    results["checks"].append({"name": "saved settings unchanged", "passed": True})
    print("PASS saved settings unchanged", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("device", help="device base URL, for example http://192.168.1.14")
    parser.add_argument("--firmware", type=Path, required=True,
                        help="known-good application .bin to reinstall; causes one reboot")
    parser.add_argument("--firmware-sha256", required=True,
                        help="expected SHA-256 of the known-good image, checked before connecting")
    parser.add_argument("--token-env", default="PHOSPHOR_OTA_TOKEN",
                        help="environment variable containing OTA_TOKEN (never printed)")
    parser.add_argument("--output", type=Path, help="optional JSON result file")
    args = parser.parse_args()
    firmware = args.firmware.read_bytes()
    if len(firmware) < 4096 or firmware[0] != 0xE9:
        parser.error("--firmware must be a complete ESP32 application image")
    digest = hashlib.sha256(firmware).hexdigest()
    if digest != args.firmware_sha256.lower():
        parser.error("--firmware does not match --firmware-sha256")
    try:
        device = Device(args.device, os.environ.get(args.token_env, ""))
    except (ValueError, UnicodeError) as error:
        parser.error(str(error))
    results = {"passed": False, "image_sha256": digest}
    try:
        run(device, firmware, results)
        results["passed"] = True
    except (OSError, http.client.HTTPException, ValueError, RuntimeError) as error:
        results["error"] = str(error)
        print(f"FAIL: {error}", flush=True)
    finally:
        if args.output:
            args.output.write_text(json.dumps(results, indent=2) + "\n")
    return 0 if results["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
