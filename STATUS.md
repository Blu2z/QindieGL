# You Are Empty compatibility status

## Phase A — instrumentation

Status: **complete in source, built, and smoke-tested**. The Phase A acceptance
criterion is met: the observed startup crash is correlated to an API pointer,
the exact `ds2render.dll` caller RVA, and the preceding GL/WGL event sequence.

YAE does not reach the menu yet. The remaining hardware-detection failure is a
Phase B startup/capability issue, not an instrumentation failure.

### Implemented diagnostics

- Structured logging with `0=ERROR`, `1=WARN`, `2=INFO`, `3=DEBUG`, and
  `4=TRACE`; INFO is the default.
- Frame and per-frame draw IDs. A frame advances only after a successful real
  D3D9 `Present`; `D3DERR_WASSTILLDRAWING` does not create a false frame.
- Requested-procedure accounting split into implemented, unknown, disabled,
  stubbed-requested, and stubbed-actually-invoked categories.
- Standards-compatible `wglGetProcAddress`: intentionally disabled and unknown
  procedures return `NULL` instead of a shared fake function pointer.
- Startup capability block containing executable/architecture, exposed OpenGL
  identity, D3D adapter/caps, diagnostic settings, and important advertised GL
  and WGL extensions.
- INFO events for the texture-unit and ARB-program limits used during hardware
  detection.
- Shutdown summary containing frames/draws, procedure categories, unsupported
  enum origins, D3D failures, resets, PBuffers, ARB program totals, and VBO
  allocation totals.
- D3D failures record HRESULT, API call, frame, and draw origin. GL errors retain
  the wrapper function that produced them until `glGetError` consumes the error.
- A fixed 256-event crash ring. Unhandled-exception reports include exception
  parameters, x86 registers, raw stack words, executable stack addresses as
  `module+RVA`, current state, and recent GL/WGL/D3D events.
- `DebugMaxDrawCall`, `DebugDumpFrame`, and `DebugDumpDraw` support draw binary
  search and a selected-draw state dump.

### Reliable GLIntercept workflow

`tools/glintercept/` contains validated x86 FULL and SUMMARY configurations plus
`Prepare-YAETrace.ps1`.

The preparation script:

- verifies that both GLIntercept and QindieGL are x86;
- verifies required definition/plugin files;
- refuses to overwrite an existing game setup;
- copies the official GL function-definition corpus beside the game;
- installs a small QindieGL overlay for DS2's historical
  `glMultiTexCoord4sdARB` spelling.

GLIntercept 1.3.4 requires the first line of every definition file to be a
unique `#define`; the overlay now satisfies this parser requirement. QindieGL
also exports `wglSwapMultipleBuffers`, which GLIntercept requires while mapping
the OpenGL 1.1/WGL surface.

The captured hardware-detection trace is clean:

```text
FunctionParser errors: 0
Unknown function diagnostics: 0
Unknown/??? parameters: 0
```

### Smoke-test configuration

Game: `H:\YAE\Original\You Are Empty\YOU_ARE_EMPTY.EXE`

```text
screen_w=1920
screen_h=1080
fullscreen=1
refresh=60
hdr=0
use_shaders=0
use_normalmaps=0
```

Final installed QindieGL profile:

```ini
[Settings]
LogLevel = 2
CrashDiagnostics = 1
DebugMaxDrawCall = -1
DebugDumpFrame = -1
DebugDumpDraw = -1
EnableARBProgramsStub = 0

[Extensions]
GL_ARB_vertex_buffer_object = 0
```

The 3440x1440 attempt failed before renderer initialization with
`Can't Initialize Screen Resolution`. The 1920x1080 monitor/config removes that
separate test-environment blocker.

### Observed startup blocker for Phase B

With the truthful baseline, ARB vertex/fragment programs are not advertised and
their procedures return `NULL`. DS2 correctly detects the missing fragment
extension, but then unconditionally calls its null `glGetProgramivARB` slot.

The enhanced crash report resolved the otherwise unhelpful `EIP=0` failure to:

```text
Exception: 0xC0000005, execute address 0
Frame: 0
Draw: 0
Caller: ds2render.dll+0x1FB7E
API slot: glGetProgramivARB
Arguments: GL_FRAGMENT_PROGRAM_ARB,
           GL_MAX_PROGRAM_ALU_INSTRUCTIONS_ARB,
           output pointer
```

Disassembly confirms that the indirect call at `ds2render.dll+0x1FB7E` is not
guarded after the extension-absent branch. This is now a deterministic Phase B
capability/fallback problem rather than an opaque crash.

