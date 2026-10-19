//   _____ ____  ____  ____             _        _
//  | ____/ ___||  _ \/ ___|  ___   ___| | _____| |_
//  |  _| \___ \| |_) \___ \ / _ \ / __| |/ / _ \ __|
//  | |___ ___) |  __/ ___) | (_) | (__|   <  __/ |_
//  |_____|____/|_|   |____/ \___/ \___|_|\_\___|\__|
//
// ==================================================== INTERFAZ DE USUARIO ================================================
// Interfaz base de comunicacion ESP32 - PS PYNQZ1 - PL PYNQZ1
// Usuario -> WebSocket -> UART -> PS(PL) -> PL(AXI)
// =========================================================================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <esp_system.h>

#if defined(ARDUINO_ESP32S3_DEV)
  #define UART_RX 16
  #define UART_TX 17
  #define PLACA_1
#elif defined(ARDUINO_ESP32_DEV)
  #define UART_RX 18
  #define UART_TX 19
  #define PLACA_2
#else
  #define UART_RX 16
  #define UART_TX 17
  #define PLACA_DESCONOCIDA
  #warning "Placa no reconocida"
#endif

#define UART_BR 115200

// ============================ CONFIGURACION WIFI ============================
// 1 = AP, 0 = STA
#define MODO_AP 0

// AP
const char *APssid = "ESPSocket";
const char *APpassword = "123456789";   // minimo 8 chars
const uint8_t APchannel = 6;

// STA
const char *ssid = "ssid";
const char *password = "password";

// ============================ DEBUG ============================
#define DEBUG_IN 25
#define DEBUG_OUT 26

// ============================ SERVIDORES ============================
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// ============================ ESTADO UART ============================
String uartRxLine;
static uint32_t lastWsBroadcastMs = 0;

