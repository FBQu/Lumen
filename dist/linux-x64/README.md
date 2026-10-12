# Lumen - Linux x86-64 build

Prebuilt binaries (Release, stripped) built on Ubuntu 24.04 from this repository's source.

| File | What it is |
|---|---|
| `LumenRuntime` | Plays a game project in a window |
| `LumenAgent` | Headless engine host for AI agents: one JSON command per line on stdin, one JSON response per line on stdout (see `../../Docs/AgentAPI.md`) |
| `PhysicsPlayground/` | A sample game project |
| `play-physics-playground.sh` | Shortcut to start the sample |

## Requirements
- Linux x86-64 with glibc 2.39 or newer (Ubuntu 24.04+, Fedora 40+, ...)
- A **Vulkan 1.3** driver (any recent NVIDIA/AMD/Intel GPU driver; `sudo apt install mesa-vulkan-drivers` for AMD/Intel, or the
  software renderer `mesa-vulkan-drivers` + lavapipe if you have no GPU)
- An X11 session or XWayland, and the Vulkan loader (`sudo apt install libvulkan1`)

## Try it
```
./play-physics-playground.sh            # W A S D / arrows move, Space jumps, E drops a ball, Esc quits
./LumenRuntime PhysicsPlayground --no-vsync
./LumenRuntime PhysicsPlayground --export ~/MyGame   # standalone game folder: run ~/MyGame/Physics_Playground
./LumenRuntime PhysicsPlayground --screenshot shot.png --frames 120
./LumenRuntime --help
```

Drive the engine from a script or an AI agent:
```
printf '%s\n' '{"cmd":"help"}' | ./LumenAgent
```

## Notes
- These binaries were only exercised here on a software Vulkan driver (Mesa llvmpipe) under a virtual display, not on real GPUs.
  If the window does not open, run `vulkaninfo --summary` to check that a Vulkan 1.3 device is present.
- Windows and macOS builds are not included (they cannot be produced from this Linux environment).
