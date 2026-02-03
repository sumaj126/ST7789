// ============================================================================
// ESP32 温湿度显示系统 - 美化版
// 功能：显示日期、星期、时间、温度和湿度
// 硬件：ESP32 + ST7789 TFT屏幕 (240x240) + DHT22温湿度传感器
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>  // ESP32 内置时间函数
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <DHT.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "esp_task_wdt.h"  // 看门狗
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>  // HTTP服务器，用于接收空调控制指令
#include <PubSubClient.h>  // MQTT客户端
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ========================== 1. 基础配置 ==========================
const char* ssid = "jiajia";
const char* password = "9812061104";

// 办公室数据上传配置
const char* serverUrl = "http://175.178.158.54:7789/update";
const unsigned long uploadInterval = 5000;  // 上传间隔5秒

#define DHTPIN 14
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// 红外模块配置（串口型）
#define IR_SERIAL Serial2  // 使用串口2连接红外模块
#define IR_RX_PIN 16      // 红外模块 RX 引脚（连接到 ESP32 的某个引脚，实际上是红外模块的 TX）
#define IR_TX_PIN 17      // 红外模块 TX 引脚（连接到 ESP32 的某个引脚，实际上是红外模块的 RX）
#define IR_BAUDRATE 115200

#define TFT_CS    5
#define TFT_RST   15
#define TFT_DC    2
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// HTTP服务器配置
WebServer webServer(80);

// MQTT配置
const char* mqttServer = "175.178.158.54";
const int mqttPort = 1883;
const char* mqttTopic = "office/ac/control";
WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);

// 颜色定义（部分由库提供）
#define ST77XX_BLACK     0x0000
#define ST77XX_WHITE     0xFFFF
#define ST77XX_RED       0xF800
#define ST77XX_GREEN     0x07E0
#define ST77XX_BLUE      0x001F
#define ST77XX_YELLOW    0xFFE0
// ST77XX_ORANGE 已在库中定义
#define ST77XX_CYAN      0x07FF
#define ST77XX_MAGENTA   0xF81F
#define ST77XX_GRAY_LIGHT 0x5AEB
#define ST77XX_GRAY_DARK  0x18E3
#define ST77XX_BG_DARK    0x0808

// 渐变色（深蓝到深灰背景）
#define BG_TOP_COLOR     0x0808
#define BG_BOTTOM_COLOR  0x0C0C
#define DATE_BG_COLOR    0x0010
#define TIME_BG_COLOR    0x0015

// NTP配置 - 使用 ESP32 内置 configTime
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 8 * 3600;  // GMT+8
const int daylightOffset_sec = 0;

// 看门狗配置
#define WDT_TIMEOUT 8  // 看门狗超时时间(秒)

// 全局变量
const unsigned long tempRefreshInterval = 5000;
const unsigned long clockRefreshInterval = 1000;
const unsigned long ntpSyncInterval = 86400000;  // NTP同步间隔：24小时（一天一次）
const unsigned long acCheckInterval = 60000;  // 空调检查间隔：60秒（1分钟）
unsigned long lastTempRefreshTime = 0;
unsigned long lastClockRefreshTime = 0;
unsigned long lastNTPSyncTime = 0;
unsigned long lastUploadTime = 0;
unsigned long lastACCheckTime = 0;
unsigned long lastSeconds = 255;  // 用于检测秒数变化
unsigned long lastWiFiCheckTime = 0;
const unsigned long wifiCheckInterval = 30000;  // WiFi检查间隔30秒
unsigned long bootCount = 0;
unsigned long systemUptime = 0;

// 空调控制状态
bool acIsOn = false;  // 空调是否开启
bool lastACCommandSent = false;  // 上次是否发送过空调命令

// ========================== 2. 函数前置声明 ==========================
void drawBeautifulBorder();
void updateClock();
void updateTempHumi();
void initTempHumiUI();
void getCenterPos(U8G2_FOR_ADAFRUIT_GFX &u8g2_obj, const char* str,
                 int area_x, int area_y, int area_w, int area_h,
                 int &out_x, int &out_y);