// ============================ HTML ============================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <title>WebSocket TFG</title>
  <style>
    body {
      font-family: 'Segoe UI', sans-serif;
      background: #1a1a2e;
      color: #eee;
      padding: 2em;
      text-align: center;
      min-height: 200px;
    }

    .menu-container {
      display: flex;
      flex-direction: row;
      gap: 1em;
      position: absolute;
      top: 7.5em;
      left: 2em;
      flex-wrap: wrap;
    }

    .menu-item {
      position: relative;
    }

    .submenu {
      position: absolute;
      top: 110%;
      left: 0;
      display: none;
      background-color: #00aacc;
      border: 1px solid #444;
      border-radius: 5px;
      padding: 0.5em;
      min-width: 120px;
      min-height: 100px;
      box-shadow: 0 0 10px #00000088;
      z-index: 10;
    }

    .menu-button {
      background-color: #3a3a3a;
      color: white;
      padding: 0.6em;
      border: none;
      border-radius: 5px;
      cursor: pointer;
      font-weight: bold;
      font-size: 1em;
      width: 6em;
      height: 3em;
      transition: background-color 0.25s;
    }

    .menu-button:hover {
      background-color: #555;
    }

    .submenu-button {
      display: block;
      background-color: #00668d;
      color: white;
      border: none;
      border-radius: 6px;
      margin: 0.4em auto;
      padding: 0.5em;
      width: 90%;
      font-size: 0.9em;
      cursor: pointer;
      transition: background-color 0.25s;
    }

    .submenu-button:hover {
      background-color: #0077cc;
    }

    .submenu-label {
      font-size: 0.8em;
      color: #FFF;
      margin-bottom: 0.5em;
    }

    #log {
      position: relative;
      width: min(1100px, 92vw);
      height: 320px;
      box-sizing: border-box;
      background: #aaaaaa;
      color: #542;
      font-family: monospace;
      padding: 1em;
      margin-top: 150px;
      margin-left: auto;
      margin-right: auto;
      border-radius: 8px;
      overflow-y: auto;
      overflow-x: hidden;
      white-space: pre-wrap;
      text-align: left;
    }

    .data-row {
      display: flex;
      justify-content: center;
      gap: 0.4em;
      margin-top: 120px;
      padding: 1em;
      position: relative;
      flex-wrap: wrap;
    }

    .data-box {
      background-color: #59de54;
      border: 1px solid #444;
      border-radius: 5px;
      padding: 0.5em;
      min-width: 130px;
      text-align: center;
      box-shadow: 0 0 10px #00000088;
      z-index: 10;
      color: #111;
    }

    .switch {
      position: relative;
      display: inline-block;
      width: 50px;
      height: 28px;
      vertical-align: middle;
    }

    .switch input {
      opacity: 0;
      width: 0;
      height: 0;
    }

    .switch-column {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 0.3em;
      width: 140px;
      text-align: center;
    }

    .slider {
      position: absolute;
      cursor: pointer;
      top: 0; left: 0; right: 0; bottom: 0;
      background-color: #ff9500;
      transition: .4s;
      border-radius: 28px;
    }

    .slider::before {
      position: absolute;
      content: "";
      height: 22px;
      width: 22px;
      left: 3px;
      bottom: 3px;
      background-color: white;
      transition: .4s;
      border-radius: 50%;
    }

    input:checked + .slider {
      background-color: #4CAF50;
    }

    input:checked + .slider::before {
      transform: translateX(22px);
    }

    .label-text {
      margin-left: 10px;
      color: white;
      font-weight: bold;
      font-size: 0.9em;
    }

    input[type="range"] {
      -webkit-appearance: none;
      appearance: none;
      width: 100px;
      height: 6px;
      background: #333;
      border-radius: 5px;
      outline: none;
      cursor: pointer;
      transition: background 0.3s;
      margin-top: 0.5em;
    }

    input[type="range"]::-webkit-slider-thumb {
      -webkit-appearance: none;
      appearance: none;
      width: 16px;
      height: 16px;
      background: #4CAF50;
      border: 2px solid #fff;
      border-radius: 50%;
      box-shadow: 0 0 4px rgba(0, 0, 0, 0.6);
      transition: background 0.3s;
    }

    input[type="range"]::-webkit-slider-thumb:hover {
      background: #66bb6a;
    }

    input[type="range"]::-moz-range-thumb:hover {
      background: #66bb6a;
    }

    .control-row {
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 0.45em;
      margin-top: 0.55em;
      flex-wrap: wrap;
    }

    .ctrl-name {
      width: 85px;
      text-align: left;
      color: #fff;
      font-size: 0.9em;
      font-weight: 600;
    }

    .ctrl-btn {
      background-color: #0b5f89;
      color: #fff;
      border: none;
      border-radius: 6px;
      padding: 0.35em 0.55em;
      min-width: 34px;
      cursor: pointer;
      transition: background-color 0.25s;
    }

    .ctrl-btn:hover {
      background-color: #0f76ab;
    }

    .ctrl-btn.apply {
      background-color: #1f8f3a;
      min-width: 74px;
    }

    .ctrl-btn.apply:hover {
      background-color: #2fa94a;
    }

    #manual-control-box input[type="range"] {
      width: 130px;
      margin-top: 0;
    }

    .control-row input[type="number"] {
      width: 72px;
      padding: 0.25em;
      border-radius: 5px;
      border: 1px solid #444;
      background: #222;
      color: #eee;
      text-align: center;
    }

    .submenu.sprite-menu {
      min-width: 210px;
      min-height: auto;
    }

    .sprite-grid {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 0.35em;
    }

    .sprite-mini {
      width: 38px;
      height: 30px;
      margin: 0;
      padding: 0;
      font-size: 0.8em;
      font-weight: 700;
      border-radius: 5px;
    }
  </style>
</head>

