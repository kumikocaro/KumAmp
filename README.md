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

## Instalador de macOS

```bash
cmake -S . -B build-universal -G Ninja -DCMAKE_BUILD_TYPE=Release -DKUMAMP_UNIVERSAL=ON -DKUMAMP_COPY_AFTER_BUILD=OFF
cmake --build build-universal
installer/macos/build_pkg.sh
```

Genera `dist/KumAmp-<versión>-macOS-Universal.pkg` (Apple Silicon + Intel, macOS 12+):
- Instala el VST3 en `/Library/Audio/Plug-Ins/VST3`, el AU en `/Library/Audio/Plug-Ins/Components` y la app en `/Applications`.
- Cada formato se puede elegir por separado.
- Al instalar el AU, Logic vuelve a escanear los plugins automáticamente.

Para quien lo pruebe hay instrucciones en `installer/macos/LEEME-Mac.txt`.

- **Firma:** por defecto es ad-hoc, así que en otro Mac habrá que abrirlo con clic derecho → Abrir. Para distribuir sin avisos hace falta un Developer ID: con `KUMAMP_SIGN_IDENTITY` / `KUMAMP_INSTALLER_IDENTITY` se firma el `.pkg`, y luego se notariza con `xcrun notarytool`.
- **En la nube:** en GitHub Actions el job de macOS genera el `.pkg` al lanzarlo a mano o con un tag `v*`.

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

### Modo Simp

El botón **SIMP** de la cabecera muestra la misma cadena de la forma más simple posible: una tarjeta numerada por cada efecto, que dice en una frase para qué sirve, con su botón de encendido y una sola perilla. La Entrada y la Salida quedan a los costados. Son los mismos valores que en el Grid: lo que cambiás en uno aparece en el otro.

Arriba hay seis íconos con presets listos: Clean, High Gain, Crunch, Synth, Blues y Sitar. Se guardan en `Documents/KumAmp/Presets/Simp`. Clean, High Gain, Crunch y Blues usan modelos NAM que hay que tener en `Documents/KumAmp` (no se pueden incluir en el plugin); si falta el modelo, KumAmp avisa cuál buscar en TONE3000. Synth y Sitar no necesitan ninguno.

### Vista Campo

El botón **Vista Campo** de la cabecera abre un instrumento aparte para descubrir sonidos: tiene los mismos elementos que tu cadena, pero con valores propios. Mientras estás ahí suena el Campo y el Grid no se toca; al volver, el Grid suena exactamente como estaba. La entrada, el gate, el pitch y la salida generales siguen siendo los mismos.

| Gesto | Efecto |
|---|---|
| Arrastrar una forma | Transforma su efecto: la posición mueve varios de sus parámetros a la vez. En su lugar de origen suena como en el Grid |
| Tirar del cuadradito del borde | La forma se estira y sus parámetros oscilan: la distancia es cuánto y la dirección, a qué velocidad. Tocar más fuerte los empuja |
| Superponer dos formas | Fusiona los efectos: la salida de cada uno entra al otro, también hacia atrás en la cadena |
| Doble clic en una forma | La apaga o la prende |
| Forma TAMPURA (mandala, si la cadena tiene Sitar) | Mueve solo los parámetros del drone. Doble clic prende el drone (y el Sitar, del que sale). Superponerla con otra forma manda el drone por ese efecto |
| Cuerda MAREA | La velocidad de todo el campo |
| Cuerda FUSIÓN | Cuánto se alimentan entre sí las formas fusionadas |
| MUTAR | Otra relación entre formas y parámetros, y las formas movidas al azar |
| GUARDAR TONO | Deja una ficha en el campo (`Documents/KumAmp/Campo`, hasta 10) |
| Clic en una ficha | Recupera ese tono |
| Arrastrar una ficha hacia otra | Mezcla los dos tonos en vivo; al soltar queda el resultado y la ficha vuelve a su lugar. Esc cancela |
| Clic derecho en una ficha | La manda a la papelera |
| DESDE GRID | Empieza de nuevo con los valores actuales del Grid |
| PASAR AL GRID | Copia los valores al Grid, sin oscilaciones ni fusiones. No se puede deshacer |

