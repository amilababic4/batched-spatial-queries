"""Reproducibilno izdvaja mjerenja; ne mijenja postojece sirove podatke."""
from pathlib import Path
import csv,json,re,statistics

D=Path(__file__).resolve().parent
R=D.parent/'sirova_mjerenja'/'poredjenje_svih'
P=D.parent/'sirova_mjerenja'/'pdb_test'
NAMES=('ref','v1','v2','v3','v4')
SCENARIOS=('uniform_0.005','uniform_0.02','uniform_0.1','clustered_0.005','clustered_0.02','clustered_0.1','malo','veliko')

def time_series(tag):
    path=R/(tag+'_compare.txt')
    s=path.read_text(encoding='utf-8')
    top=re.search(r'N=(\d+) M=(\d+) w=([\d.]+) dist=(\w+) seed=(\d+) repeats=(\d+) AVX2=(\w+)',s)
    if not top: raise ValueError('Nedostaju parametri: '+str(path))
    N,M,w,dist,seed,repeats,avx=top.groups()
    result={'N':int(N),'M':int(M),'w':float(w),'dist':dist,'seed':int(seed),'repeats':int(repeats),'AVX2':avx,'source':str(path),'variants':{}}
    for n in NAMES:
        m=re.search(r'^'+n+r' uzorci_ms=([\d.,]+) medijan_ms=([\d.]+) pogodaka=(\d+) checksum=(\d+)',s,re.M)
        if not m: raise ValueError('Nedostaje '+n+' u '+tag)
        raw,median,hits,checksum=m.groups(); vals=[float(x) for x in raw.rstrip(',').split(',')]
        if len(vals)!=int(repeats):raise ValueError('Pogresan broj ponavljanja '+tag+' '+n)
        if abs(statistics.median(vals)-float(median))>0.001:raise ValueError('Medijan se razlikuje '+tag+' '+n)
        result['variants'][n]={'samples_ms':vals,'median_ms':float(median),'min_ms':min(vals),'max_ms':max(vals),'hits':int(hits),'checksum':int(checksum)}
    base=result['variants']['ref']['median_ms']
    if len({(x['hits'],x['checksum']) for x in result['variants'].values()})!=1: raise ValueError('Razliciti rezultati u '+tag)
    for x in result['variants'].values():
        x['speedup']=base/x['median_ms']
        x['queries_per_second']=int(M)*1000/x['median_ms']
    return result

def memory(n):
    path=R/f'massif_{n}.out'
    s=path.read_text(errors='replace')
    vals=[]
    for block in re.split(r'(?m)^snapshot=',s)[1:]:
        a=re.search(r'(?m)^mem_heap_B=(\d+)',block);b=re.search(r'(?m)^mem_heap_extra_B=(\d+)',block)
        if a and b:vals.append(int(a.group(1))+int(b.group(1)))
    if not vals:raise ValueError('Nema Massif snimaka '+n)
    rss_text=(R/f'rss_{n}.txt').read_text(errors='replace')
    m=re.search(r'Maximum resident set size \(kbytes\):\s*(\d+)',rss_text)
    prep=(R/f'rss_{n}_program.txt').read_text(errors='replace')
    t=re.search(r'priprema_ms=([\d.]+)',prep)
    if not m or not t:raise ValueError('Nema RSS/priprema '+n)
    return {'heap_peak_bytes':max(vals),'rss_peak_kib':int(m.group(1)),'preparation_ms':float(t.group(1)), 'N':200000,'M':200,'w':0.02,'dist':'uniform','samples':1, 'heap_includes':'heap + allocator overhead, no stacks','rss_source':str(R/f'rss_{n}.txt'),'massif_source':str(path)}

def num(row,key):
    v=row.get(key,'').strip()
    if v in ('','N/A','NA','not available'):return None
    try:return float(v)
    except ValueError:raise ValueError('Neocekivana vrijednost '+key+': '+v)

def read_vtune_log(path):
    raw=path.read_bytes()
    # PowerShell 5.1 redirects native output to UTF-16LE with a BOM.
    return raw.decode('utf-16') if raw.startswith((b'\xff\xfe',b'\xfe\xff')) else raw.decode('utf-8-sig',errors='replace')

