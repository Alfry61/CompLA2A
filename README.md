# ALAP Opto Leveler

Emulazione di un compressore/livellatore ottico in stile **LA-2A**, sviluppata da
**Al Learned Audio Productions (ALAP)** in C++ con **JUCE 8**.
Disponibile come plugin **VST3** e come **applicazione autonoma** per Windows.

<p align="center">
  <img src="Resources/ALAP_Logo.png" alt="Logo ALAP" width="220">
</p>

---

## Caratteristiche

- **Cella ottica T4B modellata su due stadi**
  - attacco fisso di circa 10 ms;
  - rilascio rapido (~60 ms) e rilascio lento (da 0,5 a ~3,5 s);
  - **memoria della cella**: piu' a lungo si comprime, piu' lento diventa il rilascio
    e maggiore il peso della parte lenta (dal 30% al 60%), come nell'apparecchio originale.
- **Curva di compressione 4:1 a ginocchio morbido** (soglia -10 dB, ginocchio 10 dB).
- **Stadio valvolare** con funzione di trasferimento asimmetrica: 3a armonica dominante
  e una leggera 2a armonica; la distorsione cresce in modo naturale con il livello.
- **Sovracampionamento 4x** (filtri IIR polifase a mezza banda) per limitare l'aliasing
  della saturazione; latenza dichiarata correttamente alla DAW.
- **Filtro anti-componente continua** (~5 Hz) dopo la valvola.
- **VU-meter analogico** con balistica realistica e tre modalita' di lettura.
- **Preset di fabbrica e preset utente** (salva, rinomina, elimina, importa, esporta).
- **Pagina About** accessibile cliccando sul logo.

## Controlli

| Controllo          | Intervallo                              | Funzione |
| ------------------ | --------------------------------------- | -------- |
| **PEAK REDUCTION** | 0 – 100                                 | Quantita' di compressione: amplifica il segnale che illumina la cella ottica. |
| **GAIN**           | 0 – 100                                 | Guadagno d'uscita. Precede la valvola: alzandolo si aumenta anche la saturazione. |
| **VU METER**       | Output +10 / Output +4 / Gain Reduction | Cosa mostra lo strumento: livello d'uscita (due tarature) o riduzione di guadagno. |

## Preset di fabbrica

| Preset                            | Uso |
| --------------------------------- | --- |
| Iniziale                          | Punto di partenza neutro |
| Voce - livellamento dolce         | ~4 dB di riduzione |
| Voce - in primo piano             | ~7 dB di riduzione |
| Voce parlata / podcast            | ~6 dB di riduzione |
| Basso - controllo                 | ~5 dB di riduzione |
| Chitarra acustica                 | ~3 dB di riduzione |
| Pianoforte                        | ~2,5 dB di riduzione |
| Bus batteria - coesione           | ~3 dB, piu' colore valvolare |
| Batteria - compressione estrema   | ~12 dB di riduzione |
| Bus mix - tocco leggero           | ~1,5 dB di riduzione |
| Solo colore valvolare             | Nessuna compressione, solo saturazione |

I preset di fabbrica sono esposti alla DAW anche come "programmi".
I preset utente (estensione `.alappreset`) vengono salvati in:

```
Documenti\Al Learned Audio Productions\ALAP Opto Leveler\Preset
```

---

## Compilazione

### Requisiti

