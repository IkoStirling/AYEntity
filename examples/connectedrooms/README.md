# Connected Rooms (interactive headless example)

This small playable console scene has two rooms with overlapping coordinates,
two directed doors, one capsule player, one movable crate and one static marker
per room. Both room markers occupy the same coordinates in the shared Jolt
world, but their collision layers and region queries remain isolated.

Build from the engine root with a Jolt-enabled 3D configuration:

```powershell
cmake --build out/build/windows-client-3d-debug --target AYEntity_ConnectedRoomsExample
out/build/windows-client-3d-debug/AYRuntime/AYEntity/AYEntity_ConnectedRoomsExample.exe
```

`w` walks forward through the door; `s` walks backward and can return through
the reverse door. `p` sends the crate through the door when it shares the
player's region. `q` casts a region-filtered ray at the overlapping room
markers; `x` exits. The display reports each object's logical room and the
player's feet position. `--script` runs a finite crate/player round trip and
returns a failing exit code if the two room memberships or ray query disagree.

This example uses a console view so it needs no art or window. The separate
`AYEntity_ConnectedSpaceRenderTests` and Renderer GPU portal test cover view
planning and portal-surface sampling; this example does not show a rendered
portal. It uses real Jolt fixed steps and completed snapshots.
