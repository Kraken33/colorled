// Force FastLED on ESP32-C3 to use full 2-block (96-word) double-buffering instead of 1-block fallback
#define FL_RMT_STATIC_ALLOCATION 1
#define FASTLED_RMT_MEM_BLOCKS 2

#include <WiFi.h>
#include <WiFiUdp.h>
#include <FastLED.h>

// ==================== USER CONFIGURATION ====================
const char* WIFI_SSID     = "MagentaWLAN-1X46";     // Must be 2.4 GHz Wi-Fi
const char* WIFI_PASSWORD = "37895278861980387018";

#define LED_PIN        4      // GPIO 4 on ESP32-C3 (connect to GREEN data wire)
#define NUM_LEDS       100    // Total number of LEDs on your strip
#define BRIGHTNESS     200    // 0-255 brightness
#define CURRENT_LIMIT  2000   // 2000mA (2A) limit for 5V 2A charger
// ============================================================

#define DDP_PORT    4048
#define MAX_PACKET  1500

CRGB leds[NUM_LEDS];
WiFiUDP udp;
uint8_t packetBuffer[MAX_PACKET];

unsigned long lastPacketTime = 0;
unsigned long lastShowTime = 0;
const unsigned long IDLE_TIMEOUT_MS = 3000;
const unsigned long MIN_SHOW_INTERVAL_MS = 15;

void setup() {
  Serial.begin(115200);
  // CRITICAL on ESP32-C3 Super Mini: prevent Serial.print() from freezing boot
  // when powered from a wall charger instead of a computer USB port!
  Serial.setTxTimeoutMs(0);

  // Initialize strip (Red = +5V to charger, White = GND to charger AND ESP32 'G' pin, Green = GPIO 4)
  FastLED.addLeds<WS2811, LED_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  if (CURRENT_LIMIT > 0) {
    FastLED.setMaxPowerInVoltsAndMilliamps(5, CURRENT_LIMIT);
  }
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);

  // Connect to 2.4 GHz Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Prevents Wi-Fi power-save lag so music stays in sync
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint8_t step = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    // Blink first LED blue while connecting
    leds[0] = (step++ % 2 == 0) ? CRGB::Blue : CRGB::Black;
    FastLED.show();
  }

  // Flash first 5 LEDs green 3 times when connected
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 5; j++) leds[j] = CRGB::Green;
    FastLED.show();
    delay(150);
    FastLED.clear(true);
    delay(150);
  }

  // Start listening for audio-reactive DDP packets
  udp.begin(DDP_PORT);
}

void loop() {
  bool shouldShow = false;

  int packetSize = udp.parsePacket();
  while (packetSize > 0) {
    if (packetSize > 10) {
      int len = udp.read(packetBuffer, MAX_PACKET);

      // Parse DDP header
      uint8_t flags = packetBuffer[0];
      bool pushFlag = (flags & 0x01) != 0;
      uint8_t headerSize = (flags & 0x10) ? 14 : 10;

      if (len > headerSize) {
        uint32_t byteOffset = ((uint32_t)packetBuffer[4] << 24) |
                              ((uint32_t)packetBuffer[5] << 16) |
                              ((uint32_t)packetBuffer[6] << 8)  |
                               (uint32_t)packetBuffer[7];

        uint16_t dataLength = ((uint16_t)packetBuffer[8] << 8) |
                               (uint16_t)packetBuffer[9];

        uint16_t availableBytes = len - headerSize;
        if (dataLength > availableBytes) {
          dataLength = availableBytes;
        }

        uint16_t startLed = byteOffset / 3;
        uint16_t ledCount = dataLength / 3;

        for (uint16_t i = 0; i < ledCount; i++) {
          uint16_t ledIndex = startLed + i;
          if (ledIndex >= NUM_LEDS) break;

          uint16_t idx = headerSize + (i * 3);
          leds[ledIndex].r = packetBuffer[idx];
          leds[ledIndex].g = packetBuffer[idx + 1];
          leds[ledIndex].b = packetBuffer[idx + 2];
        }

        if (pushFlag || (startLed + ledCount >= NUM_LEDS)) {
          shouldShow = true;
        }

        lastPacketTime = millis();
      }
    } else {
      udp.read(packetBuffer, MAX_PACKET);
    }
    packetSize = udp.parsePacket();
  }

  if (shouldShow && (millis() - lastShowTime >= MIN_SHOW_INTERVAL_MS)) {
    FastLED.show();
    lastShowTime = millis();
  }

  // Soft breathing cyan dot on LED 0 when waiting for music
  if (millis() - lastPacketTime > IDLE_TIMEOUT_MS) {
    uint8_t breath = beatsin8(20, 5, 60);
    FastLED.clear();
    leds[0] = CRGB(0, breath, breath);
    FastLED.show();
    delay(20);
  }
}
