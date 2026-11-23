#!/usr/bin/env python3
"""Reproducibly build the sequential optimization report from archived measurements."""
from pathlib import Path
import hashlib
import json
import math
import csv
import re
import statistics
import textwrap
from xml.sax.saxutils import escape

from PIL import Image as PILImage, ImageDraw, ImageFont
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (Image, KeepTogether, PageBreak, Paragraph, Preformatted,
                                 SimpleDocTemplate, Spacer, Table, TableStyle)

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
RAW = HERE / 'sirova_mjerenja' / '08_sekvencijalno'
INTEGRATED = RAW / 'integrisano'
FIG = HERE / 'grafikoni'
DATA = HERE / 'podaci_izvjestaja'
FIG.mkdir(exist_ok=True)
DATA.mkdir(exist_ok=True)

regular = r'C:\Windows\Fonts\arial.ttf'
bold = r'C:\Windows\Fonts\arialbd.ttf'
mono = r'C:\Windows\Fonts\consola.ttf'
for name, path in [('Arial', regular), ('ArialBold', bold), ('Consolas', mono)]:
    pdfmetrics.registerFont(TTFont(name, path))

tags = [
    ('uniform_0.005', 'Uniformno / 0,005', 200000, 2000),
    ('uniform_0.02', 'Uniformno / 0,02', 200000, 2000),
    ('uniform_0.1', 'Uniformno / 0,1', 200000, 2000),
    ('clustered_0.005', 'Grupisano / 0,005', 200000, 2000),
    ('clustered_0.02', 'Grupisano / 0,02', 200000, 2000),
    ('clustered_0.1', 'Grupisano / 0,1', 200000, 2000),
    ('malo', 'Mali / 0,02', 20000, 200),
    ('veliko', 'Veliki / 0,02', 1000000, 2000),
]
def first(pattern, text):
    found = re.search(pattern, text, re.MULTILINE)
    if not found:
        raise ValueError('U rezultatu nedostaje ' + pattern)
    return float(found.group(1))

data = []
for tag, label, n, m in tags:
    d = {'tag':tag, 'label':label, 'n':n, 'm':m}
    for variant in ('psi_seq', 'psi_seq_opt'):
        logs = [(INTEGRATED / f'{tag}_{variant}_{i}.txt').read_text(encoding='utf-8')
                for i in range(1, 6)]
        q = [first(r'Svi upiti:\s*([0-9.]+)', t) for t in logs]
        b = [first(r'Izgradnja stabla:\s*([0-9.]+)', t) for t in logs]
        verify = (INTEGRATED / f'{tag}_{variant}_verify.txt').read_text(encoding='utf-8')
        if not re.search(r'OK: svih \d+ upita daje iste id-jeve', verify):
            raise ValueError('Provjera nije potvrđena: ' + tag + '/' + variant)
        d[variant] = {'query_ms':statistics.median(q), 'min_ms':min(q), 'max_ms':max(q),
                      'build_ms':statistics.median(b), 'samples_ms':q}
    d['speedup'] = d['psi_seq']['query_ms'] / d['psi_seq_opt']['query_ms']
    data.append(d)

ctest = (INTEGRATED / 'ctest.txt').read_text(encoding='utf-8')
assert '100% tests passed, 0 tests failed out of 4' in ctest

v5data = []
for tag, label, n, m in tags:
    t = (RAW / f'v5_{tag}_compare.txt').read_text(encoding='utf-8')
    a = first(r'^v4 uzorci_ms=.*?medijan_ms=([0-9.]+)', t)
    b = first(r'^v5 uzorci_ms=.*?medijan_ms=([0-9.]+)', t)
    verify = (RAW / f'v5_{tag}_verify.txt').read_text(encoding='utf-8')
    v5data.append({'tag':tag, 'label':label, 'v4_ms':a, 'v5_ms':b})

def cold(tag, mode):
    result = []
    for i in range(1,8):
        text = (RAW/'psi_uporedivo'/f'{tag}_cold_{mode}_{i}.txt').read_text(encoding='utf-8')
        result.append(first(r'zidni_od_generisanja_do_kraja_grupe_ms=([0-9.]+)', text))
    return statistics.median(result)

cold_example = {v:cold('uniform_0.02',v) for v in ('baseline','optimized')}
memroot = RAW/'psi_integrisana_memorija'
memory=[]
for width in ('0.02','0.1'):
    entry={'width':width}
    for variant in ('psi_seq','psi_seq_opt'):
        massif=(memroot/f'massif_{variant}_{width}.out').read_text(encoding='utf-8')
        blocks=re.split(r'(?m)^snapshot=\d+\s*$',massif)
        peak=[b for b in blocks if 'heap_tree=peak' in b]
        if len(peak)!=1:
            raise ValueError('Nedostaje jedinstven Massif vrh')
        heap=first(r'mem_heap_B=(\d+)',peak[0])+first(r'mem_heap_extra_B=(\d+)',peak[0])
        rss=[]
        for i in range(1,4):
            txt=(memroot/f'rss_{variant}_{width}_{i}.txt').read_text(encoding='utf-8')
            rss.append(first(r'Maximum resident set size \(kbytes\):\s*(\d+)',txt))
        entry[variant]={'heap_plus_overhead_bytes':int(heap), 'rss_kib':statistics.median(rss), 'rss_samples_kib':rss}
    memory.append(entry)
vtuneroot = RAW/'vtune_windows_finalni_ponovo'
def vtune_summary(variant):
    with (vtuneroot/f'{variant}_summary.csv').open(encoding='utf-8-sig',newline='') as f:
        rows=list(csv.DictReader(f))
    out={}
    for key in ('Elapsed Time','Instructions Retired','CPI Rate','MUX Reliability'):
        vals=[float(r['Metric Value']) for r in rows if r['Metric Name']==key and r['Metric Value']]
        if not vals: raise ValueError('Nedostaje VTune metrika '+key)
        out[key]=vals[0]
    is_pcore=False
    for row in rows:
        lvl=int(row['Hierarchy Level']); name=row['Metric Name']; value=row['Metric Value']
        if lvl==1:
            is_pcore=(name=='Performance-core (P-core)')
        if is_pcore and name in ('Retiring','Front-End Bound','Bad Speculation','Back-End Bound','Memory Bound') and value:
            out[name]=float(value)
    assert len(out)==9, out
    return out