void drawRoundedRect(int x, int y, int w, int h, int r, uint16_t color);
void drawGradientBackground();
void checkAndReconnectWiFi();
void feedWatchdog();
void uploadData(float temperature, float humidity);
void initIRModule();
void sendIRCommand(const char* command);
void handleACOn();
void handleACOff();
void handleNotFound();
void checkACControl(int weekday, int hour, int minute, float temperature);
void mqttCallback(char* topic, byte* payload, unsigned int length);
void mqttTask(void *pvParameters);

// ========================== 3. 核心工具函数 ==========================
// 喂狗函数
void feedWatchdog() {
  esp_task_wdt_reset();
}

// WiFi检查和重连
void checkAndReconnectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi断线，正在重连...");
    
    // 清除屏幕顶部显示错误信息
    tft.fillRect(10, 10, 220, 20, ST77XX_BLACK);
    u8g2.begin(tft);
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2.setForegroundColor(ST77XX_RED);
    u8g2.setBackgroundColor(ST77XX_BLACK);
    u8g2.drawUTF8(15, 25, "WiFi断线重连中...");
    
    WiFi.disconnect();
    delay(1000);
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      feedWatchdog();  // 重连过程中喂狗
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ WiFi重连成功! IP: " + WiFi.localIP().toString());
      tft.fillRect(10, 10, 220, 20, ST77XX_BLACK);  // 清除错误信息
      // 不需要重新配置时间，ESP32会自动维护时间
    } else {
      Serial.println("\n❌ WiFi重连失败，将在30秒后重试");
    }
  }
}

String formatNumber(int num) {
  return num < 10 ? "0" + String(num) : String(num);
}

void getCenterPos(U8G2_FOR_ADAFRUIT_GFX &u8g2_obj, const char* str,
                 int area_x, int area_y, int area_w, int area_h,
                 int &out_x, int &out_y) {
  int str_w = u8g2_obj.getUTF8Width(str);
  out_x = area_x + (area_w - str_w) / 2;
  int font_ascent = u8g2_obj.getFontAscent();
  int font_descent = u8g2_obj.getFontDescent();
  int font_h = font_ascent - font_descent;
  out_y = area_y + (area_h - font_h) / 2 + font_ascent;
}

// 绘制圆角矩形
void drawRoundedRect(int x, int y, int w, int h, int r, uint16_t color) {
  tft.drawRoundRect(x, y, w, h, r, color);
}

// 绘制渐变背景（纯黑背景）
void drawGradientBackground() {
  tft.fillScreen(ST77XX_BLACK);
}

// ========================== 数据上传 ==========================
void uploadData(float temperature, float humidity) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi未连接，跳过上传");
    return;
  }

  HTTPClient http;
  http.setTimeout(10000);  // 10秒超时

  // 构建JSON数据
  StaticJsonDocument<128> doc;
  doc["temperature"] = round(temperature * 10) / 10.0;  // 保留1位小数
  doc["humidity"] = round(humidity * 10) / 10.0;  // 保留1位小数

  String jsonData;
  serializeJson(doc, jsonData);

  Serial.println("📤 正在上传数据...");
  Serial.println("数据: " + jsonData);

  // 发送HTTP POST请求
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(jsonData);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf("✅ 上传成功! 状态码: %d, 响应: %s\n", httpResponseCode, response.c_str());
  } else {
    Serial.printf("❌ 上传失败! 错误码: %d, %s\n", httpResponseCode, http.errorToString(httpResponseCode).c_str());
  }

  http.end();
}

