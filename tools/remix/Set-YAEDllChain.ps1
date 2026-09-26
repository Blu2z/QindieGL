[CmdletBinding()]
param(
    # Direct: QindieGL is the game's opengl32.dll (RTX Remix runs).
    # GLIntercept: the GLIntercept loader is opengl32.dll and forwards to
    # QindieGL-traced.dll (GL call traces, Ctrl+Shift+F frame traces).
    [ValidateSet('Direct', 'GLIntercept')]
    [string]$Chain,

    # On: the Remix bridge is the game's d3d9.dll. Off: it is parked as
    # d3d9.remix.dll and QindieGL loads the system Direct3D 9.
    [ValidateSet('On', 'Off')]
    [string]$Remix,

    [string]$GameRoot = 'H:\YAE\Original\You Are Empty',

    # Optional QindieGL build to install with -Chain: as opengl32.dll (Direct)
    # or as QindieGL-traced.dll (GLIntercept). Without it the installed build is kept.
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

# GLIntercept reads gliConfig.ini, QindieGL reads QindieGL.ini, and the Remix
# bridge client starts NvRemixBridge.exe.
function Get-DllKind {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) { return 'absent' }
    $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($Path))
    if ($text.Contains('gliConfig')) { return 'GLIntercept' }
    if ($text.Contains('QindieGL.ini')) { return 'QindieGL' }
    if ($text.Contains('NvRemixBridge')) { return 'RemixBridge' }
    return 'unknown'
}

$root = (Resolve-Path -LiteralPath $GameRoot).Path
$active = Join-Path $root 'opengl32.dll'
$parkedInterceptor = Join-Path $root 'opengl32.glintercept.dll'
$parkedQindieGL = Join-Path $root 'opengl32.qindiegl.dll'
$traced = Join-Path $root 'QindieGL-traced.dll'
$bridge = Join-Path $root 'd3d9.dll'
$parkedBridge = Join-Path $root 'd3d9.remix.dll'

$source = $null
if ($QindieGLDll) {
    if (-not $Chain) {
        throw '-QindieGLDll needs -Chain, which decides where the build goes.'
    }
    $source = (Resolve-Path -LiteralPath $QindieGLDll).Path
    if ((Get-PeMachine -Path $source) -ne 0x014C) {
        throw "QindieGL DLL is not x86: $source"
    }
    if ((Get-DllKind -Path $source) -ne 'QindieGL') {
        throw "Not a QindieGL build: $source"
    }
}

if ($Chain) {
    $current = Get-DllKind -Path $active
    if ($Chain -eq 'Direct') {
        if ($current -eq 'GLIntercept') {
            if (Test-Path -LiteralPath $parkedInterceptor) {
                throw "Refusing to overwrite $parkedInterceptor. Move it away first."
            }
            $parked = (Get-DllKind -Path $parkedQindieGL) -eq 'QindieGL'
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
            throw "Unrecognised $active ($current); not changing anything."
        }
    }
    else {
        if ($current -eq 'QindieGL') {
            if ((Get-DllKind -Path $parkedInterceptor) -ne 'GLIntercept') {
                throw "No GLIntercept loader parked as $parkedInterceptor."
            }
            # The active DLL is a copy of a QindieGL build; keep the last one parked.
            $parkedKind = Get-DllKind -Path $parkedQindieGL
            if ($parkedKind -ne 'absent' -and $parkedKind -ne 'QindieGL') {
                throw "Refusing to overwrite $parkedQindieGL, which is not a QindieGL build."
            }
            Move-Item -LiteralPath $active -Destination $parkedQindieGL -Force
            Rename-Item -LiteralPath $parkedInterceptor -NewName 'opengl32.dll'
        }
        elseif ($current -ne 'GLIntercept') {
            throw "Unrecognised $active ($current); not changing anything."
        }
        if ($source) {
            Copy-Item -LiteralPath $source -Destination $traced -Force
        }
    }
}

if ($Remix) {
    $activeBridge = Get-DllKind -Path $bridge
    if ($activeBridge -ne 'absent' -and $activeBridge -ne 'RemixBridge') {
        throw "$bridge is not the Remix bridge ($activeBridge); not changing anything."
    }
    if ($Remix -eq 'Off' -and $activeBridge -eq 'RemixBridge') {
        if (Test-Path -LiteralPath $parkedBridge) {
            throw "Refusing to overwrite $parkedBridge. Move it away first."
        }
        Rename-Item -LiteralPath $bridge -NewName (Split-Path -Leaf $parkedBridge)
    }
    elseif ($Remix -eq 'On' -and $activeBridge -eq 'absent') {
        if ((Get-DllKind -Path $parkedBridge) -ne 'RemixBridge') {
            throw "The Remix runtime is not installed: no bridge parked as $parkedBridge."
        }
        Rename-Item -LiteralPath $parkedBridge -NewName 'd3d9.dll'
    }
    if ($Remix -eq 'On' -and -not (Test-Path -LiteralPath (Join-Path $root '.trex'))) {
        throw 'The bridge needs the .trex folder of the Remix runtime next to it.'
    }
}

Write-Output ("opengl32.dll: {0}" -f (Get-DllKind -Path $active))
if (Test-Path -LiteralPath $traced) { Write-Output 'QindieGL-traced.dll: present (used by GLIntercept)' }
$remixState = switch (Get-DllKind -Path $bridge) {
    'RemixBridge' { 'on' }
    'absent' { if ((Get-DllKind -Path $parkedBridge) -eq 'RemixBridge') { 'off (parked as d3d9.remix.dll)' } else { 'not installed' } }
    default { 'unknown d3d9.dll' }
}
Write-Output "Remix: $remixState"
foreach ($name in @('YOU_ARE_EMPTY.exe.local', '.trex')) {
    $present = Test-Path -LiteralPath (Join-Path $root $name)
    Write-Output ("{0}: {1}" -f $name, $(if ($present) { 'present' } else { 'absent' }))
}
