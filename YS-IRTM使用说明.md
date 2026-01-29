# YS-IRTM 红外模块使用说明

## 模块简介

YS-IRTM 是一款NEC编解码红外收发模块，支持：
- ✅ NEC红外协议编解码
- ✅ UART串口通信（115200波特率）
- ✅ 红外遥控学习功能
- ✅ 即学即发，无需编程
- ✅ 内置AT指令控制

## 硬件连接

### 引脚定义
```
YS-IRTM 模块
┌─────────────┐
│ VCC  TX  RX │
│ GND  LED    │
└─────────────┘
```

### 连接ESP32
| YS-IRTM | ESP32 | 说明 |
|---------|-------|------|
| VCC | 3.3V | 电源正极 |
| GND | GND | 电源地 |
| TX | GPIO16 | 模块发送 → ESP32接收 |
| RX | GPIO17 | 模块接收 ← ESP32发送 |

**注意**：
- 电源电压：3.3V - 5V
- 通信电平：3.3V
- LED指示灯：学习时闪烁

## AT指令集

### 基础指令

| 指令 | 说明 | 响应 |
|------|------|------|
| `AT` | 测试连接 | `OK` |
| `AT+VERSION` | 查询版本 | `V1.0` (示例) |
| `AT+RESET` | 模块复位 | `OK` |

### 发送模式设置

| 指令 | 说明 |
|------|------|
| `AT+SENDMODE=0` | NEC编码 |
| `AT+SENDMODE=1` | NEC编码（默认） |
| `AT+SENDMODE=2` | RC5编码 |
| `AT+SENDMODE?` | 查询当前模式 |

### 红外学习

| 指令 | 说明 | 响应 |
|------|------|------|
| `AT+STARTLEARN` | 开始学习 | `LEARNING...` |
| `AT+ENDLEARN` | 结束学习 | `OK` |
| 按下遥控器 | 学习按键 | `LEARNOK:XXXXXXXXXXXXXXXX` |

**学习流程**：
1. 发送 `AT+STARTLEARN` 进入学习模式
2. 对准模块按下遥控器按键
3. 模块返回 `LEARNOK:XXXXXXXXXXXXXXXX` (16位十六进制代码)
4. 如需继续学习，重复步骤2
5. 学习完成发送 `AT+ENDLEARN` 退出

### 红外发送

| 指令 | 说明 | 响应 |
|------|------|------|
| `AT+SEND=XXXXXXXXXXXXXXXX` | 发送红外代码 | `SENDOK` |

**示例**：
```
AT+SEND=FF10EF10FF10EF10
```

## 代码集成

### 初始化
```cpp
// 定义串口2
#define IR_UART_RX  16
#define IR_UART_TX  17
HardwareSerial IRSerial(2);

// 初始化
void initIR() {
  IRSerial.begin(115200);
  delay(100);

  // 查询模块
  IRSerial.println("AT");
  delay(100);

  Serial.println("YS-IRTM模块已初始化");
}
```

### 红外学习
```cpp
String learnIRCode() {
  IRSerial.println("AT+STARTLEARN");

  unsigned long startTime = millis();
  while (millis() - startTime < 30000) {  // 30秒超时
    if (IRSerial.available()) {
      String response = IRSerial.readStringUntil('\n');
      response.trim();

      if (response.startsWith("LEARNOK:")) {
        String code = response.substring(8);
        IRSerial.println("AT+ENDLEARN");
        return code;
      }
    }
  }

  IRSerial.println("AT+ENDLEARN");
  return "";  // 学习失败
}
```

### 红外发送
```cpp
void sendIRCode(String code) {
  String cmd = "AT+SEND=" + code;
  IRSerial.println(cmd);

  delay(100);
  if (IRSerial.available()) {
    String response = IRSerial.readStringUntil('\n');
    Serial.println("发送结果: " + response);
  }
}
```

## 海尔空调控制流程

### 第一步：学习遥控器
按顺序学习8个按键：
```
1. 电源键    → 索引 0
2. 温度+     → 索引 1
3. 温度-     → 索引 2
4. 模式      → 索引 3
5. 风速      → 索引 4
6. 摆风      → 索引 5
7. 强力      → 索引 6
8. 睡眠      → 索引 7
```