An isolated ARB capability probe was also run. After supplying ABI-correct
addresses for every DS2-requested ARB procedure, the trace contained no unknown
procedures and no crash, but DS2 still displayed `engine can't be run on this
hardware` after its final limit queries. That probe was archived and was not
left enabled because the shader implementation is incomplete and the target
configuration is `use_shaders=0`.

### Files changed

- `code/d3d_diagnostics.cpp/.hpp`
- `code/d3d_wrapper.cpp/.hpp`
- `code/d3d_global.cpp/.hpp`
- `code/d3d_array.cpp`, `code/d3d_immediate.cpp/.hpp`
- `code/d3d_extension.cpp/.hpp`, `code/d3d_get.cpp`
- buffer, state, texture, matrix, lighting, list, pixel, clip, blend, and
  PBuffer wrappers now route GL errors through origin-preserving diagnostics
- `msvc/QindieGL-src.vcxproj` and `.filters`
- `msvc/QindieGL.ini`
- `msvc/opengl32.def`
- `tools/glintercept/`

### Build and verification

Final build command used Visual Studio 18 Community's installed MSVC toolchain
with a Win32 `v145` command-line override:

```powershell
MSBuild.exe msvc\QindieGL-src.vcxproj /t:Rebuild /m `
  /p:Configuration=ReleaseNoRemixMods /p:Platform=Win32 `
  /p:PlatformToolset=v145
```

Results:

- full rebuild: passed;
- output: `bin\ReleaseNoRemixMods\opengl32.dll`;
- PE architecture: x86 (`0x14C`);
- `wglGetProcAddress`, `wglSwapBuffers`, and `wglSwapMultipleBuffers` exports:
  present;
- project/filter XML parse: passed;
- trace-preparation PowerShell parse: passed;
- `git diff --check`: passed; only Git LF-to-CRLF notices were emitted;
- final DLL SHA-256 matched the DLL deployed as
  `QindieGL-traced.dll` in the game directory.

The rebuild retains pre-existing warnings in the ARB compiler, pixel path,
miscellaneous string conversion, and Remix helper code; no build errors occur.

### Captured artifacts

- Clean hardware probe trace:
  `H:\YAE\Original\You Are Empty\gliInterceptLog_FULL.phaseA-clean-arb-probe.txt`
- GLIntercept parser log for that trace:
  `H:\YAE\Original\You Are Empty\gliLog.phaseA-clean-arb-probe.txt`
- Structured QindieGL probe log:
  `H:\YAE\Original\You Are Empty\QindieGL.phaseA-clean-arb-probe.log`
- Enhanced null-call crash report:
  `H:\YAE\Original\You Are Empty\QindieGL-crash-20260830-172618.txt`
- Earlier attempts are preserved under descriptive `phaseA-*` names; original
  pre-test DLL/INI/config backups are preserved as `phaseA-preexisting` files.

### Milestone state

- Native vs QindieGL visual parity: not evaluated; no rendered frame yet.
- VBO: disabled for the initial YAE path; not declared working.
- ARB shaders: unused by the game config; compatibility path remains partial
  and is disabled in the final profile.
- PBuffers: entry points were requested, but no PBuffer was created.
- D3D failures: none before hardware detection terminates.
- Pure QindieGL to D3D9: not stable/playable and not ready for RTX Remix.
- Commits created: none.

The next scoped task is Phase B: make DS2 select its genuine non-shader fallback
without advertising incomplete ARB support, then reach the main menu and first
level.

## Phase B — startup and capability detection

Status: **complete and tested through the first playable level**. YAE now boots,
passes the DS2 hardware gate, renders a stable main menu, and reaches gameplay
both from the console and through the normal New Game flow.

### Hardware gate and compatibility profile

Disassembly of `ds2render.dll` established that its startup gate requires all of
the following, even with `use_shaders=0`:

```text
GL_ARB_fragment_program
GL_ARB_vertex_program
GL_ARB_vertex_buffer_object
GL_EXT_draw_range_elements
```

The texture-unit and ARB-program limit queries are logged but are not themselves
the final pass/fail comparisons. DS2 also resolves and calls the complete ARB
program entry-point surface without consistently guarding null pointers.

The exception is isolated behind `yae_fallback_compatibility=1` in both known YAE
executable profiles, `[game.game]` and `[game.YOU_ARE_EMPTY]`. It exposes the
minimum gate extensions and ABI-correct entry points needed by this engine. The
global/default profile remains unchanged.

