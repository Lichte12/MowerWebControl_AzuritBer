#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
//#include <FS.h>
//#include <detail/RequestHandlersImpl.h>
#include <Ticker.h>




const char* basetopic = "Ardumower";
WiFiClient espClient;

#define LED 2
#define BAUDRATE 115200 

#define MAX_CONFIG_LEN  100
#define MSG_HEADER "[WSB]"
#define VERSION "vom 23.07.2020"
#define CONFIG_MSG_START "config:"

#define SET_IP_SETTING  1  //true=IP-Settings aus programm, false=IP-Settings von Mower
IPAddress myIP(xxx, xxx, xxx, xxx);
IPAddress gateway(xxx, xxx, xxx, xxx);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(xxx, xxx, xxx, xxx);
char *ssid = "xxx";
char *password = "xxx";
const char* APssid = "Ardumower";
const char* APpassword = "";

bool wifiConnected = false;
int connectCnt = 0;
ESP8266WebServer server(80); // Webserver-Instanz für Port 80 erstellen

WiFiClient PFODclient;
bool PFODclientConnected = false;

#define MENU    0
#define LINK    1
#define SLIDER  2
#define PLOT    3

#define DEBUG 1

#define PARAMID_SSID    0
#define PARAMID_PASSWD  1
#define PARAMID_LOCALIP 2
#define PARAMID_GATEWAY 3
#define PARAMID_SUBNET  4
#define NBPARAMS (sizeof(params)/sizeof(params[0]))


typedef struct {
  const char* name;
  const char* valueStr;
} param_t;

param_t params[] = {
  {"SSID", ""},
  {"Password", ""},
  {"IPAddress", ""},
  {"Gateway", ""},
  {"Subnet", ""},
};

struct PFODelement {
  String cmd;
  String title;
  String value;
  String sl_min;
  String sl_max;
  String sl_res;
  int type;
};

typedef struct ledSequence_t {
  uint8_t onTicks;
  uint8_t offTicks;
} ledSequence_t;

const ledSequence_t ledSeq_startup        =   {1, 10};
const ledSequence_t ledSeq_waitForConfig  =   {1, 1};
const ledSequence_t ledSeq_connecting  =      {3, 3};
const ledSequence_t ledSeq_connected  =       {1, 0};
const ledSequence_t ledSeq_clientConnected  = {10, 1};
Ticker ledTicker;

PFODelement elemente[30];
int nElemente = 0;

char* SerialMsg = "";
String lastMSG = "";
long timer1s = 0UL;
long counter = 0UL;


void handleSerialInput() {
   if (PFODclientConnected) {
     if (Serial.available()) {
       size_t len = min(Serial.available(), 255);
       char sbuf[len];
       String erg = "";
       Serial.readBytes(sbuf, len);
       PFODclient.write(sbuf, len);
     }
    }
}

void debug(String msg) {
  if (DEBUG) {
    Serial.print(MSG_HEADER);
    Serial.print(" ");
    Serial.print(msg);
  }
}

void debugln(String msg) {
  if (DEBUG) {
    Serial.print(MSG_HEADER);
    Serial.print(" ");
    Serial.println(msg);
  }
}

void flushInput(void) {
  while (Serial.available())
    Serial.read();
}


int count(String s, char parser) {
  int parserCnt = 0;
  for (int i = 0; i < s.length(); i++) {
    if (s[i] == parser) parserCnt++;
  }
  return parserCnt;
}

// LED

struct {
  boolean on;
  uint8_t counter;
  const ledSequence_t* currSequence;
} ledStatus;


void setLed(boolean on) {
  ledStatus.on = on;
  digitalWrite(2, on?HIGH:LOW);
}

void setLedSequence(const struct ledSequence_t& ledSeq) {
  ledStatus.currSequence = &ledSeq;
  ledStatus.counter = 0;
  setLed(true);
}

void onLedTicker(void) {
  ledStatus.counter++;
  if (ledStatus.on) {
    if (ledStatus.counter >= ledStatus.currSequence->onTicks) {
      if (ledStatus.currSequence->offTicks) {
        setLed(false);
      } else {
        // Stay ON
      }
      ledStatus.counter = 0;
    }
  } else {
    if (ledStatus.counter >= ledStatus.currSequence->offTicks) {
      setLed(true);
      ledStatus.counter = 0;
    }
  } 
}


// PARAMS

char configMsg[MAX_CONFIG_LEN];

