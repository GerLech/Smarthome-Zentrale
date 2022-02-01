#include <Adafruit_GFX.h> //Grafik Bibliothek
#include <Adafruit_ILI9341.h> // Display Treiber
#include <ArduinoJson.h> //JSON Unterstützung
#include <WiFi.h> 
#include <ESPmDNS.h>

#include <SPIFFS.h> //SPI Flash Filesystem
#include <FS.h> //Arduino file Funktionen

#include <XPT2046_Touchscreen.h> //Touchscreen Treiber
#include <TouchEvent.h> //Auswertung von Touchscreen Ereignissen

#include <TFTForm.h> //Konfiguration über Touchscreen
#include "fonts/AT_Standard9pt7b.h"

#include <PubSubClient.h>
#include <ArduiTouchSmart.h>

//Definitionen der verwendeten Pins
#define TFT_CS   5
#define TFT_DC   4
#define TFT_MOSI 23
#define TFT_CLK  18
#define TFT_RST  22
#define TFT_MISO 19
#define TFT_LED  15
#define TOUCH_CS 14
#define TOUCH_IRQ 2

//die Instanzen der verwendeten Klassen
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
TouchEvent tevent(touch);
TFTForm tftconf(&tft, NULL);
WiFiClient espClient;
PubSubClient client(espClient);
ArduiTouchSmart ats = ArduiTouchSmart(&tft,&tftconf,&AT_Standard9pt7b, true, true);



//Formular zur Konfiguration
String wlanform = "["
  "{"
  "'name':'ssid',"
  "'label':'SSID des WLAN',"
  "'type':'select'"
  "},"
  "{"
  "'name':'pwd',"
  "'label':'WLAN Passwort',"
  "'type':'password'"
  "},"
  "{"
  "'name':'broker',"
  "'label':'MQTT Broker',"
  "'type':'text'"
  "},"
  "{"
  "'name':'user',"
  "'label':'MQTT User',"
  "'type':'text'"
  "},"
  "{"
  "'name':'mqpwd',"
  "'label':'MQTT Passwort',"
  "'type':'password'"
  "},"
  "{"
  "'name':'cmndpraefix',"
  "'label':'Komnd.Präfix',"
  "'type':'text',"
  "'default':''"
  "},"
  "{"
  "'name':'statpraefix',"
  "'label':'Status Präfix',"
  "'type':'text',"
  "'default':''"
  "},"
  "{"
  "'name':'hostname',"
  "'label':'Hostname',"
  "'type':'text'"
  "},"
  "{"
  "'name':'ntpserver',"
  "'label':'NTP Server',"
  "'type':'text'"
  "}"
  "]";

int8_t subemnu1[12] = {
    CMD_PLAY,
    CMD_PAGEPLUS,
    CMD_PAGEMINUS,
    CMD_WDGPLUS,
    CMD_WDGMINUS,
    CMD_EDITPAGE,
    CMD_EDIT,
    CMD_EXCHANGE,
    CMD_SAVE,
    CMD_READ,
    CMD_LEFT,
    CMD_RIGHT
  };


//die globalen Variablen, die wir konfigurieren
char ssid[50];   //Name des WLAN
char password[30];  //Passwort
char broker[128] = "raspberrypi4";  //Adresse des MQTT Brokers
char user[128] = "broker"; //Benutzername für Broker
char mqpwd[128] = "Broker1"; //Passwort für den Broker
char cmndpraefix[10] = "cmnd"; //Präfix für MQQT Kommando Nachrichten
char statpraefix[10] = "stat"; //Präfix für MQTT Status Nachrichten
char hostname[30] = "MySmarthome";  //Name mit der die Zentrale im WLAN gefunden werden kann
char ntpserver[128] = "de.pool.ntp.org"; //Adresse eine Zeit-Servers

