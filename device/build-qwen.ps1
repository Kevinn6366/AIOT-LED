param(
    [string]$CompilerBin = 'D:/vivado/SDK/2019.1/gnu/aarch32/nt/gcc-arm-linux-gnueabi/bin',
    [string]$Ninja = 'ninja'
)
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$src = 'build/llama-qwen'
$obj = 'build/qwen-obj'
if (!(Test-Path $src)) {
    git clone --depth 1 --branch b3948 https://github.com/ggml-org/llama.cpp.git $src
    if ($LASTEXITCODE) { throw 'llama.cpp download failed' }
}
if ((git -C $src rev-parse HEAD) -ne '55e47786e373c90fc7803e718e3e1dd6d53c3db6') { throw 'Unexpected llama.cpp revision' }
foreach ($file in @('device/qwen-cortex-a9.patch', 'device/qwen-neon.patch')) {
    $patch = (Resolve-Path $file).Path
    git -C $src apply --reverse --check --quiet $patch 2>$null
    if ($LASTEXITCODE) {
        git -C $src apply $patch
        if ($LASTEXITCODE) { throw "Could not apply $file" }
    }
}
$prefix = Join-Path $CompilerBin 'arm-linux-gnueabihf-'
$flags = '-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -mfp16-format=ieee'
cmake -S $src -B $obj -G Ninja "-DCMAKE_MAKE_PROGRAM=$((Get-Command $Ninja).Source)" `
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=armv7 `
    "-DCMAKE_C_COMPILER=${prefix}gcc.exe" "-DCMAKE_CXX_COMPILER=${prefix}g++.exe" `
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags" `
    -DCMAKE_EXE_LINKER_FLAGS=-static '-DCMAKE_CXX_STANDARD_LIBRARIES=-Wl,--whole-archive -lpthread -Wl,--no-whole-archive -latomic -ldl' `
    -DBUILD_SHARED_LIBS=OFF -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF -DGGML_LLAMAFILE=OFF `
    -DLLAMA_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'
if ($LASTEXITCODE) { throw 'CMake configuration failed' }
cmake --build $obj --target llama-server -j 8
if ($LASTEXITCODE) { throw 'SDK build failed' }
& "${prefix}strip.exe" -o build/artifacts/llama-server.elf "$obj/bin/llama-server"
if ($LASTEXITCODE) { throw 'ELF export failed' }
