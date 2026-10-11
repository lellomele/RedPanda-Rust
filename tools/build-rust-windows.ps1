param(
 [Parameter(Mandatory=$true)][string]$QtRoot,
 [string]$CompilerRoot='C:/msys64/mingw64',
 [string]$BuildRoot='',
 [string]$InstallRoot=''
)
$ErrorActionPreference='Stop'
$source=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(!$BuildRoot){$BuildRoot=Join-Path $source 'build'}
if(!$InstallRoot){$InstallRoot=Join-Path $source 'dist'}
$env:PATH="$CompilerRoot/bin;$QtRoot/bin;$env:PATH"
& "$CompilerRoot/bin/cmake.exe" -S $source -B $BuildRoot -G Ninja "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_CXX_COMPILER=$CompilerRoot/bin/g++.exe" "-DCMAKE_C_COMPILER=$CompilerRoot/bin/gcc.exe" "-DCMAKE_MAKE_PROGRAM=$CompilerRoot/bin/ninja.exe" -DAPP_NAME=RedPandaRust -DPORTABLE_CONFIG=oui -DBUILD_TESTING=OFF -DSDCC=OFF -DVCS=OFF -DREDPANDA_UI_CHECKS=OFF -DCMAKE_BUILD_TYPE=Release
if($LASTEXITCODE){throw 'Configurazione fallita'}
& "$CompilerRoot/bin/cmake.exe" --build $BuildRoot --target RedPandaIDE consolepauser --parallel 4
if($LASTEXITCODE){throw 'Compilazione fallita'}
& "$CompilerRoot/bin/cmake.exe" --install $BuildRoot --prefix $InstallRoot
if($LASTEXITCODE){throw 'Installazione fallita'}
& "$QtRoot/bin/windeployqt.exe" --dir $InstallRoot --compiler-runtime --no-translations "$InstallRoot/RedPandaRust.exe"
if($LASTEXITCODE){throw 'Distribuzione Qt fallita'}
