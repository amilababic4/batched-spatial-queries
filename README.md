# Paralelna grupna obrada prostornih upita (P07)

Projekat iz predmeta **Paralelni računarski sistemi**, tema **P07: Batched Spatial Queries**.

Cilj projekta je implementirati i optimizovati grupnu obradu prostornih upita nad zajedničkim skupom dvodimenzionalnih tačaka. Fokus je na **pravougaonim prostornim upitima (2D range queries)**, odnosno pronalaženju svih tačaka koje se nalaze unutar zadane pravougaone oblasti.

Prvo se implementira sekvencijalni brute-force algoritam kao referenca za tačnost, zatim se uvodi prostorno indeksiranje pomoću ravnomjerne mreže (*Uniform Grid*). Nakon toga se obrada optimizuje i paralelizuje na CPU-u korištenjem **OpenMP-a i SIMD-a (AVX2/FMA)**.

## Dokumentacija

Folder `docs/` sadrži naučno-istraživačke radove, linkove i ostale materijale relevantne za projekat.

- **`prostorno-indeksiranje/`**: radovi o metodama prostornog indeksiranja, izboru kandidata i smanjenju broja nepotrebnih provjera.
  - *LibRTS: A Spatial Indexing Library by Ray Tracing* (Geng et al., 2025): BVH hijerarhija i filtriranje prostornih podataka.
  - *Multi-Space Tree with Incremental Construction for GPU-Accelerated Range Queries* (Donnelly i Gowanlock, 2024): kombinovanje različitih podjela prostora i izdvajanje kandidata.

- **`paralelizacija/`**: radovi o organizaciji i paralelnoj obradi većeg broja prostornih upita.
  - *GTS: GPU-based Tree Index for Fast Similarity Search* (Zhu et al., 2024): grupna obrada upita, organizacija memorije i filtriranje kandidata.
  - *RayJoin: Fast and Precise Spatial Join* (Geng et al., 2024): poređenje prostornih struktura, organizacija obrade i problem nejednakog opterećenja.

- **`evaluacija/`**: radovi o poređenju prostornih indeksa i faktorima koji utiču na performanse.
  - *Evaluating Learned Spatial Indexes* (Pai et al., 2026): uticaj raspodjele podataka, selektivnosti upita i veličine blokova na performanse prostornih indeksa.

- **`linkovi-implementacije.md`**: linkovi na korisne implementacije, dokumentaciju i primjere prostornih indeksa.

Radovi obuhvataju i pristupe koji se razlikuju od naše implementacije. Koriste se kao literatura za razumijevanje indeksiranja, grupne obrade i optimizacije, a ne kao direktna implementacijska osnova za sve dijelove projekta.

## Inicijalni plan implementacije

Implementacija je planirana u **C++20**, uz **CMake** za izgradnju projekta.

Ulaz čine skup `P` od `N` dvodimenzionalnih tačaka i skup `Q` od `M` pravougaonih upita.

Svaka tačka sadrži koordinate `(x, y)` i jedinstveni identifikator, dok je svaki upit definisan granicama `(xmin, ymin, xmax, ymax)`.

Izlaz je skup identifikatora tačaka koje pripadaju svakom upitu.

### 1. Sekvencijalna verzija

- Generisanje i učitavanje dvodimenzionalnih tačaka i pravougaonih upita.
- Implementacija brute-force algoritma koji za svaki upit provjerava sve tačke.
- Provjera pripadnosti tačke pravougaoniku uslovom:

  `xmin <= x <= xmax && ymin <= y <= ymax`

- Čuvanje rezultata odvojeno za svaki upit.
- Implementacija prostornog indeksa **Uniform Grid**, koji dijeli prostor na ravnomjerne ćelije.
- Za svaki upit određivanje relevantnih ćelija i provjera tačaka koje se u njima nalaze.
- Poređenje rezultata indeksirane pretrage sa brute-force algoritmom.

