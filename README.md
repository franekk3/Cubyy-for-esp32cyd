
# Cubyy-for-esp32cyd
### Description:
Cubyy-for-esp32cyd is Free, Open Source speedcubing timer made for cyd (cheap yellow display) board by using Arduino IDE and it's libraries. It uses BLE to connect to GAN Halo Bluetooth Smart Timer, collects state and solve resoults from it, displays them and saves for microSD card (if used). Cubyy-esp32 it self doesn't have option to start timer without bluetooth timer.

### Requirements:
- Esp 32 CYD (tested on ESP32-2432S028)
- Gan halo bluetooth smart timer
- Desktop with Arduino IDE installed
### Setup: 
1. Add ESP board's to Arduino IDE, by going to 

		File --> Preferences --> Additional board manager URL

	input:
   
		https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
	then go to "Board manager"
	and search for "esp32" and install library by "Espressif Systems"
2. Install requied libraries:
- "ArduinoJson" by Benoit Blanchon.
- "NimBLE-Arduino" by h2zero
- "TFT_eSPI" by Bodmer
- "XPT2046_Touchscreen" by Paul Stoffregen
3. Download **Cubyy-for-esp32cyd.ino** file from GitHub repo.
4. Run code
### Pictures:

### Futures: 
- Scramble Generation
- Reading state & results from Gan Timer
- Saving solves to microSD card in cstimer.net format.
- Easy time moving into [Cubyy Web](cubyy.vercel.app) timer, and other speedcubing timers using cstimer.net format. *
<sub>* - works only if you use microSD card. </sub>
### Plans for future updates:
- Local statistics
- Other cubes
- Sessions
- online sync with Cubyy Web? **
<sub>** - maybe in late future, Cubyy currently don't have cloud servers for backups</sub>