void waitForParams(void) {
  boolean done = false;
  // Loop until a valid config message is received  
  while (! done) {  
    // Loop until a line starting with "config:" is received
    uint8_t configMsgLen = 0; 
    boolean msgComplete = false; 
    while (! msgComplete) {
      if (Serial.available()) {
        char ch = Serial.read();
        if (ch == '\n' || ch == '\r') {
          msgComplete = true;
          configMsg[configMsgLen] = 0;  // Zero terminate
        } else {
          configMsg[configMsgLen] = ch;
          configMsgLen++;
          if (configMsgLen > MAX_CONFIG_LEN) {
            Serial.println(MSG_HEADER " ERR: Config too long");
            configMsgLen = 0; // discard all we have got so far  
          }
          if (memcmp(configMsg, CONFIG_MSG_START, min(uint8_t(strlen(CONFIG_MSG_START)), configMsgLen)) != 0 ) {
            configMsgLen = 0; // discard all we have got so far 
          }
        }
      }
      yield();    
    }
    
    // Analyze the message
    int i;
    if (strlen(configMsg) >= strlen(CONFIG_MSG_START)) {
      // Split message into parameters
      char* p = configMsg + sizeof(CONFIG_MSG_START)-1;  // start after the "config:"
      for (i=0; i<NBPARAMS && *p; i++) {
        params[i].valueStr = p;
        p = strchr(p, ',');
        if (p) {
          *p = 0;
          p++;
        } else {
          break;
        }
      }
      if (i==NBPARAMS-1) {
        // Correct number of parameters, done
        done = true;
        Serial.println(MSG_HEADER " OK");
      } else {
        Serial.println(MSG_HEADER " ERR: Not enough parameters");
        configMsgLen = 0;
      }
    } else {
      Serial.println(MSG_HEADER " ERR: Expected \"" CONFIG_MSG_START "...\"");
    }  
    delay(100);
    flushInput();
  }  
}



void printParams(void) {
  int i;
  Serial.println(MSG_HEADER " Configuration parameters:");
  for (i=0; i<NBPARAMS; i++) {
    Serial.print(MSG_HEADER "    ");
    Serial.print(params[i].name);
    Serial.print(" = \"");
    Serial.print(params[i].valueStr);
    Serial.println("\"");
  }
}

void str2IpAddr(const char* str, IPAddress* ip) {
  int i;
  for (i=0; i<4; i++) {
    (*ip)[i]=atoi(str);
    str=strchr(str,'.');
    if (str)
      str++;
    else
      break;
  }
}


void disconnectPFODClient(void) {
  PFODclient.stop();
  PFODclientConnected = false;
  flushInput();
}

// WIFI
void connectWIFI() {
  // Get configuration message
  if (SET_IP_SETTING == 0) {
    Serial.println(MSG_HEADER " Wait for settings from serial...");
    setLedSequence(ledSeq_waitForConfig);
    waitForParams();
    printParams();
    if (strlen(params[PARAMID_LOCALIP].valueStr) > 0) {
      str2IpAddr(params[PARAMID_LOCALIP].valueStr, &myIP);
      str2IpAddr(params[PARAMID_GATEWAY].valueStr, &gateway);
      str2IpAddr(params[PARAMID_SUBNET].valueStr, &subnet);
    }
    //WiFi.mode(WIFI_STA);
    WiFi.begin(params[PARAMID_SSID].valueStr, params[PARAMID_PASSWD].valueStr);
    delay(250);
    WiFi.config(myIP, gateway, subnet);
  } else {
    //WiFi.mode(WIFI_AP_STA);
    //WiFi.softAP(APssid, APpassword);
    //WiFi.config(myIP, gateway, subnet);
    WiFi.begin(ssid,password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }    
    Serial.println(MSG_HEADER " Connecting with programmed settings");
    Serial.println(WiFi.localIP());
    //Serial.println(WiFi.softAPIP());   
  }
}

void Check_WIFI() {
  // Handle AccessPoint connection
  if (WiFi.status() == WL_CONNECTED) {
    // Connected to AP
    if (!wifiConnected) {
      // Transition Disconnected => Connected
      WiFi.config(myIP, gateway, subnet);
      wifiConnected = true;
      setLedSequence(ledSeq_connected);
      Serial.print(MSG_HEADER " CONNECTED! ");
      Serial.print(" IP address: ");
      Serial.println(WiFi.localIP());
    }
  } else {
    // Disconnected from AP
    if (wifiConnected) {
      // Transition Connected => Disconnected
      wifiConnected = false;
      setLedSequence(ledSeq_connecting);
      Serial.print(MSG_HEADER " DISCONNECTED");
      disconnectPFODClient();
      connectCnt = 0;
    }
    Serial.print(MSG_HEADER " Connecting ...");
    Serial.println(connectCnt);
    connectCnt++;
    delay(250);
  }
}


