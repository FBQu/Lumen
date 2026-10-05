---
name: lumen-dev
description: Add or change a Lumen engine feature following project style, testing and commit rules
---
1. Add code under `Lumen/src/Lumen/<Module>/` and register the .cpp in `Lumen/CMakeLists.txt`.
2. Add a doctest file under `Tests/`, register it in `Tests/CMakeLists.txt`, cover edge cases and error paths.
3. Build, then run `ctest --test-dir build --output-on-failure`; fix every failure and warning.
4. Review `git diff` against AGENTS.md style and quality rules.
5. Commit with a focused message.
