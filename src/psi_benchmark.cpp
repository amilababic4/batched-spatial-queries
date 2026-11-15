// P07 - Batched Spatial Queries: KD-tree (PSI / SpaceTreeLib) za grupu
// pravougaonih upita.
//
// Isti izvorni fajl gradi tri programa (vidi CMakeLists.txt):
//   psi_seq    - P07_SERIAL:  upiti jedan za drugim, jedna nit
//   psi_parlay -              upiti paralelno preko parlay::parallel_for
//   psi_omp    - P07_OPENMP:  upiti paralelno preko OpenMP petlje
//
// Program:
//   1) generise iste tacke i pravougaonike kao brute_force_ViseUpita.cpp
//   2) gradi KD-tree jednom (mjeri se posebno)
//   3) za svaki pravougaonik pozove tree.range_query i spremi id-jeve
//   4) uporedi rezultate sa brute force rezultatima (izvan mjerenja)
//   5) ispise vremena (medijan od vise ponavljanja, nakon zagrijavanja)
//
// Argumenti (svi opcionalni):
//   --n 200000 --m 2000 --w 0.02 --dist uniform|clustered --seed 42
//   --repeats 7 --no-check --schedule static|dynamic   (samo psi_omp)

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "parlay/primitives.h"
#include "parlay/sequence.h"
#include "psi/dependence/splitter.h"
#include "psi/kd_tree.h"

#ifdef P07_OPENMP
#include <omp.h>
#endif

#include "common/podaci.h"

// ---------- PSI tipovi (po uzoru na reference/psi/example/kd_tree.h) ----------

struct Id {
    using id_type = int;
    id_type id = 0;
    bool operator<(const Id& r) const { return id < r.id; }
    bool operator==(const Id& r) const { return id == r.id; }
    friend std::ostream& operator<<(std::ostream& os, const Id& v) { return os << v.id; }
};

// double koordinate, kao u brute force kodu (bez zaokruzivanja ulaza)
using PsiPoint = psi::aug_point<double, 2, Id>;
using Split = psi::orthogonal_split_rule<psi::max_stretch_dim<PsiPoint>,
                                         psi::object_median<PsiPoint>>;
using Tree = psi::kd_tree<psi::tree_traits<PsiPoint, Split>>;

