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

**Online, peer to peer, no port forwarding.** One player picks *Host online game* and gets a join code such as `TYYQ-GRWV`. They send it to their friend however they like. The friend picks *Join online game*, types or pastes the code, and presses Enter. Each player sees the arena from their own end. Both players must run the same version of the game.

If the friend can't get in (both routers are strict), the Join screen also shows the friend's own code. The host types it into the box on the Host screen while the friend stays on the Join screen. Now both sides knock at the same time, which gets through most routers. On the same network, the joiner can also type the host's LAN IP address instead of a code.

How the connection works, with no server of our own, the same way many peer-to-peer games and WebRTC connect:

1. **Finding your address.** Each game asks a public STUN server (Google's or Cloudflare's) what its address looks like from the internet. It also asks the router to forward its UDP port automatically (UPnP), if the router allows that. The join code is that public address and port, written with an unambiguous alphabet and a checksum that catches typos. It is 8 characters when the router keeps the game's port (UDP 27015) and 12 otherwise.
2. **Hole punching.** The joiner keeps sending packets to the host's code. A router lets packets in from an address it has just sent to, so when both sides send to each other, both routers open. That is why typing each other's codes helps. On a LAN the joiner also broadcasts, because many routers can't loop traffic for their own public address back inside.
3. **When it can't work.** If both players are behind "symmetric" NATs (some mobile hotspots, company or university networks, carrier-grade NAT), there is no way through without a relay server, and the lobby warns about a strict router. Swapping who hosts sometimes helps. The first time you host, Windows may ask whether to allow Cube Slam on the network. Allow it.

Note: a join code contains your public IP address, just like a URL to your own server would. Only share it with people you want to play with.

How the game runs over the connection: the host runs the authoritative simulation and sends a snapshot of the world (plus its sound/particle events) after every 60 Hz step. The client sends only its paddle position, which it also predicts locally so its own paddle responds without lag. Snapshots and paddle positions travel unreliably, and only the newest one counts. The handshake, the game flow and pause requests go over a small reliable, ordered channel with acknowledgements and resends. Pausing pauses the game for both players. If either player quits, the other goes back to the title screen.

Command line shortcuts: `--local`, `--host`, `--join CODE` (or `--join ADDRESS[:PORT]` on a LAN), and `--port N` (the UDP port to host on, or the default port for joining by address). They combine with `--level N` and `--demo`.

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
             Net (online play: UDP peer to peer, join codes, STUN/UPnP, reliable channel,
             snapshot protocol), Game (state machine, modes + HUD)
```

For testing, `CubeSlam.exe --selftest` runs a headless simulation of all 12 levels, with both paddles automated, and checks for physics problems. It then plays two-player matches and pushes every step through the network snapshot format, checks join codes, and runs two connections over loopback (one with 30% simulated packet loss). `--level N --demo --shot out.png --shot-at 8` starts at level N with the autopilot playing, saves a screenshot after 8 seconds, and quits.

## Credits

Gameplay rules and level data are ported from the original Cube Slam source (MIT license) by Google Creative Lab / North Kingdom / Public Class. All code, visuals, the CUBOT character, and the sounds in this repository are new.
