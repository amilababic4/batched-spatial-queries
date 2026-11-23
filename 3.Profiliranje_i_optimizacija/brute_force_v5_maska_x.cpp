// P07 - sekvencijalni eksperiment V5: AVX2 maska x prije citanja y.
// [OPT-V5] Nova putanja je odvojena; postojeca V4 i njeni komentari su sacuvani.
// Ukljucuje V4, isti generator i referencu; main se preimenuje samo za ovaj fajl.
#define main originalni_main_v4
#include "brute_force_verzije.cpp"
#undef main
#include <climits>
#include <exception>
#include <stdexcept>

#if P07_MOZE_AVX2
// [OPT-V5] Po cetiri tacke najprije provjeri x. Y se ucitava samo kada je
// barem jedna x koordinata u intervalu. Dobit zavisi od raspodjele i ulaza.
// [SPATIAL] Ordered AVX poredenja su netacna za NaN kao skalarni uslov.
__attribute__((target("avx2"), noinline))
static void verzija5_avx2(const SoA& p, const std::vector<p07::Rect>& q, Rezultati& out) {
    const size_t n=p.x.size();
    for (size_t j=0; j<q.size(); ++j) {
        auto& ids=out[j]; ids.clear(); const auto& r=q[j]; size_t i=0;
        const __m256d xmin=_mm256_set1_pd(r.xmin), xmax=_mm256_set1_pd(r.xmax);
        const __m256d ymin=_mm256_set1_pd(r.ymin), ymax=_mm256_set1_pd(r.ymax);
        for (; i+4<=n; i+=4) {
            const __m256d x=_mm256_loadu_pd(p.x.data()+i);
            const __m256d mx=_mm256_and_pd(_mm256_cmp_pd(x,xmin,_CMP_GE_OQ),
                                           _mm256_cmp_pd(x,xmax,_CMP_LE_OQ));
            unsigned mask=static_cast<unsigned>(_mm256_movemask_pd(mx));
            if (!mask) continue; // [OPT-V5] Nema moguceg pogotka u ovoj grupi.
            const __m256d y=_mm256_loadu_pd(p.y.data()+i);
            const __m256d my=_mm256_and_pd(_mm256_cmp_pd(y,ymin,_CMP_GE_OQ),
                                           _mm256_cmp_pd(y,ymax,_CMP_LE_OQ));
            mask &= static_cast<unsigned>(_mm256_movemask_pd(my));
            // [SPATIAL] Poredak bitova je poredak ulaznih tacaka, ne ID-jeva.
            while (mask) { const unsigned bit=static_cast<unsigned>(__builtin_ctz(mask));
                ids.push_back(p.id[i+bit]); mask &= mask-1; }
        }
        // [SPATIAL] Skalarni ostatak cuva isti inkluzivni uslov kao referenca.
        for (; i<n; ++i) if (p07::unutra(p.x[i],p.y[i],r)) ids.push_back(p.id[i]);
    }
}
#endif

static void verzija5(const SoA& p,const std::vector<p07::Rect>& q,Rezultati& out) {
#if P07_MOZE_AVX2
    if (avx2_dostupan()) { verzija5_avx2(p,q,out); return; }
#endif
    // [OPT-V5] Bez AVX2 zadrzavamo postojecu sigurnu skalarnu implementaciju.
    verzija2(p,q,out);
}

static bool provjeri_v5(const std::vector<p07::Point>& p,const std::vector<p07::Rect>& q) {
    SoA s(p); Rezultati ref(q.size()),v4(q.size()),v5(q.size());
    referenca(p,q,ref); verzija4(s,q,v4); verzija5(s,q,v5);
    return ref==v4 && ref==v5;
}

static bool posebni_testovi_v5() {
    const double inf=std::numeric_limits<double>::infinity();
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const double van=std::nextafter(1.0,2.0);
    // ID-jevi su namjerno nesortirani i ponovljeni: redoslijed mora ostati isti.
    const std::vector<p07::Point> p={{0,0,42},{1,1,7},{.5,.5,42},{van,1,-2},
        {-inf,0,10},{inf,0,11},{0,inf,12},{0,-inf,13},{nan,0,14},{0,nan,15},{-0.0,0,99}};
    const std::vector<p07::Rect> q={{0,0,1,1},{-inf,-inf,inf,inf},
        {inf,0,inf,0},{0,inf,0,inf},{0,0,0,0},
        {1,0,0,1},{0,1,1,0},{nan,0,1,1},{0,0,1,nan}};
    const Rezultati expected={{42,7,42,99},{42,7,42,-2,10,11,12,13,99},
        {11},{12},{42,99},{},{},{},{}};
    Rezultati ref(q.size()); referenca(p,q,ref);
    if (ref!=expected || !provjeri_v5(p,q) || !provjeri_v5({},q) || !provjeri_v5(p,{})) return false;
    // Sve kombinacije x/y maski od po 4 bita; racuna se redoslijed ulaza.
    for (unsigned xm=0;xm<16;++xm) for (unsigned ym=0;ym<16;++ym) {
        const int ids[4]={9,-3,9,0}; std::vector<p07::Point> bits;
        std::vector<int> want;
        for (unsigned i=0;i<4;++i) {
            bits.push_back({(xm&(1u<<i))?.5:2.0,(ym&(1u<<i))?.5:2.0,ids[i]});
            if ((xm&(1u<<i)) && (ym&(1u<<i))) want.push_back(ids[i]);
        }
        const std::vector<p07::Rect> box={{0,0,1,1}};
        SoA s(bits); Rezultati out(1); verzija5(s,box,out);
        if (out[0]!=want || !provjeri_v5(bits,box)) return false;
    }
    // Sve duzine vektora, ukljucujuci ostatke 0, 1, 2 i 3.
    for (size_t n=0;n<=p.size();++n)
        if (!provjeri_v5(std::vector<p07::Point>(p.begin(),p.begin()+n),q)) return false;
    return true;
}

