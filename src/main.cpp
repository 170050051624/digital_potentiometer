#include <Arduino.h>
#include "Wire.h"
#include "evc_pt2259.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <RotaryEncoder.h>

#define PIN_IN1 0
#define PIN_IN2 3 //旋转编码器


#define ROTARYSTEPS 1
#define ROTARYMIN 0
#define ROTARYMAX 79 //衰减最大-79dB

// Setup a RotaryEncoder with 2 steps per latch for the 2 signal input pins:
RotaryEncoder encoder(PIN_IN1, PIN_IN2, RotaryEncoder::LatchMode::FOUR3);


const PROGMEM uint8_t SDA_pin = 1; //GPIO1/TX
const PROGMEM uint8_t SCL_pin = 2; //GPIO2

// 音量
const PROGMEM char *MQTT_SPEAKER_VOLUME_STATE_TOPIC = "livingroom/speaker/volume/status";
const PROGMEM char *MQTT_SPEAKER_VOLUME_COMMAND_TOPIC = "livingroom/speaker/volume/set";
struct parameter_type
{
  int volume; // 当前音量
};
parameter_type parameter;

// Last known rotary position.
int lastPos = -1;

 //const char *ssid = "Xiaomi_EB6B";
// const char *password = "mamiao@1995";
const char* ssid = "HUAWEI";//路由器
const char* password = "695583591021";
//const char *mqtt_server = "broker.mqtt-dashboard.com";
//IPAddress MQTT_SERVER_IP(192, 168, 31, 246);
IPAddress MQTT_SERVER_IP(192, 168, 3, 94);
const PROGMEM char *MQTT_USER = "lyl";
const PROGMEM char *MQTT_PASSWORD = "a2d5f7d3";

// 开关
const char *MQTT_SPEAKER_STATE_TOPIC = "livingroom/speaker/status";
const char *MQTT_SPEAKER_COMMAND_TOPIC = "livingroom/speaker/switch";

//OTA模式的标志位
boolean OTA_FLAG = false;

//运行状态
const char *state = "state";
const char *OTA = "livingroom/speaker/OTA";

// payloads by default (on/off)
const char *SPEAKER_ON = "ON";
const char *SPEAKER_OFF = "OFF";
const char *OTA_ON = "ON";

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (50)
char msg[MSG_BUFFER_SIZE];
int value = 0;

void saveConfig()
{
  //Serial.println("Save config!");

  EEPROM.begin(1024);
  uint8_t *p = (uint8_t *)(&parameter);
  for (int i = 0; i < sizeof(parameter); i++)
  {
    EEPROM.write(i, *(p + i));
  }
  EEPROM.commit();
}

void loadConfig()
{
  EEPROM.begin(1024);
  uint8_t *p = (uint8_t *)(&parameter);
  for (int i = 0; i < sizeof(parameter); i++)
  {
    *(p + i) = EEPROM.read(i);
  }
  EEPROM.commit();
  //Serial.println("Read config");
}

void setup_wifi()
{

  delay(10);
  // We start by connecting to a WiFi network
  //.println();
  //Serial.print("Connecting to ");
  //Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    //Serial.print(".");
  }

  randomSeed(micros());

  //Serial.println("");
  //Serial.println("WiFi connected");
  //Serial.println("IP address: ");
  //Serial.println(WiFi.localIP());

  // ArduinoOTA.onStart([]()
  //                    { Serial.println("Start"); });
  // ArduinoOTA.onEnd([]()
  //                  { Serial.println("End"); });
  // ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
  //                       { Serial.printf("Progress: %u%%\n", (progress / (total / 100))); });
  // ArduinoOTA.onError([](ota_error_t error)
  //                    {
  //                      Serial.printf("Error[%u]: ", error);
  //                      if (error == OTA_AUTH_ERROR)
  //                        Serial.println("Auth Failed");
  //                      else if (error == OTA_BEGIN_ERROR)
  //                        Serial.println("Begin Failed");
  //                      else if (error == OTA_CONNECT_ERROR)
  //                        Serial.println("Connect Failed");
  //                      else if (error == OTA_RECEIVE_ERROR)
  //                        Serial.println("Receive Failed");
  //                      else if (error == OTA_END_ERROR)
  //                        Serial.println("End Failed");
  //                    });
  // ArduinoOTA.begin();
}