### 第二步：存储代码
```cpp
String learnedCodes[8];  // 存储学习到的代码
String codeNames[8] = {"电源", "温度+", "温度-", "模式", "风速", "摆风", "强力", "睡眠"};
int codeCount = 0;
```

### 第三步：发送命令
```cpp
// 开关电源
sendIRCode(learnedCodes[0]);

// 调高温度
sendIRCode(learnedCodes[1]);

// 调低温度
sendIRCode(learnedCodes[2]);

// 切换模式
sendIRCode(learnedCodes[3]);
```

## 串口命令控制

通过串口监视器输入命令控制：

### 学习相关
```
LEARN         # 进入学习模式
CLEAR         # 清除学习数据
STATUS        # 查看学习状态
SEND_1        # 发送按键1
SEND_2        # 发送按键2
```

### 空调控制
```
ON            # 电源开关
TEMP_26       # 设置温度26°C
AC_MODE_COOL  # 制冷模式
AC_FAN_HIGH   # 高风速
```

### 测试
```
TEST_IR       # 测试YS-IRTM模块
HELP          # 显示帮助
```

## 常见问题

### Q1: 学习失败，没有返回代码
**A:**
- 检查模块供电是否正常（3.3V或5V）
- 遥控器对准模块距离5-10cm
- 确保模块LED指示灯闪烁
- 检查串口通信是否正常（发送AT命令测试）

### Q2: 发送代码后空调无响应
**A:**
- 确认学习代码正确（STATUS命令查看）
- 检查模块与空调的距离（建议3-5米）
- 对准空调接收器（通常在右侧面板）
- 尝试多次发送（某些空调需要重复发送）

### Q3: 串口无响应
**A:**
- 检查波特率是否设置为115200
- 确认TX/RX接线正确（交叉连接：模块TX→ESP32 RX）
- 尝试重启模块或ESP32
- 检查串口线是否松动

### Q4: 学习到的代码不完整
**A:**
- 某些遥控器需要按住按键不放
- 部分按键发送的是连续信号，需要完整接收
- 增加接收超时时间（当前30秒）

## 高级应用

### 1. 自动温控
```cpp
void autoTemperatureControl() {
  float temp = dht.readTemperature();

  if (temp > 28) {
    // 温度过高，开启空调制冷26度
    sendIRCode(learnedCodes[0]);  // 电源
    delay(500);
    // 调整到26度...
  } else if (temp < 24) {
    // 温度过低，关闭空调
    sendIRCode(learnedCodes[0]);  // 电源
  }
}
```

### 2. 定时任务
```cpp
void checkSchedule() {
  int hour = timeClient.getHours();

  // 工作日9点开启空调
  if (hour == 9 && isWeekday()) {
    sendIRCode(learnedCodes[0]);
  }

  // 18点关闭空调
  if (hour == 18) {
    sendIRCode(learnedCodes[0]);
  }
}
```

### 3. Web远程控制
```cpp
WebServer server(80);

server.on("/control", HTTP_POST, []() {
  String cmd = server.arg("cmd");

  if (cmd == "power") {
    sendIRCode(learnedCodes[0]);
    server.send(200, "text/plain", "OK");
  }
  // ... 更多命令
});
```

## 技术规格

| 参数 | 值 |
|------|-----|
| 工作电压 | 3.3V - 5V |
| 通信波特率 | 115200 bps |
| 支持协议 | NEC, RC5等 |
| 学习距离 | 5-10cm |
| 发送距离 | 3-8米 |
| 工作温度 | -20℃ ~ +70℃ |
| 模块尺寸 | 25mm x 35mm |

## 参考资料

- YS-IRTM官方文档
- NEC红外协议规范
- ESP32硬件串口使用
- 海尔空调红外代码分析

## 版本历史

- V1.0 (2026-01-27)
  - 初始版本
  - 支持NEC协议学习与发送
  - 集成海尔空调控制
