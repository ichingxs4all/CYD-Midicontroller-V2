/*******************************************************************
 MIDI Controller Main Launcher for ESP32 Cheap Yellow Display
 Main file - handles setup, menu, and mode switching
 *******************************************************************/
#include <Arduino.h>
#include "EEPROM.h"
#include "version.h"

#define EEPROM_SIZE 81   // 77 base + 4 CV output bytes (77-80)

#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <TFT_eSPI.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#ifdef ENABLE_WIFI
#include <esp_bt.h>   // needed for esp_bt_controller_mem_release
#endif

// Include mode files
#ifdef ENABLE_WIFI
#include <WiFi.h>
#endif
#include "settings_mode.h"
#ifdef ENABLE_WIFI
#include "wifi_applemidi.h"
#include "wifi_settings_mode.h"
#endif
#include "keyboard_mode.h"
#include "sequencer_mode.h"
#include "bouncing_ball_mode.h"
#include "physics_drop_mode.h"
#include "random_generator_mode.h"
#include "xy_pad_mode.h"
#include "arpeggiator_mode.h"
#include "grid_piano_mode.h"
#include "auto_chord_mode.h"
#include "lfo_mode.h"
#include "ui_elements.h"
#include "midi_utils.h"
#include "monitor_mode.h"
#include "midi_clock_mode.h"
#include "pgmchange_mode.h"
#include "sd_card.h"
#include "sd_menu_mode.h"
#include "sd_seq_mode.h"
#include "sd_player_mode.h"
#include "sd_recorder_mode.h"
#include "sd_setlist_mode.h"
#include "sd_info_mode.h"
#include "sd_sysex_mode.h"
#include "sd_filetransfer_mode.h"
#include "slider_mode.h"
#include "cv_output.h"

// Hardware setup
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// Global objects
SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

// BLE MIDI globals
BLECharacteristic *pCharacteristic;
bool deviceConnected = false;
uint8_t midiPacket[] = {0x80, 0x80, 0x00, 0x60, 0x7F};

// MIDI-OUT (DIN) on UART2
HardwareSerial MIDISerial(2); // UART2 = CN1 pin 22 and 27

// Touch state
TouchState touch;

// App state
AppMode currentMode = MENU;

// Forward declarations
// Mode forward declarations are in common_definitions.h

// Scalable App Icon System
// To add new apps:
// 1. Add new mode to AppMode enum in common_definitions.h
// 2. Create mode header file (e.g., new_mode.h)
// 3. Include header in this file
// 4. Add to initialization, loop, and enterMode switch statements
// 5. Add entry to apps[] array below
// 6. Add graphics case to drawAppGraphics() function
// 7. Increment numApps
struct AppIcon {
  String name;
  String symbol;
  uint16_t color;
  AppMode mode;
};

#define MAX_APPS 12  // Can easily expand to 3x4 grid
AppIcon apps[] = {
  {"KEYS", "♪", 0xF800, KEYBOARD},     // Red
  {"BEATS", "♫", 0xFD00, SEQUENCER},   // Orange
  {"ZEN", "●", 0xFFE0, BOUNCING_BALL}, // Yellow
  {"DROP", "⬇", 0x07E0, PHYSICS_DROP}, // Green
  {"RNG", "※", 0x001F, RANDOM_GENERATOR}, // Blue
  {"XY PAD", "◈", 0x781F, XY_PAD},     // Purple
  {"ARP", "↗", 0xF81F, ARPEGGIATOR},   // Magenta
  {"GRID", "▣", 0x07FF, GRID_PIANO},   // Cyan
  {"CHORD", "⚘", 0xFBE0, AUTO_CHORD},  // Light Orange
  {"LFO", "", 0xAFE5, LFO},           // Light Green
  {"MONITOR", "⊙", 0x7BEF, MONITOR},
  {"CLOCK",   "♩", 0xF81F, MIDI_CLOCK_MODE},
  {"PGMCH",   "",  0x04D2, PGMCHANGE_MODE},
  {"SLIDERS", "≡", 0x867F, SLIDER_MODE},   // slate blue
  {"FILES",   "▣", 0xC618, SD_MENU_MODE}
};

