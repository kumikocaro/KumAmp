# KUMAMP // MANIFIESTO DEL PROYECTO

Ficha para publicar KumAmp en kumodev. Los datos salen del repositorio y de GitHub al 6 de noviembre de 2026.

**Versión descargable: 0.1.9.**

---

## 1. Identidad

| Dato | Valor |
|---|---|
| Nombre | KumAmp |
| Tipo | Pedalera modular para guitarra y bajo, con modelado neural de amplificadores |
| Autor | Kumo Caro |
| Web | https://www.kumodev.com |
| Formatos | VST3, AU y aplicación independiente |
| Sistemas | macOS 12 o posterior (Apple Silicon e Intel), Windows 10 o posterior (64 bits) |
| Licencia | Software libre, GNU GPLv3 |
| Tecnología | C++, JUCE 8, NeuralAmpModelerCore |

## 2. Enlaces

| Para qué | Enlace |
|---|---|
| **Descargar la última versión** (siempre actualizado) | https://github.com/kumikocaro/KumAmp-downloads/releases/latest |
| Todas las versiones | https://github.com/kumikocaro/KumAmp-downloads/releases |
| Código fuente | https://github.com/kumikocaro/KumAmp |
| Versión y archivos, para leer desde la web | https://api.github.com/repos/kumikocaro/KumAmp-downloads/releases/latest |

Usá el primer enlace en el botón de descarga: siempre lleva a la última versión publicada. Los enlaces directos a cada instalador cambian en cada versión, porque el archivo lleva el número en el nombre.

## 3. Texto para la web

**Título:** KUMAMP

**Bajada:** Una pedalera entera, un amplificador neural y un campo para perderse.

**Descripción corta:**
Pedalera modular para guitarra y bajo. Armá tu cadena con 34 bloques, cargá modelos de amplificador NAM y elegí cómo trabajar: con todo el detalle, de la forma más simple o explorando sonidos que no sabías que buscabas.

**Descripción larga:**
KumAmp junta en un solo plugin amplificadores neurales, gabinetes, efectos e instrumentos que se tocan con la guitarra. La cadena se arma como un circuito, en serie o en paralelo, con cuatro escenas por preset. Tiene tres formas de usarse: Grid para controlar cada detalle, Simp para entender qué hace cada cosa con una sola perilla, y Campo para deformar y fusionar efectos hasta encontrar un sonido nuevo. Es software libre, y funciona en Mac y Windows dentro de tu DAW o por su cuenta.

## 4. Tres formas de tocarlo

| Modo | Qué es | Estado |
|---|---|---|
| **Grid** | La cadena completa: cada bloque, cada parámetro, rutas en paralelo y escenas | Disponible |
| **Simp** | Una tarjeta por efecto que explica para qué sirve, con encendido y una perilla. Seis sonidos a un toque: Clean, High Gain, Crunch, Synth, Blues y Sitar | Disponible |
| **Campo** | Un instrumento aparte con formas elásticas. Arrastrá para transformar un efecto, estirá para que oscile y superponé dos para fusionarlos. Guardá lo que descubras como tono. No toca el Grid | Disponible |

## 5. Funciones

- Modelos de amplificador NAM, con búsqueda y descarga desde TONE3000 dentro del plugin.
- IR Loader doble con 11 gabinetes propios y 55 respuestas de micrófono.
- Cadena con rutas en serie y en paralelo, y compensación de latencia.
- Cuatro escenas por preset, con cambios sin cortes.
- Instrumentos que se tocan con la guitarra: Guitar Synth, Sitar con tampura, Charango y Sampler de cualquier sonido.
- Guitarra a MIDI y control por MIDI (escenas y macros).
- Afinador, noise gate y pitch shifter globales.
- Sincronía de delays y modulaciones con el tempo del DAW.
- Repeat Percussion, con Cortina para que el sonido siga sonando.
- Tampura como forma propia en el Campo, que puede pasar por otros efectos.
- 18 presets de referencia en 6 categorías.

## 6. Bloques

