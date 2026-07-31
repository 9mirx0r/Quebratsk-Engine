## Description

Provide a clear summary of the changes introduced in this pull request and the problem or feature it addresses.

## Type of Change

- [ ] Bug fix (non-breaking change which fixes an asset parsing or VFS issue)
- [ ] New feature (non-breaking change which adds support for a format or API)
- [ ] Breaking change (fix or feature that alters existing GDScript API signatures in `docs/API.md`)
- [ ] Documentation / UX improvement

## Checklist & Contribution Rules

Before submitting this pull request, please verify that your changes adhere to the project's quality standards:

- [ ] **The Golden Rule:** The format or parser introduced in this PR has been executed and verified against an **actual retail game installation** (e.g. retail Half-Life 2, Garry's Mod, CS 1.6, Arma 3). *No unverified or stub formats!*
- [ ] **Build Verification:** Code compiles cleanly with MSVC 2022 and CMake across both build trees:
  - [ ] Debug build: `cmake -S . -B build && cmake --build build --config Debug`
  - [ ] Release build: `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release && cmake --build build-release --config Release`
- [ ] **C++ Unit Tests:** All unit test suites pass locally via `cd build && ctest -C Debug --output-on-failure`.
- [ ] **End-to-End Test Harnesses:** Verified with Godot headless harnesses:
  - [ ] `godot --headless --path demo res://tests/verify_api.tscn`
  - [ ] `godot --headless --path demo res://tests/verify_dock.tscn`
- [ ] **Documentation & Changelog:** Updated `CHANGELOG.md` and `docs/API.md` if new API signatures were introduced.
