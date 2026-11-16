// P07 - Batched Spatial Queries: testovi ispravnosti (docs/plan-testiranja.md)
//
// Za svaki slucaj iz plana testiranja pravimo male ulaze s poznatim
// ocekivanim rezultatom i provjeravamo:
//   - brute force (referenca)
//   - KD-tree (PSI), upiti serijski
//   - KD-tree, upiti paralelno preko ParlayLib (parlay::parallel_for)
//   - KD-tree, upiti paralelno preko OpenMP (schedule(dynamic))
// Sve cetiri metode moraju vratiti TACNO iste id-jeve za svaki upit.
//
// Pokretanje:  ./build/testovi        (izlazni kod 0 = svi testovi prosli)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <omp.h>

#include "parlay/primitives.h"
#include "parlay/sequence.h"
#include "psi/dependence/splitter.h"
#include "psi/kd_tree.h"

#include "common/podaci.h"

using p07::Point;
using p07::Rect;
using Ids = std::vector<int>;

// ---------- KD-tree (isti tipovi kao u psi_benchmark.cpp) ----------
struct Id {
    using id_type = int;
    id_type id = 0;
    bool operator<(const Id& r) const { return id < r.id; }
    bool operator==(const Id& r) const { return id == r.id; }
    friend std::ostream& operator<<(std::ostream& os, const Id& v) { return os << v.id; }
};
using PsiPoint = psi::aug_point<double, 2, Id>;
using Split = psi::orthogonal_split_rule<psi::max_stretch_dim<PsiPoint>,
                                         psi::object_median<PsiPoint>>;
using Tree = psi::kd_tree<psi::tree_traits<PsiPoint, Split>>;

enum class Nacin { Serijski, Parlay, OpenMP };
static const char* ime(Nacin n) {
    return n == Nacin::Serijski ? "KD serijski" : n == Nacin::Parlay ? "KD Parlay" : "KD OpenMP";
}

// Brute force: referenca. Neispravan pravougaonik -> prazan rezultat.
static std::vector<Ids> brute(const std::vector<Point>& t, const std::vector<Rect>& q) {
    std::vector<Ids> rez(q.size());
    for (size_t j = 0; j < q.size(); ++j) {
        if (!p07::ispravan(q[j])) continue;
        for (const auto& p : t)
            if (p07::unutra(p.x, p.y, q[j])) rez[j].push_back(p.id);
        std::sort(rez[j].begin(), rez[j].end());
    }
    return rez;
}

// KD-tree: isti postupak kao psi_benchmark.cpp (range_query + tacan uslov).
static std::vector<Ids> kd(const std::vector<Point>& t, const std::vector<Rect>& q, Nacin nacin) {
    std::vector<Ids> rez(q.size());
    if (t.empty()) return rez;   // nema tacaka -> nema sta graditi, svi rezultati prazni

    parlay::sequence<PsiPoint> pts(t.size());
    for (size_t i = 0; i < t.size(); ++i) {
        pts[i][0] = t[i].x; pts[i][1] = t[i].y; pts[i].aug.id = t[i].id;
    }
    auto stablo = std::make_unique<Tree>();
    stablo->build(parlay::make_slice(pts));
    const Tree& tree = *stablo;

    auto jedan = [&](size_t j) {
        const Rect& r = q[j];
        if (!p07::ispravan(r)) return;            // neispravne granice se odbijaju
        Tree::box_type b;
        b.first[0] = r.xmin; b.first[1] = r.ymin;
        b.second[0] = r.xmax; b.second[1] = r.ymax;
        auto nadjene = tree.range_query(b);
        for (const auto& p : nadjene)
            if (p07::unutra(p[0], p[1], r)) rez[j].push_back(p.aug.id);
        std::sort(rez[j].begin(), rez[j].end());
    };

    const int64_t m = (int64_t)q.size();
    if (nacin == Nacin::Serijski) {
        for (int64_t j = 0; j < m; ++j) jedan((size_t)j);
    } else if (nacin == Nacin::Parlay) {
        parlay::parallel_for(0, q.size(), jedan, 1);
    } else {
#pragma omp parallel for schedule(dynamic, 1) num_threads(4)
        for (int64_t j = 0; j < m; ++j) jedan((size_t)j);
    }
    return rez;
}

