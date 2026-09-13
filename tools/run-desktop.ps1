param(
    [string]$Configuration = 'Release',
    [string]$ProjectFile,
    [string]$QtDirectory,
    [string]$Ffprobe
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
if (-not $QtDirectory) { $QtDirectory = Join-Path $repositoryRoot 'build/tools/Qt/6.10.3/msvc2022_64' }
if (Test-Path -LiteralPath (Join-Path $QtDirectory 'bin')) {
    $env:PATH = (Join-Path $QtDirectory 'bin') + ';' + $env:PATH
    $env:QT_PLUGIN_PATH = Join-Path $QtDirectory 'plugins'
}
if (-not $Ffprobe) {
    $localProbe = Join-Path $repositoryRoot 'build/tools/ffmpeg-9.0.1-essentials_build/bin/ffprobe.exe'
    $Ffprobe = if (Test-Path -LiteralPath $localProbe) { $localProbe } else { 'ffprobe' }
}
$executable = Join-Path $repositoryRoot "build/$Configuration/editor-desktop.exe"
if (-not (Test-Path -LiteralPath $executable)) { throw "Build the desktop target first: $executable" }
$launchArguments = @('--ffprobe', $Ffprobe)
if ($ProjectFile) { $launchArguments += $ProjectFile }
& $executable @launchArguments
