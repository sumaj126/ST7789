const http = require('http');

let latestData = { temperature: 0, humidity: 0, timestamp: 0 };
let scheduleStatus = { enabled: true, lastUpdate: 0 };
let lastScheduleCommandTime = 0; // 记录最后发送定时空调命令的时间

// MQTT客户端配置
const mqtt = require('mqtt');
const mqttClient = mqtt.connect('mqtt://175.178.158.54:1883');

mqttClient.on('connect', () => {
  console.log('MQTT 已连接');
  mqttClient.subscribe('office/ac/schedule/status');
});

mqttClient.on('message', (topic, message) => {
  if (topic === 'office/ac/schedule/status') {
    const data = JSON.parse(message.toString());
    const now = Date.now();

    // 如果是在用户操作后的30秒内，优先接受ESP32的确认
    if (now - lastScheduleCommandTime < 30000) {
      scheduleStatus.enabled = data.enabled;
      scheduleStatus.lastUpdate = now;
      console.log('收到定时空调状态确认:', data);
    } else {
      // ESP32的定期上报（每60秒）
      // 只更新时间戳，不覆盖用户手动设置的enabled状态
      scheduleStatus.lastUpdate = now;
      console.log('收到ESP32定期心跳上报，保持当前状态:', scheduleStatus.enabled);
    }
  }
});

mqttClient.on('error', (err) => {
  console.log('MQTT 错误:', err);
});

