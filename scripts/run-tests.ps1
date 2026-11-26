# Host-side unit tests (no AMXX module deps). Usage:
#   .\scripts\run-tests.ps1
#   .\scripts\run-tests.ps1 -Zig "C:\path\to\zig.exe"

param(
    [string]$Zig = $(if ($env:ZIG) { $env:ZIG } else { "zig" })
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$OutDir = Join-Path $Root "zig-out"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$Exe = Join-Path $OutDir "kz_global_api_tests-$([Guid]::NewGuid()).exe"
$Sources = @(
    "src/kz_global_api/kz_path_validate.cpp",
    "src/kz_global_api/krp_header_validate.cpp",
    "src/kz_global_api/kz_replay_uid.cpp",
    "src/kz_global_api/test/test_main.cpp",
    "src/kz_global_api/test/path_validate_test.cpp",
    "src/kz_global_api/test/krp_validate_test.cpp",
    "src/kz_global_api/test/replay_uid_test.cpp"
) | ForEach-Object { Join-Path $Root $_ }

Push-Location $Root
try {
    & $Zig c++ -std=c++17 -Isrc/kz_global_api/include @Sources -o $Exe
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    & $Exe
    exit $LASTEXITCODE
}
finally {
    Pop-Location
    if (Test-Path -LiteralPath $Exe) {
        Remove-Item -LiteralPath $Exe
    }
}