int numApps = 15;

class MIDICallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      if (currentMode == MENU) {
        drawMenu(); // Redraw menu to clear "BLE WAITING..."
      }
      updateStatus();
    }
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      if (currentMode == MENU) {
        drawMenu(); // Redraw menu to show "BLE WAITING..."
      }
      updateStatus();
      // All Sound Off + All Notes Off on DIN for melody and drum channels
      byte mc = 0xB0 | (byte)(channel - 1);
      byte dc = 0xB0 | (byte)(drumChannel - 1);
      MIDISerial.write(mc); MIDISerial.write(120); MIDISerial.write(0);
      MIDISerial.write(mc); MIDISerial.write(123); MIDISerial.write(0);
      if (drumChannel != channel) {
        MIDISerial.write(dc); MIDISerial.write(120); MIDISerial.write(0);
        MIDISerial.write(dc); MIDISerial.write(123); MIDISerial.write(0);
      }
      // Restart advertising so new connections can be made
      BLEDevice::startAdvertising();
    }
};

// sysexReceiveByte forward declared in common_definitions.h

// ------------------------------------------------------------------
//  Unified BLE MIDI receive dispatcher
//  Routes incoming BLE MIDI bytes to whichever modes need them.
//  Defined here (after all mode headers) so it can call into any mode.
// ------------------------------------------------------------------
class BLEMidiReceiveCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) {
    uint8_t* data = pChar->getData();
    size_t   len  = pChar->getLength();
    if (len < 3) return;

    // BLE→DIN bridge: copy the raw MIDI bytes (skip BLE-MIDI header/timestamps)
    // and BLE MIDI Thru: echo back on BLE
    // We parse the packet to extract clean status+data bytes for both purposes.

    size_t i = 2;
    byte   runningStatus  = 0;
    bool   expectingStatus = true;

    while (i < len) {
      byte b = data[i];

      // Mid-packet timestamp detection (peek-ahead)
      if (expectingStatus && b >= 0x80 && b <= 0xBF) {
        if (i + 1 < len && (data[i+1] & 0x80)) { i++; continue; }
      }

      // Real-time single-byte
      if (b >= 0xF8) {
        monitorPushEvent(b, 0, 0);  // recorder called inside monitorPushEvent
        clockReceiveByte(b);
        if (ble2serial) MIDISerial.write(b);
        if (midiThru)   { uint8_t p[3] = {data[0], data[1], b}; pCharacteristic->setValue(p, 3); pCharacteristic->notify(); }
        i++;
        continue;
      }

      if (b & 0x80) {
        // Check for sysex start/continuation
        if (b == 0xF0 || (sysexRxActive && b != 0xF7 && b < 0xF8)) {
          sysexReceiveByte(b);
          if (ble2serial) MIDISerial.write(b);
          i++;
          continue;
        }
        if (b == 0xF7) {
          sysexReceiveByte(b);
          if (ble2serial) MIDISerial.write(b);
          i++;
          continue;
        }
        runningStatus  = b;
        expectingStatus = false;
        i++;
        continue;
      }

      if (runningStatus == 0) { i++; continue; }

      byte cmd = runningStatus & 0xF0;

      if (cmd == 0xC0 || cmd == 0xD0 || runningStatus == 0xF3) {
        // Two-byte message
        monitorPushEvent(runningStatus, b, 0);
        if (ble2serial) { MIDISerial.write(runningStatus); MIDISerial.write(b); }
        if (midiThru)   { uint8_t p[4] = {data[0], data[1], runningStatus, b}; pCharacteristic->setValue(p, 4); pCharacteristic->notify(); }
        expectingStatus = true;
        i++;
        continue;
      }

      // Three-byte message
      if (i + 1 < len && !(data[i+1] & 0x80)) {
        monitorPushEvent(runningStatus, b, data[i+1]);
        if (ble2serial) { MIDISerial.write(runningStatus); MIDISerial.write(b); MIDISerial.write(data[i+1]); }
        if (midiThru)   { uint8_t p[5] = {data[0], data[1], runningStatus, b, data[i+1]}; pCharacteristic->setValue(p, 5); pCharacteristic->notify(); }
        i += 2;
      } else {
        monitorPushEvent(runningStatus, b, 0);
        if (ble2serial) { MIDISerial.write(runningStatus); MIDISerial.write(b); MIDISerial.write(0); }
        if (midiThru)   { uint8_t p[5] = {data[0], data[1], runningStatus, b, 0}; pCharacteristic->setValue(p, 5); pCharacteristic->notify(); }
        i++;
      }
      expectingStatus = true;
    }
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("[BOOT] Starting up...");

#ifdef ENABLE_WIFI
  // Free Classic BT memory — we only use BLE, not Classic BT.
  // This recovers ~30KB of heap that WiFi needs for its timer subsystem.
  // Must be called before BLEDevice::init().
  Serial.println("[BOOT] Releasing Classic BT memory...");
  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  Serial.printf("[BOOT] Free heap after BT mem release: %d bytes\n", ESP.getFreeHeap());
#endif

