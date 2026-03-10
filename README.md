# Leia Track App

Standalone head tracking for any SteamVR game using a Leia autostereoscopic display.

Reads eye positions from the Leia SR SDK, applies a [One-Euro filter](https://gery.casiez.net/1euro/) for smooth noise rejection, maps head lean to camera rotation, and sends it as OpenTrack UDP to [VRto3D](https://github.com/oneup03/VRto3D).

**by [evilkermitreturns](https://github.com/evilkermitreturns)**

## Requirements

- Leia display (e.g. Acer SpatialLabs) with SR Platform installed and running
- [VRto3D](https://github.com/oneup03/VRto3D) with `use_open_track` set to `true` in `Steam/config/vrto3d/default_config.json`
- SteamVR
- [LeiaSR SDK](https://www.immersity.ai/) (for building from source)

## Quick Start

1. Set `"use_open_track": true` in your VRto3D config (`Steam/config/vrto3d/default_config.json`)
2. Launch SteamVR
3. Launch your game
4. Run `leia_track_app.exe`
5. Press **Ctrl+C** to calibrate your sitting position (look straight at the screen)
6. Lean to look around!

## Controls

All hotkeys use **Ctrl+key** to avoid conflicts with game input.

| Key | Action |
|-----|--------|
| **Ctrl+C** | Calibrate center position |
| **Ctrl+A/D** | Smoothness at rest (min_cutoff down/up) |
| **Ctrl+E/F** | Movement response speed (beta down/up) |
| **Ctrl+J/K** | Yaw sensitivity (left/right lean) |
| **Ctrl+N/M** | Pitch sensitivity (up/down lean) |
| **Ctrl+G/B** | Response curve (more linear / more curved) |
| **Ctrl+S** | Save settings to file |
| **Ctrl+L** | Load settings from file |
| **Ctrl+P** | Print current settings |
| **Ctrl+H** | Show help |
| **Ctrl+Q** | Quit |

## Settings

Settings are saved to `Steam/config/vrto3d/leia_track_config.txt` (next to VRto3D's config). They auto-load on startup.

| Setting | Default | Description |
|---------|---------|-------------|
| `filter_mincutoff` | 0.02 | Smoothness at rest. Lower = silkier but laggier |
| `filter_beta` | 0.3 | Responsiveness to fast movement. Higher = less lag |
| `sens_yaw` | 3.0 | Degrees of rotation per cm of left/right lean |
| `sens_pitch` | 2.0 | Degrees of rotation per cm of up/down lean |
| `curve_power` | 1.2 | Response curve. 1.0 = linear, 2.0+ = gentle center |
| `mag_strength` | 0.15 | How strongly the center pulls back |
| `mag_radius` | 2.0 | How far the center pull reaches (cm) |
| `dead_zone_cm` | 0.2 | Ignore movements smaller than this (cm) |
| `max_yaw` | 45 | Maximum left/right rotation (degrees) |
| `max_pitch` | 15 | Maximum up/down rotation (degrees) |

## Building from Source

Requires the LeiaSR SDK and CMake:

```bash
cd leia_track_app
mkdir build && cd build
cmake .. -DLEIASR_ROOT="path/to/simulatedreality-SDK"
cmake --build . --config Release
```

## How It Works

```
Leia IR Camera → SR SDK (eye positions)
    → Lean offset from calibrated center
    → One-Euro filter (speed-adaptive smoothing)
    → Magnetic center pull (gentle return to neutral)
    → Smooth dead zone (no snap at center)
    → Power curve sensitivity mapping
    → Clamp to max rotation
    → OpenTrack UDP (port 4242)
    → VRto3D → SteamVR camera rotation
```

## Tips

- **Calibrate often** — press Ctrl+C whenever you shift in your seat
- **Good lighting helps** — the IR camera works best with some ambient light
- **For racing/flight sims** — try lower sensitivity (Ctrl+J/N) for subtle looks
- **For action games** — try higher sensitivity (Ctrl+K/M) for quick glances
- **VRto3D's `launch_script`** — set to `"start leia_track_app.exe"` to auto-launch with VRto3D

## License

One-Euro Filter: BSD 3-Clause (Casiez/Roussel 2019)
Application: MIT
