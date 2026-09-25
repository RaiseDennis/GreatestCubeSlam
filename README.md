# Greatest Cube Slam

A 3D remake of [Cube Slam](https://github.com/schnabear/cubeslam) (the WebGL/WebRTC Chrome Experiment) in C++ with **SFML 3** and a small hand-written OpenGL 3D library.

Deflect the puck past CUBOT the robot's shields. First to 3 points wins the level, and there are 12 levels with obstacles, force fields, and power-ups.

You can also play another person, either on the same computer or online (see [Multiplayer](#multiplayer)).

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
| Up/Down on the title screen | choose the mode (1 player, 2 players, host, join) |
| Left/Right on the title screen | choose the starting level / arena |
| Esc or P | pause (Q in the pause menu quits to the title screen) |
| M | mute |

## Multiplayer

The 12 levels double as versus arenas. First to 3 points wins the match, then Enter moves to the next arena. Power-ups work the same as in single player.

**2 players, same computer.** Player 1 defends the near end with the mouse or A/D. Player 2 defends the far end with the Left/Right arrow keys. The camera switches to a shared overhead view. Fog is left out in this mode because it would blind both players.

**Online.** One player picks *Host online game*. The lobby shows their LAN address and the port (TCP 27015). The other player picks *Join online game* and types that address, optionally as `address:port`. Each player sees the arena from their own end. For play over the internet, the host forwards TCP port 27015 on their router and shares their public IP. Both players must run the same version of the game.

How it works: the host runs the authoritative simulation and sends a snapshot of the world (plus its sound/particle events) after every 60 Hz step. The client sends only its paddle position, which it also predicts locally so its own paddle responds without lag. Pausing pauses the game for both players. If either player quits, the other goes back to the title screen.

Command line shortcuts: `--local`, `--host`, `--join ADDRESS[:PORT]`, and `--port N` (for hosting, or the default port when joining). They combine with `--level N` and `--demo`.

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
             Net (online play: TCP transport + snapshot protocol), Game (state machine, modes + HUD)
```

For testing, `CubeSlam.exe --selftest` runs a headless simulation of all 12 levels, with both paddles automated, and checks for physics problems. It then plays two-player matches and pushes every step through the network snapshot format. `--level N --demo --shot out.png --shot-at 8` starts at level N with the autopilot playing, saves a screenshot after 8 seconds, and quits.

## Credits

Gameplay rules and level data are ported from the original Cube Slam source (MIT license) by Google Creative Lab / North Kingdom / Public Class. All code, visuals, the CUBOT character, and the sounds in this repository are new.
