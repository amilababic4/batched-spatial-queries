// P07 - cetvrta eksperimentalna verzija sekvencijalne brute-force pretrage.
// Referentne implementacije i njihovi komentari ostaju netaknuti.
// [SPATIAL] oznacava prostorni upit; [OPT-V4] oznacava novu optimizaciju.
#include "../src/common/podaci.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#if !defined(P07_DISABLE_AVX2) && (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define P07_MOZE_AVX2 1
#else
#define P07_MOZE_AVX2 0
#endif

using Rezultati = std::vector<std::vector<int>>;
using Sat = std::chrono::steady_clock;

// [OPT-V4] Koordinate u odvojenim nizovima omogucavaju vektorsko ucitavanje.
struct SoA {
    std::vector<double> x, y;
    std::vector<int> id;
    SoA() = default;
    explicit SoA(const std::vector<p07::Point>& tacke) {
        x.reserve(tacke.size()); y.reserve(tacke.size()); id.reserve(tacke.size());
        for (const auto& t : tacke) { x.push_back(t.x); y.push_back(t.y); id.push_back(t.id); }
    }
};

// [OPT-V4] Generisanje direktno u SoA izbjegava istovremenu AoS kopiju.
// Redoslijed RNG poziva odgovara p07::generisi; to se provjerava u --mode verify.
static void generisi_soa(const p07::Postavke& s, SoA& out, std::vector<p07::Rect>& q) {
    std::mt19937_64 rng(s.seed);
    std::uniform_real_distribution<double> U(0.0,1.0);
    out.x.clear(); out.y.clear(); out.id.clear();
    out.x.reserve(s.N); out.y.reserve(s.N); out.id.reserve(s.N);
    auto dodaj=[&](double x,double y,int id) { out.x.push_back(x); out.y.push_back(y); out.id.push_back(id); };
    if (s.dist=="clustered") {
        const int K=16; std::vector<std::pair<double,double>> centri(K);
        for (auto& c:centri) c={U(rng),U(rng)};
        std::normal_distribution<double> rasipanje(0.0,0.02);
        std::uniform_int_distribution<int> izaberi(0,K-1);
        for (int i=0;i<s.N;++i) {
            auto& c=centri[izaberi(rng)];
            const double x=p07::clamp01(c.first+rasipanje(rng));
            const double y=p07::clamp01(c.second+rasipanje(rng));
            dodaj(x,y,i);
        }
    } else {
        for (int i=0;i<s.N;++i) { const double x=U(rng), y=U(rng); dodaj(x,y,i); }
    }
    q.assign(s.M,p07::Rect{});
    for (auto& r:q) {
        const double cx=U(rng), cy=U(rng);
        r={p07::clamp01(cx-s.w/2),p07::clamp01(cy-s.w/2),p07::clamp01(cx+s.w/2),p07::clamp01(cy+s.w/2)};
    }
}

// [SPATIAL] Referentna provjera obuhvata i tacke na granicama pravougaonika.
static void referenca(const std::vector<p07::Point>& tacke, const std::vector<p07::Rect>& upiti, Rezultati& out) {
    for (size_t j=0; j<upiti.size(); ++j) {
        auto& ids=out[j]; ids.clear();
        for (const auto& t : tacke) if (p07::unutra(t.x,t.y,upiti[j])) ids.push_back(t.id);
    }
}

// Verzija 2: SoA i odmotavanje cetiri skalarne provjere; sluzi za usporedbu.
static void verzija2(const SoA& p, const std::vector<p07::Rect>& q, Rezultati& out) {
    const size_t n=p.x.size();
    for (size_t j=0; j<q.size(); ++j) {
        auto& ids=out[j]; ids.clear(); const auto& r=q[j]; size_t i=0;
        for (; i+4<=n; i+=4) {
            if (p07::unutra(p.x[i],p.y[i],r)) ids.push_back(p.id[i]);
            if (p07::unutra(p.x[i+1],p.y[i+1],r)) ids.push_back(p.id[i+1]);
            if (p07::unutra(p.x[i+2],p.y[i+2],r)) ids.push_back(p.id[i+2]);
            if (p07::unutra(p.x[i+3],p.y[i+3],r)) ids.push_back(p.id[i+3]);
        }
        for (; i<n; ++i) if (p07::unutra(p.x[i],p.y[i],r)) ids.push_back(p.id[i]);
    }
}