<body>
  <h2>Interfaz PYNQ</h2>

  <div class="menu-container">
    <div class="menu-item">
      <button class="menu-button" onclick="toggleMenu('menu1')">Fondo</button>
      <div id="menu1" class="submenu">
        <div class="submenu-label">Selecciona fondo</div>
        <button class="submenu-button" onclick="enviar('fondo1')">Fondo 1</button>
        <button class="submenu-button" onclick="enviar('fondo2')">Fondo 2</button>
      </div>
    </div>

    <div class="menu-item">
      <button class="menu-button" onclick="toggleMenu('menu2')">Sprite</button>
      <div id="menu2" class="submenu sprite-menu">
        <div class="submenu-label">Selecciona sprite (0..15)</div>
        <div class="sprite-grid">
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:0')">0</button>
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:1')">1</button>
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:2')">2</button>
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:3')">3</button>
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:4')">4</button>
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:5')">5</button>
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:6')">6</button>
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:7')">7</button>
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:8')">8</button>
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:9')">9</button>
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:10')">10</button>
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:11')">11</button>
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:12')">12</button>
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:13')">13</button>
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:14')">14</button>
          <button class="submenu-button sprite-mini" onclick="enviar('sprite:15')">15</button>
        </div>
      </div>
    </div>

    <div class="menu-item">
      <button class="menu-button" onclick="clearLog()">Limpiar LOGS</button>
    </div>

    <div class="menu-item switch-column">
      <span id="modoTexto" class="label-text">Modo automatico</span>
      <label class="switch">
        <input type="checkbox" id="modoSwitch" onclick="toggleMode()">
        <span class="slider"></span>
      </label>
    </div>
  </div>

  <div id="log"></div>

  <div class="data-row">
    <div id="data-container" class="data-box">
      <div class="submenu-label">Telemetria UART</div>
      <p id="uart-data">...</p>
    </div>

    <div id="data-container-2" class="data-box">
      <div class="submenu-label">Cadera</div>
      <p id="cadera">40</p>
    </div>

    <div id="data-container-3" class="data-box">
      <div class="submenu-label">Separacion paso</div>
      <p id="paso">80</p>
    </div>

    <div class="data-box" id="manual-control-box" style="display:none; min-width: 540px;">
      <div class="submenu-label">Control manual</div>

      <div class="control-row">
        <span class="ctrl-name">Velocidad</span>
        <button class="ctrl-btn" onclick="stepControl('vel', -1)">-</button>
        <input type="range" id="velSlider" min="0" max="10" value="5" step="1" />
        <button class="ctrl-btn" onclick="stepControl('vel', 1)">+</button>
        <input type="number" id="velInput" min="0" max="10" value="5" step="1" />
        <button class="ctrl-btn apply" onclick="sendControl('vel')">Aplicar</button>
      </div>

      <div class="control-row">
        <span class="ctrl-name">Cadera</span>
        <button class="ctrl-btn" onclick="stepControl('cadera', -1)">-</button>
        <input type="range" id="caderaSlider" min="-240" max="240" value="40" step="1" />
        <button class="ctrl-btn" onclick="stepControl('cadera', 1)">+</button>
        <input type="number" id="caderaInput" min="-240" max="240" value="40" step="1" />
        <button class="ctrl-btn apply" onclick="sendControl('cadera')">Aplicar</button>
      </div>

      <div class="control-row">
        <span class="ctrl-name">Paso (gap)</span>
        <button class="ctrl-btn" onclick="stepControl('gap', -1)">-</button>
        <input type="range" id="gapSlider" min="0" max="1023" value="80" step="1" />
        <button class="ctrl-btn" onclick="stepControl('gap', 1)">+</button>
        <input type="number" id="gapInput" min="0" max="1023" value="80" step="1" />
        <button class="ctrl-btn apply" onclick="sendControl('gap')">Aplicar</button>
      </div>

      <div class="control-row">
        <button class="ctrl-btn apply" onclick="sendAllControls()">Aplicar todo</button>
        <span id="manualStatus" class="submenu-label"></span>
      </div>
    </div>
  </div>

  <script>
    const log = document.getElementById("log");
    let ws = null;
    let conBool = false;
    let reconnectTimer = null;
    let pingTimer = null;

    const manualControlBox = document.getElementById("manual-control-box");
    const manualStatus = document.getElementById("manualStatus");
    const caderaText = document.getElementById("cadera");
    const pasoText = document.getElementById("paso");

    const controls = {
      vel: {
        slider: document.getElementById("velSlider"),
        input: document.getElementById("velInput"),
        min: 0,
        max: 10,
        prefix: "vel:",
        label: "Velocidad"
      },
      cadera: {
        slider: document.getElementById("caderaSlider"),
        input: document.getElementById("caderaInput"),
        min: -240,
        max: 240,
        prefix: "cadera:",
        label: "Cadera"
      },
      gap: {
        slider: document.getElementById("gapSlider"),
        input: document.getElementById("gapInput"),
        min: 0,
        max: 1023,
        prefix: "gap:",
        label: "Gap"
      }
    };

    function appendLog(msg) {
      log.textContent += msg + "\n";
      log.scrollTop = log.scrollHeight;
    }

    function clearLog() {
      log.textContent = conBool ? "[WS] Conectado\n" : "[WS] Desconectado\n";
    }

    function connectWS() {
      if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
        return;
      }

      const proto = (location.protocol === "https:") ? "wss://" : "ws://";
      ws = new WebSocket(proto + location.hostname + ":81/");

      ws.onopen = () => {
        conBool = true;
        appendLog("[WS] Conectado");
        if (reconnectTimer) {
          clearTimeout(reconnectTimer);
          reconnectTimer = null;
        }
        if (pingTimer) clearInterval(pingTimer);
        pingTimer = setInterval(() => {
          if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send("ping");
          }
        }, 5000);
      };

      ws.onmessage = (e) => {
        if (typeof e.data !== "string") return;

        if (e.data.startsWith("UART:")) {
          document.getElementById("uart-data").textContent = e.data.substring(5);
        } else if (e.data === "pong") {
          // heartbeat ok
        } else {
          appendLog(e.data);
        }
      };

      ws.onerror = () => {
        appendLog("[WS] Error");
      };

      ws.onclose = () => {
        conBool = false;
        appendLog("[WS] Desconectado");
        if (pingTimer) {
          clearInterval(pingTimer);
          pingTimer = null;
        }
        if (!reconnectTimer) {
          reconnectTimer = setTimeout(() => {
            reconnectTimer = null;
            connectWS();
          }, 1500);
        }
      };
    }

    function safeSend(msg) {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(msg);
        return true;
      }
      appendLog("[WS] No conectado, no se envia: " + msg);
      return false;
    }

    function enviar(msg) {
      safeSend(msg);
    }

    function clampControlValue(value, min, max) {
      const n = Number(value);
      if (Number.isNaN(n)) return min;
      return Math.max(min, Math.min(max, Math.round(n)));
    }

    function setControlValue(name, value) {
      const cfg = controls[name];
      const v = clampControlValue(value, cfg.min, cfg.max);
      cfg.slider.value = v;
      cfg.input.value = v;

      if (name === "cadera") caderaText.textContent = v;
      if (name === "gap") pasoText.textContent = v;

      return v;
    }

    function sendControl(name) {
      const cfg = controls[name];
      const v = setControlValue(name, cfg.input.value);
      if (safeSend(cfg.prefix + v)) {
        manualStatus.textContent = cfg.label + "=" + v;
      }
    }

    function stepControl(name, delta) {
      const cfg = controls[name];
      const current = Number(cfg.input.value);
      setControlValue(name, current + Number(delta));
    }

    function sendAllControls() {
      sendControl("vel");
      sendControl("cadera");
      sendControl("gap");
      manualStatus.textContent = "Valores enviados";
    }

    Object.keys(controls).forEach((name) => {
      const cfg = controls[name];

      cfg.slider.addEventListener("input", () => {
        setControlValue(name, cfg.slider.value);
      });

      cfg.slider.addEventListener("change", () => {
        sendControl(name);
      });

      cfg.input.addEventListener("change", () => {
        sendControl(name);
      });

      cfg.input.addEventListener("keydown", (e) => {
        if (e.key === "Enter") {
          e.preventDefault();
          sendControl(name);
        }
      });

      setControlValue(name, cfg.slider.value);
    });

    function toggleMode() {
      const isManual = document.getElementById("modoSwitch").checked;
      const modoTexto = document.getElementById("modoTexto");

      if (isManual) {
        modoTexto.textContent = "Modo manual";
        manualControlBox.style.display = "block";
        safeSend("modo: manual");
        sendAllControls();
      } else {
        modoTexto.textContent = "Modo automatico";
        manualControlBox.style.display = "none";
        safeSend("modo: auto");
      }
    }

    function toggleMenu(id) {
      document.querySelectorAll(".submenu").forEach(menu => {
        if (menu.id !== id) menu.style.display = "none";
      });

      const menu = document.getElementById(id);
      menu.style.display = (menu.style.display === "block") ? "none" : "block";
    }

    document.addEventListener("click", function(e) {
      const isButton = e.target.classList.contains("menu-button");
      if (!isButton) {
        document.querySelectorAll(".submenu").forEach(menu => {
          menu.style.display = "none";
        });
      }
    });

    connectWS();
  </script>
