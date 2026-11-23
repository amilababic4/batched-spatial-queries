#!/usr/bin/env bash
set -euo pipefail
bin='/home/dt_user/prs-eksperimenti/poredjenje5/poredjenje'
work='/home/dt_user/prs-eksperimenti/poredjenje5'
raw='/mnt/c/Users/DT User/Desktop/VI semestar/PRS/3.Profiliranje_i_optimizacija/sirova_mjerenja/poredjenje_svih'
vt='/opt/intel/oneapi/vtune/latest/bin64/vtune'
for v in ref v1 v2 v3 v4; do
  repeats=8
  if [ "$v" = v4 ]; then repeats=60; fi
  "$vt" -collect hotspots -knob sampling-mode=sw -result-dir "$work/hotspots_$v" -- "$bin" --mode "$v" --n 200000 --m 2000 --w 0.02 --repeats "$repeats" > "$raw/hotspots_wsl_${v}.log" 2>&1
  "$vt" -report hotspots -r "$work/hotspots_$v" -group-by function -format csv -csv-delimiter comma -report-output "$raw/hotspots_wsl_${v}.csv" > "$raw/hotspots_wsl_${v}_report.log" 2>&1
  echo "HOTSPOTS $v zavrsen"
done