  if (!EEPROM.begin(EEPROM_SIZE)) Serial.println("failed to initialize EEPROM");
  if (EEPROM.read(0) != 130) initEeprom();
  channel      = EEPROM.read(1);
  gateLength   = EEPROM.read(2);
  drumChannel  = EEPROM.read(4);
  drumNotes[0] = EEPROM.read(5);
  drumNotes[1] = EEPROM.read(6);
  drumNotes[2] = EEPROM.read(7);
  drumNotes[3] = EEPROM.read(8);
  ble2serial   = EEPROM.read(9)  == 1;
  midiThru     = EEPROM.read(10) == 1;
#ifdef ENABLE_WIFI
  loadWifiSettings();  // load WiFi SSID, password, AppleMIDI flag
#endif
  cvLoadSettings();    // load CV output settings (bytes 77-80)

  // Touch setup
  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(mySpi);
  ts.setRotation(1);
  
  // Display setup
  tft.init();
  tft.setRotation(1);
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);

    // --- DIN MIDI OUT init on UART2 ---
  // Use RX=16, TX=17 (or change to your actual TX pin and comment)
  //MIDISerial.begin(31250, SERIAL_8N1, 16, 17); // RX=16, TX=17
  
  MIDISerial.begin(31250, SERIAL_8N1,22,27); //RX = 22 , TX = 27

  // BLE MIDI Setup
  Serial.println("Initializing BLE MIDI...");
  BLEDevice::init("CYD MIDI");
  Serial.println("BLE Device initialized");
  
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new MIDICallbacks());
  Serial.println("BLE Server created");
  
  BLEService *service = server->createService(BLEUUID(SERVICE_UUID));
  Serial.println("BLE Service created");
  
  pCharacteristic = service->createCharacteristic(
    BLEUUID(CHARACTERISTIC_UUID),
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE_NR |  // BLE MIDI spec: Write Without Response
    BLECharacteristic::PROPERTY_NOTIFY
  );
  
  pCharacteristic->addDescriptor(new BLE2902());
  // Unified BLE MIDI receive dispatcher – defined below after all mode headers
  pCharacteristic->setCallbacks(new BLEMidiReceiveCallbacks());
  service->start();
  Serial.println("BLE Service started");
  
  BLEAdvertising *advertising = server->getAdvertising();
  advertising->addServiceUUID(service->getUUID());
  BLEAdvertisementData adData;
  adData.setName("CYD MIDI");
  adData.setCompleteServices(BLEUUID(SERVICE_UUID));
  advertising->setAdvertisementData(adData);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMaxPreferred(0x12);
  advertising->start();
  Serial.println("BLE Advertising started - Device discoverable as 'CYD MIDI'");
  
  // Initialize mode systems
  initializeKeyboardMode();
  initializeSequencerMode();
  initializeBouncingBallMode();
  initializeRandomGeneratorMode();
  initializeXYPadMode();
  initializeArpeggiatorMode();
  initializeGridPianoMode();
  initializeAutoChordMode();
  initializeLFOMode();
  initializeSettingsMode();