Los niveles quedan fuera de la transformación y la salida del Campo tiene un limitador, para que explorar no signifique un salto de volumen.

### Presets de referencia

La primera vez que abrís la pestaña PRESETS, KumAmp crea `Documents/KumAmp/Presets/Referencia` con 18 presets en 6 categorías: Limpios, Blues y Crunch, Rock, Metal, Ambient y Bajo. Cada uno trae cuatro escenas (Base, Solo, Más espacio, Seco). Son archivos comunes: se pueden editar o borrar y no vuelven a escribirse.

**No incluyen modelos NAM.** Los términos de TONE3000 no permiten empaquetar sus tonos, así que cada preset nombra el archivo y su comentario dice de dónde bajarlo. Si el modelo está en `Documents/KumAmp`, lo encuentra solo; si no, el resto de la cadena carga igual y el amplificador queda sin modelo (el detalle está en el ⓘ del bloque).

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
| Alt (Option) + clic sostenido | Escuchar la banda mientras se mantiene (se puede seguir arrastrando) |

- La banda seleccionada abre una tira con tipo, pendiente, Freq, Gain, Q y **Escuchar**.
- **Escuchar banda (como en FabFilter):** solo suena la zona sobre la que actúa la banda, a nivel unidad. En Bell, Notch y Band Pass es un pasabanda en su frecuencia y Q; en Low Shelf y Low Cut, lo que está por debajo; en High Shelf y High Cut, lo que está por encima (24 dB/oct). La zona se sombrea en la curva, entra y sale con un fundido de 15 ms y sigue al nodo mientras lo arrastrás. No se guarda en el preset.
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

**Guitar Synth** (bloque de la familia Experimental):
- Mono: osciladores PolyBLEP (sierra, cuadrada o pulso), sub-octava y glide, siguiendo la nota tocada.
- Poly: funciona con acordes mediante fuzz cuadrado y sub-octava polifónica (WSOLA).
- Textura: una capa sintética con ataque de guitarra, pensada para mezclar debajo de la guitarra real (funciona con acordes). Cada púa se detecta y dispara una envolvente de "pluck" (Decay) que maneja el nivel y el filtro, como un pluck de sintetizador. La voz es la guitarra cuadrada más una octava arriba (+1 Oct) y el sub, abierta en estéreo con dos delays cortos modulados (Ancho). Para usarla de complemento, la Mezcla va bien entre 25 % y 40 %.
- Los tres modos pasan por un filtro resonante con envolvente: en Mono y Poly la maneja la dinámica de la guitarra, en Textura el pluck.
- Instrumento: un banco propio de KumAmp con 16 instrumentos sintetizados que toca la guitarra. Son síntesis pura, sin samples, así que van bajo la licencia del proyecto y no pesan nada.
  - **Sonidos:** Piano, E. Piano, Órgano Jazz, Órgano Rock, Órgano Iglesia, Cuerdas, Cuerdas Solina, Pad Cálido, Pad Brillante, Coro, Brass, Campanas, Mallets, Pluck, Lead y Bajo Synth.
  - **Motores:** aditivo (órganos de rueda fónica con click y percusión, órgano de tubos, piano con parciales inarmónicos, dos cuerdas por nota y martillo), FM (piano eléctrico de lengüeta, campanas) y analógico (sierras desafinadas, sub y filtro con envolvente, con formantes para el coro).
  - **Efectos por sonido:** saturación y Leslie (lento y rápido) en los órganos, ensemble en cuerdas y pads, auto-pan en el piano eléctrico.
  - **Notas:** en «Acordes», un detector polifónico lee lo que suena en las cuerdas. Usa un banco de filtros por semitono y la saliencia armónica, y quita los armónicos de cada nota encontrada. Las octavas duplicadas de un acorde se escuchan como parte de la nota grave. Tarda unos 40 ms en los agudos y hasta ~80 ms en las cuerdas graves. «Nota simple» usa el afinador interno y responde más rápido; el Lead y el Bajo Synth tocan la nota más aguda o la más grave.
  - **Re-ataque:** en los sonidos percusivos (piano, E. piano, campanas, mallets, pluck, bajo), cada púa vuelve a atacar las notas.
  - **Controles:** Tono (más oscuro o más brillante), Cola (decays y releases más cortos o más largos), Octava y Ancho.
  - **Secuencia:** Arp Sube, Arp Baja, Arp Sube-Baja, Arp Azar, Acordes, Tresillo, Galope u Octavas, sincronizada al tempo del proyecto. El patrón arranca con la primera púa.
  - **Ajustes de la secuencia:** Ritmo (1/4 a 1/32, con tresillos), Duración de cada nota, Octavas del arpegio (1 a 3) y Swing.
