#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <SD_MMC.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_NeoPixel.h>

// Passive receive-only Wi-Fi management-frame monitor for networks you own.
// It never joins a network, transmits Wi-Fi frames, injects packets, or disconnects devices.

struct CaptureRecord {
  uint8_t kind;
  int8_t rssi;
  uint8_t channel;
  uint16_t length;
  uint32_t timestamp;
  uint8_t frame[128];
};

void drawPatrolLights(bool blueOn);
void updateRgbAlert(uint32_t now);
void drawPolice(bool closed);
void drawStreetScene();
void drawScene();
void redrawPoliceSprite(int16_t oldX);
void drawStatus();
void bufferRecord(uint8_t kind, const wifi_promiscuous_pkt_t *packet);
void wifiPacketCallback(void *buffer, wifi_promiscuous_pkt_type_t packetType);
void writeLe16(Stream &output, uint16_t value);
void writeLe32(Stream &output, uint32_t value);
void writePcapHeader(Stream &output);
void writePcapRecord(Stream &output, const CaptureRecord &record);
void beginSdLog();
void logRecordToSd(const CaptureRecord &record);
void drainCaptureBuffer();
void hopChannel();
void startPassiveMonitor();

constexpr int LCD_MOSI = 45;
constexpr int LCD_SCLK = 40;
constexpr int LCD_CS = 42;
constexpr int LCD_DC = 41;
constexpr int LCD_RST = 39;
constexpr int LCD_BL = 48;
constexpr int BUTTON_PIN = 0;
constexpr int RGB_LED_PIN = 38;
constexpr int SD_CLK = 14;
constexpr int SD_CMD = 15;
constexpr int SD_D0 = 16;
constexpr int SD_D1 = 18;
constexpr int SD_D2 = 17;
constexpr int SD_D3 = 21;
constexpr uint8_t RGB_LED_COUNT = 1;
constexpr int16_t SCREEN_W = 172;
constexpr int16_t SCREEN_H = 320;
constexpr uint16_t SKY = 0x19C7;
constexpr uint16_t UNIFORM = 0x0010;
constexpr uint16_t SKIN = 0xFD20;
constexpr uint32_t BLINK_INTERVAL_MS = 3500;
constexpr uint32_t BLINK_LENGTH_MS = 180;
constexpr uint32_t PATROL_STEP_MS = 260;
constexpr uint32_t BATON_SWING_MS = 620;
constexpr uint32_t KEY_SPIN_MS = 180;
constexpr uint32_t SIREN_FLASH_MS = 180;
constexpr uint32_t SIREN_DURATION_MS = 5000;
constexpr uint32_t DEAUTH_ALERT_DURATION_MS = 8000;
constexpr uint32_t CHANNEL_HOP_MS = 350;
constexpr uint32_t DISPLAY_STATUS_MS = 750;
constexpr uint32_t SD_FLUSH_MS = 1000;
constexpr uint8_t FIRST_CHANNEL = 1;
constexpr uint8_t LAST_CHANNEL = 11;
constexpr uint8_t CAPTURE_BUFFER_SIZE = 12;
constexpr uint16_t PCAP_SNAPLEN = 128;
constexpr uint32_t SD_LOG_LIMIT_BYTES = 16UL * 1024UL * 1024UL;

CaptureRecord captureBuffer[CAPTURE_BUFFER_SIZE];
volatile uint8_t bufferHead = 0;
volatile uint8_t bufferTail = 0;
volatile uint32_t droppedRecords = 0;
portMUX_TYPE captureMux = portMUX_INITIALIZER_UNLOCKED;

SPIClass displaySPI(HSPI);
Adafruit_ST7789 gfx(&displaySPI, LCD_CS, LCD_DC, LCD_RST);
Adafruit_NeoPixel rgbLed(RGB_LED_COUNT, RGB_LED_PIN, NEO_RGB + NEO_KHZ800);
File pcapFile;

