# Lumen - Agent Development Guide

Lumen is a production-grade 3D game engine (Windows, macOS, Linux/Ubuntu 24+).
Stable, fully tested, no hacks. Work autonomously; ask questions only when truly blocked.

## Hard rules
- Stay inside this repository. Never read or reuse code from elsewhere on the machine.
- Every feature ships with unit tests. Run `ctest` before every commit; all tests must pass.
- Code review the diff before each commit (style, correctness, tests, no dead code).
- Warnings are errors (`-Wall -Wextra -Wpedantic -Werror`, `/W4 /WX`).
- Dependencies are pinned to exact tags/commits in `cmake/Dependencies.cmake`.

## Build and test
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Code style (Hazel conventions)
- Namespace `Lumen`; headers use `#pragma once`; includes as `"Lumen/<Module>/<File>.h"`.
- Types, methods, functions, enum values: `PascalCase`. Locals and parameters: `camelCase`.
- Members: `m_PascalCase`. Statics: `s_PascalCase`. Public struct fields: `PascalCase`.
- `Ref<T>`/`Scope<T>` with `CreateRef`/`CreateScope`; no raw owning pointers.
- Macros are prefixed `LM_`. Log with `LM_TRACE/INFO/WARN/ERROR/FATAL`, assert with `LM_ASSERT`.
- Tabs for indentation, Allman braces (see `.clang-format`).

## Layout
- `Lumen/` core static library (`LumenCore`): `Core/` base utilities, `Scene/` ECS (EnTT).
- `Apps/LumenAgent/` headless JSON-over-stdio host for AI agents. Keep `Docs/AgentAPI.md` in sync with new commands.
- `Tests/` doctest unit tests, one file per module. GPU-free logic must be unit tested.

## Roadmap
1. Core + ECS (done) 2. Lua scripting API (done: lifecycle, vec3, Transform, Scene API, sandbox; TODO: instruction-count limit, more components) 3. Physics (Jolt) (done: bodies, forces, raycast, tests; Lua API done; TODO: collision events, triggers, rebuild on component change) 4. Scene JSON serialization (done) 4b. Agent control API (done: AgentSession + LumenAgent stdio app, see Docs/AgentAPI.md; TODO: render/screenshot, asset import, prefab commands)
5. Renderer (done: headless RenderDevice on nvrhi/Vulkan + readback tests via llvmpipe; TODO: shaders, mesh+glTF, PBR, IBL, shadows, SSAO, HDR, GLFW window) (GLFW, nvrhi/Vulkan, glTF, PBR, IBL, shadows, SSAO, HDR) 6. Editor + export
7. Test scene exercising every component and the full scripting API