//andere globale Variablen
bool connected; //WLAN Verbindung existiert
uint32_t lt = 0; //Letzter Zeitstempel
bool ledOn = false; //Zeigt, dass Bildschirm eingeschaltet ist
bool info = false; //Zeigt dass Information angezeigt wird
bool wlan = false; //zeigt dass WLAN Formular angezeigt wird
String clientId; //ID für MQTT
uint32_t nextConnect = 0; //nächster Verbindungsversuch

//zeigt eine Zeile von unserem Infoschirm an
void showLine(uint16_t * y, const char label[], const char value[]) {
  tft.setCursor(5,*y); 
  tft.print(label);
  tft.setCursor(80,*y);
  tft.print(value);
  *y += 15;
}

//zeigt den Infoschirm an
void showInfo() {
  float num;
  uint8_t z;
  uint16_t y = 25;
  info = true;
  //Vorbereitung der Textausgabe
  tft.fillScreen(ILI9341_WHITE);
  tft.setFont(NULL); //wir verwenden die internen 5x7 Schrift 
  tft.setTextColor(ILI9341_BLACK);
  tft.setTextWrap(false);
  showLine(&y,"WLAN SSID",ssid);
  if (connected) {
    showLine(&y,"Verbunden","Ja");
  } else {
    showLine(&y,"Verbunden","Nein");
  }
  showLine(&y,"MQTT Broker",broker);
  showLine(&y,"NTP Server",ntpserver); 
  showLine(&y,"Hostname",hostname); 
  showLine(&y,"IP-Adr.",WiFi.localIP().toString().c_str());
  showLine(&y,"MAC Adresse:",WiFi.macAddress().c_str()); 
  showLine(&y,"Freier RAM",""); 
  num = ESP.getFreeHeap()/1024;  tft.printf("%.2f kByte",num);
  showLine(&y,"Rev.Nr",""); 
  z = ESP.getChipRevision(); tft.print(z);
  showLine(&y,"CPU Freq",""); 
  z = ESP.getCpuFreqMHz(); tft.printf("%i MHz",z);
}

//Verbindung zum WLAN wird hergestellt
bool initWiFi() {
    boolean connected = false;
    //Stationsmodus
    WiFi.mode(WIFI_STA);
    //wenn eine SSID konfiguriert wurde versuchen wir uns anzumelden
    if (strlen(ssid) != 0) {
      Serial.print("Verbindung zu ");
      Serial.print(ssid);
      Serial.println(" herstellen");
      //Verbindungsaufbau starten
      WiFi.begin(ssid,password);
      uint8_t cnt = 0;
      //10 Sekunden auf erfolgreiche Verbindung warten
      while ((WiFi.status() != WL_CONNECTED) && (cnt<20)){
        delay(500);
        Serial.print(".");
        cnt++;
      }
      Serial.println();
      //Wenn die Verbindung erfolgreich war, wird die IP-Adresse angezeigt
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("IP-Adresse = ");
        Serial.println(WiFi.localIP());
        connected = true;
      }
    }
    return connected;
}

//Verbindung zum Broker überprüfen und falls nötig wiederherstellen
void checkMQTT() {
  const char * thema;
  if (!client.connected() && (millis() > nextConnect)) {
    //Wir haben keine Verbindung, also Wiederherstellung erforderlich
    Serial.print("Versuche MQTT Verbindung zu ");
    Serial.print(broker);
    Serial.print(" mit ID ");
    Serial.println(clientId);
    client.setServer(broker,1883);
    boolean con = false;
    if (user != "") {
      con = client.connect(clientId.c_str(),user,mqpwd);
    } else {
      con = client.connect(clientId.c_str());
    }
    if (con) {
      Serial.println("Verbunden");
      //Nach erfolgreicher Verbindung werden die Themen abonniert die
      //ein Status Präfix haben
      char tp[50];
      strcpy(tp,statpraefix); strcat(tp,"/#");
      client.subscribe(tp);
      if (client.connected()) {
        nextConnect=0;
      } else {
        Serial.println("Fehler");
        nextConnect = millis()+5000;
      }
    } else {
      Serial.print("Fehlgeschlagen, rc=");
      Serial.print(client.state());
      Serial.println(" Nächster Versuch in 5 s");
      nextConnect = millis()+5000;
    }
  }
}

