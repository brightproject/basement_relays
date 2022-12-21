//раздефайнить для использования
//#define DEBUG_ENABLE
#ifdef DEBUG_ENABLE
#define DEBUG(x) Serial.println(x)
#else
#define DEBUG(x)
#endif


// библиотека для работы с Wi-Fi
#include "ESP8266WiFi.h"
// библиотека для работы с MQTT - Arduino Client for MQTT
#include "PubSubClient.h"
// библиотека для работы с датчиками DS18B20
#include "microDS18B20.h"

// WIFI

const char* ssid = "**************";
const char* password = ""**************";

// MQTT
const char* mqtt_server = "wqtt.ru";
const int mqtt_port = "*******";
const char* mqtt_user = "******";
const char* mqtt_password = "******";

bool Sensor1OK = false;   //Состояние сенсора 1
bool Sensor2OK = false;   //Состояние сенсора 2
bool SensorsOK = false;   //Состояние сенсоров
volatile bool preheating_state = false; // состояние преднагрева
// RELAY
const String relay_nagrev = "/nagrev_topic";
const String relay_lamp = "/lamp_topic";

char* relay_nagrev_self = "/nagrev_topic";
char* podval_temp = "/temp_topic";
char* ten_temp = "/ten_topic";
#define RELAY_1 4 //D1
#define RELAY_2 5 //D2
// количество датчиков для удобства
#define DS_SENSOR_AMOUNT 2

// создаём двухмерный массив с адресами
uint8_t addr[][8] = {
  {0x28, 0x1D, 0xF1, 0x48, 0xF6, 0x4E, 0x3C, 0x9E},//датчик ТЭНа подвала
  {0x28, 0xA5, 0xE6, 0x48, 0xF6, 0x86, 0x3C, 0xD7}//датчик температуры воздуха в подвале
};

// указываем DS_ADDR_MODE для подключения блока адресации
// и создаём массив датчиков на пине D3 - цифра 0

MicroDS18B20<0, DS_ADDR_MODE> sensor[DS_SENSOR_AMOUNT];

int TempTarget = 70;          //Целевая температура нагревателя
int AirTarget = 5;          //Целевая температура воздуха в подвале
int Gisterezis = 20;          //Гистерезис ТЭНа
int GisterezisAir = 1;          //Гистерезис воздуха
float celsius1 = 0;       //Текущая температура датчика №1
float celsius2 = 0;       //Текущая температура датчика №2

//переменные millis();
static unsigned long tmr1;// переменная опроса датчиков
//static uint32_t tmr1;//то же, что и unsigned long
//static unsigned long tmr2;// переменная wifi
int t_sensor = 4000; // период опроса датчиков и отправки данных в MQTT
long lastMsg = 0;



WiFiClient espClient;
PubSubClient client(espClient);

bool relay_1 = false;
bool relay_2 = false;

// connecting to a WiFi network

void setup_wifi() {

  delay(10);
  DEBUG("Connecting to ");
  DEBUG(ssid);

  WiFi.mode(WIFI_STA);
  //WiFi.mode(WIFI_AP);//режим работы как точка доступа
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    sensorsRead();
    RelayControl();
    delay(1000);
    DEBUG(".");
  }



  DEBUG("RSSI: ");
  DEBUG(WiFi.RSSI());
  DEBUG("");
  DEBUG("WiFi connected");
  DEBUG("IP address: ");
  DEBUG(WiFi.localIP());
}

void reconnectWifi() {
  while (WiFi.status() != WL_CONNECTED) {
    if (relay_2) {
      DEBUG("relay 2 ON");
      long now = millis();
      if (now - lastMsg > 10000) {
        lastMsg = now;
        relay_2 = false;
        DEBUG("relay 2 OFF");
      }
    }
  }
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    DEBUG(".");
    reconnect();
  }
}

//connecting to a mqtt broker

void reconnect() {
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  while (!client.connected()) {
    DEBUG("Attempting MQTT connection...");
    String clientId = "ESP8266-" + WiFi.macAddress();
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password) ) {
      DEBUG("!!!MQTT connected!!!");
    } else {
      //      DEBUG("Failed with state ");
      //      DEBUG(client.state());
      if (client.state() == -2) DEBUG("MQTT_CONNECT_FAILED(-2) -> the network connection failed");
      if (client.state() == 5) DEBUG("MQTT_CONNECT_UNAUTHORIZED(5) - the client was not authorized to connect");
      if (client.state() == -4) DEBUG("MQTT_CONNECTION_TIMEOUT(-4) - the server didn't respond within the keepalive time");
      //      -4 : MQTT_CONNECTION_TIMEOUT - the server didn't respond within the keepalive time
      //      -3 : MQTT_CONNECTION_LOST - the network connection was broken
      //      -2 : MQTT_CONNECT_FAILED - the network connection failed
      //      -1 : MQTT_DISCONNECTED - the client is disconnected cleanly
      //        0 : MQTT_CONNECTED - the client is connected
      //        1 : MQTT_CONNECT_BAD_PROTOCOL - the server doesn't support the requested version of MQTT
      //        2 : MQTT_CONNECT_BAD_CLIENT_ID - the server rejected the client identifier
      //        3 : MQTT_CONNECT_UNAVAILABLE - the server was unable to accept the connection
      //        4 : MQTT_CONNECT_BAD_CREDENTIALS - the username/password were rejected
      //        5 : MQTT_CONNECT_UNAUTHORIZED - the client was not authorized to connect
      DEBUG("Try again in 2 seconds");
      sensorsRead();
      RelayControl();
      delay(2000);
    }
  }
  client.subscribe( (relay_nagrev + "/#").c_str() );
  client.subscribe( (relay_lamp + "/#").c_str() );
}

