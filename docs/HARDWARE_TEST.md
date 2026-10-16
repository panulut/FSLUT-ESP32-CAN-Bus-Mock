# Laitteiston hyväksymistestaus

Nämä testit on tarkoitettu suoritettaviksi oikealla ESP32 + MCP2515 + TJA1051
-kokoonpanolla. Niitä ei ole vielä suoritettu. Kirjaa kortti, moduulin kide,
transceiverin tarkka versio, pinnit, ohjelmistoversio, adapteri ja havaintojen
aikaleimat. Käytä erillistä testipenkkiä.

## 1. Sähköinen tarkistus ja käynnistys

1. Tarkista jännitteet ja logiikkatasot osien omista datalehdistä. ESP32:n
   tuloon ei saa tulla 5 V. Tarkista CS-pinnin käynnistysvaikutus.
2. Tarkista yhteinen maareferenssi ja CANH/CANL. Jännitteettömän väylän
   päätevastusten yhteisvastus on tyypillisesti noin 60 ohmia, kun kaksi
   120 ohmin vastusta on kytketty oikeisiin päihin.
3. Aseta `board_config.h`:n SPI-pinnit ja oskillaattorin taajuus.
   Käännä ja lataa `esp32dev`-firmware.
4. Avaa sarjaportti 115200 baudilla. Lähetä `STATUS`.
   Odotus: `running=0`, `ready=1`, `fault=0`, `pending=0`.
5. Tarkista ulkoisella adapterilla, ettei ohjelma käynnistyessä lähetä omia
   CAN-kehyksiä. Kuuntelutila ei myöskään kuittaa muiden kehyksiä.

## 2. Ohjaimen sisäinen loopback

```text
CONFIG 500000 16000000 LOOPBACK
MONITOR ON
START
```

Vaihda oskillaattoriluku todellisen moduulin mukaan. Odotus: noin kerran
sekunnissa `0x701`, DLC 1, data `05`; RX- ja TX-laskurit kasvavat. Loopback
testaa sisäisen ohjainpolun, ei transceiveria tai väylän ACK:ta.

```text
SET_SIGNAL NMT_STATE 127
INTERVAL 0x701 100
STATUS
STOP
```

Odotus: data muuttuu `7F`:ksi, väli noin 100 ms. STOPin jälkeen oma syklinen
lähetys päättyy; jo väylällä oleva kehys voi vielä valmistua. Odota
`pending=0` ennen seuraavaa alustusta.

## 3. Normaali väylä ja vastaanotto

1. Kytke toinen aktiivinen CAN-solmu tai lähetys-/ACK-tilassa toimiva adapteri.
   Pelkkä listen-only-adapteri ei riitä ACK-kuittaukseen.
2. `CONFIG 1000000 16000000 NORMAL`, `MONITOR ON`, `START`.
3. Tallenna ulkoisella adapterilla ID, DLC, tavut ja aikaleimat. Odotus:
   `0x701` ja nykyinen asetettu arvo; virheettömällä väylällä `TX_OK` kasvaa
   eikä `TX_FAILED` kasva. STOPin jälkeen jonon tyhjennyttyä
   `TX_QUEUED = TX_OK + TX_FAILED`.
4. Lähetä adapterilta 11-bittinen 0x301 / DLC 7 ja tunnetut tavut.
   Odotus: RX-tulosteessa juuri 7 tavua, ei lisättyä kahdeksatta tavua.
5. Kokeile myös 29-bittistä kehystä sekä RTR-kehystä. Odotus: oikea ID,
   EXT/STD ja RTR-merkintä; RTR:stä ei näytetä keksittyä payloadia.
6. `FILTER 0x301`: vain sen monitoririvit näkyvät, mutta RX-laskuri laskee
   edelleen kaiken vastaanotetun liikenteen. `FILTER OFF` palauttaa tulostuksen.

## 4. Tallenteen järjestys, tauotus ja ajoitus

```text
STOP
MODE PLAYBACK
RESTART
LOOP OFF
MONITOR TX ON
PLAY
```

Odota, että viimeinenkin lähetys valmistuu. Ulkoisessa tallennuksessa pitää
olla 49 datakehystä samassa järjestyksessä ja samalla DLC:llä ja payloadilla
kuin `examples/motor_excerpt.txt`:ssä. Tavoitekesto ensimmäisestä viimeiseen
on 1805 ms; mittaa poikkeama ja ilmoita mittausmenetelmä. USB-/SPI-pollaus,
arbitrointi ja toinen solmu voivat kasvattaa viivettä. Tarkista, ettei yksikään
0x301-viesti muutu seitsemästä tavusta kahdeksaan.

