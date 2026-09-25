# Greatest Cube Slam

A 3D remake of [Cube Slam](https://github.com/schnabear/cubeslam) (the WebGL/WebRTC Chrome Experiment) in C++ with **SFML 3** and a small hand-written OpenGL 3D library.

Deflect the puck past CUBOT the robot's shields. First to 3 points wins the level, and there are 12 levels with obstacles, force fields, and power-ups.

## Build (Windows)

No preinstalled tools needed. The scripts download a portable toolchain into `.tools/` (no admin rights, no system changes):

```powershell
.\setup.ps1          # one-time: portable CMake 3.31 + MinGW-w64 GCC 16 (+ Ninja)
.\build.ps1 -Run     # fetches & builds SFML 3.1 from source, builds and launches the game
```

The result is a single self-contained `build\Release\CubeSlam.exe` (SFML and the C++ runtime are linked statically). `.\build.ps1 -Debug` makes a debug build with a console.

Other platforms: any C++17 compiler plus CMake 3.24+ works with `cmake -S . -B build && cmake --build build`.

## Controls

| Input | Action |
| --- | --- |
| Mouse, or A/D / arrow keys | move paddle |
| Click / Enter | start / continue |
| Left/Right on the title screen | choose starting level |
| Esc or P | pause (Q in the pause menu quits to the title screen) |
| M | mute |

## Power-ups

Hit a power-up with the puck. It goes to whoever touched the puck last.

| Power-up | Effect |
| --- | --- |
| Extra life | restores one of your destroyed shields |
| Fireball | your next hit launches a 1.5x fast fireball; whoever returns it gets dizzy |
| Ghost ball | the puck turns almost invisible |
| Big paddle | your paddle becomes 1.75x wider |
| Laser | your paddle fires lasers that shrink the opponent's paddle |
| Bulletproof | your shields are invulnerable for a while |
| Mirror | the opponent's controls are inverted |
| Fog | the opponent's view is fogged |
| Multiball | adds a second puck |
| Time bomb | the puck explodes after 4 s and destroys nearby shields |
| Death ball | whoever hits it loses the round |

## Code layout

```
src/gfx/     tiny 3D library: math (Vec/Mat4), GL function loader (via sf::Context),
             shaders, flat-shaded mesh builder (box/prism/cone/...), forward renderer
src/game/    World (fixed 60 Hz simulation: SAT collisions, puck/paddle/shield rules),
             Levels (the 12 levels + obstacle/force layouts), AI, Scene (3D visuals,
             particles, camera, CUBOT), Audio (all sound effects synthesized at runtime),
             Game (state machine + HUD)
```

For testing, `CubeSlam.exe --selftest` runs a headless simulation of all 12 levels, with both paddles automated, and checks for physics problems. `--level N --demo --shot out.png --shot-at 8` starts at level N with the autopilot playing, saves a screenshot after 8 seconds, and quits.

## Credits

Gameplay rules and level data are ported from the original Cube Slam source (MIT license) by Google Creative Lab / North Kingdom / Public Class. All code, visuals, the CUBOT character, and the sounds in this repository are new.