void callback(char *topic, byte *payload, unsigned int length)
{
  String payload_str;

  //复制payload的内容
    payload[length] = '\0';
  String s = String((char*)payload);


  // for (uint8_t i = 0; i < length; i++)
  // {
  //   payload_str.concat((char)payload_str[i]);
  // }

  // Serial.print("Message arrived [");
  // Serial.print(topic);
  // Serial.print("] ");
  // for (int i = 0; i < length; i++)
  // {
  //   Serial.print((char)payload[i]);
  // }
  // Serial.println();

  //如果在OTA_MODE订阅里收到ON，就把OTA标志位设为ture
  if (String(topic) == OTA)
  {
    if (s.equals(String(OTA_ON)))
    {
      OTA_FLAG = true;

      client.publish(state, OTA);
    }
    else
      OTA_FLAG = false;
  }

  if (String(MQTT_SPEAKER_COMMAND_TOPIC).equals(topic))
  {
    // test if the payload is equal to "ON" or "OFF"
    if (s.equals(String(SPEAKER_ON)))
    {
      pt_mute(false);             // 取消静音
      pt_setAttenuation(parameter.volume); //恢复音量
      
    }
    else if (payload_str.equals(String(SPEAKER_OFF)))
    {
      pt_mute(true); //静音
    }
  }

  if (String(MQTT_SPEAKER_VOLUME_COMMAND_TOPIC).equals(topic))
  {

    parameter.volume = s.toInt();
    pt_setAttenuation(parameter.volume);                 //调整音量
    encoder.setPosition(parameter.volume / ROTARYSTEPS); //设置电位器位置
    saveConfig();
  }
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    //Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(),MQTT_USER,MQTT_PASSWORD))
    {
      //Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("outTopic", "hello world");
      // ... and resubscribe
      client.subscribe("inTopic");
      client.subscribe(MQTT_SPEAKER_VOLUME_COMMAND_TOPIC);
      client.subscribe(OTA);
    }
    else
    {
      //Serial.print("failed, rc=");
      //Serial.print(client.state());
      //Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup()
{
  // put your setup code here, to run once:
  //parameter.volume = 0;
  //Serial.begin(9600);
  setup_wifi();
  client.setServer(MQTT_SERVER_IP, 1883);
  client.setCallback(callback);
  //Serial.print("!!!");
  // //订阅
  // client.subscribe(MQTT_SPEAKER_VOLUME_COMMAND_TOPIC);
  // client.subscribe(OTA);
  // client.publish(MQTT_SPEAKER_VOLUME_STATE_TOPIC, "mqtt contened!");//似乎可以全写在reconnect里
  reconnect();
  pt_init(SDA_pin, SCL_pin); //init pt2259
  pt_mute(false);             // 取消静音

  loadConfig();

  encoder.setPosition(parameter.volume / ROTARYSTEPS); // start with the value of 上次存储的音量
  for (int i = 79; i >= parameter.volume; i--)
  {
    pt_setAttenuation(i);
    delay(40);
  } //音量逐渐增加
}

void loop()
{

  if (OTA_FLAG == false)
  { //如果OTA_FLAG == false，那就正常工作,否则就进入ota模式
    // 正常工作状态的代码

    if (!client.connected())
    {
      reconnect();
    }
    client.loop();

    unsigned long now = millis();
    if (now - lastMsg > 2000)
    {
      lastMsg = now;
      ++value;
      snprintf(msg, MSG_BUFFER_SIZE, "hello world #%ld", value);
      //Serial.print("Publish message: ");
      //Serial.println(msg);
      client.publish("outTopic", msg);
    }

    encoder.tick();
    // get the current physical position and calc the logical position
    int newPos = encoder.getPosition() * ROTARYSTEPS;

    if (newPos < ROTARYMIN)
    {
      encoder.setPosition(ROTARYMIN / ROTARYSTEPS);
      newPos = ROTARYMIN;
    }
    else if (newPos > ROTARYMAX)
    {
      encoder.setPosition(ROTARYMAX / ROTARYSTEPS);
      newPos = ROTARYMAX;
    } // if

    if (lastPos != newPos)
    {
      //Serial.print(newPos);
      //Serial.println();
      lastPos = newPos;
      char buf[9];
     snprintf(buf, 9, "%d", lastPos); //整数转换成字符串，位数随便给的
     client.publish(MQTT_SPEAKER_VOLUME_STATE_TOPIC, buf);
     pt_mute(false);
       pt_setAttenuation(parameter.volume);
       
    } // if
    parameter.volume = newPos;

  
    saveConfig();
  }
  else
  {
    ArduinoOTA.handle(); //OTA时的代码
  }
}





