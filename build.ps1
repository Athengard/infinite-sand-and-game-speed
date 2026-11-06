param([string]$Compiler = 'tcc.exe')

$ErrorActionPreference = 'Stop'
$tcc = (Get-Command $Compiler -CommandType Application).Source
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path build -Force | Out-Null
    & $tcc -shared -o build/dinput8.dll roadcraft_mod.c -lkernel32 -luser32
    if ($LASTEXITCODE -ne 0) { throw 'Failed to build dinput8.dll.' }
} finally {
    Pop-Location
}