#ifdef ENABLE_WIFI
  initializeWifiSettingsMode();
  initWifiAppleMidi();
#endif
  initializeMonitorMode();
  initializeMidiClockMode();
  initializePgmChangeMode();
  initializeSDMenuMode();
  initializeSDSeqMode();
  initializeSDPlayerMode();
  initializeSDRecorderMode();
  initializeSDSetlistMode();
  initializeSDInfoMode();
  initializeSDSysexMode();
  initializeSDFiletransferMode();
  initializeSliderMode();
  initializeCVOutput();   // MCP4728 on I2C (GPIO21 SDA / GPIO22 SCL)

  drawMenu();
  updateStatus();
  Serial.println("MIDI Controller ready!");
}

void loop() {
  updateTouch();
#ifdef ENABLE_WIFI
  appleMidiTick();  // service AppleMIDI/RTP-MIDI stack
#endif

  // ------------------------------------------------------------------
  //  Central DIN MIDI reader — runs every tick regardless of mode.
  //  Feeds: monitor parser, clock slave, DIN Thru, DIN→BLE bridge.
  // ------------------------------------------------------------------
  while (MIDISerial.available()) {
    byte b = (byte)MIDISerial.read();

    monitorParseDIN(b);   // feeds monitor, clock, and recorder via monitorPushEvent
    clockReceiveByte(b);  // always feed clock slave
    sysexReceiveByte(b);  // feed sysex assembler (active only during F0..F7)

    // DIN MIDI Thru: echo back out on DIN
    if (midiThru) MIDISerial.write(b);

    // DIN→BLE bridge: wrap in a 3-byte BLE-MIDI packet
    if (ble2serial && deviceConnected) {
      unsigned long now = millis();
      uint8_t hdr = 0x80 | ((now >> 7) & 0x3F);
      uint8_t ts  = 0x80 | (now & 0x7F);
      uint8_t pkt[3] = { hdr, ts, b };
      pCharacteristic->setValue(pkt, 3);
      pCharacteristic->notify();
    }
  }

  switch (currentMode) {
    case MENU:
      if (touch.justPressed) handleMenuTouch();
      break;
    case KEYBOARD:
      handleKeyboardMode();
      break;
    case SEQUENCER:
      handleSequencerMode();
      break;
    case BOUNCING_BALL:
      handleBouncingBallMode();
      break;
    case PHYSICS_DROP:
      handlePhysicsDropMode();
      break;
    case RANDOM_GENERATOR:
      handleRandomGeneratorMode();
      break;
    case XY_PAD:
      handleXYPadMode();
      break;
    case ARPEGGIATOR:
      handleArpeggiatorMode();
      break;
    case GRID_PIANO:
      handleGridPianoMode();
      break;
    case AUTO_CHORD:
      handleAutoChordMode();
      break;
    case LFO:
      handleLFOMode();
      break;
    case SETTINGS:
      handleSettingsMode();
      break;
    case MONITOR:
      handleMonitorMode();
      break;
    case MIDI_CLOCK_MODE:
      handleMidiClockMode();
      break;
    case PGMCHANGE_MODE:
      handlePgmChangeMode();
      break;
    case SD_MENU_MODE:
      handleSDMenuMode();
      break;
    case SD_SEQ_MODE:
      handleSDSeqMode();
      break;
    case SD_PLAYER_MODE:
      handleSDPlayerMode();
      break;
    case SD_RECORDER_MODE:
      handleSDRecorderMode();
      break;
    case SD_SETLIST_MODE:
      handleSDSetlistMode();
      break;
    case SD_INFO_MODE:
      handleSDInfoMode();
      break;
    case SD_SYSEX_MODE:
      handleSDSysexMode();
      break;
    case SD_FILETRANSFER_MODE:
      handleSDFiletransferMode();
      break;
    case SLIDER_MODE:
      handleSliderMode();
      break;
  }

  delay(10);  // 10ms: 2× faster than original (better MIDI timing) without starving ESP32 WiFi/LWIP
}

