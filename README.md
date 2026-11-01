# KumAmp

Pedalera modular VST3 / AU / Standalone para macOS (Apple Silicon nativo), construida con JUCE 8.

## Compilar

```bash
brew install cmake ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/KumAmpTests
```

El plugin se copia automáticamente a `~/Library/Audio/Plug-Ins/{VST3,Components}`.
Opciones: `-DKUMAMP_UNIVERSAL=ON` (arm64 + x86_64), `-DKUMAMP_JUCE_PATH=/ruta/JUCE`, `-DKUMAMP_COPY_AFTER_BUILD=OFF`.

## Arquitectura

```
Source/
  Core/        Constantes, smoothing, delay de compensación, colas lock-free (AbstractFifo)
  Graph/       ParamStore, DSPBlock, GridModel, RenderPlan (compilador del grid), BlockFactory
  Scenes/      SceneBank (escenas A-D)
  DSP/         Pitch/ Routing/ Utility/  (+ carpetas reservadas para el roadmap)
  Engine/      GridEngine: puente mensaje <-> audio
  UI/          GridView, BlockPanel
  PluginProcessor / PluginEditor
Tests/         Tests del núcleo, sin JUCE
```

`KumAmpCore` (Graph, Scenes, DSP) no depende de JUCE, así que se puede testear y perfilar por separado.

### Hilos

| Hilo     | Responsabilidad |
|----------|-----------------|
| Mensaje  | Edita `GridModel`, crea y prepara bloques, guarda escenas y compila un `RenderPlan` inmutable |
| Audio    | `GridEngine::process`: adopta el plan nuevo con un `exchange` atómico. No reserva memoria, no bloquea y no libera |
| Basura   | Los planes retirados vuelven por una `juce::AbstractFifo` y se liberan en un `Timer` del hilo de mensajes |

Los parámetros de cada bloque son `std::atomic<float>`. La GUI, las escenas, las macros y la automatización escriben en ellos y el DSP los suaviza internamente.

### Grid

- Nodos `Input`, `Output`, `Block`, `Split` (2-4 ramas con nivel por rama) y `Mix` (nivel y pan por entrada, más master).
- El compilador descarta los nodos sin camino Input→Output, ordena el grafo topológicamente y asigna buffers según su vida útil. Un bloque procesa *in-place* cuando nadie más lee su entrada.
- Compensación de latencia: en cada Mixer se inserta un delay por entrada para que las ramas paralelas queden alineadas a la muestra. La latencia total se reporta al host.
- Los cambios de topología hacen un fundido de salida/entrada de ~5 ms. Los bloques se comparten entre planes, así que su estado (colas de delay, etc.) sobrevive.

### Escenas

Las escenas guardan valores y estado de bypass de cada bloque y nodo de ruteo. La topología es común a todas, así que cambiar de escena solo mueve objetivos suavizados y el cambio no corta el audio. La escena activa es un parámetro del host (`scene`). "Guardar escena" sobrescribe la escena activa con el estado actual.

### Pitch Shifter (WSOLA)

Funciona en el dominio del tiempo y es polifónico: no detecta tono, así que los acordes salen limpios. Dos cabezales de lectura sobre un buffer circular. Cuando hay que empalmar, la posición del cabezal nuevo se elige por correlación cruzada normalizada (±½ periodo de un Mi grave) y se hace un crossfade de igual ganancia entre los dos cabezales, ya en fase.

- Rango de -12 a +12 semitonos más cents. Admite glide continuo, tipo whammy (Macro 1 → Transpose, de 0 a +12).
- Latencia dinámica de entre 0 y ~20 ms (grano de 15 ms). A 0 st la señal sale sin procesar y la latencia es cero.
- Rubber Band (R3 Live Shifter) queda como alternativa futura: trabaja con un tamaño de bloque fijo, lo que añade latencia, y tiene licencia GPL o comercial.

### NAM (Core)

- Motor: [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) v0.6.0 (MIT). Lee archivos `.nam` de las versiones 0.5 a 0.7: WaveNet A1/A2 ("NAM2"), LSTM, ConvNet y SlimmableContainer.
- La carga se hace en un hilo aparte (`FileLoaderPool`): parseo, `Reset()` y prewarm. El modelo pasa al audio por `RTCrossfadeSlot` con un crossfade de 50 ms, sin clics.
- La inferencia es mono, como todas las capturas NAM. Si la tasa de muestreo del host no coincide con la del entrenamiento, `RateBridge` remuestrea (Hermite; está pendiente un FIR polifásico).
- Normalización a -18 dB usando el campo `loudness` de los metadatos.
- **Detección de perfil:** el campo `gear_type` de los metadatos decide el tipo. Si contiene `cab` o `full_rig` el perfil es *End-to-End*; con `amp`, `pedal`, `preamp` y similares es *Amp-Only*. El parámetro "Perfil" permite forzarlo (0 Auto, 1 Amp, 2 E2E).

### IR Loader (Cab)

- Dos IR estéreo (A y B), cada una con nivel y pan, más low/high cut y mezcla. La convolución usa `juce::dsp::Convolution` con latencia cero en una cola compartida.
- Las IR no se recortan, para no romper la relación de fase entre los micros A y B.
- **Desacople condicional:** si hay un NAM End-to-End aguas arriba (y no está en bypass), el motor marca el IR Loader. El bloque hace un fundido a seco de 30 ms y deja de convolucionar. Funciona aparte del bypass manual y de las escenas, y se puede desactivar con el parámetro "Auto E2E".

Las rutas de los modelos y las IR se guardan en el estado del plugin y se recargan al abrir el proyecto.

### Preset por defecto

`IN → Transpose → Split → { Dry | Harmony(+7) } → Mix → Amp (NAM) → Cab (IR) → Output → OUT`

Sin modelo ni IR cargados, los bloques Amp y Cab dejan pasar la señal sin cambios.

| Escena | Contenido |
|--------|-----------|
| A | Limpio |
| B | Un tono abajo (-2 st) |
| C | Armonía de quinta abierta en estéreo |
| D | Octava arriba |

## Validación

- `KumAmpTests`: grid serie/paralelo, compensación de latencia, ciclos, bypass, escenas, precisión del pitch shifter (error < 0,05 Hz) y RateBridge.
- `KumAmpIntegrationTests`, con JUCE y NAM reales:
  - carga de los modelos de ejemplo WaveNet, LSTM, A2 y A1, incluido A2 remuestreado de 44,1 a 48 kHz;
  - clasificación de perfiles y desacople E2E→IR, con override manual y opt-out;
  - convolución con una IR de prueba e ida y vuelta del estado;
  - **cero `operator new` en el hilo de audio**.
- `auval -v aufx KAmp Kumo`: pasa.
- `pluginval --strictness-level 7`: pasa.

## Roadmap

1. Remuestreo FIR polifásico para modelos entrenados a otra tasa, e inferencia NAM estéreo opcional.
2. Navegador de modelos (carpetas, Tone3000) y, opcionalmente, IR y modelos incrustados en el preset.
3. Afinador estroboscópico y Noise Gate.
4. Wah, Auto-Wah, Octaver; compresor, EQ paramétrico y gráfico, transient shaper.
5. Modulación: chorus, flanger, phaser, tremolo, rotary, univibe, bitcrusher.
6. Delays (tape, analog, digital, reverse) y reverbs (plate, spring, hall, shimmer, cloud) con *trails* al hacer bypass.
7. Editor del grid: arrastrar bloques, cablear y añadir o quitar ramas.
