#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <FastLED.h>

// ==================== CONFIGURATION ====================
const char* WIFI_SSID     = "MagentaWLAN-1X46";
const char* WIFI_PASSWORD = "37895278861980387018";

#define LED_PIN        4
#define NUM_LEDS       100
#define CURRENT_LIMIT  2000 // 2A current limit for 5V 2A supply
#define DDP_PORT       4048
#define MAX_PACKET     1500

CRGB leds[NUM_LEDS];
WebServer server(80);
WiFiUDP udp;
uint8_t packetBuffer[MAX_PACKET];

// Controller State
bool powerOn = true;
uint8_t brightness = 180;
CRGB solidColor = CRGB::White;
uint8_t currentMode = 1; // 0=Solid, 1=Rainbow, 2=Fire, 3=Breathe, 4=Chase, 5=Sparkle, 99=DDP (Music)

unsigned long lastDdpTime = 0;
const unsigned long DDP_TIMEOUT_MS = 2500;
uint8_t animStep = 0;
unsigned long lastAnimUpdate = 0;

// Embedded HTML UI
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 LED Controller</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #121212; color: #fff; text-align: center; margin: 0; padding: 20px; }
    .card { background: #1e1e1e; max-width: 420px; margin: 0 auto; padding: 25px; border-radius: 20px; box-shadow: 0 8px 30px rgba(0,0,0,0.5); }
    h2 { margin-top: 0; font-size: 26px; font-weight: 700; }
    .btn { display: inline-block; width: 100%; box-sizing: border-box; padding: 16px; margin: 8px 0; border: none; border-radius: 12px; font-size: 18px; font-weight: 600; cursor: pointer; transition: 0.2s; }
    .btn-power { background: #e53935; color: #fff; }
    .btn-power.on { background: #43a047; }
    .btn-effect { background: #2c2c2c; color: #eee; width: 48%; margin: 1%; }
    .btn-effect.active { background: #2196f3; color: #fff; }
    .effects-grid { display: flex; flex-wrap: wrap; justify-content: space-between; margin: 15px 0; }
    .slider-box { margin: 20px 0; text-align: left; }
    .slider-box label { display: block; font-size: 14px; color: #bbb; margin-bottom: 8px; }
    input[type=range] { width: 100%; height: 8px; border-radius: 4px; background: #444; outline: none; -webkit-appearance: none; }
    input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 22px; height: 22px; border-radius: 50%; background: #2196f3; cursor: pointer; }
    input[type=color] { border: none; width: 100%; height: 50px; border-radius: 12px; background: none; cursor: pointer; }
  </style>
</head>
<body>
  <div class="card">
    <h2>LED Controller</h2>
    <button id="pwrBtn" class="btn btn-power" onclick="togglePower()">Power ON/OFF</button>

    <div class="slider-box">
      <label>Brightness (<span id="brightVal">180</span>/255)</label>
      <input type="range" min="5" max="255" value="180" id="brightSlider" oninput="setBrightness(this.value)">
    </div>

    <div class="slider-box">
      <label>Solid Color</label>
      <input type="color" value="#ffffff" id="colorPicker" onchange="setColor(this.value)">
    </div>

    <div class="effects-grid">
      <button class="btn btn-effect" onclick="setEffect(1)">Rainbow</button>
      <button class="btn btn-effect" onclick="setEffect(2)">Fire</button>
      <button class="btn btn-effect" onclick="setEffect(3)">Breathe</button>
      <button class="btn btn-effect" onclick="setEffect(4)">Chase</button>
      <button class="btn btn-effect" onclick="setEffect(5)">Sparkle</button>
      <button class="btn btn-effect" onclick="setEffect(0)">Solid</button>
    </div>
  </div>

  <script>
    function togglePower() {
      fetch('/toggle').then(r => r.json()).then(data => updateUI(data));
    }
    function setBrightness(val) {
      document.getElementById('brightVal').innerText = val;
      fetch('/brightness?val=' + val);
    }
    function setColor(hex) {
      fetch('/color?hex=' + encodeURIComponent(hex));
    }
    function setEffect(id) {
      fetch('/mode?id=' + id);
    }
    function updateUI(data) {
      const pwr = document.getElementById('pwrBtn');
      if (data.power) {
        pwr.innerText = 'Power ON';
        pwr.classList.add('on');
      } else {
        pwr.innerText = 'Power OFF';
        pwr.classList.remove('on');
      }
      document.getElementById('brightSlider').value = data.brightness;
      document.getElementById('brightVal').innerText = data.brightness;
    }
    fetch('/status').then(r => r.json()).then(data => updateUI(data));
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  String json = "{\"power\":" + String(powerOn ? "true" : "false") + ",\"brightness\":" + String(brightness) + ",\"mode\":" + String(currentMode) + "}";
  server.send(200, "application/json", json);
}

void handleToggle() {
  powerOn = !powerOn;
  FastLED.setBrightness(powerOn ? brightness : 0);
  if (!powerOn) FastLED.clear(true);
  handleStatus();
}

void handleBrightness() {
  if (server.hasArg("val")) {
    brightness = server.arg("val").toInt();
    if (powerOn) FastLED.setBrightness(brightness);
  }
  server.send(200, "text/plain", "OK");
}

void handleColor() {
  if (server.hasArg("hex")) {
    String hex = server.arg("hex");
    if (hex.startsWith("#")) hex = hex.substring(1);
    long rgb = strtol(hex.c_str(), NULL, 16);
    solidColor = CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    currentMode = 0; // Solid
  }
  server.send(200, "text/plain", "OK");
}

void handleMode() {
  if (server.hasArg("id")) {
    currentMode = server.arg("id").toInt();
  }
  server.send(200, "text/plain", "OK");
}

void updateAnimations() {
  if (!powerOn) return;

  // Mode 0: Solid color
  if (currentMode == 0) {
    fill_solid(leds, NUM_LEDS, solidColor);
    FastLED.show();
    return;
  }

  // Animation timer ~50 FPS
  if (millis() - lastAnimUpdate < 20) return;
  lastAnimUpdate = millis();
  animStep++;

  switch (currentMode) {
    case 1: // Rainbow
      fill_rainbow(leds, NUM_LEDS, animStep * 2, 5);
      break;

    case 2: // Fire simulation
      for (int i = 0; i < NUM_LEDS; i++) {
        uint8_t heat = qsub8(inoise8(i * 60, animStep * 15), 50);
        leds[i] = HeatColor(heat);
      }
      break;

    case 3: // Breathe
      {
        uint8_t breath = beatsin8(25, 20, 255);
        fill_solid(leds, NUM_LEDS, solidColor);
        nscale8_video(leds, NUM_LEDS, breath);
      }
      break;

    case 4: // Chase
      fadeToBlackBy(leds, NUM_LEDS, 40);
      leds[animStep % NUM_LEDS] = solidColor;
      break;

    case 5: // Sparkle
      fadeToBlackBy(leds, NUM_LEDS, 30);
      if (random8() < 60) {
        leds[random16(NUM_LEDS)] = CRGB::White;
      }
      break;
  }
  FastLED.show();
}

void handleDdpStreaming() {
  int packetSize = udp.parsePacket();
  while (packetSize > 0) {
    if (packetSize > 10) {
      int len = udp.read(packetBuffer, MAX_PACKET);
      uint8_t flags = packetBuffer[0];
      uint8_t headerSize = (flags & 0x10) ? 14 : 10;

      if (len > headerSize) {
        uint32_t byteOffset = ((uint32_t)packetBuffer[4] << 24) |
                              ((uint32_t)packetBuffer[5] << 16) |
                              ((uint32_t)packetBuffer[6] << 8)  |
                               (uint32_t)packetBuffer[7];
        uint16_t dataLength = ((uint16_t)packetBuffer[8] << 8) | packetBuffer[9];
        uint16_t availableBytes = len - headerSize;
        if (dataLength > availableBytes) dataLength = availableBytes;

        uint16_t startLed = byteOffset / 3;
        uint16_t ledCount = dataLength / 3;

        for (uint16_t i = 0; i < ledCount; i++) {
          uint16_t idx = startLed + i;
          if (idx >= NUM_LEDS) break;
          uint16_t p = headerSize + (i * 3);
          leds[idx].r = packetBuffer[p];
          leds[idx].g = packetBuffer[p + 1];
          leds[idx].b = packetBuffer[p + 2];
        }

        if (powerOn) FastLED.show();
        lastDdpTime = millis();
      }
    } else {
      udp.read(packetBuffer, MAX_PACKET);
    }
    packetSize = udp.parsePacket();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);

  FastLED.addLeds<WS2811, LED_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  if (CURRENT_LIMIT > 0) FastLED.setMaxPowerInVoltsAndMilliamps(5, CURRENT_LIMIT);
  FastLED.setBrightness(brightness);
  FastLED.clear(true);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    leds[0] = (animStep++ % 2 == 0) ? CRGB::Blue : CRGB::Black;
    FastLED.show();
  }

  // Flash green 3 times when connected
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 5; j++) leds[j] = CRGB::Green;
    FastLED.show();
    delay(120);
    FastLED.clear(true);
    delay(120);
  }

  // Web routes
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/toggle", handleToggle);
  server.on("/brightness", handleBrightness);
  server.on("/color", handleColor);
  server.on("/mode", handleMode);
  server.begin();

  udp.begin(DDP_PORT);
}

void loop() {
  server.handleClient();
  handleDdpStreaming();

  // If receiving live DDP stream from PC/LedFx, DDP takes priority
  // Otherwise, run standalone web-selected animations
  if (millis() - lastDdpTime > DDP_TIMEOUT_MS) {
    updateAnimations();
  }
}