- `LOOP ON`, `RESTART`, `PLAY`: tarkista kierrosten rajaväli. Toimitetussa
  esimerkissä viimeisestä seuraavaan ensimmäiseen on tavoiteltu 190 ms.
- `PAUSE`: ajastus pysähtyy, mutta jo jonossa oleva viesti saa valmistua.
  `PLAY` jatkaa seuraavasta viestistä säilyttäen jäljellä olevan odotusajan.
- `STOP` kesken jonossa olevan viestin: peruutuksen onnistuessa viesti
  säilyy uudelleenyrityksessä. Jo onnistuneeksi kuitattua viestiä ei yritetä uudelleen.
- `PAUSE`, odota `pending=0`, `RESTART`, `PLAY`: toisto alkaa alusta.
- Kuormita väylää toisella solmulla. Tarkista, että järjestys säilyy,
  `replay_late` kertoo myöhästymisistä eikä ruuhka pudota toistoviestejä.
  ACK-virhe tai 100 ms:n aikakatkaisu pysäyttää toiston; se on erillinen virhetesti.

## 5. Sarjaliikenteen virheet ja kuormitus

1. Lähetä `SET 0x701 0 256`, `INTERVAL 0x701 0`, `SET_SOC 75.5`,
   `FRAME 0x701 1 0xGG` ja yli 191 merkin rivi. Odotus: `ERR` ja aiempi data
   säilyy. `SET_SOC` epäonnistuu oletusprofiilissa, koska määritys puuttuu.
2. Kokeile `MODE AUTO` lähetyksen aikana. Odotus: `ERR`; pysäytyksen jälkeen
   tilan vaihto onnistuu.
3. Kokeile `CONFIG 1000000 8000000 NORMAL`. Odotus: `ERR`, koska ajoitusta
   ei voi muodostaa sallituilla aikakvanteilla. Vanha konfiguraatio säilyy.
4. Lähetä LF-, CR- ja CRLF-rivinvaihdoilla. Kunkin komennon pitää toteutua kerran.
5. Kuormita RX:ää ja pidä monitorointi päällä. Mittaa lähetysten ajoitus.
   Hidas sarjaportti saa pudottaa monitoririvejä (`monitor_drop` kasvaa),
   mutta pääsilmukka ei saa pysähtyä tulostukseen. Seuraa myös `RX_OVERRUN`-
   laskuria; MCP2515:n kahdesta RX-puskurista johtuva yläraja pitää mitata.
6. Vertaa kuormitusta monitorointi pois/päällä ja kirjaa suurin häviötön
   vastaanottotaajuus tällä kytkennällä. Tarkista, että komennot saavat vastauksen.

## 6. ACK:n puuttuminen, bus-off ja palautuminen

1. Irrota toinen solmu tai vaihda se listen-only-tilaan ja käynnistä normaali
   lähetys. Odotus: enintään 100 ms:n TX-odotuksen jälkeen `fault=1`,
   `running=0`, epäonnistumislaskuri kasvaa. Bus-off voi laueta jo tätä ennen;
   ACK-virheet voivat myös jäädä error-passive-tilaan ennen aikakatkaisua.
2. Tarkista `TEC`, `REC`, `EFLG` ja ulkoinen väylämonitorointi.
3. Palauta oikea nopeus ja kuittaava solmu. Odota `pending=0`, käytä
   `RECOVER` ja sitten `START` tai `PLAY`. RECOVER ei aloita lähetystä itsestään.
4. Varmista toistossa, että virheellinen/peruutettu viesti yritetään uudelleen
   samasta kohdasta. Uudelleenkäynnistys palauttaa sen sijaan asetukset ja toiston alkuun.

## 7. Pitkä ajo ja hyväksymisen kirjaus

Aja useita tunteja edustavalla liikennekuormalla. Kirjaa väylän kuorma,
syklisen lähetyksen ajastuspoikkeamat, toiston myöhästymiset, RX-ylivuodot ja
UART-tulostuksen pudotukset. Jos lisäät fyysisiä BMS-/moottorisignaaleja,
vertaa niiden raakakoodaus riippumattomaan valmistajan määrittelyyn tunnetuilla
negatiivisilla, positiivisilla ja raja-arvoilla. Vasta onnistuneiden laitteisto-
ja protokollatestien jälkeen kokoonpanoa voi pitää kyseiseen testipenkkiin hyväksyttynä.
