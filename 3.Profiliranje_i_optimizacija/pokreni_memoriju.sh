#!/usr/bin/env bash
set -euo pipefail
root='/mnt/c/Users/DT User/Desktop/VI semestar/PRS/3.Profiliranje_i_optimizacija/sirova_mjerenja/poredjenje_svih'
bin='/home/dt_user/prs-eksperimenti/poredjenje5/poredjenje'
g++-14 -std=c++20 -O3 -g -fno-omit-frame-pointer -Wall -Wextra -DP07_DISABLE_AVX2 '/mnt/c/Users/DT User/Desktop/VI semestar/PRS/3.Profiliranje_i_optimizacija/poredjenje_brute_force_verzija.cpp' -o /home/dt_user/prs-eksperimenti/poredjenje5/poredjenje_scalar
/home/dt_user/prs-eksperimenti/poredjenje5/poredjenje_scalar --mode verify --n 20000 --m 200 --w 0.02 > "$root/bez_avx2_verify.txt"
# Razdvojeni procesi; 200.000 tacaka i 200 upita, kako bi Massif bio prakticno brz.
for v in ref v1 v2 v3 v4; do
  valgrind --tool=massif --stacks=no --detailed-freq=1 --massif-out-file="$root/massif_${v}.out" "$bin" --mode "$v" --n 200000 --m 200 --w 0.02 --repeats 1 > "$root/massif_${v}_program.txt" 2> "$root/massif_${v}_log.txt"
  ms_print "$root/massif_${v}.out" > "$root/massif_${v}.txt"
  /usr/bin/time -v -o "$root/rss_${v}.txt" "$bin" --mode "$v" --n 200000 --m 200 --w 0.02 --repeats 1 > "$root/rss_${v}_program.txt"
  echo "MEMORIJA $v zavrsena"
done
