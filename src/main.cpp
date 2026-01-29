// ============================================================================
// ESP32 温湿度显示系统 - 美化版
// 功能：显示日期、星期、时间、温度和湿度
// 硬件：ESP32 + ST7789 TFT屏幕 (240x240) + DHT22温湿度传感器
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <DHT.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "esp_task_wdt.h"  // 看门狗
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ========================== 1. 基础配置 ==========================
const char* ssid = "jiajia";
const char* password = "9812061104";

// 办公室数据上传配置
const char* serverUrl = "http://175.178.158.54:7789/update";
const unsigned long uploadInterval = 5000;  // 上传间隔5秒

#define DHTPIN 14
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define TFT_CS    5
#define TFT_RST   15
#define TFT_DC    2
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// 颜色定义（优化配色）
#define ST77XX_BLACK     0x0000
#define ST77XX_WHITE     0xFFFF
#define ST77XX_RED       0xF800
#define ST77XX_GREEN     0x07E0
#define ST77XX_BLUE      0x001F
#define ST77XX_YELLOW    0xFFE0
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

// NTP配置
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 28800, 60000);  // 使用更稳定的NTP服务器

// 看门狗配置
#define WDT_TIMEOUT 8  // 看门狗超时时间(秒)

// 全局变量
const unsigned long tempRefreshInterval = 5000;
const unsigned long clockRefreshInterval = 1000;
unsigned long lastTempRefreshTime = 0;
unsigned long lastClockRefreshTime = 0;
unsigned long lastUploadTime = 0;
unsigned long lastSeconds = 255;  // 用于检测秒数变化
unsigned long lastWiFiCheckTime = 0;
const unsigned long wifiCheckInterval = 30000;  // WiFi检查间隔30秒
unsigned long bootCount = 0;
unsigned long systemUptime = 0;

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
      timeClient.forceUpdate();  // 强制同步时间
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
  // 尝试更新时间，每分钟只尝试一次，避免频繁失败日志
  static unsigned long lastNTPAttempt = 0;
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastNTPAttempt >= 60000) {  // 每分钟尝试一次
    lastNTPAttempt = currentMillis;
    if (!timeClient.update()) {
      static int failCount = 0;
      failCount++;
      if (failCount % 5 == 0) {  // 每5次失败才打印一次
        Serial.printf("⚠️ NTP同步失败 (已失败%d次)，使用缓存时间\n", failCount);
      }
    } else {
      Serial.println("✅ NTP同步成功");
    }
  }
  
  unsigned long epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epochTime);
  if (ptm == NULL) {
    return;
  }

  int year = ptm->tm_year + 1900;
  int month = ptm->tm_mon + 1;
  int day = ptm->tm_mday;
  int weekday = ptm->tm_wday;
  int hours = ptm->tm_hour;
  int minutes = ptm->tm_min;
  int seconds = ptm->tm_sec;

  String weekdayStrs[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
  String weekdayStr = weekdayStrs[weekday % 7];

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
    tft.fillRect(15, 162, 210, 70, ST77XX_BLACK);
    u8g2.begin(tft);
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setForegroundColor(ST77XX_RED);
    u8g2.setBackgroundColor(ST77XX_BLACK);
    String errorStr = "传感器错误";
    int error_x, error_y;
    getCenterPos(u8g2, errorStr.c_str(), 15, 162, 210, 70, error_x, error_y);
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

  // 清除区域
  tft.fillRect(15, 162, 100, 70, ST77XX_BLACK);
  tft.fillRect(135, 162, 90, 70, ST77XX_BLACK);

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

  dht.begin();
  Serial.println("🌡️  DHT22传感器已初始化");
  
  tft.init(240, 240);
  tft.setRotation(3);
  Serial.println("📺 ST7789屏幕已初始化");
  feedWatchdog();
  
  timeClient.begin();
  Serial.println("🕒 NTP客户端已启动");
  
  // 尝试首次NTP同步
  Serial.print("⏰ 正在同步网络时间...");
  for (int i = 0; i < 3; i++) {
    feedWatchdog();
    if (timeClient.forceUpdate()) {
      Serial.println(" ✅ 成功!");
      Serial.println("当前时间: " + timeClient.getFormattedTime());
      break;
    }
    Serial.print(".");
    delay(1000);
  }
  if (!timeClient.isTimeSet()) {
    Serial.println("\n⚠️ NTP同步失败，将使用默认时间并稍后重试");
  }

  drawBeautifulBorder();
  u8g2.begin(tft);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setBackgroundColor(ST77XX_BLACK);
  String msg = "正在同步时间...";
  int msg_x, msg_y;
  getCenterPos(u8g2, msg.c_str(), 0, 100, 240, 40, msg_x, msg_y);
  u8g2.drawUTF8(msg_x, msg_y, msg.c_str());
  
  // 等待时间同步
  for (int i = 0; i < 4; i++) {
    delay(500);
    feedWatchdog();
  }

  initTempHumiUI();
  updateClock();
  
  Serial.println("✅ 系统初始化完成！");
  Serial.println("========================================\n");
}

void loop() {
  // 首要任务：喂狗
  feedWatchdog();
  
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