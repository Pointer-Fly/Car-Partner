#include "SGP30.h"
#include <Wire.h>
#include <WiFi.h>
#include <FastLED.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DFRobot_DHT11.h>
#include <vector>

#define SCREEN_WIDTH 128 // 设置OLED宽度,单位:像素
#define SCREEN_HEIGHT 64 // 设置OLED高度,单位:像素
#define OLED_RESET 4     // 自定义重置引脚,虽然教程未使用,但却是Adafruit_SSD1306库文件所必需的
#define LED_PIN 15
#define NUM_LEDS 1
#define GpsSerial Serial
#define DebugSerial Serial

#define FONT_SIZE 1

#define WIFI_SSID "M20"
#define WIFI_PASS "@@806321@@806321"

#define MQTT_SERVER "124.222.71.199"
#define MQTT_PORT 1883
#define MQTT_PUB_TOPIC "/order/anti/publish"
#define MQTT_SUB_TOPIC "/order/anti/subscribe"

DFRobot_DHT11 DHT;
#define DHT11_PIN 16
#define BUZZER_PIN 17
#define BUZZER_KEY 0
bool buzzer_key_state = false;
#define BLUE_LED_PIN 23
// 红外接收器引脚
#define IR_RECEIVE_PIN 5
#define IR_RECEIVE_PIN_2 13
#define VIBRATION_SENSOR 18
bool vibration_sensor_state = false;
#define DEBUG_LED 2

CRGB leds[NUM_LEDS];
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WiFiClient tcpClient;
PubSubClient mqttClient;
const uint16_t mqtt_client_buff_size = 4096; // 客户端缓存大小（非必须）
String mqtt_client_id = "esp32_client";      // 客户端ID

// 函数声明
void words_display(void);
void reconnect(void);
void mqtt_callback(char *topic, byte *payload, unsigned int length);
void deal_message(char *topic, byte *payload, unsigned int length);
SGP mySGP30;

u16 CO2Data = 0, TVOCData = 0; // 定义CO2浓度变量与TVOC浓度变量
u32 sgp30_dat = 0;

struct
{
  char GPS_Buffer[80];
  bool isGetData;     // 是否获取到GPS数据
  bool isParseData;   // 是否解析完成
  char UTCTime[11];   // UTC时间
  char latitude[11];  // 纬度
  char N_S[2];        // N/S
  char longitude[12]; // 经度
  char E_W[2];        // E/W
  bool isUsefull;     // 定位信息是否有效
} Save_Data;

const unsigned int gpsRxBufferLength = 600;
char gpsRxBuffer[gpsRxBufferLength];
unsigned int ii = 0;
bool IR_RECEIVE_STATE = false;
std::vector<bool> ir_receive_states;

void gps_task(void *arg);
void report_task(void *arg);
void key_scan_task(void *arg);
void ir_report_task(void *arg);

