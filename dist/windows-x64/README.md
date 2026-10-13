# Lumen - Windows x86-64 build

| Path | What it is |
|---|---|
| `PhysicsPlayground/Physics_Playground.exe` | **The exported game.** Double-click to play (keep the folder together: it needs `game.lumen` and `scene.json` next to it) |
| `PhysicsPlayground-windows-x64.zip` | The same exported game folder as one zip |
| `LumenRuntime.exe` | The generic runtime: `LumenRuntime.exe <project-folder>` plays any Lumen project; `--export <dir>` makes a standalone game folder |
| `LumenAgent.exe` | Headless engine host for AI agents (one JSON command per line on stdin, see `Docs/AgentAPI.md`) |

## Controls (Physics Playground)
W A S D / arrow keys move the red ball, Space jumps, E drops a ball, Esc quits.

## Requirements
- 64-bit Windows 10 or 11
- A **Vulkan 1.3** capable graphics driver (current NVIDIA, AMD and Intel drivers include `vulkan-1.dll`). If the window does not open,
  update your graphics driver.
- No other runtime is needed: the executables are fully static apart from standard Windows DLLs.
- The physics library is built without AVX2 so it also runs on older CPUs.

## Notes
- A console window opens next to the game; it shows the GPU in use and any error messages.
- These executables were cross-compiled from Linux with MinGW-w64. They were run here under **Wine** (including the full Vulkan
  rendering path through a software GPU), which is not the same as testing on real Windows with a real GPU. Please report anything that
  misbehaves.
- Making your own exported game: `LumenRuntime.exe MyProject --export C:\Games\MyGame` creates `C:\Games\MyGame\<Name>.exe` plus the project files.