### Startup fixes

- Added the complete ARB vertex-attribute entry-point surface requested by DS2.
- Made `GL_PROGRAM_ERROR_POSITION_ARB` return the required no-error value `-1`.
- Implemented correct CPU-backed VBO offset resolution for legacy vertex arrays
  and element arrays. This fixed the startup/level-load null dereference caused
  by `glVertexPointer(..., 0)` while an array buffer was bound.
- Saved the array-buffer binding at array specification time and validated the
  referenced byte range when resolving it for a draw.
- Silently accepted `GL_LINE_SMOOTH`, which DS2 enables but QindieGL cannot map
  directly to D3D9.
- Capped repeated invalid-combiner diagnostics so a bad scene cannot grow the
  log without bound.
- Added a YAE-only `GL_TEXTURE_RECTANGLE` storage compatibility path. Rectangle
  textures are backed by D3D9 2D texture storage; the three DS2 startup surfaces
  now allocate without `GL_INVALID_OPERATION`.

### Test configuration

```text
1920x1080x32 fullscreen at 60 Hz
depth_bpp=24
stencil_bpp=0
hdr=0
use_shaders=0
use_normalmaps=0
motion_blur=false
use_cached_skinning=false
```

The reproducible engine configuration is stored as
`tools/glintercept/ds2engine.phase-b-test.cfg`.

The final New Game validation ran for 3272 presented frames and 447085 draw
calls. The QindieGL summary reported:

```text
unsupported enums: none
failed D3D calls: none
device resets: 0
PBuffers created: 0
ARB programs uploaded: 7
ARB programs compiled: 0
VBOs created: 184
peak VBO storage: 57990452 bytes
```

The user confirmed that the intro opens, its audio plays, and the first level
starts. The video image is currently reduced to a thin white strip; this is a
remaining rectangle/video-coordinate rendering defect, not a loading blocker.

### Captured Phase B artifacts

- Normal New Game run:
  `H:\YAE\Original\You Are Empty\QindieGL.phaseB-new-game-success.log`
- Matching clean GLIntercept trace:
  `H:\YAE\Original\You Are Empty\gliInterceptLog_FULL.phaseB-new-game-success.txt`
- Matching GLIntercept parser log:
  `H:\YAE\Original\You Are Empty\gliLog.phaseB-new-game-success.txt`
- Longer console-to-level run:
  `H:\YAE\Original\You Are Empty\QindieGL.phaseB-gameplay-console.log`
- First QindieGL gameplay screenshot:
  `tools/glintercept/phaseB-texture2d-current.png`

### Compatibility exceptions and remaining work

The task's proposed Phase C configuration assumes that DS2 can run with VBO and
ARB programs absent. The retail `ds2render.dll` disproves that assumption: it
hard-requires both families and, after loading the level, invokes the ARB program
API heavily even though `use_shaders=0`. The present Phase B profile therefore:

- advertises VBO and uses corrected CPU-backed VBO semantics;
- exposes ARB programs in explicit compatibility-stub mode;
- keeps HDR, normal maps, and optional material shaders disabled.

This is sufficient for startup but not visual parity. In the first level, base
textures are missing from much of the static geometry, lightmaps are mapped
incorrectly, nearby geometry can disappear, input/frame pacing is jerky, and
performance is low. These are the starting defects for Phase C. The trace shows
seven actual DS2 ARB programs and no generic vertex-attrib-array invocations, so
the first Phase C experiment should compile that small observed program corpus
instead of broadening generic OpenGL support.

Pure QindieGL to D3D9 is now usable for Phase C investigation, but it is not
visually correct, stability-qualified, or ready for RTX Remix.

## Phase C - basic world-rendering checkpoint

Status: **validated in the first playable level**.

The following QindieGL rendering fixes are now working together in the YAE
fallback profile:

- static and dynamic geometry remain visible at normal viewing distances;
- the lightmap atlas uses the correct vertical orientation;
- VBO-backed index ranges no longer produce full-screen flashing triangles;
- texture matrices use affine 2D transforms without corrupting the vertex layout;
- the menu remains intact after entering and leaving gameplay.

The apparent missing-diffuse defect was traced to modified game data rather
than another texture-stage bug. The active `med1.ds2` and `med1_lm_0.tga` had
been rebuilt on February 21, 2026. That scene registered only 219 materials,
created 11 static vertex buffers, requested a missing `baked_atlas_0` four
times, and intentionally bound the neutral gray texture to the lightmapped
static diffuse stage. Replacing that stage with a diagnostic texture proved
that its UVs and lightmap combine state were already correct.

