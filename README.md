# Digital Clock
This project was made by using an ESP32 and some RGBW LED strips.
The whole thing is housed inside a 3D printed case, everything is 3D printed but a diffusing panel on the front to get better colors and brightness

The code is written using the Arduino framework and FastLEDRGBW.h which is needed to work with FastLED and RGBW LEDs.
Insiide the */Code/main/data/* folder are files which need to be uploaded to the ESP32 via SPIFFS, these are needed for the web inteface to work and are formatted to take less space.
You'll find unformatted files inside */WebInterface/*

This is what the web page looks like at the moment:
![Main Page](/WebInterface/main.png)
*Main Page*
![Settings Page](/WebInterface/settings.png)
*Settings Page*

The design is slightly adjusted on mobile for better user experience.