- El panel muestra solo los controles del modo elegido. En Instrumento, los chips indican el sonido y las notas que está leyendo.

### Sensibilidad de detección

En SETTINGS, **Detección de notas** ajusta lo que escuchan los detectores de nota de Guitar Synth, Sitar, Charango, Sampler y Guitarra → MIDI, de -12 a +24 dB. Es para que agarren una guitarra de poca salida (o dejen de dispararse con una muy caliente): no cambia el volumen de la guitarra, aunque en el Guitar Synth, que se arma a partir de la señal, sí cambia cuánto entra al sintetizador. Debajo, un medidor muestra el nivel mientras tocás y dice si alcanza. Los detectores empiezan a -55 dB.

### Sitar

Convierte la guitarra en un lead de sitar con un anillo de cuerdas simpáticas. Tiene dos modos:

- **Modelado** (por defecto): el lead es una cuerda de sitar propia. El afinador interno lee la nota y cada púa pulsa una cuerda de acero modelada con el jawari *dentro* de la cuerda. Mientras la cuerda pasa sobre el puente curvo, lo toca más adelante y su largo se acorta, así que el zumbido aparece en la púa y va bajando por los armónicos a medida que la nota decae, como en el puente real. La cuerda sigue a la guitarra todo el tiempo, así que los slides y bends se vuelven meend. Suena mientras suene la nota de la guitarra y pasa por un cuerpo de sitar (calabaza, medios nasales, brillo de acero). El click de la púa de la guitarra cubre los ~20 ms que tarda en leer la nota. Es para notas sueltas, no para acordes.
- **Procesado** (sin latencia, funciona con acordes). **Lead (jawari):** la cuerda del sitar roza un puente ancho y curvo, así que su largo efectivo cambia con la amplitud. Se modela como un retardo que depende de la amplitud y actúa de un solo lado, más un recorte de contacto también de un solo lado, ambos sobre la señal normalizada por el pico del ataque. El zumbido es máximo en la púa y se suaviza a medida que la nota decae. Un pasabanda resonante que sigue la nota (afinador interno) barre desde un armónico alto hacia uno bajo después de cada púa.
- **Bend:** sube el pitch del lead hasta una octava (en Modelado la cuerda misma, en Procesado con WSOLA), para hacer el meend desde un pedal de expresión o automatización.
- **Cuerdas simpáticas:** 12 cuerdas de guía de onda afinadas a una Tonalidad y una Escala (Bilawal, Khamaj, Kafi, Asavari, Bhairav, Yaman, Todi, Bhupali, Sa-Pa), excitadas por la guitarra. Solo resuenan cuando lo que tocás comparte sus notas, como en un sitar. **Congelar** sostiene el drone que está sonando.
- **Tampura:** un drone sostenido de fondo para ambientar. Cuatro cuerdas (la primera en Pa, Ma o Ni, dos en Sa y el Sa grave) suenan sin parar, cada una como un banco de armónicos. El brillo del puente del tampura se mueve despacio por sus armónicos, cada cuerda a su ritmo, con una respiración suave del nivel: el drone nunca queda quieto pero siempre está en su nota.
  - **Drone** es su nivel. **Tono tampura** sigue la Tonalidad del sitar o se elige aparte. **Textura** fija cuánto se mueve ese brillo y **Movimiento** su velocidad (2 a 30 s por vuelta).
  - **Desafinar** es cuánto se separan las dos cuerdas Sa (0 a 10 cents; las hace batir y le da esa fluctuación microtonal). **Respiración** es la subida y bajada lenta del nivel, y **Deriva** lo irregular que es el barrido. Con los tres y Textura en 0 el drone queda totalmente quieto.
  - **Salida tampura:** «Directo» (por defecto) la manda a la salida por fuera de la cadena, sin pasar por el amp ni los efectos que vienen después; «Con la cadena» la suma a la guitarra. Entra y sale con un fundido de 1,5 s, y también hace sonar un poco las cuerdas simpáticas.