Restoring the retail files dated July 28, 2006 removed the missing-atlas
requests. The game then registered 338 scene materials and created 16 static
vertex buffers in the QindieGL path. The user confirmed that static diffuse
textures, lightmaps, geometry stability, and the menu all render correctly.

Known retail asset hashes for this test installation:

```text
med1.ds2       B5DAF95A6606725AF54B9C3598FD15BFDAD97725038CC5E7F245931F9C11D529
med1_lm_0.tga  E4CB830A2C12431837D0937C2041DE0E87472847ED75764F2DE5AEC27D6341EF
```

The modified files were preserved outside the repository as
`*.phaseC-rebuilt-backup`; they are not required by QindieGL.

### Intro video and fullscreen recovery

The intro AVI is a 720x420 `GL_TEXTURE_RECTANGLE` upload. OpenGL rectangle
coordinates are expressed in pixels, but the D3D9 2D texture used by the YAE
compatibility path requires normalized coordinates. Fixed-function vertex-array
and immediate-mode draws now scale S/T by the bound rectangle texture size.
ARB fragment-program draws are deliberately excluded because the generated
shader already applies its own `_rectScale` constant.

The first complete-video test exposed a separate pre-existing fullscreen-reset
failure. QindieGL retained the interface returned by `GetSwapChain(0)` while
calling `IDirect3DDevice9::Reset`, causing `D3DERR_INVALIDCALL`; subsequent
draws targeted the invalid device and a failed dynamic-buffer allocation could
leave a dangling COM pointer. Reset now ends the active scene, releases and
reacquires the implicit swap chain, recreates streaming resources, invalidates
cached render state, and reapplies it. Dynamic vertex/index allocation failures
also leave null, zero-sized slots and abort the affected draw safely.

User validation confirmed all of the following in one normal New Game run:

- the intro has a full image with correct orientation and synchronized audio;
- the first level appears after the video;
- control and menus work;
- static textures and lightmaps remain correct;
- no full-screen flashing triangles, hang, or crash occurs.

A second run explicitly exercised `Alt+Tab` and return to the fullscreen game.
The log recorded `Reset hr=0x00000000 S_OK`, the game remained fully usable,
and the session summary reported:

```text
unsupported enums: none
failed D3D calls: none
device resets: 1
ARB programs uploaded/compiled: 8/8
ARB program compilation failures: 0
peak VBO storage: 25175072 bytes
```

Phase C's basic world-rendering acceptance target is therefore complete for the
reference level. The known pickup post-effect alpha mismatch is deferred as an
advanced effect and does not block the Phase D stability run.

## Phase D - stability checkpoint

Status: **15-minute gameplay and repeated save loading validated without a
crash**.

An earlier save/load and menu run ended with the Microsoft runtime dialog
`R6025 - pure virtual function call`. QindieGL's crash report identified the
actual first fault as an access violation in `D3DTextureObject::CopyTextureSubLevel`.
DS2 had requested a 512x512 framebuffer copy immediately after `CreateTexture`
failed with `E_OUTOFMEMORY`; release assertions did not stop the wrapper from
dereferencing the resulting null D3D texture.

The failure paths used while extending a texture from one level to a complete
mip chain also leaked the newly-created COM texture and/or level surfaces when
a get/copy operation failed. Those paths now release every temporary resource
before returning. Texture image/subimage/copy entry points validate the backing
D3D texture and return a GL error instead of invoking through a null pointer.
Object-buffer growth no longer loses the original allocation when `realloc`
fails, and deleting a currently bound texture detaches all wrapper bindings
before destroying the object so no dangling texture pointers remain.

The validating run repeated the original save/load scenario, exercised menus,
performed two fullscreen device resets, and continued through more than 15
minutes of stable gameplay. Its clean shutdown summary reported:

```text
frames: 35127
draw calls: 16741901
failed D3D calls: none
device resets: 2
ARB programs uploaded/compiled: 8/8
ARB program compilation failures: 0
peak VBO storage: 39930622 bytes
```

No texture allocation/recreation warning, skipped framebuffer copy,
`GL_OUT_OF_MEMORY`, or new crash report occurred. Save loading, menus, and
`Alt+Tab` all remained operational.

Remaining observed rendering issues for follow-up:

- some menu/drop-down background quads extend outside their intended clipping
  area;
- materials which scroll their texture coordinates in native OpenGL currently
  appear static;
- the pickup post-effect works but lacks the original translucent appearance.
