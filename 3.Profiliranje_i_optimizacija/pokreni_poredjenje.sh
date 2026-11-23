#!/usr/bin/env bash
set -euo pipefail
root='/mnt/c/Users/DT User/Desktop/VI semestar/PRS/3.Profiliranje_i_optimizacija/sirova_mjerenja/poredjenje_svih'
bin='/home/dt_user/prs-eksperimenti/poredjenje5/poredjenje'
mkdir -p "$root"
{
  date -Is
  g++-14 --version | head -1
  uname -a
  lscpu | grep -E '^(Architecture|CPU\(s\)|Model name|Thread|Core|Socket)' || true
  sha256sum "$bin"
} > "$root/sistem_wsl.txt"
ctest --test-dir /home/dt_user/prs-markeri --output-on-failure > "$root/ctest.txt"
for dist in uniform clustered; do
  for width in 0.005 0.02 0.1; do
    tag="${dist}_${width}"
    "$bin" --mode verify --n 200000 --m 2000 --dist "$dist" --w "$width" > "$root/${tag}_verify.txt"
    "$bin" --mode compare --n 200000 --m 2000 --dist "$dist" --w "$width" --repeats 7 > "$root/${tag}_compare.txt"
    echo "KOMPLETNO $tag"
  done
done
for spec in '20000 200 malo' '1000000 2000 veliko'; do
  read -r n m tag <<< "$spec"
  "$bin" --mode verify --n "$n" --m "$m" --w 0.02 > "$root/${tag}_verify.txt"
  "$bin" --mode compare --n "$n" --m "$m" --w 0.02 --repeats 7 > "$root/${tag}_compare.txt"
  echo "KOMPLETNO $tag"
done