int16_t policeX = 8;
int16_t lastRenderedPoliceX = 8;
int8_t patrolDirection = 1;
bool eyesClosed = false;
bool previousButton = HIGH;
bool sirenActive = false;
bool blueLightOn = true;
bool batonRaised = false;
bool keyTurned = false;
bool usbPcapHeaderSent = false;
bool sdLogEnabled = false;
bool sdLogLimitReached = false;
uint32_t lastBlinkAt = 0;
uint32_t blinkStartedAt = 0;
uint32_t lastPatrolStepAt = 0;
uint32_t lastBatonSwingAt = 0;
uint32_t lastKeySpinAt = 0;
uint32_t sirenStartedAt = 0;
uint32_t lastSirenFlashAt = 0;
uint32_t lastChannelHopAt = 0;
uint32_t lastDisplayStatusAt = 0;
uint32_t lastSdFlushAt = 0;
uint8_t currentChannel = FIRST_CHANNEL;
// Each action lasts for one walk across the screen: plain pace, baton tap, then key spin.
uint8_t patrolAction = 0;
uint32_t beaconCount = 0;
uint32_t probeCount = 0;
uint32_t deauthCount = 0;
uint32_t lastDeauthAt = 0;

void drawPatrolLights(bool blueOn) {
  gfx.fillRoundRect(22, 8, 128, 30, 8, ST77XX_BLACK);
  gfx.fillRoundRect(27, 12, 55, 22, 5, blueOn ? 0x001F : 0x7800);
  gfx.fillRoundRect(90, 12, 55, 22, 5, blueOn ? 0x7800 : 0xF800);
}

void updateRgbAlert(uint32_t now) {
  const bool deauthAlertActive = lastDeauthAt != 0 && now - lastDeauthAt < DEAUTH_ALERT_DURATION_MS;
  if (!(sirenActive || deauthAlertActive)) {
    rgbLed.setPixelColor(0, rgbLed.Color(255, 255, 255));
  } else {
    const bool showRed = ((now / SIREN_FLASH_MS) % 2) == 0;
    rgbLed.setPixelColor(0, showRed ? rgbLed.Color(255, 0, 0) : rgbLed.Color(0, 0, 255));
  }
  rgbLed.show();
}

