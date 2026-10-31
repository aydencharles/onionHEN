#!/usr/bin/env python3
"""Summarize Rest Mode recovery cycles in an OnionHEN daemon log.

This is a read-only, standard-library helper for issue reports. It prints
event counts and timings without echoing log lines, which may contain private
paths or other identifying details.

Usage from the OnionHEN repository root:
    python scripts/analyze_rest_mode_log.py daemon_log.txt
    python scripts/analyze_rest_mode_log.py daemon_log.txt --json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import TextIO


TIMESTAMP_RE = re.compile(r"^\s*\[(?P<seconds>\d+(?:\.\d+)?)\]")

EVENTS = {
    "suspend": "power state -> SUSPEND_ON_GOING",
    "working": "power state -> WORKING",
    "listener_recovery_ready": "listener recovery ready",
    "shellui_scheduled": "resume compensation scheduling ShellUI",
    "toolbox_online": "Toolbox online for SceShellUI",
    "toolbox_activation": "Activating toolbox...",
}

SIGNALS = {
    "elf_mapping_or_protection": ("pt_mmap", "pt_mprotect"),
    "elf_synchronization": ("pt_msync",),
    "payload_load_or_arguments": ("Failed to load ELF", "Failed to allocate payload args"),
    "loader_recovery_failed": ("loader recovery failed",),
}


@dataclass
class Cycle:
    number: int
    standby_at: float | None
    suspend_at: float | None = None
    working_at: float | None = None
    listener_recovery_ready_at: float | None = None
    shellui_scheduled_at: float | None = None
    toolbox_online_at: float | None = None
    toolbox_activation_at: float | None = None
    logged_resume_delay_seconds: int | None = None
    warnings: int = 0
    errors: int = 0
    signals: dict[str, int] = field(default_factory=dict)

    def summary(self) -> dict[str, object]:
        """Return this cycle as a JSON-friendly summary of observed events."""
        elapsed = None
        if self.standby_at is not None and self.working_at is not None:
            delta = self.working_at - self.standby_at
            elapsed = round(delta, 3) if delta >= 0 else None
        schedule_to_activation = None
        if self.shellui_scheduled_at is not None and self.toolbox_activation_at is not None:
            delta = self.toolbox_activation_at - self.shellui_scheduled_at
            schedule_to_activation = round(delta, 3) if delta >= 0 else None
        return {
            "cycle": self.number,
            "standby_at": self.standby_at,
            "suspend_at": self.suspend_at,
            "working_at": self.working_at,
            "standby_to_working_seconds": elapsed,
            "listener_recovery_ready_at": self.listener_recovery_ready_at,
            "shellui_scheduled_at": self.shellui_scheduled_at,
            "toolbox_activation_at": self.toolbox_activation_at,
            "schedule_to_activation_seconds": schedule_to_activation,
            "logged_resume_delay_seconds": self.logged_resume_delay_seconds,
            "toolbox_online_at": self.toolbox_online_at,
            "warnings": self.warnings,
            "errors": self.errors,
            "signals": dict(sorted(self.signals.items())),
        }


def parse_log(lines: list[str]) -> list[dict[str, object]]:
    """Group daemon log lines into Rest Mode cycles and failure signals."""
    cycles: list[Cycle] = []
    current: Cycle | None = None

    for line in lines:
        match = TIMESTAMP_RE.match(line)
        timestamp = float(match.group("seconds")) if match else None
        lower = line.casefold()

        if "power state -> main_on_standby" in lower:
            if current is not None:
                cycles.append(current)
            current = Cycle(number=len(cycles) + 1, standby_at=timestamp)
            continue

        if current is None:
            continue

        for event, marker in EVENTS.items():
            if marker.casefold() in lower:
                attribute = f"{event}_at"
                if getattr(current, attribute) is None:
                    setattr(current, attribute, timestamp)

        delay_match = re.search(r"resume delay (\d+) s after trophy sprx", lower)
        if delay_match and current.logged_resume_delay_seconds is None:
            current.logged_resume_delay_seconds = int(delay_match.group(1))

        if " warn:" in lower or " warning:" in lower:
            current.warnings += 1
        if " error:" in lower or " fatal:" in lower:
            current.errors += 1

        for signal, markers in SIGNALS.items():
            if any(marker.casefold() in lower for marker in markers):
                current.signals[signal] = current.signals.get(signal, 0) + 1

    if current is not None:
        cycles.append(current)
    return [cycle.summary() for cycle in cycles]


def print_text(
    summaries: list[dict[str, object]], output: TextIO, expected_delay: int | None
) -> None:
    """Write readable cycle summaries and any requested delay comparison."""
    print(f"Rest Mode cycles found: {len(summaries)}", file=output)
    if not summaries:
        print("No MAIN_ON_STANDBY markers found.", file=output)
        return

    for cycle in summaries:
        print(f"\nCycle {cycle['cycle']} (standby at {cycle['standby_at']})", file=output)
        if cycle["suspend_at"] is not None:
            print(f"  suspend: {cycle['suspend_at']}", file=output)
        if cycle["working_at"] is not None:
            elapsed = cycle["standby_to_working_seconds"]
            suffix = f" ({elapsed}s after standby)" if elapsed is not None else ""
            print(f"  working: {cycle['working_at']}{suffix}", file=output)
        else:
            print("  working: not recorded before the next standby or end of log", file=output)

        for key, label in (
            ("listener_recovery_ready_at", "listener recovery ready"),
            ("shellui_scheduled_at", "ShellUI reinjection scheduled"),
            ("toolbox_activation_at", "Toolbox activation started"),
            ("toolbox_online_at", "Toolbox online"),
        ):
            value = cycle[key]
            rendered = value if value is not None else "not recorded"
            print(f"  {label}: {rendered}", file=output)

        elapsed = cycle["schedule_to_activation_seconds"]
        if elapsed is not None:
            print(
                f"  schedule-to-activation: {elapsed}s "
                "(also includes SPRX readiness and loader setup)",
                file=output,
            )
        if expected_delay is not None:
            logged_delay = cycle["logged_resume_delay_seconds"]
            if logged_delay is None:
                print("  configured delay in log: not recorded", file=output)
            else:
                relation = "matches" if logged_delay == expected_delay else "differs from"
                print(
                    f"  configured delay in log: {logged_delay}s ({relation} expected)",
                    file=output,
                )
            if elapsed is None:
                result = "cannot compare (missing timing markers)"
            elif elapsed < expected_delay:
                result = "shorter than expected; configured delay may not have taken effect"
            else:
                result = "not shorter than expected; this does not confirm the setting"
            print(f"  expected delay check ({expected_delay}s): {result}", file=output)

        print(f"  warnings/errors: {cycle['warnings']}/{cycle['errors']}", file=output)
        signals = cycle["signals"]
        rendered_signals = ", ".join(f"{name}={count}" for name, count in signals.items())
        print(
            f"  recognized failure signals: {rendered_signals if rendered_signals else 'none'}",
            file=output,
        )


def main() -> int:
    """Read the selected log input and emit the requested report format."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", help="log file path, or - to read from stdin")
    parser.add_argument("--json", action="store_true", help="write machine-readable JSON")
    parser.add_argument(
        "--expected-delay",
        type=int,
        metavar="SECONDS",
        help="compare schedule-to-first-activation time with the delay you configured",
    )
    args = parser.parse_args()
    if args.expected_delay is not None and not 0 <= args.expected_delay <= 255:
        parser.error("--expected-delay must be between 0 and 255")

    try:
        if args.log == "-":
            lines = sys.stdin.read().splitlines()
        else:
            lines = Path(args.log).read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        print(f"Could not read log: {exc}", file=sys.stderr)
        return 2

    summaries = parse_log(lines)
    if args.expected_delay is not None:
        for cycle in summaries:
            elapsed = cycle["schedule_to_activation_seconds"]
            logged_delay = cycle["logged_resume_delay_seconds"]
            cycle["expected_delay_seconds"] = args.expected_delay
            cycle["logged_delay_matches_expected"] = (
                None if logged_delay is None else logged_delay == args.expected_delay
            )
            cycle["activation_timing_check"] = (
                "insufficient_data"
                if elapsed is None
                else "shorter_than_expected"
                if elapsed < args.expected_delay
                else "not_contradicted"
            )
    if args.json:
        print(json.dumps({"cycles": summaries}, indent=2))
    else:
        print_text(summaries, sys.stdout, args.expected_delay)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