// ========================== MQTT控制 ==========================
// MQTT回调函数：收到消息
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("📨 收到MQTT消息: %s\n", topic);

  // 解析JSON消息
  StaticJsonDocument<64> doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    Serial.printf("❌ JSON解析失败: %s\n", error.c_str());
    return;
  }

  const char* action = doc["action"];

  if (strcmp(action, "on") == 0) {
    Serial.println("❄️ MQTT指令：开启空调");
    sendIRCommand("fs00");
    acIsOn = true;
  } else if (strcmp(action, "off") == 0) {
    Serial.println("🔴 MQTT指令：关闭空调");
    sendIRCommand("fs20");
    acIsOn = false;
  }
}

// MQTT 任务函数 - 在独立任务中运行，不阻塞主循环
void mqttTask(void *pvParameters) {
  Serial.println("📡 MQTT任务启动...");
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setSocketTimeout(5000);  // 5秒超时

  String clientId = "ESP32-Office-" + String(random(0xffff), HEX);
  Serial.printf("   服务器: %s:%d\n", mqttServer, mqttPort);
  Serial.printf("   客户端ID: %s\n", clientId.c_str());
  Serial.printf("   主题: %s\n", mqttTopic);

  bool lastWiFiStatus = false;

  while (1) {
    bool currentWiFiStatus = (WiFi.status() == WL_CONNECTED);

    // 只在WiFi状态变化时打印日志
    if (!currentWiFiStatus && lastWiFiStatus) {
      Serial.println("⚠️ WiFi断开，MQTT任务等待...");
    }

    if (currentWiFiStatus) {
      if (!mqttClient.connected()) {
        Serial.print("🔄 连接MQTT...");

        unsigned long connectStart = millis();
        if (mqttClient.connect(clientId.c_str())) {
          Serial.println(" ✅ 已连接");
          mqttClient.subscribe(mqttTopic);
          Serial.printf("   订阅主题: %s\n", mqttTopic);
        } else {
          int state = mqttClient.state();
          Serial.print(" ❌ 失败 (状态: ");
          Serial.print(state);
          Serial.printf(") [耗时: %lums]\n", millis() - connectStart);

          // PubSubClient 状态码说明
          switch(state) {
            case -4: Serial.println("   原因: MQTT_CONNECTION_TIMEOUT"); break;
            case -3: Serial.println("   原因: MQTT_CONNECTION_LOST"); break;
            case -2: Serial.println("   原因: MQTT_CONNECT_FAILED (服务器拒绝连接)"); break;
            case -1: Serial.println("   原因: MQTT_DISCONNECTED"); break;
            case 0: Serial.println("   原因: MQTT_CONNECTED"); break;
            case 1: Serial.println("   原因: 连接协议错误"); break;
            case 2: Serial.println("   原因: 客户端ID错误"); break;
            case 3: Serial.println("   原因: 服务不可用"); break;
            case 4: Serial.println("   原因: 用户名密码错误"); break;
            case 5: Serial.println("   原因: 未授权"); break;
            default: Serial.println("   原因: 未知错误"); break;
          }
        }
      } else {
        mqttClient.loop();  // 处理MQTT消息
      }
    }

    lastWiFiStatus = currentWiFiStatus;

    // 每5秒检查一次
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

// ========================== 红外模块控制 ==========================
// 初始化红外模块
void initIRModule() {
  Serial.println("📡 初始化红外模块...");
  IR_SERIAL.begin(IR_BAUDRATE, SERIAL_8N1, IR_RX_PIN, IR_TX_PIN);
  delay(1000);
  Serial.println("✅ 红外模块已初始化");
  Serial.printf("   波特率: %d\n", IR_BAUDRATE);
  Serial.printf("   引脚: RX=%d, TX=%d\n", IR_RX_PIN, IR_TX_PIN);
}

// 发送红外命令
void sendIRCommand(const char* command) {
  Serial.printf("📤 发送红外命令: %s\n", command);
  IR_SERIAL.println(command);
  delay(500);
  
  // 读取红外模块响应
  if (IR_SERIAL.available()) {
    String response = IR_SERIAL.readString();
    Serial.printf("   模块响应: %s\n", response.c_str());
  } else {
    Serial.println("   无响应");
  }
}

// HTTP 服务器处理函数：空调开机
void handleACOn() {
  Serial.println("🔴 收到空调开机请求");
  sendIRCommand("fs00");
  
  String response = "{\"status\":\"success\",\"action\":\"ac_on\",\"message\":\"空调开机指令已发送\"}";
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.send(200, "application/json", response);
  
  Serial.println("✅ 空调开机响应已发送");
}

// HTTP 服务器处理函数：空调关机
void handleACOff() {
  Serial.println("🔴 收到空调关机请求");
  sendIRCommand("fs20");
  
  String response = "{\"status\":\"success\",\"action\":\"ac_off\",\"message\":\"空调关机指令已发送\"}";
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.send(200, "application/json", response);
  
  Serial.println("✅ 空调关机响应已发送");
}

// HTTP 服务器处理函数：404
void handleNotFound() {
  String response = "{\"status\":\"error\",\"message\":\"API not found\"}";
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.send(404, "application/json", response);
}

// 空调自动控制逻辑
void checkACControl(int weekday, int hour, int minute, float temperature) {
  // weekday: 0=周日, 1=周一, ..., 6=周六
  
  // 判断是否在工作日（周一到周五）
  bool isWorkday = (weekday >= 1 && weekday <= 5);
  
  if (!isWorkday) {
    // 周末不做自动控制
    return;
  }

  // 早上 8:00 检查：温度低于17度，打开空调
  if (hour == 8 && minute == 0) {
    if (temperature < 17.0) {
      Serial.println("🕗 早上8点，温度低于17°C，准备开启空调...");
      sendIRCommand("fs00");
      acIsOn = true;
      lastACCommandSent = true;
    } else {
      Serial.printf("🕗 早上8点，温度%.1f°C，不需要开启空调\n", temperature);
    }
  }
  
  // 下午 17:30：无论空调是否开启，都发送关机命令
  if (hour == 17 && minute == 30) {
    Serial.println("🕕 下午5:30，准备关闭空调...");
    sendIRCommand("fs20");
    acIsOn = false;
    lastACCommandSent = true;
  }
}

// ========================== 4. 界面绘制（美化版） ==========================
void drawBeautifulBorder() {
  // 外边框（圆角）
  drawRoundedRect(2, 2, 236, 236, 8, ST77XX_GRAY_LIGHT);

  // 内装饰线
  tft.drawRoundRect(6, 6, 228, 228, 6, ST77XX_GRAY_DARK);

  // 分隔线
  tft.drawFastHLine(8, 80, 224, ST77XX_GRAY_DARK);
  tft.drawFastHLine(8, 160, 224, ST77XX_GRAY_DARK);
  tft.drawFastVLine(120, 162, 76, ST77XX_GRAY_DARK);
}

void initTempHumiUI() {
  drawGradientBackground();
  drawBeautifulBorder();
}

// ========================== 5. 时钟更新（消除闪烁版） ==========================
void updateClock() {
  // 使用 time() 获取时间戳，然后用 localtime() 转换
  time_t now = time(nullptr);
  if (now < 1000000) {  // 时间未同步（epoch太小）
    return;
  }

  struct tm *timeinfo = localtime(&now);
  if (timeinfo == nullptr) {
    return;
  }

  int year = timeinfo->tm_year + 1900;
  int month = timeinfo->tm_mon + 1;
  int day = timeinfo->tm_mday;
  int weekday = timeinfo->tm_wday;
  int hours = timeinfo->tm_hour;
  int minutes = timeinfo->tm_min;
  int seconds = timeinfo->tm_sec;

  String weekdayStrs[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
  String weekdayStr = weekdayStrs[weekday % 7];

  // 检查空调控制（每分钟检查一次）
  if (seconds == 0 && !lastACCommandSent) {
    float temp = dht.readTemperature();
    if (!isnan(temp)) {
      checkACControl(weekday, hours, minutes, temp);
    }
  }

  // 重置命令标志（每分钟重置一次）
  if (seconds == 0) {
    lastACCommandSent = false;
  }

  u8g2.begin(tft);

  // 日期和星期显示（分两行显示）
  static String lastDateNum = "";
  static String lastWeekday = "";
  String dateNum = String(year) + "-" + formatNumber(month) + "-" + formatNumber(day);

  if (dateNum != lastDateNum || weekdayStr != lastWeekday) {
    tft.fillRect(10, 10, 220, 70, ST77XX_BLACK); // 清除日期区
    u8g2.setFont(u8g2_font_wqy16_t_gb2312b);   // 使用加粗16号中文字体
    u8g2.setForegroundColor(ST77XX_WHITE);
    u8g2.setBackgroundColor(ST77XX_BLACK);

    // 第一行：日期
    int date_x, date_y;
    getCenterPos(u8g2, dateNum.c_str(), 10, 10, 220, 35, date_x, date_y);
    u8g2.drawUTF8(date_x, date_y, dateNum.c_str());

    // 第二行：星期
    int weekday_x, weekday_y;
    getCenterPos(u8g2, weekdayStr.c_str(), 10, 45, 220, 35, weekday_x, weekday_y);
    u8g2.drawUTF8(weekday_x, weekday_y, weekdayStr.c_str());

    lastDateNum = dateNum;
    lastWeekday = weekdayStr;
  }

  // 时间显示（优化：只重绘秒数区域）
  if (seconds != lastSeconds) {
    // 格式化时间
    String timeStr = formatNumber(hours) + ":" + formatNumber(minutes) + ":" + formatNumber(seconds);

    u8g2.setFont(u8g2_font_logisoso26_tn);

    // 清除并重绘整个时间区域（使用背景色）
    tft.fillRect(10, 92, 220, 60, ST77XX_BLACK);

    u8g2.setForegroundColor(ST77XX_WHITE);
    int time_x, time_y;
    getCenterPos(u8g2, timeStr.c_str(), 10, 92, 220, 60, time_x, time_y);
    u8g2.drawUTF8(time_x, time_y, timeStr.c_str());

    lastSeconds = seconds;
  }
}

// ========================== 6. 温湿度更新（美化版） ==========================
void updateTempHumi() {
  // 喂狗，防止传感器读取超时
  feedWatchdog();

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("❌ DHT22读取错误!");
    // 清除整个温湿度区域（包括竖线位置）
    tft.fillRect(10, 162, 220, 70, ST77XX_BLACK);
    u8g2.begin(tft);
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setForegroundColor(ST77XX_RED);
    u8g2.setBackgroundColor(ST77XX_BLACK);
    String errorStr = "传感器错误";
    int error_x, error_y;
    getCenterPos(u8g2, errorStr.c_str(), 10, 162, 220, 70, error_x, error_y);
    u8g2.drawUTF8(error_x, error_y, errorStr.c_str());
    return;
  }

  // 动态颜色
  uint16_t tempColor = ST77XX_YELLOW;
  if (temperature < 20) tempColor = ST77XX_BLUE;
  else if (temperature > 30) tempColor = ST77XX_RED;

  uint16_t humiColor = ST77XX_GREEN;
  if (humidity < 30) humiColor = ST77XX_ORANGE;
  else if (humidity > 80) humiColor = ST77XX_CYAN;

  // 清除区域（包括竖线位置）
  tft.fillRect(10, 162, 220, 70, ST77XX_BLACK);

  u8g2.begin(tft);
  u8g2.setBackgroundColor(ST77XX_BLACK);

  // -------------------------- 温度区 --------------------------
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(ST77XX_WHITE);
  int temp_text_x, temp_text_y;
  getCenterPos(u8g2, "温度", 15, 165, 105, 25, temp_text_x, temp_text_y);
  u8g2.drawUTF8(temp_text_x, temp_text_y, "温度");

  u8g2.setFont(u8g2_font_helvR18_tf);
  u8g2.setForegroundColor(tempColor);
  String tempStr = String(temperature, 1) + "°C";
  int temp_val_x, temp_val_y;
  getCenterPos(u8g2, tempStr.c_str(), 15, 190, 105, 35, temp_val_x, temp_val_y);
  u8g2.drawUTF8(temp_val_x, temp_val_y, tempStr.c_str());

  // -------------------------- 湿度区 --------------------------
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(ST77XX_WHITE);
  int humi_text_x, humi_text_y;
  getCenterPos(u8g2, "湿度", 135, 165, 100, 25, humi_text_x, humi_text_y);
  u8g2.drawUTF8(humi_text_x, humi_text_y, "湿度");

  u8g2.setFont(u8g2_font_helvR18_tf);
  u8g2.setForegroundColor(humiColor);
  String humiStr = String(humidity, 1) + "%";
  int humi_val_x, humi_val_y;
  getCenterPos(u8g2, humiStr.c_str(), 135, 190, 100, 35, humi_val_x, humi_val_y);
  u8g2.drawUTF8(humi_val_x, humi_val_y, humiStr.c_str());

  // 重新绘制中间分隔竖线
  tft.drawFastVLine(120, 162, 70, ST77XX_GRAY_DARK);

  Serial.printf("Temp: %.1f C, Humi: %.1f %%\n", temperature, humidity);
}

// ========================== 7. 初始化/主循环 ==========================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // 检查重启原因
  esp_reset_reason_t reset_reason = esp_reset_reason();
  bootCount++;
  Serial.println("\n========================================");
  Serial.printf("🚀 系统启动 #%lu\n", bootCount);
  Serial.print("重启原因: ");
  switch(reset_reason) {
    case ESP_RST_POWERON:   Serial.println("上电复位"); break;
    case ESP_RST_SW:        Serial.println("软件复位"); break;
    case ESP_RST_PANIC:     Serial.println("异常崩溃"); break;
    case ESP_RST_INT_WDT:   Serial.println("看门狗超时"); break;
    case ESP_RST_TASK_WDT:  Serial.println("任务看门狗"); break;
    case ESP_RST_WDT:       Serial.println("其他看门狗"); break;
    case ESP_RST_DEEPSLEEP: Serial.println("深度睡眠唤醒"); break;
    case ESP_RST_BROWNOUT:  Serial.println("欠压复位"); break;
    default:                Serial.println("未知原因"); break;
  }
  Serial.println("========================================\n");

  // 初始化看门狗 (8秒超时)
  Serial.println("⏱️  启用看门狗 (超时时间: 8秒)");
  esp_task_wdt_init(WDT_TIMEOUT, true);  // 启用panic重启
  esp_task_wdt_add(NULL);                // 添加当前任务到看门狗
  feedWatchdog();

  Serial.print("📡 连接WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  int wifi_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_attempts < 20) {
    delay(500);
    Serial.print(".");
    feedWatchdog();  // WiFi连接过程中喂狗
    wifi_attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi连接成功! IP: " + WiFi.localIP().toString());
    Serial.println("📶 信号强度: " + String(WiFi.RSSI()) + " dBm");
  } else {
    Serial.println("\n⚠️ WiFi连接失败，将继续尝试...");
  }
  feedWatchdog();

  // 初始化红外模块
  initIRModule();

  dht.begin();
  Serial.println("🌡️  DHT22传感器已初始化");
  
  tft.init(240, 240);
  tft.setRotation(3);
  Serial.println("📺 ST7789屏幕已初始化");
  feedWatchdog();

  // 配置 NTP 时间
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("🕒 NTP时间同步已配置");

  // 尝试首次NTP同步
  Serial.print("⏰ 正在同步网络时间...");
  struct tm timeinfo;
  for (int i = 0; i < 10; i++) {  // 增加尝试次数
    feedWatchdog();
    if (getLocalTime(&timeinfo)) {
      Serial.println(" ✅ 成功!");
      Serial.printf("当前时间: %04d-%02d-%02d %02d:%02d:%02d\n",
                   timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                   timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      lastNTPSyncTime = millis();  // 标记同步成功
      break;
    }
    Serial.print(".");
    delay(500);
  }
  if (!getLocalTime(&timeinfo)) {
    Serial.println("\n⚠️ NTP同步失败，将使用默认时间并稍后重试");
  }

  initTempHumiUI();
  updateClock();
  
  // 启动 HTTP 服务器（空调控制 API）
  Serial.println("🌐 启动 HTTP 服务器...");
  webServer.on("/ac/on", HTTP_GET, handleACOn);
  webServer.on("/ac/off", HTTP_GET, handleACOff);
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  Serial.println("✅ HTTP 服务器已启动");
  Serial.printf("   API 端点:\n");
  Serial.printf("     - http://%s/ac/on  (空调开机)\n", WiFi.localIP().toString().c_str());
  Serial.printf("     - http://%s/ac/off (空调关机)\n", WiFi.localIP().toString().c_str());
  
  Serial.println("✅ 系统初始化完成！");
  Serial.println("========================================\n");

  // 创建 MQTT 任务，在独立任务中运行
  xTaskCreate(
    mqttTask,           // 任务函数
    "MQTTTask",         // 任务名称
    4096,              // 堆栈大小
    NULL,              // 参数
    1,                 // 优先级
    NULL               // 任务句柄
  );
  Serial.println("📡 MQTT任务已创建");
}

void loop() {
  // 首要任务：喂狗
  feedWatchdog();

  // 处理 HTTP 服务器请求
  webServer.handleClient();
  
  // 处理串口命令（用于测试）
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      Serial.printf("🔤 收到串口命令: %s\n", command.c_str());
      IR_SERIAL.println(command);
      delay(500);
      if (IR_SERIAL.available()) {
        String response = IR_SERIAL.readString();
        Serial.printf("📥 红外模块响应: %s\n", response.c_str());
      }
    }
  }
  
  unsigned long currentTime = millis();
  systemUptime = currentTime / 1000;  // 运行时间(秒)

  // 定期检查WiFi连接状态
  if (currentTime - lastWiFiCheckTime >= wifiCheckInterval) {
    lastWiFiCheckTime = currentTime;
    checkAndReconnectWiFi();
    
    // 每小时输出一次运行状态
    if (systemUptime % 3600 == 0) {
      Serial.printf("📊 系统运行时间: %lu小时 %lu分钟\n", 
                    systemUptime / 3600, (systemUptime % 3600) / 60);
      Serial.printf("   空闲内存: %d bytes\n", ESP.getFreeHeap());
    }
  }

  // NTP时间同步（每天同步一次）
  // 注意：ESP32在首次configTime后会自动维护系统时间
  // 定期重新调用configTime可以校正时间漂移
  if (currentTime - lastNTPSyncTime >= ntpSyncInterval) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    lastNTPSyncTime = currentTime;
    Serial.println("🕒 NTP时间已重新同步");
  }

  // 更新时钟显示
  if (currentTime - lastClockRefreshTime >= clockRefreshInterval) {
    lastClockRefreshTime = currentTime;
    updateClock();
  }

  // 更新温湿度显示
  if (currentTime - lastTempRefreshTime >= tempRefreshInterval) {
    lastTempRefreshTime = currentTime;
    updateTempHumi();

    // 定时上传数据到服务器
    if (currentTime - lastUploadTime >= uploadInterval) {
      lastUploadTime = currentTime;
      feedWatchdog();
      uploadData(dht.readTemperature(), dht.readHumidity());
    }
  }

  // 短暂延时，避免CPU满载
  delay(10);
}