void drawPolice(bool closed) {
  const int16_t y = 130;
  gfx.fillCircle(policeX + 28, y, 25, SKIN);
  gfx.fillRect(policeX + 5, y - 34, 46, 10, UNIFORM);
  gfx.fillRoundRect(policeX + 11, y - 47, 34, 16, 5, UNIFORM);
  gfx.fillRect(policeX + 25, y - 44, 6, 8, 0xFFE0);
  if (closed) {
    gfx.drawLine(policeX + 15, y - 2, policeX + 24, y - 2, ST77XX_BLACK);
    gfx.drawLine(policeX + 33, y - 2, policeX + 42, y - 2, ST77XX_BLACK);
  } else {
    gfx.fillCircle(policeX + 20, y, 5, ST77XX_BLACK);
    gfx.fillCircle(policeX + 37, y, 5, ST77XX_BLACK);
  }
  gfx.drawLine(policeX + 22, y + 14, policeX + 35, y + 14, ST77XX_BLACK);
  gfx.fillRoundRect(policeX + 8, y + 25, 40, 52, 8, UNIFORM);
  gfx.fillRect(policeX + 10, y + 37, 36, 5, ST77XX_WHITE);
  gfx.fillRect(policeX + 10, y + 42, 36, 5, ST77XX_RED);
  gfx.fillCircle(policeX + 28, y + 57, 7, 0xFFE0);

  // Plain pacing keeps both arms swinging. The other two actions use the right hand.
  gfx.drawLine(policeX + 11, y + 37, policeX + 2, y + 58, UNIFORM);
  gfx.fillCircle(policeX + 2, y + 59, 5, SKIN);
  if (patrolAction == 0) {
    gfx.drawLine(policeX + 45, y + 38, policeX + 55, y + 58, UNIFORM);
    gfx.fillCircle(policeX + 55, y + 59, 5, SKIN);
  } else if (patrolAction == 1) {
    // Thick black baton: the slow raised/tapping positions change independently of walking.
    if (batonRaised) {
      gfx.drawLine(policeX + 45, y + 38, policeX + 55, y + 22, UNIFORM);
      gfx.fillCircle(policeX + 55, y + 21, 5, SKIN);
      gfx.drawLine(policeX + 59, y + 6, policeX + 49, y + 30, ST77XX_BLACK);
      gfx.drawLine(policeX + 62, y + 8, policeX + 52, y + 32, ST77XX_BLACK);
      gfx.drawLine(policeX + 56, y + 7, policeX + 64, y + 10, ST77XX_BLACK);
    } else {
      gfx.drawLine(policeX + 45, y + 38, policeX + 53, y + 58, UNIFORM);
      gfx.fillCircle(policeX + 53, y + 59, 5, SKIN);
      gfx.drawLine(policeX + 51, y + 47, policeX + 48, y + 73, ST77XX_BLACK);
      gfx.drawLine(policeX + 54, y + 47, policeX + 51, y + 73, ST77XX_BLACK);
      gfx.drawLine(policeX + 44, y + 70, policeX + 54, y + 72, ST77XX_BLACK);
    }
  } else {
    // One classic key: round bow, narrow shaft, and two teeth; it turns as he walks.
    gfx.drawLine(policeX + 45, y + 38, policeX + 56, y + 48, UNIFORM);
    gfx.fillCircle(policeX + 56, y + 48, 5, SKIN);
    if (keyTurned) {
      gfx.drawCircle(policeX + 65, y + 42, 6, 0xFFE0);
      gfx.drawCircle(policeX + 65, y + 42, 3, SKY);
      gfx.drawLine(policeX + 65, y + 48, policeX + 65, y + 66, 0xFFE0);
      gfx.drawLine(policeX + 65, y + 59, policeX + 72, y + 59, 0xFFE0);
      gfx.drawLine(policeX + 65, y + 64, policeX + 70, y + 64, 0xFFE0);
    } else {
      gfx.drawCircle(policeX + 69, y + 51, 6, 0xFFE0);
      gfx.drawCircle(policeX + 69, y + 51, 3, SKY);
      gfx.drawLine(policeX + 63, y + 51, policeX + 48, y + 51, 0xFFE0);
      gfx.drawLine(policeX + 55, y + 51, policeX + 55, y + 58, 0xFFE0);
      gfx.drawLine(policeX + 50, y + 51, policeX + 50, y + 56, 0xFFE0);
    }
  }
  gfx.fillRect(policeX + 15, y + 76, 9, 20, UNIFORM);
  gfx.fillRect(policeX + 33, y + 76, 9, 20, UNIFORM);
}

void drawStreetScene() {
  // Houston-inspired downtown silhouette: warm windows and a tall central tower.
  const uint16_t building = 0x2124;
  const uint16_t window = 0xFFE0;
  const uint16_t road = 0x4208;
  const uint16_t sidewalk = 0x8410;
  gfx.fillRect(0, 82, SCREEN_W, 118, SKY);
  gfx.fillRect(5, 112, 20, 74, building);
  gfx.fillRect(29, 92, 24, 94, building);
  gfx.fillRect(57, 105, 18, 81, building);
  gfx.fillRect(79, 76, 27, 110, building);
  gfx.fillTriangle(79, 76, 92, 57, 106, 76, building);
  gfx.fillRect(110, 99, 22, 87, building);
  gfx.fillRect(136, 116, 29, 70, building);
  for (int16_t x = 9; x < 164; x += 10) {
    for (int16_t y = 102; y < 176; y += 15) {
      if ((x > 29 && x < 53) || (x > 79 && x < 106) || (x > 110 && x < 132) || (x > 136)) {
        gfx.fillRect(x, y, 3, 5, window);
      }
    }
  }
  gfx.fillRect(0, 186, SCREEN_W, 14, sidewalk);
  gfx.fillRect(0, 200, SCREEN_W, 30, road);
  gfx.drawFastHLine(0, 208, SCREEN_W, ST77XX_WHITE);
  gfx.drawFastHLine(0, 224, SCREEN_W, 0xFFE0);

  // Static parked car at the far right.
  gfx.fillRoundRect(121, 183, 42, 14, 4, 0x001F);
  gfx.fillRect(130, 178, 22, 7, 0x7DFF);
  gfx.fillCircle(131, 198, 5, ST77XX_BLACK);
  gfx.fillCircle(155, 198, 5, ST77XX_BLACK);
  gfx.fillCircle(131, 198, 2, ST77XX_WHITE);
  gfx.fillCircle(155, 198, 2, ST77XX_WHITE);

  // Street lamp at left, plus stop sign and traffic signal at the right.
  gfx.drawFastVLine(9, 141, 57, ST77XX_BLACK);
  gfx.drawFastHLine(9, 141, 13, ST77XX_BLACK);
  gfx.fillCircle(23, 145, 5, 0xFFE0);
  gfx.drawFastVLine(166, 151, 49, ST77XX_BLACK);
  gfx.fillRect(158, 145, 16, 25, ST77XX_BLACK);
  gfx.fillCircle(166, 150, 3, ST77XX_RED);
  gfx.fillCircle(166, 157, 3, 0xFFE0);
  gfx.fillCircle(166, 164, 3, ST77XX_GREEN);
  gfx.drawFastVLine(145, 160, 39, ST77XX_BLACK);
  gfx.fillCircle(145, 154, 9, ST77XX_RED);
  gfx.setTextColor(ST77XX_WHITE);
  gfx.setTextSize(1);
  gfx.setCursor(137, 151);
  gfx.print("STOP");
}