using Clock = std::chrono::steady_clock;
static double sekunde(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}
static double medijan(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

static const char* naziv_varijante() {
#if defined(P07_SERIAL)
    return "psi_seq (serijski)";
#elif defined(P07_OPENMP)
    return "psi_omp (OpenMP)";
#else
    return "psi_parlay (ParlayLib)";
#endif
}

static int broj_niti() {
#if defined(P07_SERIAL)
    return 1;
#elif defined(P07_OPENMP)
    return omp_get_max_threads();
#else
    return (int)parlay::num_workers();
#endif
}

int main(int argc, char** argv) {
    p07::Postavke s;
    int ponavljanja = 7;
    bool provjera = true;
    std::string raspored = "static";

    for (int i = 1; i < argc; ++i) {
        auto arg = [&](const char* ime) { return std::strcmp(argv[i], ime) == 0 && i + 1 < argc; };
        if (arg("--n")) s.N = std::atoi(argv[++i]);
        else if (arg("--m")) s.M = std::atoi(argv[++i]);
        else if (arg("--w")) s.w = std::atof(argv[++i]);
        else if (arg("--dist")) s.dist = argv[++i];
        else if (arg("--seed")) s.seed = std::strtoull(argv[++i], nullptr, 10);
        else if (arg("--repeats")) ponavljanja = std::max(1, std::atoi(argv[++i]));
        else if (arg("--schedule")) raspored = argv[++i];
        else if (std::strcmp(argv[i], "--no-check") == 0) provjera = false;
        else { std::fprintf(stderr, "Nepoznat argument: %s\n", argv[i]); return 2; }
    }

    // ===== 1) PODACI =====
    std::vector<p07::Point> tacke;
    std::vector<p07::Rect> pravougaonici;
    p07::generisi(s, tacke, pravougaonici);
    const size_t N = tacke.size(), M = pravougaonici.size();

    // Pretvaranje u PSI format (id ostaje uz tacku)
    parlay::sequence<PsiPoint> psi_tacke(N);
    for (size_t i = 0; i < N; ++i) {
        psi_tacke[i][0] = tacke[i].x;
        psi_tacke[i][1] = tacke[i].y;
        psi_tacke[i].aug.id = tacke[i].id;
    }
    std::vector<Tree::box_type> kutije(M);
    for (size_t j = 0; j < M; ++j) {
        const auto& r = pravougaonici[j];
        kutije[j].first[0] = r.xmin;  kutije[j].first[1] = r.ymin;
        kutije[j].second[0] = r.xmax; kutije[j].second[1] = r.ymax;
    }

    // ===== 2) IZGRADNJA STABLA =====
    // build() preuredjuje ulazni niz, pa svako mjerenje radi nad svjezom kopijom.
    // Stablo se ne moze kopirati ni dodijeliti, pa za svako mjerenje pravimo novo.
    std::vector<double> t_izgradnja;
    std::unique_ptr<Tree> stablo;
    for (int rep = 0; rep <= ponavljanja; ++rep) {   // rep 0 = zagrijavanje
        stablo.reset();
        stablo = std::make_unique<Tree>();
        auto kopija = psi_tacke;
        auto t0 = Clock::now();
        stablo->build(parlay::make_slice(kopija));
        auto t1 = Clock::now();
        if (rep > 0) t_izgradnja.push_back(sekunde(t0, t1));
    }
    const Tree& tree = *stablo;   // stablo se tokom upita ne mijenja

    // ===== 3) UPITI =====
    std::vector<std::vector<int>> rezultati(M);

    // Jedan upit: svaka iteracija pise samo u svoj rezultati[j] -> nema data race.
    auto obradi_upit = [&](size_t j) {
        auto nadjene = tree.range_query(kutije[j]);
        auto& ids = rezultati[j];
        ids.clear();
        ids.reserve(nadjene.size());
        const auto& r = pravougaonici[j];
        for (const auto& p : nadjene) {
            // PSI za double koristi malu toleranciju na ivicama; ovdje
            // primjenjujemo doslovno isti uslov kao brute force.
            if (p07::unutra(p[0], p[1], r)) ids.push_back(p.aug.id);
        }
    };

    auto sve_upite = [&]() {
#if defined(P07_SERIAL)
        for (size_t j = 0; j < M; ++j) obradi_upit(j);
#elif defined(P07_OPENMP)
        const int64_t m = (int64_t)M;
        if (raspored == "dynamic") {
#pragma omp parallel for schedule(dynamic, 1)
            for (int64_t j = 0; j < m; ++j) obradi_upit((size_t)j);
        } else {
#pragma omp parallel for schedule(static)
            for (int64_t j = 0; j < m; ++j) obradi_upit((size_t)j);
        }
#else
        parlay::parallel_for(0, M, obradi_upit, 1);
#endif
    };

    std::vector<double> t_upiti;
    for (int rep = 0; rep <= ponavljanja; ++rep) {   // rep 0 = zagrijavanje
        auto t0 = Clock::now();
        sve_upite();
        auto t1 = Clock::now();
        if (rep > 0) t_upiti.push_back(sekunde(t0, t1));
    }

    // ===== 4) PROVJERA TACNOSTI (izvan mjerenja) =====
    uint64_t ukupno = 0;
    for (auto& r : rezultati) ukupno += r.size();
    uint64_t kontrolni = p07::kontrolni_zbir(rezultati);

    int pogresnih = -1;
    double t_brute = 0;
    if (provjera) {
        pogresnih = 0;
        auto t0 = Clock::now();
        std::vector<std::vector<int>> brute(M);
        for (size_t j = 0; j < M; ++j)
            for (size_t i = 0; i < N; ++i)
                if (p07::unutra(tacke[i].x, tacke[i].y, pravougaonici[j]))
                    brute[j].push_back(tacke[i].id);
        t_brute = sekunde(t0, Clock::now());

        for (size_t j = 0; j < M; ++j) {
            auto a = rezultati[j];       // kopija, da sortiranje ne dira rezultate
            std::sort(a.begin(), a.end());
            // brute je vec sortiran po id (tacke idu redom)
            if (a != brute[j]) {
                if (pogresnih < 5)
                    std::printf("  RAZLIKA u upitu %zu: KD-tree %zu, brute force %zu tacaka\n",
                                j, a.size(), brute[j].size());
                ++pogresnih;
            }
        }
    }

    // ===== 5) ISPIS =====
    double izg = medijan(t_izgradnja), upi = medijan(t_upiti);
    std::printf("==================== %s ====================\n", naziv_varijante());
    std::printf("Tacaka (N):                   %zu\n", N);
    std::printf("Pravougaonika (M):            %zu\n", M);
    std::printf("Sirina pravougaonika (w):     %g\n", s.w);
    std::printf("Raspodjela tacaka:            %s\n", s.dist.c_str());
    std::printf("Broj niti:                    %d\n", broj_niti());
#ifdef P07_OPENMP
    std::printf("OpenMP schedule:              %s\n", raspored.c_str());
#endif
    std::printf("Ukupno pronadjenih (K):       %llu\n", (unsigned long long)ukupno);
    std::printf("Prosjecno po pravougaoniku:   %.1f\n", (double)ukupno / M);
    std::printf("Kontrolni zbir:               %llu\n", (unsigned long long)kontrolni);

    std::printf("\n-------------------- VRIJEME (medijan od %d) --------------------\n", ponavljanja);
    std::printf("Izgradnja stabla:             %.2f ms\n", izg * 1000);
    std::printf("Svi upiti:                    %.2f ms\n", upi * 1000);
    std::printf("Ukupno (izgradnja + upiti):   %.2f ms\n", (izg + upi) * 1000);
    std::printf("Vrijeme po upitu:             %.2f us\n", upi / M * 1e6);
    std::printf("QPS (upita u sekundi):        %.0f\n", M / upi);

    if (provjera) {
        std::printf("\n-------------------- PROVJERA --------------------\n");
        std::printf("Brute force (1 prolaz, 1 nit): %.2f ms\n", t_brute * 1000);
        std::printf("Ubrzanje upita u odnosu na brute force: %.1fx\n", t_brute / upi);
        if (pogresnih == 0)
            std::printf("OK: svih %zu upita daje iste id-jeve kao brute force.\n", M);
        else
            std::printf("GRESKA: %d od %zu upita se razlikuje od brute force!\n", pogresnih, M);
    }
    return pogresnih > 0 ? 1 : 0;
}
