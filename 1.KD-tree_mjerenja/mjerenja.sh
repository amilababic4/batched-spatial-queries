#!/usr/bin/env bash
# P07 - automatska mjerenja (vrijeme programa + perf brojaci) -> CSV
#
# Pokretanje iz korijena repoa, nakon "cmake --build build -j":
#   bash mjerenja.sh            # puni skup mjerenja
#   bash mjerenja.sh brzo       # mali skup, za probu (~1 min)
#
# Rezultati idu u folder rezultati/:
#   mjerenja_<datum>.csv   - jedan red po pokretanju (program, niti, ulaz, vremena, perf)
#   provjera_<datum>.csv   - tacnost prema brute forceu + vrijeme brute forcea
#   racunar_<datum>.txt    - podaci o racunaru, kompajleru i verziji biblioteke
#
# Napomena: vremena (izgradnja, upiti) mjeri nas program, medijan od REPEATS.
# perf brojaci se mjere u posebnom pokretanju s --repeats 1 i --no-check,
# i obuhvataju CIJELI program (generisanje podataka + 1 izgradnja + 1 prolaz upita).

set -uo pipefail

# ======================= POSTAVKE =======================
N_LIST=(10000 100000 1000000)
M_LIST=(100 1000 10000)
DIST_LIST=(uniform clustered)
W_LIST=(0.02)                 # npr. (0.005 0.02 0.1) za male/srednje/velike pravougaonike
THREADS=(1 2 4 8)
REPEATS=7
SEED=42
EVENTS="task-clock,cycles,instructions,cache-misses,duration_time"

if [[ "${1:-}" == "brzo" ]]; then
    N_LIST=(100000); M_LIST=(1000); DIST_LIST=(uniform); THREADS=(1 2 4); REPEATS=3
fi

BUILD=${BUILD:-build}
OUT=rezultati
mkdir -p "$OUT"
DATUM=$(date +%Y-%m-%d_%H-%M)
CSV="$OUT/mjerenja_$DATUM.csv"
PROV="$OUT/provjera_$DATUM.csv"
INFO="$OUT/racunar_$DATUM.txt"

