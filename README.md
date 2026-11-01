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

## Windows

El proyecto compila en Windows x64 con Visual Studio 2022 (MSVC). Usa runtime estático, así que no hace falta instalar el VC++ Redistributable, y genera **VST3 + Standalone**. AU solo existe en macOS.

**Opción A: en un PC con Windows.**
1. Instala Visual Studio 2022 Community con la carga "Desarrollo para el escritorio con C++", Git e [Inno Setup 6](https://jrsoftware.org/isdl.php).
2. Ejecuta `build_windows.bat` (doble clic). Configura, compila, pasa los tests y genera `dist\KumAmp-<versión>-Windows-x64-Setup.exe`.

**Opción B: en la nube, sin Windows.** Al subir el repositorio a GitHub, `.github/workflows/build.yml` compila Windows y macOS en cada push. El instalador `.exe` y los zips de macOS se descargan desde *Actions → el run → Artifacts*.

**El instalador** (`installer/KumAmp.iss`, en español e inglés):
- Instala el VST3 en `C:\Program Files\Common Files\VST3\KumAmp.vst3` y la app Standalone en `Program Files\KumAmp`.
- Crea accesos directos en el menú Inicio (y en el escritorio, de forma opcional).
- Permite elegir entre instalación completa o solo VST3, e incluye desinstalador.

Sin firma de código, Windows SmartScreen avisará de "editor desconocido" la primera vez. Para distribuir públicamente conviene firmar el instalador con un certificado de firma de código.

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

### Interfaz

```
┌ Cabecera: logo · GRID / PRESETS / SNAPSHOTS / IRs / SETTINGS · CPU · kHz · buffer · estado ┐
│ Lateral         │ Escenas A-D · ‹ › · nombre editable · guardar · deshacer/rehacer · menú │
│  Afinador       │ Grid (tiles por categoría, arrastrar para reordenar)                     │
│  Input + medidor│ Paleta: + · Pitch · Dynamics · EQ · NAM · IR · Modulation · Delay · ... │
│  Gate global    │                                                                         │
│  Pitch global   │                                                                         │
└ Panel del bloque: power · archivo (lista la carpeta) · chips · perillas · selectores · OUT ┘
```

- Cadena de señal: input gain → [afinador] → **gate global** → **pitch global** (polifónico) → grid → output gain.
- El afinador compacto del lateral está siempre activo. Al hacer clic se abre el afinador grande, que puede silenciar la salida.
- El NAM tiene tone stack después del modelo: Gain, Bass, Mid, Treble, Presence y Master (5 = plano).
- Solo funciona la pestaña GRID. PRESETS, SNAPSHOTS, IRs y SETTINGS muestran "próximamente".
- `KumAmpUiSnapshot` instancia el procesador y el editor reales y guarda `build/snapshots/editor.png`.

### Editor del grid

| Acción | Efecto |
|--------|--------|
| **Arrastrar un bloque** sobre otro cable | Lo mueve conservando la misma instancia, con sus parámetros, archivos y colas. Se puede deshacer. |
| **Arrastrar una familia de la paleta** sobre un cable | Pregunta qué bloque insertar ahí. |
| **Clic en la paleta** | Inserta después del bloque seleccionado, o antes de OUT si no hay ninguno. |
| **⌘Z / ⇧⌘Z** o los botones | Deshacer y rehacer cambios de estructura (insertar, quitar, mover). |
| **"+" en un cable** | Inserta un bloque **en serie**, o **en paralelo** con una rama A/B: Split → bloque / cable seco → Mix, con cada entrada del mixer a -6 dB para no subir el volumen. |
| **Clic** | Selecciona un bloque y abre sus controles. |
| **Clic derecho** | Bypass. |
| **Supr / Retroceso o "Quitar"** | Elimina el bloque y une su cable. Si un paralelo queda vacío, el Split/Mix desaparece solo. |

El layout es automático (`GridModel::autoLayout`): la columna es el camino más largo desde la entrada, cada rama paralela ocupa su propia fila y las filas libres se reutilizan.

Los parámetros discretos se muestran como selectores con nombre (Off/On, Lento/Rápido…), usando la convención `unit = "A|B|C"` en `ParamInfo`.

### Sincronía con el tempo

Delays (Digital, Analog, Tape, Reverse) y modulaciones (Chorus, Flanger, Phaser, Tremolo, Univibe) tienen un botón **SYNC** y una **División**:

- Divisiones disponibles: 4/1, 2/1, 1/1, 1/2, 1/4, 1/8, 1/16 y 1/32, todas salvo 4/1, 2/1, 1/1 y 1/32 también con puntillo y tresillo.
- El BPM se lee del DAW en cada bloque de audio y se muestra en la cabecera. En Standalone se usa 120.
- Con SYNC activo, la perilla de tiempo o velocidad se bloquea y su valor muestra el resultado, por ejemplo "1/8. = 375 ms".

### EQ gráfico

El EQ paramétrico tiene 8 bandas: Bell, Low/High Shelf, Low/High Cut (12, 24, 36 y 48 dB/oct, Butterworth con resonancia), Notch y Band Pass. Se edita en una vista gráfica, en el panel o en grande con ⤢:

- Curva de respuesta sobre un analizador en vivo, pre (contorno) y post (relleno), de 10 Hz a 30 kHz con inclinación de +4,5 dB/oct.
- Nodos de colores numerados:

| Acción | Efecto |
|--------|--------|
| Arrastrar | Frecuencia y ganancia (en Cut/Notch/Band Pass, la Q) |
| Rueda del ratón | Q |
| Shift | Ajuste fino |
| Doble clic en vacío | Nueva banda |
| Doble clic en un nodo | Quitar la banda |
| Clic derecho | Tipo, pendiente y quitar |

- La banda seleccionada abre una tira con tipo, pendiente, Freq, Gain y Q.
- La curva se calcula con el mismo código (`EqDesign.h`) que procesa el audio. Verificado en los tests: -3,0 dB en la frecuencia de corte y -48 dB una octava por debajo en 48 dB/oct.
- El analizador usa taps sin bloqueo (`SampleRing`) que solo se activan mientras la vista está visible.

### MIDI

**Guitarra → MIDI** (pestaña SETTINGS):
- Un tracker de tono en tiempo real corre en el hilo de audio: McLeod a ~16 kHz, ventana de 40 ms y salto de 4 ms.
- Genera notas con velocity y pitch bend (±2, ±12 o ±24) para bends, vibrato y slides.
- Detecta re-púas, y los saltos de traste (hammer-on) los convierte en notas nuevas legato.
- Tiene canal de salida, sensibilidad y transposición configurables.
- Es monofónico: con una pastilla normal no se pueden separar las cuerdas; el MIDI polifónico necesita una pastilla hexafónica.
- Latencia medida: ~30–40 ms en las cuerdas graves.
- En REAPER: envío desde la pista de KumAmp a la del instrumento con Audio: None y MIDI: All.

**Control MIDI**:
- Program Change 1–4 cambia a las escenas A–D. El cambio llega al parámetro del host desde el hilo de mensajes.
- CC 11 / CC 4 (expresión) → Macro 1 (wah), CC 1 → Macro 2 y CC 2 → Macro 3.
- La entrada MIDI no se reenvía: la salida solo lleva las notas generadas por la guitarra.

**Guitar Synth** (bloque de la familia Pitch):
- Mono: osciladores PolyBLEP (sierra, cuadrada o pulso), sub-octava y glide, siguiendo la nota tocada.
- Poly: funciona con acordes mediante fuzz cuadrado y sub-octava polifónica (WSOLA).
- Ambos modos pasan por un filtro resonante con envolvente controlada por la dinámica de la guitarra.

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

### Afinador

Se abre con el botón **Afinador** de la cabecera. Como en una pedalera, lee la entrada del plugin (tras el *Input Gain*, antes del grid) y puede silenciar la salida mientras está abierto ("Silenciar salida", con fundido de 30 ms). La referencia A4 se ajusta entre 430 y 450 Hz.

- **Detección:** McLeod Pitch Method sobre una ventana de 85 ms a ~24 kHz, recalculada cada 10 ms, con mediana de 3 lecturas y suavizado dentro de cada nota. Error < 0,3 cents de 31 Hz (Si grave de bajo de 5 cuerdas) a 1 kHz, sin errores de octava aunque la fundamental sea más débil que el 2º armónico.
- **Hilos:** el audio solo diezma y escribe en una `AudioSampleFifo` (sobre `juce::AbstractFifo`) mientras la vista está abierta. El análisis corre en un hilo propio y publica con un seqlock.
- **Modo Tradicional:** nota grande, tres bandas estroboscópicas (fundamental, ×2 y ×4) que se desplazan según la desviación y se paran al afinar, más una aguja de ±50 cents.
- **Modo Trazo:** historial tipo sismógrafo de 8 s. La lectura más reciente aparece arriba y el trazo baja. La escala horizontal está expandida alrededor de 0 para que ±5 cents se lea bien. Los colores indican la desviación: verde ±3 cents, amarillo ±10 cents y rojo más allá. Cada nota se rotula donde empieza.

Los parámetros `tuner_reference`, `tuner_mute` y `tuner_mode` se guardan con el proyecto.

### Bloques disponibles

| Categoría | Bloques |
|-----------|---------|
| Entrada | Noise Gate (histéresis, hold, rango) · Afinador global |
| Pitch / Pre-FX | Pitch Shifter WSOLA · Wah (SVF TPT) · Auto-Wah · Octaver polifónico (-1 y +1 octava, WSOLA) |
| Amp / Cab | NAM · IR Loader dual |
| Dinámica / EQ | Compresor (detector de pico desacoplado, knee, mezcla en paralelo) · EQ paramétrico (6 etapas) · EQ gráfico de 10 bandas · Transient Shaper |
| Modulación | Chorus · Flanger · Phaser (2 a 12 etapas) · Tremolo (de seno a cuadrada, auto-pan) · Rotary (horn/drum con Doppler e inercia) · Univibe · Bitcrusher |
| Delay | Digital (ping-pong y filtros en la realimentación) · Analog (BBD: oscurece y satura) · Tape (wow, flutter, saturación, desgaste) · Reverse |
| Reverb | Hall, Shimmer y Cloud (FDN de 8 líneas con matriz Householder y modulación) · Plate (Dattorro 1997) · Spring (cadena de allpass dispersiva) |
| Utilidad | Volumen/Pan · Splitter · Mixer |

**Trails:** los delays y reverbs heredan de `TrailsBlock`. Su bypass silencia la entrada (no la salida), así que la cola sigue sonando. Cuando la cola se apaga, el bloque deja de consumir CPU. La ventana de silencio cubre el hueco más largo posible entre ecos.

Cada bloque consume entre 0,03 % y 0,45 % de un núcleo de Apple Silicon (medido en `KumAmpTests`).

### Preset por defecto

`IN → Wah → Oct → Amp (NAM) → Cab (IR) → EQ → Chorus → Delay → Reverb → OUT`

Sin modelo ni IR cargados, los bloques Amp y Cab dejan pasar la señal sin cambios. La Macro 1 (pedal de expresión) controla el wah.

| Escena | Contenido |
|--------|-----------|
| A | Crunch Ambient: delay y reverb |
| B | Lead: empuje de medios y delay más presente |
| C | Chorus Clean: chorus, sin delay |
| D | Octave Fuzz: octaver -1 y +1 |

## Validación

- `KumAmpTests`:
  - grid serie/paralelo, compensación de latencia, ciclos, bypass, escenas, pitch shifter, RateBridge y detector de tono;
  - **estrés de los 26 bloques** con parámetros aleatorios y extremos más bypass: salida finita y acotada, cero asignaciones;
  - comportamiento de cada bloque: compresor 4:1 (-14,98 dB frente a -15 esperados), EQ +12,00 dB, eco en la muestra exacta, trails, colas y RT60 de las reverbs, tremolo, octaver (110,00 Hz) y wah.
- `KumAmpIntegrationTests`, con JUCE y NAM reales:
  - carga de los modelos de ejemplo WaveNet, LSTM, A2 y A1, incluido A2 remuestreado de 44,1 a 48 kHz;
  - clasificación de perfiles y desacople E2E→IR, con override manual y opt-out;
  - convolución con una IR de prueba e ida y vuelta del estado;
  - editor del grid (insertar en serie/paralelo, quitar, colapsar ramas vacías, audio tras cada edición) con captura en `build/snapshots/grid_editor.png`;
  - afinador de punta a punta (nota y cents exactos, detección de silencio) e imágenes de las dos vistas en `build/snapshots/`;
  - **cero `operator new` en el hilo de audio**.
- `KumAmpHostTest`: carga el VST3 compilado como un DAW, le inyecta un `.nam` en el estado y comprueba que el modelo procesa audio. También verifica que el VST3 emite MIDI: un A2 de entrada produce la nota 45.
- `auval -v aufx KAmp Kumo`: pasa.
- `pluginval --strictness-level 7`: pasa.

## Roadmap

1. Remuestreo FIR polifásico para modelos entrenados a otra tasa, e inferencia NAM estéreo opcional.
2. Navegador de modelos (carpetas, Tone3000) y, opcionalmente, IR y modelos incrustados en el preset.
3. Sincronización de tempo del host para delays y modulaciones, más tap tempo.
4. Medidores en el panel: reducción de ganancia del compresor y estado del gate.
5. Editor: mover bloques arrastrando, deshacer/rehacer y cambiar el tipo de un bloque.