vtune={v:vtune_summary(v) for v in ('baseline','optimized')}
sources = [ROOT/'src'/'psi_benchmark.cpp', ROOT/'src'/'psi_seq_buffer.h']
hashes = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
summary = {'psi':data,'brute_v4_v5':v5data,'cold_experiment':cold_example,'memory':memory,'vtune':vtune,'source_sha256':hashes}
(DATA/'integrisani_rezultati.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')

def chart(name, title, subtitle, rows, fmt, unit):
    w, h = 1600, 260 + 100*len(rows)
    im = PILImage.new('RGB', (w,h), 'white')
    dr = ImageDraw.Draw(im)
    F = ImageFont.truetype(regular, 26)
    Fsmall = ImageFont.truetype(regular, 22)
    FB = ImageFont.truetype(bold, 31)
    dr.text((40,25), title, font=FB, fill='#17365d')
    dr.text((40,70), subtitle, font=Fsmall, fill='#506174')
    x0,x1 = 425, 1300
    maxval=max(max(a,b) for _,a,b in rows)*1.2
    dr.text((425,113),'Referenca',font=Fsmall,fill='#315a87')
    dr.rectangle((555,117,582,133),fill='#315a87')
    dr.text((610,113),'Optimizovano',font=Fsmall,fill='#00978b')
    dr.rectangle((760,117,787,133),fill='#00978b')
    for i,(label,a,b) in enumerate(rows):
        y=165+i*100
        dr.text((40,y+15),label,font=F,fill='#25384a')
        for offset,val,col in [(0,a,'#315a87'),(32,b,'#00978b')]:
            end=x0+int((x1-x0)*val/maxval)
            dr.rectangle((x0,y+offset,end,y+offset+24),fill=col)
            dr.text((end+10,y+offset-2),fmt(val),font=Fsmall,fill='#25384a')
    dr.text((x0,h-42),unit,font=Fsmall,fill='#506174')
    dest=FIG/name
    im.save(dest)
    return dest

psi_fig = chart('01_psi_vrijeme.png','PSI: zagrijana obrada upita',
    'Medijan 5 procesa; svaki proces medijan 7 ponavljanja; manje je bolje.',
    [(d['label'],d['psi_seq']['query_ms'],d['psi_seq_opt']['query_ms']) for d in data],
    lambda x:f'{x:.2f} ms','Vremena uključuju obradu svih upita, ne izgradnju stabla.')
brute_fig=chart('02_brute_v4_v5.png','Brute-force: V4 i pokušaj V5',
    'Odvojeni eksperiment, isti ulaz po paru; manje je bolje.',
    [(d['label'],d['v4_ms'],d['v5_ms']) for d in v5data],
    lambda x:f'{x:.1f} ms','Referenca = V4; optimizovano = eksperimentalna V5.')
old_fig=chart('03_brute_verzije.png','Prethodne brute-force verzije',
    'Ranija serija: 200.000 tačaka / 2.000 upita, uniformno, w=0,02.',
    [('Referenca / V1',1084.58,964.96),('V2 / V3',907.80,952.77),('Referenca / V4',1084.58,121.14)],
    lambda x:f'{x:.2f} ms','Plavo / zeleno predstavljaju imenovane verzije u svakoj vrsti.')

# Jedna boja po verziji: ove vrijednosti potječu iz prethodne dokumentovane
# serije (07), a ne iz novog PSI eksperimenta. Serije različitih mjerača ne
# spajaju se u zajednički benchmark.
versions = ['Ref.', 'V1', 'V2', 'V3', 'V4']
palette = ['#315a87', '#6a80a3', '#a46c9a', '#e69a45', '#00978b']
old_median = [1084.58, 964.96, 907.80, 952.77, 121.14]
old_ranges = [(1052.3,1106.8),(946.1,1039.6),(872.3,962.5),(920.1,1008.4),(108.7,137.9)]
old_metrics = [
 ('11_vrijeme.png','1. Vrijeme obrade upita (medijan)',old_median,'ms', 'Manje je bolje.'),
 ('12_raspon.png','2. Raspon izmjerenih vremena',[b-a for a,b in old_ranges],'ms', 'Širina min-max; nije interval pouzdanosti.'),
 ('13_ubrzanje.png','3. Ubrzanje prema referenci',[old_median[0]/x for x in old_median],'x', 'Izračunato iz medijana; više je bolje.'),
 ('14_propusnost.png','4. Propusnost',[2000*1000/x for x in old_median],'upita/s', 'Izvedeno iz istih medijana, nije nezavisno mjerenje.'),
 ('15_heap.png','5. Vršni živi heap i overhead',[4.76,4.00,4.00,5.52,4.00],'MiB', 'Massif, M=200; jedno pokretanje po verziji.'),
 ('16_rss.png','6. Maksimalni rezidentni skup',[8.12,7.25,7.38,7.38,7.38],'MiB', '/usr/bin/time, M=200; odvojeno mjerenje.'),
 ('17_cpi.png','7. CPI - Windows P-core',[1.037,.954,1.469,1.119,.235],'ciklusa / instrukciji', 'Profilna mjerenja imaju različit broj ponavljanja.'),
 ('18_retiring.png','8. Retiring - Windows P-core',[16.5,19.1,12.0,16.4,82.3],'%', 'P-core procjena; uslovi profila nisu identični.'),
 ('19_backend.png','9. Back-End Bound - Windows P-core',[7.0,7.4,7.9,8.7,21.4],'%', 'P-core procjena; nije direktno poređenje uzroka.'),
 ('20_memory.png','10. Memory Bound - Windows P-core',[.9,1.0,.4,1.5,5.9],'%', 'Podmetrika Back-End Bound; ne sabirati.'),
]

