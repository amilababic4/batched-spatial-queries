// P07: izolirano i uporedivo poredjenje SEKVENCIJALNIH PSI KD-tree pretraga.
// Ne mijenja postojeci benchmark niti PSI biblioteku/podmodul.
// [PSI-ORIGINAL] range_query(box): prvo count, zatim alokacija i ispis.
// [PSI-OPT] range_query(box, scratch): ponovo koristi pozivaocev buffer.
// Oba puta koriste isti tree i isti tacni zavrsni filter, ukljucujuci ivice.
#ifndef PARLAY_SEQUENTIAL
#define PARLAY_SEQUENTIAL
#endif
#include "../src/common/podaci.h"
#include "parlay/primitives.h"
#include "parlay/sequence.h"
#include "psi/dependence/splitter.h"
#include "psi/kd_tree.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

struct Id {
    using id_type = int;
    id_type id = 0;
    bool operator<(const Id& r) const { return id < r.id; }
    bool operator==(const Id& r) const { return id == r.id; }
    friend std::ostream& operator<<(std::ostream& os, const Id& v) { return os << v.id; }
};
using PsiPoint=psi::aug_point<double,2,Id>;
using Split=psi::orthogonal_split_rule<psi::max_stretch_dim<PsiPoint>,psi::object_median<PsiPoint>>;
using Tree=psi::kd_tree<psi::tree_traits<PsiPoint,Split>>;
using Rezultati=std::vector<std::vector<int>>;
using Sat=std::chrono::steady_clock;
static double ms(Sat::time_point a,Sat::time_point b) {
    return std::chrono::duration<double,std::milli>(b-a).count();
}
#if defined(P07_PROFILE_MARKERS) && (defined(__GNUC__) || defined(__clang__))
#define P07_FAZA __attribute__((noinline))
#elif defined(P07_PROFILE_MARKERS) && defined(_MSC_VER)
#define P07_FAZA __declspec(noinline)
#else
#define P07_FAZA
#endif

// U zasebnom profiler buildu ove oznake omogucavaju pregled faza. Uobicajeni
// build ne namece noinline i koristi standardnu optimizaciju kompajlera.
static P07_FAZA void izgradi_stablo(Tree& tree,parlay::sequence<PsiPoint>& pts) {
    // Ne pozivamo build nad praznim ulazom, kao ni izvorni projektni test.
    if(!pts.empty()) tree.build(parlay::make_slice(pts));
}
static Tree::box_type napravi_kutiju(const p07::Rect& r) {
    Tree::box_type b;
    b.first[0]=r.xmin; b.first[1]=r.ymin;
    b.second[0]=r.xmax; b.second[1]=r.ymax;
    return b;
}
// [PSI-PRIPREMA] Kao u projektnom psi_seq, granice i PSI kutije se prave
// jednom prije mjerenja upita. Neispravne/NaN granice se ne salju u PSI.
struct PripremljeniUpiti {
    std::vector<Tree::box_type> kutije;
    // Posebno: 0/1, da se ne mijesa s referencama vector<bool>.
    std::vector<unsigned char> ispravni;
};
static PripremljeniUpiti pripremi_upite(const std::vector<p07::Rect>& upiti) {
    PripremljeniUpiti rezultat;
    rezultat.kutije.reserve(upiti.size());
    rezultat.ispravni.reserve(upiti.size());
    for(const auto& r:upiti) {
        rezultat.kutije.push_back(napravi_kutiju(r));
        rezultat.ispravni.push_back(static_cast<unsigned char>(p07::ispravan(r)));
    }
    return rezultat;
}
static parlay::sequence<PsiPoint> psi_podaci(const std::vector<p07::Point>& p) {
    parlay::sequence<PsiPoint> result(p.size());
    for(size_t i=0;i<p.size();++i) {
        result[i][0]=p[i].x; result[i][1]=p[i].y; result[i].aug.id=p[i].id;
    }
    return result;
}
// [SPATIAL] PSI moze zbog tolerancije vratiti i kandidata malo izvan granice.
// Ovaj tacni filter je obavezan u oba rezima. Redoslijed obilaska KD-stabla
// nije nuzno redoslijed ulaza; za provjeru sortiramo kopije rezultata.
static P07_FAZA void tacni_filter(const PsiPoint* p,size_t n,const p07::Rect& r,std::vector<int>& out) {
    out.clear();
    out.reserve(n);
    for(size_t i=0;i<n;++i) if(p07::unutra(p[i][0],p[i][1],r)) out.push_back(p[i].aug.id);
}
struct Scratch {
    parlay::sequence<PsiPoint> buf;
    size_t max_tacaka=0;
    size_t prosirenja=0;
    size_t ponovljenih_upita=0;
    explicit Scratch(size_t max_points,size_t initial=64):
        buf(std::min(max_points,std::max<size_t>(1,initial))),max_tacaka(max_points) {}
};

