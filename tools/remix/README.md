# RTX Remix runs of You Are Empty

Phase G setup for running You Are Empty (x86, DS2 engine) through QindieGL and
RTX Remix. See `STATUS.md` for the current state.

## DLL chains

`ds2render.dll` imports `OPENGL32.dll`, so the game directory's
`opengl32.dll` decides the chain:

| Chain | `opengl32.dll` | Use |
|---|---|---|
| `GLIntercept` | GLIntercept 1.3.4 x86 loader, forwarding to `QindieGL-traced.dll` (`GLSystemLib` in `gliConfig.ini`) | GL call traces, Ctrl+Shift+F frame traces (`tools/glintercept`) |
| `Direct` | QindieGL itself | RTX Remix runs, fewer moving parts |

QindieGL loads Direct3D with `LoadLibrary("d3d9.dll")`, so a Remix bridge
`d3d9.dll` in the game directory is picked up in both chains. QindieGL refuses
to create a context if that `d3d9.dll` does not export `D3DPERF_BeginEvent`
and `D3DPERF_EndEvent`.

Switch with `Set-YAEDllChain.ps1`, with the game closed:

```powershell
# Remix runs: the Remix-enabled build as opengl32.dll, Remix bridge on
.\tools\remix\Set-YAEDllChain.ps1 -Chain Direct -QindieGLDll .\bin\Release.x86\opengl32.dll -Remix On

# Without Remix: park the bridge as d3d9.remix.dll
.\tools\remix\Set-YAEDllChain.ps1 -Remix Off

# Back to GLIntercept; optionally install a build as QindieGL-traced.dll
.\tools\remix\Set-YAEDllChain.ps1 -Chain GLIntercept

# Only print the current state
.\tools\remix\Set-YAEDllChain.ps1
```

The script identifies the DLLs by content, parks the inactive ones as
`opengl32.glintercept.dll`, `opengl32.qindiegl.dll` or `d3d9.remix.dll`, and
refuses to touch anything it does not recognise. It leaves
`YOU_ARE_EMPTY.exe.local` alone.

## Remix runtime

Installed from the official release `remix-1.5.2`
(`remix-1.5.2-release.zip`, GitHub NVIDIAGameWorks/rtx-remix, published
2026-06-16); the binaries were checked against the release's SHA256 list.
Only these parts are copied next to `YOU_ARE_EMPTY.EXE`:

- `d3d9.dll`: the x86 bridge client. It exports `D3DPERF_BeginEvent`,
  `D3DPERF_EndEvent` and `remixapi_InitializeLibrary`.
- `.trex\`: `NvRemixBridge.exe` (x64 bridge server), `d3d9.dll` (x64
  DXVK-Remix renderer) and their dependencies.
- The license files.

`d3d8to9.dll` (Direct3D 8 games) and `NvRemixLauncher32.exe` (injection for
games that do not load `d3d9.dll` from their directory) are not needed.

Remix opens its menu with Alt+X. Remix 1.5.2 writes its logs to
`rtx-remix\logs\` in the game directory: `bridge32.log` (bridge client),
`bridge64.log` (bridge server) and `remix-dxvk.log` (renderer), all
overwritten at each launch. A server crash leaves
`.trex\NvRemixBridge.exe_<date>_<time>.dmp`. Without `bridge.conf`,
`dxvk.conf` and `rtx.conf` the runtime uses its defaults.

## Builds

- `ReleaseNoRemixMods` (`bin\ReleaseNoRemixMods\opengl32.dll`): no Remix API,
  ImGui overlay or Detours hooks.
- `Release|Win32` through `msvc\QindieGL.sln` (`bin\Release.x86\opengl32.dll`):
  rebuilds `idtech3_mixup` and links Detours. For You Are Empty the hooks find
  no configured targets (`SurfaceSort: detouring result: 0 hint: 0`). F8 or
  Alt+C toggles the ImGui overlay; the Remix API is used only with
  `remixapi = 1`, which the YAE sections do not set.

## QindieGL settings for Remix

In `[game.game]` and `[game.YOU_ARE_EMPTY]` of `QindieGL.ini`:

- `yae_camera_split = 1`: DS2's camera reaches D3D9 as `D3DTS_VIEW`. The
  output without Remix is unchanged; the session log's camera census shows
  how DS2 built each view. DS2 gives its lights in world coordinates under
  the camera, and they reach D3D9 unchanged. Remix tells game lights apart by
  their exact position: a lamp whose position changed in the last bits as
  the camera moved counted as a moving light and went out as soon as DS2
  stopped passing it, which it does when no lit model is near the lamp.
- `remix_server_all_cpus = 1` (set in `msvc/QindieGL.ini`): required.
  `ds2kernel.dll` pins the game to CPU 0 with
  `SetProcessAffinityMask(GetCurrentProcess(), 1)`, and `NvRemixBridge.exe`,
  which the bridge's `Direct3DCreate9` starts with `HIGH_PRIORITY_CLASS`,
  inherits that affinity. The server's busy-waiting threads then keep the
  game's thread off CPU 0: the game freezes at the logo, its window stops
  responding and `remix-dxvk.log` repeats "Message channel ... handshake
  timeout". With the key, QindieGL runs `Direct3DCreate9` with every CPU
  allowed and restores the game's pin afterwards (`[REMIX]` lines in
  `QindieGL.log`). Check with Task Manager (Details, Set affinity) that
  `NvRemixBridge.exe` may use every CPU.

## DS2 settings for Remix

DS2's model shadows must be off for Remix runs. In
`config_user\startup_autorun.cmd` (DS2 writes it back at exit):

```
set r_mdl_fake_shadows                 0
set r_mdl_shadows                      0
```

With shadows on, each frame starts by drawing shadow-caster silhouettes with
an orthographic light view and depth writes off into a 512x512 corner of the
back buffer. Remix treats such draws as UI (`isRenderingUI` in
`d3d9_rtx.cpp`) and ray traces the frame at the first one, before the world
is drawn, so those frames show the rasterized image: the picture flickers
between that and the ray-traced frame. The shadow receivers also draw the
world a second time with a projected texture, which Remix does not support.
Remix computes its own shadows. Turn the shadows back on for comparisons
without Remix.

## Remix settings

`tools/remix/rtx.conf` holds the Remix options for You Are Empty; copy it
into the game directory. The bridge server inherits the game's working
directory, where Remix looks for `rtx.conf` and `user.conf`. Saving settings
from the Remix menu rewrites `rtx.conf` without its comments; copy new
entries such as texture tags back here. The file explains each option; the
two that matter most:

- `rtx.dlfg.enable = False`: with DLSS frame generation, which Remix enables
  by default on GPUs that support it, the first level load crashed the
  server in the NVIDIA driver (`vkQueueSubmit`); runs without it did not.
- `rtx.lightConversionMaxIntensity`: DS2 lights its models with GL lights
  that have no attenuation, and QindieGL passes their unlimited range as
  D3D9 `Range = sqrtf(FLT_MAX)`. Remix sizes a converted light to reach its
  range, so without the cap every lamp is about 1e38 bright and the picture
  is white.

Decals (blood, dirt) are drawn with multiply blending over the world. Remix
needs their textures in `rtx.decalTextures`, or they become transparent
holes: Alt+X, Developer Settings Menu, Game Setup, Step 1: Categorize
Textures, then click the decal in the game and tick Decal Texture.
