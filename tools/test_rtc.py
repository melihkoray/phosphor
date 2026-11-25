#!/usr/bin/env python3
"""Compile the actual RTC-related functions from two Git revisions with host stubs.

The harness extracts syncTime(), Net_SetTimezone(), and the SNTP callback
registration expression from firmware/src/core/net.cpp. It does not reimplement
their scheduling logic. The old revision is a baseline expected to demonstrate
the pre-NTP write; the fixed revision must satisfy the intended write sequence.
Requires git and a C++17 compiler.
"""

import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile


SOURCE = "firmware/src/core/net.cpp"


def git_source(repo, revision):
    return subprocess.run(
        ["git", "-C", str(repo), "show", f"{revision}:{SOURCE}"],
        check=True, text=True, capture_output=True,
    ).stdout


def cpp_block(source, marker, opening):
    """Return a balanced C++ {...} or (...) block starting at opening."""
    start = source.find(marker)
    if start < 0:
        return None
    begin = source.find(opening, start + len(marker))
    if begin < 0:
        raise ValueError(f"opening {opening!r} not found after {marker!r}")
    close = "}" if opening == "{" else ")"
    depth, i, state = 0, begin, "code"
    while i < len(source):
        c = source[i]
        n = source[i + 1] if i + 1 < len(source) else ""
        if state == "code":
            if c == '/' and n == '/':
                state, i = "line", i + 2
                continue
            if c == '/' and n == '*':
                state, i = "block", i + 2
                continue
            if c == '"':
                state = "string"
            elif c == "'":
                state = "char"
            elif c == opening:
                depth += 1
            elif c == close:
                depth -= 1
                if depth == 0:
                    return source[begin:i + 1]
        elif state == "line":
            if c == "\n":
                state = "code"
        elif state == "block":
            if c == '*' and n == '/':
                state, i = "code", i + 2
                continue
        elif state in ("string", "char"):
            if c == "\\":
                i += 2
                continue
            if (state == "string" and c == '"') or (state == "char" and c == "'"):
                state = "code"
        i += 1
    raise ValueError(f"unbalanced block after {marker!r}")


def extract_function(source, signature):
    match = re.search(re.escape(signature) + r"\s*\{", source)
    if not match:
        raise ValueError(f"function not found: {signature}")
    block = cpp_block(source, signature, "{")
    return signature + " " + block


def extract_callback_registration(source):
    marker = "sntp_set_time_sync_notification_cb("
    if marker not in source:
        return "", False
    args = cpp_block(source, marker[:-1], "(")
    return f"void registerSntpCallback() {{ sntp_set_time_sync_notification_cb{args}; }}", True