void setup()
{
  GpsSerial.begin(9600); // 定义波特率9600，和我们店铺的GPS模块输出的波特率一致
  DebugSerial.begin(9600);
  delay(1000);

  Save_Data.isGetData = false;
  Save_Data.isParseData = false;
  Save_Data.isUsefull = false;

  mySGP30.SGP30_Init();
  mySGP30.SGP30_Write(0x20, 0x08);
  sgp30_dat = mySGP30.SGP30_Read(); // 读取SGP30的值
  CO2Data = (sgp30_dat & 0xffff0000) >> 16;
  TVOCData = sgp30_dat & 0x0000ffff;
  // SGP30模块开机需要一定时间初始化，在初始化阶段读取的CO2浓度为400ppm，TVOC为0ppd且恒定不变，因此上电后每隔一段时间读取一次
  // SGP30模块的值，如果CO2浓度为400ppm，TVOC为0ppd，发送“正在检测中...”，直到SGP30模块初始化完成。
  // 初始化完成后刚开始读出数据会波动比较大，属于正常现象，一段时间后会逐渐趋于稳定。
  // 气体类传感器比较容易受环境影响，测量数据出现波动是正常的，可自行添加滤波函数。
  while (CO2Data == 400 && TVOCData == 0)
  {
    mySGP30.SGP30_Write(0x20, 0x08);
    sgp30_dat = mySGP30.SGP30_Read();         // 读取SGP30的值
    CO2Data = (sgp30_dat & 0xffff0000) >> 16; // 取出CO2浓度值
    TVOCData = sgp30_dat & 0x0000ffff;        // 取出TVOC值
    Serial.println("正在检测中...");
    delay(500);
  }

  // 连接WiFi
  Serial.printf("\nConnecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("ok.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // 设置MQTT客户端
  mqttClient.setClient(tcpClient);
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setBufferSize(mqtt_client_buff_size);
  mqttClient.setCallback(mqtt_callback);

  // 初始化Wire库
  Wire.begin();

  // 初始化OLED并设置其IIC地址为 0x3C
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  // set buzzer output
  pinMode(BUZZER_PIN, OUTPUT);
  // set buzzer key input
  pinMode(BUZZER_KEY, INPUT);
  digitalWrite(BUZZER_PIN, HIGH);
  // set blue led output
  pinMode(BLUE_LED_PIN, OUTPUT);
  digitalWrite(BLUE_LED_PIN, LOW);
  // set IR receiver input
  pinMode(IR_RECEIVE_PIN, INPUT);
  pinMode(IR_RECEIVE_PIN_2, INPUT);
  // set vibration sensor input
  pinMode(VIBRATION_SENSOR, INPUT);

  xTaskCreate(gps_task, "gps_task", 4096 * 2, NULL, 5, NULL);
  xTaskCreate(key_scan_task, "key_scan_task", 4096 * 2, NULL, 2, NULL);
  xTaskCreate(report_task, "report_task", 4096 * 2, NULL, 6, NULL);
  xTaskCreate(ir_report_task, "ir_report_task", 4096 * 2, NULL, 4, NULL);
}

void loop()
{
  if (!mqttClient.connected())
  {
    reconnect();
    Serial.println("reconnect");
  }
  mqttClient.loop();
  words_display();
  display.display();

  mySGP30.SGP30_Write(0x20, 0x08);
  sgp30_dat = mySGP30.SGP30_Read();         // 读取SGP30的值
  CO2Data = (sgp30_dat & 0xffff0000) >> 16; // 取出CO2浓度值
                                            //  TVOCData = sgp30_dat & 0x0000ffff;        //取出TVOC值
                                            //  Serial.print("CO2:");
                                            //  Serial.print(CO2Data, DEC);
                                            //  Serial.println("ppm");
                                            //  Serial.print("TVOC:");
                                            //  Serial.print(TVOCData, DEC);
                                            //  Serial.println("ppd");

  DHT.read(DHT11_PIN);

  if (DHT.temperature >= 35)
  {
    leds[0] = CRGB::Red;
    FastLED.show();
    digitalWrite(BUZZER_PIN, LOW);
  }
  else if (DHT.temperature <= 23)
  {
    leds[0] = CRGB::Blue;
    FastLED.show();
    digitalWrite(BUZZER_PIN, HIGH);
  }
  else
  {
    leds[0] = CRGB::Green;
    digitalWrite(BUZZER_PIN, HIGH);
    FastLED.show();
  }

  if (buzzer_key_state)
  {
    digitalWrite(BUZZER_PIN, LOW);
  }
  else
  {
    digitalWrite(BUZZER_PIN, HIGH);
  }

  delay(500); // 延时
}

void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  deal_message(topic, payload, length);
}

void deal_message(char *topic, byte *payload, unsigned int length)
{
  if (strcmp(topic, MQTT_SUB_TOPIC) == 0)
  {
    if (strncmp((char *)payload, "led on", 6) == 0)
    {
      digitalWrite(BLUE_LED_PIN, HIGH);
      Serial.println("BLUE_LED ON");
    }
    else if (strncmp((char *)payload, "led off", 7) == 0)
    {
      digitalWrite(BLUE_LED_PIN, LOW);
      Serial.println("BLUE_LED OFF");
    }
    else
    {
      Serial.println("Invalid message.");
    }
  }
}