# perf: u WSL-u nije u PATH-u, pa ga trazimo u /usr/lib/linux-tools
if [[ -z "${PERF:-}" ]]; then
    if PERF=$(ls /usr/lib/linux-tools/*/perf 2>/dev/null | head -1) && [[ -n "$PERF" ]]; then :;
    elif command -v perf >/dev/null && perf --version >/dev/null 2>&1; then PERF=perf;
    else PERF=""; fi
fi
[[ -z "$PERF" ]] && echo "UPOZORENJE: perf nije pronadjen, perf kolone ce biti prazne."

for p in psi_seq psi_parlay psi_omp; do
    [[ -x "$BUILD/$p" ]] || { echo "Nema $BUILD/$p - prvo pokreni: cmake --build build -j"; exit 1; }
done

# ======================= PODACI O RACUNARU =======================
{
    echo "Datum: $(date)"
    echo "CPU: $(lscpu | grep -m1 'Model name' | sed 's/.*: *//')"
    echo "Broj logickih jezgri (nproc): $(nproc)"
    echo "Kompajler: $(grep -m1 CMAKE_CXX_COMPILER: $BUILD/CMakeCache.txt 2>/dev/null | cut -d= -f2) ($(grep -m1 CMAKE_CXX_COMPILER_VERSION $BUILD/CMakeFiles/*/CMakeCXXCompiler.cmake 2>/dev/null | grep -o '"[^"]*"' | tr -d '"'))"
    echo "Build tip: $(grep -m1 CMAKE_BUILD_TYPE: $BUILD/CMakeCache.txt 2>/dev/null | cut -d= -f2)"
    echo "PSI verzija: $(git -C reference/psi rev-parse --short HEAD 2>/dev/null)"
    echo "Nas commit: $(git rev-parse --short HEAD 2>/dev/null)"
    echo "perf: ${PERF:-nema} $( [[ -n "$PERF" ]] && $PERF --version 2>/dev/null)"
    echo "Ponavljanja (medijan od): $REPEATS"
} > "$INFO"
cat "$INFO"; echo

echo "program,schedule,niti,N,M,w,dist,izgradnja_ms,upiti_ms,ukupno_ms,qps,K,kontrolni_zbir,perf_task_clock_ms,perf_cycles,perf_instructions,perf_ipc,perf_cache_misses,perf_elapsed_s" > "$CSV"
echo "N,M,w,dist,K,kontrolni_zbir,brute_force_ms,tacno" > "$PROV"

# vrijednost iz ispisa naseg programa, npr. polje "Svi upiti:" -> 5.31
uzmi() { grep -m1 "$1" <<<"$2" | awk -F: '{print $2}' | awk '{print $1}'; }
# vrijednost dogadjaja iz perf -x, ispisa
perf_polje() { awk -F, -v e="$1" '$3 ~ "^"e || $3 ~ "/"e"/" {print $1; exit}' "$2" | grep -E '^[0-9.]+$' || true; }

UKUPNO=0; GRESKE=0

pokreni() {   # pokreni <program> <schedule> <niti> <argumenti...>
    local prog=$1 sched=$2 niti=$3; shift 3
    local env=()
    case $prog in
        psi_parlay) env=(PARLAY_NUM_THREADS=$niti) ;;
        psi_omp)    env=(OMP_NUM_THREADS=$niti) ;;
    esac
    local sched_arg=(); [[ $prog == psi_omp ]] && sched_arg=(--schedule "$sched")

    # 1) vrijeme (nas program, medijan)
    local ispis
    ispis=$(env "${env[@]}" "$BUILD/$prog" "$@" "${sched_arg[@]}" --repeats "$REPEATS" --no-check) \
        || { echo "  GRESKA pri pokretanju $prog"; GRESKE=$((GRESKE+1)); return; }
    local izg upi uk qps K kz
    izg=$(uzmi "Izgradnja stabla:" "$ispis"); upi=$(uzmi "Svi upiti:" "$ispis")
    uk=$(uzmi "Ukupno (izgradnja" "$ispis"); qps=$(uzmi "QPS" "$ispis")
    K=$(uzmi "Ukupno pronadjenih" "$ispis"); kz=$(uzmi "Kontrolni zbir:" "$ispis")

    # 2) perf brojaci (jedan prolaz)
    local tc="" cy="" ins="" ipc="" cm="" el=""
    if [[ -n "$PERF" ]]; then
        local tmp; tmp=$(mktemp)
        env "${env[@]}" "$PERF" stat -x, -e "$EVENTS" -o "$tmp" \
            "$BUILD/$prog" "$@" "${sched_arg[@]}" --repeats 1 --no-check >/dev/null 2>&1
        tc=$(perf_polje task-clock "$tmp"); cy=$(perf_polje cycles "$tmp")
        ins=$(perf_polje instructions "$tmp"); cm=$(perf_polje cache-misses "$tmp")
        [[ -n "$cy" && -n "$ins" ]] && ipc=$(awk -v i="$ins" -v c="$cy" 'BEGIN{printf "%.3f", i/c}')
        el=$(perf_polje duration_time "$tmp")
        [[ -n "$el" ]] && el=$(awk -v d="$el" 'BEGIN{printf "%.4f", d/1e9}')
        rm -f "$tmp"
    fi

    echo "$prog,$sched,$niti,$N,$M,$W,$DIST,$izg,$upi,$uk,$qps,$K,$kz,$tc,$cy,$ins,$ipc,$cm,$el" >> "$CSV"
    printf "  %-10s %-7s niti=%-2s  izgradnja=%8s ms  upiti=%8s ms  IPC=%s\n" "$prog" "$sched" "$niti" "$izg" "$upi" "${ipc:--}"
    UKUPNO=$((UKUPNO+1))
}

# ======================= GLAVNA PETLJA =======================
for DIST in "${DIST_LIST[@]}"; do
for N in "${N_LIST[@]}"; do
for M in "${M_LIST[@]}"; do
for W in "${W_LIST[@]}"; do
    ARGS=(--n "$N" --m "$M" --w "$W" --dist "$DIST" --seed "$SEED")
    echo "=== N=$N  M=$M  w=$W  dist=$DIST ==="

    # provjera tacnosti + vrijeme brute forcea (jednom po ulazu)
    prov=$("$BUILD/psi_seq" "${ARGS[@]}" --repeats 1)
    ok=$(grep -q "^OK:" <<<"$prov" && echo DA || echo NE)
    [[ $ok == NE ]] && GRESKE=$((GRESKE+1))
    echo "$N,$M,$W,$DIST,$(uzmi 'Ukupno pronadjenih' "$prov"),$(uzmi 'Kontrolni zbir:' "$prov"),$(uzmi 'Brute force' "$prov"),$ok" >> "$PROV"
    echo "  provjera prema brute forceu: $ok   (brute force: $(uzmi 'Brute force' "$prov") ms)"

    pokreni psi_seq - 1 "${ARGS[@]}"
    for t in "${THREADS[@]}"; do pokreni psi_parlay - "$t" "${ARGS[@]}"; done
    for t in "${THREADS[@]}"; do pokreni psi_omp static "$t" "${ARGS[@]}"; done
    for t in "${THREADS[@]}"; do pokreni psi_omp dynamic "$t" "${ARGS[@]}"; done
done; done; done; done

echo
echo "Gotovo: $UKUPNO mjerenja, gresaka: $GRESKE"
echo "Rezultati: $CSV"
echo "Provjera:  $PROV"
echo "Racunar:   $INFO"
