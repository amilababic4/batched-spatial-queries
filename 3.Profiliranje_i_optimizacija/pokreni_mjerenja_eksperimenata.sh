#!/usr/bin/env bash
# Sekvencijalni eksperimenti: isti ulazi, puni ID-jevi i odvojene faze.
set -euo pipefail
prs='/mnt/c/Users/DT User/Desktop/VI semestar/PRS'
folder="$prs/3.Profiliranje_i_optimizacija"
raw="$folder/sirova_mjerenja/08_sekvencijalno"
work="$HOME/prs-seq-opt"
mkdir -p "$raw" "$work"
incs=(-I"$prs/reference/psi/include" -I"$prs/reference/psi/include/parlaylib/include" -I"$prs/reference/psi/include/libmorton/include")
g++-14 -std=c++20 -O3 -DNDEBUG -g -fno-omit-frame-pointer -Wall -Wextra -DPARLAY_SEQUENTIAL "${incs[@]}" "$folder/psi_sekvencijalni_eksperiment.cpp" -o "$work/psi_compare" > "$raw/kompajliranje_psi.txt" 2>&1
g++-14 -std=c++20 -O3 -DNDEBUG -g -fno-omit-frame-pointer -Wall -Wextra -DPARLAY_SEQUENTIAL -DP07_PROFILE_MARKERS "${incs[@]}" "$folder/psi_sekvencijalni_eksperiment.cpp" -o "$work/psi_compare_prof" > "$raw/kompajliranje_psi_prof.txt" 2>&1
g++-14 -std=c++20 -O3 -g -fno-omit-frame-pointer -Wall -Wextra "$folder/brute_force_v5_maska_x.cpp" -o "$work/v5_compare" > "$raw/kompajliranje_v5.txt" 2>&1
g++-14 -std=c++20 -O3 -g -fno-omit-frame-pointer -Wall -Wextra -DP07_DISABLE_AVX2 "$folder/brute_force_v5_maska_x.cpp" -o "$work/v5_scalar" > "$raw/kompajliranje_v5_scalar.txt" 2>&1
{
  date -Is
  uname -a
  g++-14 --version | head -1
  sha256sum "$folder/psi_sekvencijalni_eksperiment.cpp" "$folder/brute_force_v5_maska_x.cpp" "$work/psi_compare" "$work/psi_compare_prof" "$work/v5_compare"
} > "$raw/okruzenje_i_hash.txt"
ctest --test-dir "$work/build" --output-on-failure -V > "$raw/ctest.txt"
"$work/psi_compare" --mode verify --n 0 --m 20 > "$raw/psi_prazno.txt"
"$work/psi_compare" --mode verify --n 200 --m 0 > "$raw/psi_bez_upita.txt"
"$work/v5_scalar" --mode verify --n 20000 --m 200 --dist uniform > "$raw/v5_scalar_uniform.txt"
"$work/v5_scalar" --mode verify --n 20000 --m 200 --dist clustered > "$raw/v5_scalar_clustered.txt"
for dist in uniform clustered; do
  for w in 0.005 0.02 0.1; do
    tag="${dist}_${w}"
    "$work/v5_compare" --mode verify --n 200000 --m 2000 --dist "$dist" --w "$w" > "$raw/v5_${tag}_verify.txt"
    "$work/v5_compare" --mode compare --n 200000 --m 2000 --dist "$dist" --w "$w" --repeats 7 > "$raw/v5_${tag}_compare.txt"
    "$work/psi_compare" --mode verify --n 200000 --m 2000 --dist "$dist" --w "$w" > "$raw/psi_${tag}_verify.txt"
    "$work/psi_compare" --mode compare --n 200000 --m 2000 --dist "$dist" --w "$w" --repeats 7 --build-repeats 7 > "$raw/psi_${tag}_compare.txt"
    "$work/build/psi_seq" --n 200000 --m 2000 --dist "$dist" --w "$w" --seed 42 --repeats 7 > "$raw/psi_original_${tag}.txt"
    echo "ZAVRSENO $tag"
  done
done
for spec in '20000 200 malo' '1000000 2000 veliko'; do
  read -r n m tag <<< "$spec"
  "$work/v5_compare" --mode verify --n "$n" --m "$m" --w 0.02 > "$raw/v5_${tag}_verify.txt"
  "$work/v5_compare" --mode compare --n "$n" --m "$m" --w 0.02 --repeats 7 > "$raw/v5_${tag}_compare.txt"
  "$work/psi_compare" --mode verify --n "$n" --m "$m" --w 0.02 > "$raw/psi_${tag}_verify.txt"
  "$work/psi_compare" --mode compare --n "$n" --m "$m" --w 0.02 --repeats 7 --build-repeats 7 > "$raw/psi_${tag}_compare.txt"
  echo "ZAVRSENO $tag"
done
echo 'SVI_TESTOVI_I_MJERENJA_PROSLI'
