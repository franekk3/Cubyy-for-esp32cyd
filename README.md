
# <img src="https://github.com/franekk3/Cubyy/blob/main/media/icons/icon.png?raw=true" height="50px" width="50px" align="top"> Cubyy

Online FOSS Speed Cubing timer. 

## Features

 - Full [**PWA**](https://en.wikipedia.org/wiki/Progressive_web_app) support.
 - [**GAN Halo**](https://www.gancube.com/products/gan-halo-smart-timer) Bluetooth smart timer support.
 - Material 3-like user interface (custom-built, inspired by [**Google**](https://m3.material.io/)).
 - All basic speedcubing timer features (time measurement, DNF, +2, statistics, solves list...).
 - Option to export/import solves from/to other timers ([cstimer.net](https://cstimer.net) format).
 - UI designed to suit mobile and desktop.

## Screenshots
#### Home Screen
<table> <tr> <td align="center"><img src="https://github.com/franekk3/Cubyy/blob/main/media/screenshots/screenshot1-mobile.png?raw=true" height="400px"></td><td align="center"> <img src="https://github.com/franekk3/Cubyy/blob/main/media/screenshots/screenshot1-desktop.png?raw=true" width="500px"></td> </tr> </table>

#### Statistics Screen
<table> <tr> <td align="center"><img src="https://github.com/franekk3/Cubyy/blob/main/media/screenshots/screenshot3-mobile.png?raw=true" height="400px"></td><td align="center"> <img src="https://github.com/franekk3/Cubyy/blob/main/media/screenshots/screenshot3-desktop.png?raw=true" width="500px"></td> </tr> </table>

#### Solves List Screen
<table> <tr> <td align="center"><img src="https://github.com/franekk3/Cubyy/blob/main/media/screenshots/screenshot2-mobile.png?raw=true" height="400px"></td><td align="center"> <img src="https://github.com/franekk3/Cubyy/blob/main/media/screenshots/screenshot2-desktop.png?raw=true" width="500px"></td> </tr> </table>

## License
**Cubyy** is licensed under the **GPLv3** License (General Public License version 3).  
[License](https://github.com/franekk3/Cubyy/tree/main?tab=GPL-3.0-1-ov-file)

## Self hosting Cubyy
#### Clone Cubyy repository onto your device:

    git clone https://github.com/franekk3/Cubyy.git

<sub> requires **git** installed </sub>  
or just [download repository as **.zip**](https://github.com/franekk3/Cubyy/archive/refs/heads/main.zip)

#### Enter Cubyy's directory:

    cd Cubyy

#### Setup HTTP server:

    python3 -m http.server
or

    php -S 0.0.0.0:8000
or any other static HTTP server. 

<sup>Remember: features like Web Bluetooth API (for GAN timer) or keeping the device screen on require **HTTPS** (secure connection) or localhost (all features should work if you host the code on the target machine).</sup>
**Done.**