- **Niveles:** Seco, Lead y Simpáticas por separado, con Tono lead y Tono simp para el lead y para las cuerdas, y Cola de las simpáticas.

### Charango

El charango del norte argentino y los Andes: 10 cuerdas de nylon en 5 órdenes dobles, temple natural (sol, do, mi, la, mi), re-entrante, con el orden del medio en octavas, caja chica y brillante y poco sustain.

- **Procesado** (baja latencia, ~15 ms): la guitarra una octava arriba (WSOLA polifónico), cada nota doblada como un orden (dos copias apenas desafinadas y retrasadas, abiertas en estéreo), el **Octavado** (la otra octava mezclada), un **Sustain** más corto (el decay se acorta respecto de la púa) y el **Cuerpo** chico (sin graves, resonancias de aire y tapa, agudos brillantes). **Registro** elige entre «+1 octava» y el registro de la guitarra.
- **Modelado:** el detector polifónico lee el acorde y lo re-voicea como un charango (cada nota se pliega a Mi4–Re#5 y el orden de Mi va en octavas). Lo tocan órdenes dobles de nylon modelados (guías de onda) a través del mismo cuerpo. Cada púa rasguea los órdenes con el **Rasgueo** elegido (0 a 40 ms entre cuerdas) y alterna abajo/arriba cuando las púas vienen rápidas. Es lo más fiel en acordes, con los 40–80 ms del detector.
- Los chips muestran el temple y, en Modelado, el voicing que está tocando.

### Sampler

Cualquier sonido, cargado de un archivo (WAV, AIFF, FLAC, OGG, MP3, M4A; hasta 60 s), recortado ahí mismo y tocado en el tono de la guitarra. Como en todo sampler, para subir de tono se lee más rápido: la duración y el carácter cambian con la nota (no hay time stretching).

- **Editar** (o cargar un sonido) abre el editor sobre el grid: forma de onda con los bordes del recorte para arrastrar, rueda para hacer zoom, **Escuchar**, **Quitar silencio** y **Zoom al recorte**. También se puede soltar un archivo encima.
- **Nota base:** en Auto se detecta del recorte (también los cents); un sonido sin tono claro suena como si fuera C4. Se puede fijar a mano y afinar.
- **Notas:** «Una nota» usa el detector rápido (~10-30 ms desde la púa) y sigue bends y slides, con **Seguimiento** por semitono o libre. «Acordes» dispara una voz por nota con el detector polifónico (suma 40-80 ms).
- **Disparo:** «Una vez» suena hasta el final del recorte, «Sostener» corta cuando termina la nota, «Loop» repite con **Cruce loop** mientras la nota suena.
- **Ataque**, **Release**, **Dinámica** (cuánto sigue a la púa), **Octava**, **Mezcla** y **Nivel**.

El preset guarda la ruta del archivo, no el audio: si el archivo se mueve, el bloque avisa que no lo encuentra. El recorte y la nota base son del sonido, así que no cambian con la escena.

### Noise Gate: Clásico y Avanzado

El gate de entrada (barra lateral) y el bloque Noise Gate tienen dos modos:

- **Clásico:** un detector y una ganancia para toda la señal.
- **Avanzado (guitarra):** el soplido que deja pasar un gate clásico está en los agudos, y queda abierto mientras suene la fundamental de la nota. En este modo el detector ignora el zumbido (pasaaltos de 100 Hz en la llave) y la señal se divide en "Corte" (2,5 kHz) en dos bandas complementarias, que suman exacto con el gate abierto. Los graves siguen al gate principal. Los agudos además necesitan su propio nivel (medido con un pasaaltos de 24 dB/oct) por encima del umbral + "Agudos", y cierran más rápido. En el test, con una nota que suena y soplido de fondo, el hiss por encima de 5 kHz en la cola baja de -63 a -88 dB, y el cuerpo de la nota no cambia.

### Escenas

Las escenas guardan valores y estado de bypass de cada bloque y nodo de ruteo. La topología es común a todas, así que cambiar de escena solo mueve objetivos suavizados y el cambio no corta el audio. La escena activa es un parámetro del host (`scene`). "Guardar escena" sobrescribe la escena activa con el estado actual.

### Pitch Shifter (WSOLA adaptativo)

Funciona en el dominio del tiempo y es polifónico: no detecta tono. Dos cabezales de lectura sobre un buffer circular;
cada empalme hace un crossfade entre ellos, ya en fase. El salto de cada empalme es **adaptativo**: una nota se repite
cada periodo, pero un acorde solo cada periodo común de sus notas (24 ms un Mi abierto, ~41 ms un Sol abierto), así
que el empalme se hace en cuanto aparece un pico real de correlación normalizada ≥ 0,98 (buscado en una copia a 6 kHz
y refinado a resolución completa), eligiendo la menor latencia posible. Las notas empalman tras un periodo y los
acordes cuando su periodo común entra en la ventana (45 ms por defecto, parámetro **Ventana**).

- Rango de -12 a +12 semitonos más cents. Admite glide continuo, tipo whammy (Macro 1 → Transpose, de 0 a +12).
- Instrumento **Guitarra** (saltos de 6 a 45 ms) o **Bajo** (hasta 70 ms, correlación de 25 ms para el Si grave).
- Medido (acordes rasgueados, −2 y −5 st): ruido de empalme en acordes −25 dB (antes −11 dB), notas −30 dB, latencia
  media ~14 ms (bajando) y ~12 ms (subiendo). A 0 st la señal sale sin procesar y la latencia es cero.
- Rubber Band y Signalsmith Stretch quedan descartados para tocar en vivo: ~50 ms de latencia por bloque.

### NAM (Core)

- Motor: [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) v0.6.0 (MIT). Lee archivos `.nam` de las versiones 0.5 a 0.7: WaveNet A1/A2 ("NAM2"), LSTM, ConvNet y SlimmableContainer.
- La carga se hace en un hilo aparte (`FileLoaderPool`): parseo, `Reset()` y prewarm. El modelo pasa al audio por `RTCrossfadeSlot` con un crossfade de 50 ms, sin clics.
- La inferencia es mono, como todas las capturas NAM. Si la tasa de muestreo del host no coincide con la del entrenamiento, `RateBridge` remuestrea (Hermite; está pendiente un FIR polifásico).
- Normalización a -18 dB usando el campo `loudness` de los metadatos.
- **Detección de perfil:** el campo `gear_type` de los metadatos decide el tipo. Si contiene `cab` o `full_rig` el perfil es *End-to-End*; con `amp`, `pedal`, `preamp` y similares es *Amp-Only*. El parámetro "Perfil" permite forzarlo (0 Auto, 1 Amp, 2 E2E).

### Overdrive (Tube Screamer)

Modelo del circuito TS808 / TS9. Va antes del amp y arranca apagado.

- **Etapa de recorte:** op-amp no inversor cuya rama de ganancia (4,7k + 47 nF) solo amplifica lo que está por encima de ~720 Hz (de ahí la "joroba" de medios). En la realimentación hay 51k + Drive (500k, curva logarítmica), dos 1N914 y 51 pF. El nodo de realimentación se resuelve muestra a muestra (Euler implícito + Newton sobre la ley de Shockley del diodo) con oversampling 4x. La salida es la entrada más ese voltaje recortado, y por eso el Screamer conserva tanto la nota limpia.
- **Tono:** el pasabajos pasivo 1k / 220 nF (723 Hz) se mezcla de vuelta hacia plano con la perilla, y después viene el nivel.
- **Modelo:** TS808 (original), TS9 (más brillante) y Mod (condensador de 100 nF que deja entrar más graves al recorte, diodos asimétricos 1+2 y más ganancia).
- Mide unos 6 samples de latencia (0,13 ms), que se informan al host. El aliasing queda por debajo de -50 dB con una nota de 2,5 kHz a drive máximo, y consume ~1,9 % de un núcleo.

### IR Loader (Cab)

- Dos IR estéreo (A y B), cada una con nivel y pan, más low/high cut y mezcla. La convolución usa `juce::dsp::Convolution` con latencia cero en una cola compartida.
- Las IR no se recortan, para no romper la relación de fase entre los micros A y B.
- **Desacople condicional:** si hay un NAM End-to-End aguas arriba (y no está en bypass), el motor marca el IR Loader. El bloque hace un fundido a seco de 30 ms y deja de convolucionar. Funciona aparte del bypass manual y de las escenas, y se puede desactivar con el parámetro "Auto E2E".

**Gabinetes KumAmp:** IR propias, sintetizadas por el plugin (no son grabaciones de terceros, así que se distribuyen con la licencia del proyecto). Aparecen en el selector de IR, agrupadas por gabinete, con 5 opciones de micrófono cada uno: dinámico al centro, dinámico al borde, cinta, condensador y dinámico + cinta.

| Guitarra | Bajo |
|----------|------|
| 1x10, 2x10, 4x10 americano abierto · 1x12 americano abierto · 1x12 británico cerrado · 2x12 británico abierto · 2x12 moderno cerrado · 4x12 británico vintage · 4x12 británico moderno | 1x15 cerrado · 8x10 cerrado |

Cada IR sale de una respuesta en magnitud con varias capas: resonancia del parlante, carga del gabinete (cerrado, o abierto con cancelación de la onda trasera), modos de la caja, rolloff y break-up del cono, y la voz del micrófono. Esa respuesta se convierte en un impulso de fase mínima con el cepstrum real, y se le suman las reflexiones tempranas del gabinete. El 4x12 moderno está calibrado contra IRs medidas de un 4x12 cerrado con parlantes británicos de 30 W: la diferencia es de ±2 dB entre 40 Hz y 8 kHz, en bandas de 1/6 de octava. En los presets se guardan como `kumamp:cab/<gabinete>/<micro>`, sin archivos, así que funcionan en cualquier máquina.

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
| Entrada | Noise Gate (histéresis, hold, rango; modo Avanzado multibanda) · Afinador global |
| Pitch / Pre-FX | Pitch Shifter WSOLA · Wah (SVF TPT) · Auto-Wah · Octaver polifónico (-1 y +1 octava, WSOLA) |
| Experimental | Guitar Synth · Sitar (cuerda modelada o procesado, jawari, cuerdas simpáticas y tampura) · Charango (procesado o modelado) · Sampler (cualquier sonido, recortado y tocado en el tono de la guitarra) |
| Drive | Overdrive (Tube Screamer TS808 / TS9 / Mod, oversampling 4x) |
| Amp / Cab | NAM · IR Loader dual con 11 gabinetes KumAmp (55 IR) |
| Dinámica / EQ | Compresor (detector de pico desacoplado, knee, mezcla en paralelo) · EQ paramétrico (6 etapas) · EQ gráfico de 10 bandas · Transient Shaper |
| Modulación | Chorus · Flanger · Phaser (2 a 12 etapas) · Tremolo (de seno a cuadrada, auto-pan) · Rotary (horn/drum con Doppler e inercia) · Univibe · Bitcrusher |
| Delay | Digital (ping-pong y filtros en la realimentación) · Analog (BBD: oscurece y satura) · Tape (wow, flutter, saturación, desgaste) · Reverse · Repeat Percussion (corte de volumen en diente de sierra, inspirado en los amplificadores de los 60, con una Cortina que hace que el sonido siga) |
| Reverb | Hall, Shimmer y Cloud (FDN de 8 líneas con matriz Householder y modulación) · Plate (Dattorro 1997) · Spring (cadena de allpass dispersiva) |
| Utilidad | Volumen/Pan · Splitter · Mixer |

**Modelo:** en el panel de cada bloque, el selector de arriba cambia el efecto por otro de su familia sin moverlo del grid (por ejemplo Chorus → Phaser, Plate → Hall). El nuevo arranca con sus valores por defecto, conserva el encendido/apagado de cada escena y se puede deshacer.

**Trails:** los delays y reverbs heredan de `TrailsBlock`. Su bypass silencia la entrada (no la salida), así que la cola sigue sonando. Cuando la cola se apaga, el bloque deja de consumir CPU. La ventana de silencio cubre el hueco más largo posible entre ecos.

Cada bloque consume entre 0,03 % y 0,5 % de un núcleo de Apple Silicon; los más pesados son el Guitar Synth (~0,8 %) y el Sitar (~1,9 %) (medido en `KumAmpTests`).

### Preset por defecto

`IN → Synth → Wah → Oct → Drive → Amp (NAM) → Cab (IR) → EQ → Chorus → Delay → Reverb → OUT`

Sin modelo cargado, el Amp deja pasar la señal sin cambios. El Cab trae cargado el 4x12 británico moderno de KumAmp pero arranca apagado (y, encendido, se desacopla solo con perfiles NAM End-to-End). La Macro 1 (pedal de expresión) controla el wah.
El Guitar Synth va primero, porque su detector de notas necesita la guitarra limpia. Viene preparado como capa de Cuerdas en modo Instrumento, con Mezcla al 40 %.
El synth, el overdrive, el cab, el delay y el reverb arrancan apagados en todas las escenas (sus ajustes por escena ya están cargados para cuando los enciendas).

| Escena | Contenido |
|--------|-----------|
| A | Crunch Ambient: delay 420 ms y reverb listos para encender |
| B | Lead: empuje de medios, delay 480 ms |
| C | Chorus Clean: chorus |
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

## Presets

Pestaña **PRESETS**: guarda la cadena completa (bloques, las 4 escenas, macros, gate, pitch y volumen de
salida) como archivos `.kumamp` en `Documentos/KumAmp/Presets`. Las subcarpetas son categorías (packs de
autor). Los modelos `.nam` / IRs se referencian por ruta; si faltan, KumAmp los busca por ruta relativa y por
nombre dentro de la carpeta del preset y de `Documentos/KumAmp`, así un pack con sus archivos se puede
compartir comprimido.

## TONE3000

El bloque Amp tiene un botón **TONE3000**: abre tone3000.com en el navegador, inicias sesión con tu cuenta,
eliges un tono y KumAmp descarga el modelo elegido en `Documentos/KumAmp/TONE3000/<tono>` (con `tone.txt`:
creador, licencia y enlace) y lo carga. Usa la API v1 de TONE3000 (OAuth 2.0 + PKCE con redirect a
`127.0.0.1`); la clave pública está en `KUMAMP_T3K_PUBLISHABLE_KEY` (CMake). El token vive solo en memoria.

## Licencia

KumAmp es software libre bajo la **GNU General Public License v3** (ver [`LICENSE`](LICENSE)).
El standalone de Windows incluye soporte **ASIO** mediante el SDK de Steinberg, usado bajo la opción
GPLv3 de su licencia dual; CMake lo descarga solo en Windows (`-DKUMAMP_ASIO=OFF` lo desactiva).
Componentes de terceros y sus licencias: [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

