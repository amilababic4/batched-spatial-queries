
// Brute force: skup tacaka u prostoru + JEDAN pravougaonik.
// Program pronalazi sve tacke koje su unutar pravougaonika (ivice se racunaju).

#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

struct Point { double x, y; int id; };

int main() {
    // ===== 1) PROSTOR SA TACKAMA =====
    int N = 200000;                    // broj tacaka
    std::mt19937_64 rng(42);           //  iste tacke svaki put
    std::uniform_real_distribution<double> U(0.0, 1.0);

    std::vector<Point> tacke(N);
    for (int i = 0; i < N; ++i)
        tacke[i] = { U(rng), U(rng), i };   // nasumicna tacka u kvadratu [0,1] x [0,1]

    // ===== 2) PRAVOUGAONIK  =====
    double xmin = 0.40, xmax = 0.42;   
    double ymin = 0.60, ymax = 0.62;   

    // ===== 3) BRUTE FORCE PRETRAGA =====
    // Prolazak kroz sve tacke
    auto t0 = std::chrono::steady_clock::now();

    std::vector<Point> pronadjene;
    for (int i = 0; i < N; ++i) {
        const Point& p = tacke[i];
        if (p.x >= xmin && p.x <= xmax && p.y >= ymin && p.y <= ymax)
            pronadjene.push_back(p);
    }

    auto t1 = std::chrono::steady_clock::now();
    double vrijeme = std::chrono::duration<double>(t1 - t0).count();

    // ===== 4) ISPIS =====
    std::printf("Ukupno tacaka u prostoru: %d\n", N);
    std::printf("Pravougaonik: x od %.3f do %.3f, y od %.3f do %.3f\n", xmin, xmax, ymin, ymax);
    std::printf("Pronadjeno tacaka unutar pravougaonika: %zu\n", pronadjene.size());
    std::printf("Vrijeme pretrage: %.6f s\n\n", vrijeme);

    std::printf("  id        x          y\n");
    for (const Point& p : pronadjene)
        std::printf("%6d   %.6f   %.6f\n", p.id, p.x, p.y);

    return 0;
}