</body>
</html>
)rawliteral";

// ============================ UTILIDADES ============================
void sendRoot() {
  server.send_P(200, "text/html; charset=UTF-8", INDEX_HTML);
}

void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void logResetReason() {
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("Reset reason: %d\n", (int)reason);
}

void printHeap() {
  Serial.printf("Heap libre: %u bytes\n", ESP.getFreeHeap());
}

void setupWiFi() {
  WiFi.mode(WIFI_OFF);
  delay(100);

#if MODO_AP
  Serial.println("Modo AP seleccionado");
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  bool ok = WiFi.softAP(APssid, APpassword, APchannel, 0, 4);
  if (!ok) {
    Serial.println("Error iniciando AP");
  } else {
    Serial.println("AP iniciado");
    Serial.print("SSID: ");
    Serial.println(APssid);
    Serial.print("IP AP: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("Canal AP: ");
    Serial.println(APchannel);
  }
#else
  Serial.println("Modo STA seleccionado");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  Serial.print("Conectando a WiFi");
  uint32_t t0 = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
    if (millis() - t0 > 20000) {
      Serial.println("\nTimeout conectando a WiFi. Reiniciando intento...");
      WiFi.disconnect(true, true);
      delay(500);
      WiFi.begin(ssid, password);
      t0 = millis();
    }
  }

  Serial.println("\nConectado a WiFi");
  Serial.print("IP STA: ");
  Serial.println(WiFi.localIP());
  Serial.print("RSSI: ");
  Serial.println(WiFi.RSSI());
#endif
}

