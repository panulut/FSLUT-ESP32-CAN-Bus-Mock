# Formula Student CAN-simulaattori

ESP32:n ja MCP2515-CAN-ohjaimen ohjelmisto pöytätestaukseen. Toteutus sisältää
syklisen lähetyksen, ajonaikaisen muokkauksen, automaattiset signaalirampit,
aikaleimatun tallenteen toiston, CAN-vastaanoton ja USB-sarjaporttiohjauksen.
TJA1051 hoitaa fyysisen CAN-liitännän. LoRa-esimerkin koodia ei käytetä.

**Ohjelmisto on käännetty ESP32:lle ja testattu tietokoneella. Oikean laitteiston
kytkentöjä ja väylätoimintaa ei ole vielä testattu.** Katso
[laitteiston testausohje](docs/HARDWARE_TEST.md) ja
[testauksen tila](docs/VALIDATION.md).

## Viestimääritysten tila

Alkuperäinen `CAN.xlsx` ei sisällä valmista protokollamääritystä. Sen `Name`-sivun
vihreät ID-solut ovat A7 (`0x701`) ja A18 (`0x500`). Tehtävänannon heartbeat-rajaus
on ristiriidassa tämän kanssa. Ensimmäiseen versioon on otettu pieni poikkeus:
`0x701`:n yhden tavun NMT-tila, jonka pituus ja arvot näkyvät myös tallenteessa.
Oletusarvo on `0x05` (operational), lähetysväli 1000 ms on muokattava
testioletus. Ohjelma käynnistyy pysäytettynä ja CAN-ohjain kuuntelutilassa.

`0x500`:n DLC ja varatut tavut eivät ole vahvistettuja, joten viestiä ei luoda
oletuksena. Sinisten BMS-viestien tavujärjestystä, etumerkkejä ja tarkistussummia
ei arvata. `SET_SOC` ja `SET_RPM` antavat selkeän virheen, kunnes vastaavat
vahvistetut signaalit on lisätty profiiliin. Tämä versio ei siis vielä väitä
toteuttavansa auton kaikkia fyysisiä signaaleja. Lisätiedot:
[protokollan avoimet kohdat](docs/PROTOCOL.md).

Raakaviestit voi lisätä tietoisesti `ADD`-komennolla. Tallenteiden toisto säilyttää
niiden omat ID:t ja datan; sitä ei rajata vihreisiin viesteihin.

## Kytkennät ja asetukset

