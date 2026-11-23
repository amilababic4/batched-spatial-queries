// Eksperimentalne brute-force varijante. Izvorni referentni program ostaje netaknut.
#include "../src/common/podaci.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using Rezultati = std::vector<std::vector<int>>;
using Sat = std::chrono::steady_clock;

struct SoA {
    std::vector<double> x, y;
    std::vector<int> id;
    explicit SoA(const std::vector<p07::Point>& p) {
        x.reserve(p.size()); y.reserve(p.size()); id.reserve(p.size());
        for (const auto& t : p) { x.push_back(t.x); y.push_back(t.y); id.push_back(t.id); }
    }
};

// Varijanta 0: osnovna implementacija, isti uslov i redoslijed kao referenca.
static void osnovna(const std::vector<p07::Point>& p, const std::vector<p07::Rect>& q, Rezultati& out) {
    for (size_t j = 0; j < q.size(); ++j) {
        out[j].clear();
        for (const auto& t : p) if (p07::unutra(t.x, t.y, q[j])) out[j].push_back(t.id);
    }
}

// Iteracija 1: odvojeni nizovi koordinata i identifikatora (SoA).
static void soa(const SoA& p, const std::vector<p07::Rect>& q, Rezultati& out) {
    for (size_t j = 0; j < q.size(); ++j) {
        auto& ids = out[j]; ids.clear(); const auto& r = q[j];
        for (size_t i = 0; i < p.x.size(); ++i)
            if (p07::unutra(p.x[i], p.y[i], r)) ids.push_back(p.id[i]);
    }
}

// Iteracija 2: SoA i rucno odmotavanje cetiri provjere.
static void odmotano(const SoA& p, const std::vector<p07::Rect>& q, Rezultati& out) {
    const size_t n = p.x.size();
    for (size_t j = 0; j < q.size(); ++j) {
        auto& ids = out[j]; ids.clear(); const auto& r = q[j]; size_t i = 0;
        for (; i + 4 <= n; i += 4) {
            if (p07::unutra(p.x[i], p.y[i], r)) ids.push_back(p.id[i]);
            if (p07::unutra(p.x[i+1], p.y[i+1], r)) ids.push_back(p.id[i+1]);
            if (p07::unutra(p.x[i+2], p.y[i+2], r)) ids.push_back(p.id[i+2]);
            if (p07::unutra(p.x[i+3], p.y[i+3], r)) ids.push_back(p.id[i+3]);
        }
        for (; i < n; ++i) if (p07::unutra(p.x[i], p.y[i], r)) ids.push_back(p.id[i]);
    }
}

// Iteracija 3: u prvom prolazu izdvojiti x kandidate, zatim provjeriti y.
static void x_kandidati(const SoA& p, const std::vector<p07::Rect>& q, Rezultati& out) {
    const size_t n = p.x.size();
    std::vector<size_t> kandidati;
    kandidati.reserve(n);
    for (size_t j = 0; j < q.size(); ++j) {
        auto& ids = out[j]; ids.clear(); kandidati.clear(); const auto& r = q[j];
        for (size_t i = 0; i < n; ++i)
            if (p.x[i] >= r.xmin && p.x[i] <= r.xmax) kandidati.push_back(i);
        for (size_t i : kandidati)
            if (p.y[i] >= r.ymin && p.y[i] <= r.ymax) ids.push_back(p.id[i]);
    }
}

static void pokreni(int v, const std::vector<p07::Point>& p, const SoA& s,
                    const std::vector<p07::Rect>& q, Rezultati& out) {
    switch (v) { case 0: osnovna(p,q,out); break; case 1: soa(s,q,out); break;
        case 2: odmotano(s,q,out); break; default: x_kandidati(s,q,out); }
}

static bool provjeri(const std::vector<p07::Point>& p, const std::vector<p07::Rect>& q) {
    SoA s(p); Rezultati ref(q.size()), out(q.size()); osnovna(p,q,ref);
    for (int v = 1; v < 4; ++v) { pokreni(v,p,s,q,out); if (out != ref) {
        std::fprintf(stderr,"GRESKA: razlika u varijanti %d\n",v); return false;
    }}
    return true;
}

