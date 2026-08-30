# GLIntercept tracing for You Are Empty

Use the official **x86** GLIntercept 1.3.4 manual-install package because
`YOU_ARE_EMPTY.EXE` is a 32-bit process. Do not select the tracer architecture
from the Windows architecture.

`Prepare-YAETrace.ps1` validates all critical inputs before touching the game
directory:

- the GLIntercept DLL and the QindieGL DLL must both be x86 PE images;
- `GLFunctions/gliIncludes.h` must exist;
- the official definition corpus is copied beside the game and extended by
  `QindieGL-gliIncludes.h` for the legacy YAE name `glMultiTexCoord4sdARB`;
- SUMMARY mode additionally requires `Plugins/GLFuncStats/GLFuncStats.dll`;
- an existing game-directory `opengl32.dll` is never overwritten.

Example:

```powershell
.\tools\glintercept\Prepare-YAETrace.ps1 `
  -Mode FULL `
  -GameRoot 'H:\YAE\Original\You Are Empty' `
  -GLInterceptRoot 'C:\Tools\GLIntercept_1_3_4' `
  -QindieGLDll '.\bin\ReleaseNoRemixMods\opengl32.dll'
```

FULL logs every call for at most 300 presented frames and flushes after every
call. Use it only for a short startup/crash reproduction. SUMMARY disables the
per-call text log and uses the official GLFuncStats plugin to write call counts
to `gliLog.txt`; combine it with QindieGL's own INFO/DEBUG session log for longer
gameplay.

After every run, reject the trace if `gliLog.txt` contains either
`FunctionParser::Parse` errors or `Unknown function ... being logged` messages.
Those diagnostics mean the function-definition corpus did not load and call
parameters in `gliInterceptLog_*.txt` are not authoritative.