void drawScene() {
  gfx.fillScreen(SKY);
  drawPatrolLights(blueLightOn);
  gfx.setTextColor(ST77XX_WHITE);
  gfx.setTextSize(2);
  gfx.setCursor(52, 58);
  gfx.print("POLICE");
  // Plain background: no skyline or street objects behind the character.
  gfx.fillRect(0, 78, SCREEN_W, 152, SKY);
  gfx.drawFastHLine(0, 230, SCREEN_W, ST77XX_WHITE);
  drawPolice(eyesClosed);
}

// Restore only the small rectangle covered by Police's old and new positions.
// This avoids retransmitting the entire street scene for every walking frame.
void redrawPoliceSprite(int16_t oldX) {
  // There is no scene to reconstruct: clear the plain animation area and redraw Police.
  (void)oldX;
  gfx.fillRect(0, 78, SCREEN_W, 152, SKY);
  gfx.drawFastHLine(0, 230, SCREEN_W, ST77XX_WHITE);
  drawPolice(eyesClosed);
  lastRenderedPoliceX = policeX;
  return;

  const int16_t left = max<int16_t>(0, min(oldX, policeX) - 3);
  const int16_t right = min<int16_t>(SCREEN_W - 1, max(oldX, policeX) + 78);
  const int16_t width = right - left + 1;
  const int16_t top = 78;

  // Base layers of the backdrop, limited to this sprite patch.
  gfx.fillRect(left, top, width, 108, SKY);
  gfx.fillRect(left, 186, width, 14, 0x8410);
  gfx.fillRect(left, 200, width, 30, 0x4208);
  gfx.drawFastHLine(left, 208, width, ST77XX_WHITE);
  gfx.drawFastHLine(left, 224, width, 0xFFE0);
  gfx.drawFastHLine(left, 230, width, ST77XX_WHITE);

  // Repaint only background objects touched by this patch.
  const uint16_t building = 0x2124;
  const uint16_t window = 0xFFE0;
  auto touches = [left, right](int16_t objectLeft, int16_t objectRight) {
    return objectRight >= left && objectLeft <= right;
  };
  if (touches(5, 24)) gfx.fillRect(5, 112, 20, 74, building);
  if (touches(29, 52)) gfx.fillRect(29, 92, 24, 94, building);
  if (touches(57, 74)) gfx.fillRect(57, 105, 18, 81, building);
  if (touches(79, 106)) {
    gfx.fillRect(79, 76, 27, 110, building);
    gfx.fillTriangle(79, 76, 92, 57, 106, 76, building);
  }
  if (touches(110, 131)) gfx.fillRect(110, 99, 22, 87, building);
  if (touches(136, 164)) gfx.fillRect(136, 116, 29, 70, building);
  for (int16_t x = 9; x < 164; x += 10) {
    if (x < left || x + 2 > right) continue;
    for (int16_t y = 102; y < 176; y += 15) {
      if ((x > 29 && x < 53) || (x > 79 && x < 106) || (x > 110 && x < 132) || (x > 136)) {
        gfx.fillRect(x, y, 3, 5, window);
      }
    }
  }
  if (touches(121, 163)) {
    gfx.fillRoundRect(121, 183, 42, 14, 4, 0x001F);
    gfx.fillRect(130, 178, 22, 7, 0x7DFF);
    gfx.fillCircle(131, 198, 5, ST77XX_BLACK);
    gfx.fillCircle(155, 198, 5, ST77XX_BLACK);
    gfx.fillCircle(131, 198, 2, ST77XX_WHITE);
    gfx.fillCircle(155, 198, 2, ST77XX_WHITE);
  }
  if (touches(9, 27)) {
    gfx.drawFastVLine(9, 141, 57, ST77XX_BLACK);
    gfx.drawFastHLine(9, 141, 13, ST77XX_BLACK);
    gfx.fillCircle(23, 145, 5, 0xFFE0);
  }
  if (touches(137, 153)) {
    gfx.drawFastVLine(145, 160, 39, ST77XX_BLACK);
    gfx.fillCircle(145, 154, 9, ST77XX_RED);
    gfx.setTextColor(ST77XX_WHITE);
    gfx.setTextSize(1);
    gfx.setCursor(137, 151);
    gfx.print("STOP");
  }
  if (touches(158, 174)) {
    gfx.drawFastVLine(166, 151, 49, ST77XX_BLACK);
    gfx.fillRect(158, 145, 16, 25, ST77XX_BLACK);
    gfx.fillCircle(166, 150, 3, ST77XX_RED);
    gfx.fillCircle(166, 157, 3, 0xFFE0);
    gfx.fillCircle(166, 164, 3, ST77XX_GREEN);
  }
  drawPolice(eyesClosed);
  lastRenderedPoliceX = policeX;
}