Vremenska složenost brute-force algoritma je **O(M × N)**. Indeksiranje može smanjiti broj pregledanih tačaka, zavisno od raspodjele podataka i veličine upita.

Metrike: vrijeme izgradnje indeksa, vrijeme pretrage i broj obrađenih upita u sekundi (QPS).

### 2. SIMD optimizacija

Provjera pripadnosti tačaka pravougaoniku implementira se u više verzija:

- Skalarna implementacija.
- Automatska vektorizacija kompajlera.
- `#pragma omp simd`.
- Ručni AVX2 intrinsics, uz FMA gdje je primjenjiv.

Za efikasniju vektorizaciju razmatra se **SoA (Structure of Arrays)** raspored, gdje se koordinate `x` i `y` čuvaju u odvojenim nizovima.

SIMD omogućava istovremenu provjeru više tačaka u odnosu na granice istog pravougaonika.

Sve SIMD verzije moraju dati iste rezultate kao skalarna verzija.

### 3. OpenMP paralelizacija

Pošto su pravougaoni upiti nezavisni i koriste isti skup tačaka, paralelizacija se izvodi prvenstveno po upitima.

- Paralelizacija vanjske petlje korištenjem `#pragma omp parallel for`.
- Raspodjela upita između procesorskih niti.
- Poređenje `static` i `dynamic` raspoređivanja.
- Odvojeni rezultati za svaki upit radi izbjegavanja data race problema.
- Paralelna obrada brute-force i indeksirane pretrage.
- Kombinovanje **OpenMP + AVX2** optimizacija.

Posebno se analizira uticaj različitih veličina upita na raspodjelu opterećenja između niti.

### 4. Mjerenja

Predložene veličine eksperimentalnih skupova:

- Broj tačaka `N` ∈ {10.000, 100.000, 1.000.000}.
- Broj upita `M` ∈ {100, 1.000, 10.000}.
- Broj niti ∈ {1, 2, 4, 8}, u skladu s dostupnim hardverom.
- Ravnomjerna i grupisana raspodjela tačaka.
- Pravougaonici različitih veličina i selektivnosti.

Porede se:

1. Sekvencijalni brute-force.
2. Sekvencijalni Uniform Grid.
3. SIMD optimizovane verzije.
4. OpenMP paralelne verzije.
5. Kombinovana OpenMP + AVX2 verzija.

Metrike:

- Vrijeme izgradnje indeksa.
- Vrijeme izvršavanja upita.
- Ukupno vrijeme.
- QPS (*Queries Per Second*).
- Ubrzanje (*speedup*): `S = T_seq / T_par`.
- Paralelna efikasnost: `E = S / broj_niti`.

Ubrzanje paralelne indeksirane verzije mjeri se u odnosu na sekvencijalnu indeksiranu verziju, kako bi se razlikovao doprinos paralelizacije od doprinosa samog indeksa.

Mjerenja se ponavljaju više puta, uz zagrijavanje programa i korištenje medijana vremena izvršavanja.

## Tok implementacije

Svaki korak se testira prije prelaska na sljedeći.

1. **Priprema podataka.** Generisanje tačaka i pravougaonih upita s fiksnim seedom radi ponovljivosti. Provjera koordinata, identifikatora i granica upita.

2. **Brute-force pretraga.** Implementacija sekvencijalnog algoritma koji prolazi kroz sve tačke za svaki upit. Provjera rezultata na malim, unaprijed definisanim primjerima.

3. **Uniform Grid indeks.** Podjela prostora na ćelije i dodjeljivanje tačaka odgovarajućim ćelijama. Provjera da je svaka tačka pravilno indeksirana.

4. **Indeksirana pretraga.** Pronalaženje ćelija koje se sijeku s pravougaonikom i provjera njihovih tačaka. Rezultati moraju biti identični brute-force rezultatima.

