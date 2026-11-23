@echo off
rem Profilisu se stvarni projektni programi, jedan sekvencijalni proces.
set "exe="
if /I "%~1"=="baseline" set "exe=%~dp0windows_build\08_psi_final_pdb\psi_seq_pdb.exe"
if /I "%~1"=="optimized" set "exe=%~dp0windows_build\08_psi_final_pdb\psi_seq_opt_pdb.exe"
if not defined exe (
    echo Upotreba: pokreni_psi_vtune.cmd baseline^|optimized 1>&2
    exit /b 2
)
"%exe%" --n 200000 --m 20000 --w 0.02 --dist uniform --seed 42 --repeats 20 --no-check
exit /b %ERRORLEVEL%
