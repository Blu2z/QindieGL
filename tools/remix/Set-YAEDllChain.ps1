[CmdletBinding()]
param(
    # Direct: QindieGL is the game's opengl32.dll (RTX Remix runs).
    # GLIntercept: the GLIntercept loader is opengl32.dll and forwards to
    # QindieGL-traced.dll (GL call traces, Ctrl+Shift+F frame traces).
    [Parameter(Mandatory = $true)]
    [ValidateSet('Direct', 'GLIntercept')]
    [string]$Chain,

    [string]$GameRoot = 'H:\YAE\Original\You Are Empty',

    # Optional QindieGL build to install: as opengl32.dll (Direct) or as
    # QindieGL-traced.dll (GLIntercept). Without it the installed build is kept.
    [string]$QindieGLDll
)

$ErrorActionPreference = 'Stop'

function Get-PeMachine {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [IO.File]::OpenRead($Path)
    try {
        $reader = New-Object IO.BinaryReader($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) {
            throw "Not a PE image: $Path"
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadUInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "Invalid PE signature: $Path"
        }
        return $reader.ReadUInt16()
    }
    finally {
        $stream.Dispose()
    }
}

# GLIntercept reads gliConfig.ini, QindieGL reads QindieGL.ini.
function Get-DllKind {
    param([Parameter(Mandatory = $true)][string]$Path)

    $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($Path))
    if ($text.Contains('gliConfig')) { return 'GLIntercept' }
    if ($text.Contains('QindieGL.ini')) { return 'QindieGL' }
    return 'unknown'
}

$root = (Resolve-Path -LiteralPath $GameRoot).Path
$active = Join-Path $root 'opengl32.dll'
$parkedInterceptor = Join-Path $root 'opengl32.glintercept.dll'
$parkedQindieGL = Join-Path $root 'opengl32.qindiegl.dll'
$traced = Join-Path $root 'QindieGL-traced.dll'

$source = $null
if ($QindieGLDll) {
    $source = (Resolve-Path -LiteralPath $QindieGLDll).Path
    if ((Get-PeMachine -Path $source) -ne 0x014C) {
        throw "QindieGL DLL is not x86: $source"
    }
    if ((Get-DllKind -Path $source) -ne 'QindieGL') {
        throw "Not a QindieGL build: $source"
    }
}

if (-not (Test-Path -LiteralPath $active)) {
    throw "Missing $active"
}
$current = Get-DllKind -Path $active

if ($Chain -eq 'Direct') {
    if ($current -eq 'GLIntercept') {
        if (Test-Path -LiteralPath $parkedInterceptor) {
            throw "Refusing to overwrite $parkedInterceptor. Move it away first."
        }
        $parked = (Test-Path -LiteralPath $parkedQindieGL) -and (Get-DllKind -Path $parkedQindieGL) -eq 'QindieGL'
        Rename-Item -LiteralPath $active -NewName (Split-Path -Leaf $parkedInterceptor)
        if ($source) {
            Copy-Item -LiteralPath $source -Destination $active
            # The build parked by the last switch to GLIntercept is superseded.
            if ($parked) { Remove-Item -LiteralPath $parkedQindieGL }
        }
        elseif ($parked) {
            Move-Item -LiteralPath $parkedQindieGL -Destination $active
        }
        else {
            Copy-Item -LiteralPath $traced -Destination $active
        }
    }
    elseif ($current -eq 'QindieGL') {
        if ($source) {
            Copy-Item -LiteralPath $source -Destination $active -Force
        }
    }
    else {
        throw "Unrecognised $active; not changing anything."
    }
}
else {
    if ($current -eq 'QindieGL') {
        if (-not (Test-Path -LiteralPath $parkedInterceptor) -or
            (Get-DllKind -Path $parkedInterceptor) -ne 'GLIntercept') {
            throw "No GLIntercept loader parked as $parkedInterceptor."
        }
        # The active DLL is a copy of a QindieGL build; keep the last one parked.
        if ((Test-Path -LiteralPath $parkedQindieGL) -and (Get-DllKind -Path $parkedQindieGL) -ne 'QindieGL') {
            throw "Refusing to overwrite $parkedQindieGL, which is not a QindieGL build."
        }
        Move-Item -LiteralPath $active -Destination $parkedQindieGL -Force
        Rename-Item -LiteralPath $parkedInterceptor -NewName 'opengl32.dll'
    }
    elseif ($current -ne 'GLIntercept') {
        throw "Unrecognised $active; not changing anything."
    }
    if ($source) {
        Copy-Item -LiteralPath $source -Destination $traced -Force
    }
}

Write-Output ("opengl32.dll: {0}" -f (Get-DllKind -Path $active))
if (Test-Path -LiteralPath $traced) { Write-Output "QindieGL-traced.dll: present (used by GLIntercept)" }
foreach ($name in @('YOU_ARE_EMPTY.exe.local', 'd3d9.dll', '.trex')) {
    $present = Test-Path -LiteralPath (Join-Path $root $name)
    Write-Output ("{0}: {1}" -f $name, $(if ($present) { 'present' } else { 'absent' }))
}