void drawStatus() {
  gfx.fillRect(0, 231, SCREEN_W, 89, SKY);
  gfx.setTextSize(1);
  gfx.setTextColor(ST77XX_WHITE);
  gfx.setCursor(8, 239);
  gfx.printf("WATCH  CH %u", currentChannel);
  gfx.setCursor(8, 255);
  gfx.printf("BEACONS: %lu", static_cast<unsigned long>(beaconCount));
  gfx.setCursor(8, 271);
  gfx.printf("PROBES:  %lu", static_cast<unsigned long>(probeCount));
  gfx.setCursor(8, 287);
  const bool deauthAlertActive = lastDeauthAt != 0 && millis() - lastDeauthAt < DEAUTH_ALERT_DURATION_MS;
  gfx.setTextColor(deauthAlertActive ? ST77XX_RED : ST77XX_WHITE);
  gfx.printf("DEAUTH:  %lu", static_cast<unsigned long>(deauthCount));
  gfx.setCursor(8, 304);
  if (deauthAlertActive) {
    gfx.setTextColor(ST77XX_RED);
    gfx.print("ALERT: DEAUTH SEEN");
  } else if (sdLogLimitReached) {
    gfx.setTextColor(ST77XX_YELLOW);
    gfx.print("SD: LOG LIMIT 16MB");
  } else if (sdLogEnabled) {
    gfx.setTextColor(ST77XX_WHITE);
    gfx.printf("SD: LOG %lu KB", static_cast<unsigned long>(pcapFile.size() / 1024));
  } else {
    gfx.setTextColor(ST77XX_RED);
    gfx.print("SD: CARD NOT READY");
  }
}

void bufferRecord(uint8_t kind, const wifi_promiscuous_pkt_t *packet) {
  const uint16_t capturedLength = min<uint16_t>(packet->rx_ctrl.sig_len, PCAP_SNAPLEN);
  portENTER_CRITICAL(&captureMux);
  const uint8_t next = (bufferHead + 1) % CAPTURE_BUFFER_SIZE;
  if (next == bufferTail) {
    ++droppedRecords;
  } else {
    CaptureRecord &record = captureBuffer[bufferHead];
    record.kind = kind;
    record.rssi = packet->rx_ctrl.rssi;
    record.channel = packet->rx_ctrl.channel;
    record.length = capturedLength;
    record.timestamp = millis();
    memcpy(record.frame, packet->payload, capturedLength);
    bufferHead = next;
  }
  portEXIT_CRITICAL(&captureMux);
}