// ---------- mali okvir za testove ----------
static int prosli = 0, pali = 0;

static std::string opis(const Ids& v) {
    std::string s = std::to_string(v.size()) + " tacaka";
    if (!v.empty() && v.size() <= 6) {
        s += " {";
        for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + std::to_string(v[i]);
        s += "}";
    }
    return s;
}

// ocekivano: po jedan sortiran vektor id-jeva za svaki upit;
// prazan 'ocekivano' = uporedi samo s brute forceom (slucajni testovi).
static void provjeri(const std::string& naziv, const std::vector<Point>& t,
                     const std::vector<Rect>& q, std::vector<Ids> ocekivano = {}) {
    for (auto& e : ocekivano) std::sort(e.begin(), e.end());
    auto bf = brute(t, q);
    bool ok = true;
    auto uporedi = [&](const char* metoda, const std::vector<Ids>& dobijeno, const std::vector<Ids>& ref) {
        for (size_t j = 0; j < q.size(); ++j)
            if (dobijeno[j] != ref[j]) {
                if (ok) std::printf("PAO     %s\n", naziv.c_str());
                std::printf("          upit %zu, %s: dobijeno %s, ocekivano %s\n", j, metoda,
                            opis(dobijeno[j]).c_str(), opis(ref[j]).c_str());
                ok = false;
            }
    };
    const auto& ref = ocekivano.empty() ? bf : ocekivano;
    if (!ocekivano.empty()) uporedi("brute force", bf, ref);
    for (Nacin n : {Nacin::Serijski, Nacin::Parlay, Nacin::OpenMP}) uporedi(ime(n), kd(t, q, n), ref);

    size_t ukupno = 0;
    for (auto& r : ref) ukupno += r.size();
    if (ok) {
        std::printf("PROSAO  %-62s (%zu upita, %zu pogodaka)\n", naziv.c_str(), q.size(), ukupno);
        ++prosli;
    } else {
        ++pali;
    }
}

// Mreza 10 x 10 tacaka: (i/10, j/10), i,j = 0..9, id = 10*i + j.
static std::vector<Point> mreza() {
    std::vector<Point> t;
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 10; ++j) t.push_back({i / 10.0, j / 10.0, 10 * i + j});
    return t;
}
// Ocekivani id-jevi tacaka mreze za koje vrijedi uslov nad (i, j).
static Ids mreza_gdje(const std::function<bool(int, int)>& uslov) {
    Ids v;
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 10; ++j)
            if (uslov(i, j)) v.push_back(10 * i + j);
    return v;
}

