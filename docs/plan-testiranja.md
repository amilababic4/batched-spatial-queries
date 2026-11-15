# Plan testiranja prostornih upita

## Cilj testiranja

Cilj je provjeriti ispravnost algoritama za obradu prostornih upita pri različitim veličinama pravougaonika i posebnim slučajevima rasporeda tačaka.

## Planirani testni slučajevi

1. **Mali pravougaonici** – provjera upita koji obuhvataju mali broj tačaka.
2. **Srednji pravougaonici** – provjera upita koji obuhvataju veći dio posmatranog prostora.
3. **Veliki pravougaonici** – provjera upita koji obuhvataju većinu ili sve tačke.
4. **Prazni pravougaonici** – provjera slučajeva kada nijedna tačka ne pripada zadanoj oblasti.
5. **Tačke na granicama** – provjera uključivanja tačaka koje se nalaze na ivicama i uglovima pravougaonika.
6. **Pravougaonici različitih proporcija** – testiranje uskih i izduženih pravougaonika.
7. **Ponovljene koordinate** – provjera ispravnosti kada više tačaka ima iste koordinate.

## Plan implementacije

- Pripremiti ulazne podatke za navedene testne slučajeve.
- Definisati očekivane rezultate za jednostavne primjere.
- Pokrenuti testove nad postojećom brute-force implementacijom.
- Uporediti rezultate ostalih implementacija s referentnom brute-force verzijom.
- Evidentirati rezultate i eventualne nepravilnosti.