def version_chart(name,title,values,unit,note):
    im=PILImage.new('RGB',(1600,550),'white'); d=ImageDraw.Draw(im)
    font=ImageFont.truetype(regular,25); small=ImageFont.truetype(regular,21)
    header=ImageFont.truetype(bold,32)
    d.text((40,24),title,font=header,fill='#17365d')
    d.text((40,70),note,font=small,fill='#53677b')
    xmax=max(values)*1.19 or 1
    for i,(name_,v,color) in enumerate(zip(versions,values,palette)):
        y=126+i*68; end=230+int(1030*v/xmax)
        d.text((42,y+6),name_,font=font,fill='#24394b')
        d.rectangle((230,y,end,y+32),fill=color)
        number=f'{v:,.2f}' if max(values)>10 else f'{v:.3f}'
        if name=='12_raspon.png':
            number=f'{old_ranges[i][0]:.1f}–{old_ranges[i][1]:.1f}'
        d.text((end+10,y+3),f'{number} {unit}',font=small,fill='#24394b')
    if name=='12_raspon.png':
        d.text((40,500),'Dužina stupca = maksimum - minimum; natpis = stvarni min-max.',font=small,fill='#53677b')
    im.save(FIG/name)

for _name,_title,_values,_unit,_note in old_metrics:
    version_chart(_name,_title,_values,_unit,_note)

def query_diagram():
    im=PILImage.new('RGB',(1600,430),'white'); d=ImageDraw.Draw(im)
    header=ImageFont.truetype(bold,32); title=ImageFont.truetype(bold,25)
    body=ImageFont.truetype(regular,23)
    d.text((40,15),'Kako nastaje rezultat prostornog upita',font=header,fill='#17365d')
    boxes=[(45,125,345,295,'1  ULAZ','Tačke (x, y, ID)','i pravougaonik'),
           (435,125,735,295,'2  PSI KD-STABLO','Odabir kandidata','pomoću indeksa'),
           (825,125,1125,295,'3  TAČAN FILTER','Provjera granica','p07::unutra'),
           (1215,125,1515,295,'4  IZLAZ','ID-jevi tačaka','unutar granica')]
    for x0,y0,x1,y1,heading,a,b in boxes:
        d.rounded_rectangle((x0,y0,x1,y1),radius=16,fill='#edf4f8',outline='#9bb5ca',width=2)
        d.text((x0+15,y0+22),heading,font=title,fill='#17365d')
        d.text((x0+15,y0+69),a,font=body,fill='#24394b')
        d.text((x0+15,y0+105),b,font=body,fill='#24394b')
    for x in (360,750,1140):
        d.line((x,210,x+48,210),fill='#00978b',width=6)
        d.polygon([(x+48,198),(x+67,210),(x+48,222)],fill='#00978b')
    d.text((45,338),'Nova verzija ponovo koristi bafer u koraku 2; funkcionalni rezultat ostaje isti.',font=body,fill='#53677b')
    im.save(FIG/'00_tok_upita.png')
query_diagram()

def scenarios_diagram():
    matrix=[
      ('Uniformno 0,005',[981.6,881.0,814.1,871.7,118.5]),
      ('Uniformno 0,02',[1084.6,965.0,907.8,952.8,121.1]),
      ('Uniformno 0,1',[1308.3,1200.9,1177.6,1189.2,172.5]),
      ('Grupisano 0,005',[914.5,827.0,756.9,792.7,123.4]),
      ('Grupisano 0,02',[997.9,893.4,805.8,866.0,114.9]),
      ('Grupisano 0,1',[1162.8,1057.7,998.4,1071.4,161.6]),
      ('N=20.000, M=200',[9.7,8.7,8.4,8.6,.9]),
      ('N=1.000.000, M=2.000',[6279.3,5220.3,4844.3,5091.0,1077.7]),
    ]
    im=PILImage.new('RGB',(1600,770),'white'); d=ImageDraw.Draw(im)
    font=ImageFont.truetype(regular,25); boldfont=ImageFont.truetype(bold,26)
    title=ImageFont.truetype(bold,32); small=ImageFont.truetype(regular,21)
    d.text((35,20),'Ranije brute-force verzije kroz osam ulaza',font=title,fill='#17365d')
    d.text((35,68),'Medijani u ms; intenzitet je relativan samo unutar jednog reda.',font=small,fill='#53677b')
    x0=430; cw=210
    for j,v in enumerate(versions): d.text((x0+j*cw+35,115),v,font=boldfont,fill=palette[j])
    for i,(label,vals) in enumerate(matrix):
        y=160+60*i; d.text((35,y+10),label,font=font,fill='#24394b')
        lo=min(vals); hi=max(vals)
        for j,v in enumerate(vals):
            t=(v-lo)/(hi-lo) if hi>lo else 0
            c=(int(220+30*(1-t)),int(244-25*t),int(241-35*t))
            d.rounded_rectangle((x0+j*cw,y,x0+(j+1)*cw-6,y+48),radius=5,fill=c)
            d.text((x0+j*cw+17,y+10),f'{v:,.1f}',font=font,fill='#24394b')
    d.text((35,690),'Jedno zagrijavanje, sedam uzoraka; N/M su navedeni u izvještaju.',font=small,fill='#53677b')
    im.save(FIG/'21_brute_scenariji.png')
scenarios_diagram()
chart('04_heap.png','Vršni živi heap s dodatnim prostorom alokatora',
    'Massif, po jedno pokretanje za svaku verziju i širinu.',
    [(f"Uniformno / {x['width']}",x['psi_seq']['heap_plus_overhead_bytes']/1048576,
      x['psi_seq_opt']['heap_plus_overhead_bytes']/1048576) for x in memory],
    lambda x:f'{x:.2f} MiB','Heap ne uključuje stek. Jedno mjerenje nije statistička procjena.')
chart('05_rss.png','Vršni rezidentni skup procesa',
    '/usr/bin/time, medijan tri nezavisna procesa po varijanti.',
    [(f"Uniformno / {x['width']}",x['psi_seq']['rss_kib']/1024,
      x['psi_seq_opt']['rss_kib']/1024) for x in memory],
    lambda x:f'{x:.2f} MiB','RSS i Massif heap prikazuju različite veličine; ne sabirati ih.')
