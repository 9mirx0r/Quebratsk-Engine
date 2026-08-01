#!/usr/bin/env python3
"""Build the Counter-Strike weapon manifest from ReGameDLL_CS.

A model does not say which weapon it is. It carries a mesh, a skeleton and some sequences,
and everything that makes an AK an AK rather than a shape lives in the game's own code: which
view model goes with which world model, which sounds it makes, how hard it hits, how much it
slows you down. Quebratsk was guessing at that from filenames, which works until it does not.

ReGameDLL_CS is a reimplementation of the Counter-Strike game library under MIT, so the
relationships can be read out and shipped as data rather than inferred. Each weapon declares
itself plainly:

    SET_MODEL(edict(), "models/w_ak47.mdl");
    PRECACHE_MODEL("models/v_ak47.mdl");
    PRECACHE_SOUND("weapons/ak47-1.wav");
    DefaultDeploy("models/v_ak47.mdl", "models/p_ak47.mdl", AK47_DRAW, "ak47", ...);

This reads those, resolves the numeric constants out of the shared headers, and writes
demo/addons/quebratsk_editor/data/cs_weapons.json.

Run it when ReGameDLL_CS changes, which is rarely. The output is committed, so nobody needs
network access or this script to use Quebratsk.
"""

import json
import re
import subprocess
import sys
from pathlib import Path

REPO = "s1lentq/ReGameDLL_CS"
OUT = Path(__file__).resolve().parent.parent / "demo/addons/quebratsk_editor/data/cs_weapons.json"


def fetch(path: str) -> str:
    """One file out of the repository, as text."""
    raw = subprocess.run(
        ["gh", "api", f"repos/{REPO}/contents/{path}", "--jq", ".content"],
        capture_output=True, text=True, check=True).stdout
    import base64
    return base64.b64decode(raw).decode("utf-8", "replace")


def list_weapon_files() -> list[str]:
    tree = subprocess.run(
        ["gh", "api", f"repos/{REPO}/git/trees/master?recursive=1", "--jq", ".tree[].path"],
        capture_output=True, text=True, check=True).stdout.splitlines()
    return [p for p in tree if "wpn_shared/wpn_" in p and p.endswith(".cpp")]


def read_constants(text: str) -> dict[str, float]:
    """Named numbers, so AK47_DAMAGE resolves to 36 rather than to a name.

    Three spellings, found one at a time, each because something came out missing:

      const float AK47_DAMAGE = 36.0f;     damage, range, speed
      AK47_MAX_CLIP = 30,                  enum members: clip size, price, weight
      #define SOMETHING 5                  the rare survivor

    The first version knew only the last of those and every weapon came out with its numbers
    absent, from a file that had them on consecutive lines. Worth remembering that "the field
    is empty" was a fact about this parser and never about the game.
    """
    out = {}
    patterns = [
        r"const\s+(?:float|int|unsigned int)\s+([A-Z0-9_]+)\s*=\s*(-?[0-9.]+)f?\s*;",
        # Enum members. Anchored to the line so an assignment inside a function body cannot
        # be mistaken for a declared constant.
        r"^\s*([A-Z][A-Z0-9_]{2,})\s*=\s*(-?[0-9.]+)\s*,\s*(?://.*)?$",
        r"#define\s+([A-Z0-9_]+)\s+\(?(-?[0-9.]+)f?\)?\s*$",
    ]
    for pattern in patterns:
        for name, value in re.findall(pattern, text, re.M):
            try:
                out.setdefault(name, float(value))
            except ValueError:
                pass
    return out