def hardware(n):
    path=D/f'vtune_process_{n}.tsv'
    with path.open(encoding='utf-8-sig',newline='') as f: rows=list(csv.DictReader(f,delimiter='\t'))
    target=[x for x in rows if x.get('Process')=='poredjenje_pdb.exe']
    if len(target)!=1:raise ValueError('Potreban je tacno jedan ciljni proces za '+n)
    x=target[0]
    report_log=read_vtune_log(P/f'uarch_pdb_wrapper_{n}.log')
    mux=re.search(r'MUX Reliability:\s*([\d.]+)',report_log)
    elapsed=re.search(r'Elapsed Time:\s*([\d.]+)s',report_log)
    result={'p_core_cpi':num(x,'CPI Rate:Performance-core (P-core)'),
        'p_core_retiring_pct':num(x,'Performance-core (P-core):Retiring(%)'),
        'p_core_backend_pct':num(x,'Performance-core (P-core):Back-End Bound(%)'),
        'p_core_memory_pct':num(x,'Performance-core (P-core):Back-End Bound:Memory Bound(%)'),
        'p_core_clockticks':num(x,'Clockticks:Performance-core (P-core)'),
        'e_core_clockticks':num(x,'Clockticks:Efficient-core (E-core)'),
        'cpu_time_s':num(x,'CPU Time'),'process':x['Process'],
        'result_mux_reliability':float(mux.group(1)) if mux else None,
        'result_elapsed_time_s':float(elapsed.group(1)) if elapsed else None,
        'mux_scope':'Cijeli VTune rezultat, ukljucujuci cmd.exe; nije posebno za ciljni proces',
        'result':str(P/f'uarch_pdb_wrapper_{n}'),'tsv':str(path),
        'N':200000,'M':2000,'w':0.02,'dist':'uniform','repeats':80 if n=='v4' else 6,
        'collector':'Event-based sampling driver; process selected, with cmd.exe launch and follow-child'}
    if not result['p_core_clockticks'] or result['p_core_clockticks']<=0:
        for key in ('p_core_cpi','p_core_retiring_pct','p_core_backend_pct','p_core_memory_pct'):result[key]=None
        result['warning']='Nema uzorkovanih P-core ciklusa; P-core metrika nije dostupna.'
    return result

data={'scenarios':{n:time_series(n) for n in SCENARIOS},'memory':{n:memory(n) for n in NAMES},'hardware':{n:hardware(n) for n in NAMES},
      'warnings':['Vremena: WSL/G++14, izolovana obrada upita, 7 ponavljanja, topli kapaciteti izlaza; podaci generisani prije mjerenja.',
                  'Heap i RSS: WSL/G++14, N=200000 M=200, razliciti procesi, po jedno mjerenje; ne dodavati ih.',
                  'VTune: Windows/Clang 22, 6 ponavljanja ref/V1/V2/V3 i 80 V4, razlicito trajanje; normalizovane P-core metrike su procjene.',
                  'Retiring, Back-End i Memory Bound imaju hijerarhijske ili procijenjene vrijednosti i ne sabiraju se; kod hibridnog procesora moguca je migracija.',
                  'Priprema u compare modu stvara i AoS i SoA i nije trosak jedne verzije; preparation_ms je zaseban pojedinacni RSS proces.',
                  'Provjera jednakih pogodaka i zbira nije zamjena za punu provjeru ID-jeva iz zasebnih verify izlaza.']}
(D/'metrics.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')

with (D/'vrijeme_svi_scenariji.csv').open('w',newline='',encoding='utf-8-sig') as f:
    fields=['scenario','N','M','w','dist','seed','repeats','version','median_ms','min_ms','max_ms','speedup','queries_per_second','hits','checksum','samples_ms']
    w=csv.DictWriter(f,fieldnames=fields);w.writeheader()
    for tag,s in data['scenarios'].items():
        for n,x in s['variants'].items():
            row={k:s[k] for k in ('N','M','w','dist','seed','repeats')};row.update({k:x[k] for k in ('median_ms','min_ms','max_ms','speedup','queries_per_second','hits','checksum')});row.update(scenario=tag,version=n,samples_ms=';'.join(map(str,x['samples_ms'])));w.writerow(row)
with (D/'memorija_hardver.csv').open('w',newline='',encoding='utf-8-sig') as f:
    fields=['version','heap_peak_bytes','rss_peak_kib','preparation_ms','p_core_cpi','p_core_retiring_pct','p_core_backend_pct','p_core_memory_pct','p_core_clockticks','e_core_clockticks','cpu_time_s']
    w=csv.DictWriter(f,fieldnames=fields);w.writeheader()
    for n in NAMES:w.writerow({'version':n,**{k:data['memory'][n][k] for k in fields if k in data['memory'][n]},**{k:data['hardware'][n][k] for k in fields if k in data['hardware'][n]}})
print('Zapisani metrics.json, vrijeme_svi_scenariji.csv i memorija_hardver.csv')
