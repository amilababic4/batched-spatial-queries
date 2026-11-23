#!/usr/bin/env bash
# Stvarni projektni ciljevi, izgrađeni u Release načinu; nema profiler markera.
# Program izvodi zagrijavanje plus jedno ponavljanje (dvije izgradnje/upitne grupe).
set -euo pipefail
folder='/mnt/c/Users/DT User/Desktop/VI semestar/PRS/3.Profiliranje_i_optimizacija'
raw="$folder/sirova_mjerenja/08_sekvencijalno/psi_integrisana_memorija"
build="$HOME/prs-integration-check"
mkdir -p "$raw"
{
 date -Is
 uname -a
 g++-14 --version | head -1
 valgrind --version
 sha256sum "$build/psi_seq" "$build/psi_seq_opt"
} > "$raw/okruzenje_i_hash.txt"
for width in 0.02 0.1; do
 for bin in psi_seq psi_seq_opt; do
  tag="${bin}_${width}"
  args=(--n 200000 --m 2000 --dist uniform --w "$width" --seed 42 --repeats 1 --no-check)
  valgrind --tool=massif --stacks=no --detailed-freq=1 --massif-out-file="$raw/massif_${tag}.out" "$build/$bin" "${args[@]}" > "$raw/massif_${tag}_program.txt" 2> "$raw/massif_${tag}_log.txt"
  ms_print "$raw/massif_${tag}.out" > "$raw/massif_${tag}.txt"
  for rep in 1 2 3; do
   /usr/bin/time -v -o "$raw/rss_${tag}_${rep}.txt" "$build/$bin" "${args[@]}" > "$raw/rss_${tag}_${rep}_program.txt"
  done
  echo "INTEGRISANA MEMORIJA ZAVRSENA $tag"
 done
done
echo 'INTEGRISANA_MEMORIJA_PROSLA'
