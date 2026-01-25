// ============================================================================
// ESP32 温湿度显示系统 - 美化版
// 功能：显示日期、星期、时间、温度和湿度
// 硬件：ESP32 + ST7789 TFT屏幕 (240x240) + DHT11温湿度传感器
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

// ========================== 1. 基础配置 ==========================
const char* ssid = "jiajia";
const char* password = "9812061104";

#define DHTPIN 14
#define DHTTYPE DHT11
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
NTPClient timeClient(ntpUDP, "ntp.aliyun.com", 28800, 60000);

// 全局变量
const unsigned long tempRefreshInterval = 5000;
const unsigned long clockRefreshInterval = 1000;
unsigned long lastTempRefreshTime = 0;
unsigned long lastClockRefreshTime = 0;
unsigned long lastSeconds = 255;  // 用于检测秒数变化

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
void drawIcon(int x, int y, const char* type, uint16_t color);
void drawClockIcon(int x, int y, uint16_t color);

// ========================== 3. 核心工具函数 ==========================
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

// 绘制时钟图标
void drawClockIcon(int x, int y, uint16_t color) {
  tft.drawCircle(x, y, 10, color);
  tft.drawCircle(x, y, 8, color);
  // 时针
  tft.drawLine(x, y, x, y - 5, color);
  // 分针
  tft.drawLine(x, y, x + 4, y, color);
}

// 绘制温湿度图标
void drawIcon(int x, int y, const char* type, uint16_t color) {
  if (strcmp(type, "temp") == 0) {
    // 温度计图标
    tft.drawCircle(x, y, 8, color);
    tft.drawCircle(x, y, 5, color);
    tft.fillRect(x - 2, y + 8, 4, 8, color);
    // 刻度
    tft.drawPixel(x - 5, y, color);
    tft.drawPixel(x + 5, y, color);
    tft.drawPixel(x, y - 8, color);
    tft.drawPixel(x, y + 12, color);
  } else if (strcmp(type, "humi") == 0) {
    // 水滴图标
    tft.drawCircle(x, y - 3, 8, color);
    tft.drawCircle(x, y - 3, 6, color);
    // 底部尖端
    tft.drawLine(x, y - 3, x - 5, y + 10, color);
    tft.drawLine(x, y - 3, x + 5, y + 10, color);
    tft.drawLine(x - 5, y + 10, x + 5, y + 10, color);
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
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epochTime);
  if (ptm == NULL) return;

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
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("DHT11 read error!");
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

  Serial.printf("Temp: %.1f C, Humi: %.1f %%\n", temperature, humidity);
}

// ========================== 7. 初始化/主循环 ==========================
void setup() {
  Serial.begin(115200);

  Serial.print("Connecting WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());

  dht.begin();
  tft.init(240, 240);
  tft.setRotation(3);
  timeClient.begin();

  drawBeautifulBorder();
  u8g2.begin(tft);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setBackgroundColor(ST77XX_BLACK);
  String msg = "正在同步时间...";
  int msg_x, msg_y;
  getCenterPos(u8g2, msg.c_str(), 0, 100, 240, 40, msg_x, msg_y);
  u8g2.drawUTF8(msg_x, msg_y, msg.c_str());
  delay(2000);

  initTempHumiUI();
  updateClock();
}

void loop() {
  unsigned long currentTime = millis();

  if (currentTime - lastClockRefreshTime >= clockRefreshInterval) {
    lastClockRefreshTime = currentTime;
    updateClock();
  }

  if (currentTime - lastTempRefreshTime >= tempRefreshInterval) {
    lastTempRefreshTime = currentTime;
    updateTempHumi();
  }
}