for filename,title,key,scale,fmt,unit in [
    ('06_vtune_proteklo.png','VTune: proteklo vrijeme cijelog rezultata','Elapsed Time',1,lambda x:f'{x:.3f} s','Sekunde; profilni build i pokretanje preko cmd.exe.'),
    ('07_vtune_instrukcije.png','VTune: završene instrukcije','Instructions Retired',1e-9,lambda x:f'{x:.2f} milijardi','Milijarde instrukcija; odnosi se na cijeli rezultat.'),
    ('08_vtune_cpi.png','VTune: CPI','CPI Rate',1,lambda x:f'{x:.3f}','Ciklusi po završenoj instrukciji; manji CPI sam ne dokazuje brži program.'),
    ('09_vtune_mux.png','VTune: pouzdanost multipleksiranja','MUX Reliability',1,lambda x:f'{x:.3f}','Odnos pouzdanosti za čitav rezultat; veće je u pravilu bolje.')]:
    chart(filename,title,'Jedno mjerenje po verziji; Windows profilni build.',
          [('PSI',vtune['baseline'][key]*scale,vtune['optimized'][key]*scale)],fmt,unit)
chart('10_vtune_pcore.png','VTune: P-core pokazatelji',
    'Procjene pipeline slotova; jedno mjerenje po verziji.',
    [(key,vtune['baseline'][key],vtune['optimized'][key]) for key in
     ('Retiring','Front-End Bound','Bad Speculation','Back-End Bound','Memory Bound')],
    lambda x:f'{x:.1f}%','Memory Bound je podmetrika Back-End Bound; ne sabirati ih.')

main = data[1]
percentage=(1-main['psi_seq_opt']['query_ms']/main['psi_seq']['query_ms'])*100
md=[]
def M(s=''): md.append(s)
M('# Sekvencijalna optimizacija prostornih upita')
M('\n**Stanje mjerenja:** 22. 11. 2026. Cilj ove faze je ispravna i brža sekvencijalna obrada. Paralelizacija ostaje za kasnije.\n')
M('## 1. Nalaz i preporuka\n')
M('Početni VTune Hotspots odnosio se na PSI KD-stablo, dok su V1-V4 posebni brute-force eksperimenti. Zbog toga ih treba objašnjavati odvojeno: prethodno ubrzanje V4 ne znači da je isto poboljšano projektno PSI stablo. U ovoj fazi izrađen je zaseban CMake cilj `psi_seq_opt`; `psi_seq` je sačuvan kao referenca. Oba rade jednom niti.\n')
M(f"Za glavni ulaz N=200.000, M=2.000, uniformno, w=0,02, medijan obrade upita bio je **{main['psi_seq']['query_ms']:.2f} ms** za `psi_seq` i **{main['psi_seq_opt']['query_ms']:.2f} ms** za `psi_seq_opt`: {main['speedup']:.2f}×, odnosno {percentage:.1f}% manje vremena upita. Ovi brojevi se odnose na zagrijane upite, ne na ukupno pokretanje. Medijan izgradnje stabla bio je {main['psi_seq']['build_ms']:.2f} ms i {main['psi_seq_opt']['build_ms']:.2f} ms.\n")
M('**Preporuka:** koristiti `psi_seq_opt` za nastavak mjerenja PSI sekvencijalnog programa i zadržati `psi_seq` kao kontrolu. Brute-force V4 ostaje zasebna poredbena varijanta; eksperimentalna V5 nije opće poboljšanje. Nije dokazano da je bilo koja verzija globalno optimalna za sve ulaze i mašine.\n')
M('## 2. Šta je urađeno i šta je tražila grupa\n')
M('Prostorni upit traži sve tačke unutar pravougaonika: xmin ≤ x ≤ xmax i ymin ≤ y ≤ ymax. Granice su uključene, a tačke s istim koordinatama i različitim ID-jevima ostaju različiti rezultati. Brute-force provjerava svaku od N tačaka za svaki od M upita, što daje O(N × M) provjera. PSI koristi KD-stablo koje može preskočiti dijelove prostora, uz dodatni trošak izgradnje indeksa. [5]\n')
M('![Tok prostornog upita](grafikoni/00_tok_upita.png)\n')
M('Dijagram prikazuje odnos cijelog zadatka i mjesta optimizacije. Pravougaonici se najprije pretvaraju u PSI format; stablo pronalazi kandidate, a završni uslov p07::unutra vraća ID-jeve koji zaista zadovoljavaju tačne granice.\n')
M('Grupa je tražila identificiranje funkcija koje troše najviše vremena, pokušaj odmotavanja petlje, poboljšavanje najskupljeg dijela, podjelu rada na jasne funkcije i simbole za VTune/PDB. Ranije su urađeni VTune Hotspots, razdvojene mjerne funkcije i verzije V1-V4, uključujući odmotavanje i AVX2. Sada je dodatno analiziran i unaprijeđen stvarni sekvencijalni PSI put. Windows Microarchitecture i PDB treba tumačiti prema konkretno potvrđenom rezultatu profiliranja, a ne zaključivati samo iz postojanja PDB datoteke.\n')
M('Tok rada: (1) početno profiliranje sekvencijalnog PSI stabla, (2) razvoj i provjera brute-force verzija, (3) PDB i hardverski VTune na Windowsu, (4) ispitivanje nove ideje V5 i (5) posebna optimizacija PSI pretrage pomoću bafera. Početni WSL Hotspots za PSI imao je N=200.000, M=20.000, 20 ponavljanja i --no-check; prikazano vrijeme 1,612 s opisuje taj konkretan profil, ne V4 i ne potvrđuje ispravnost. Windows matrix.exe bio je samo provjera profilera.\n')
M('Referentni `tree.range_query(box)` u PSI biblioteci najprije određuje veličinu rezultata, a zatim alocira i ponovo obilazi stablo. Nova varijanta koristi PSI preopterećenje koje upisuje u prethodno pripremljen bafer. Taj se bafer zadržava između upita i ponavljanja. Ako se napuni, proširuje se i cijeli upit ponavlja dok nema dovoljno mjesta, ili kapacitet ne dosegne broj ulaznih tačaka. Ne prihvata se tiho skraćen rezultat. Zadržan je `p07::unutra` jer PSI može koristiti toleranciju granica.\n')
M('Ova optimizacija ima cijenu: privremeni bafer može narasti do N tačaka, a upit se ponavlja pri proširenju. Valjanost pravougaonika unaprijed se određuje samo u novoj varijanti, izvan mjerenog dijela upita; to je dio implementacije i bitno za pošteno tumačenje vremena. Izvorna biblioteka i postojeći cilj `psi_seq` nisu zamijenjeni.\n')
M('## 3. Ispravnost\n')
M('CTest: **4/4 prolaze**. Izvorni testni program prijavio je 23 prolaza; dodatni test obuhvata rubove, duplikate, prazan ulaz i puni odnosno nedovoljni bafer, a još dva testa pokreću novi CLI uključujući N=0/M=0. Za svih osam scenarija obje verzije dodatno su zasebno pokrenute bez `--no-check`: rezultat svakog upita poređen je s brute-forceom kao sortirani niz ID-jeva. U svih 16 provjera ispisan je `OK`. Sortiranje je potrebno jer redoslijed KD-stabla može biti drugačiji. To je dokaz za ispitane ulaze, ne matematička garancija za sve moguće podatke. [1]\n')
M('U zasebnom eksperimentu V1-V4 su provjereni puni nizovi ID-jeva uključujući njihov redoslijed, rubne i neparne ulaze. Build s P07_DISABLE_AVX2 provjeren je na manjim uniformnim i grupisanim ulazima, čime je ispitan i skalarni povratni put. PSI poređenje se razlikuje jer redoslijed nije definisan jednako kao kod brute-forcea. Konačni bafer je ispitan i za tačno popunjavanje te ponovno proširivanje. [3]\n')
M('Nakon izmjena su uspješno kompajlirani i već postojeći ciljevi `psi_omp` i `psi_parlay`, koji dijele istu izvornu datoteku. To je regresijska provjera kompilacije, a ne nova paralelizacija niti mjerenje njihovih performansi.\n')
M('## 4. Poređenje stvarnog PSI programa\n')
M('Obje verzije izgrađene su u istom WSL Release okruženju s G++ 14 i istim ulazima/seed 42. Za svaki scenario pokrenuto je pet nezavisnih procesa po verziji, naizmjeničnim redoslijedom. Svaki proces prijavio je medijan sedam mjerenja nakon zagrijavanja. Tabela prikazuje medijan pet tih vrijednosti; opseg je najmanji i najveći od pet zaokruženih procesnih medijana, ne interval pouzdanosti. U pripremu i cijelo trajanje procesa ovdje se ne ulazi. [1]\n')
M('Mjerenja iz ranije projektne dokumentacije rađena na AMD Ryzen 5 7530U ne porede se direktno s ovdje prikazanim mjerenjima na Intel računaru: različiti procesor i okruženje mijenjaju rezultate.\n')
M('| Raspodjela / w | N / M | Referenca upiti ms (raspon) | Opt. upiti ms (raspon) | Ubrzanje |')
M('| --- | --- | ---: | ---: | ---: |')
for d in data:
    a,b=d['psi_seq'],d['psi_seq_opt']
    M(f"| {d['label']} | {d['n']:,} / {d['m']:,} | {a['query_ms']:.2f} ({a['min_ms']:.2f}-{a['max_ms']:.2f}) | {b['query_ms']:.2f} ({b['min_ms']:.2f}-{b['max_ms']:.2f}) | {d['speedup']:.2f}× |")