//serialisiert die Konfigurationswerte in einem JSON String
String getConfJson() {
  char buf[500];
  StaticJsonDocument<500> doc;
  doc["ssid"]=ssid;
  doc["pwd"]=password;
  doc["broker"]=broker;
  doc["user"]=user;
  doc["mqpwd"]=mqpwd;
  doc["cmndpraefix"]=cmndpraefix;
  doc["statpraefix"]=statpraefix;
  doc["hostname"]=hostname;
  doc["ntpserver"]=ntpserver;
  serializeJson(doc,buf);
  return String(buf);
}

//setzt die Konfigurationswerte aus einem JSON String
void setConf(String json) {
    StaticJsonDocument<500> doc;
    deserializeJson(doc,json);
    if (doc.containsKey("ssid")) strlcpy(ssid, doc["ssid"],30);
    if (doc.containsKey("pwd")) strlcpy(password, doc["pwd"],30);
    if (doc.containsKey("broker")) strlcpy(broker, doc["broker"],128);
    if (doc.containsKey("user")) strlcpy(user, doc["user"],128);
    if (doc.containsKey("mqpwd")) strlcpy(mqpwd, doc["mqpwd"],128);
    if (doc.containsKey("cmndpraefix")) strlcpy(cmndpraefix, doc["cmndpraefix"],10);
    if (doc.containsKey("statpraefix")) strlcpy(statpraefix, doc["statpraefix"],10);
    if (doc.containsKey("hostname")) strlcpy(hostname, doc["hostname"],30);
    if (doc.containsKey("ntpserver")) strlcpy(ntpserver, doc["ntpserver"],128);
}

//zeigt Formular zur WLAN Konfiguration an
void showWlanForm(){
  //Formular erstellen
  tftconf.setDescription(wlanform);
  //Vorgabewerte setzen
  tftconf.setValues(getConfJson().c_str());
  //Wir füllen die Auswahlliste für ssid mit den Namen der gefundenen Netzwerke
  int n = WiFi.scanNetworks();
  if (n>0) {
    int8_t ix = tftconf.findName("ssid");
    if (ix>=0) {
      tftconf.clearOptions("ssid");
      for (uint8_t i = 0; i<n; i++) tftconf.addOption(ix,WiFi.SSID(i));
    }
  }
  wlan = true;
  tftconf.showForm();
}

//schaltet die Hintergrundbeleuchtung des Displays ein
void displayLed(bool on) {
  digitalWrite(TFT_LED, !on);
  ledOn = on;
}

//speichert die Konfiguration im SPIFFS
void saveConfiguration(String conf) {
    File f = SPIFFS.open("/wlansetup.jsn","w+");
    if (f) {
      Serial.println("Setup wird gespeichert:");
      Serial.println(conf);
      f.print(conf);
      f.close();
    }  
}

//liest Konfiguration vonm Filesystem falls vorhanden
//falls das File nicht gelesen werden kann ist der Rückgabewert ""
String loadConfiguration() {
  if (SPIFFS.exists("/wlansetup.jsn")) {
    File f = SPIFFS.open("/wlansetup.jsn","r");
    if (f) {
      String data = f.readString();
      f.close();
      return data; 
    }
  }
  return "";
}

/*****************************************************
/ Callback Funktionen
*****************************************************/