- Windows 10/11 a 64 bit
- **Visual Studio 2022** (o successivo) con il carico di lavoro "Sviluppo di applicazioni desktop con C++"
- **CMake 3.22** o successivo
- **JUCE 8** (facoltativo: se non e' presente in locale viene scaricato automaticamente, versione 8.0.14)
- **SDK ASIO di Steinberg** (facoltativo, solo per l'applicazione autonoma)

### Reperimento di JUCE

Il file `CMakeLists.txt` cerca JUCE in quest'ordine:

1. la cartella indicata da `JUCE_LOCAL_DIR` (predefinita `F:/_AI_PROJECTS/SDKs/JUCE`);
2. una cartella `JUCE/` accanto al progetto;
3. altrimenti lo scarica da GitHub.

Per indicare un percorso diverso:

```bash
cmake -S . -B build -DJUCE_LOCAL_DIR="C:/percorso/di/JUCE"
```

Allo stesso modo `ASIO_SDK_DIR` indica la cartella dell'SDK ASIO; se manca, l'applicazione
autonoma usera' solo WASAPI/DirectSound.

### Script `build.bat`

Da PowerShell, nella cartella del progetto:

```bash
.\build.bat
```

| Opzione   | Effetto |
| --------- | ------- |
| *(nessuna)* | Compila in Release |
| `debug`   | Compila in Debug |
| `clean`   | Cancella la cartella `build` e ricompila |
| `test`    | Compila ed esegue il programma di prova |
| `install` | Copia il VST3 in `C:\Program Files\Common Files\VST3` (richiede privilegi di amministratore) |

Le opzioni si possono combinare, per esempio `.\build.bat clean test`.
Per un'altra versione di Visual Studio impostare prima la variabile `GENERATOR`
(per esempio `Visual Studio 18 2026`).

I risultati si trovano in:

```
build\ALAPOptoLeveler_artefacts\Release\VST3\ALAP Opto Leveler.vst3
build\ALAPOptoLeveler_artefacts\Release\Standalone\ALAP Opto Leveler.exe
```

---

## Programma di prova

`build.bat test` compila ed esegue `ALAPOptoTests.exe`, che verifica il DSP fuori dalla DAW
e scrive i dati in formato CSV nella cartella `risultati_test/`. Le prove comprendono:

- **curva statica**: riduzione attesa e misurata a vari livelli d'ingresso;
- **attacco** a diverse frequenze (63% e 90% della riduzione);
- **rilascio e memoria della cella** (impulso breve contro compressione prolungata);
- **armoniche** dello stadio valvolare (H2…H5, THD);
- **aliasing** delle armoniche oltre la banda udibile;
- **stabilita'** (picchi d'uscita, coda del silenzio);
- **prestazioni** (volte il tempo reale, carico per istanza, latenza dichiarata);
- **preset** di fabbrica e utente (riduzione attesa, salvataggio e ricaricamento).

Al termine viene mostrato il riepilogo delle prove superate, non superate e saltate;
se qualche prova fallisce lo script termina con codice d'errore 2.

---

## Struttura del progetto

```
CompLA2A/
├── CMakeLists.txt        configurazione di compilazione
├── build.bat             script di compilazione per Windows
├── AGENTS.md             regole del progetto
├── Resources/
│   └── ALAP_Logo.png     logo incorporato nel plugin
├── source/
│   ├── PluginProcessor.* DSP: rivelatore, curva, cella T4B, valvola, sovracampionamento
│   ├── PluginEditor.*    interfaccia: manopole, VU-meter, LookAndFeel d'epoca
│   └── PresetManager.*   preset di fabbrica e utente
└── Tests/
    └── TestMain.cpp      programma di prova da riga di comando
```

## Note tecniche

- Nel `processBlock` non avvengono allocazioni di memoria, lock, chiamate di sistema o log.
- I parametri passano tramite `AudioProcessorValueTreeState` e sono addolciti con `SmoothedValue`.
- Runtime MSVC statico: il plugin non richiede i redistribuibili di Visual C++.
- Canali collegati: il rivelatore lavora sul massimo tra i canali, l'immagine stereo resta stabile.

---

## Licenza

Copyright (C) 2026 Al Learned Audio Productions

Questo progetto e' distribuito con licenza **GNU Affero General Public License v3.0**
(AGPLv3): vedere il file [LICENSE](LICENSE).

La scelta deriva da JUCE 8, che nella versione open source e' distribuito proprio con
licenza AGPLv3: chiunque distribuisca il plugin, anche modificato, deve rendere
disponibile il codice sorgente con la stessa licenza.
I componenti esterni hanno licenze compatibili: l'SDK VST3 di Steinberg e' distribuito
con licenza MIT, l'SDK ASIO con licenza GPLv3 (oppure con licenza proprietaria di Steinberg).

## Crediti

Sviluppato da **Al Learned Audio Productions**.
Realizzato con [JUCE](https://juce.com). VST e' un marchio registrato di Steinberg Media Technologies GmbH.

*LA-2A e' un marchio dei rispettivi proprietari, citato solo come riferimento:
questo progetto non e' affiliato ne' approvato da essi.*
