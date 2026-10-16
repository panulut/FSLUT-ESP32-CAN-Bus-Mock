# Protokollan rajaus ja avoimet tiedot

Lähteet ovat alkuperäinen `CAN_SIM.md`, `CAN SIM/CAN SIM/CAN.xlsx` ja saman
kansion `Some CAN DATA FROM MOTOR to use as mock data.txt`. Alkuperäisiä tiedostoja
ei ole muutettu. Excelin solujen täyttövärit luettiin suoraan tiedoston
tyyli- ja teemamäärityksistä: vihreä on accent6 `70AD47` vaalennettuna.

| Viestit | Excelin Name-sivu | Toteutuksen päätös |
|---|---|---|
| 0x701 | A7:J7, vihreä, NMT state; 05 operational / 7F pre-operational | Profiilissa: DLC 1, raakabittiä muuttamaton NMT_STATE. DLC varmistuu tallenteesta. Väli 1000 ms on bench-oletus. |
| 0x500 | A18:I18, vihreä; insulator heartbeat ja kaksi CAN-virhetilaa | Ei oletusviestiä. Vahvistettava DLC, varatut tavut, virhetilojen arvot ja lähetysväli. Käyttäjä voi määrittää raakaviestiksi. |
| 0x154, 0x128, 0x294, 0x301, 0x298 | A2:I6, pääosin keltainen; sisältö tyhjä | Vain raaka-/tallennetoisto. Tallenteessa on 0x297, kun taulukossa on 0x294: niitä ei samasteta. |
| 0x6B0 | A8:I8, sininen | Nykyinen virta 0,1 A, jännite 0,1 V ja SOC 0,5 % näkyvät tekstissä. Tavujärjestys, etumerkki, relay-kentän leveys ja checksum puuttuvat. Ei fyysistä profiilia. |
| 0x6B1 | A9:I9, sininen, DCL/CCL-alueella punainen huomautus | Yksikkö epäselvä jo lähteessä: 0,1 kW vai 1 A. Ei fyysistä profiilia. |
| 0x6B2 | A10:I10, sininen | Solujännitteiden kerroin 0,0001 V ja ID-tavut näkyvät, mutta tavujärjestys, checksum ja ID-numerointi puuttuvat. |
| 0x6B3 | A11:I11, sininen | Lämpötilojen 1 C kerroin näkyy. Etumerkki/offset ja checksum puuttuvat. |
| 0x6B4 | A12:I12, harmaa; Explanation-sivulla sininen | Tavujen sisältö puuttuu. Ei oletusviestiä. |
| 0x036, 0x076 | A13:I14, vaaleansininen; 0x036:n checksum-solu vihreä | Solu- ja termistorilähetteitä. Pelkkä vihreä checksum-solu ei tee koko viestistä vihreällä merkittyä vaatimuksen tarkoittamaa viestiä. |
| 0x600, 0x601 | A15:I16, sininen | Heartbeat-/vikaviestit on siirretty myöhemmäksi. |
| 0x007 | A17:I17, oranssi | Ei oletusviestiä. |

## Tarkistussummat

`Name!B30:B33` kuvaa **solulähetteen** summan: ID + pituus 8 + tavut 0–6,
jonka tulos rajataan tavuun. Kohdetta ei ole luotettavasti laajennettu kaikkiin
taulukon CHECKSUM-kenttiin. BMS:n valmistajaa, mallia, konfiguraatiota tai
virallista viestikarttaa ei toimitettu. Oletusprofiili ei siksi generoi
tarkistussummallisia BMS-viestejä.

Yleinen `sum_id_dlc`-algoritmi on testattu synteettisellä vertailuvektorilla.
Se on käytettävissä vasta kun profiiliin on kirjattu algoritmin vahvistava
`checksum_source`. Tämä kenttä dokumentoi käyttäjän tarkistuksen; generaattori
ei pysty itsenäisesti todistamaan annetun lähteen sisältöä oikeaksi.

## Mitä tarvitaan BMS-/moottorisignaalien käyttöönottoon

Jokaiselle viestille tarvitaan valmistajan viestikartta, DBC tai muu vahvistettu
määrittely: ID ja kehystyyppi, DLC, bittien/tavujen sijainti ja järjestys,
etumerkillisyys, skaala ja offset, sallitut arvot, lähetysväli sekä mahdollinen
checksum/counter-algoritmi. Vahvista vähintään yksi tunnettu fyysinen arvo ja
sitä vastaava raakakehys riippumattomalla dekooderilla.

Kun tiedot ovat käytettävissä, lisää ne `config/vehicle.json`-tiedostoon,
generoi profiili uudelleen ja testaa tunnettuja vertailukehyksiä vastaan.
Nimet `SOC` ja `RPM` ottavat vastaavat sarjaporttialiakset käyttöön automaattisesti.
Koodi ei sisällä oletusta, että tehtävänannon esimerkki-API olisi auton
vahvistettu viestikartta.

## Tallenteen eheys

Alkuperäinen loki sisältää 0-aikaleimaisia SDO-lähetyksiä muiden aikaleimojen
välissä sekä tekstiksi purettuja EMCY-rivejä ilman raakapayloadia. Koko lokin
täydellinen ajoitettu toisto ei ole mahdollista näistä tiedoista arvaamatta.
Muunnin hylkää nämä oletuksena. Toimitettu `examples/motor_excerpt.txt` on
alkuperäisten rivien 126–174 muuttamaton ote kommenttiotsaketta lukuun ottamatta.
Testi vertaa otetta alkuperäiseen lähteeseen ja generoitua dataa muuntimen tulokseen.

LoRa-hakemiston `.inc`-esimerkkiä ei käytetä toiston lähteenä: siinä alkuperäisiä
aikaleimoja ja DLC-tietoja ei ole. Seitsemän tavun `0x301`-kehys säilyy uudessa
muuntimessa seitsemän tavun kehyksenä.