void sendWsText(uint8_t num, const String &msg) {
  webSocket.sendTXT(num, msg.c_str());
}

void processIncomingCommand(uint8_t num, const String &msg) {
  if (msg == "ping") {
    sendWsText(num, "pong");
    return;
  }

  if (msg.startsWith("vel:")) {
    String s = msg.substring(4);
    s.trim();
    int v = s.toInt();

    if (s.length() == 0) {
      sendWsText(num, "Error: vel sin valor");
      return;
    }

    v = constrain(v, 0, 10);
    Serial1.printf("SETSPEED::%d\n", v);
    sendWsText(num, "OK: velocidad=" + String(v));
    return;
  }

  if (msg.startsWith("gap:")) {
    String s = msg.substring(4);
    s.trim();
    int g = s.toInt();

    if (s.length() == 0) {
      sendWsText(num, "Error: gap sin valor");
      return;
    }

    g = constrain(g, 0, 1023);
    Serial1.printf("SETGAP::%d\n", g);
    sendWsText(num, "OK: gap=" + String(g));
    return;
  }

  if (msg.startsWith("cadera:")) {
    String s = msg.substring(7);
    s.trim();
    int c = s.toInt();

    if (s.length() == 0) {
      sendWsText(num, "Error: cadera sin valor");
      return;
    }

    c = constrain(c, -240, 240);
    Serial1.printf("SETCADERA::%d\n", c);
    sendWsText(num, "OK: cadera=" + String(c));
    return;
  }

  if (msg.startsWith("sprite:")) {
    String s = msg.substring(7);
    s.trim();

    if (s.length() == 0) {
      sendWsText(num, "Error: sprite sin valor");
      return;
    }

    bool isNum = true;
    for (size_t i = 0; i < s.length(); i++) {
      if (!isDigit((unsigned char)s[i])) {
        isNum = false;
        break;
      }
    }

    if (!isNum) {
      sendWsText(num, "Error: sprite no numerico");
      return;
    }

    int sp = s.toInt();
    if (sp < 0 || sp > 15) {
      sendWsText(num, "Error: sprite fuera de rango [0..15]");
      return;
    }

    Serial1.printf("SETSPRITE::%d\n", sp);
    sendWsText(num, "OK: sprite=" + String(sp));
    return;
  }

  if (msg == "fondo1") {
    Serial.println("Fondo 1 seleccionado");
    Serial1.print("SETBG::0\n");
    sendWsText(num, "Recibido: fondo1");
    return;
  }

  if (msg == "fondo2") {
    Serial.println("Fondo 2 seleccionado");
    Serial1.print("SETBG::1\n");
    sendWsText(num, "Recibido: fondo2");
    return;
  }

  if (msg == "modo: manual") {
    Serial.println("Modo manual activo");
    sendWsText(num, "Modo manual activado");
    return;
  }

  if (msg == "modo: auto") {
    Serial.println("Modo automatico activo");
    sendWsText(num, "Modo automatico activado");
    return;
  }

  Serial.printf("Error de mensaje: %s\n", msg.c_str());
  sendWsText(num, "Error: comando no reconocido");
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[WS] Cliente %u desconectado\n", num);
      break;

    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[WS] Cliente %u conectado desde %u.%u.%u.%u\n",
                    num, ip[0], ip[1], ip[2], ip[3]);
      sendWsText(num, "[WS] Conexion establecida");
      break;
    }

    case WStype_TEXT: {
      String msg;
      msg.reserve(len + 1);
      for (size_t i = 0; i < len; i++) {
        msg += (char)payload[i];
      }
      msg.trim();

      Serial.printf("[WS] Recibido (%u): %s\n", num, msg.c_str());
      processIncomingCommand(num, msg);
      break;
    }

    case WStype_BIN:
      Serial.printf("[WS] BIN recibido de cliente %u, %u bytes\n", num, (unsigned)len);
      break;

    case WStype_PING:
      Serial.printf("[WS] PING desde cliente %u\n", num);
      break;

    case WStype_PONG:
      Serial.printf("[WS] PONG desde cliente %u\n", num);
      break;

    default:
      break;
  }
}