M('\n![Poređenje PSI upita](grafikoni/01_psi_vrijeme.png)\n')
M('U svih osam izmjerenih slučajeva novi cilj je brži u mjerenoj fazi upita. Širina i raspodjela mijenjaju broj pogodaka, a samim tim i trošak rada i upisa rezultata. Mali slučaj je naročito osjetljiv na zaokruživanje jer program ispisuje vremena na 0,01 ms.\n')
M('Izgradnja se mjeri zasebno nakon pravljenja kopije podataka; ispis programa „ukupno“ je zbir medijana faza, a ne nužno stvarno izmjereno vrijeme jednog potpunog pokretanja. Zbog toga iz omjera upita ne slijedi isti omjer kompletnog programa.\n')
M(f"Za dodatni uvid provedeno je drugo mjerenje u uporedivom eksperimentalnom programu: sedam zasebnih procesa s jednom gradnjom i jednim skupom upita bez zagrijavanja aplikacije. Za uniformno w=0,02 medijani od početka generisanja do kraja upita iznose {cold_example['baseline']:.2f} ms i {cold_example['optimized']:.2f} ms. OS/CPU cache nije čišćen, završavanje procesa nije uključeno, a ovo je eksperimentalni program - ne zamjenjuje rezultat integrisanih ciljeva. [2]\n")
M('## 5. Šta znače prethodne verzije brute-forcea\n')
M('| Verzija | Glavna ideja |')
M('| --- | --- |\n| Referenca | Skalarni pregled svih tačaka, raspored AoS. |\n| V1 | Odvojeni nizovi x, y i ID (SoA); skalarni pregled. |\n| V2 | SoA i ručno odmotana petlja po četiri tačke. |\n| V3 | Najprije se izdvajaju kandidati po x, zatim provjerava y. |\n| V4 | SoA i AVX2 za grupe od četiri `double`, uz skalarni povratni put. |\n| V5 eksperiment | Najprije maska po x; y se preskače kad nijedna x koordinata ne odgovara. |\n')
M('Ranija serija [3] na glavnom ulazu dala je medijane reference/V1/V2/V3/V4: 1084,58 / 964,96 / 907,80 / 952,77 / 121,14 ms za zagrijane upite. V4 je bila 8,95× brža od brute-force reference u toj seriji. To nije mjerenje PSI KD-stabla niti ukupno vrijeme rada.\n')
M('![Ranije verzije](grafikoni/03_brute_verzije.png)\n')
M('U novoj zasebnoj seriji provjeren je pokušaj V5. Na uskim pravougaonicima može pomoći, ali na širim može biti sporiji jer dodatno grananje i maske nose trošak. Zato V5 nije proglašena općom zamjenom za V4. Detaljni uzorci i testovi sačuvani su u projektnim datotekama.\n')
M('![V4 i V5](grafikoni/02_brute_v4_v5.png)\n')
M('### Kako se verzije razlikuju tokom izvršavanja\n')
M('Referenca pregledava niz struktura (AoS) i za svaku tačku skalarno provjerava obje koordinate. V1 prebacuje x, y i ID u odvojene nizove (SoA), ali ih i dalje provjerava skalarno. V2 ručno obrađuje četiri tačke po iteraciji i ima poseban ostatak; odmotavanje samo po sebi ne znači paralelni rad niti.\n')
M('V3 u jednom prolazu izdvaja x-kandidate, pa nad njima provjerava y. Time štedi neke provjere, ali plaća dodatnim prolazom i privremenim indeksom; ona je alternativa, a ne nadogradnja V2. V4 obrađuje po četiri double vrijednosti pomoću AVX2 i maske, uz skalarni ostatak i povratni put ako AVX2 nije dostupan. SIMD ne mijenja činjenicu da je to ovdje sekvencijalan program u jednoj niti.\n')
M('Za poređenje V1-V4 korišten je zajednički ulaz u istoj seriji; priprema AoS i SoA reprezentacija obuhvaćena je zajedno i ne može se dijeliti po verzijama. Zagrijana obrada ponovo koristi kapacitet izlaznih vektora. Ovi brojevi i nova mjerenja PSI stabla opisuju različite algoritme i ne smiju se predstaviti kao vremenski niz jedne implementacije.\n')
M('### Deset metrika ranijih brute-force verzija\n')
M('Vrijeme je mjereno u WSL/G++14 na N=200.000, M=2.000, uniformno, w=0,02, seed 42: zagrijavanje pa sedam ponavljanja uz rotaciju redoslijeda. Memorija je snimljena zasebno s M=200; Windows VTune/Clang hardverski podaci su opet zasebna mjerenja. U pojedinim Windows profilima reference i V1-V3 izvršeno je šest, a V4 80 ponavljanja. Zbog različitog trajanja, migracije P/E jezgri i MUX-a one su indikativne, ne strogo kontrolisano poređenje. [3]\n')
old_explanations = [
 'Medijan je srednja vrijednost sedam sortiranih uzoraka. V4 ovdje obrađuje grupu upita za 121,14 ms, dok referenca treba 1.084,58 ms. Priprema nije uključena.',
 'Za svaku verziju prikazani su najmanje i najveće izmjereno vrijeme; dužina stupca je razlika između njih. Sedam uzoraka ne određuje interval pouzdanosti niti garantuje buduću stabilnost.',
 'Ubrzanje = medijan reference / medijan verzije. V4 postiže 8,95 puta na istom ulazu i u istoj mjerenoj fazi; ovo nije ubrzanje čitavog pokretanja niti rezultat paralelizacije.',
 'Propusnost = 1.000 × M / vrijeme u ms. To je drugi način prikaza istog mjerenja, ne nezavisno mjerenje rada servera.',
 'Vršni živi heap uključuje dinamički alociranu memoriju i overhead alokatora, bez steka. V3 ima dodatni bafer kandidata; rezultat se odnosi na jedno pokretanje s M=200.',
 'RSS je vršni rezidentni skup stranica procesa, koji obuhvata i dijelove izvan heapa. Zato se ne izjednačava niti sabira s Massif vrijednošću.',
 'CPI je broj ciklusa po završenoj instrukciji. Budući da verzije izvršavaju različite instrukcije i različit broj ponavljanja, sam CPI ne dokazuje razlog bržeg izvršavanja.',
 'Retiring procjenjuje dio pipeline slotova u kojima su instrukcije završene korisnim radom na P-jezgrama. Procjena nije procenat ukupnog vremena čitavog programa.',
 'Back-End Bound procjenjuje ograničenja izvršnog dijela procesora. Povećan udio sam ne znači da je apsolutno vrijeme rada poraslo, jer se ukupno trajanje verzija razlikuje.',
 'Memory Bound je dio Back-End Bound vezan za memorijski podsistem. To je podmetrika i ne sabira se s Back-End vrijednošću; različiti profili daju samo indikaciju.'
]
for (filename,title,values,unit,note), explanation in zip(old_metrics,old_explanations):
    M('#### '+title+'\n')
    M(f'![{title}](grafikoni/{filename})\n')
    M(explanation+'\n')