const server = http.createServer((req, res) => {
  res.setHeader('Access-Control-Allow-Origin', '*');

  if (req.method === 'POST' && req.url === '/update') {
    // ESP32上传温湿度数据
    let body = '';
    req.on('data', chunk => { body += chunk; });
    req.on('end', () => {
      try {
        const data = JSON.parse(body);
        latestData = {
          temperature: data.temperature,
          humidity: data.humidity,
          timestamp: Date.now()
        };
        console.log('收到数据:', latestData);
        res.setHeader('Content-Type', 'application/json');
        res.writeHead(200);
        res.end(JSON.stringify({ status: 'success' }));
      } catch (e) {
        res.setHeader('Content-Type', 'application/json');
        res.writeHead(400);
        res.end(JSON.stringify({ status: 'error', message: e.message }));
      }
    });
  } else if (req.method === 'POST' && req.url === '/ac') {
    // 手动空调控制
    let body = '';
    req.on('data', chunk => { body += chunk; });
    req.on('end', () => {
      try {
        const data = JSON.parse(body);
        const action = data.action;

        // 发送MQTT消息
        mqttClient.publish('office/ac/control', JSON.stringify({ action: action }));

        console.log(`发送空调控制: ${action}`);
        res.setHeader('Content-Type', 'application/json');
        res.writeHead(200);
        res.end(JSON.stringify({ status: 'success', message: `空调${action === 'on' ? '开启' : '关闭'}指令已发送` }));
      } catch (e) {
        res.setHeader('Content-Type', 'application/json');
        res.writeHead(400);
        res.end(JSON.stringify({ status: 'error', message: e.message }));
      }
    });
  } else if (req.method === 'POST' && req.url === '/schedule') {
    // 定时空调开关控制
    let body = '';
    req.on('data', chunk => { body += chunk; });
    req.on('end', () => {
      try {
        const data = JSON.parse(body);
        const enabled = data.enabled;

        // 立即更新服务器端状态
        scheduleStatus.enabled = enabled;
        scheduleStatus.lastUpdate = Date.now();

        // 记录发送命令的时间
        lastScheduleCommandTime = Date.now();

        // 发送MQTT消息给ESP32
        mqttClient.publish('office/ac/schedule/enabled', JSON.stringify({ enabled: enabled }));

        console.log(`定时空调控制: ${enabled ? '启用' : '禁用'}`);
        res.setHeader('Content-Type', 'application/json');
        res.writeHead(200);
        res.end(JSON.stringify({ status: 'success', message: `定时空调已${enabled ? '启用' : '禁用'}` }));
      } catch (e) {
        res.setHeader('Content-Type', 'application/json');
        res.writeHead(400);
        res.end(JSON.stringify({ status: 'error', message: e.message }));
      }
    });
  } else if (req.method === 'GET' && req.url === '/api/data') {
    // 获取温湿度数据API
    res.setHeader('Content-Type', 'application/json');
    res.writeHead(200);
    res.end(JSON.stringify(latestData));
  } else if (req.method === 'GET' && req.url === '/api/status') {
    // 获取定时空调状态API
    res.setHeader('Content-Type', 'application/json');
    res.writeHead(200);
    res.end(JSON.stringify(scheduleStatus));
  } else if (req.method === 'GET') {
    // 返回HTML页面
    const age = Math.floor((Date.now() - latestData.timestamp) / 1000);
    const html = `<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>办公室监控</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { font-family: 'Microsoft YaHei', Arial, sans-serif; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; display: flex; align-items: center; justify-content: center; padding: 20px; }
    .container { background: white; border-radius: 20px; padding: 40px; box-shadow: 0 10px 40px rgba(0,0,0,0.1); max-width: 500px; width: 100%; }
    .header { text-align: center; margin-bottom: 30px; padding-bottom: 20px; border-bottom: 2px solid #f0f0f0; }
    .title { font-size: 28px; color: #333; margin-bottom: 10px; font-weight: bold; }
    .subtitle { font-size: 14px; color: #999; }
    .time-display { text-align: center; font-size: 48px; font-weight: bold; color: #667eea; margin-bottom: 30px; font-family: 'Courier New', monospace; }
    .data-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-bottom: 20px; }
    .data-card { border-radius: 15px; padding: 25px; text-align: center; color: #333; }
    .data-label { font-size: 16px; opacity: 0.9; margin-bottom: 10px; }
    .data-value { font-size: 42px; font-weight: bold; }
    .status-bar { background: #f8f9fa; border-radius: 10px; padding: 15px; text-align: center; font-size: 14px; color: #666; }
    .status-online { color: #28a745; font-weight: bold; }
    .status-offline { color: #dc3545; font-weight: bold; }
    .icon { font-size: 32px; margin-bottom: 10px; }
    .control-section { margin-top: 30px; padding-top: 20px; border-top: 2px solid #f0f0f0; }
    .section-title { font-size: 18px; font-weight: bold; color: #333; margin-bottom: 15px; text-align: center; }
    .ac-controls { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 20px; }
    .ac-btn { border: none; border-radius: 12px; padding: 20px; font-size: 18px; font-weight: bold; cursor: pointer; transition: all 0.3s; display: flex; align-items: center; justify-content: center; gap: 10px; }
    .ac-btn:hover { transform: translateY(-2px); box-shadow: 0 5px 15px rgba(0,0,0,0.2); }
    .ac-btn:active { transform: translateY(0); }
    .ac-btn-on { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; }
    .ac-btn-off { background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%); color: white; }
    .ac-btn:disabled { opacity: 0.5; cursor: not-allowed; transform: none; }
    .ac-status { text-align: center; margin-top: 15px; padding: 10px; border-radius: 8px; font-size: 14px; display: none; }
    .ac-status.show { display: block; }
    .ac-status.success { background: #d4edda; color: #155724; }
    .ac-status.error { background: #f8d7da; color: #721c24; }
    .schedule-toggle { display: flex; align-items: center; justify-content: center; gap: 15px; padding: 15px; background: #f8f9fa; border-radius: 10px; }
    .switch-label { font-size: 16px; color: #333; }
    .toggle-switch { position: relative; width: 60px; height: 34px; }
    .toggle-switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 34px; }
    .slider:before { position: absolute; content: ""; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
    input:checked + .slider { background-color: #28a745; }
    input:checked + .slider:before { transform: translateX(26px); }
    .schedule-info { text-align: center; font-size: 12px; color: #999; margin-top: 10px; }
    .schedule-status { text-align: center; margin-top: 10px; padding: 8px; border-radius: 8px; font-size: 13px; }
    .schedule-status.confirmed { background: #d4edda; color: #155724; }
    .schedule-status.waiting { background: #fff3cd; color: #856404; }
    @media (max-width: 480px) { .container { padding: 20px; } .title { font-size: 24px; } .time-display { font-size: 36px; } .data-card { padding: 15px; text-align: center; } .data-value { font-size: 32px; text-align: center; } .ac-btn { font-size: 16px; padding: 15px; } }
  </style>
  <script>
    let scheduleEnabled = ${scheduleStatus.enabled};
    let scheduleConfirmed = true; // 是否已确认

    function updateTime() {
      const now = new Date();
      const hours = String(now.getHours()).padStart(2, '0');
      const minutes = String(now.getMinutes()).padStart(2, '0');
      const seconds = String(now.getSeconds()).padStart(2, '0');
      document.getElementById('time').textContent = hours + ':' + minutes + ':' + seconds;
    }

    async function updateSensorData() {
      try {
        const response = await fetch('/api/data');
        const data = await response.json();
        const tempValue = document.getElementById('temp-value');
        const humValue = document.getElementById('hum-value');

        tempValue.textContent = data.temperature.toFixed(1) + '°C';
        humValue.textContent = data.humidity.toFixed(1) + '%';

        // 更新温度颜色
        const temp = data.temperature;
        let tempColor = temp < 20 ? '#3498db' : temp >= 20 && temp < 30 ? 'rgb(241,196,15)' : '#e74c3c';
        tempValue.style.color = tempColor;

        // 更新在线状态
        const age = Math.floor((Date.now() - data.timestamp) / 1000);
        const onlineStatus = document.getElementById('onlineStatus');
        const updateTimeSpan = document.getElementById('updateTime');
        if (onlineStatus && updateTimeSpan) {
          onlineStatus.textContent = age < 90 ? '● 在线' : '● 离线';
          onlineStatus.className = age < 90 ? 'status-online' : 'status-offline';
          updateTimeSpan.textContent = age + '秒前更新';
        }
      } catch (e) {
        console.error('获取传感器数据失败:', e);
      }
    }

    const temperature = ${latestData.temperature};
    let tempColor = temperature < 20 ? '#3498db' : temperature >= 20 && temperature < 30 ? 'rgb(241,196,15)' : '#e74c3c';
    const humColor = '#28a745';
    async function controlAC(action) {
      const statusDiv = document.getElementById('acStatus');
      const btnOn = document.getElementById('btnOn');
      const btnOff = document.getElementById('btnOff');
      btnOn.disabled = true; btnOff.disabled = true;
      statusDiv.className = 'ac-status show';
      statusDiv.textContent = '发送指令中...';
      try {
        const response = await fetch('/ac', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ action: action }) });
        const data = await response.json();
        if (data.status === 'success') {
          statusDiv.className = 'ac-status success show';
          statusDiv.textContent = '✅ ' + data.message;
        } else throw new Error(data.message || '操作失败');
      } catch (error) {
        statusDiv.className = 'ac-status error show';
        statusDiv.textContent = '❌ ' + error.message;
      }
      setTimeout(() => { btnOn.disabled = false; btnOff.disabled = false; setTimeout(() => statusDiv.className = 'ac-status', 3000); }, 2000);
    }
    async function toggleSchedule() {
      const enabled = !scheduleEnabled;
      const statusDiv = document.getElementById('acStatus');
      const scheduleStatusDiv = document.getElementById('scheduleStatusDiv');
      statusDiv.className = 'ac-status show';
      statusDiv.textContent = '切换中...';
      scheduleStatusDiv.className = 'schedule-status waiting';
      scheduleStatusDiv.textContent = '⏳ 等待ESP32确认...';
      try {
        const response = await fetch('/schedule', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ enabled: enabled }) });
        const data = await response.json();
        if (data.status === 'success') {
          document.getElementById('scheduleSwitch').checked = enabled;
          // 等待ESP32确认
          scheduleEnabled = enabled;
          scheduleConfirmed = false;
          await waitForConfirmation(5000); // 等待5秒确认
          statusDiv.className = 'ac-status success show';
          statusDiv.textContent = '✅ ' + data.message;
        } else throw new Error(data.message || '操作失败');
      } catch (error) {
        statusDiv.className = 'ac-status error show';
        statusDiv.textContent = '❌ ' + error.message;
        document.getElementById('scheduleSwitch').checked = scheduleEnabled;
        scheduleStatusDiv.className = 'schedule-status';
        scheduleStatusDiv.textContent = '';
      }
      setTimeout(() => statusDiv.className = 'ac-status', 3000);
    }
    async function waitForConfirmation(timeout) {
      const startTime = Date.now();
      const scheduleStatusDiv = document.getElementById('scheduleStatusDiv');

      while (Date.now() - startTime < timeout) {
        try {
          const response = await fetch('/api/status');
          const data = await response.json();
          if (data.enabled === scheduleEnabled) {
            scheduleConfirmed = true;
            scheduleStatusDiv.className = 'schedule-status confirmed';
            scheduleStatusDiv.textContent = '✅ ESP32已确认: ' + (data.enabled ? '启用' : '禁用');
            return;
          }
        } catch (e) {
          console.error('获取状态失败:', e);
        }
        await new Promise(resolve => setTimeout(resolve, 500)); // 每0.5秒检查一次
      }

      // 超时未确认
      scheduleStatusDiv.className = 'schedule-status waiting';
      scheduleStatusDiv.textContent = '⚠️ 等待ESP32确认超时，请检查网络连接';
    }
    async function checkScheduleStatus() {
      try {
        const response = await fetch('/api/status');
        const data = await response.json();
        if (Date.now() - data.lastUpdate < 60000) { // 60秒内的状态才有效
          scheduleEnabled = data.enabled;
          document.getElementById('scheduleSwitch').checked = scheduleEnabled;
        }
      } catch (e) {
        console.error('获取定时空调状态失败:', e);
      }
    }
    document.addEventListener('DOMContentLoaded', function() {
      document.getElementById('temp-value').style.color = tempColor;
      document.getElementById('hum-value').style.color = humColor;
      checkScheduleStatus(); // 初始化时检查状态
      updateSensorData(); // 初始化时获取传感器数据
    });
    setInterval(updateTime, 1000);
    setInterval(updateSensorData, 10000); // 每10秒更新传感器数据
    setInterval(checkScheduleStatus, 10000); // 每10秒检查一次状态
    window.onload = updateTime;
  </script>
</head>
<body>
  <div class="container">
    <div class="header">
      <div class="icon">🏢</div>
      <div class="title">办公室监控</div>
      <div class="subtitle">Office Monitor</div>
    </div>
    <div class="time-display" id="time">--:--:--</div>
    <div class="data-grid">
      <div class="data-card">
        <div class="data-label">🌡️ 温度</div>
        <div class="data-value" id="temp-value">${latestData.temperature.toFixed(1)}°C</div>
      </div>
      <div class="data-card">
        <div class="data-label">💧 湿度</div>
        <div class="data-value" id="hum-value">${latestData.humidity.toFixed(1)}%</div>
      </div>
    </div>

    <div class="control-section">
      <div class="section-title">❄️ 手动空调控制</div>
      <div class="ac-controls">
        <button class="ac-btn ac-btn-on" id="btnOn" onclick="controlAC('on')"><span>❄️</span><span>开启空调</span></button>
        <button class="ac-btn ac-btn-off" id="btnOff" onclick="controlAC('off')"><span>🔴</span><span>关闭空调</span></button>
      </div>
      <div class="ac-status" id="acStatus"></div>
    </div>

    <div class="control-section">
      <div class="section-title">📅 定时空调控制</div>
      <div class="schedule-toggle">
        <span class="switch-label">启用定时空调</span>
        <label class="toggle-switch">
          <input type="checkbox" id="scheduleSwitch" ${scheduleStatus.enabled ? 'checked' : ''} onchange="toggleSchedule()">
          <span class="slider"></span>
        </label>
      </div>
      <div class="schedule-status" id="scheduleStatusDiv"></div>
      <div class="schedule-info">
        工作日 8:00-17:30 自动控制（温度低于17°C时开启）
      </div>
    </div>

    <div class="status-bar">
      <span class="${age < 90 ? 'status-online' : 'status-offline'}" id="onlineStatus">${age < 90 ? '● 在线' : '● 离线'}</span>
      <span style="margin: 0 10px;">|</span>
      <span id="updateTime">${age}秒前更新</span>
    </div>
  </div>
</body>
</html>`;
    res.setHeader('Content-Type', 'text/html; charset=utf-8');
    res.writeHead(200);
    res.end(html);
  } else {
    res.writeHead(404);
    res.end('Not Found');
  }
});

server.listen(3789, '127.0.0.1', () => {
  console.log('办公室ESP32监控服务运行在端口 3789');
});