def parse_weapon(name: str, text: str, constants: dict[str, float]) -> dict:
    """What one weapon says about itself."""
    weapon: dict = {"name": name}

    # Models. The world model is what SET_MODEL puts in the level; the view and player models
    # come from DefaultDeploy, which is also where the animation extension is named.
    if m := re.search(r'SET_MODEL\(\s*edict\(\)\s*,\s*"([^"]+)"', text):
        weapon["world_model"] = m.group(1)
    # Several weapons deploy twice, once plain and once with the shield, and the shield
    # variant is a different model in a different folder. Taking whichever matched first gave
    # the knife models/shield/v_shield_knife.mdl, which is a real file and the wrong answer.
    deploys = re.findall(r'DefaultDeploy\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*\w+\s*,\s*"([^"]+)"',
                         text)
    plain = [d for d in deploys if "shield" not in d[0]]
    if chosen := (plain or deploys):
        weapon["view_model"] = chosen[0][0]
        weapon["player_model"] = chosen[0][1]
        weapon["animation_extension"] = chosen[0][2]
        if len(plain) < len(deploys):
            shield = [d for d in deploys if "shield" in d[0]]
            weapon["shield_view_model"] = shield[0][0]

    # Every sound the weapon precaches, in declaration order. The firing sounds are the ones
    # numbered with a dash, which is the convention across the whole game.
    sounds = re.findall(r'PRECACHE_SOUND\(\s*"([^"]+)"', text)
    if sounds:
        weapon["sounds"] = sounds
        # The firing sounds are numbered with a dash, which holds across the whole game:
        # ak47-1.wav fires, ak47_clipout.wav does not. Names that describe an action other
        # than firing are excluded even when they carry a number, since a bolt being pulled
        # and a knife being drawn both do.
        not_firing = ("clipin", "clipout", "boltpull", "deploy", "draw", "slideback",
                      "sliderelease", "reload", "pinpull", "hit", "stab", "wall")
        fire = [x for x in sounds
                if re.search(r"[-_]?\d+\.wav$", x) and not any(w in x.lower() for w in not_firing)]
        if fire:
            weapon["fire_sounds"] = fire

    # Numbers, resolved through the constants when the source names one.
    upper = name.upper()
    for key, suffixes in [
        ("damage", ["_DAMAGE"]),
        ("range_modifier", ["_RANGE_MODIFER", "_RANGE_MODIFIER"]),
        ("max_speed", ["_MAX_SPEED", "_PLAYER_SPEED"]),
        ("reload_time", ["_RELOAD_TIME"]),
        ("clip_size", ["_MAX_CLIP", "_CLIP_SIZE"]),
        ("cycle_time", ["_CYCLETIME", "_CYCLE_TIME", "_FIRE_RATE"]),
        ("weight", ["_WEIGHT"]),
        ("price", ["_PRICE"]),
    ]:
        for suffix in suffixes:
            if (value := constants.get(upper + suffix)) is not None:
                # Whole numbers read as whole numbers; a clip of 30.0 is noise.
                weapon[key] = int(value) if value == int(value) else value
                break

    # The silenced variant, which several weapons carry as a second damage figure.
    if (value := constants.get(upper + "_DAMAGE_SIL")) is not None:
        weapon["damage_silenced"] = int(value) if value == int(value) else value

    return weapon


def main() -> int:
    print("reading weapon definitions from", REPO)
    constants = {}
    for header in ["regamedll/dlls/weapons.h", "regamedll/dlls/weapontype.h"]:
        try:
            constants |= read_constants(fetch(header))
        except subprocess.CalledProcessError:
            print("  could not read", header, file=sys.stderr)
    print(f"  {len(constants)} constants")

    weapons = {}
    for path in list_weapon_files():
        name = Path(path).stem.removeprefix("wpn_")
        try:
            weapons[name] = parse_weapon(name, fetch(path), constants)
        except subprocess.CalledProcessError:
            print("  could not read", path, file=sys.stderr)

    # A weapon with no models told us nothing, and an entry that says nothing is worse than
    # no entry: it looks like an answer.
    weapons = {k: v for k, v in weapons.items() if "view_model" in v or "world_model" in v}

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({
        "source": f"https://github.com/{REPO}",
        "licence": "MIT",
        "note": "Generated by tools/build_weapon_manifest.py. Facts read out of the game's "
                "own code, not inferred from filenames.",
        "weapons": dict(sorted(weapons.items())),
    }, indent=2) + "\n", encoding="utf-8")

    print(f"wrote {len(weapons)} weapons to {OUT.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