M('Windows procesni P-core brojači u ovoj starijoj seriji dolaze iz zasebnih profila. Za kompletan VTune rezultat MUX je bio 0,885 / 0,994 / 0,957 / 0,992 / 0,990 za Ref./V1/V2/V3/V4; referentni profil time ima slabiju pokrivenost. Noviji PSI profili u narednom poglavlju nastali su drugim programima i uslovima. [3]\n')
M('### Ranije verzije na više ulaza\n')
M('Na sljedećem prikazu prvih šest redova ima N=200.000 i M=2.000, uz navedenu raspodjelu i širinu; posljednja dva reda su uniformna, w=0,02. Intenzitet boje zavisi od vrijednosti u tom redu, pa se boje ne smiju koristiti za poređenje brzine između redova. To je prethodna brute-force serija, a ne novo mjerenje PSI verzija.\n')
M('![Osam ulaza](grafikoni/21_brute_scenariji.png)\n')
M('V4 je najbrža u svim ovdje prikazanim redovima, ali veličina dobitka varira. Sedam uzoraka po ulazu ne određuje performanse na svim budućim podacima.\n')
M('## 6. VTune i memorija\n')
M('Raniji WSL Hotspots profil odnosio se na PSI `psi_seq` i prikazao funkcije pretrage stabla i provjere granica. Prvobitni Windows `matrix.exe` bio je provjera hardverskog profilera i nije projektni rezultat. Sada su zasebno snimljene obje stvarne PSI verzije pomoću Windows VTune **Microarchitecture Exploration** i hardverskog uzorkovanja; rezultati, CSV izvodi i EXE/PDB parovi su sačuvani. [7]\n')
M('Profilni Windows build koristi Clang `-O3`, PDB/CodeView i `P07_PROFILE_MARKERS`, koje olakšavaju pronalaženje faza ali mogu promijeniti optimizacije. Ulaz je N=200.000, M=20.000, w=0,02, uniformno, seed 42, 20 ponavljanja i `--no-check`. Snima se cijeli rezultat pokretanja preko cmd.exe, uključujući pripremu i izgradnje. Ispravnost je provjerena odvojeno. Mjerenja u nastavku su po jedno uzorkovano pokretanje, nisu WSL Release benchmark i ne dokazuju uzročnost.\n')
M('| VTune pokazatelj | Referenca | Optimizovana |')
M('| --- | ---: | ---: |')
for label,key,fmt in [('Proteklo vrijeme (s)','Elapsed Time',lambda x:f'{x:.3f}'),
    ('Završene instrukcije (milijarde)','Instructions Retired',lambda x:f'{x/1e9:.3f}'),
    ('CPI','CPI Rate',lambda x:f'{x:.3f}'),('MUX pouzdanost','MUX Reliability',lambda x:f'{x:.3f}'),
    ('P-core Retiring (%)','Retiring',lambda x:f'{x:.1f}'),('P-core Front-End Bound (%)','Front-End Bound',lambda x:f'{x:.1f}'),
    ('P-core Bad Speculation (%)','Bad Speculation',lambda x:f'{x:.1f}'),
    ('P-core Back-End Bound (%)','Back-End Bound',lambda x:f'{x:.1f}'),
    ('P-core Memory Bound (%)','Memory Bound',lambda x:f'{x:.1f}')]:
    M(f"| {label} | {fmt(vtune['baseline'][key])} | {fmt(vtune['optimized'][key])} |")
