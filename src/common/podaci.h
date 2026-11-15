// P07 - Batched Spatial Queries: zajednicko generisanje podataka.
//
// Generise ISTE tacke i pravougaonike kao src/brute_force_ViseUpita.cpp
// (isti seed, isti redoslijed poziva generatora), da bi brute force i
// KD-tree radili nad identicnim ulazom.

#pragma once

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace p07 {

struct Point { double x, y; int id; };
struct Rect { double xmin, ymin, xmax, ymax; };

struct Postavke {
    int N = 200000;                 // broj tacaka
    int M = 2000;                   // broj pravougaonika (upita)
    double w = 0.02;                // sirina stranice pravougaonika
    std::string dist = "uniform";   // "uniform" ili "clustered"
    unsigned long long seed = 42;
};

inline double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }

// Tacno isti uslov kao u brute force kodu (ivice se racunaju).
inline bool unutra(double x, double y, const Rect& r) {
    return x >= r.xmin && x <= r.xmax && y >= r.ymin && y <= r.ymax;
}

inline void generisi(const Postavke& s, std::vector<Point>& tacke,
                     std::vector<Rect>& pravougaonici) {
    std::mt19937_64 rng(s.seed);
    std::uniform_real_distribution<double> U(0.0, 1.0);

    tacke.assign(s.N, Point{});
    if (s.dist == "clustered") {
        const int K = 16;
        std::vector<std::pair<double, double>> centri(K);
        for (auto& c : centri) c = { U(rng), U(rng) };
        std::normal_distribution<double> rasipanje(0.0, 0.02);
        std::uniform_int_distribution<int> izaberi(0, K - 1);
        for (int i = 0; i < s.N; ++i) {
            auto& c = centri[izaberi(rng)];
            tacke[i] = { clamp01(c.first + rasipanje(rng)),
                         clamp01(c.second + rasipanje(rng)), i };
        }
    } else {
        for (int i = 0; i < s.N; ++i) tacke[i] = { U(rng), U(rng), i };
    }

    pravougaonici.assign(s.M, Rect{});
    for (auto& r : pravougaonici) {
        double cx = U(rng), cy = U(rng);
        r = { clamp01(cx - s.w / 2), clamp01(cy - s.w / 2),
              clamp01(cx + s.w / 2), clamp01(cy + s.w / 2) };
    }
}

// Isti kontrolni zbir kao u brute_force_ViseUpita.cpp
// (ne zavisi od redoslijeda id-jeva unutar upita).
inline uint64_t kontrolni_zbir(const std::vector<std::vector<int>>& rez) {
    uint64_t k = 0;
    for (size_t j = 0; j < rez.size(); ++j)
        for (int id : rez[j]) k += (uint64_t)id * (j + 1);
    return k;
}

}  // namespace p07
