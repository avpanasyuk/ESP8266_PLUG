# ESP8266_PLUG
Firmware replacement for SONOFF S31 "smart plug", so it can be controlled over LAN. It is built using platformIO, all necessary 
libraries can be found in platformio.ini file.

I really love SONOFF S31 plug, for $9 from Amazon you get not only power control module which can be easily reprogrammed, but voltage/current/power monitor as well.

Firmware notes:
You may want to change #define NAME and set default ssid and password. ssid and password can be set later as well.

Behaviour:
If the plug can not connect to its most recent SSID it goes into Access Point Mode. You can find its WiFi network which will have the same as as "#define NAME" and connect to it. 
After you are connected you can browse to 192.168.4.1 and its home page will open. Here you can set proper SSID and password, see help and set calibration if you want to.
To set calibration you will need thirt-party reference device. Just enter the ratio between the reference and what you get from URL "S31/read" into corresponding fields.
The plugs I got were just a couple of percent off, and the calibrated value is set in firmware as a default factor.
