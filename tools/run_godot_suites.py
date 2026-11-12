#!/usr/bin/env python3
"""Run the Godot-side suites in demo/tests and exit non-zero when one fails.

The suites are scenes that print what they find and then quit. On their own they
always exit 0, so nothing could tell a broken run from a good one. This runner is
what makes them able to fail. A suite fails when:

  * Godot exits non-zero or does not finish within the timeout,
  * its output contains a line the suite itself marked `** FAILED`,
  * or Godot reports a script or parse error.

A suite that says it skipped something (usually because the game data it wants is
not mounted) is reported as `skipped`, with the line that said so.

Suites whose script never prints a failure marker are surveys: they only show
that the scene loads and runs to the end. They are reported as `ran`, never as
`passed`, so the summary cannot claim more than was checked.

    python tools/run_godot_suites.py                     # every verify_* suite
    python tools/run_godot_suites.py verify_api verify_dock
    python tools/run_godot_suites.py --godot "C:/path/to/godot.exe"

The Godot binary comes from --godot, then the GODOT_BIN environment variable,
then `godot` on PATH. Exit codes: 0 nothing failed, 1 at least one suite failed,
2 no Godot binary was found.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TESTS_DIR = REPO_ROOT / "demo" / "tests"

FAILURE_MARKERS = ("** FAILED", "SCRIPT ERROR", "Parse Error", "Failed to load script")
# What the suites print when the game data they need is not mounted and they give up
# without checking anything. Such a run proves nothing, so it is never `passed`.
SKIP_MARKERS = ("SKIP", "no player model found", "nothing to verify", "nothing to check",
                "not both are here")


def find_godot(explicit: str | None) -> str | None:
    for candidate in (explicit, os.environ.get("GODOT_BIN")):
        if candidate and Path(candidate).is_file():
            return candidate
    return shutil.which("godot") or shutil.which("godot4")


def has_checks(suite: str) -> bool:
    script = TESTS_DIR / f"{suite}.gd"
    return script.is_file() and "** FAILED" in script.read_text(encoding="utf-8", errors="replace")


def run_suite(godot: str, suite: str, timeout: int) -> tuple[str, list[str], float]:
    """Returns (verdict, evidence lines, seconds)."""
    command = [godot, "--headless", "--path", str(REPO_ROOT / "demo"), f"res://tests/{suite}.tscn"]
    started = time.monotonic()
    try:
        done = subprocess.run(command, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return "FAILED", [f"did not finish within {timeout} s"], time.monotonic() - started
    elapsed = time.monotonic() - started

    output = (done.stdout + done.stderr).decode("utf-8", errors="replace")
    evidence = [line.strip() for line in output.splitlines()
                if any(marker in line for marker in FAILURE_MARKERS)]
    if done.returncode != 0:
        evidence.insert(0, f"exit code {done.returncode}")
    if evidence:
        return "FAILED", evidence, elapsed
    skipped = [line.strip() for line in output.splitlines()
               if any(marker in line for marker in SKIP_MARKERS)]
    if skipped:
        return "skipped", skipped, elapsed
    return ("passed" if has_checks(suite) else "ran"), [], elapsed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run the Godot suites in demo/tests.")
    parser.add_argument("suites", nargs="*", help="Suite names (default: every verify_*.tscn).")
    parser.add_argument("--godot", help="Path to the Godot binary.")
    parser.add_argument("--timeout", type=int, default=600, help="Seconds per suite (default 600).")
    args = parser.parse_args(argv)

    godot = find_godot(args.godot)
    if godot is None:
        print("No Godot binary: pass --godot, set GODOT_BIN, or put godot on PATH.")
        return 2

    suites = args.suites or sorted(path.stem for path in TESTS_DIR.glob("verify_*.tscn"))
    missing = [suite for suite in suites if not (TESTS_DIR / f"{suite}.tscn").is_file()]
    if missing:
        print(f"No such suite: {', '.join(missing)}")
        return 1

    width = max(len(suite) for suite in suites)
    counts = {"passed": 0, "ran": 0, "skipped": 0, "FAILED": 0}
    for suite in suites:
        verdict, evidence, elapsed = run_suite(godot, suite, args.timeout)
        counts[verdict] += 1
        print(f"{suite:<{width}}  {verdict:<7}  {elapsed:6.1f} s", flush=True)
        for line in evidence[:10]:
            print(f"    {line}")
        if len(evidence) > 10:
            print(f"    ... {len(evidence) - 10} more")

    print("")
    print(f"{counts['passed']} passed, {counts['ran']} ran without checks (surveys), "
          f"{counts['skipped']} skipped part or all of their checks, {counts['FAILED']} failed.")
    return 1 if counts["FAILED"] else 0


if __name__ == "__main__":
    sys.exit(main())
