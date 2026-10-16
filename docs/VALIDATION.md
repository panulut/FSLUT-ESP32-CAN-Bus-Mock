# Testauksen tila 16.10.2026

## Suoritetut tarkistukset

| Tarkistus | Tulos |
|---|---|
| `python tools/check.py` | Läpäisty: 10 Python-testiä ja 725 C++-tarkistusta |
| `powershell -NoProfile -File tools/pio.ps1 run` | Läpäisty: ESP32:n firmware kääntyy ja linkittyy |
| Profiilin uudelleengenerointi | Generoitu otsake vastaa JSON-lähdettä |
| Tallenteen uudelleengenerointi | Generoitu `.inc` vastaa lähdeotetta ja 190 ms:n silmukkaväliä |
| Lähdeotteen vertailu | 49 viestiä vastaa alkuperäisen lokin rivejä 126–174 |
| Alkuperäiset tiedostot | Kaikki 5 purettua lähdetiedostoa vastaavat tavutasolla alkuperäisen `CAN SIM.zip`-arkiston sisältöä |

Python-testit tarkistavat CSV/TXT-lukemisen, DLC:n ja payloadin vastaavuuden,
ID-rajat, extended-/RTR-kehykset, aikaleimojen järjestyksen ja keston,
puuttuvan datan käsittelyn, virheellisen muunnoksen vaikutuksen aiempaan
tiedostoon sekä signaaliprofiilien validoinnin ja generoinnin toistettavuuden.

C++-testit tarkistavat tunnettuja etumerkillisiä ja etumerkittömiä
koodausvektoreita molemmilla tavujärjestyksillä, fyysisiä rajoja, 32-bittisiä
kenttiä, synteettisen checksumin, syklisen ajastuksen, tasapuolisen lähetyksen,
myöhästymisen, yli uint32-millisekuntirajan jatkuvan ajon, murto-osien
kertymisen automaattirampissa, toiston ajoituksen ja järjestyksen,
tauotuksen, silmukoinnin ja epäonnistuneen/peruutetun toistoviestin säilyttämisen.

MCP2515-testit käyttävät injektoitua rekisterimallia: kuuntelu-/normaali-/loopback-
tilojen vaihto, tarkka bittinopeus 8/16/20 MHz:n kiteillä, virheellisten
yhdistelmien hylkäys, jonotuksen erottaminen onnistuneesta lähetyksestä,
aikakatkaisu, bus-off, peruutus, peruutuksen ohittava jo onnistunut lähetys,
jumittuneen ohjaimen palautumispolku, standardi-/extended-/RTR-vastaanotto,
virheellinen DLC ja vastaanottopuskurien ylivuodot testataan ilman laitteistoa.
Komento- ja tulostusjonotestit kattavat virheelliset arvot, argumenttimäärät,
liian pitkät rivit, CRLF:n, signaalialiasvirheet, tilanvaihdot,
pysähtyneen UART-kirjoittajan, jonon kiertymisen ja kapasiteettirajat.

Rekisterimalli tarkistaa ohjelmiston toimintaa. Se ei emuloi CAN:n sähköistä
kerrosta, kaikkia ohjaimen aikakilpailuja tai todellista arbitrointia.

## Käännösympäristö ja artefakti

- PlatformIO Core 6.1.18, `platformio/espressif32@6.10.0`, board `esp32dev`.
- Arduino-ESP32-paketti `3.20017.241212+sha.dcc1105b` (core 2.0.17).
- ESP32-kääntäjä `8.4.0+2021r2-patch5`, C++17.
- PC-testit: Python 3.13 ja Zig 0.13.0:n C++17-kääntäjä; projektin
  C++-testit käännettiin asetuksilla `-Wall -Wextra -Werror`.
- Firmware käyttää 36128 tavua staattista RAM-muistia / 327680 tavua (11,0 %).
- Linkitetty ohjelma käyttää 297969 tavua / 1310720 tavua (22,7 %).
- Latauskuva: `.pio/build/esp32dev/firmware.bin`, 298336 tavua.
- Latauskuvan SHA-256:
  `98baab458101909c1e863e8167487a6ff39686274a0c1beb8c3cc193c03cd4f2`.

Kuvan tiiviste koskee tätä käännöstä. Uusi käännös voi sisältää eri
metatietoja ja saada eri tiivisteen. Lataa laitteeseen PlatformIO:n
upload-komennolla, joka käyttää myös oikeaa bootloaderia ja osiotaulua.

## Vielä varmistettavat asiat

1. Varsinaisen ESP32-kortin pinnit, MCP2515-moduulin kide ja transceiverin
   sähkökytkentä eivät ole tiedossa. `board_config.h` sisältää dokumentoidut esimerkit.
2. Ohjelmistoa ei ole ladattu eikä ajettu oikealla ESP32-/CAN-kokoonpanolla.
   Ulkoiset ACK:t, fyysinen vastaanotto, todelliset viiveet, suurin häviötön
   kuorma ja pitkä ajo on tarkistettava [laitteisto-ohjeella](HARDWARE_TEST.md).
3. Auton BMS- ja moottoriviestien fyysiset määritykset eivät ole täydellisiä.
   Oletusprofiilissa on vain 0x701/NMT_STATE. RPM-/SOC-rampin moottori ja
   koodaus on testattu synteettisillä signaaleilla, mutta auton vastaavia
   viestejä ei ole otettu käyttöön ilman vahvistettuja määrityksiä.
   Katso [protokollan avoimet kohdat](PROTOCOL.md).

Tulos on kääntyvä ja PC-testeillä tarkistettu ohjelmistototeutus. Sitä ei ole
vielä hyväksytty todellisen auton CAN-protokollan tai fyysisen testipenkin
korvikkeeksi.