// TCP_Client
long timeout = 0UL;
long timeout2 = 0UL;
#define TIMEOUT_TIME  10000UL


void handlePFODclient() {
  // Handle Client connection
  if (PFODclientConnected) {
    if (!PFODclient.connected())  {
      // Client is disconnected
      disconnectPFODClient();
      setLedSequence(ledSeq_connected);
      Serial.println(MSG_HEADER " Client Disconnected");
    }
 }
 if (PFODclientConnected) {
    // Send all bytes received form the client to the Serial Port
    if (PFODclient.available()) {
      timeout = millis();
      while (PFODclient.available()) {
        size_t len = min(PFODclient.available(), 255);
        uint8_t sbuf[len];
        PFODclient.read(sbuf, len);
        Serial.write(sbuf, len);
      }
    }    
  }
  //Timeout for Connection
  if ( millis() > timeout + TIMEOUT_TIME)disconnectPFODClient();    
}


String getSkaleVal(String value, String skale) {
  float fValue = value.toFloat();
  float fskale = skale.toFloat();
  float wert = 0.0;
  String erg = "";
  wert = fValue * fskale;
  if (fskale >= 1.0) {
    erg = String(long(wert));
  } else if (fskale == 0.001) {
    erg = String(wert, 3);
  } else if (fskale == 0.01) {
    erg = String(wert, 2);
  } else if (fskale == 0.1 ) {
    erg = String(wert, 1);
  } else {
    erg = String(wert, 1);
  }
  //debugln("fskale=" + String(fskale, 5) + " fValue=" + String(fValue) + " erg=" + erg + " wert=" + wert);
  return erg;
}

String split(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;
  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}



void parse_PFOD(String s) {
  String element = "";
  String params = "";
  String temp;
  if (!s.startsWith("{")){
    s = s.substring(s.indexOf("{"),s.indexOf("}")+1);
  }  
  if (!s.startsWith("{") && !s.endsWith("}")) return;  
  debugln("Parse String: " + s);
  s.replace("{", "");
  s.replace("}", "");
  //s.replace("\n", "");
  //s.replace("\r", "");
  nElemente = count(s, '|') + 1;
  for (int i = 0; i < nElemente; i++) {
    element = split(s, '|', i);
    //Element auswerten
    if (count(element, '~') == 0) {
      //es handelt sich um die Menü-Überschrift
      elemente[i].cmd = ".";
      if (count(element, '`') > 0) {
        temp = split(element, '`', 0);
        elemente[i].value = split(element, '`', 1); //Refresh-Time
      }
      else temp = element;
      temp.replace(".", "");
      elemente[i].title = temp;
      elemente[i].type = MENU;
    } else if (count(element, '~') == 1) {
      //es handelt sich um ein text-Element (bzw. link)
      elemente[i].cmd = split(element, '~', 0);
      elemente[i].title = split(element, '~', 1);
      elemente[i].value = split(element, '~', 1);
      elemente[i].type = LINK;
    } else if (count(element, '~') == 3) {
      //es handelt sich um ein Slider-Element
      String params = split(element, '~', 1);
      elemente[i].cmd = split(element, '~', 0);
      elemente[i].title = split(params, '`', 0);
      elemente[i].sl_res = split(element, '~', 3);
      elemente[i].value = getSkaleVal(split(params, '`', 1), elemente[i].sl_res);
      elemente[i].sl_max = getSkaleVal(split(params, '`', 2), elemente[i].sl_res);
      elemente[i].sl_min = getSkaleVal(split(params, '`', 3), elemente[i].sl_res);
      elemente[i].type = SLIDER;
    }
    elemente[i].title.replace("=",":");
    elemente[i].value.replace("=",":");    
    debug("CMD " + elemente[i].cmd);
    debug(" ,title " + elemente[i].title);
    debug(" ,value " + elemente[i].value);
    debug(" ,sl_min " + elemente[i].sl_min);
    debug(" ,sl_max " + elemente[i].sl_max);
    debug(" ,sl_res " + String(elemente[i].sl_res));
    debugln(" ,typ " + String(elemente[i].type));
  }
}

String getElementValue(String cmd) {
  for (int i = 0; i < nElemente; i++) {
    if (elemente[i].cmd == cmd) {
      return elemente[i].value;
    }
    return "na";
  }
}

