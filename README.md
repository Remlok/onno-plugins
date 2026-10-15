# Onno Plugins

Eigene Audio-Plugins (VST3, auf dem Mac auch AU) mit C++ und JUCE 8. Sie laufen in Reaper und in jeder anderen DAW.

| Plugin | Inhalt | Stand |
|---|---|---|
| Onno VC-1 | VCA-Kompressor, Ansichten Konsole und Modern | portiert vom JSFX, klanglich identisch, pluginval Stufe 7 bestanden |
| Onno RV-1 | Algorithmischer Hall (Idee: Waves Atlas): 32 Programme in 8 Kategorien, Depth, Movement, Character, Ducking, Gated, Freeze | Nachhallzeit gemessen, alle Programme stabil, pluginval Stufe 7 bestanden |
| Onno CS-1 | Channel Strip: Trim/Drive, HPF/LPF, Gate, 4-Band-EQ, Kompressor, De-Esser; Quell-Presets, Auto-Trim, Auto-Gain, A/B | 33 Messungen bestanden, pluginval Stufe 7 bestanden |

### Moderne Suite (Nachbauten der Reaper-Stock-Plugins)

| Plugin | Vorbild | Inhalt |
|---|---|---|
| Onno EQ | ReaEQ | 8 Bänder (Low/High Cut 12/24/48 dB, Shelf, Bell, Notch, Band Pass), Kurve mit ziehbaren Knoten, Analyzer Pre/Post |
| Onno Comp | ReaComp | Threshold, Ratio bis unendlich, Knee, Attack, Release, Lookahead, RMS, Detektor-HPF, Sidechain, Mix, Auto Gain, Limit 0 dB; Kennlinie und Verlauf |
| Onno Gate | ReaGate | Threshold mit Hysterese, Attack, Hold, Release, Range, Lookahead, Detektor-HPF/LPF, Sidechain, Listen |
| Onno Delay | ReaDelay | 3 Taps mit Sync oder ms, Feedback, Pegel, Pan, Low/High Cut im Feedback, Ping-Pong; Dry/Wet, Ducking; Taps im Display ziehbar |
| Onno MultiComp | ReaXcomp | 4 Bänder mit Linkwitz-Riley-Weichen (Summe flach), je Band Threshold/Ratio/Attack/Release/Gain, Solo, Band aus; Analyzer und Gain Reduction pro Band |
| Onno Limit | ReaLimit | Brickwall mit 1,5 ms Lookahead: Threshold, Ceiling, Release; keine Spitze über der Ceiling |

### Eigene Werkzeuge

| Plugin | Inhalt |
|---|---|
| Onno Tame | Spektraler Resonanz-Zähmer (Idee: soothe): findet Frequenzen, die aus der eigenen spektralen Hüllkurve herausragen, und drückt sie dynamisch, Bin für Bin. Depth, Sharpness, Selectivity, Attack, Release, Bereich (Ränder im Display ziehen), Empfindlichkeitskurve mit 4 Knoten, 8 Metal-Profile (Rhythm Gtr, Lead Gtr, Scream, Clean Vox, Snare, Overheads, Bass, Mix Bus), Delta (nur das Entfernte hören), Stereo Link, Mix. Latenz 1024 Samples bei 44,1/48 kHz, wird dem Host gemeldet. |

**Analoge Klangfarbe** (Leiste ANALOG unten in allen sechs): CLEAN (aus), BRIT (Übertrager, dichte Bässe, weiche Höhen), CONSOLE (sauber, ungerade Obertöne, etwas Glanz), TUBE (Röhre, warme geradzahlige Obertöne), FET (1176-artig, bissig, Präsenz). DRIVE bestimmt, wie stark gesättigt wird; bei 35 % liegt der Klirr bei -12 dBFS um 2-4 %, leise Signale bleiben im Pegel gleich. Im Delay läuft jede Wiederholung erneut durch die Sättigung wie bei einem Bandecho, im Limit sitzt sie vor dem Limiter.

Alle sechs teilen sich eine Oberfläche: oben die große Anzeige zum Anfassen, unten die Regler. Ziehen (Shift = fein), Mausrad und Doppelklick (Standardwert) funktionieren überall gleich. Das Fenster lässt sich frei skalieren.

## CS-1: schneller Workflow