| Familia | Bloques |
|---|---|
| Pitch | Pitch Shifter, Wah, Auto Wah, Octaver |
| Experimental | Guitar Synth, Sitar, Charango, Sampler |
| Dinámica | Noise Gate, Compresor, Transient Shaper |
| EQ | Paramétrico, Gráfico |
| Drive | Overdrive |
| Amp y gabinete | NAM, IR Loader |
| Modulación | Chorus, Flanger, Phaser, Tremolo, Rotary, Univibe, Bitcrusher |
| Delay | Digital, Analog, Tape, Reverse, Repeat Percussion |
| Reverb | Plate, Hall, Spring, Shimmer, Cloud |
| Utilidad | Volumen |

## 7. Avisos para la página de descarga

- **Es una versión de prueba.** Los instaladores no están firmados, así que el sistema avisa al abrirlos.
- **Mac:** la primera vez, clic derecho sobre el instalador y "Abrir". Si no aparece, en Ajustes del Sistema > Privacidad y seguridad, "Abrir igualmente".
- **Windows:** puede aparecer un aviso de SmartScreen.
- **Modelos NAM:** no vienen incluidos. Se bajan aparte desde TONE3000, también desde dentro del plugin, y cada uno tiene la licencia de su autor. Los presets que usan un modelo avisan cuál falta.

## 8. Datos para integrarlo

```json
{
  "id": "kumamp",
  "name": "KumAmp",
  "author": "Kumo Caro",
  "tagline": "Una pedalera entera, un amplificador neural y un campo para perderse.",
  "summary": "Pedalera modular para guitarra y bajo. Armá tu cadena con 34 bloques, cargá modelos de amplificador NAM y elegí cómo trabajar: con todo el detalle, de la forma más simple o explorando sonidos que no sabías que buscabas.",
  "license": "GPL-3.0",
  "stack": ["C++", "JUCE 8", "NeuralAmpModelerCore"],
  "formats": ["VST3", "AU", "Standalone"],
  "platforms": [
    { "os": "macOS", "minimum": "12", "architectures": ["arm64", "x86_64"], "formats": ["VST3", "AU", "Standalone"] },
    { "os": "Windows", "minimum": "10", "architectures": ["x64"], "formats": ["VST3", "Standalone"] }
  ],
  "links": {
    "download": "https://github.com/kumikocaro/KumAmp-downloads/releases/latest",
    "releases": "https://github.com/kumikocaro/KumAmp-downloads/releases",
    "source": "https://github.com/kumikocaro/KumAmp",
    "latestApi": "https://api.github.com/repos/kumikocaro/KumAmp-downloads/releases/latest",
    "author": "https://www.kumodev.com"
  },
  "publishedVersion": "0.1.9",
  "modes": [
    { "name": "Grid", "available": true },
    { "name": "Simp", "available": true },
    { "name": "Campo", "available": true }
  ]
}
```

Para que la web muestre sola la última versión y sus instaladores:

```ts
export async function getKumAmpRelease() {
  const res = await fetch(
    "https://api.github.com/repos/kumikocaro/KumAmp-downloads/releases/latest",
    { next: { revalidate: 3600 } } // GitHub limita las consultas sin token: una por hora alcanza
  );
  if (!res.ok) return null; // sin datos: mostrar solo el botón a /releases/latest

  const release = await res.json();
  const asset = (ending: string) =>
    release.assets.find((a: { name: string }) => a.name.endsWith(ending))?.browser_download_url;

  return {
    version: release.tag_name.replace(/^v/, ""),
    date: release.published_at,
    mac: asset(".pkg"),
    windows: asset(".exe"),
  };
}
```

## 9. Créditos

Usa JUCE, el SDK de VST3, el SDK de ASIO, NeuralAmpModelerCore, Eigen y nlohmann/json. Tipografías Jost e IBM Plex Mono. La lista completa con sus licencias está en [`THIRD_PARTY_NOTICES.md`](https://github.com/kumikocaro/KumAmp/blob/main/THIRD_PARTY_NOTICES.md).
