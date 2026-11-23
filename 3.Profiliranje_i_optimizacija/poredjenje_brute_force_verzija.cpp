// Uporedivo mjerenje pet sekvencijalnih varijanti. Postojeci kod i komentari su sacuvani.
// Ukljucujemo V4 pod drugim imenom ulazne tacke, bez mijenjanja prethodne datoteke.
#define main originalni_main_v4
#include "brute_force_verzije.cpp"
#undef main
#include <array>

// [SPATIAL] V1: odvojeni nizovi koordinata, bez rucnog odmotavanja.
__attribute__((noinline)) static void verzija1(const SoA& p,const std::vector<p07::Rect>& q,Rezultati& out) {
    for (size_t j=0;j<q.size();++j) {
        auto& ids=out[j]; ids.clear(); const auto& r=q[j];
        for (size_t i=0;i<p.x.size();++i) if (p07::unutra(p.x[i],p.y[i],r)) ids.push_back(p.id[i]);
    }
}
// [SPATIAL] V3: kandidati po x; dodatni niz postoji samo tokom ove varijante.
__attribute__((noinline)) static void verzija3(const SoA& p,const std::vector<p07::Rect>& q,Rezultati& out) {
    std::vector<size_t> kandidati; kandidati.reserve(p.x.size());
    for (size_t j=0;j<q.size();++j) {
        auto& ids=out[j]; ids.clear(); kandidati.clear(); const auto& r=q[j];
        for (size_t i=0;i<p.x.size();++i) if (p.x[i]>=r.xmin && p.x[i]<=r.xmax) kandidati.push_back(i);
        for (size_t i:kandidati) if (p.y[i]>=r.ymin && p.y[i]<=r.ymax) ids.push_back(p.id[i]);
    }
}
static void odradi(int v,const std::vector<p07::Point>& p,const SoA& s,const std::vector<p07::Rect>& q,Rezultati& out) {
    switch(v) {case 0: referenca(p,q,out);break; case 1: verzija1(s,q,out);break;
       case 2: verzija2(s,q,out);break; case 3: verzija3(s,q,out);break; case 4: verzija4(s,q,out);break;}
}
static bool provjeri_sve(const std::vector<p07::Point>& p,const std::vector<p07::Rect>& q) {
    SoA s(p); Rezultati ref(q.size()),out(q.size()); referenca(p,q,ref);
    for(int v=1;v<=4;++v) { odradi(v,p,s,q,out); if(ref!=out) return false; }
    return true;
}
int main(int argc,char** argv) {
    p07::Postavke s; int runs=7; std::string mode="compare";
    for(int i=1;i<argc;++i) {
        if(i+1>=argc) return 2;
        std::string a=argv[i], b=argv[++i];
        if(a=="--mode")mode=b; else if(a=="--dist")s.dist=b;
        else if(a=="--n")s.N=std::atoi(b.c_str()); else if(a=="--m")s.M=std::atoi(b.c_str());
        else if(a=="--w")s.w=std::atof(b.c_str()); else if(a=="--repeats")runs=std::atoi(b.c_str()); else return 2;
    }
    const std::array<std::string,5> names={"ref","v1","v2","v3","v4"};
    int selected=-1; for(int i=0;i<5;++i) if(mode==names[i]) selected=i;
    if((selected<0 && mode!="compare" && mode!="verify") || (s.dist!="uniform" && s.dist!="clustered") ||
       s.N<0 || s.M<0 || !std::isfinite(s.w) || s.w<0 || s.w>1 || runs<1 || runs>1000) return 2;
    if(mode=="verify") {
        const double nan=std::numeric_limits<double>::quiet_NaN();
        std::vector<p07::Point> e={{0,0,10},{1,1,11},{.5,.5,12},{.5,.5,13},{-1,0,14},{nan,0,15}};
        std::vector<p07::Rect> eq={{0,0,1,1},{.5,.5,.5,.5},{-2,-1,0,0},{2,2,3,3},{1,0,0,1},{nan,0,1,1}};
        for(size_t n=0;n<=e.size();++n) if(!provjeri_sve({e.begin(),e.begin()+n},eq)) return 1;
        if(!provjeri_sve(e,{}) || !rubni_testovi()) return 1;
        std::vector<p07::Point> p; std::vector<p07::Rect> q,q2; SoA d;
        p07::generisi(s,p,q); generisi_soa(s,d,q2);
        if(p.size()!=d.x.size() || q.size()!=q2.size()) return 1;
        for(size_t i=0;i<p.size();++i) if(p[i].x!=d.x[i] || p[i].y!=d.y[i] || p[i].id!=d.id[i]) return 1;
        for(size_t i=0;i<q.size();++i) if(q[i].xmin!=q2[i].xmin || q[i].xmax!=q2[i].xmax || q[i].ymin!=q2[i].ymin || q[i].ymax!=q2[i].ymax) return 1;
        if(!provjeri_sve(p,q)) return 1;
        std::puts("PROSAO: referenca i sve cetiri verzije imaju jednake ID-jeve i redoslijed."); return 0;
    }
    std::vector<p07::Point> p; SoA data; std::vector<p07::Rect> q,q2;
    auto prep0=Sat::now();
    if(selected<=0) p07::generisi(s,p,q);
    if(selected!=0) {generisi_soa(s,data,q2); if(selected>0) q=std::move(q2);}
    auto prep1=Sat::now();
    std::array<Rezultati,5> result; std::array<std::vector<double>,5> times;
    for(int v=0;v<5;++v) if(selected<0 || selected==v) result[v].resize(q.size());
    auto run=[&](int v) {auto a=Sat::now(); odradi(v,p,data,q,result[v]); auto b=Sat::now(); times[v].push_back(std::chrono::duration<double,std::milli>(b-a).count());};
    for(int v=0;v<5;++v) if(selected<0 || selected==v) {run(v);times[v].clear();}
    for(int rep=0;rep<runs;++rep) for(int k=0;k<5;++k) {int v=(rep+k)%5; if(selected<0 || selected==v) run(v);}
    if(selected<0) for(int v=1;v<5;++v) if(result[0]!=result[v]) {std::fprintf(stderr,"GRESKA: %s\n",names[v].c_str());return 1;}
    std::printf("N=%d M=%d w=%.3f dist=%s seed=%llu repeats=%d AVX2=%s mode=%s\n",s.N,s.M,s.w,s.dist.c_str(),s.seed,runs,avx2_dostupan()?"da":"ne",mode.c_str());
    std::printf("priprema_ms=%.3f\n",std::chrono::duration<double,std::milli>(prep1-prep0).count());
    for(int v=0;v<5;++v) if(!times[v].empty()) {
        auto sorted=times[v]; std::sort(sorted.begin(),sorted.end()); size_t hits=0;for(const auto& x:result[v])hits+=x.size();
        std::printf("%s uzorci_ms=",names[v].c_str()); for(double t:times[v])std::printf("%.3f,",t);
        std::printf(" medijan_ms=%.3f pogodaka=%zu checksum=%llu\n",sorted[sorted.size()/2],hits,(unsigned long long)p07::kontrolni_zbir(result[v]));
    }
}
