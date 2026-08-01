# Contributing to Quebratsk Engine

Thank you for your interest in contributing to **Quebratsk Engine**! This document outlines the build process, testing guidelines, and core contribution rules for the project.

---

## 🌟 The Golden Rule of Quebratsk Engine

> **A format is NEVER documented or claimed as supported until it has been executed and verified against an actual retail game installation.**

Earlier versions of this repository contained stub classes named after formats (such as Unreal `.uasset`, Unity `.bundle`, or Bohemia ODOL `.p3d`) that returned empty meshes or hardcoded placeholder rows. These stubs wasted developer time and led users to believe formats were functional when they were not.

If you are adding a new parser or format reader:
1. It **must** decode real binary data from actual retail game installations (e.g. retail Half-Life 2, Garry's Mod, CS 1.6, or Arma 3).
2. It **must** pass end-to-end verification tests in `demo/tests/`.
3. Do **not** claim support for formats on the [Roadmap](README.md#roadmap) (such as UnityFS, Unreal Engine PAK, Source 2, or Arma ODOL models) until the parser is 100% functional against real game files.

---

## 🛠️ Building the Extension

Quebratsk Engine is built using **C++23**, **CMake**, and **MSVC 2022** on Windows. `godot-cpp` is fetched automatically by CMake.

### 1. Debug Build Tree

```bash
# Configure the Debug build tree
cmake -S . -B build

# Compile the Debug extension DLL
cmake --build build --config Debug
```

### 2. Release Build Tree

> [!IMPORTANT]
> Debug and Release builds **must use separate build trees** (`build` vs `build-release`). `godot-cpp 4.3` inspects `CMAKE_BUILD_TYPE`, which is empty under multi-config Visual Studio generators, so it defaults to Debug and bakes `/MDd` into a release build unless configured in a separate tree with `-DCMAKE_BUILD_TYPE=Release`.

```bash
# Configure a separate Release build tree
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release

# Compile the Release extension DLL
cmake --build build-release --config Release
```

Both build commands write the extension into `demo/bin/`, where `quebratsk.gdextension` expects it.

> [!NOTE]
> **A fresh clone contains no binary.** The compiled `.dll` is deliberately not tracked — it is a 7 MB build output that changed on nearly every commit and once accounted for more of the repository's history than everything else combined. Build at least the Debug tree before opening `demo/` in Godot, or the extension will fail to load and the editor dock will not appear.

---

## 🧪 Testing Guidelines

### 1. C++ Unit Tests (`ctest`)

From the `build/` directory, run CTest:

```bash
cd build
ctest -C Debug --output-on-failure
```

The test suites in `tests/` cover:
- Binary reader bounds checking and integer overflow prevention
- DXT1/BC1 and DXT5/BC3 texture block decoding
- Quantized animation tracks and run-length decoding
- GoldSrc and Source 1 format parser invariants

### 2. End-to-End Godot Test Harnesses

Unit tests do not catch issues with Godot SceneTree integration. `demo/tests/` contains headless Godot harnesses that exercise the real GDExtension API against retail game files:

```bash
# Verify GDExtension API bindings
godot --headless --path demo res://tests/verify_api.tscn

# Verify Editor Dock plugin
godot --headless --path demo res://tests/verify_dock.tscn
```

---

### 3. What a good measurement looks like

Compiling proves nothing about these formats and neither does review. Almost every serious
defect in this project's history was invisible until the code ran against a retail install and
somebody counted the result.

Two habits have earned their place, both the hard way:

**Measure something that could come out wrong.** "It loaded" is not a result. "10 of 10 models
with more than one sequence move a bone" is. A check that cannot fail for the reason that
matters is a check that will pass while the thing it watches is broken. Four harnesses missed
an axis remap that had every imported model facing ninety degrees away from Godot's front,
because all four measured counts, heights and distances, and a rotation changes none of those.

**When a check reports a failure, suspect the check first.** More than once the first draft of
a harness measured its own setup rather than the thing under test. One reported everybody
standing correctly on the floor while the screenshots showed them buried to the waist, because
it compared each body against itself and never against the world. Another read a result count
after selecting a category that never runs a search.

And when a unit test disagrees with reality, read it before trusting it. Two of them had the
axis defect written in as the expected answer, so every run for months certified that the
wrong result was the right one.

---

## ⚠️ Things that bite

Small, specific, and each one cost a working session to find.

**Binary layouts are silent when wrong.** A struct missing a field is a struct whose later
fields are all shifted, and nothing about the result looks broken: plausible numbers get read
from the wrong places. Every offset the code indexes by carries a `static_assert` in
`src/parsers/*/structs/`, so a layout that drifts stops compiling. Add the assert rather than
trusting a comment.

**Bounds checks by subtraction, never by addition.** `offset + count * size` overflows in
silence and the comparison then passes exactly when it should not. Compare against
`size - offset` and divide by the stride instead.

**The winding order is already correct.** `source_to_godot()` has determinant +1, so it
preserves orientation. Pairing it with a winding flip renders every face inside out.

**GDScript `:=` cannot infer from an untyped value.** An element of an `Array` literal or the
return of an untyped method will fail to parse. Write `var x: String = ...` there.

**Never give a shared helper in `demo/addons/` a `class_name`.** It resolves through
`.godot/global_script_class_cache.cfg`, which clearing the project drops, and everything that
referenced it stops parsing. Use `preload` by path.

---

## 📁 Repository Structure & Ownership

- `src/`: C++ engine source code — parsers, VFS, converters, GDExtension ClassDB bindings.
- `tests/`: C++ unit test suites run by CTest.
- `demo/`: Godot 4 project hosting the GDExtension binary and editor addon.
  - `demo/addons/quebratsk_editor/`: The editor dock plugin.
  - `demo/tests/`: Headless end-to-end verification scenes.
- `docs/`: Documentation (API contract, beginner's guide).

---

## 📋 Submitting Pull Requests

1. Follow the checklist in `.github/PULL_REQUEST_TEMPLATE.md`.
2. Ensure both Debug and Release build trees compile cleanly with Visual Studio 2022.
3. Verify that all CTest suites and headless Godot test harnesses pass.
4. Update `CHANGELOG.md` following [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/) guidelines.
