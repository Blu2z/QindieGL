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