- **Quell-Presets** (KICK, SNARE, TOMS, BASS, RHY GTR, LEAD GTR, VOCAL, SCREAM, OVERHEAD, BUS): ein Klick setzt alle Module auf einen guten Startpunkt für Metal. Trim und Output bleiben dabei stehen.
- **AUTO TRIM**: anklicken, Spur abspielen. Nach 3 s Signal steht der Trim so, dass der Pegel bei -18 dBFS RMS liegt.
- **AUTO GAIN**: gleicht die Lautheit am Ausgang an den Eingang an. So klingt eine Einstellung nicht nur besser, weil sie lauter ist.
- **A / B**: zwei Einstellungen vergleichen. B startet als Kopie von A.
- Modul-Titel (GATE, EQ, COMP, DE-ESS) anklicken schaltet das Modul an oder aus. PRE EQ setzt den Kompressor vor den EQ.
- Bei jedem Regler: ziehen (Shift = fein), Mausrad, Doppelklick = Standardwert. Das Display zeigt den genauen Wert.

## RV-1: Hall

- **Programme**: Kategorie anklicken (HALL, PLATE, ROOM, CHAMBER, AMBIENCE, CHORUS, GATED, BIG), dann mit PREV/NEXT, Klick ins Display oder Mausrad über dem Display weiterblättern. Mix, 100% WET und Freeze bleiben beim Blättern stehen, damit du direkt im Mix vergleichen kannst.
- **DEPTH**: nah (viele frühe Reflexionen, hell) bis fern (mehr Hallfahne, dunkler). **MOVEMENT**: Modulation der Fahne. **CHARACTER**: dunkel bis hell.
- **DUCK**: Hall wird leiser, solange das Signal spielt, und blüht in den Pausen auf (gut für Vocals und Leads im dichten Metal-Mix).
- **100% WET** für den Einsatz auf einem Send, **FREEZE** hält den Hall endlos stehen, **ALGORITHM** wechselt den Algorithmus bei gleichen Reglern.
- Das Display zeigt die Hüllkurve: Pre-Delay, frühe Reflexionen und Abklingen bis -60 dB.

## Aufbau

- `Source/Common/OnnoLook.h`: gemeinsamer Look (3D-Regler, Chrom-Schalter, Frontplatte, Modern-Teile). Jedes neue Plugin nutzt ihn.
- `Source/Common/OnnoDSP.h`: gemeinsame Filter (Biquads).
- `Source/Common/ModernProcessor.h`, `ModernEditor.h`, `Analyzer.h`: Grundgerüst der modernen Suite (Parameter, Zustand, Sidechain, Regler, Anzeige, Spektrum).
- `Source/VC1/`, `Source/CS1/`, `Source/RV1/`: Prozessor (Klang) und Editor (Oberfläche) je Plugin.
- `Source/EQ/`, `Comp/`, `Gate/`, `Delay/`, `MultiComp/`, `Limit/`: die moderne Suite, je Plugin eine Datei.
- `Tests/Tests.cpp`: misst den Klang und rendert die Oberfläche als PNG, ohne DAW (`OnnoTests <Ordner> [EQ Comp ...]`).
- `.github/workflows/build.yml`: baut auf GitHub automatisch die Windows- und Mac-Version und prüft sie mit pluginval.

## Bauen

Am einfachsten über GitHub: Projekt in ein Repository legen, dann baut jeder Push die Plugins. Die fertigen Dateien liegen unter *Actions → letzter Lauf → Artifacts*.

Lokal (braucht CMake und unter Windows Visual Studio 2022, auf dem Mac Xcode):

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

JUCE wird dabei automatisch geladen. Die fertigen Dateien liegen in `build/<Plugin>_artefacts/Release/VST3/`.

## Installieren

- Windows: die `.vst3`-Dateien nach `C:\Program Files\Common Files\VST3\` kopieren.
- Mac: die `.vst3`-Dateien nach `~/Library/Audio/Plug-Ins/VST3/` kopieren (AU: `.component` nach `~/Library/Audio/Plug-Ins/Components/`).
- In Reaper: *Options → Preferences → Plug-ins → VST → Re-scan*.

Auf dem Mac sind die Plugins nicht signiert. Beim ersten Laden kann macOS sie blockieren; dann einmal im Terminal ausführen:
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Onno*.vst3`

Sidechain (Onno Comp, Onno Gate) in Reaper: Spur mit dem Plugin auf 4 Kanäle stellen, das Signal per Send auf Kanal 3/4 schicken und im Plugin DETEKTOR auf SIDECHAIN stellen.

## Lizenz

JUCE ist für private Nutzung und kleine Firmen (unter 50.000 $ Umsatz im Jahr) kostenlos.
