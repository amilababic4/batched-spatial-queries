#!/usr/bin/env bash
# Isti PSI tree, prethodno pripremljene kutije; sve sekvencijalno.
set -euo pipefail
prs='/mnt/c/Users/DT User/Desktop/VI semestar/PRS'
folder="$prs/3.Profiliranje_i_optimizacija"
raw="$folder/sirova_mjerenja/08_sekvencijalno/psi_uporedivo"
work="$HOME/prs-seq-opt"
mkdir -p "$raw" "$work"
incs=(-I"$prs/reference/psi/include" -I"$prs/reference/psi/include/parlaylib/include" -I"$prs/reference/psi/include/libmorton/include")
g++-14 -std=c++20 -O3 -DNDEBUG -g -fno-omit-frame-pointer -Wall -Wextra -DPARLAY_SEQUENTIAL "${incs[@]}" "$folder/psi_poredjenje_bafera.cpp" -o "$work/psi_uporedivo" > "$raw/kompajliranje.txt" 2>&1
g++-14 -std=c++20 -O3 -DNDEBUG -g -fno-omit-frame-pointer -Wall -Wextra -DPARLAY_SEQUENTIAL -DP07_PROFILE_MARKERS "${incs[@]}" "$folder/psi_poredjenje_bafera.cpp" -o "$work/psi_uporedivo_prof" > "$raw/kompajliranje_prof.txt" 2>&1
{
 date -Is
 uname -a
 g++-14 --version | head -1
 sha256sum "$folder/psi_poredjenje_bafera.cpp" "$work/psi_uporedivo" "$work/psi_uporedivo_prof"
} > "$raw/okruzenje_i_hash.txt"
"$work/psi_uporedivo" --mode verify --n 0 --m 20 > "$raw/provjera_prazno.txt"
"$work/psi_uporedivo" --mode verify --n 200 --m 0 > "$raw/provjera_bez_upita.txt"
for dist in uniform clustered; do
 for width in 0.005 0.02 0.1; do
  tag="${dist}_${width}"
  "$work/psi_uporedivo" --mode verify --n 200000 --m 2000 --dist "$dist" --w "$width" > "$raw/${tag}_verify.txt"
  "$work/psi_uporedivo" --mode compare --n 200000 --m 2000 --dist "$dist" --w "$width" --repeats 7 --build-repeats 7 > "$raw/${tag}_compare.txt"
  # Nezavisni proces po uzorku; rotira prvi program. Cache se ne cisti.
  for rep in 1 2 3 4 5 6 7; do
   if ((rep % 2)); then order=(baseline optimized); else order=(optimized baseline); fi
   for v in "${order[@]}"; do
    "$work/psi_uporedivo" --mode "cold-$v" --n 200000 --m 2000 --dist "$dist" --w "$width" > "$raw/${tag}_cold_${v}_${rep}.txt"
   done
  done
  echo "PSI ZAVRSENO $tag"
 done
done
for spec in '20000 200 malo' '1000000 2000 veliko'; do
 read -r n m tag <<< "$spec"
 "$work/psi_uporedivo" --mode verify --n "$n" --m "$m" --w 0.02 > "$raw/${tag}_verify.txt"
 "$work/psi_uporedivo" --mode compare --n "$n" --m "$m" --w 0.02 --repeats 7 --build-repeats 7 > "$raw/${tag}_compare.txt"
 echo "PSI ZAVRSENO $tag"
done
echo 'PSI_PROVJERE_I_MJERENJA_PROSLA'
