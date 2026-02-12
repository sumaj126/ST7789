#!/bin/bash
# 定时空调控制脚本
# 用于启用/禁用ESP32的定时开关空调功能

STATE_DIR="/opt/ac-control"
STATE_FILE="$STATE_DIR/enabled"
MQTT_BROKER="175.178.158.54"
MQTT_PORT="1883"
MQTT_TOPIC="office/ac/schedule/enabled"

# 创建状态目录
mkdir -p "$STATE_DIR"

case "$1" in
    on)
        echo "1" > "$STATE_FILE"
        mosquitto_pub -h "$MQTT_BROKER" -p "$MQTT_PORT" -t "$MQTT_TOPIC" -m '{"enabled":true}'
        echo "✅ 定时空调已启用"
        echo "ESP32将在工作日 8:00-17:30 自动控制空调"
        ;;
    off)
        echo "0" > "$STATE_FILE"
        mosquitto_pub -h "$MQTT_BROKER" -p "$MQTT_PORT" -t "$MQTT_TOPIC" -m '{"enabled":false}'
        echo "⏸️  定时空调已禁用"
        echo "ESP32将不再执行定时空调控制"
        ;;
    status)
        if [ -f "$STATE_FILE" ]; then
            STATE=$(cat "$STATE_FILE")
            if [ "$STATE" = "1" ]; then
                echo "📅 定时空调: 已启用"
                echo "   工作日 8:00-17:30 自动控制"
            else
                echo "📅 定时空调: 已禁用"
            fi
        else
            echo "📅 定时空调: 已禁用"
        fi
        ;;
    *)
        echo "用法: $0 {on|off|status}"
        echo ""
        echo "命令说明:"
        echo "  on     - 启用定时空调控制"
        echo "  off    - 禁用定时空调控制"
        echo "  status - 查看当前状态"
        echo ""
        echo "示例:"
        echo "  $0 on      # 启用定时空调"
        echo "  $0 off     # 禁用定时空调"
        echo "  $0 status  # 查看状态"
        exit 1
        ;;
esac
