#!/usr/bin/env python3
"""Build the entity catalogue from the GoldSrc FGD collection.

A map is a list of entities and every entity is a bag of strings. `ambient_generic` has a
`health` of 8 and a `spawnflags` of 17, and nothing in the .bsp says that health is the volume
on a scale of ten, or that bit 1 means play everywhere and bit 16 means start silent.

An FGD says all of it. It is the schema the level editor uses, written by the people who made
the game:

    @PointClass base(Targetname) = ambient_generic : "Universal sound Ambient"
    [
        message(sound) : "Path/filename.wav of WAV"
        health(integer) : "Volume (10 = loudest)" : 10
        spawnflags(flags) =
        [
            1 : "Play Everywhere" : 0
            16 : "Start Silent" : 0
        ]
    ]

Quebratsk deduced some of this by reading engine source and guessing at the rest. This reads
it instead, and covers entities nobody has looked at: the campaign classes in Condition Zero,
the Opposing Force additions, everything a mod declares.

Writes demo/addons/quebratsk_editor/data/goldsrc_entities.json. The output is committed, so
using Quebratsk needs neither this script nor network access.
"""

import base64
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = "twhl-community/GoldSrcFGDCollection"
OUT = Path(__file__).resolve().parent.parent / "demo/addons/quebratsk_editor/data/goldsrc_entities.json"

# One FGD per game, and the order matters: a later file adds to what an earlier one said
# rather than replacing it, so the base game goes first and the mods build on top.
WANTED = [
    ("valve", "Half-Life/half-life.fgd"),
    ("cstrike", "Counter-Strike/counter-strike.fgd"),
    ("czero", "Counter-Strike Condition Zero Deleted Scenes/cs-cz.fgd"),
    ("czeror", "Counter-Strike Condition Zero Deleted Scenes/cs-cz-ds.fgd"),
    ("gearbox", "Opposing Force/op4.fgd"),
    ("bshift", "Blue Shift/bshift.fgd"),
    ("dod", "Day of Defeat/dod.fgd"),
]


def fetch(path: str) -> str:
    raw = subprocess.run(
        ["gh", "api", f"repos/{REPO}/contents/{path}", "--jq", ".content"],
        capture_output=True, text=True, check=True).stdout
    return base64.b64decode(raw).decode("utf-8", "replace")


def strip_comments(text: str) -> str:
    """// to end of line, but not inside a quoted string.

    An FGD is full of file paths and descriptions, and "sprites/CS/AmbientGeneric.spr" has no
    comment in it. Stripping on the bare token eats half of them.
    """
    out = []
    for line in text.splitlines():
        quoted = False
        for i, ch in enumerate(line):
            if ch == '"':
                quoted = not quoted
            elif ch == "/" and not quoted and line[i:i + 2] == "//":
                line = line[:i]
                break
        out.append(line)
    return "\n".join(out)


def split_blocks(text: str) -> list[tuple[str, str]]:
    """Each @Class declaration and the bracketed body that follows it."""
    blocks = []
    for m in re.finditer(r"^@(\w+)", text, re.M):
        head_start = m.start()
        open_at = text.find("[", head_start)
        if open_at < 0:
            continue

        # Bodies nest: a choices key has its own brackets inside the entity's.
        depth = 0
        end = open_at
        for i in range(open_at, len(text)):
            if text[i] == "[":
                depth += 1
            elif text[i] == "]":
                depth -= 1
                if depth == 0:
                    end = i
                    break
        blocks.append((text[head_start:open_at], text[open_at + 1:end]))
    return blocks


def parse_keys(body: str) -> tuple[dict, list]:
    """The keys an entity accepts, and what its spawnflag bits mean."""
    keys: dict = {}
    flags: list = []

    for m in re.finditer(
            r'^\s*(\w+)\(([\w ]+)\)\s*(?::\s*"([^"]*)")?\s*(?::\s*("?[^\n=\[]*?"?))?\s*(=)?\s*$',
            body, re.M):
        name, kind, label, default, has_choices = m.groups()
        name = name.lower()

        if name == "spawnflags":
            continue  # handled below, since its shape is different

        entry = {"type": kind.strip().lower()}
        if label:
            entry["label"] = label.strip()
        if default and default.strip():
            entry["default"] = default.strip().strip('"')
        keys[name] = entry

    # Choices, matched against the key they belong to by position in the text.
    for m in re.finditer(r'^\s*(\w+)\(choices\)[^\[]*\[(.*?)\]', body, re.M | re.S):
        name, choices = m.group(1).lower(), m.group(2)
        options = {}
        for value, text in re.findall(r'^\s*"?([^":\n]*)"?\s*:\s*"([^"]*)"', choices, re.M):
            options[value.strip()] = text.strip()
        if options and name in keys:
            keys[name]["choices"] = options

    # Spawnflags. Each line is a bit value, what it means, and whether it is on by default.
    if fm := re.search(r"spawnflags\(flags\)[^\[]*\[(.*?)\]", body, re.S):
        for bit, label, _default in re.findall(
                r'^\s*(\d+)\s*:\s*"([^"]*)"\s*(?::\s*(\d+))?', fm.group(1), re.M):
            flags.append({"bit": int(bit), "means": label.strip()})

    return keys, flags


def parse_fgd(text: str, game: str, catalogue: dict) -> None:
    text = strip_comments(text)

    for head, body in split_blocks(text):
        kind = re.match(r"@(\w+)", head).group(1)
        # BaseClass declarations are shared fragments, not placeable entities.
        if kind.lower() == "baseclass":
            continue

        m = re.search(r'=\s*(\w+)\s*(?::\s*"([^"]*)")?', head)
        if not m:
            continue
        classname, description = m.group(1), (m.group(2) or "").strip()

        keys, flags = parse_keys(body)
        entry = catalogue.setdefault(classname, {
            "kind": "brush" if kind.lower() == "solidclass" else "point",
            "games": [],
            "keys": {},
            "spawnflags": [],
        })
        if game not in entry["games"]:
            entry["games"].append(game)
        if description and not entry.get("description"):
            entry["description"] = description
        # A later game adds keys rather than replacing what the base game said.
        for k, v in keys.items():
            entry["keys"].setdefault(k, v)
        known = {f["bit"] for f in entry["spawnflags"]}
        entry["spawnflags"] += [f for f in flags if f["bit"] not in known]


def main() -> int:
    catalogue: dict = {}
    for game, path in WANTED:
        try:
            parse_fgd(fetch(path), game, catalogue)
            print(f"  {game:10} {len(catalogue):4} classes so far")
        except subprocess.CalledProcessError:
            print(f"  {game:10} not in the collection", file=sys.stderr)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({
        "source": f"https://github.com/{REPO}",
        "note": "Generated by tools/build_entity_catalogue.py. The schema the level editors "
                "used, written by the people who made the games.",
        "entities": dict(sorted(catalogue.items())),
    }, indent=2) + "\n", encoding="utf-8")

    keyed = sum(1 for e in catalogue.values() if e["keys"])
    flagged = sum(1 for e in catalogue.values() if e["spawnflags"])
    print(f"wrote {len(catalogue)} entity classes, {keyed} with keys, {flagged} with spawnflags")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