void drawGearIcon(int cx, int cy, int outerR, int innerR, int teeth, uint16_t color) {
  // Filled inner circle
  tft.fillCircle(cx, cy, innerR - 2, color);
  // Centre hole
  tft.fillCircle(cx, cy, innerR / 2, THEME_SURFACE);

  // Teeth: draw filled rectangles radiating outward around the circle
  for (int t = 0; t < teeth; t++) {
    float angle     = (2.0f * PI * t) / teeth;
    float angleNext = (2.0f * PI * (t + 0.5f)) / teeth;

    // Two points at inner radius, two at outer radius
    float cos0 = cos(angle - 0.2f),  sin0 = sin(angle - 0.2f);
    float cos1 = cos(angle + 0.2f),  sin1 = sin(angle + 0.2f);

    int x0 = cx + (int)(innerR * cos0);
    int y0 = cy + (int)(innerR * sin0);
    int x1 = cx + (int)(innerR * cos1);
    int y1 = cy + (int)(innerR * sin1);
    int x2 = cx + (int)(outerR * cos1);
    int y2 = cy + (int)(outerR * sin1);
    int x3 = cx + (int)(outerR * cos0);
    int y3 = cy + (int)(outerR * sin0);

    // Draw tooth as two triangles
    tft.fillTriangle(x0, y0, x1, y1, x2, y2, color);
    tft.fillTriangle(x0, y0, x2, y2, x3, y3, color);
  }
}

void drawMenu() {
  tft.fillScreen(THEME_BG);

  // Header bar
  tft.fillRect(0, 0, 320, 48, THEME_SURFACE);
  tft.drawFastHLine(0, 48, 320, THEME_PRIMARY);

  // Gear button (top-right, 38x38)
  int gearX  = 276;
  int gearY  = 5;
  int gearW  = 38;
  int gearH  = 38;
  tft.fillRoundRect(gearX, gearY, gearW, gearH, 6, THEME_BG);
  drawGearIcon(gearX + gearW / 2, gearY + gearH / 2, 14, 9, 8, THEME_TEXT_DIM);

  // Row 1: title centred in the space left of the gear
  tft.setTextColor(THEME_PRIMARY, THEME_SURFACE);
  tft.drawCentreString("MIDI CONTROLLER", 135, 5, 4);

  // Row 2: connection status — BLE on left, WiFi middle, SD right
  tft.fillRect(0, 29, 268, 17, THEME_SURFACE);  // clear status area

  // BLE status
  if (deviceConnected) {
    tft.setTextColor(THEME_SUCCESS, THEME_SURFACE);
    tft.drawString("● BLE", 16, 32, 1);
  } else {
    tft.setTextColor(THEME_ERROR, THEME_SURFACE);
    tft.drawString("○ BLE", 16, 32, 1);
  }

#ifdef ENABLE_WIFI
  // AppleMIDI / WiFi status (middle)
  if (appleMidiRunning) {
    tft.setTextColor(THEME_ACCENT, THEME_SURFACE);
    tft.drawCentreString("♪ NET: " + wifiIPText, 135, 32, 1);
  } else if (wifiConnected) {
    tft.setTextColor(THEME_WARNING, THEME_SURFACE);
    tft.drawCentreString("WiFi: " + wifiIPText, 135, 32, 1);
  } else if (appleMidiEnabled) {
    tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
    tft.drawCentreString("WiFi off", 135, 32, 1);
  }
#endif

  // SD card status (right side, before gear)
  if (sdMounted) {
    tft.setTextColor(THEME_SUCCESS, THEME_SURFACE);
    tft.drawString("● SD", 222, 32, 1);
  } else {
    tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
    tft.drawString("○ SD", 222, 32, 1);
  }

  // Dynamic grid layout - 5 icons per row
  int iconSize = 40;
  int spacing = 8;  // Slightly smaller spacing for 5 columns
  int cols = 5;  // Always 5 icons per row
  int rows = (numApps + cols - 1) / cols;  // Calculate needed rows
  int startX = (320 - (cols * iconSize + (cols-1) * spacing)) / 2;
  int startY = 54;

  for (int i = 0; i < numApps; i++) {
    int col = i % cols;
    int row = i / cols;
    int x = startX + col * (iconSize + spacing);
    int y = startY + row * (iconSize + spacing + 15);
    
    // App icon background
    uint16_t iconColor = apps[i].color;
    
    tft.fillRoundRect(x, y, iconSize, iconSize, 8, iconColor);
    tft.drawRoundRect(x, y, iconSize, iconSize, 8, THEME_TEXT);
    
    // Draw app-specific graphics
    drawAppGraphics(apps[i].mode, x, y, iconSize);
    
    // Icon symbol
    tft.setTextColor(THEME_BG, iconColor);
    tft.drawCentreString(apps[i].symbol, x + iconSize/2, y + iconSize/2 - 8, 2);
    
    // App name
    tft.setTextColor(THEME_TEXT, THEME_BG);
    tft.drawCentreString(apps[i].name, x + iconSize/2, y + iconSize + 5, 1);
  }
}

