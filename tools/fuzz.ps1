# tools/fuzz.sh for Windows: fuzzes every parser of what reaches the hub from outside (test/fuzz/fuzz_parsers.cpp) with
# libFuzzer, AddressSanitizer and UndefinedBehaviorSanitizer, for -Seconds (120 unless given). Needs LLVM for Windows
# (winget install LLVM.LLVM), which brings libFuzzer, and Visual Studio's C++ build tools with a Windows SDK, whose C
# library clang builds against. A fault stops it, prints the input that caused it, and keeps it in .pio\fuzz\ to run
# again: .pio\fuzz\fuzz_parsers.exe <file>, with LLVM's lib\clang\<version>\lib\windows on PATH for its sanitizer DLL.
#   powershell -ExecutionPolicy Bypass -File tools\fuzz.ps1 [-Seconds 600]
param([int]$Seconds = 120)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot)

$llvm = Join-Path $env:ProgramFiles 'LLVM'
$clang = Join-Path $llvm 'bin\clang++.exe'
if (-not (Test-Path $clang)) { throw "No $clang. Install LLVM for Windows: winget install LLVM.LLVM" }
$runtime = Get-ChildItem (Join-Path $llvm 'lib\clang\*\lib\windows') -Directory | Select-Object -Last 1
if (-not $runtime) { throw "No sanitizer runtime under $llvm\lib\clang" }
$env:PATH = "$($runtime.FullName);$env:PATH"

$out = '.pio\fuzz'
New-Item -ItemType Directory -Force "$out\corpus" | Out-Null
py -3 tools\fuzz_seeds.py "$out\corpus"
if ($LASTEXITCODE) { throw 'fuzz_seeds.py failed' }

# The library but its pages, as fuzz.sh builds it. test\fuzz\msvc holds the two POSIX headers cam_storage.cpp needs and
# Microsoft's C library lacks. The STL's container annotations are off because the prebuilt libFuzzer was built without
# them and the linker refuses the mix; AddressSanitizer still checks every access. Monocypher, in C, builds as C++ here.
$sources = Get-ChildItem lib\sensorcam\*.cpp, lib\s2o_proto\*.cpp, lib\monocypher\*.c | Where-Object Name -ne 'cam_web_pages.cpp' |
  ForEach-Object { $_.FullName }
$flags = @('-std=gnu++17', '-g', '-O1', '-fsanitize=fuzzer,address,undefined', '-fno-sanitize-recover=undefined',
  '-D_DISABLE_STRING_ANNOTATION', '-D_DISABLE_VECTOR_ANNOTATION', '-D_CRT_SECURE_NO_WARNINGS',
  '-D_CRT_NONSTDC_NO_DEPRECATE', '-D_WINSOCK_DEPRECATED_NO_WARNINGS', '-Wno-deprecated-declarations', '-DCAM_TEST_HOOKS',
  '-Itest\fuzz\msvc', '-Ilib\sensorcam', '-Ilib\s2o_proto', '-Ilib\monocypher', '-Iinclude', '-Wno-deprecated')
& $clang @flags test\fuzz\fuzz_parsers.cpp test\fuzz\pages_stub.cpp @sources -lws2_32 -o "$out\fuzz_parsers.exe"
if ($LASTEXITCODE) { throw 'The fuzzer did not build' }

& "$out\fuzz_parsers.exe" "-max_total_time=$Seconds" -max_len=2048 -print_final_stats=1 "-artifact_prefix=$out\" "$out\corpus"
exit $LASTEXITCODE
