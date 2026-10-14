# Onno Plugins

Eigene Audio-Plugins (VST3, auf dem Mac auch AU) mit C++ und JUCE 8. Sie laufen in Reaper und in jeder anderen DAW.

| Plugin | Inhalt | Stand |
|---|---|---|
| Onno VC-1 | VCA-Kompressor, Ansichten Konsole und Modern | portiert vom JSFX, klanglich identisch, pluginval Stufe 7 bestanden |
| Onno CS-1 | Channel Strip: Trim/Drive, HPF/LPF, Gate, 4-Band-EQ, Kompressor, De-Esser; Quell-Presets, Auto-Trim, Auto-Gain, A/B | 33 Messungen bestanden, pluginval Stufe 7 bestanden |

## CS-1: schneller Workflow

- **Quell-Presets** (KICK, SNARE, TOMS, BASS, RHY GTR, LEAD GTR, VOCAL, SCREAM, OVERHEAD, BUS): ein Klick setzt alle Module auf einen guten Startpunkt für Metal. Trim und Output bleiben dabei stehen.
- **AUTO TRIM**: anklicken, Spur abspielen. Nach 3 s Signal steht der Trim so, dass der Pegel bei -18 dBFS RMS liegt.
- **AUTO GAIN**: gleicht die Lautheit am Ausgang an den Eingang an. So klingt eine Einstellung nicht nur besser, weil sie lauter ist.
- **A / B**: zwei Einstellungen vergleichen. B startet als Kopie von A.
- Modul-Titel (GATE, EQ, COMP, DE-ESS) anklicken schaltet das Modul an oder aus. PRE EQ setzt den Kompressor vor den EQ.
- Bei jedem Regler: ziehen (Shift = fein), Mausrad, Doppelklick = Standardwert. Das Display zeigt den genauen Wert.

## Aufbau

- `Source/Common/OnnoLook.h`: gemeinsamer Look (3D-Regler, Chrom-Schalter, Frontplatte, Modern-Teile). Jedes neue Plugin nutzt ihn.
- `Source/Common/OnnoDSP.h`: gemeinsame Filter (Biquads).
- `Source/VC1/`, `Source/CS1/`: Prozessor (Klang) und Editor (Oberfläche) je Plugin.
- `Tests/Tests.cpp`: misst den Klang und rendert die Oberfläche als PNG, ohne DAW.
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
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Onno VC-1.vst3"`

## Lizenz

JUCE ist für private Nutzung und kleine Firmen (unter 50.000 $ Umsatz im Jahr) kostenlos.