void wifiPacketCallback(void *buffer, wifi_promiscuous_pkt_type_t packetType) {
  if (packetType != WIFI_PKT_MGMT || buffer == nullptr) return;
  const wifi_promiscuous_pkt_t *packet = static_cast<const wifi_promiscuous_pkt_t *>(buffer);
  if (packet->rx_ctrl.sig_len < 2) return;
  const uint8_t *frame = packet->payload;
  const uint16_t frameControl = static_cast<uint16_t>(frame[0]) | (static_cast<uint16_t>(frame[1]) << 8);
  const uint8_t type = (frameControl >> 2) & 0x03;
  const uint8_t subtype = (frameControl >> 4) & 0x0F;
  if (type != 0) return;
  uint8_t kind = 0;
  if (subtype == 8) kind = 1;
  else if (subtype == 4) kind = 2;
  else if (subtype == 5) kind = 3;
  else if (subtype == 12) kind = 4;
  if (kind) bufferRecord(kind, packet);
}

void writeLe16(Stream &output, uint16_t value) {
  output.write(static_cast<uint8_t>(value));
  output.write(static_cast<uint8_t>(value >> 8));
}

void writeLe32(Stream &output, uint32_t value) {
  writeLe16(output, static_cast<uint16_t>(value));
  writeLe16(output, static_cast<uint16_t>(value >> 16));
}

void writePcapHeader(Stream &output) {
  writeLe32(output, 0xA1B2C3D4);
  writeLe16(output, 2);
  writeLe16(output, 4);
  writeLe32(output, 0);
  writeLe32(output, 0);
  writeLe32(output, PCAP_SNAPLEN);
  writeLe32(output, 105);
}

void writePcapRecord(Stream &output, const CaptureRecord &record) {
  const uint32_t seconds = record.timestamp / 1000;
  const uint32_t micros = (record.timestamp % 1000) * 1000;
  writeLe32(output, seconds);
  writeLe32(output, micros);
  writeLe32(output, record.length);
  writeLe32(output, record.length);
  output.write(record.frame, record.length);
}

void beginSdLog() {
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
  if (!SD_MMC.begin("/sdcard", false, false, SDMMC_FREQ_DEFAULT, 5)) return;
  if (SD_MMC.cardType() == CARD_NONE) return;
  char fileName[20];
  for (uint8_t index = 0; index < 100; ++index) {
    snprintf(fileName, sizeof(fileName), "/POLICE%02u.PCAP", index);
    if (!SD_MMC.exists(fileName)) {
      pcapFile = SD_MMC.open(fileName, FILE_WRITE);
      break;
    }
  }
  if (!pcapFile) return;
  writePcapHeader(pcapFile);
  pcapFile.flush();
  sdLogEnabled = true;
}

void logRecordToSd(const CaptureRecord &record) {
  if (!sdLogEnabled || !pcapFile) return;
  const uint32_t recordBytes = 16UL + record.length;
  if (pcapFile.size() + recordBytes > SD_LOG_LIMIT_BYTES) {
    pcapFile.flush();
    pcapFile.close();
    sdLogEnabled = false;
    sdLogLimitReached = true;
    return;
  }
  writePcapRecord(pcapFile, record);
}

void drainCaptureBuffer() {
  CaptureRecord record;
  bool available = false;
  portENTER_CRITICAL(&captureMux);
  if (bufferTail != bufferHead) {
    record = captureBuffer[bufferTail];
    bufferTail = (bufferTail + 1) % CAPTURE_BUFFER_SIZE;
    available = true;
  }
  portEXIT_CRITICAL(&captureMux);
  if (!available) return;
  if (record.kind == 1) ++beaconCount;
  else if (record.kind == 2 || record.kind == 3) ++probeCount;
  else if (record.kind == 4) {
    ++deauthCount;
    lastDeauthAt = millis();
  }
  if (!usbPcapHeaderSent) {
    writePcapHeader(Serial);
    usbPcapHeaderSent = true;
  }
  writePcapRecord(Serial, record);
  logRecordToSd(record);
}

