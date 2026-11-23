# Windows Microarchitecture Exploration stvarnih projektnih PSI programa.
# Pokrenuti kao administrator nakon izgradi_psi_pdb.ps1.
# VTune progress and warnings use stderr; in Windows PowerShell redirecting
# these with ErrorActionPreference=Stop incorrectly terminates the script.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $root
$raw = Join-Path $root 'sirova_mjerenja\08_sekvencijalno\vtune_windows_finalni_ponovo'
$build = Join-Path $root 'windows_build\08_psi_final_pdb'
$runner = Join-Path $root 'pokreni_psi_vtune.cmd'
$vtune = 'C:\Program Files (x86)\Intel\oneAPI\vtune\2026.7\bin64\vtune.exe'
$cmd = Join-Path $env:WINDIR 'System32\cmd.exe'
$env:Path = 'C:\msys64\ucrt64\bin;' + $env:Path
foreach ($file in @((Join-Path $build 'psi_seq_pdb.exe'),(Join-Path $build 'psi_seq_pdb.pdb'),(Join-Path $build 'psi_seq_opt_pdb.exe'),(Join-Path $build 'psi_seq_opt_pdb.pdb'),$runner,$vtune,$cmd)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Nedostaje datoteka: $file" }
}
if (Test-Path -LiteralPath $raw) { throw "Izlazni direktorij vec postoji; sacuvaj ga i izaberi novo ime: $raw" }
New-Item -ItemType Directory -Path $raw | Out-Null
$status = Join-Path $raw 'status.txt'
$meta = Join-Path $raw 'metapodaci.txt'
@(
    "Datum: $(Get-Date -Format o)"
    'Ulaz: N=200000 M=20000 w=0.02 dist=uniform seed=42 repeats=20 --no-check'
    'Stvarni projektni cilj psi_seq ili psi_seq_opt, Windows clang++ -O3 -DNDEBUG, CodeView/PDB, P07_PROFILE_MARKERS.'
    'Profile markers oznacavaju projektne funkcije i mogu promijeniti performanse u odnosu na standardni Release build.'
    'Windows cmd.exe uz VTune -follow-child; mjeri se cijeli proces, ukljucujuci izgradnje i upite.'
    'Provjera ispravnosti izvrsena je odvojeno bez --no-check.'
) | Set-Content -LiteralPath $meta -Encoding utf8
Get-FileHash -Algorithm SHA256 -LiteralPath @((Join-Path $build 'psi_seq_pdb.exe'),(Join-Path $build 'psi_seq_pdb.pdb'),(Join-Path $build 'psi_seq_opt_pdb.exe'),(Join-Path $build 'psi_seq_opt_pdb.pdb'),(Join-Path $repo 'src\psi_benchmark.cpp'),(Join-Path $repo 'src\psi_seq_buffer.h')) | Format-List | Out-String | Add-Content -LiteralPath $meta -Encoding utf8
& $vtune -version *> (Join-Path $raw 'vtune_verzija.log')
foreach ($variant in @('baseline','optimized')) {
    $result = Join-Path $raw "uarch_$variant"
    $collectionLog = Join-Path $raw "${variant}_collect.log"
    & $vtune -collect uarch-exploration -follow-child -result-dir $result -- $cmd /d /c $runner $variant *> $collectionLog
    $rc = $LASTEXITCODE
    "$(Get-Date -Format o) collect $variant exit=$rc" | Add-Content -LiteralPath $status -Encoding utf8
    if ($rc -ne 0) { throw "Prikupljanje $variant nije uspjelo; vidi $collectionLog" }
    $started = Select-String -LiteralPath $collectionLog -Pattern 'Tacaka \(N\):\s+200000' -Quiet
    $finished = Select-String -LiteralPath $collectionLog -SimpleMatch 'QPS (upita u sekundi):' -Quiet
    if (-not $started -or -not $finished) { throw "VTune se zavrsio, ali nema potpunog izlaza $variant; vidi $collectionLog" }
    "$(Get-Date -Format o) target $variant output_ok" | Add-Content -LiteralPath $status -Encoding utf8
    $jobs = @(
        @{ Name='funkcije_linije'; Report='hotspots'; Group='function,source-line' },
        @{ Name='procesi'; Report='hotspots'; Group='process' },
        @{ Name='summary'; Report='summary'; Group=$null }
    )
    foreach ($job in $jobs) {
        $outfile = Join-Path $raw ("${variant}_" + $job.Name + '.csv')
        $logfile = Join-Path $raw ("${variant}_" + $job.Name + '.log')
        $reportArgs = @('-report', $job.Report, '-r', $result, '-format', 'csv', '-csv-delimiter', 'comma', '-report-output', $outfile)
        if ($job.Group) { $reportArgs += @('-group-by', $job.Group) }
        & $vtune @reportArgs *> $logfile
        $rc = $LASTEXITCODE
        "$(Get-Date -Format o) report $variant $($job.Name) exit=$rc" | Add-Content -LiteralPath $status -Encoding utf8
        if ($rc -ne 0 -or -not (Test-Path -LiteralPath $outfile -PathType Leaf) -or (Get-Item -LiteralPath $outfile).Length -eq 0) { throw "Izvoz $variant $($job.Name) nije uspio; vidi $logfile" }
    }
}
"$(Get-Date -Format o) gotovo" | Add-Content -LiteralPath $status -Encoding utf8