#if P07_MOZE_AVX2
// [OPT-V4] Cetiri double koordinate istovremeno; maska zadrzava redoslijed ID-jeva.
// Ordered poredenje je false za NaN, kao i referentni skalarni uslov.
__attribute__((target("avx2"), noinline))
static void verzija4_avx2(const SoA& p, const std::vector<p07::Rect>& q, Rezultati& out) {
    const size_t n=p.x.size();
    for (size_t j=0; j<q.size(); ++j) {
        auto& ids=out[j]; ids.clear(); const auto& r=q[j]; size_t i=0;
        const __m256d xmin=_mm256_set1_pd(r.xmin), xmax=_mm256_set1_pd(r.xmax);
        const __m256d ymin=_mm256_set1_pd(r.ymin), ymax=_mm256_set1_pd(r.ymax);
        for (; i+4<=n; i+=4) {
            const __m256d x=_mm256_loadu_pd(p.x.data()+i), y=_mm256_loadu_pd(p.y.data()+i);
            // [SPATIAL] xmin <= x <= xmax i ymin <= y <= ymax.
            const __m256d mx=_mm256_and_pd(_mm256_cmp_pd(x,xmin,_CMP_GE_OQ),_mm256_cmp_pd(x,xmax,_CMP_LE_OQ));
            const __m256d my=_mm256_and_pd(_mm256_cmp_pd(y,ymin,_CMP_GE_OQ),_mm256_cmp_pd(y,ymax,_CMP_LE_OQ));
            unsigned mask=static_cast<unsigned>(_mm256_movemask_pd(_mm256_and_pd(mx,my)));
            while (mask) { const unsigned bit=static_cast<unsigned>(__builtin_ctz(mask)); ids.push_back(p.id[i+bit]); mask &= mask-1; }
        }
        // [SPATIAL] Skalarni ostatak za broj tacaka koji nije djeljiv sa cetiri.
        for (; i<n; ++i) if (p07::unutra(p.x[i],p.y[i],r)) ids.push_back(p.id[i]);
    }
}
#endif

static bool avx2_dostupan() {
#if P07_MOZE_AVX2
    return __builtin_cpu_supports("avx2");
#else
    return false;
#endif
}
static void verzija4(const SoA& p,const std::vector<p07::Rect>& q,Rezultati& out) {
#if P07_MOZE_AVX2
    if (avx2_dostupan()) { verzija4_avx2(p,q,out); return; }
#endif
    // Siguran skalarni povratni put na procesorima bez AVX2.
    verzija2(p,q,out);
}

static bool provjeri(const std::vector<p07::Point>& p,const std::vector<p07::Rect>& q) {
    SoA s(p); Rezultati ref(q.size()),v2(q.size()),v4(q.size());
    referenca(p,q,ref); verzija2(s,q,v2); verzija4(s,q,v4);
    return ref==v2 && ref==v4;
}

static bool rubni_testovi() {
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const double izvan=std::nextafter(1.0,2.0);
    const std::vector<p07::Point> p={{0,0,10},{1,1,11},{0.5,0.5,12},{0.5,0.5,13},{-1,0,14},{izvan,1,15},{nan,0,16}};
    const std::vector<p07::Rect> q={{0,0,1,1},{0.5,0.5,0.5,0.5},{-2,-1,0,0},{2,2,3,3},{1,0,0,1},{nan,0,1,1}};
    Rezultati ocekivani={{10,11,12,13},{12,13},{10,14},{},{},{}};
    Rezultati stvarni(q.size()); referenca(p,q,stvarni);
    if (stvarni!=ocekivani || !provjeri(p,q) || !provjeri({},q) || !provjeri(p,{})) return false;
    // Testira sve moguce ostatke nakon AVX bloka: 0, 1, 2 i 3.
    for (size_t n=0;n<=p.size();++n) if (!provjeri(std::vector<p07::Point>(p.begin(),p.begin()+n),q)) return false;
    return true;
}