void updateStatePins(void) {
  if (relay_1) {
    digitalWrite(RELAY_1, HIGH);

    long now = millis();
    if (now - lastMsg > t_sensor) {
      lastMsg = now;
      client.publish(relay_nagrev_self, "1", true);
    }

    //client.publish(relay_nagrev_self, "ON", true);
  } else {
    digitalWrite(RELAY_1, LOW);
    long now = millis();
    if (now - lastMsg > t_sensor) {
      lastMsg = now;
      client.publish(relay_nagrev_self, "0", true);
    }
  }
  if (relay_2) {
    digitalWrite(RELAY_2, HIGH);
  } else {
    digitalWrite(RELAY_2, LOW);
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String data_relay;
  for (int i = 0; i < length; i++) {
    data_relay += String((char)payload[i]);
  }



  DEBUG(data_relay);

  if ( String(topic) == relay_nagrev ) {
    if (data_relay == "ON" || data_relay == "1") relay_1 = true;
    if (data_relay == "OFF" || data_relay == "0") relay_1 = false;
  }
  if ( String(topic) == relay_lamp ) {
    if (data_relay == "ON" || data_relay == "1") relay_2 = true;
    if (data_relay == "OFF" || data_relay == "0") relay_2 = false;
  }
  updateStatePins();
}

// опрос сенсоров с заданным периодом
void sensorsRead() {

  if (millis() - tmr1 >= t_sensor) {

    // устанавливаем адреса сенсоров
    for (int i = 0; i < DS_SENSOR_AMOUNT; i++) {
      sensor[i].setAddress(addr[i]);
    }


    if (sensor[1].readTemp()) {
      celsius2 = sensor[1].getTemp();
      client.publish(podval_temp,  String(celsius2, 1).c_str(), true);
      DEBUG(celsius2);
      Sensor2OK = true;
            DEBUG("Sensor status ");
            DEBUG(SensorOK);
    }
    else {
      DEBUG("Sensor №1 disappear");
      Sensor2OK = false;
            DEBUG("Sensor status ");
            DEBUG(SensorOK);
    }
    if (sensor[0].readTemp()) {
      celsius1 = sensor[0].getTemp();
      client.publish(ten_temp,  String(celsius1, 1).c_str(), true);
      DEBUG(celsius1);
      Sensor1OK = true;
    }
    else {
      DEBUG("Sensor №2 disappear");
      Sensor1OK = false;
    }
    DEBUG(sensor[1].getTemp());

    // запрашиваем новые
    for (int i = 0; i < DS_SENSOR_AMOUNT; i++) {
      sensor[i].requestTemp();
    }
    sensorsDebug();
    tmr1 = millis();
  }
}

void sensorsDebug() {
  Sensor1OK == 1 && Sensor2OK == 1 ? SensorsOK = true : SensorsOK = false;
  DEBUG("Sensor 1 status ");
  DEBUG(Sensor1OK);
  DEBUG("Sensor 2 status ");
  DEBUG(Sensor2OK);
  DEBUG("Sensors status ");
  DEBUG(SensorsOK);
}

void RelayControl() {
  if (SensorsOK)
  {
    if (!preheating_state) {
      if (celsius1 < (TempTarget - Gisterezis))
      {
        relay_1 = true;
        preheating_state = true;
      }
      else if (celsius1 > TempTarget)
      {
        relay_1 = false;
        preheating_state = false;
      }
    }
    if (preheating_state) {
      if (celsius2 < (AirTarget - GisterezisAir))
      {
        relay_1 = true;
        preheating_state = false;
      }
      else if (celsius2 > AirTarget)
      {
        relay_1 = false;
        preheating_state = true;
      }
    }
    updateStatePins();
  }
  else
  {
    relay_1 = false;
    updateStatePins();
  }
}




void setup() {
  //ArduinoOTA.begin();
  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);
  digitalWrite(RELAY_1, LOW);
  digitalWrite(RELAY_2, LOW);

#ifdef DEBUG_ENABLE
  Serial.begin(115200);
#endif
  //первоначальное подключение к Wi-Fi
  setup_wifi();
  //первоначальное подключение к MQTT
  reconnect();
  RelayControl();
}

void loop() {
  //ArduinoOTA.handle();


  //общие показания dBm:
  //выше −30 dBm: слишком хорошо, чтобы быть правдой, или перенасыщение сигнала (что плохо).
  //−30 dBm: наилучшее из возможного.
  //−50 dBm: отличный сигнал.
  //−60 dBm: очень хороший сигнал.
  //−70 dBm: это порог, при котором вы, возможно, потеряете полоску сигнала и вот-вот потеряете другую, если уже не потеряли. Но связь по-прежнему стабильная.
  //−75 dBm: здесь начинаются проблемы, но соединение всё ещё можно использовать.
  //−80 dBm: граница полезного – у вас едва ли есть только одна полоска.
  //−90 dBm: сигнал очень слабый, к нему (почти) невозможно подключиться.
  //ниже −90 dBm: забудьте об этом.

  //  DEBUG("RSSI: ");
  //  DEBUG(WiFi.RSSI());
  //  delay(2000);
  sensorsRead();
  RelayControl();
  reconnectWifi();
  //    if (!client.connected()) {
  //      reconnect();
  //    }
  client.loop();
}