void readUARTNonBlocking() {
  while (Serial1.available()) {
    char c = (char)Serial1.read();

    if (c == '\n') {
      uartRxLine.trim();
      if (uartRxLine.length() > 0) {
        // opcional: pequeña limitacion temporal si la UART vomita demasiado
        uint32_t now = millis();
        if (now - lastWsBroadcastMs >= 5) {
          webSocket.broadcastTXT(("UART:" + uartRxLine).c_str());
          lastWsBroadcastMs = now;
        }
      }
      uartRxLine = "";
    } else if (c != '\r') {
      // Evita crecimiento infinito si llega basura sin fin de linea
      if (uartRxLine.length() < 256) {
        uartRxLine += c;
      } else {
        uartRxLine = "";
        Serial.println("[UART] Buffer reseteado por exceso de longitud");
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("========================================");
  Serial.println("BOOT ESP32 WebSocket UART bridge");
  Serial.println("========================================");

  logResetReason();

  Serial1.begin(UART_BR, SERIAL_8N1, UART_RX, UART_TX);
  Serial1.setTimeout(10);

#if defined(PLACA_DESCONOCIDA)
  Serial.println("Placa desconocida");
#elif defined(PLACA_1)
  Serial.println("Placa ESP32-S3");
#elif defined(PLACA_2)
  Serial.println("Placa ESP32 convencional");
#endif

#ifdef ARDUINO_BOARD
  Serial.print("ARDUINO_BOARD: ");
  Serial.println(ARDUINO_BOARD);
#endif

  pinMode(DEBUG_IN, INPUT);
  pinMode(DEBUG_OUT, OUTPUT);
  digitalWrite(DEBUG_OUT, LOW);

  setupWiFi();

  server.on("/", HTTP_GET, sendRoot);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Servidor HTTP iniciado en puerto 80");

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  // ping cada 15 s, timeout 3 s, desconecta tras 2 fallos
  webSocket.enableHeartbeat(15000, 3000, 2);
  Serial.println("WebSocket iniciado en puerto 81");

  printHeap();
}

void loop() {
  server.handleClient();
  webSocket.loop();

  readUARTNonBlocking();

  digitalWrite(DEBUG_OUT, digitalRead(DEBUG_IN) ? HIGH : LOW);
}