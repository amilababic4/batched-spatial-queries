// P07 - Batched Spatial Queries: BRUTE FORCE sa VISE pravougaonika


#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

struct Point { double x, y; int id; };
struct Rect { double xmin, ymin, xmax, ymax; };

static double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }

// Da li je tacka p unutar pravougaonika r
static inline bool unutra(const Point& p, const Rect& r) {
    return p.x >= r.xmin && p.x <= r.xmax && p.y >= r.ymin && p.y <= r.ymax;
}

int main() {

    int N = 200000;                 // broj tacaka u prostoru
    int M = 2000;                   // broj pravougaonika (upita)
    double w = 0.02;                // velicina pravougaonika (sirina stranice)
    std::string dist = "uniform";   // "uniform" = ravnomjerno, "clustered" = u gomilama
    unsigned long long seed = 42;
    int prikazi = 3;
    int ponavljanja = 5;            // koliko puta ponoviti mjerenje


    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> U(0.0, 1.0);

    // PROSTOR SA TACKAMA 
    std::vector<Point> tacke(N);
    if (dist == "clustered") {
        // 16 centara, tacke rasute oko njih
        const int K = 16;
        std::vector<std::pair<double, double>> centri(K);
        for (auto& c : centri) c = { U(rng), U(rng) };
        std::normal_distribution<double> rasipanje(0.0, 0.02);
        std::uniform_int_distribution<int> izaberi(0, K - 1);
        for (int i = 0; i < N; ++i) {
            auto& c = centri[izaberi(rng)];
            tacke[i] = { clamp01(c.first + rasipanje(rng)), clamp01(c.second + rasipanje(rng)), i };
        }
    }
    else {
        for (int i = 0; i < N; ++i) tacke[i] = { U(rng), U(rng), i };
    }

    //  M PRAVOUGAONIKA 
    // Svaki pravougaonik: nasumican centar, stranica w, odsjecen na granice prostora.
    std::vector<Rect> pravougaonici(M);
    for (auto& r : pravougaonici) {
        double cx = U(rng), cy = U(rng);
        r = { clamp01(cx - w / 2), clamp01(cy - w / 2), clamp01(cx + w / 2), clamp01(cy + w / 2) };
    }

    //3) BRUTE FORCE: za svaki pravougaonik provjeri sve tacke 
    // rezultati[j] = lista id-ova tacaka koje su unutar pravougaonika j
    // Mjerenje se ponavlja vise puta, pa se uzima najbolje vrijeme i medijan.
    std::vector<std::vector<int>> rezultati(M);
    std::vector<double> vremena;

    for (int rep = 0; rep < ponavljanja; ++rep) {
        for (auto& r : rezultati) r.clear();      // isprazni rezultate prije novog mjerenja

        auto t0 = std::chrono::steady_clock::now();
        for (int j = 0; j < M; ++j) {             // VANJSKA petlja: pravougaonici
            for (int i = 0; i < N; ++i) {         // UNUTRASNJA petlja: sve tacke
                if (unutra(tacke[i], pravougaonici[j]))
                    rezultati[j].push_back(tacke[i].id);
            }
        }
        auto t1 = std::chrono::steady_clock::now();

        vremena.push_back(std::chrono::duration<double>(t1 - t0).count());
    }
    std::sort(vremena.begin(), vremena.end());
    double t_min = vremena.front();               // najbolje vrijeme
    double t_med = vremena[vremena.size() / 2];   // medijan

    //  ISPIS DETALJA ZA PRVIH NEKOLIKO PRAVOUGAONIKA 
    for (int j = 0; j < std::min(prikazi, M); ++j) {
        const Rect& r = pravougaonici[j];
        std::printf("Pravougaonik %d: x od %.4f do %.4f, y od %.4f do %.4f -> %zu tacaka\n",
            j, r.xmin, r.xmax, r.ymin, r.ymax, rezultati[j].size());
        for (int k = 0; k < (int)rezultati[j].size() && k < 5; ++k) {
            const Point& p = tacke[rezultati[j][k]];
            std::printf("     tacka id=%6d  (%.4f, %.4f)\n", p.id, p.x, p.y);
        }
        if (rezultati[j].size() > 5) std::printf("     ... i jos %zu\n", rezultati[j].size() - 5);
        std::printf("\n");
    }

    //  5) SAZETAK 
    uint64_t ukupno = 0, kontrolni = 0;
    for (int j = 0; j < M; ++j) {
        ukupno += rezultati[j].size();
        for (int id : rezultati[j]) kontrolni += (uint64_t)id * (j + 1);
    }

    std::printf("==================== SAZETAK ====================\n");
    std::printf("Tacaka u prostoru (N):        %d\n", N);
    std::printf("Pravougaonika (M):            %d\n", M);
    std::printf("Raspodjela tacaka:            %s\n", dist.c_str());
    std::printf("Ukupno pronadjenih (K):       %llu\n", (unsigned long long)ukupno);
    std::printf("Prosjecno po pravougaoniku:   %.1f\n", (double)ukupno / M);
    std::printf("Kontrolni zbir:               %llu\n", (unsigned long long)kontrolni);
    std::printf("Broj provjera (N*M):          %.0f\n", (double)N * M);

    //  6) VRIJEME 
    std::printf("\n==================== VRIJEME ====================\n");
    std::printf("Broj mjerenja:                %d\n", ponavljanja);
    std::printf("Najbolje vrijeme (min):       %.2f ms\n", t_min * 1000);
    std::printf("Medijan vremena:              %.2f ms\n", t_med * 1000);
    std::printf("Vrijeme po pravougaoniku:     %.2f us\n", t_med / M * 1e6);
    std::printf("Provjera u sekundi:           %.0f miliona\n", (double)N * M / t_med / 1e6);
    return 0;
}
