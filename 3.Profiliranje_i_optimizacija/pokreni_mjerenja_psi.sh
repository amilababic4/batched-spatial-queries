#!/usr/bin/env bash
# Poredjenje stvarnih CMake ciljeva psi_seq i psi_seq_opt; oba su sekvencijalna.
set -euo pipefail
folder='/mnt/c/Users/DT User/Desktop/VI semestar/PRS/3.Profiliranje_i_optimizacija'
raw="$folder/sirova_mjerenja/08_sekvencijalno/integrisano"
build="$HOME/prs-integration-check"
mkdir -p "$raw"
cmake --build "$build" --target psi_seq psi_seq_opt testovi testovi_psi_seq_opt -j 2 > "$raw/build.txt" 2>&1
ctest --test-dir "$build" --output-on-failure -V > "$raw/ctest.txt"
{
 date -Is
 uname -a
 g++-14 --version | head -1
 sha256sum "$build/psi_seq" "$build/psi_seq_opt"
} > "$raw/okruzenje_i_hash.txt"
for dist in uniform clustered; do
 for width in 0.005 0.02 0.1; do
  tag="${dist}_${width}"
  # Provjera bez --no-check poredi sve sortirane ID-jeve prema brute-force.
  for bin in psi_seq psi_seq_opt; do
   "$build/$bin" --n 200000 --m 2000 --dist "$dist" --w "$width" --seed 42 --repeats 1 > "$raw/${tag}_${bin}_verify.txt"
  done
  # Pet nezavisnih procesa; svaki mjeri sedam ponavljanja nakon zagrijavanja.
  for rep in 1 2 3 4 5; do
   if ((rep % 2)); then order=(psi_seq psi_seq_opt); else order=(psi_seq_opt psi_seq); fi
   for bin in "${order[@]}"; do
    "$build/$bin" --n 200000 --m 2000 --dist "$dist" --w "$width" --seed 42 --repeats 7 --no-check > "$raw/${tag}_${bin}_${rep}.txt"
   done
  done
  echo "INTEGRISANO ZAVRSENO $tag"
 done
done
for spec in '20000 200 malo' '1000000 2000 veliko'; do
 read -r n m tag <<< "$spec"
 for bin in psi_seq psi_seq_opt; do
  "$build/$bin" --n "$n" --m "$m" --w 0.02 --seed 42 --repeats 1 > "$raw/${tag}_${bin}_verify.txt"
 done
 for rep in 1 2 3 4 5; do
  if ((rep % 2)); then order=(psi_seq psi_seq_opt); else order=(psi_seq_opt psi_seq); fi
  for bin in "${order[@]}"; do
   "$build/$bin" --n "$n" --m "$m" --w 0.02 --seed 42 --repeats 7 --no-check > "$raw/${tag}_${bin}_${rep}.txt"
  done
 done
 echo "INTEGRISANO ZAVRSENO $tag"
done
echo 'INTEGRISANI_TESTOVI_I_MJERENJA_PROSLA'
