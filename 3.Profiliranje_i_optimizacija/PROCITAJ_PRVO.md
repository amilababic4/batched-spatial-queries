# PRS - sekvencijalno profiliranje i optimizacija

- **Za čitanje i predaju:** `Izvjestaj_sekvencijalna_optimizacija.pdf`. Markdown datoteka istog imena je uređiva verzija, a slike su u `grafikoni`.
- **Konačni projektni program:** `../src/psi_benchmark.cpp` s `../src/psi_seq_buffer.h`; CMake cilj `psi_seq_opt`. `psi_seq` je referentna PSI verzija. Upute za projekat su u `../README.md`.
- **Ranije verzije brute-forcea:** `poredjenje_brute_force_verzija.cpp` uključuje `brute_force_verzije.cpp` i sadrži referencu i verzije V1-V4. Eksperimentalna V5 je u `brute_force_v5_maska_x.cpp`.
- **Dokazi i ponavljanje:** `sirova_mjerenja` sadrži sačuvane CSV izvode, logove, provjere i konačne PSI VTune rezultate. `podaci_ranijih_mjerenja` sadrži zbirne podatke ranijih mjerenja; `izradi_izvjestaj.py` ponovo izrađuje izvještaj. Skripte `pokreni_poredjenje_psi.sh`, `pokreni_mjerenja_psi.sh`, `pokreni_mjerenja_memorije_psi.sh` i Windows PDB/VTune skripte služe za ponavljanje navedenih mjerenja.
- **VTune/PDB:** dva konačna interaktivna PSI rezultata su u `sirova_mjerenja/08_sekvencijalno/vtune_windows_finalni_ponovo`, a njihovi EXE i PDB parovi u `windows_build/08_psi_final_pdb`. Raniji interaktivni rezultati V1-V4 uklonjeni su; sačuvani su njihovi CSV izvozi i logovi.

Fascikle `../src`, `../reference`, `../1.KD-tree_mjerenja` i `../2.Testovi_i_velicine` pripadaju projektu i treba ih zadržati. Paralelizacija nije dio sadašnje faze.

Sirovi logovi zadržavaju izvorne nazive iz vremena mjerenja; neke interne VTune putanje također imaju stare brojeve kako bi sačuvani profili i PDB simboli ostali povezani. To ne utiče na nove nazive datoteka iznad.