// Werte an Browser senden
void handleWerte() {
  debugln("handleWerte");

  int anzahl = server.args();
  String erg = "";
  if (!PFODclientConnected) {
    if (anzahl == 2) {
      //Zwei Parameter werden erwartet: z.b. cmd={s1}&refresh=true
      if (server.hasArg("cmd") && server.hasArg("refresh")) {
        //Parameter vorhanden

        if (!PFODclientConnected) {
          Serial.println("{" + server.arg(0) + "}"); //Kommando an Ardumower senden
          String buf = Serial.readStringUntil('}');
          //buf = msgTest3;
          if (buf != "") {
            //Daten empfangen
            parse_PFOD(buf);
            erg = "con=Ardumower connection OK...&";
            for (int i = 0; i < nElemente; i++) {
              if (elemente[i].type == SLIDER) {
                if (server.arg(1) == "false" || DEBUG == 1) {
                  //Paramtersatz vollständig übertragen
                  erg += elemente[i].cmd + "_min=" + elemente[i].sl_min + "&";
                  erg += elemente[i].cmd + "_max=" + elemente[i].sl_max + "&";
                  erg += elemente[i].cmd + "_res=" + elemente[i].sl_res + "&";
                }
                erg += elemente[i].cmd + "_val=" + elemente[i].value;
                if (i < nElemente - 1) erg += "&"; //es gibt weitere elemente
              } else if (elemente[i].type == LINK) {
                erg += elemente[i].cmd + "=" + elemente[i].value;
                if (i < nElemente - 1) erg += "&"; //es gibt weitere elemente
              }
            }//for elemente
          } else {
            erg = "con=Ardumower not connected...";
          }
        }

        debugln(erg);
        server.send( 200, "text/plain", erg );
      } else {
        server.send(500, "text/plain", "falsche Parameter: z.B. cmd=s1&refresh=true wird erwartet!");
      }
    } else {
      server.send(500, "text/plain", "zuwenig Parameter");
    }
  } else {
    erg = "con=Ardumower connected to PFOD-App...";
    debugln(erg);
    server.send( 200, "text/plain", erg );
  }
}

void handleSet() {
  debugln("handleSet");
  int anzahl = server.args();
  String erg = "";
  for (int i = 0; i < anzahl; i++) {
    if (server.arg(i) == "true") {
      //Kommando kommt von einem button
      Serial.println("{" + server.argName(i) + "}"); //Kommando an Ardumower senden

    } else {
      //kommt von einem slider
      Serial.println("{" + server.argName(i) + "`" + server.arg(i) + "}"); //Kommando an Ardumower senden
    }

    if (anzahl == 0) {
      server.send(500, "text/plain", "zuwenig Parameter");
    } else server.send( 200, "text/plain", "OK" );
  }
}

// File an Browser senden
bool handleFileRead(String path) {
  debugln("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";
  String contentType = esp8266webserver::getContentType(path);
  if (SPIFFS.exists(path)) {
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

// Rootverzeichnis
void handleRoot() {
  debugln("handleRoot");
  if (!handleFileRead(server.uri()))  server.send(404, "text/plain", "FileNotFound");
}



void setup() {
  // Configure Serial Port
  Serial.begin(BAUDRATE);
  Serial.setTimeout(500);

  // Configure LED
  pinMode(LED, OUTPUT);
  setLedSequence(ledSeq_startup);
  ledTicker.attach(0.1, onLedTicker);

  // Welcome message
  delay(500);
  Serial.println("\n\n");
  Serial.println(MSG_HEADER " ESP8266 Serial WIFI Bridge with Webinterface " VERSION);

 
  connectWIFI();
  
  
  setLedSequence(ledSeq_connecting);

  if (!SPIFFS.begin()) {
    debugln("SPIFFS not initialized! Stop!");
    while (1) yield();
  }
  // add handler for webserver
  server.onNotFound([]() {
    if (!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });
  // Die Abfrage auf die reine URL '/' wird auf '/index.html' umgelenkt
  server.serveStatic("/", SPIFFS, "/index.html");
  // Files ausliefern
  server.on("/", HTTP_GET, handleRoot);
  // LED steuern
  server.on("/set", HTTP_POST, handleSet);
  // Werte abrufen
  server.on("/werte", HTTP_POST, handleWerte);

  server.begin(); // Web-Server starten
  debugln("HTTP Server running on Port 80");
  flushInput();
}

void loop() {
  Check_WIFI();
  server.handleClient();  //Webserver
  handlePFODclient();
  handleSerialInput();
  if (millis() > timer1s + 1000UL) {
    timer1s = millis();
  }
}