void reconnect()
{
  // 连接MQTT服务器
  if (!mqttClient.connected()) // 如果未连接
  {
    mqtt_client_id += String(WiFi.macAddress());    // 每个客户端需要有唯一的ID，不然上线时会把其他相同ID的客户端踢下线
    if (mqttClient.connect(mqtt_client_id.c_str())) // 尝试连接服务器
    {
      mqttClient.publish(MQTT_PUB_TOPIC, "reconnected"); // 连接成功后可以发送消息
      mqttClient.subscribe(MQTT_SUB_TOPIC);              // 连接成功后可以订阅主题
    }
  }
}

void words_display()
{
  // 清除屏幕
  display.clearDisplay();

  // 设置字体颜色,白色可见
  display.setTextColor(WHITE);

  // 设置字体大小
  display.setTextSize(FONT_SIZE);

  // 设置光标位置
  display.setCursor(0, 0);
  // display.print("TaichiMaker");
  display.print("temp:");
  display.print(DHT.temperature);

  display.setCursor(0, 20);
  display.print("humid:");
  display.print(DHT.humidity);

  // display.print("time: ");
  // 打印自开发板重置以来的秒数：
  // display.print(millis() / 1000);
  // display.print(" s");

  display.setCursor(0, 40);
  display.print("CO2:");
  display.print(CO2Data, DEC);
  // display.print("Author: ");
  // display.print("Dapenson");
}

void errorLog(int num)
{
  DebugSerial.print("ERROR");
  DebugSerial.println(num);
  while (1)
  {
    digitalWrite(DEBUG_LED, HIGH);
    delay(300);
    digitalWrite(DEBUG_LED, LOW);
    delay(300);
  }
}

void printGpsBuffer()
{
  if (Save_Data.isParseData)
  {
    Save_Data.isParseData = false;

    DebugSerial.print("Save_Data.UTCTime = ");
    DebugSerial.println(Save_Data.UTCTime);

    if (Save_Data.isUsefull)
    {
      Save_Data.isUsefull = false;
      DebugSerial.print("Save_Data.latitude = ");
      DebugSerial.println(Save_Data.latitude);
      DebugSerial.print("Save_Data.N_S = ");
      DebugSerial.println(Save_Data.N_S);
      DebugSerial.print("Save_Data.longitude = ");
      DebugSerial.println(Save_Data.longitude);
      DebugSerial.print("Save_Data.E_W = ");
      DebugSerial.println(Save_Data.E_W);
    }
    else
    {
      DebugSerial.println("GPS DATA is not usefull!");
    }
  }
}

void parseGpsBuffer()
{
  char *subString;
  char *subStringNext;
  if (Save_Data.isGetData)
  {
    Save_Data.isGetData = false;
    DebugSerial.println("**************");
    DebugSerial.println(Save_Data.GPS_Buffer);

    for (int i = 0; i <= 6; i++)
    {
      if (i == 0)
      {
        if ((subString = strstr(Save_Data.GPS_Buffer, ",")) == NULL)
          errorLog(1); // 解析错误
      }
      else
      {
        subString++;
        if ((subStringNext = strstr(subString, ",")) != NULL)
        {
          char usefullBuffer[2];
          switch (i)
          {
          case 1:
            memcpy(Save_Data.UTCTime, subString, subStringNext - subString);
            break; // 获取UTC时间
          case 2:
            memcpy(usefullBuffer, subString, subStringNext - subString);
            break; // 获取UTC时间
          case 3:
            memcpy(Save_Data.latitude, subString, subStringNext - subString);
            break; // 获取纬度信息
          case 4:
            memcpy(Save_Data.N_S, subString, subStringNext - subString);
            break; // 获取N/S
          case 5:
            memcpy(Save_Data.longitude, subString, subStringNext - subString);
            break; // 获取纬度信息
          case 6:
            memcpy(Save_Data.E_W, subString, subStringNext - subString);
            break; // 获取E/W

          default:
            break;
          }

          subString = subStringNext;
          Save_Data.isParseData = true;
          if (usefullBuffer[0] == 'A')
            Save_Data.isUsefull = true;
          else if (usefullBuffer[0] == 'V')
            Save_Data.isUsefull = false;
        }
        else
        {
          errorLog(2); // 解析错误
        }
      }
    }
  }
}