int main(int argc,char** argv) {
    p07::Postavke s; int repeats=7; std::string mode="compare";
    try {
        for (int i=1;i<argc;++i) {
            if (i+1>=argc) { std::fprintf(stderr,"Nedostaje vrijednost za %s\n",argv[i]); return 2; }
            const std::string key=argv[i], val=argv[++i]; size_t used=0;
            if (key=="--mode") mode=val;
            else if (key=="--dist") s.dist=val;
            else if (key=="--n" || key=="--m" || key=="--repeats") {
                long long x=std::stoll(val,&used);
                if (used!=val.size() || x<0 || x>INT_MAX) throw std::invalid_argument("integer");
                if (key=="--n") s.N=static_cast<int>(x);
                else if (key=="--m") s.M=static_cast<int>(x);
                else repeats=static_cast<int>(x);
            } else if (key=="--w") {
                s.w=std::stod(val,&used); if (used!=val.size()) throw std::invalid_argument("w");
            } else if (key=="--seed") {
                if (val.empty() || val[0]=='-') throw std::invalid_argument("seed");
                s.seed=std::stoull(val,&used); if (used!=val.size()) throw std::invalid_argument("seed");
            } else { std::fprintf(stderr,"Nepoznat argument: %s\n",key.c_str()); return 2; }
        }
    } catch (const std::exception&) { std::fprintf(stderr,"Neispravna vrijednost argumenta\n"); return 2; }
    if ((mode!="verify" && mode!="compare" && mode!="v4" && mode!="v5") ||
        (s.dist!="uniform" && s.dist!="clustered") || !std::isfinite(s.w) ||
        s.w<0 || s.w>1 || repeats<1 || repeats>1000) return 2;

    if (mode=="verify") {
        if (!posebni_testovi_v5()) { std::fprintf(stderr,"GRESKA: rubni slucajevi V5\n"); return 1; }
        std::vector<p07::Point> p; std::vector<p07::Rect> q,qs; SoA direct;
        p07::generisi(s,p,q); generisi_soa(s,direct,qs);
        if (p.size()!=direct.x.size() || q.size()!=qs.size()) return 1;
        for (size_t i=0;i<p.size();++i)
            if (p[i].x!=direct.x[i] || p[i].y!=direct.y[i] || p[i].id!=direct.id[i]) return 1;
        for (size_t i=0;i<q.size();++i)
            if (q[i].xmin!=qs[i].xmin || q[i].xmax!=qs[i].xmax ||
                q[i].ymin!=qs[i].ymin || q[i].ymax!=qs[i].ymax) return 1;
        Rezultati ref(q.size()),v5(q.size()); referenca(p,q,ref); verzija5(direct,qs,v5);
        if (ref!=v5 || !provjeri_v5(p,q)) { std::fprintf(stderr,"GRESKA: ID-jevi se razlikuju\n"); return 1; }
        std::printf("PROSAO: 256 parova maski, rubovi i svi ID-jevi; AVX2=%s\n",avx2_dostupan()?"da":"ne (skalarni put)");
        return 0;
    }

    SoA data; std::vector<p07::Rect> q; auto prep0=Sat::now();
    generisi_soa(s,data,q); auto prep1=Sat::now();
    Rezultati out4(q.size()),out5(q.size()); std::vector<double> t4,t5;
    auto run=[&](int v) {
        auto a=Sat::now(); if(v==4) verzija4(data,q,out4); else verzija5(data,q,out5); auto b=Sat::now();
        (v==4?t4:t5).push_back(std::chrono::duration<double,std::milli>(b-a).count());
    };
    if (mode=="compare" || mode=="v4") {run(4);t4.clear();}
    if (mode=="compare" || mode=="v5") {run(5);t5.clear();}
    for(int rep=0;rep<repeats;++rep) {
        if(mode=="compare") {if(rep%2==0) {run(4);run(5);} else {run(5);run(4);}}
        else run(mode=="v4"?4:5);
    }
    if(mode=="compare" && out4!=out5) {std::fprintf(stderr,"GRESKA: V4 i V5 daju razlicite rezultate\n");return 1;}
    std::printf("N=%d M=%d w=%.3f dist=%s seed=%llu repeats=%d AVX2=%s\n",s.N,s.M,s.w,s.dist.c_str(),s.seed,repeats,avx2_dostupan()?"da":"ne");
    std::printf("generisanje_soa_i_upita_ms=%.3f\n",std::chrono::duration<double,std::milli>(prep1-prep0).count());
    auto ispis=[&](const char* name,std::vector<double> t,const Rezultati& r) {
        if(t.empty()) return;
        std::printf("%s uzorci_ms=",name); for(double x:t)std::printf("%.3f,",x);
        std::sort(t.begin(),t.end());size_t count=0;for(const auto& v:r)count+=v.size();
        const size_t mid=t.size()/2;
        const double medijan=t.size()%2 ? t[mid] : (t[mid-1]+t[mid])/2;
        std::printf(" medijan_ms=%.3f pogodaka=%zu checksum=%llu\n",medijan,count,(unsigned long long)p07::kontrolni_zbir(r));
    };
    ispis("v4",t4,out4);ispis("v5",t5,out5);return 0;
}