int main(int argc,char** argv) {
    p07::Postavke s; int ponavljanja=7; std::string mode="compare";
    for (int i=1;i<argc;++i) {
        if (i+1>=argc) { std::fprintf(stderr,"Nedostaje vrijednost za %s\n",argv[i]); return 2; }
        const std::string arg=argv[i], val=argv[++i];
        if (arg=="--mode") mode=val; else if (arg=="--dist") s.dist=val;
        else if (arg=="--n") s.N=std::atoi(val.c_str()); else if (arg=="--m") s.M=std::atoi(val.c_str());
        else if (arg=="--w") s.w=std::atof(val.c_str()); else if (arg=="--repeats") ponavljanja=std::atoi(val.c_str());
        else { std::fprintf(stderr,"Nepoznat argument: %s\n",arg.c_str()); return 2; }
    }
    if ((mode!="verify" && mode!="compare" && mode!="v2" && mode!="v4") ||
        (s.dist!="uniform" && s.dist!="clustered") || s.N<0 || s.M<0 ||
        !std::isfinite(s.w) || s.w<0 || s.w>1 || ponavljanja<1 || ponavljanja>1000) return 2;
    if (mode=="verify") {
        std::vector<p07::Point> p; std::vector<p07::Rect> q, q_soa; SoA direktno;
        p07::generisi(s,p,q); generisi_soa(s,direktno,q_soa);
        if (p.size()!=direktno.x.size() || q.size()!=q_soa.size()) return 1;
        for (size_t i=0;i<p.size();++i)
            if (p[i].x!=direktno.x[i] || p[i].y!=direktno.y[i] || p[i].id!=direktno.id[i]) return 1;
        for (size_t i=0;i<q.size();++i)
            if (q[i].xmin!=q_soa[i].xmin || q[i].xmax!=q_soa[i].xmax ||
                q[i].ymin!=q_soa[i].ymin || q[i].ymax!=q_soa[i].ymax) return 1;
        Rezultati ref(q.size()), direktni(q.size()); referenca(p,q,ref); verzija4(direktno,q_soa,direktni);
        if (!rubni_testovi() || !provjeri(p,q) || ref!=direktni) { std::fprintf(stderr,"GRESKA: rezultati se razlikuju\n"); return 1; }
        std::printf("PROSAO: rubni slucajevi i identicni ID-jevi svih upita; AVX2=%s\n",avx2_dostupan()?"da":"ne (skalarni put)"); return 0;
    }
    SoA data; std::vector<p07::Rect> q;
    const auto prep0=Sat::now(); generisi_soa(s,data,q); const auto prep1=Sat::now();
    Rezultati out2(q.size()),out4(q.size()); std::vector<double> t2,t4;
    auto run=[&](int v) {
        auto t0=Sat::now(); if (v==2) verzija2(data,q,out2); else verzija4(data,q,out4); auto t1=Sat::now();
        (v==2?t2:t4).push_back(std::chrono::duration<double,std::milli>(t1-t0).count());
    };
    if (mode=="compare" || mode=="v2") { run(2); t2.clear(); }
    if (mode=="compare" || mode=="v4") { run(4); t4.clear(); }
    for (int rep=0;rep<ponavljanja;++rep) {
        if (mode=="compare") { if (rep%2==0) { run(2); run(4); } else { run(4); run(2); } }
        else run(mode=="v2"?2:4);
    }
    if (mode=="compare" && out2!=out4) { std::fprintf(stderr,"GRESKA: verzije se razlikuju\n"); return 1; }
    std::printf("N=%d M=%d w=%.3f dist=%s seed=%llu repeats=%d AVX2=%s\n",s.N,s.M,s.w,s.dist.c_str(),s.seed,ponavljanja,avx2_dostupan()?"da":"ne");
    std::printf("generisanje_soa_i_upita_ms=%.3f\n",std::chrono::duration<double,std::milli>(prep1-prep0).count());
    auto ispis=[&](const char* name,std::vector<double> t,const Rezultati& r) {
        if(t.empty()) return;
        std::printf("%s uzorci_ms=",name);
        for(double x:t) std::printf("%.3f,",x);
        std::sort(t.begin(),t.end()); size_t ukupno=0; for(const auto& v:r) ukupno+=v.size();
        std::printf(" medijan_ms=%.3f pogodaka=%zu checksum=%llu\n",t[t.size()/2],ukupno,(unsigned long long)p07::kontrolni_zbir(r));
    };
    ispis("v2",t2,out2); ispis("v4",t4,out4);
    return 0;
}
