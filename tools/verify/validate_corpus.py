#!/usr/bin/env python3
"""Run every format validator against the retail GoldSrc corpus.

This is the Stage 1 gate command:

    python tools/verify/validate_corpus.py --all

Each validator is a module in this folder exposing `validate(root, games)` and
returning a result dict with a `summary.hard_errors` count. A format that is
planned but has no validator yet is reported as `not implemented`, never as a
pass: the gate is only green when every format has one.

Exit code is 1 when any hard error exists.
"""

from __future__ import annotations

import argparse
import importlib
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))
sys.path.insert(0, str(REPO_ROOT / "tools" / "verify"))

from corpus_scan import discover_games, resolve_corpus_root, write_json  # noqa: E402

# Format -> validator module. Order follows Stage 1's dependency order.
VALIDATORS = {
    "wad3": "validate_wad3",
    "bsp30": "validate_bsp30",
    "mdl10": "validate_mdl10",
    "spr": "validate_spr",
}

# Formats Stage 1 still owes a validator. Listed so the gate cannot claim more
# coverage than it has.
PLANNED = ["pal/lmp/dem/res/cfg"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run the GoldSrc corpus validators.")
    parser.add_argument("--root", help="Half-Life install root (default: auto-detect).")
    parser.add_argument(
        "--all", action="store_true", help="Run every implemented validator (the gate)."
    )
    parser.add_argument(
        "formats", nargs="*", help="Subset of formats to run (default: every implemented one)."
    )
    parser.add_argument("--json", help="Write the aggregated result as JSON to this path.")
    args = parser.parse_args(argv)

    wanted = args.formats or list(VALIDATORS)
    unknown = [name for name in wanted if name not in VALIDATORS]
    if unknown:
        raise SystemExit(
            f"No validator for: {', '.join(unknown)}. Implemented: {', '.join(VALIDATORS)}"
        )

    root = resolve_corpus_root(args.root)
    games = discover_games(root)

    results: dict[str, dict] = {}
    total_hard = 0
    total_warnings = 0
    for name in wanted:
        module = importlib.import_module(VALIDATORS[name])
        result = module.validate(root, games)
        results[name] = result
        total_hard += result["summary"]["hard_errors"]
        total_warnings += result["summary"]["warnings"]

    width = max(len(name) for name in results)
    print(f"Corpus root: {root}")
    print(f"Game folders: {len(games)}")
    print("")
    print(f"{'Format':<{width}}  {'Files':>7}  {'Hard':>6}  {'Warnings':>9}")
    for name, result in results.items():
        summary = result["summary"]
        print(
            f"{name:<{width}}  {summary['validated']:>7,}  "
            f"{summary['hard_errors']:>6,}  {summary['warnings']:>9,}"
        )
    for name in PLANNED:
        print(f"{name:<{width}}  {'-':>7}  {'-':>6}  {'not implemented':>9}")

    print("")
    if total_hard:
        print(f"GATE FAILED: {total_hard:,} hard error(s).")
    else:
        print(f"GATE PARTIAL: 0 hard errors in the formats implemented so far; {len(PLANNED)} "
              "format group(s) still have no validator.")

    if args.json:
        write_json(
            Path(args.json),
            {
                "corpus_root_name": root.name,
                "games": sorted({game.folder for game in games}, key=str.lower),
                "results": results,
                "planned_not_implemented": PLANNED,
                "hard_errors": total_hard,
                "warnings": total_warnings,
            },
        )

    return 1 if total_hard else 0


if __name__ == "__main__":
    raise SystemExit(main())
