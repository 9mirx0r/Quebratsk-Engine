# The GoldSrc Encyclopedia

Evidence-first notes on GoldSrc formats, engine behaviour and the retail content
installed on this machine. The plan, the evidence classes and the stage gates live in
[`docs/GOLDSRC-ENCYCLOPEDIA.md`](../../GOLDSRC-ENCYCLOPEDIA.md); this folder holds the
entries.

## How to read an entry

Every entry starts with YAML front matter:

```yaml
---
id: wad3.container            # stable, dotted
status: confirmed             # hypothesis | confirmed | refuted
confidence: high
games: [hl, cs16, czero]      # which games the evidence comes from
sources: [observed]           # observed | sdk | reference | decompiled
verified_by: tools/verify/validate_wad3.py
see_also: [formats.qpic]
---
```

The body answers four questions: **Claim** (one falsifiable statement),
**Evidence** (numbers measured on the corpus), **How to check** (the command that
fails when the claim is wrong) and **Open questions**.

A `reference` or `decompiled` claim is a lead, not proof: it is marked
`status: hypothesis` until an `observed` or `sdk` source confirms it.

## Coverage

| Entry | Status | Verified by |
|---|---|---|
| [`formats/wad3.md`](formats/wad3.md) | confirmed | `tools/verify/validate_wad3.py` |
| [`formats/bsp30.md`](formats/bsp30.md) | confirmed | `tools/verify/validate_bsp30.py` |
| [`formats/mdl10.md`](formats/mdl10.md) | confirmed | `tools/verify/validate_mdl10.py` |
| [`formats/spr.md`](formats/spr.md) | confirmed | `tools/verify/validate_spr.py` |

Per-game inventories are generated: see [`games/README.md`](games/README.md).

## Regenerate

```powershell
python tools/corpus_scan.py scan
python tools/verify/validate_corpus.py --all
```

`data/corpus/`, `data/deltas/` and `data/validation/` are generated; nothing there is
edited by hand.