int main(int argc, char** argv) {
    p07::Postavke s; s.N=200000; s.M=2000; s.w=0.02; s.seed=42;
    int runs=7; if (argc > 1) s.dist=argv[1]; if (argc > 2) s.w=std::atof(argv[2]);
    const char* imena[]={"osnovna","soa","odmotano","x_kandidati"};
    int odabrana = -1;
    if (argc > 3) {
        for (int v=0; v<4; ++v) if (std::string(argv[3]) == imena[v]) odabrana=v;
        if (odabrana < 0) {
            std::fprintf(stderr,"Nepoznata varijanta: %s\n",argv[3]); return 2;
        }
    }
    if (argc > 4 || (s.dist!="uniform" && s.dist!="clustered") || s.w<0 || s.w>1) return 2;
    std::vector<p07::Point> p; std::vector<p07::Rect> q; p07::generisi(s,p,q);
    // Odabrana varijanta za VTune: provjeru izvesti zasebno, bez ovog argumenta.
    if (odabrana >= 0) {
        SoA data(p); Rezultati rez(q.size()); std::vector<double> ms;
        pokreni(odabrana,p,data,q,rez); // zagrijavanje
        for (int rep=0; rep<runs; ++rep) {
            auto pocetak=Sat::now(); pokreni(odabrana,p,data,q,rez); auto kraj=Sat::now();
            ms.push_back(std::chrono::duration<double,std::milli>(kraj-pocetak).count());
        }
        std::printf("PROFIL: provjera iskljucena; pokrenuti poseban test ispravnosti.\n");
        std::printf("dist=%s N=%d M=%d w=%.3f seed=%llu ponavljanja=%d varijanta=%s\n",s.dist.c_str(),s.N,s.M,s.w,s.seed,runs,imena[odabrana]);
        std::printf("uzorci_ms="); for (double t : ms) std::printf("%.3f,",t);
        std::sort(ms.begin(),ms.end());
        std::printf(" medijan_ms=%.3f kontrolni_zbir=%llu\n",ms[runs/2],(unsigned long long)p07::kontrolni_zbir(rez));
        return 0;
    }
    // Rubovi, duplikati, prazan ulaz i velicina koja nije djeljiva sa 4.
    const std::vector<p07::Point> rubovi={{0,0,10},{1,1,11},{0.5,0.5,12},{0.5,0.5,13},{-1,0,14}};
    const std::vector<p07::Rect> upiti={{0,0,1,1},{0.5,0.5,0.5,0.5},{-2,-1,0,0},{2,2,3,3}};
    if (!provjeri(rubovi,upiti) || !provjeri({},upiti) || !provjeri(p,q)) return 1;
    std::puts("PROVJERA: sve varijante vracaju identicne ID-jeve za svaki upit.");
    SoA data(p);
    std::vector<Rezultati> rezultati(4,Rezultati(q.size()));
    std::vector<std::vector<double>> ms(4);
    for(int v=0;v<4;++v) pokreni(v,p,data,q,rezultati[v]); // zagrijavanje
    for(int rep=0;rep<runs;++rep) for(int k=0;k<4;++k) {
        int v=(rep+k)%4; auto pocetak=Sat::now(); pokreni(v,p,data,q,rezultati[v]); auto kraj=Sat::now();
        ms[v].push_back(std::chrono::duration<double,std::milli>(kraj-pocetak).count());
    }
    std::printf("dist=%s N=%d M=%d w=%.3f seed=%llu ponavljanja=%d\n",s.dist.c_str(),s.N,s.M,s.w,s.seed,runs);
    for(int v=0;v<4;++v) {
        std::printf("%s uzorci_ms=",imena[v]);
        for (double t : ms[v]) std::printf("%.3f,",t);
        std::sort(ms[v].begin(),ms[v].end());
        std::printf(" medijan_ms=%.3f kontrolni_zbir=%llu\n",ms[v][runs/2],(unsigned long long)p07::kontrolni_zbir(rezultati[v])); }
}
