# Running FVD

**Development gameplay always runs the current local build, not an extracted package.**

Windows:

```powershell
.\build-fvd.cmd game                       # incrementally build, then open lobby
.\build-fvd.cmd solo                       # incrementally build, then play
.\build-fvd.cmd solo --no-audio            # forward game flags
.\build-fvd.cmd relay --listen 0.0.0.0 --port 40000
.\build-fvd.cmd test                       # build and run C++ tests
```

Linux:

```bash
bash build-fvd.sh game
bash build-fvd.sh solo --no-audio
bash build-fvd.sh relay --listen 0.0.0.0 --port 40000
bash build-fvd.sh test
```

Both wrappers build their required native executable before launching it directly from the build directory, with the checkout's `assets/` for games. Arguments after `game`, `solo` or `relay` are passed to the executable; use `game --help` or `relay --help` to see options.
Graphical launches enable VSync and independently cap rendering to the active display's refresh rate by default; use `--max-fps FPS` to select a lower cap or `--no-vsync` to diagnose swap synchronization without removing the frame cap.

## Release distribution is separate

Only run `.\build-fvd.cmd package` or `bash build-fvd.sh package` when you need a **release archive** under `dist/`. This does not extract or launch the archive. End users and relay-server operators can unpack the release in their own deployment environment and run `openage-native` or `openage-relay` there. See [build instructions](building.md) and [release acceptance](code/network/FVD_RELEASE_ACCEPTANCE.md).

The original-game converter and Python/Cython launcher `bin/run` are retired.
