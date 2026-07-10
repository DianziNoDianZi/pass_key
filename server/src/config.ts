export const config = {
  // HTTP Server
  httpPort: parseInt(process.env.HTTP_PORT || '3000', 10),
  host: process.env.HOST || '0.0.0.0',

  // MQTT Broker (aedes)
  mqttTcpPort: parseInt(process.env.MQTT_TCP_PORT || '1883', 10),
  mqttWsPort: parseInt(process.env.MQTT_WS_PORT || '8883', 10),

  // Database
  dbPath: process.env.DB_PATH || './data/passkey.db',

  // Weather API (HeFeng / 和风天气)
  weatherApiKey: process.env.WEATHER_API_KEY || '',
  weatherApiBase: 'https://devapi.qweather.com/v7',

  // Challenge timeout (seconds)
  challengeTimeout: parseInt(process.env.CHALLENGE_TIMEOUT || '120', 10),

  // Admin panel password (set via env ADMIN_PASSWORD)
  adminPassword: process.env.ADMIN_PASSWORD || 'passkey123',
};
