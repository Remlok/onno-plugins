# Onno Plugins

Eigene Audio-Plugins (VST3, auf dem Mac auch AU) mit C++ und JUCE 8. Sie laufen in Reaper und in jeder anderen DAW.

| Plugin | Inhalt | Stand |
|---|---|---|
| Onno VC-1 | VCA-Kompressor, Ansichten Konsole und Modern | portiert vom JSFX, klanglich identisch, pluginval Stufe 7 bestanden |

## Aufbau

- `Source/Common/OnnoLook.h`: gemeinsamer Look (3D-Regler, Chrom-Schalter, Frontplatte, Modern-Teile). Jedes neue Plugin nutzt ihn.
- `Source/VC1/`: Prozessor (Klang) und Editor (Oberfläche) des VC-1.
- `Tests/Tests.cpp`: misst den Klang und rendert die Oberfläche als PNG, ohne DAW.
- `.github/workflows/build.yml`: baut auf GitHub automatisch die Windows- und Mac-Version und prüft sie mit pluginval.

## Bauen

Am einfachsten über GitHub: Projekt in ein Repository legen, dann baut jeder Push die Plugins. Die fertigen Dateien liegen unter *Actions → letzter Lauf → Artifacts*.

Lokal (braucht CMake und unter Windows Visual Studio 2022, auf dem Mac Xcode):

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

JUCE wird dabei automatisch geladen. Die fertige Datei liegt in `build/OnnoVC1_artefacts/Release/VST3/Onno VC-1.vst3`.

## Installieren

- Windows: `Onno VC-1.vst3` nach `C:\Program Files\Common Files\VST3\` kopieren.
- Mac: `Onno VC-1.vst3` nach `~/Library/Audio/Plug-Ins/VST3/` kopieren (AU: `.component` nach `~/Library/Audio/Plug-Ins/Components/`).
- In Reaper: *Options → Preferences → Plug-ins → VST → Re-scan*.

Auf dem Mac sind die Plugins nicht signiert. Beim ersten Laden kann macOS sie blockieren; dann einmal im Terminal ausführen:
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Onno VC-1.vst3"`

## Lizenz

JUCE ist für private Nutzung und kleine Firmen (unter 50.000 $ Umsatz im Jahr) kostenlos.
