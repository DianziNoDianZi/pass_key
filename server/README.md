# PassKey Server

PassKey 系统服务器后端，提供 MQTT Broker、REST API、SQLite 数据库等功能。

## 技术栈

- **运行时**: Node.js + TypeScript
- **MQTT Broker**: Aedes（嵌入式）
- **HTTP 框架**: Express.js
- **数据库**: SQLite (better-sqlite3)

## 快速开始

```bash
# 安装依赖
npm install

# 开发模式启动（自动监听文件变化）
npm run dev

# 构建
npm run build

# 生产模式启动
npm start
```

## 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `HTTP_PORT` | 3000 | HTTP 服务端口 |
| `HOST` | 0.0.0.0 | 监听地址 |
| `MQTT_TCP_PORT` | 1883 | MQTT TCP 端口 |
| `MQTT_WS_PORT` | 8883 | MQTT WebSocket 端口 |
| `DB_PATH` | ./data/passkey.db | SQLite 数据库路径 |
| `WEATHER_API_KEY` | - | 和风天气 API Key |
| `CHALLENGE_TIMEOUT` | 120 | 认证挑战超时时间（秒） |

## Docker 部署

```bash
# 使用 docker-compose（包含 EMQX 和 Server）
docker-compose up -d

# 或者构建单独镜像
docker build -t passkey-server .
docker run -p 3000:3000 -p 1883:1883 passkey-server
```

## API 端点

### 设备管理
- `POST /api/device/register` - 注册新设备
- `GET /api/device/:deviceId` - 获取设备信息
- `GET /api/devices` - 列出所有设备

### 认证挑战
- `POST /api/auth/challenge` - 创建登录挑战
- `GET /api/auth/status/:requestId` - 查询挑战状态
- `POST /api/auth/verify` - 验证签名

### 短信转发
- `POST /api/sms/forward` - 接收手机转发的短信

### 天气服务
- `GET /api/weather/:deviceId` - 获取设备所在地天气

## MQTT 主题

- `passkey/{deviceId}/cmd` - 下行命令（服务器 → 设备）
- `passkey/{deviceId}/resp` - 上行响应（设备 → 服务器）