M('\n![VTune proteklo vrijeme](grafikoni/06_vtune_proteklo.png)\n')
M('Proteklo vrijeme odnosi se na cijeli VTune rezultat s konkretnim profilnim buildom. Razlika ne predstavlja procjenu stabilnog ubrzanja bez ponovljenih mjerenja.\n')
M('![VTune instrukcije](grafikoni/07_vtune_instrukcije.png)\n')
M('Završene instrukcije ukazuju na obim izvršenih instrukcija u profilu; varijante mogu raditi različite instrukcije, pa sam broj ne objašnjava vrijeme.\n')
M('![VTune CPI](grafikoni/08_vtune_cpi.png)\n')
M('CPI je broj ciklusa po završenoj instrukciji. Sam po sebi ne govori koja verzija završava posao brže jer se razlikuje i ukupan broj instrukcija.\n')
M('![VTune MUX](grafikoni/09_vtune_mux.png)\n')
M('MUX Reliability pokazuje pouzdanost procjene kada VTune dijeli ograničen broj hardverskih brojača; vrijednost bliža 1 ovdje ukazuje na visok nivo pokrivenosti, ali nije garancija tačnosti svakog zaključka.\n')
M('![VTune P jezgra](grafikoni/10_vtune_pcore.png)\n')
M('Retiring se odnosi na završeni korisni rad u pipelineu; Front-End Bound na ograničenja dobave i dekodiranja instrukcija; Bad Speculation na izgubljeni rad zbog pogrešnih spekulacija; Back-End Bound na čekanja i ograničenja izvršnog dijela. Memory Bound je podmetrika Back-End Bound i odnosi se na memorijski dio. Ovi podaci su procjene za P-jezgra, ne procenat ukupnog vremena aplikacije. Podmetriku ne treba sabirati s roditeljskom.\n')
M('U grupisanom izvozu funkcija početnog PSI profila, među najznačajnijim granama su `comparator::lt` oko 0,526 s, `find_bucket` oko 0,126 s, `box_intersect_box` oko 0,125 s i `within_box` oko 0,110 s samostalnog CPU vremena, zbrojeno po odgovarajućem nazivu i lokacijama. To su procjene uzorkovanja i ne moraju se sabirati u ukupno proteklo vrijeme. PDB je omogućio prikaz projektnih i PSI funkcija/linija, uključujući `psi_seq_buffer.h` i `psi_benchmark.cpp`; nedostaju simboli nekih sistemskih DLL-ova. Broj niti u ukupnom rezultatu može obuhvatiti `cmd.exe` i pomoćne procese; sam projektni program prijavljuje jednu nit.\n')
M('### Memorija integrisanih PSI verzija\n')
M('Zasebno su izmjerene obje stvarne verzije na WSL Release buildu: uniformno N=200.000, M=2.000, seed 42, jedno zagrijavanje i jedno ponavljanje, bez provjere. Program pri tome gradi stablo dva puta i obrađuje dvije grupe upita; generisanje ulaza je uključeno. Massif mjeri jedan proces bez steka; vrh je živi heap plus dodatni prostor alokatora. RSS je medijan vršnih rezidentnih skupova tri procesa. To su različite metrike u odvojenim pokretanjima, a ne količina memorije samog bafera. [6]\n')
M('| Širina | Heap ref. / opt. (MiB) | RSS ref. / opt. (MiB) |')
M('| --- | ---: | ---: |')
for x in memory:
    a,b=x['psi_seq'],x['psi_seq_opt']
    M(f"| {x['width']} | {a['heap_plus_overhead_bytes']/1048576:.2f} / {b['heap_plus_overhead_bytes']/1048576:.2f} | {a['rss_kib']/1024:.2f} / {b['rss_kib']/1024:.2f} |")
M('\n![Vršni heap](grafikoni/04_heap.png)\n')
M('Heap prikazuje koliko je dinamičke memorije bilo živo na vršnom Massif snimku. Optimizovana verzija ovdje ima neznatno veći vrh; rezultat ne dokazuje koliko je memorije privremeni bafer trošio u svim trenucima. Za w=0,02 je u stablu alokacija vršnog snimka reference među značajnim granama `parlay::internal::block_allocator::alloc()` s 12.845.232 B, uključujući put vezan za izgradnju KD-stabla. To je atribucija grani poziva u tom snimku, ne memorija koju funkcija trajno posjeduje; inline optimizacije ograničavaju preciznost.\n')
M('![Vršni RSS](grafikoni/05_rss.png)\n')
M('RSS pokazuje vršni broj rezidentnih memorijskih stranica procesa i uključuje više od heap alokacija. Medijan tri ponavljanja smanjuje uticaj jednog očitanja, ali ne garantuje da će rezultat biti isti kod drugih ulaza. Heap i RSS se ne sabiraju niti direktno izjednačavaju.\n')
markdown=re.sub(r'\s*\[[1-7]\]', '', '\n'.join(md))+'\n'
(HERE/'Izvjestaj_sekvencijalna_optimizacija.md').write_text(markdown, encoding='utf-8')