def harness(source, revision):
    sync = extract_function(source, "static void syncTime()")
    timezone = extract_function(source, "void Net_SetTimezone(const char *posix_tz)")
    callback, has_callback = extract_callback_registration(source)
    if not callback:
        callback = "void registerSntpCallback() {}"
    return r'''#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

constexpr uint32_t TIME_RESYNC_MS = 6UL * 3600 * 1000;
enum : uint32_t { SYNC_NTP = 1, SYNC_ZONE = 2 };
std::atomic<uint32_t> sync_wanted{0};
uint32_t time_synced_ms = 0;
uint32_t fake_ms = 100;
uint32_t millis() { return fake_ms; }
bool clock_available = true;
struct tm clock_value = {};
bool getLocalTime(struct tm *out, uint32_t) {
  if (!clock_available) return false;
  *out = clock_value;
  return true;
}
struct DateTime { int year, month, day, hour, minute, second; };
struct FakeBoard {
  std::vector<DateTime> writes;
  void setTime(DateTime t) { writes.push_back(t); }
};
FakeBoard fake_board;
FakeBoard &board() { return fake_board; }
struct FakePrefs {
  std::string getString(const char *, const char *fallback) { return fallback; }
  void putString(const char *, const char *) {}
} prefs;
struct FakeSerial {
  template <typename... Args> void printf(const char *, Args...) {}
} Serial;
std::function<void(struct timeval *)> sntp_callback;
void sntp_set_time_sync_notification_cb(std::function<void(struct timeval *)> cb) {
  sntp_callback = cb;
}
'''+sync+r'''
'''+timezone+r'''
'''+callback+r'''

static void setClock(int year, int month, int day, int hour, int minute, int second) {
  clock_value = {};
  clock_value.tm_year = year - 1900;
  clock_value.tm_mon = month - 1;
  clock_value.tm_mday = day;
  clock_value.tm_hour = hour;
  clock_value.tm_min = minute;
  clock_value.tm_sec = second;
}
static void mark(const char *label) {
  std::cout << label << '=' << fake_board.writes.size() << '\n';
}
int main() {
  registerSntpCallback();
  // A plausible but stale system clock can survive a software restart. It
  // must not be copied into the RTC until SNTP confirms a fresh time.
  setClock(2025, 1, 2, 3, 4, 5);
  syncTime();
  mark("before_ntp");

  Net_SetTimezone("UTC0");
  syncTime();
  mark("timezone_before_ntp");

  setClock(2026, 11, 25, 12, 13, 14);
  if (sntp_callback) { struct timeval tv{}; sntp_callback(&tv); }
  mark("callback_only");
  fake_ms += 1000;
  syncTime();
  mark("after_ntp");
  syncTime();
  mark("poll_without_ntp");

  setClock(2026, 11, 25, 12, 14, 14);
  if (sntp_callback) { struct timeval tv{}; sntp_callback(&tv); }
  mark("next_callback_only");
  fake_ms += 1000;
  syncTime();
  mark("next_ntp");

  Net_SetTimezone("EST5");
  setClock(2026, 11, 25, 7, 14, 14);
  syncTime();
  mark("timezone_after_ntp");
  for (const auto &t : fake_board.writes)
    std::cout << "write=" << t.year << '-' << t.month << '-' << t.day << 'T'
              << t.hour << ':' << t.minute << ':' << t.second << '\n';
}
''', has_callback


def run_revision(repo, revision, compiler):
    source = (Path(repo) / SOURCE).read_text() if revision == "WORKTREE" else git_source(repo, revision)
    code, has_callback = harness(source, revision)
    with tempfile.TemporaryDirectory(prefix="rtc-regression-") as temp:
        cpp = Path(temp) / "rtc_test.cpp"
        binary = Path(temp) / "rtc_test"
        cpp.write_text(code)
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", str(cpp), "-o", str(binary)], check=True)
        output = subprocess.run([str(binary)], check=True, text=True, capture_output=True).stdout
    counts, writes = {}, []
    for line in output.splitlines():
        if line.startswith("write="):
            writes.append(line[6:])
        else:
            key, value = line.split("=", 1)
            counts[key] = int(value)
    return {"revision": revision, "has_sntp_callback": has_callback,
            "write_counts": counts, "writes": writes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--old", default="4524626a31975dd51b442edacfd0264057f1cf63", help="pre-fix regression baseline")
    parser.add_argument("--fixed", default="WORKTREE", help="fixed revision, or WORKTREE for current source")
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    old = run_revision(args.repo, args.old, args.cxx)
    fixed = run_revision(args.repo, args.fixed, args.cxx)
    expected = {"before_ntp": 0, "timezone_before_ntp": 0, "callback_only": 0, "after_ntp": 1,
                "poll_without_ntp": 1, "next_callback_only": 1, "next_ntp": 2, "timezone_after_ntp": 3}
    expected_fixed_writes = ["2026-11-25T12:13:14", "2026-11-25T12:14:14",
                             "2026-11-25T7:14:14"]
    baseline_proves_regression = (
        old["write_counts"].get("before_ntp", 0) > 0
        and old["write_counts"].get("timezone_before_ntp", 0)
        > old["write_counts"].get("before_ntp", 0)
        and not old["has_sntp_callback"]
    )
    result = {
        "passed": (baseline_proves_regression
                   and fixed["write_counts"] == expected
                   and fixed["writes"] == expected_fixed_writes
                   and fixed["has_sntp_callback"]),
        "baseline_proves_pre_ntp_write": baseline_proves_regression,
        "expectation": expected,
        "expected_fixed_writes": expected_fixed_writes,
        "baseline_old": old,
        "fixed": fixed,
    }
    rendered = json.dumps(result, indent=2)
    print(rendered)
    if args.output:
        args.output.write_text(rendered + "\n")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
