# Selain-demo ilman ESP32-laitetta

Käynnistä projektin juuresta:

```powershell
python tools/demo.py
```

Komento kääntää tarvittaessa PC-demomoottorin, käynnistää vain tämän koneen
osoitteessa `http://127.0.0.1:8765` kuuntelevan palvelimen ja avaa selaimen.
Tarvitset C++17-kääntäjän; tämän työtilan paikallinen Zig löytyy automaattisesti.
Myöhemmillä käynnistyksillä valmista `build/demo_engine.exe`-tiedostoa käytetään,
jos sen lähteet eivät ole muuttuneet. Jos portti on varattu, käytä esimerkiksi
`python tools/demo.py --port 8766`. Sammuta palvelin päätteen Ctrl+C:llä.

Demo käyttää suoraan `include/can_core.h`- ja `include/commands.h`-luokkia.
Se ei jäljittele niiden logiikkaa JavaScriptillä. Python välittää komennot
C++-prosessiin, ja selain näyttää sen muodostamat todelliset tilat ja kehykset.
Demossa ei käytetä SPI:tä eikä MCP2515:n fyysistä laitteistoa. Ohjelmallinen
testisovitin kuittaa kehyksen 1 ms:n kuluttua; ulkoisen ACK:n puuttumista voidaan
kokeilla ohjelmallisesti 100 ms:n aikakatkaisulla. RX-painike syöttää ohjelmallisen
vastaanottotapahtuman. Molempien suuntien monitorointi on demossa valmiiksi päällä.

## Kokeile näin

1. **Nykyinen auton profiili → Käynnistä.** Monitoriin tulee kerran sekunnissa
   `0x701`, DLC 1, data `05`. Vaihda tilaksi `7F · Pre-operational` ja väliksi
   `100 ms`: seuraavissa kehyksissä näkyvät muutos ja nopeampi lähetys.
2. **Toista alusta.** Näet alkuperäisen tallenteen 49 viestiä. `0x301`:n DLC on 7.
   Tauota, jatka ja kokeile silmukointia. Toisto käyttää samaa generoitua `.inc`-
   tallennetta kuin ESP32-ohjelmisto.
3. **Automaattirampin testi → Käynnistä.** RPM kasvaa 600 yksikköä sekunnissa ja
   SOC laskee 0,5 prosenttiyksikköä sekunnissa. Arvot koodataan `0x100`-testiviestiin.
   Tämä profiili on tarkoituksella synteettinen eikä muuta auton JSON-profiilia
   tai firmwarea. Voit kokeilla myös `SET_RPM 3500` ja `SET_SOC 75.5`.
4. **Poista ACK-kuittaus** lähetyksen aikana. Lähetys pysähtyy, virhelaskuri
   kasvaa ja näkyviin tulee virheilmoitus. Palauta ACK, paina `RECOVER` ja
   käynnistä uudelleen.
5. **Kokeile virheellistä arvoa.** `SET 0x701 0 256` antaa virheen ja jättää
   viestin ennalleen. Komentorivi käyttää firmwaren oikeaa parseria.

Nykyisen auton profiilissa `SET_SOC`/`SET_RPM` antavat edelleen tarkoituksella
virheen: auton vastaavien signaalien määrittely puuttuu. Profiilin vaihto nollaa
demon laskurit. Monitori näyttää viimeiset 400 tapahtumaa, eikä sivun tyhjennyspainike
nollaa itse simulaattoria. Palvelimen käynnistys tai profiilin vaihto aloittaa
uuden demon tilan. Useat selainvälilehdet ohjaavat samaa simulaatioistuntoa.

## Testit ja rajaus

```powershell
python tools/demo.py --self-test
```

Integraatiotesti tarkistaa pysäytetyn aloituksen, syklisen lähetyksen,
signaalin muokkauksen, virheellisen arvon, tallenteen loppuun toistamisen ja
DLC:n, automaattirampit, ACK-aikakatkaisun, palautumisen ja RX-tapahtuman.
Alkuperäiset PC-testit ajetaan edelleen komennolla `python tools/check.py`.

PC-demo ei vahvista ESP32:n suorituskykyä, todellista arbitrointia, transceiveria,
johtoja tai auton viestikarttaa. Niitä varten käytetään
[laitteiston testausohjetta](../docs/HARDWARE_TEST.md).