int main() {
    std::printf("Testovi prostornih upita (brute force + KD-tree serijski / Parlay / OpenMP)\n\n");
    const auto M = mreza();

    // ---- 1. Mali pravougaonici ----
    provjeri("1. Mali pravougaonik oko jedne tacke", M,
             {{0.29, 0.69, 0.31, 0.71}}, {{37}});
    provjeri("1. Mali pravougaonik izmedju tacaka (0 pogodaka)", M,
             {{0.31, 0.31, 0.39, 0.39}}, {{}});

    // ---- 2. Srednji pravougaonici ----
    provjeri("2. Srednji pravougaonik [0.25,0.75]^2 (5x5 tacaka)", M,
             {{0.25, 0.25, 0.75, 0.75}},
             {mreza_gdje([](int i, int j) { return i >= 3 && i <= 7 && j >= 3 && j <= 7; })});

    // ---- 3. Veliki pravougaonici ----
    provjeri("3. Veliki pravougaonik = cijeli prostor [0,1]^2", M,
             {{0, 0, 1, 1}}, {mreza_gdje([](int, int) { return true; })});
    provjeri("3. Veliki pravougaonik veci od prostora [-5,5]^2", M,
             {{-5, -5, 5, 5}}, {mreza_gdje([](int, int) { return true; })});

    // ---- 4. Prazni pravougaonici ----
    provjeri("4. Pravougaonik potpuno izvan tacaka", M,
             {{2, 2, 3, 3}, {-1, -1, -0.5, -0.5}, {0.95, 0, 2, 1}}, {{}, {}, {}});
    provjeri("4. Prazan skup tacaka", {},
             {{0, 0, 1, 1}, {0.2, 0.2, 0.3, 0.3}}, {{}, {}});

    // ---- 5. Tacke na granicama ----
    provjeri("5. Ivice i uglovi ukljuceni: [0.2,0.5]^2 (4x4 tacaka)", M,
             {{0.2, 0.2, 0.5, 0.5}},
             {mreza_gdje([](int i, int j) { return i >= 2 && i <= 5 && j >= 2 && j <= 5; })});
    {
        // Tacke za "dlaku" (jedan double) izvan ivice ne smiju biti ukljucene,
        // a tacke tacno na ivici moraju.
        const double lo = 0.2, hi = 0.5;
        const double ispod = std::nextafter(lo, 0.0), iznad = std::nextafter(hi, 1.0);
        std::vector<Point> t = {
            {lo, 0.3, 0}, {hi, 0.3, 1}, {0.3, lo, 2}, {0.3, hi, 3},          // na ivicama
            {lo, lo, 4}, {hi, hi, 5}, {lo, hi, 6}, {hi, lo, 7},              // uglovi
            {ispod, 0.3, 8}, {iznad, 0.3, 9}, {0.3, ispod, 10}, {0.3, iznad, 11},  // za dlaku izvan
            {iznad, iznad, 12}, {ispod, ispod, 13}};
        provjeri("5. Tacke za jedan double izvan ivice nisu ukljucene", t,
                 {{lo, lo, hi, hi}}, {{0, 1, 2, 3, 4, 5, 6, 7}});
    }

    // ---- 6. Pravougaonici razlicitih proporcija ----
    provjeri("6. Vodoravna duz (visina 0) kroz red y = 0.5", M,
             {{0, 0.5, 1, 0.5}}, {mreza_gdje([](int, int j) { return j == 5; })});
    provjeri("6. Uspravna duz (sirina 0) kroz kolonu x = 0.3", M,
             {{0.3, 0, 0.3, 1}}, {mreza_gdje([](int i, int) { return i == 3; })});
    provjeri("6. Uska izduzena traka [0,1] x [0.45,0.55]", M,
             {{0, 0.45, 1, 0.55}}, {mreza_gdje([](int, int j) { return j == 5; })});
    provjeri("6. Pravougaonik nulte povrsine = jedna tacka (0.7, 0.2)", M,
             {{0.7, 0.2, 0.7, 0.2}}, {{72}});

    // ---- 7. Ponovljene koordinate ----
    {
        std::vector<Point> t = M;
        for (int k = 0; k < 50; ++k) t.push_back({0.55, 0.55, 1000 + k});   // 50 tacaka na istom mjestu
        Ids svih50;
        for (int k = 0; k < 50; ++k) svih50.push_back(1000 + k);
        Ids sa_mrezom = svih50;
        for (int id : mreza_gdje([](int i, int j) { return i >= 5 && i <= 6 && j >= 5 && j <= 6; }))
            sa_mrezom.push_back(id);
        provjeri("7. 50 tacaka s istim koordinatama: upit tacno u toj tacki", t,
                 {{0.55, 0.55, 0.55, 0.55}}, {svih50});
        provjeri("7. 50 tacaka s istim koordinatama: sire okruzenje", t,
                 {{0.5, 0.5, 0.6, 0.6}}, {sa_mrezom});
        provjeri("7. 50 tacaka s istim koordinatama: upit pored njih", t,
                 {{0.56, 0.56, 0.59, 0.59}}, {{}});
    }
    {
        std::vector<Point> t;   // svih 1000 tacaka na istoj poziciji
        for (int k = 0; k < 1000; ++k) t.push_back({0.5, 0.5, k});
        Ids svi(1000);
        for (int k = 0; k < 1000; ++k) svi[k] = k;
        provjeri("7. Svih 1000 tacaka na istoj poziciji", t,
                 {{0, 0, 1, 1}, {0.5, 0.5, 0.5, 0.5}, {0.6, 0, 1, 1}}, {svi, svi, {}});
    }

    // ---- Dodatno: negativne koordinate i neispravne granice ----
    {
        std::vector<Point> t = M;
        for (auto& p : t) { p.x -= 0.5; p.y -= 0.5; }   // mreza pomjerena u [-0.5, 0.4]
        provjeri("Dodatno: negativne koordinate", t,
                 {{-0.5, -0.5, -0.25, -0.25}, {-0.15, -0.15, 0.15, 0.15}},
                 {mreza_gdje([](int i, int j) { return i <= 2 && j <= 2; }),
                  mreza_gdje([](int i, int j) { return i >= 4 && i <= 6 && j >= 4 && j <= 6; })});
    }
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        std::vector<Rect> losi = {{0.6, 0.2, 0.4, 0.8}, {0.2, 0.6, 0.8, 0.4}, {nan, 0, 1, 1}};
        bool odbijeni = true;
        for (auto& r : losi) odbijeni = odbijeni && !p07::ispravan(r);
        std::printf("%s  %-62s\n", odbijeni ? "PROSAO" : "PAO   ", "Dodatno: ispravan() odbija xmin > xmax, ymin > ymax i NaN");
        odbijeni ? ++prosli : ++pali;
        provjeri("Dodatno: neispravne granice daju prazan rezultat", M, losi, {{}, {}, {}});
    }

    // ---- Slucajni testovi: KD-tree mora dati isto sto i brute force ----
    {
        std::mt19937_64 rng(7);
        std::uniform_real_distribution<double> U(0, 1);
        // Koordinate zaokruzene na 0.01 => mnogo ponovljenih koordinata i tacaka tacno na ivicama.
        auto zaokruzi = [](double v) { return std::round(v * 100) / 100; };
        for (int skup = 0; skup < 3; ++skup) {
            p07::Postavke s;
            s.N = 20000; s.M = 0; s.seed = 100 + skup;
            s.dist = skup == 1 ? "clustered" : "uniform";
            std::vector<Point> t; std::vector<Rect> prazno;
            p07::generisi(s, t, prazno);
            if (skup == 2) for (auto& p : t) { p.x = zaokruzi(p.x); p.y = zaokruzi(p.y); }

            std::vector<Rect> q;
            const double sirine[] = {0.001, 0.02, 0.1, 0.5};
            for (int k = 0; k < 400; ++k) {
                double w = sirine[k % 4], h = sirine[(k / 4) % 4];   // razne velicine i proporcije
                double cx = U(rng), cy = U(rng);
                Rect r{cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2};
                if (skup == 2) r = {zaokruzi(r.xmin), zaokruzi(r.ymin), zaokruzi(r.xmax), zaokruzi(r.ymax)};
                q.push_back(r);
            }
            const char* nazivi[] = {"Slucajno: 20.000 tacaka ravnomjerno, 400 upita",
                                    "Slucajno: 20.000 tacaka grupisano, 400 upita",
                                    "Slucajno: koordinate na mrezi 0.01 (mnogo ivica i duplikata)"};
            provjeri(nazivi[skup], t, q);
        }
    }

    std::printf("\nUkupno: %d proslo, %d palo\n", prosli, pali);
    return pali == 0 ? 0 : 1;
}
