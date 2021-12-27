#include <Arduino.h>

//Edit and copy to Settings.h

#define SET_IP_SETTING  1  //true=IP-Settings aus programm, false=IP-Settings von Mower. For config see Settings.h

IPAddress myIP(xxx, xxx, xxx, xxx);
IPAddress gateway(xxx, xxx, xxx, xxx);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(xxx, xxx, xxx, xxx);
char *ssid = "xxx";
char *password = "xxx";
const char* APssid = "Ardumower";
const char* APpassword = "";