//wird immer aufgerufen wenn ein Touchscreen-Ereignis auftritt
void onTouchEvent(int16_t x, int16_t y, EV event) {
  if (event == EV::EVT_CLICK) {
    Serial.printf("Klick x=%i y=%i \n",x,y);
    if (tftconf.isActive()) {
      tftconf.handleClick(x,y);
    } else {
      if ((x<20) && (y>300)) {
        info=false;
        ats.setEditForm(FRM_EDITEXTERN);
        showWlanForm();
      } else {
        ats.touchEvent(x,y,event);
      }
    }
  } else {
    ats.touchEvent(x,y,event);
  }
}

//wird immer aufgerufen, wenn ein Formular mit Save beendet wird
void onConfSave(String data) {
  if (wlan) {
    saveConfiguration(data);
    ESP.restart();
  } else {
    ats.endEdit(data);
  }
}

//wird immer aufgerufen wenn ein Formular mit Cancel beendet wird
void onConfCancel() {
  wlan = false;
  ats.endEdit("");
}

//Empfangene Daten vom MQTT Broker auswerten
void onMQTTData(char* topic, byte* payload, unsigned int length) {
  //Das Thema wird zur Protokollierung ausgegeben
  payload[length]=0;
  Serial.println(topic);
  Serial.println((char *)payload);
  char * pch = strchr(topic,'/');
  if (pch == NULL) {
    ats.updateTopic(topic,(char *)payload);
  } else {
    //Thema ohne Präfix
    ats.updateTopic(pch+1,(char *)payload);
  }
}

//Eine Nachricht an den MQTT Broker senden
bool publish(const char * thema, const char * message) {
  char tp[128];
  if (client.connected()) {
    strcpy(tp,cmndpraefix); strcat(tp,"/");strcat(tp,thema);
    Serial.printf("MQTT send %s -> %s\n",tp,message);
    client.publish(tp,message,true);
    return true;
  } else {
    return false;
  }
}

//Setup
void setup() {
  Serial.begin(115200);
  //SPIFFS starten und falls erforderlich formatieren
  if(!SPIFFS.begin(true)){
      Serial.println("SPIFFS Mount Failed");
  }
  //Display einschalten und initialisieren
  pinMode(TFT_LED,OUTPUT);
  displayLed(true);
  tft.begin();
  tft.fillScreen(ILI9341_BLACK);
  //Wir versuchen die Konfigurtion zu laden
  String conf = loadConfiguration();
  //wenn das Laden erfolgreich war setzen wir die Variablen
  if (conf != "") setConf(conf);
  //nun versuchen wir eine WLAN Verbindung herzustellen
  connected = initWiFi();
  //MQTT starten
  //Als MQTT Client-Id wird die MAC-Adresse verwendet
  clientId=WiFi.macAddress();
  clientId.replace(":","");
  //Die MQTT Verbindungsparameter werden gesetzt
  client.setServer(broker,1883);
  //Callback Funktion wenn Daten empfangen wurden
  client.setCallback(onMQTTData);

  //Touchscreen vorbereiten
  touch.begin();
  tevent.setResolution(tft.width(),tft.height());
  tevent.setDrawMode(false);
  //Callback Funktionen registrieren
  tevent.registerOnAllEvents(onTouchEvent);
  tftconf.registerOnSave(onConfSave);
  tftconf.registerOnCancel(onConfCancel);
  
  //Multicast DNS starten
  if (MDNS.begin(hostname)) {
    Serial.println("MDNS responder gestartet");
  }
  //callback für publish definieren
  ats.registerOnPublish(publish);
  //Untermenü hinzufügen
  ats.addMenu(subemnu1);
  //leere Seite einfügen
  ats.addPage();
  //falls vorhanden Seiten und Widgets aus dem SPIFFS laden 
  ats.loadAllPages();
}

void loop() {
  if (connected) {
    //wenn eine Verbindung besteht
    //wird die Verbindung zum Broker getestet
    checkMQTT();
    if (client.connected()) {
      //Auf neue Nachrichten vom Broker prüfen
      client.loop();
    }
  }
  ats.timeEvent(connected,false);
  tevent.pollTouchScreen();
}
