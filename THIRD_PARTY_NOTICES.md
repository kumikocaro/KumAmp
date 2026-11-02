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

ASIO es una marca comercial y software de Steinberg Media Technologies GmbH.
VST es una marca comercial de Steinberg Media Technologies GmbH.

El código fuente completo de KumAmp, con las instrucciones para compilarlo, está en
https://github.com/kumikocaro/KumAmp
