$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# Rdzeń bez punktów wejścia i bez adaptera sieciowego (ten wymaga ws2_32).
$lib = Get-ChildItem -Path src -Recurse -Filter *.c |
    Where-Object { $_.Name -notin @('main.c', 'main_web.c', 'web.c') } |
    ForEach-Object { $_.FullName }
$flags = @('-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-O2', '-Isrc', '-D_CRT_SECURE_NO_WARNINGS')

clang @flags @lib src\main.c -o ziomki.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
clang @flags @lib src\interface\web.c src\main_web.c -o ziomki-web.exe -lws2_32
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
clang @flags @lib tests\test_ziomki.c -o test_ziomki.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

.\test_ziomki.exe
exit $LASTEXITCODE