styles=getSampleStyleSheet()
styles.add(ParagraphStyle(name='Title08',fontName='ArialBold',fontSize=20,leading=25,textColor=colors.HexColor('#17365d'),spaceAfter=14))
styles.add(ParagraphStyle(name='H08',fontName='ArialBold',fontSize=13,leading=17,textColor=colors.HexColor('#17365d'),spaceBefore=12,spaceAfter=7,keepWithNext=True))
styles.add(ParagraphStyle(name='Body08',fontName='Arial',fontSize=9.2,leading=12.8,spaceAfter=6))
styles.add(ParagraphStyle(name='Small08',fontName='Arial',fontSize=7.6,leading=10.4,spaceAfter=5))
styles.add(ParagraphStyle(name='Code08',fontName='Consolas',fontSize=6.2,leading=8))
styles.add(ParagraphStyle(name='Cmd08',fontName='Consolas',fontSize=7,leading=9))
styles.add(ParagraphStyle(name='Table08',fontName='Arial',fontSize=7.4,leading=9.4))

story=[]
def para(s,style='Body08'):
    s=escape(s).replace('**','').replace('`','')
    return Paragraph(s,styles[style])

story.append(Paragraph('Sekvencijalna optimizacija<br/>prostornih upita',styles['Title08']))

# Markdown drives the text, tables and figures of the PDF; appendices use controlled page breaks.
body=markdown
lines=body.splitlines()
in_fence=False
fenced=[]
i=0
while i<len(lines):
    line=lines[i]
    if line.startswith('# Sekvencijalna') or line.startswith('**Stanje mjerenja:**'):
        i+=1; continue
    if line.startswith('```'):
        if in_fence:
            wrapped=[]
            for item in fenced:
                wrapped.extend(textwrap.wrap(item,width=94,replace_whitespace=False,drop_whitespace=False) or [''])
            story.append(Preformatted('\n'.join(wrapped),styles['Cmd08']))
            story.append(Spacer(1,5*mm)); fenced=[]; in_fence=False
        else: in_fence=True
        i+=1; continue
    if in_fence:
        fenced.append(line); i+=1; continue
    if not line.strip(): i+=1; continue
    if line.startswith('## '): story.append(para(line[3:],'H08')); i+=1; continue
    if line.startswith('### '): story.append(para(line[4:],'H08')); i+=1; continue
    if line.startswith('#### '): story.append(para(line[5:],'H08')); i+=1; continue
    if line.startswith('!['):
        match=re.search(r'\]\(([^)]+)\)',line)
        fig=HERE/match.group(1)
        im=PILImage.open(fig)
        width=170*mm
        story.append(Image(str(fig),width=width,height=width*im.height/im.width))
        story.append(Spacer(1,4*mm)); i+=1; continue
    if line.startswith('|'):
        table_lines=[]
        while i<len(lines) and lines[i].startswith('|'):
            if not re.match(r'^\|[ :|-]+\|$',lines[i]): table_lines.append(lines[i])
            i+=1
        cells=[[para(cell.strip(),'Table08') for cell in row.strip('|').split('|')] for row in table_lines]
        widths={5:[36,25,43,43,24],3:[95,38,38],2:[27,144]}
        colwidths=widths[len(cells[0])]
        t=Table(cells,colWidths=[x*mm for x in colwidths],repeatRows=1,hAlign='LEFT')
        t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),colors.HexColor('#dceaf5')),
          ('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,colors.HexColor('#f2f6f9')]),
          ('VALIGN',(0,0),(-1,-1),'TOP'),('GRID',(0,0),(-1,-1),.3,colors.HexColor('#ced9e0')),
          ('LEFTPADDING',(0,0),(-1,-1),4),('RIGHTPADDING',(0,0),(-1,-1),4),
          ('TOPPADDING',(0,0),(-1,-1),4),('BOTTOMPADDING',(0,0),(-1,-1),4)]))
        story.append(t); story.append(Spacer(1,3*mm)); continue
    story.append(para(line,'Small08' if line.startswith(('SHA-256','[1]','[2]','[3]','[4]','[5]','[6]','[7]')) else 'Body08'))
    i+=1

def footer(canvas,doc):
    canvas.setStrokeColor(colors.HexColor('#d5e0e8'))
    canvas.line(19*mm,15*mm,191*mm,15*mm)
    canvas.setFont('Arial',7.5); canvas.setFillColor(colors.HexColor('#315d86'))
    label='batched-spatial-queries'
    x,y=19*mm,10.5*mm
    canvas.drawString(x,y,label)
    width=canvas.stringWidth(label,'Arial',7.5)
    canvas.linkURL('https://github.com/amilababic4/batched-spatial-queries/tree/main',
                   (x,y-1,x+width,y+9),relative=0,thickness=0)
    canvas.setStrokeColor(colors.HexColor('#315d86'))
    canvas.line(x,y-1,x+width,y-1)
    canvas.setFillColor(colors.HexColor('#64798a'))
    canvas.drawRightString(191*mm,10.5*mm,str(doc.page))

pdf=HERE/'Izvjestaj_sekvencijalna_optimizacija.pdf'
doc=SimpleDocTemplate(str(pdf),pagesize=A4,leftMargin=19*mm,rightMargin=19*mm,topMargin=17*mm,bottomMargin=21*mm,
                      title='Sekvencijalna optimizacija prostornih upita',author='PRS')
doc.build(story,onFirstPage=footer,onLaterPages=footer)
print(f'PDF: {pdf}\nMarkdown: {HERE / "Izvjestaj_sekvencijalna_optimizacija.md"}')