static P07_FAZA void original_upit(const Tree& tree,const p07::Rect& r,const Tree::box_type& box,bool ispravan,std::vector<int>& out) {
    out.clear();
    if(!ispravan) return;
    const auto nadjene=tree.range_query(box);
    tacni_filter(nadjene.begin(),nadjene.size(),r,out);
}
static P07_FAZA void buffer_upit(const Tree& tree,const p07::Rect& r,const Tree::box_type& box,bool ispravan,Scratch& s,std::vector<int>& out) {
    out.clear();
    if(!ispravan || s.max_tacaka==0) return;
    size_t written=0;
    for(;;) {
        // Overload zapisuje najvise buf.size() i NE signalizira skraćenje.
        written=tree.range_query(box,parlay::make_slice(s.buf)).first;
        if(written<s.buf.size() || s.buf.size()==s.max_tacaka) break;
        // Puno moze znaciti i tacno stajanje: ponovi dok nema sumnje ili
        // buffer ne moze primiti sve tacke stabla. Nema prekoracenja size_t.
        const size_t next=s.buf.size()>s.max_tacaka/2 ? s.max_tacaka : s.buf.size()*2;
        s.buf.resize(next);
        ++s.prosirenja;
        ++s.ponovljenih_upita;
    }
    tacni_filter(s.buf.begin(),written,r,out);
}
static P07_FAZA void obradi_original(const Tree& tree,const std::vector<p07::Rect>& q,const PripremljeniUpiti& u,Rezultati& out) {
    for(size_t j=0;j<q.size();++j) original_upit(tree,q[j],u.kutije[j],u.ispravni[j]!=0,out[j]);
}
static P07_FAZA void obradi_buffer(const Tree& tree,const std::vector<p07::Rect>& q,const PripremljeniUpiti& u,Scratch& s,Rezultati& out) {
    for(size_t j=0;j<q.size();++j) buffer_upit(tree,q[j],u.kutije[j],u.ispravni[j]!=0,s,out[j]);
}
static Rezultati brute(const std::vector<p07::Point>& p,const std::vector<p07::Rect>& q) {
    Rezultati out(q.size());
    for(size_t j=0;j<q.size();++j) {
        if(!p07::ispravan(q[j])) continue;
        for(const auto& t:p) if(p07::unutra(t.x,t.y,q[j])) out[j].push_back(t.id);
        std::sort(out[j].begin(),out[j].end());
    }
    return out;
}
static bool jednako_bez_redoslijeda(Rezultati a,Rezultati b) {
    if(a.size()!=b.size()) return false;
    for(size_t j=0;j<a.size();++j) {
        std::sort(a[j].begin(),a[j].end()); std::sort(b[j].begin(),b[j].end());
        if(a[j]!=b[j]) { std::fprintf(stderr,"GRESKA: razlika u upitu %zu (%zu / %zu ID-jeva)\n",j,a[j].size(),b[j].size());return false; }
    }
    return true;
}
static std::unique_ptr<Tree> izgradi(const std::vector<p07::Point>& p) {
    auto tree=std::make_unique<Tree>();
    if(!p.empty()) { auto pts=psi_podaci(p); izgradi_stablo(*tree,pts); }
    return tree;
}
static bool provjeri_ulaz(const std::vector<p07::Point>& p,const std::vector<p07::Rect>& q,size_t initial,bool mora_rasti=false) {
    auto tree=izgradi(p);
    const auto pripremljeni=pripremi_upite(q);
    Rezultati ref=brute(p,q),a(q.size()),b(q.size());
    Scratch scratch(p.size(),initial);
    obradi_original(*tree,q,pripremljeni,a); obradi_buffer(*tree,q,pripremljeni,scratch,b);
    if(mora_rasti && scratch.prosirenja==0) {std::fprintf(stderr,"GRESKA: test nije aktivirao rast buffera\n");return false;}
    if(!jednako_bez_redoslijeda(ref,a) || !jednako_bez_redoslijeda(ref,b)) return false;
    // Ponovna upotreba istog buffera i kapaciteta izlaza ne smije promijeniti ID-jeve.
    obradi_buffer(*tree,q,pripremljeni,scratch,b);
    return jednako_bez_redoslijeda(ref,b);
}
static bool rubni_testovi() {
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const double lo=.2,hi=.5,ispod=std::nextafter(lo,0.0),iznad=std::nextafter(hi,1.0);
    std::vector<p07::Point> p={{lo,.3,0},{hi,.3,1},{.3,lo,2},{.3,hi,3},
        {lo,lo,4},{hi,hi,5},{lo,hi,6},{hi,lo,7},{ispod,.3,8},{iznad,.3,9},
        {.3,ispod,10},{.3,iznad,11},{.3,.3,12},{.3,.3,13},{-.5,-.5,14}};
    const std::vector<p07::Rect> q={{lo,lo,hi,hi},{.3,.3,.3,.3},{-1,-1,1,1},
        {2,2,3,3},{1,0,0,1},{0,1,1,0},{nan,0,1,1}};
    if(!provjeri_ulaz(p,q,2,true) || !provjeri_ulaz({},q,2) || !provjeri_ulaz(p,{},2)) return false;
    // 64 pogodaka puni pocetni buffer tacno, iako stablo ima vise tacaka.
    // Sljedeci upit ima sve tacke te puni buffer do N; oba slucaja su provjerena.
    std::vector<p07::Point> many;
    for(int i=0;i<64;++i) many.push_back({.25,.25,1000+i});
    for(int i=0;i<67;++i) many.push_back({.75,.75,2000+i});
    if(!provjeri_ulaz(many,{{.25,.25,.25,.25},{0,0,1,1},{2,2,3,3}},64,true)) return false;
    if(!provjeri_ulaz({{.1,.1,4}},{{0,0,1,1}},1)) return false;
    return true;
}
template<typename T> static bool parse_integer(const char* s,T& out) {
    const char* end=s+std::strlen(s); auto r=std::from_chars(s,end,out);
    return r.ec==std::errc{} && r.ptr==end;
}
static void ispis(const char* naziv,const std::vector<double>& v,const Rezultati& r) {
    if(v.empty()) return;
    std::vector<double> sort=v;std::sort(sort.begin(),sort.end());
    size_t total=0;for(const auto& ids:r) total+=ids.size();
    std::printf("%s uzorci_ms=",naziv);
    for(double x:v) std::printf("%.6f,",x);
    const size_t mid=sort.size()/2;
    const double medijan=(sort.size()%2) ? sort[mid] : (sort[mid-1]/2.0+sort[mid]/2.0);
    std::printf(" medijan_ms=%.6f pogodaka=%zu checksum=%llu\n",medijan,total,(unsigned long long)p07::kontrolni_zbir(r));
}
int main(int argc,char** argv) {
    p07::Postavke settings;int repeats=7,build_repeats=7;size_t initial=64;std::string mode="compare";
    for(int i=1;i<argc;++i) {
        if(i+1>=argc) {std::fprintf(stderr,"Nedostaje vrijednost za %s\n",argv[i]);return 2;}
        std::string arg=argv[i];const char* value=argv[++i];bool ok=true;
        if(arg=="--mode") mode=value;else if(arg=="--dist") settings.dist=value;
        else if(arg=="--n") ok=parse_integer(value,settings.N);
        else if(arg=="--m") ok=parse_integer(value,settings.M);
        else if(arg=="--seed") ok=parse_integer(value,settings.seed);
        else if(arg=="--repeats") ok=parse_integer(value,repeats);
        else if(arg=="--build-repeats") ok=parse_integer(value,build_repeats);
        else if(arg=="--initial") ok=parse_integer(value,initial);
        else if(arg=="--w") {char* end=nullptr;settings.w=std::strtod(value,&end);ok=end!=value && *end=='\0';}
        else ok=false;
        if(!ok) {std::fprintf(stderr,"Neispravan argument: %s\n",arg.c_str());return 2;}
    }
    if((mode!="verify" && mode!="compare" && mode!="baseline" && mode!="optimized" &&
        mode!="cold-baseline" && mode!="cold-optimized") ||
       (settings.dist!="uniform" && settings.dist!="clustered") || settings.N<0 || settings.M<0 ||
       !std::isfinite(settings.w) || settings.w<0 || settings.w>1 || repeats<1 || repeats>1000 ||
       build_repeats<1 || build_repeats>1000 || initial==0) {
        std::fprintf(stderr,"Neispravne postavke\n");return 2;
    }
    const auto ukupno0=Sat::now();
    auto p0=Sat::now();std::vector<p07::Point> pts;std::vector<p07::Rect> q;
    p07::generisi(settings,pts,q);auto psi_pts=psi_podaci(pts);auto p1=Sat::now();
    const auto q0=Sat::now();const auto pripremljeni=pripremi_upite(q);const auto q1=Sat::now();
    if(mode=="cold-baseline" || mode=="cold-optimized") {
        // Zaseban proces, jedan build i jedan batch: nema zagrijavanja. Ovo ne
        // garantuje hladan OS/CPU cache niti ukljucuje ispis/checksum/destrukciju.
        const auto copy0=Sat::now();auto tree=std::make_unique<Tree>();
        Sat::time_point build0,build1;
        {
            // Izvorni psi_seq oslobadja privremenu kopiju prije faze upita.
            auto input=psi_pts;
            build0=Sat::now();izgradi_stablo(*tree,input);build1=Sat::now();
        }
        Rezultati out(q.size());Scratch scratch(mode=="cold-optimized"?pts.size():0,initial);
        const auto query0=Sat::now();
        if(mode=="cold-optimized") obradi_buffer(*tree,q,pripremljeni,scratch,out);
        else obradi_original(*tree,q,pripremljeni,out);
        const auto query1=Sat::now();
        std::printf("mode=%s N=%d M=%d w=%.6f dist=%s seed=%llu initial=%zu\n",
            mode.c_str(),settings.N,settings.M,settings.w,settings.dist.c_str(),settings.seed,initial);
        std::puts("NAPOMENA: zaseban proces, jedan build i batch bez zagrijavanja; OS/CPU cache nije ociscen.");
        std::printf("generisanje_i_psi_konverzija_ms=%.6f\n",ms(p0,p1));
        std::printf("priprema_kutija_i_validnosti_ms=%.6f\n",ms(q0,q1));
        std::printf("kreiranje_stabla_i_kopija_ms=%.6f\n",ms(copy0,build0));
        std::printf("gradnja_stabla_ms=%.6f\n",ms(build0,build1));
        std::printf("oslobadjanje_kopije_i_priprema_izlaza_buffera_ms=%.6f\n",ms(build1,query0));
        std::printf("grupa_upita_ms=%.6f\n",ms(query0,query1));
        std::printf("zidni_od_generisanja_do_kraja_grupe_ms=%.6f\n",ms(ukupno0,query1));
        std::vector<double> uzorak{ms(query0,query1)};
        ispis(mode=="cold-optimized"?"buffer":"original",uzorak,out);
        if(mode=="cold-optimized") std::printf("buffer_velicina_tacaka=%zu buffer_logicki_bajtovi=%zu prosirenja=%zu ponovnih_prolaza=%zu\n",
            scratch.buf.size(),scratch.buf.size()*sizeof(PsiPoint),scratch.prosirenja,scratch.ponovljenih_upita);
        return 0;
    }
    std::printf("mode=%s N=%d M=%d w=%.6f dist=%s seed=%llu repeats=%d build_repeats=%d initial=%zu\n",
        mode.c_str(),settings.N,settings.M,settings.w,settings.dist.c_str(),settings.seed,repeats,build_repeats,initial);
    std::printf("generisanje_i_psi_konverzija_ms=%.6f\n",ms(p0,p1));
    std::printf("priprema_kutija_i_validnosti_ms=%.6f\n",ms(q0,q1));
    if(mode=="verify") {
        if(!rubni_testovi() || !provjeri_ulaz(pts,q,initial)) return 1;
        std::puts("PROSAO: rubni slucajevi, rast buffera i svi sortirani ID-jevi odgovaraju brute-force referenci.");return 0;
    }
    std::unique_ptr<Tree> tree;std::vector<double> builds;
    for(int rep=0;rep<=build_repeats;++rep) {
        tree=std::make_unique<Tree>();auto input=psi_pts;
        auto b0=Sat::now();izgradi_stablo(*tree,input);auto b1=Sat::now();
        if(rep>0) builds.push_back(ms(b0,b1));
    }
    const bool use_a=(mode=="compare" || mode=="baseline");
    const bool use_b=(mode=="compare" || mode=="optimized");
    Rezultati a,b;if(use_a)a.resize(q.size());if(use_b)b.resize(q.size());
    const auto s0=Sat::now();Scratch scratch(use_b?pts.size():0,initial);const auto s1=Sat::now();
    if(use_b) std::printf("inicijalni_scratch_ms=%.6f\n",ms(s0,s1));
    std::vector<double> ta,tb;
    // 'Prvi' nije pravi cold-start: original ide prvi, a OS/CPU cache nije
    // flushiran. Pocetna Scratch alokacija je izvan mjerenja prvog upita.
    std::puts("NAPOMENA: prvi upit i prva grupa nisu pravi cold-start niti uravnotezeno poredjenje.");
    if(!q.empty()) {
        std::vector<int> ids;Scratch first(use_b?pts.size():0,initial);
        if(use_a) {auto x=Sat::now();original_upit(*tree,q[0],pripremljeni.kutije[0],pripremljeni.ispravni[0]!=0,ids);auto y=Sat::now();std::printf("prvi_upit_original_ms=%.6f\n",ms(x,y));}
        if(use_b) {ids.clear();ids.shrink_to_fit();auto x=Sat::now();buffer_upit(*tree,q[0],pripremljeni.kutije[0],pripremljeni.ispravni[0]!=0,first,ids);auto y=Sat::now();std::printf("prvi_upit_buffer_ms=%.6f\n",ms(x,y));}
    } else std::puts("prvi_upit=nema_upita");
    if(use_a) {auto x=Sat::now();obradi_original(*tree,q,pripremljeni,a);auto y=Sat::now();std::printf("prva_grupa_original_ms=%.6f\n",ms(x,y));}
    if(use_b) {auto x=Sat::now();obradi_buffer(*tree,q,pripremljeni,scratch,b);auto y=Sat::now();std::printf("prva_grupa_buffer_ms=%.6f\n",ms(x,y));}
    auto run=[&](bool opt) {
        auto x=Sat::now();if(opt) obradi_buffer(*tree,q,pripremljeni,scratch,b);else obradi_original(*tree,q,pripremljeni,a);auto y=Sat::now();
        (opt?tb:ta).push_back(ms(x,y));
    };
    for(int rep=0;rep<repeats;++rep) {
        if(use_a && use_b) {if(rep%2==0){run(false);run(true);}else{run(true);run(false);}}
        else run(use_b);
    }
    if(use_a && use_b && !jednako_bez_redoslijeda(a,b)) return 1;
    Rezultati prazno;ispis("gradnja_stabla",builds,prazno);
    ispis("original",ta,a);ispis("buffer",tb,b);
    // Ove vrijednosti su velicina elemenata i logicki bajtovi, ne vrh RSS-a.
    if(use_b) std::printf("buffer_velicina_tacaka=%zu buffer_logicki_bajtovi=%zu prosirenja=%zu ponovnih_prolaza=%zu\n",
        scratch.buf.size(),scratch.buf.size()*sizeof(PsiPoint),scratch.prosirenja,scratch.ponovljenih_upita);
    if(use_a && use_b) std::puts("PROSAO: sve liste ID-jeva originala i buffera identicne su nakon sortiranja.");
    return 0;
}