5. **Zaključavanje sekvencijalne verzije.** Nakon uspješnih testova sekvencijalna verzija se označava Git tagom i koristi kao referenca za optimizacije.

6. **SIMD optimizacija.** Implementacija vektorizovane provjere koordinata. Poređenje skalarne, automatski vektorizovane, OpenMP SIMD i AVX2 verzije.

7. **OpenMP paralelizacija.** Paralelna raspodjela upita između niti, uz poređenje statičkog i dinamičkog raspoređivanja. Provjera da paralelna verzija vraća iste rezultate kao sekvencijalna.

8. **Kombinovanje OpenMP-a i SIMD-a.** Istovremena obrada različitih upita na više niti, uz vektorizovanu provjeru tačaka unutar svakog upita.

9. **Mjerenja i analiza.** Poređenje svih verzija na različitim veličinama skupova podataka, uz analizu ubrzanja, efikasnosti i uticaja prostornog indeksa.

Razvoj i testiranje započinju na manjim skupovima, a konačna mjerenja se izvode na većim skupovima podataka.

## Provjera ispravnosti

- **Tačke unutar pravougaonika:** sve odgovarajuće tačke moraju biti pronađene.
- **Tačke na granici:** tačke koje leže na ivici pravougaonika uključuju se u rezultat.
- **Prazan rezultat:** upit koji ne obuhvata nijednu tačku vraća prazan skup.
- **Ponovljene koordinate:** tačke s istim koordinatama i različitim identifikatorima moraju biti pravilno obrađene.
- **Uniform Grid:** indeksirana pretraga mora dati isti skup identifikatora kao brute-force.
- **SIMD:** vektorizovane verzije moraju dati identične rezultate kao skalarna.
- **OpenMP:** paralelna verzija mora dati iste rezultate kao sekvencijalna, nezavisno od broja niti.
- **Determinističnost:** isti ulazni podaci moraju dati iste skupove rezultata, bez obzira na redoslijed izvršavanja.

Poređenje rezultata zasniva se na skupovima identifikatora, a ne na redoslijedu njihovog pronalaženja.

## Pojmovi i skraćenice

| Pojam | Značenje |
|---|---|
| Batched Spatial Queries | Grupna obrada više prostornih upita nad zajedničkim podacima |
| Range Query | Upit kojim se pronalaze tačke unutar zadane oblasti |
| Bounding Rectangle | Pravougaonik definisan minimalnim i maksimalnim koordinatama |
| Brute-force | Potpuni pregled svih tačaka za svaki upit |
| Spatial Index | Struktura podataka za ubrzavanje prostorne pretrage |
| Uniform Grid | Prostorni indeks zasnovan na ravnomjernoj mreži ćelija |
| BVH | Bounding Volume Hierarchy, hijerarhija obuhvatnih volumena |
| MiSTIC | Multi-Space Tree with Incremental Construction |
| Selectivity | Udio tačaka koje zadovoljavaju uslov upita |
| SoA / AoS | Structure of Arrays / Array of Structures, načini rasporeda podataka u memoriji |
| OpenMP | Tehnologija za paralelizaciju programa pomoću više niti |
| SIMD | Single Instruction, Multiple Data, obrada više podataka jednom instrukcijom |
| AVX2 | Skup 256-bitnih vektorskih instrukcija |
| FMA | Fused Multiply-Add, kombinovana operacija množenja i sabiranja |
| Data Race | Nesinhronizovan pristup zajedničkim podacima iz više niti |
| Static Scheduling | Unaprijed određena raspodjela iteracija između niti |
| Dynamic Scheduling | Dinamička dodjela iteracija nitima tokom izvršavanja |
| QPS | Queries Per Second, broj obrađenih upita u sekundi |
| Speedup | Odnos vremena sekvencijalnog i paralelnog izvršavanja |
| Parallel Efficiency | Odnos ubrzanja i broja korištenih niti |
