# Working on Quebratsk Engine

## Before touching a GoldSrc or Source binary format

Read `docs/goldsrc_source_architecture_guide.xml` first. It is normative: struct layouts,
field offsets, the search path cascade, and how a model names its sounds.

It is not merely advisory because the failure mode here is silent. A struct missing a field
is a struct whose later fields are all shifted, and nothing about the result looks wrong:
plausible numbers are read from the wrong places. Three defects fixed on 2026-08-01 were
exactly this, and one of them (`mstudioevent_t` read at 76 bytes instead of 80) left the
first event of every sequence correct and turned every one after it into garbage.

Where the guide and the code disagree, **the code wins and the guide gets corrected in the
same commit.** The ground truth for any layout is the header that pins it:

- `src/parsers/goldsrc/structs/mdl10_structs.h`
- `src/parsers/source1/structs/anim_structs.h`

An offset stated anywhere and not carried by a `static_assert` in one of those files is not
verified. Add the assert rather than trusting the prose.

That guide now also carries the player's physics: hull dimensions, the movement cvars, the
jump formula, and how acceleration and friction actually work. Those replaced numbers that had
been invented, so treat the same way: the guide is where they live and the code is what proves
them.

`CREDITS.md` records which outside project answered which question. Add to it when another one
does, and check the licence before reading code rather than after.

## Verification

Compiling proves nothing about these formats, and neither does review. Almost every defect
worth fixing in this project was invisible until the code ran against a retail install and
someone counted the result. The harnesses in `demo/tests/` exist for that and are run with:

```
godot --headless --path demo res://tests/<name>.tscn
```

Two habits that have paid for themselves:

- Measure something that could come out wrong. "It loaded" is not a result; "10 of 10 models
  with more than one sequence move a bone" is.
- When a check reports a failure, suspect the check first. Several times the first draft of a
  harness measured its own setup rather than the thing under test.

## Things that bite

- GDScript `:=` cannot infer from an untyped value, such as an element of an `Array` literal
  or the return of an untyped method. Write `var x: String = ...` there.
- Never give shared helpers in `demo/addons/` a `class_name`. It depends on
  `.godot/global_script_class_cache.cfg`, which clearing the project drops. Use `preload`.
- The winding order is already correct after `source_to_godot()`. Pairing it with a winding
  flip renders everything inside out.