void clrGpsRxBuffer(void)
{
  memset(gpsRxBuffer, 0, gpsRxBufferLength); // 清空
  ii = 0;
}

void gpsRead()
{
  while (GpsSerial.available())
  {
    gpsRxBuffer[ii++] = GpsSerial.read();
    if (ii == gpsRxBufferLength)
      clrGpsRxBuffer();
  }

  char *GPS_BufferHead;
  char *GPS_BufferTail;
  if ((GPS_BufferHead = strstr(gpsRxBuffer, "$GPRMC,")) != NULL || (GPS_BufferHead = strstr(gpsRxBuffer, "$GNRMC,")) != NULL)
  {
    if (((GPS_BufferTail = strstr(GPS_BufferHead, "\r\n")) != NULL) && (GPS_BufferTail > GPS_BufferHead))
    {
      memcpy(Save_Data.GPS_Buffer, GPS_BufferHead, GPS_BufferTail - GPS_BufferHead);
      Save_Data.isGetData = true;

      clrGpsRxBuffer();
    }
  }
}

void key_scan_task(void *arg)
{
  while (1)
  {
    if (digitalRead(BUZZER_KEY) == LOW)
    {
      while (digitalRead(BUZZER_KEY) == LOW)
        ;
      buzzer_key_state = !buzzer_key_state;
      Serial.println(buzzer_key_state);
    }
    if (digitalRead(IR_RECEIVE_PIN) == HIGH || digitalRead(IR_RECEIVE_PIN_2) == HIGH)
    {
      ir_receive_states.push_back(IR_RECEIVE_STATE);
      IR_RECEIVE_STATE = true;
    }
    else
    {
      ir_receive_states.push_back(IR_RECEIVE_STATE);
      IR_RECEIVE_STATE = false;
    }
    if (digitalRead(VIBRATION_SENSOR) == HIGH)
    {
      vibration_sensor_state = false;
    }
    else
    {
      vibration_sensor_state = true;
    }
    vTaskDelay(10 / portTICK_RATE_MS);
  }
}

void ir_report_task(void *arg)
{
  while (1)
  {
    // if ir_receive_states 全是true，则不发送 如果全是false则不发送 如果有true和false则发送并清空
    if (ir_receive_states.size() >= 15)
    {
      bool all_true = true;
      bool all_false = true;
      for (int i = 0; i < ir_receive_states.size(); i++)
      {
        if (ir_receive_states[i] == false)
        {
          all_true = false;
        }
        else
        {
          all_false = false;
        }
      }
      if (!all_true && !all_false)
      {
        if (mqttClient.connected())
        {
          mqttClient.publish(MQTT_PUB_TOPIC, "hw");
        }
        ir_receive_states.clear();
      }
    }
    if (vibration_sensor_state && mqttClient.connected())
    {
      mqttClient.publish(MQTT_PUB_TOPIC, "Vibration");
    }
    vTaskDelay(100 / portTICK_RATE_MS);
  }
}

void report_task(void *arg)
{
  while (1)
  {
    if (mqttClient.connected())
    {
      char message[1024];
      // message contain temperature and humidity co2 gps
      sprintf(message, "CO2:%d,temperature:%d,humidity:%d,", CO2Data, DHT.temperature, DHT.humidity);
      sprintf(message, "%s%s:%s,%s:%s", message, Save_Data.E_W, Save_Data.latitude, Save_Data.N_S, Save_Data.longitude);
      mqttClient.publish(MQTT_PUB_TOPIC, message);
    }
    vTaskDelay(10000 / portTICK_RATE_MS);
  }
}

void gps_task(void *arg)
{
  while (1)
  {
    gpsRead();        // 获取GPS数据
    parseGpsBuffer(); // 解析GPS数据
    printGpsBuffer(); // 输出解析后的数据
    vTaskDelay(10000 / portTICK_RATE_MS);
  }
}