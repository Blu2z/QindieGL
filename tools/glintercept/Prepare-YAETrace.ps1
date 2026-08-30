[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('FULL', 'SUMMARY')]
    [string]$Mode,

    [Parameter(Mandatory = $true)]
    [string]$GameRoot,

    [Parameter(Mandatory = $true)]
    [string]$GLInterceptRoot,

    [Parameter(Mandatory = $true)]
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

$resolvedGameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
$resolvedGLInterceptRoot = (Resolve-Path -LiteralPath $GLInterceptRoot).Path
$resolvedQindieGLDll = (Resolve-Path -LiteralPath $QindieGLDll).Path

$interceptorDll = Join-Path $resolvedGLInterceptRoot 'OpenGL32.dll'
$functionDefinitionsDirectory = Join-Path $resolvedGLInterceptRoot 'GLFunctions'
$functionDefinitions = Join-Path $functionDefinitionsDirectory 'gliIncludes.h'
$pluginDirectory = Join-Path $resolvedGLInterceptRoot 'Plugins'
$functionStats = Join-Path $pluginDirectory 'GLFuncStats\GLFuncStats.dll'
$customFunctionDefinitions = Join-Path $PSScriptRoot 'QindieGL-gliIncludes.h'

foreach ($requiredPath in @($interceptorDll, $functionDefinitions, $pluginDirectory, $customFunctionDefinitions)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required GLIntercept path is missing: $requiredPath"
    }
}
if ($Mode -eq 'SUMMARY' -and -not (Test-Path -LiteralPath $functionStats)) {
    throw "SUMMARY mode requires GLFuncStats: $functionStats"
}

# You Are Empty is x86. Refuse an x64 interceptor or QindieGL DLL before any
# files in the game directory are changed.
if ((Get-PeMachine -Path $interceptorDll) -ne 0x014C) {
    throw "GLIntercept OpenGL32.dll is not x86: $interceptorDll"
}
if ((Get-PeMachine -Path $resolvedQindieGLDll) -ne 0x014C) {
    throw "QindieGL DLL is not x86: $resolvedQindieGLDll"
}

$templateName = if ($Mode -eq 'FULL') {
    'gliConfig.full.template.ini'
} else {
    'gliConfig.summary.template.ini'
}
$templatePath = Join-Path $PSScriptRoot $templateName
$template = [IO.File]::ReadAllText($templatePath)
$targetDefinitionsDirectory = Join-Path $resolvedGameRoot 'QindieGL-GLFunctions'
$targetFunctionDefinitions = Join-Path $targetDefinitionsDirectory 'QindieGL-gliIncludes.h'
$config = $template.Replace('@GL_FUNCTION_DEFINES@', $targetFunctionDefinitions)
$config = $config.Replace('@QINDIEGL_DLL@', (Join-Path $resolvedGameRoot 'QindieGL-traced.dll'))
$config = $config.Replace('@PLUGIN_DIR@', $pluginDirectory)

$targetInterceptor = Join-Path $resolvedGameRoot 'opengl32.dll'
$targetQindieGL = Join-Path $resolvedGameRoot 'QindieGL-traced.dll'
$targetConfig = Join-Path $resolvedGameRoot 'gliConfig.ini'

foreach ($targetPath in @($targetInterceptor, $targetQindieGL, $targetConfig, $targetDefinitionsDirectory)) {
    if (Test-Path -LiteralPath $targetPath) {
        throw "Refusing to overwrite existing $targetPath. Back it up explicitly first."
    }
}

Copy-Item -LiteralPath $resolvedQindieGLDll -Destination $targetQindieGL
Copy-Item -LiteralPath $interceptorDll -Destination $targetInterceptor
Copy-Item -LiteralPath $functionDefinitionsDirectory -Destination $targetDefinitionsDirectory -Recurse
Copy-Item -LiteralPath $customFunctionDefinitions -Destination $targetFunctionDefinitions
[IO.File]::WriteAllText($targetConfig, $config, (New-Object Text.UTF8Encoding($false)))

Write-Output "Prepared GLIntercept $Mode mode in $resolvedGameRoot"
Write-Output "Definitions: $targetFunctionDefinitions"
Write-Output "Real GL library: $targetQindieGL"
Write-Output 'After the run, verify gliLog.txt contains no FunctionParser errors or Unknown function diagnostics.'
