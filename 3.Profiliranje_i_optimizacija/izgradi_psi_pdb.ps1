# Zaseban Windows build za profiliranje; ne mijenja CMake Release build.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $root
$build = Join-Path $root 'windows_build\08_psi_final_pdb'
$env:Path = 'C:\msys64\ucrt64\bin;' + $env:Path
New-Item -ItemType Directory -Path $build -Force | Out-Null
$includes = @(
    ('-I' + (Join-Path $repo 'src')),
    ('-I' + (Join-Path $repo 'reference\psi\include')),
    ('-I' + (Join-Path $repo 'reference\psi\include\parlaylib\include')),
    ('-I' + (Join-Path $repo 'reference\psi\include\libmorton\include'))
)
$common = @('-std=c++20','-O3','-DNDEBUG','-g','-gcodeview','-fno-omit-frame-pointer','-DP07_SERIAL','-DPARLAY_SEQUENTIAL','-DP07_PROFILE_MARKERS','-pthread','-fuse-ld=lld') + $includes
Push-Location $build
try {
    foreach ($name in @('psi_seq','psi_seq_opt')) {
        $opt = @()
        if ($name -eq 'psi_seq_opt') { $opt = @('-DP07_SEQ_BUFFER') }
        $exe = "${name}_pdb.exe"
        $pdb = "${name}_pdb.pdb"
        $argsForClang = $common + $opt + @("-Wl,--pdb=$pdb", (Join-Path $repo 'src\psi_benchmark.cpp'), '-o', $exe)
        & clang++.exe @argsForClang *> "${name}_build.log"
        if ($LASTEXITCODE -ne 0) { throw "Build $name nije uspio. Vidi ${name}_build.log" }
        & (Join-Path $build $exe) --n 20000 --m 200 --w 0.02 --dist uniform --seed 42 --repeats 1 *> "${name}_provjera.log"
        if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath "${name}_provjera.log" -SimpleMatch 'OK: svih 200 upita' -Quiet)) { throw "Provjera $name nije uspjela." }
        & objdump.exe -x (Join-Path $build $exe) *> "${name}_objdump.log"
        if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath "${name}_objdump.log" -Pattern 'CodeView|RSDS' -Quiet)) { throw "CodeView zapis nije pronadjen u $name." }
        Get-FileHash -Algorithm SHA256 -LiteralPath @((Join-Path $build $exe),(Join-Path $build $pdb)) | Format-List | Out-String | Set-Content -LiteralPath "${name}_sha256.txt" -Encoding utf8
    }
} finally { Pop-Location }
