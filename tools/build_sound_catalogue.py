#!/usr/bin/env python3
"""Build the Counter-Strike sound catalogue from ReGameDLL_CS.

Most of the noise a Counter-Strike level makes is not recorded anywhere in the files Quebratsk
reads. A .mdl names the sounds its own animations trigger, which covers a gunshot and not much
else. Everything a player does that is not firing a weapon comes out of the game library:

    footsteps, which depend on what you are standing on
    the C4 beeping, being planted, being defused, exploding
    a grenade bouncing off a wall, and each of the three exploding differently
    hitting flesh, hitting a wall, taking a fall, drowning
    the radio, the hostages, the round starting

So a model imported perfectly still stands in silence. The names live in ReGameDLL_CS, an MIT
reimplementation of the Counter-Strike game library, written plainly:

    PRECACHE_SOUND("weapons/c4_beep1.wav");
    EMIT_SOUND(ENT(pev), CHAN_WEAPON, "weapons/c4_explode1.wav", ...);
    strcpy(szbuffer, "player/pl_step%i.wav");

This reads them, groups them by what they are for, and writes
demo/addons/quebratsk_editor/data/cs_sounds.json.

Two things it deliberately does not do. It does not invent a name that is not in the source,
and it does not check whether the file exists: whether cstrike/sound actually ships
player/pl_step1.wav is a question about an install, and demo/tests/verify_sounds_resolve.gd is
where that gets answered. A name here is a claim about the game's code and nothing more.

The output is committed, so nobody needs network access or this script to use Quebratsk.
"""

import base64
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = "s1lentq/ReGameDLL_CS"
OUT = Path(__file__).resolve().parent.parent / "demo/addons/quebratsk_editor/data/cs_sounds.json"

# The files that actually emit sound, and what part of the game each one is. Chosen rather
# than scanning everything because the repository also contains a bot, a test suite and the
# engine headers, and a name in any of those is not a sound the game plays.
SOURCES = [
    ("regamedll/dlls/player.cpp", "player"),
    ("regamedll/dlls/client.cpp", "player"),
    ("regamedll/dlls/weapons.cpp", "weapon"),
    ("regamedll/dlls/ggrenade.cpp", "grenade"),
    ("regamedll/dlls/wpn_shared/wpn_c4.cpp", "c4"),
    ("regamedll/dlls/wpn_shared/wpn_hegrenade.cpp", "grenade"),
    ("regamedll/dlls/wpn_shared/wpn_flashbang.cpp", "grenade"),
    ("regamedll/dlls/wpn_shared/wpn_smokegrenade.cpp", "grenade"),
    ("regamedll/dlls/wpn_shared/wpn_knife.cpp", "weapon"),
    ("regamedll/dlls/hostage/hostage.cpp", "hostage"),
    ("regamedll/dlls/multiplay_gamerules.cpp", "round"),
    ("regamedll/dlls/sound.cpp", "world"),
    ("regamedll/dlls/func_break.cpp", "world"),
    ("regamedll/dlls/doors.cpp", "world"),
]

# What a name is for, matched against the name itself. First match wins, so the specific ones
# come before the general ones.
BUCKETS = [
    # By folder first. A path says what a sound belongs to more reliably than its name does:
    # hostage/hdie/hdeath1.wav is a hostage dying, not a player, and debris/bustcrate1.wav is
    # a crate breaking rather than a grenade. Both landed in the wrong group when the name
    # patterns ran first.
    ("hostage", [r"^hostage/"]),
    ("radio", [r"^radio/"]),
    ("breaking", [r"^debris/"]),
    ("doors", [r"^doors/", r"^buttons/"]),
    ("ambience", [r"^ambience/"]),

    ("footstep", [r"pl_step", r"pl_dirt", r"pl_duct", r"pl_grate", r"pl_metal", r"pl_slosh",
                  r"pl_tile", r"pl_snow", r"pl_ladder", r"pl_wood", r"pl_swim"]),
    ("landing", [r"pl_fallpain", r"pl_jumpland"]),
    ("pain", [r"pl_pain", r"bhit_flesh", r"bhit_kevlar", r"headshot"]),
    ("death", [r"die\d", r"death"]),
    ("drowning", [r"pl_drown", r"h2odeath", r"wade", r"water_"]),
    ("c4_beep", [r"c4_beep"]),
    ("c4_plant", [r"c4_plant", r"c4_click"]),
    ("c4_defuse", [r"c4_disarm"]),
    ("c4_explode", [r"c4_explode"]),
    ("grenade_bounce", [r"he_bounce", r"grenade_hit", r"gren_bounce"]),
    ("grenade_explode", [r"explode\d", r"flashbang_explode", r"sg_explode"]),
    ("grenade_pin", [r"pinpull", r"_pin"]),
    ("round", [r"rounds", r"ctwin", r"terwin", r"bombpl", r"bombdef", r"clock"]),
    ("weapon_handling", [r"_draw", r"_clipin", r"_clipout", r"_slideback", r"_boltpull",
                         r"knife_", r"zoom", r"deploy"]),
]


