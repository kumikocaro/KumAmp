# Componentes de terceros

KumAmp se distribuye bajo la **GNU General Public License v3** (ver `LICENSE`).
Incluye o usa estos componentes, cada uno con su propia licencia:

| Componente | Uso en KumAmp | Licencia |
|---|---|---|
| [JUCE 8](https://juce.com) | Framework de audio y de interfaz, formatos VST3 / AU / Standalone | AGPLv3 (opción de código abierto de JUCE) |
| VST3 SDK (incluido en JUCE) | Formato de plugin VST3 | Licencia del SDK de Steinberg (incluida en JUCE) |
| [Steinberg ASIO SDK 2.3.4](https://www.steinberg.net/developers/) | Drivers ASIO en el standalone de Windows | GPLv3 (opción de código abierto de su licencia dual) |
| [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) | Inferencia de modelos NAM | MIT |
| [Eigen](https://eigen.tuxfamily.org) (vía NeuralAmpModelerCore) | Álgebra lineal | MPL 2.0 |
| [nlohmann/json](https://github.com/nlohmann/json) (vía NeuralAmpModelerCore) | Lectura de archivos .nam | MIT |
| [Ejemplo JUCE de la API de TONE3000](https://github.com/tone-3000/api) | Base del flujo OAuth (PKCE + redirect local) en `Source/Tone3000` | MIT |

ASIO es una marca comercial y software de Steinberg Media Technologies GmbH.
VST es una marca comercial de Steinberg Media Technologies GmbH.
TONE3000 es un servicio de terceros: los tonos que descargas tienen la licencia que eligió su creador
(se guarda en `tone.txt` junto a cada modelo). Powered by TONE3000 — https://www.tone3000.com

El código fuente completo de KumAmp, con las instrucciones para compilarlo, está en
https://github.com/kumikocaro/KumAmp

## Tipografías de la interfaz

- **Jost** — Copyright 2020 The Jost Project Authors (https://github.com/indestructible-type/Jost). SIL Open Font License 1.1, ver [`Resources/Fonts/OFL-Jost.txt`](Resources/Fonts/OFL-Jost.txt). Se incluyen instancias estáticas (pesos 400, 650 y 850) del subconjunto latino.
- **IBM Plex Mono** — Copyright 2017 IBM Corp. SIL Open Font License 1.1, ver [`Resources/Fonts/OFL-IBMPlexMono.txt`](Resources/Fonts/OFL-IBMPlexMono.txt). Se incluyen los pesos 400 y 500 del subconjunto latino.