Haluatko ensin kokeilla ilman laitteistoa? Käynnistä `python tools/demo.py` ja
avaa [paikallinen selain-demo](http://127.0.0.1:8765).
Demossa voi kokeilla samaa C++-ydintä, tallenteita ja komentoja ohjelmallisella
testiväylällä. [Demon ohje ja esimerkit](demo/README.md).

Muokkaa `include/board_config.h` oman kortin ja moduulin mukaan. Nykyiset
**esimerkkiasetukset** ovat klassinen ESP32 Dev Module, MCP2515:n 16 MHz:n kide,
1 Mbit/s ja SPI-nopeus 1 MHz:

| ESP32 | MCP2515-moduuli |
|---|---|
| GPIO18 | SCK |
| GPIO19 | SO / MISO |
| GPIO23 | SI / MOSI |
| GPIO5 | CS |
| GND | GND |

INT-liitäntää ei tarvita: vastaanotto tarkistetaan toistuvasti SPI:n kautta.
GPIO5 on klassisen ESP32:n käynnistysasetuksiin vaikuttava pinni; tarkista
kortin kytkentä tai valitse sopiva toinen CS-pinni.

Tarkista moduulin käyttöjännite ja logiikkatasot ennen kytkentää. ESP32:n GPIO:t
eivät siedä 5 V:n signaaleja. TJA1051:n eri versioiden syöttö- ja VIO-kytkennät
eroavat toisistaan; tarkista juuri oman komponentin datalehti. MCP2515:n TXCAN/RXCAN
liitetään transceiveriin ja transceiverin CANH/CANL testiväylään. Väylän kummassakin
fyysisessä päässä on oltava asianmukainen 120 ohmin päätevastus sekä yhteinen
maareferenssi. Käytä tätä vain erillisessä testipenkissä.

## Kääntäminen ja lataus

Projekti käyttää PlatformIO:ta. ESP32-alusta on lukittu versioon 6.10.0
(Arduino-ESP32 2.0.17). Ulkopuolisia CAN-kirjastoja ei tarvita; mukana on rajattu,
rekisteritasolla testattava MCP2515-ajuri.

Kun PlatformIO on asennettuna:

```powershell
python tools/generate_profile.py config/vehicle.json include/vehicle_profile.h
python tools/can_to_inc.py examples/motor_excerpt.txt include/recording.inc --loop-gap-ms 190
pio run
pio run --target upload --upload-port COM5
pio device monitor --port COM5 --baud 115200
```

Vaihda `COM5` oman laitteen portiksi. Tässä työtilassa PlatformIO asennettiin
paikallisesti `.tools`-kansioon. Sen kanssa samat komennot ovat
`powershell -File tools/pio.ps1 run`,
`powershell -File tools/pio.ps1 run --target upload --upload-port COM5` ja
`powershell -File tools/pio.ps1 device monitor --port COM5 --baud 115200`.
Laitteelle latausta ei ole tehty tämän toteutuksen yhteydessä.

Jos PlatformIO puuttuu toisesta työtilasta, asenna PlatformIO IDE tai Pythonin
virtuaaliympäristöön `platformio==6.1.18`. Tämän jälkeen käytä ympäristön
`python -m platformio` -komentoa `pio`-komennon tilalla.

## Ensimmäinen kokeilu

Sarjaportti käyttää 115200 baudia. Komennot ovat kirjainkoon huomioivia.
Päätä komento LF-, CR- tai CRLF-rivinvaihtoon. Lähetä komento kerrallaan ja odota
vastausta. Numero on desimaali tai `0x`-alkuinen heksadesimaali: `50` tarkoittaa
desimaalilukua 50, `0x50` lukua 80. Fyysisissä arvoissa käytetään desimaalipistettä.

Ilman toista CAN-solmua voi käyttää MCP2515:n sisäistä loopback-tilaa:

```text
CONFIG 500000 16000000 LOOPBACK
MONITOR ON
START
STATUS
STOP
```

Loopback tarkistaa ohjaimen sisäisen lähetys-/vastaanottopolun. Se ei tarkista
transceiveria, kaapelointia tai toisen solmun ACK-kuittausta.

Normaalissa testiväylässä toinen aktiivinen CAN-solmu tarvitaan kuittaamaan
lähetykset. Pelkkä kuunteleva adapteri ei riitä:

```text
CONFIG 1000000 16000000 NORMAL
MONITOR RX ON
MONITOR TX ON
START
SET_SIGNAL NMT_STATE 127
INTERVAL 0x701 5000
STATUS
STOP
```

## Komennot

| Komento | Toiminta |
|---|---|
| `HELP` | Komentojen nimet |
| `START` / `STOP` | Käynnistä nykyinen tila / pysäytä ja pyydä odottavan lähetyksen peruutusta |
| `PAUSE` | Pysäytä ajastus; jo jonossa oleva viesti saa valmistua |
| `STATUS` | Tila, asetukset, jonotus-/onnistumis-/virhelaskurit ja monitoroinnin pudotukset |
| `LIST` | Viestit ja fyysisten signaalien määritykset; päättyy `OK END LIST` |
| `MODE MANUAL` | Sykliset viestit käyttäjän arvoilla |
| `MODE AUTO` | Sykliset viestit ja profiiliin määritetyt lineaariset rampit |
| `MODE PLAYBACK` | Tallenteen toisto |
| `SET 0x701 0 5` | Raakaviestin tavun muokkaus; tavuindeksi alkaa nollasta |
| `SET_SIGNAL NMT_STATE 127` | Nimetyn signaalin fyysinen arvo |
| `SET_SOC 75.5` / `SET_RPM 3500` | Aliakset profiilin `SOC`-/`RPM`-signaaleille; oletusprofiilissa niitä ei ole |
| `FRAME 0x701 1 0x05` | Päivitä olemassa oleva koko viesti: ID, DLC, tavut |
| `ADD 0x500 100 3 0x05 0 0` | Esimerkki **käyttäjän määrittelemästä raakaviestistä**: ID, väli ms, DLC, tavut; ei vahvistettu auton 0x500-määritys |
| `ENABLE 0x701 OFF` / `ON` | Poista viesti syklisestä lähetyksestä / ota käyttöön |
| `INTERVAL 0x701 1000` | Lähetysväli 1..3600000 ms |
| `PLAY` | Valitse toisto tarvittaessa ja aloita/jatka |
| `RESTART` | Palauta toisto alkuun; ensin `PAUSE` tai `STOP`, sitten `RESTART` ja `PLAY` |
| `LOOP ON` / `OFF` | Toiston silmukointi |
| `MONITOR ON` / `OFF` | RX- ja TX-tulostus yhdessä |
| `MONITOR RX ON` / `MONITOR TX OFF` | Suunnat erikseen |
| `FILTER 0x301` / `FILTER OFF` | Monitoritulostuksen ID-suodatin; vastaanottolaskuri laskee kaikki viestit |
| `CONFIG 500000 8000000 NORMAL` | Bittinopeus, MCP2515:n kide Hz, NORMAL/LOOPBACK; vain pysäytettynä |
| `RECOVER` | Alusta CAN-ohjain uudelleen nykyisillä asetuksilla; vain pysäytettynä |

Tilan vaihto ja uudelleenalustus vaativat pysäytyksen sekä mahdollisen odottavan
lähetyksen valmistumisen. `STOP` ei pysty vetämään takaisin jo väylällä olevaa
viestiä. Peruutettu toistoviesti säilytetään seuraavaa `PLAY`-komentoa varten.
`RESTART` ei aloita lähetystä itsestään. Kerran loppuun toistettu tallenne vaatii
`RESTART`-komennon ennen uutta `PLAY`-komentoa. `ENABLE` vaikuttaa vain syklisiin
viesteihin, ei tallenteeseen. Ajonaikaiset muutokset eivät säily uudelleenkäynnistyksessä.

Vain `CONFIG`/`RECOVER` ja CAN-tilan vaihto voivat odottaa laitteistoa rajatun
ajan. Aktiivisessa lähetyksessä, toistossa ja monitoroinnissa ei käytetä odottavia
viivesilmukoita. Virheelliset komentorivit hylätään kokonaisina ennen viestin
päivitystä. Enimmäispituus on 191 merkkiä.

## Tallenteet

CSV-muoto:

```csv
timestamp_ms,can_id,dlc,data
100,0x301,7,00 10 20 30 40 50 60
110,0x6B0,8,01 02 03 04 05 06 07 08
```

Valinnaiset `extended`- ja `rtr`-sarakkeet hyväksyvät `true`/`false` tai `1`/`0`.
Ilman `extended`-saraketta yli 0x7FF:n ID tulkitaan laajennetuksi. Pienen ID:n
laajennettu kehys vaatii `extended=true`. RTR-kehyksen data on tyhjä, mutta DLC
voi olla 0..8. Sykliset sarjaporttikomennot ja signaaliprofiilit käyttävät
11-bittisiä datakehyksiä; tallennetoisto ja vastaanotto tukevat myös 29-bittisiä
ja RTR-kehyksiä. CAN FD:tä ei tueta.

TXT-muoto:

```text
0x0301 2144402 PDO { 0x0D 0x05 0x16 0x00 0x00 0x00 0x00 }
```

Muunnin tarkistaa ID:t, DLC:n, kaikki tavut ja aikaleimojen järjestyksen.
Ensimmäinen aikaleima siirretään nollaan; muut suhteelliset ajat säilyvät.
Samalla aikaleimalla olevien viestien järjestys säilyy. Tallennetta ei järjestetä
uudelleen eikä seitsemän tavun viestejä pidennetä kahdeksaan tavuun.

Mukana oleva ote sisältää 49 peräkkäistä raakaviestiriviä alkuperäisen tallenteen
riveiltä 126–174. Kesto on 1805 ms; viimeisen viestin ja seuraavan kierroksen
ensimmäisen väliksi valittiin esimerkissä 190 ms. Tämä silmukan rajaväli on
testiasetus. Alkuperäisessä kokonaislogissa on ajattomia SDO-rivejä, puuttuvia
EMCY-payload-tietoja ja adapterin teksti-ilmoituksia; se pitää rajata sopiviin
tallennusjaksoihin ennen häviötöntä toistoa. `--skip-decoded-events` sallii
EMCY-tekstirivien ohituksen **ilmoittaen ohitettujen rivien määrän**. Se ei korjaa
SDO-aikaleimoja. `--ignore-nonframes` sallii muun tekstimuotoisen metadatan
ohituksen, mutta ei rikkinäisiä `0x`-alkuisia viestirivejä.

```powershell
python tools/can_to_inc.py oma.csv include/recording.inc --loop-gap-ms 50
```

Raja on oletuksena 100000 viestiä (`--max-frames`). Tallenne sijoitetaan
`constexpr`-taulukkona ESP32:n flash-muistiin, eikä sitä kopioida työmuistiin.
Tämä rakenne käyttää tällä työkaluketjulla noin 20 tavua/viesti. Todellinen
enimmäiskoko riippuu firmware-osiosta; kääntäjä tarkistaa lopullisen koon.
Tallenteen suhteellinen enimmäiskesto on uint32-millisekuntia, noin 49,7 päivää.
Tallenne pitää kääntää ja ladata firmwareen uudelleen muunnoksen jälkeen.

## Signaaliprofiilin laajentaminen

Muokkaa `config/vehicle.json` ja suorita `generate_profile.py`. Kullekin viestille
määritetään ID, DLC, oletustavut, lähetysväli, tarkistussumma ja protokollalähde.
Signaalilla on nimi, ensimmäinen tavu, leveys 1–4 tavua, tavujärjestys,
etumerkillisyys, kerroin, offset, fyysiset rajat ja oletusarvo.

Koodaus on `raw = round((physical - offset) / scale)`. Lähin esitettävä arvo
lähetetään; esimerkiksi SOC:n 0,5 % resoluutio vaatii kertoimen 0.5, jos se
vahvistetaan protokollasta. Bitin sisäisiä kenttiä ei vielä ole: niitä voi muokata
raakana tai lisätä erillisen tarkistetun koodauksen. Päällekkäiset kentät ja
esittämättömät rajat hylätään. Nimet ovat yksilöllisiä ja isoilla kirjaimilla.

`rate_per_second` ohjaa AUTO-tilan lineaarista muutosta. Positiivinen arvo
kasvattaa ja negatiivinen vähentää arvoa sekunnissa; minimiin tai maksimiin
saavuttaessa arvo jää rajalle. Näin voidaan määrittää RPM-nousu, SOC-lasku ja
virran muutos heti kun niiden autonkohtainen koodaus on vahvistettu.
Oletusprofiilin NMT_STATE-arvo ei muutu AUTO-tilassa.

`checksum="sum_id_dlc"` on **vain erikseen vahvistettavaa protokollaa varten**:
kahdeksannen tavun arvoksi tulee `(CAN_ID + DLC + sum(bytes[0:7])) & 0xFF`.
Generaattori vaatii tällöin `checksum_source`-kentän. Oletusprofiilissa algoritmia
ei käytetä. Excelin solulähetteen kuvausta ei ole yleistetty BMS-viesteihin.

C++-rajapinta on `Simulator::add`, `setMessage`, `setByte`, `setSignal`,
`setBatterySOC`, `setRPM`, `setInterval`, `start`, `stop`, `setMode`, `load`,
`restart` ja `tick`. `Transport::offer` erottaa viestin muodostuksen laitteistosta.
Laitteisto ilmoittaa jonotetun toistoviestin tuloksen `transmitResult`-metodilla
ennen seuraavaa toistoviestiä. Rajapinta on yhden pääsilmukan käyttöön; sitä ei
pidä kutsua samanaikaisesti keskeytyksestä tai toisesta FreeRTOS-tehtävästä.

## Ajoitus, virheet ja kapasiteetti

- Ajastus käyttää ESP32:n 64-bittistä monotonista kelloa. `millis()`-ylivuoto
  ei katkaise pitkää ajoa.
- Sykliset viestit käyttävät vaiheeseen sidottuja määräaikoja ja tasapuolista
  vuorottelua. Myöhästyneitä syklisiä kierroksia ohitetaan; `cyclic_skipped`
  kertoo määrän. Vanhentuneita viestejä ei lähetetä purskeena.
- Toistosta ei pudoteta viestejä puskurin täyttyessä. Aikataulusta myöhästyneet
  viestit lähetetään järjestyksessä; `replay_late` laskee jonotushetken myöhästymiset.
  Aikataulu on millisekuntitasoinen tavoite. Väylän arbitrointi ja kuorma voivat
  viivästyttää todellista lähetystä.
- Käytössä on yksi CAN-lähetyspuskuri viestijärjestyksen säilyttämiseksi.
  `TX_QUEUED` on hyväksyttyjen lähetyspyyntöjen määrä ja `TX_OK` ohjaimen
  onnistuneiksi ilmoittamien määrä. Loopback-tilassa TX_OK ei tarkoita ulkoista ACK:ta.
- Bus-off tai yli 100 ms odottava lähetys pysäyttää ajon. Korjaa syy ja käytä
  `RECOVER`, sitten `PLAY`/`START`. Epäonnistunut toistoviesti jää uudelleenyritykseen.
  Ohjain voi yrittää samaa kehystä uudelleen aikakatkaisuun asti.
  Jos ohjain ei vastaa peruutukseenkaan, 200 ms:n kohdalla ohjelmiston
  lähetyspyyntö vapautetaan virheellisenä ja ohjain merkitään alustamattomaksi,
  jotta `RECOVER` voi yrittää laitteiston resetointia.
- RX tarkistetaan jokaisella pääsilmukan kierroksella, ja MCP2515:n molemmat
  vastaanottopuskurit ovat käytössä. Ylivuodot lasketaan. Suurin häviötön
  kuorma pitää mitata oikealla laitteistolla; yhden megabitin nimellisnopeus
  ei tarkoita, että täysi väyläkuorma voitaisiin tulostaa sarjaporttiin.
- Sarjatulostus käyttää kiinteää 8192 tavun jonoa, josta 1024 tavua varataan
  vastauksille. UART:iin kirjoitetaan vain vapaan puskuritilan verran. Monitorin
  ylikuorma pudottaa **tulosterivejä**, ja `monitor_drop` näyttää määrän.
  Myös vastausten mahdollinen hävikki näkyy `reply_drop`-laskurissa. Asetukset
  ja viestimuisti ovat kiinteäkokoisia: 32 viestiä ja 64 signaalia.
- Tarkka bittiajoitus lasketaan 8/16/20 MHz:n kiteelle. Mahdoton yhdistelmä,
  kuten 8 MHz ja 1 Mbit/s vähintään kahdeksalla aikakvantilla, hylätään.
  Näytepiste näkyy `STATUS`-tulosteessa; pitkän kaapeloinnin ajoitus on mitattava.

## Testit ja rakenne

```powershell
python tools/check.py
```

Tarvitsee Python 3.10+:n ja C++17-kääntäjän. `--cxx POLKU` valitsee g++/clang++/Zig-
kääntäjän. Tässä työtilassa mukana oleva paikallinen Zig löytyy automaattisesti.
Python-työkaluilla ei ole ulkopuolisia riippuvuuksia. PC-testit eivät tarvitse
Arduino-kirjastoja tai laitetta.

| Polku | Sisältö |
|---|---|
| `src/main.cpp` | ESP32:n pääsilmukka, SPI ja sarjaportti |
| `include/can_core.h` | Viestit, signaalit, ajastus, automatiikka ja toisto |
| `include/mcp2515.h` | Ohjain, bittiajoitus ja lähetys-/vastaanottotila |
| `include/commands.h` | Validoitu komentoparseri |
| `include/output_queue.h` | Rajattu, odottamaton tulostusjono |
| `config/vehicle.json` | Muokattava lähde viestimäärityksille |
| `include/vehicle_profile.h` | Generoitu profiili |
| `include/recording.inc` | Generoitu tallenne |
| `examples/motor_excerpt.txt` | Alkuperäisestä tallenteesta otettu esimerkki |
| `tools/` | Muuntimet, testiajo ja paikallisen PlatformIO:n käynnistys |
| `tests/` | C++- ja Python-testit |

MCP2515-ajuri perustuu [Microchipin datalehteen DS20001801K](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP2515-Family-Data-Sheet-DS20001801K.pdf),
erityisesti lähetys-, vastaanotto-, bittiajoitus- ja SPI-rekisterien kuvauksiin.
Muut tekniset lähteet:
[ESP32 SPI](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/spi.html),
[ESP32 UART](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html) ja
[PlatformIO esp32dev](https://docs.platformio.org/en/stable/boards/espressif32/esp32dev.html).
