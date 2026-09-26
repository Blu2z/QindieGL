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
# Remix runs: the Remix-enabled build as opengl32.dll
.\tools\remix\Set-YAEDllChain.ps1 -Chain Direct -QindieGLDll .\bin\Release.x86\opengl32.dll

# Back to GLIntercept; optionally install a build as QindieGL-traced.dll
.\tools\remix\Set-YAEDllChain.ps1 -Chain GLIntercept
```

The script identifies the DLLs by content, parks the inactive one as
`opengl32.glintercept.dll` or `opengl32.qindiegl.dll`, and refuses to touch
anything it does not recognise. It leaves `YOU_ARE_EMPTY.exe.local` alone.

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
  how DS2 built each view.