void drawAppGraphics(AppMode mode, int x, int y, int iconSize) {
  switch (mode) {
    case KEYBOARD: // KEYS - piano keys
      {
        int keyWidth = 4;
        int totalWidth = 5 * keyWidth + 4 * 1; // 5 keys + 4 gaps
        int startX = x + (iconSize - totalWidth) / 2;
        for (int i = 0; i < 5; i++) {
          tft.fillRect(startX + i*5, y + iconSize/2 - 6, keyWidth, 12, THEME_BG);
        }
      }
      break;
    case SEQUENCER: // BEATS - grid pattern
      {
        int gridW = 4, gridH = 4, gapX = 2, gapY = 2;
        int totalW = 4 * gridW + 3 * gapX;
        int totalH = 3 * gridH + 2 * gapY;
        int startX = x + (iconSize - totalW) / 2;
        int startY = y + (iconSize - totalH) / 2;
        for (int r = 0; r < 3; r++) {
          for (int c = 0; c < 4; c++) {
            tft.fillRect(startX + c*(gridW+gapX), startY + r*(gridH+gapY), gridW, gridH, THEME_BG);
          }
        }
      }
      break;
    case BOUNCING_BALL: // ZEN - circle with dots
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        tft.drawCircle(centerX, centerY, 12, THEME_BG);
        tft.fillCircle(centerX - 6, centerY - 4, 2, THEME_BG);
        tft.fillCircle(centerX + 5, centerY + 2, 2, THEME_BG);
        tft.fillCircle(centerX - 2, centerY + 6, 2, THEME_BG);
      }
      break;
    case PHYSICS_DROP: // DROP - balls falling on platforms
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        // Draw platforms
        tft.fillRect(centerX - 10, centerY + 8, 8, 2, THEME_BG);
        tft.fillRect(centerX + 4, centerY + 4, 6, 2, THEME_BG);
        // Draw falling balls
        tft.fillCircle(centerX - 6, centerY - 8, 2, THEME_BG);
        tft.fillCircle(centerX + 2, centerY - 4, 2, THEME_BG);
        tft.fillCircle(centerX + 8, centerY, 2, THEME_BG);
      }
      break;
    case RANDOM_GENERATOR: // RNG - random dots
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        tft.fillCircle(centerX - 8, centerY - 6, 2, THEME_BG);
        tft.fillCircle(centerX - 1, centerY - 3, 2, THEME_BG);
        tft.fillCircle(centerX + 7, centerY + 1, 2, THEME_BG);
        tft.fillCircle(centerX - 4, centerY + 6, 2, THEME_BG);
      }
      break;
    case XY_PAD: // XY PAD - crosshairs
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        int crossSize = 14;
        tft.drawFastHLine(centerX - crossSize/2, centerY, crossSize, THEME_BG);
        tft.drawFastVLine(centerX, centerY - crossSize/2, crossSize, THEME_BG);
        tft.fillCircle(centerX, centerY, 3, THEME_BG);
      }
      break;
    case ARPEGGIATOR: // ARP - ascending notes
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        for (int i = 0; i < 4; i++) {
          tft.fillCircle(centerX - 7 + i*5, centerY + 5 - i*3, 2, THEME_BG);
        }
      }
      break;
    case GRID_PIANO: // GRID - grid pattern
      {
        int cellW = 5, cellH = 4, gapX = 1, gapY = 2;
        int totalW = 4 * cellW + 3 * gapX;
        int totalH = 3 * cellH + 2 * gapY;
        int startX = x + (iconSize - totalW) / 2;
        int startY = y + (iconSize - totalH) / 2;
        for (int r = 0; r < 3; r++) {
          for (int c = 0; c < 4; c++) {
            tft.drawRect(startX + c*(cellW+gapX), startY + r*(cellH+gapY), cellW, cellH, THEME_BG);
          }
        }
      }
      break;
    case AUTO_CHORD: // CHORD - stacked notes
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        int lineWidth = 14;
        tft.fillRect(centerX - lineWidth/2, centerY + 4, lineWidth, 2, THEME_BG);
        tft.fillRect(centerX - lineWidth/2, centerY, lineWidth, 2, THEME_BG);
        tft.fillRect(centerX - lineWidth/2, centerY - 4, lineWidth, 2, THEME_BG);
      }
      break;
    case LFO: // LFO - simple sine wave line
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        
        // Draw sine wave as connected line segments
        int lastX = centerX - 15;
        int lastY = centerY;
        
        for (int i = 1; i <= 15; i++) {
          int px = centerX - 15 + i * 2;
          float angle = (i * 3.14159) / 4.0; // One and a half cycles
          int py = centerY + (int)(6 * sin(angle));
          
          // Draw line from last point to current point
          tft.drawLine(lastX, lastY, px, py, THEME_BG);
          
          lastX = px;
          lastY = py;
        }
      }
      break;
      case SETTINGS: // SETTINGS
      {
        int centerX = x + iconSize/2;
        int centerY = y + iconSize/2;
        
        tft.drawFastHLine(x + 10, y + 10, 20, THEME_BG);
        tft.drawFastHLine(x + 10 ,y + 15, 20, THEME_BG);
        tft.drawFastHLine(x + 10 ,y + 20, 20, THEME_BG); 
      }
      break;

      case MONITOR: // MONITOR – mini table rows with a dot indicator
      {
        int bx = x + 6;
        int by = y + 10;
        for (int r = 0; r < 3; r++) {
          tft.drawFastHLine(bx,      by + r * 8, 6,  THEME_BG);
          tft.drawFastHLine(bx + 9,  by + r * 8, 12, THEME_BG);
          tft.drawFastHLine(bx + 24, by + r * 8, 8,  THEME_BG);
        }
        tft.fillCircle(x + iconSize - 8, y + 8, 3, THEME_BG);
      }
      break;
      case MIDI_CLOCK_MODE: // CLOCK – four beat dots + pulse line
      {
        int centerY = y + iconSize / 2 - 4;
        int spacing = 9;
        int startX  = x + 4;
        for (int b = 0; b < 4; b++) {
          tft.fillCircle(startX + b * spacing, centerY, 3, THEME_BG);
        }
        tft.drawFastHLine(x + 4, centerY + 8, iconSize - 8, THEME_BG);
      }
      break;
      case PGMCHANGE_MODE: // PGMCH – "123" digits
      {
        int cx = x + iconSize / 2;
        int cy = y + iconSize / 2 - 8;
        tft.setTextColor(THEME_BG, 0x04D2);
        tft.drawCentreString("123", cx, cy, 2);
      }
      break;
      case SD_MENU_MODE: // SD card outline shape
      {
        int cx = x + iconSize/2, cy = y + iconSize/2;
        tft.fillRoundRect(cx-10, cy-13, 20, 26, 2, THEME_BG);
        tft.fillTriangle(cx-10, cy-13, cx-4, cy-13, cx-10, cy-7, 0xC618);
        tft.drawFastHLine(cx-7, cy-4, 14, 0xC618);
        tft.drawFastHLine(cx-7, cy,   14, 0xC618);
      }
      break;
      case SLIDER_MODE: // 6 slider tracks with thumb marks at staggered positions
      {
        int sx = x + 4, sw = iconSize - 8;
        int thumbPositions[] = {20, 45, 60, 30, 75, 50};  // % across track
        for (int s = 0; s < 6; s++) {
          int sy = y + 4 + s * 6;
          tft.drawFastHLine(sx, sy, sw, THEME_BG);
          int thumbX = sx + (sw * thumbPositions[s]) / 100;
          tft.fillRect(thumbX - 1, sy - 2, 3, 5, THEME_BG);
        }
      }
      break;
  }
}