void hopChannel() {
  currentChannel = (currentChannel >= LAST_CHANNEL) ? FIRST_CHANNEL : currentChannel + 1;
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
}

void startPassiveMonitor() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(&wifiPacketCallback);
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(true);
}

void setup() {
  Serial.begin(115200);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  rgbLed.begin();
  rgbLed.setBrightness(80);
  updateRgbAlert(millis());
  displaySPI.begin(LCD_SCLK, -1, LCD_MOSI, LCD_CS);
  gfx.init(SCREEN_W, SCREEN_H);
  // A faster display link shortens each partial repaint and makes tearing much less visible.
  gfx.setSPISpeed(80000000);
  gfx.setRotation(0);
  gfx.setTextWrap(false);
  drawScene();
  beginSdLog();
  startPassiveMonitor();
  const uint32_t now = millis();
  lastBlinkAt = now;
  lastPatrolStepAt = now;
  lastBatonSwingAt = now;
  lastKeySpinAt = now;
  lastChannelHopAt = now;
  lastDisplayStatusAt = now;
  lastSdFlushAt = now;
}

void loop() {
  const uint32_t now = millis();
  // Several timers may change in one pass. Repaint the street once after all changes.
  bool patrolNeedsRedraw = false;
  const bool buttonNow = digitalRead(BUTTON_PIN);
  if (previousButton == HIGH && buttonNow == LOW && !sirenActive) {
    sirenActive = true;
    sirenStartedAt = now;
    lastSirenFlashAt = now;
    blueLightOn = false;
    drawPatrolLights(blueLightOn);
  }
  previousButton = buttonNow;
  if (sirenActive && now - lastSirenFlashAt >= SIREN_FLASH_MS) {
    blueLightOn = !blueLightOn;
    lastSirenFlashAt = now;
    drawPatrolLights(blueLightOn);
  }
  if (sirenActive && now - sirenStartedAt >= SIREN_DURATION_MS) {
    sirenActive = false;
    blueLightOn = true;
    drawPatrolLights(blueLightOn);
  }
  if (now - lastPatrolStepAt >= PATROL_STEP_MS) {
    lastPatrolStepAt = now;
    policeX += patrolDirection * 3;
    if (policeX >= 112 || policeX <= 5) {
      policeX = constrain(policeX, 5, 112);
      patrolDirection = -patrolDirection;
      patrolAction = (patrolAction + 1) % 3;
    }
    patrolNeedsRedraw = true;
  }
  if (patrolAction == 1 && now - lastBatonSwingAt >= BATON_SWING_MS) {
    lastBatonSwingAt = now;
    batonRaised = !batonRaised;
    patrolNeedsRedraw = true;
  }
  if (patrolAction == 2 && now - lastKeySpinAt >= KEY_SPIN_MS) {
    lastKeySpinAt = now;
    keyTurned = !keyTurned;
    patrolNeedsRedraw = true;
  }
  if (!eyesClosed && now - lastBlinkAt >= BLINK_INTERVAL_MS) {
    eyesClosed = true;
    blinkStartedAt = now;
    patrolNeedsRedraw = true;
  }
  if (eyesClosed && now - blinkStartedAt >= BLINK_LENGTH_MS) {
    eyesClosed = false;
    lastBlinkAt = now;
    patrolNeedsRedraw = true;
  }
  if (patrolNeedsRedraw) {
    redrawPoliceSprite(lastRenderedPoliceX);
  }
  drainCaptureBuffer();
  if (sdLogEnabled && now - lastSdFlushAt >= SD_FLUSH_MS) {
    pcapFile.flush();
    lastSdFlushAt = now;
  }
  updateRgbAlert(now);
  if (now - lastChannelHopAt >= CHANNEL_HOP_MS) {
    lastChannelHopAt = now;
    hopChannel();
  }
  if (now - lastDisplayStatusAt >= DISPLAY_STATUS_MS) {
    lastDisplayStatusAt = now;
    drawStatus();
  }
}