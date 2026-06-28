$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$project = Join-Path $root "SilentPixel.vcxproj"
$expectedExe = Join-Path $root "bin\Release\SilentPixel.exe"

Get-Process SilentPixel -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = $null

if (Test-Path $vswhere) {
    $msbuild = & $vswhere `
        -latest `
        -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
}

if (-not $msbuild) {
    $cmd = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        $msbuild = $cmd.Source
    }
}

if (-not $msbuild -or -not (Test-Path $msbuild)) {
    Write-Host "[SilentPixel] MSBuild no encontrado." -ForegroundColor Red
    Write-Host "Instala Visual Studio Build Tools 2022 con Desktop development with C++."
    pause
    exit 1
}

Write-Host "[SilentPixel] MSBuild:" $msbuild
Write-Host "[SilentPixel] Compilando Release x64..."

& $msbuild $project /t:Build /p:Configuration=Release /p:Platform=x64 /m:1 /v:minimal
if ($LASTEXITCODE -ne 0) {
    Write-Host "[SilentPixel] Compilacion fallida." -ForegroundColor Red
    pause
    exit $LASTEXITCODE
}

$exe = $expectedExe
if (-not (Test-Path $exe)) {
    $exe = Get-ChildItem -Path $root -Filter "SilentPixel.exe" -Recurse -File |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

if (-not $exe -or -not (Test-Path $exe)) {
    Write-Host "[SilentPixel] No se encontro SilentPixel.exe tras compilar." -ForegroundColor Red
    pause
    exit 1
}

Write-Host "[SilentPixel] Abriendo:" $exe
Start-Process -FilePath $exe
exit 0