void handleMenuTouch() {
  // Gear / settings button in header
  if (isButtonPressed(276, 5, 38, 38)) {
    enterMode(SETTINGS);
    return;
  }

  int iconSize = 40;
  int spacing = 8;  // Matching the drawMenu spacing
  int cols = 5;     // Always 5 icons per row
  int startX = (320 - (cols * iconSize + (cols-1) * spacing)) / 2;
  int startY = 54;

  for (int i = 0; i < numApps; i++) {
    int col = i % cols;
    int row = i / cols;
    int x = startX + col * (iconSize + spacing);
    int y = startY + row * (iconSize + spacing + 15);
    
    if (isButtonPressed(x, y, iconSize, iconSize)) {
      enterMode(apps[i].mode);
      return;
    }
  }
}

void enterMode(AppMode mode) {
  currentMode = mode;
  switch (mode) {
    case KEYBOARD:
      drawKeyboardMode();
      break;
    case SEQUENCER:
      drawSequencerMode();
      break;
    case BOUNCING_BALL:
      drawBouncingBallMode();
      break;
    case PHYSICS_DROP:
      drawPhysicsDropMode();
      break;
    case RANDOM_GENERATOR:
      drawRandomGeneratorMode();
      break;
    case XY_PAD:
      drawXYPadMode();
      break;
    case ARPEGGIATOR:
      drawArpeggiatorMode();
      break;
    case GRID_PIANO:
      drawGridPianoMode();
      break;
    case AUTO_CHORD:
      drawAutoChordMode();
      break;
    case LFO:
      drawLFOMode();
      break;
    case SETTINGS:
      drawSettingsMode();
      break;
    case MONITOR:
      drawMonitorMode();
      break;
    case MIDI_CLOCK_MODE:
      drawMidiClockMode();
      break;
    case PGMCHANGE_MODE:
      drawPgmChangeMode();
      break;
    case SD_MENU_MODE:
      drawSDMenuMode();
      break;
    case SD_SEQ_MODE:
      drawSDSeqMode();
      break;
    case SD_PLAYER_MODE:
      drawSDPlayerMode();
      break;
    case SD_RECORDER_MODE:
      drawSDRecorderMode();
      break;
    case SD_SETLIST_MODE:
      drawSDSetlistMode();
      break;
    case SD_INFO_MODE:
      drawSDInfoMode();
      break;
    case SD_SYSEX_MODE:
      sdInit();
      sysexRefreshList();
      drawSDSysexMode();
      break;
    case SD_FILETRANSFER_MODE:
      sdInit();
      drawSDFiletransferMode();
      break;
    case SLIDER_MODE:
      drawSliderMode();
      break;
  }
  updateStatus();
}

void exitToMenu() {
  currentMode = MENU;
  stopAllModes();
  delay(50);
  drawMenu();
  updateStatus();
}