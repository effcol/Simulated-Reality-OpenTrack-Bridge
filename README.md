# Simulated Reality OpenTrack Bridge

**A fork of Leia Track App focused on OpenTrack integration with full 6DOF head tracking support.**

Standalone head tracking for any OpenTrack-compatible game using a Leia Simulated Reality sparse lightfield display.

Reads head pose from the LeiaSR Runtime, applies a [One-Euro filter](https://gery.casiez.net/1euro/) for smooth adaptive filtering on yaw, pitch and roll, and sends stable 6DOF pose (position + orientation) as OpenTrack UDP to [OpenTrack](https://github.com/opentrack/opentrack).

## Games

Some of the games playable in 3D and headtracking on Leia Simulated Reality monitors:

| Title | 3D support | OpenTrack Support |
|-------|------|-------|
| Assetto Corsa | [Geo-11](https://github.com/Stereoscopic3D/ShaderFixes/releases/tag/AssettoCorsa) | Native |
| Assetto Corsa Competizione | Acer TrueGame | Native |
| BeamNG.drive | [Geo-11](https://helixmod.blogspot.com/2023/11/beamngdrive.html) | Native |
| DCS World | [Geo-11](https://helixmod.blogspot.com/2018/02/dcs-25-3d-fix-wip.html) | Native |
| Dirt Rally 2.0 | Acer TrueGame/[NewAxis](https://github.com/marcussacana/NewAxis) | Native |
| Dying Light 2 | [SuperDepth3D](https://airtable.com/appByPZJsOQSVGDID/shrAfMuGs1IOIEpRT?uMgeK=reci0NN7x8YDzjtch) | [Head Tracking Mod](https://www.nexusmods.com/dyinglight2/mods/1900) |
| Elite Dangerous | Native | Native |
| Euro Truck Simulator 2 | Acer TrueGame/[NewAxis](https://github.com/marcussacana/NewAxis) | Native |
| Gone Home | [VorpX](https://www.vorpx.com/) | [Head Tracking Mod](https://github.com/itsloopyo/gone-home-headtracking) |
| Green Hell | [Geo-11](https://helixmod.blogspot.com/2026/02/green-hell-geo11-fix.html) | [Head Tracking Mod](https://www.nexusmods.com/greenhell/mods/83) |
| MechWarrior 5: Mercenaries | [Geo-11](https://helixmod.blogspot.com/2020/01/mechwarrior-5-mercenaries.html) | Native |
| Minecraft: Java Edition | [Stereo 3D mod](https://modrinth.com/mod/stereopsis) | [Head Tracking Mod](https://www.curseforge.com/minecraft/mc-mods/correct-gaming-posture) |
| Outer Wilds | [Geo-11](https://helixmod.blogspot.com/2019/07/outer-wilds-3d-vision-fix-losti-v100.html) | [Head Tracking Mod](https://outerwildsmods.com/mods/headtracking/) |
| Star Wars: Squadrons | [Geo-11](https://helixmod.blogspot.com/2025/07/star-wars-squadrons-geo-11-fix-update.html) | Native |

*Add more games to this table as tested and mods released them.*

**Recommended play modes:**
- **XYZ + Yaw/Pitch** (Mode Z) — Best for most games, full 3-axis head tracking (position + yaw/pitch rotation) without roll
- **XYZ only** (Mode X) — Pure positional tracking, matches native Leia Simulated Reality display support in games like Stellar Blade, Lies of P and The First Berserker: Khazan

**by [evilkermitreturns](https://github.com/evilkermitreturns)** & [effcol](https://github.com/effcol)

## Requirements

- Leia Simulated Reality display (e.g. Acer SpatialLabs, Samsung Odyssey 3D) with LeiaSR Runtime installed and running
- [OpenTrack](https://github.com/opentrack/opentrack)
- [LeiaSR SDK](https://www.immersity.ai/sdk/) (for building from source)

## How to start

1. Run `Simulated_Reality_OpenTrack_Bridge.exe`
2. Launch OpenTrack
3. Set Input to UDP over network (should be on this by default)
4. Press Start in OpenTrack
5. Launch game or app of choice.
6. Look around naturally - no manual recenter needed.

## Controls

Hotkeys only work when the console window is focused — they won't interfere with your game.

### Tuning Hotkeys

| Key | Action |
|-----|--------|
| **Ctrl+L** | Lock/unlock tuning hotkeys |
| **1/2** | Yaw/Pitch/Roll smoothness (min_cutoff down/up) |
| **3/4** | Yaw/Pitch/Roll response speed (beta down/up) |
| **5/6** | Yaw sensitivity (down/up) |
| **7/8** | Pitch sensitivity (down/up) |
| **9/0** | Roll sensitivity (down/up) |
| **-/=** | Toggle translation passthrough |
| **[/]** | Toggle radians/degrees orientation input |

### Output Modes

Press one of these keys to switch output modes during gameplay:

| Key | Mode | Description |
|-----|------|-------------|
| **Z** | 1: XYZ + Yaw/Pitch | **Recommended** — 3D position + head look (default) |
| **X** | 2: XYZ only | Pure positional tracking, no rotation |
| **C** | 3: XYZ + Yaw/Pitch/Roll | Full 6DOF (position + all rotations) |
| **V** | 4: Yaw/Pitch/Roll only | Head rotation only, no position |
| **B** | 5: Yaw/Pitch only | Head look without roll (reduces jitter) |

### Output Target

| Key | Target | Description |
|-----|--------|-------------|
| **A** | OpenTrack | No inversion (default) |
| **S** | SteamVR/VRto3D | With X/Yaw/Roll inversion |

Settings auto-save on every change. Press **Ctrl+L** to lock hotkeys and prevent accidental changes.

## Settings

Settings are saved to `Steam/config/vrto3d/leia_track_config.txt` (next to VRto3D's config) and auto-load on startup.

| Setting | Default | Description |
|---------|---------|-------------|
| `filter_mincutoff` | 0.08 | Yaw/Pitch/Roll smoothness at rest. Lower = smoother but laggier. Position is unfiltered (1:1). |
| `filter_beta` | 0.08 | Yaw/Pitch/Roll responsiveness to fast movement. Higher = less lag |
| `angle_deadzone_deg` | 0.2 | Ignore tiny angle noise around center |
| `orientation_radians` | 1 | Set to 1 when SDK orientation output is radians |
| `sens_yaw` | 1.0 | Yaw output scale |
| `sens_pitch` | 1.0 | Pitch output scale |
| `sens_roll` | 1.0 | Roll output scale |
| `yaw_offset` | 0.0 | Yaw trim offset in degrees |
| `pitch_offset` | 0.0 | Pitch trim offset in degrees |
| `roll_offset` | 0.0 | Roll trim offset in degrees |
| `max_yaw` | 70 | Maximum yaw output (degrees) |
| `max_pitch` | 70 | Maximum pitch output (degrees) |
| `max_roll` | 70 | Maximum roll output (degrees) |
| `passthrough_translation` | 1 | Include position X/Y/Z in OpenTrack packet |
| `invert_x` | 0 | Invert X translation output (managed by output target) |
| `invert_yaw` | 0 | Invert yaw output (managed by output target) |
| `invert_roll` | 0 | Invert roll output (managed by output target) |
| `output_mode` | 1 | 1: XYZ+YP (default), 2: XYZ, 3: XYZ+YPR, 4: YPR, 5: YP |
| `output_target` | 1 | 1: OpenTrack (default), 2: SteamVR/VRto3D |

### Editing the Config File

The config file is plain text — open it in any text editor. Default location:

```
C:\Program Files (x86)\Steam\config\vrto3d\leia_track_config.txt
```

Example contents:
```
# Leia Track App — Settings
filter_mincutoff = 0.08
filter_beta = 0.08
angle_deadzone_deg = 0.2
orientation_radians = 1
sens_yaw = 1
sens_pitch = 1
sens_roll = 1
yaw_offset = 0
pitch_offset = 0
roll_offset = 0
max_yaw = 70
max_pitch = 70
max_roll = 70
passthrough_translation = 1
invert_x = 0
invert_yaw = 0
invert_roll = 0
output_mode = 1
output_target = 1
```

Lines starting with `#` are comments. Any setting not in the file uses its default value. Changes take effect next time you launch the app.

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
Leia IR Camera → SR SDK (head pose)
    → Orientation unit conversion (radians/degrees)
    → One-Euro filter (speed-adaptive smoothing)
    → Sensitivity + trim offsets
    → Clamp to max rotation
    → Optional translation passthrough
    → OpenTrack UDP (port 4242)
    → OpenTrack Software → Any Supported Game
```

## Tips

- **Good lighting helps** — the IR camera works best with some ambient light
- **Lock when playing** — press Ctrl+L to prevent accidental hotkey presses

## License

One-Euro Filter: BSD 3-Clause (Casiez/Roussel 2019)
Application: MIT