def fetch(path: str) -> str:
    raw = subprocess.run(
        ["gh", "api", f"repos/{REPO}/contents/{path}", "--jq", ".content"],
        capture_output=True, text=True, check=True).stdout
    return base64.b64decode(raw).decode("utf-8", "replace")


def names_in(text: str) -> list[str]:
    """Every sound path a file mentions, in the order it mentions them.

    A printf pattern is expanded rather than dropped. GoldSrc writes its four footstep
    variants as one string with a %i in it, so pl_step%i.wav is four files and taking the
    literal would give a name that resolves to nothing.
    """
    out = []
    for raw in re.findall(r'"([A-Za-z0-9_/\-]+\.wav)"', text):
        if "%" not in raw:
            out.append(raw)
            continue
        for n in range(1, 5):
            out.append(re.sub(r"%d|%i", str(n), raw))

    # Some are built by concatenation: "weapons/" then the weapon's own name. Those cannot be
    # resolved here and are left to the weapon manifest, which reads them per weapon.
    return out


def bucket_of(name: str) -> str:
    lowered = name.lower()
    for label, patterns in BUCKETS:
        for pattern in patterns:
            if re.search(pattern, lowered):
                return label
    return ""


def main() -> int:
    print("reading sound names from", REPO)
    groups: dict[str, dict] = {}
    # Which group each name went into. A sound belongs in one place, and without this the
    # unclassified ones were filed by the source file that mentioned them, so
    # weapons/debris1.wav landed in both grenade_other and weapon_other because ggrenade.cpp
    # and weapons.cpp both name it. Nine names were in two groups at once.
    where: dict[str, str] = {}
    missing_files = []

    for path, area in SOURCES:
        try:
            text = fetch(path)
        except subprocess.CalledProcessError:
            missing_files.append(path)
            continue

        found = 0
        for name in names_in(text):
            # The first file to mention an unclassified name decides where it lives, so the
            # order of SOURCES is the tie-breaker rather than the dictionary's.
            label = where.get(name) or bucket_of(name) or ("%s_other" % area)
            entry = groups.setdefault(label, {"area": area, "files": [], "from": []})
            if name not in where:
                where[name] = label
                entry["files"].append(name)
                found += 1
            if path not in entry["from"]:
                entry["from"].append(path)
        print(f"  {path.split('/')[-1]:28} {found:4} new")

    for path in missing_files:
        print(f"  {path} not in the repository", file=sys.stderr)

    for entry in groups.values():
        entry["files"].sort()

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({
        "source": f"https://github.com/{REPO}",
        "note": "Generated by tools/build_sound_catalogue.py. These are the sound names the "
                "Counter-Strike game library plays, which a .mdl does not record: footsteps, "
                "the C4, grenades, pain and death. A name here is a claim about the game's "
                "code, not a promise that the file is installed.",
        "groups": dict(sorted(groups.items())),
    }, indent=2) + "\n", encoding="utf-8")

    # Every name in exactly one group, which is the whole point of the map above.
    total = sum(len(entry["files"]) for entry in groups.values())
    assert total == len(where), f"{total} entries for {len(where)} names: a name is in two groups"

    print(f"wrote {len(where)} sound names in {len(groups)} groups")
    for label in sorted(groups):
        print(f"  {label:20} {len(groups[label]['files']):3}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
