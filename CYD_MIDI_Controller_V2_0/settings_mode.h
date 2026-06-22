#ifndef SETTINGS_MODE_H
#define SETTINGS_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"
#include "version.h"

// ------------------------------------------------------------------
//  Drum settings – shared with sequencer_mode.h
//  GM defaults: Kick=36, Snare=38, Hi-hat=42, Open Hi-hat=46, CH=10
// ------------------------------------------------------------------
int drumChannel  = 10;
int drumNotes[4] = {36, 38, 42, 46};  // KICK, SNRE, HHAT, OPEN

// ------------------------------------------------------------------
//  MIDI routing flags
// ------------------------------------------------------------------
bool ble2serial = false;   // bridge BLE↔DIN in both directions
bool midiThru   = false;   // echo received messages back out the same port

// ------------------------------------------------------------------
//  Settings sub-menu state
// ------------------------------------------------------------------
bool inDrumSubMenu  = false;
bool inCvSubMenu    = false;
#ifdef ENABLE_WIFI
bool inWifiSubMenu  = false;
#endif

// ------------------------------------------------------------------
//  EEPROM layout
//  0     = magic (130)
//  1     = channel
//  2     = gateLength
//  3     = reserved
//  4     = drumChannel
//  5..8  = drumNotes[0..3]
//  9     = ble2serial
//  10    = midiThru
//  11..42 = WiFi SSID (32 bytes)
//  43..74 = WiFi Password (32 bytes)
//  75    = appleMidiEnabled
//  76    = wifiAutoConnect
// ------------------------------------------------------------------

// Function declarations
void initializeSettingsMode();
void drawSettingsMode();
void handleSettingsMode();
void drawDrumSubMenu();
void handleDrumSubMenu();
void storeSetting();
void initEeprom();

// ------------------------------------------------------------------
//  Init
// ------------------------------------------------------------------
void initializeSettingsMode() {
  inDrumSubMenu = false;
  inCvSubMenu   = false;
#ifdef ENABLE_WIFI
  inWifiSubMenu = false;
#endif
}

// ------------------------------------------------------------------
//  Draw helpers
// ------------------------------------------------------------------
#define DRUM_ROW_LABEL_X    10
#define DRUM_ROW_NAME_X     90
#define DRUM_ROW_NUM_X     148
#define DRUM_ROW_BTN_MINUS 185
#define DRUM_ROW_BTN_PLUS  240

void drawDrumRow(int y, const String& label, int noteNum, uint16_t labelColor) {
  tft.fillRect(0, y, 320, 25, THEME_BG);
  tft.setTextColor(labelColor, THEME_BG);
  tft.drawString(label, DRUM_ROW_LABEL_X, y + 7, 2);
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString(getNoteNameFromMIDI(noteNum), DRUM_ROW_NAME_X, y + 7, 2);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString(String(noteNum), DRUM_ROW_NUM_X, y + 7, 2);
  drawRoundButton(DRUM_ROW_BTN_MINUS, y, 45, 25, "-", THEME_SECONDARY);
  drawRoundButton(DRUM_ROW_BTN_PLUS,  y, 45, 25, "+", THEME_SECONDARY);
}

// Toggle button: label + hint on left, filled ON/OFF pill on right
// Row height = 32px.  label at y+5 (font2 ~16px), hint at y+21 (font1 ~8px)
void drawToggleRow(int y, const String& label, const String& hint, bool state) {
  tft.fillRect(0, y, 320, 32, THEME_BG);

  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString(label, 10, y + 5, 2);        // font 2, ~16px tall

  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString(hint,  10, y + 22, 1);        // font 1, below label

  // Filled pill: green when ON, red when OFF — always filled so colour is obvious
  uint16_t col = state ? THEME_SUCCESS : THEME_ERROR;
  drawRoundButton(240, y + 2, 70, 28, state ? "ON" : "OFF", col, true);
}

// ------------------------------------------------------------------
//  Drum sub-menu
// ------------------------------------------------------------------
void drawDrumSubMenu() {
  tft.fillScreen(THEME_BG);
  drawHeader("DRUM", "Beat note mapping");

  int y = 55;
  tft.fillRect(0, y, 320, 25, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("MIDI CH", DRUM_ROW_LABEL_X, y + 7, 2);
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString(String(drumChannel), DRUM_ROW_NAME_X, y + 7, 2);
  drawRoundButton(DRUM_ROW_BTN_MINUS, y, 45, 25, "-", THEME_SECONDARY);
  drawRoundButton(DRUM_ROW_BTN_PLUS,  y, 45, 25, "+", THEME_SECONDARY);

  tft.drawFastHLine(0, 84, 320, THEME_SURFACE);

  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("DRUM", DRUM_ROW_LABEL_X, 86, 1);
  tft.drawString("NOTE", DRUM_ROW_NAME_X,  86, 1);
  tft.drawString("#",    DRUM_ROW_NUM_X,   86, 1);

  const String   labels[4] = {"KICK", "SNRE", "HHAT", "OPEN"};
  const uint16_t colors[4] = {THEME_ERROR, THEME_WARNING, THEME_PRIMARY, THEME_ACCENT};
  const int      rowY[4]   = {88, 122, 156, 190};

  for (int i = 0; i < 4; i++) {
    drawDrumRow(rowY[i], labels[i], drumNotes[i], colors[i]);
  }
}

void handleDrumSubMenu() {
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    storeSetting();
    inDrumSubMenu = false;
    drawSettingsMode();
    return;
  }
  if (!touch.justPressed) return;

  const int rowY[4] = {88, 122, 156, 190};

  int y = 55;
  if (isButtonPressed(DRUM_ROW_BTN_MINUS, y, 45, 25)) { drumChannel = max(1,   drumChannel - 1); drawDrumSubMenu(); return; }
  if (isButtonPressed(DRUM_ROW_BTN_PLUS,  y, 45, 25)) { drumChannel = min(16,  drumChannel + 1); drawDrumSubMenu(); return; }

  for (int i = 0; i < 4; i++) {
    if (isButtonPressed(DRUM_ROW_BTN_MINUS, rowY[i], 45, 25)) { drumNotes[i] = max(0,   drumNotes[i] - 1); drawDrumSubMenu(); return; }
    if (isButtonPressed(DRUM_ROW_BTN_PLUS,  rowY[i], 45, 25)) { drumNotes[i] = min(127, drumNotes[i] + 1); drawDrumSubMenu(); return; }
  }
}

// ------------------------------------------------------------------
//  Main settings screen
// ------------------------------------------------------------------
void drawSettingsMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("SETTINGS", FW_VERSION);

  // MIDI channel
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("MIDI Channel: " + String(channel), 10, 62, 2);
  drawRoundButton(200, 58, 45, 25, "CH -", THEME_PRIMARY);
  drawRoundButton(252, 58, 45, 25, "CH +", THEME_PRIMARY);

  // Gate length
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString("Gate (ms): " + String(gateLength), 10, 96, 2);
  drawRoundButton(200, 92, 45, 25, "GT -", THEME_PRIMARY);
  drawRoundButton(252, 92, 45, 25, "GT +", THEME_PRIMARY);

  tft.drawFastHLine(0, 119, 320, THEME_SURFACE);

  drawToggleRow(122, "BLE2SERIAL", "Bridge BLE <-> DIN MIDI", ble2serial);
  drawToggleRow(158, "MIDI THRU",  "Echo input back to output", midiThru);

  tft.drawFastHLine(0, 194, 320, THEME_SURFACE);

  // Sub-menu buttons (3 when WiFi compiled in, 2 otherwise)
#ifdef ENABLE_WIFI
  drawRoundButton(  5, 200, 95, 35, "DRUM",   THEME_ERROR);
  drawRoundButton(113, 200, 95, 35, "CV OUT", THEME_SUCCESS);
  drawRoundButton(221, 200, 94, 35, "WIFI",   THEME_ACCENT);
#else
  drawRoundButton( 10, 200, 90, 35, "DRUM",   THEME_ERROR);
  drawRoundButton(170, 200, 90, 35, "CV OUT", THEME_SUCCESS);
#endif
}

void handleSettingsMode() {
  if (inDrumSubMenu) { handleDrumSubMenu(); return; }
  if (inCvSubMenu)   { handleCVSettingsMode(); return; }
#ifdef ENABLE_WIFI
  if (inWifiSubMenu) { handleWifiSettingsMode(); return; }
#endif

  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    storeSetting();
    exitToMenu();
    return;
  }
  if (!touch.justPressed) return;

  if (isButtonPressed(200, 58, 45, 25)) { channel = max(1,   channel - 1); drawSettingsMode(); return; }
  if (isButtonPressed(252, 58, 45, 25)) { channel = min(16,  channel + 1); drawSettingsMode(); return; }
  if (isButtonPressed(200, 92, 45, 25)) { gateLength = max(10,  gateLength - 5); drawSettingsMode(); return; }
  if (isButtonPressed(252, 92, 45, 25)) { gateLength = min(500, gateLength + 5); drawSettingsMode(); return; }
  if (isButtonPressed(240, 124, 70, 28)) { ble2serial = !ble2serial; drawSettingsMode(); return; }
  if (isButtonPressed(240, 160, 70, 28)) { midiThru   = !midiThru;  drawSettingsMode(); return; }
#ifdef ENABLE_WIFI
  if (isButtonPressed(  5, 200, 95, 35)) { inDrumSubMenu = true; drawDrumSubMenu(); return; }
  if (isButtonPressed(113, 200, 95, 35)) { inCvSubMenu   = true; drawCVSettingsMode(); return; }
  if (isButtonPressed(221, 200, 94, 35)) { inWifiSubMenu = true; drawWifiSettingsMode(); return; }
#else
  if (isButtonPressed( 10, 200, 90, 35)) { inDrumSubMenu = true; drawDrumSubMenu(); return; }
  if (isButtonPressed(170, 200, 90, 35)) { inCvSubMenu   = true; drawCVSettingsMode(); return; }
#endif
}

// ------------------------------------------------------------------
//  EEPROM persistence
// ------------------------------------------------------------------
void storeSetting() {
  EEPROM.write(1,  channel);
  EEPROM.write(2,  gateLength);
  EEPROM.write(4,  drumChannel);
  EEPROM.write(5,  drumNotes[0]);
  EEPROM.write(6,  drumNotes[1]);
  EEPROM.write(7,  drumNotes[2]);
  EEPROM.write(8,  drumNotes[3]);
  EEPROM.write(9,  ble2serial ? 1 : 0);
  EEPROM.write(10, midiThru   ? 1 : 0);
  EEPROM.commit();
  Serial.println("Settings saved");
}

void initEeprom() {
  EEPROM.write(0,  130);  // magic v2.x WiFi
  EEPROM.write(1,  1);    // channel
  EEPROM.write(2,  30);   // gateLength
  EEPROM.write(3,  5);    // reserved
  EEPROM.write(4,  10);   // drumChannel
  EEPROM.write(5,  36);   // KICK
  EEPROM.write(6,  38);   // SNARE
  EEPROM.write(7,  42);   // HI-HAT
  EEPROM.write(8,  46);   // OPEN HI-HAT
  EEPROM.write(9,  0);    // ble2serial OFF
  EEPROM.write(10, 0);    // midiThru OFF
  // WiFi: blank SSID, password, flags (bytes 11-76)
  for (int i = 11; i < 77; i++) EEPROM.write(i, 0);
  // CV output defaults (bytes 77-80)
  EEPROM.write(77, 0);    // cvEnabled OFF
  EEPROM.write(78, 36);   // cvBaseNote = C2 (MIDI 36)
  EEPROM.write(79, 1);    // cvCCNumber = CC#1 (Mod Wheel)
  EEPROM.write(80, 0);    // cvMidiCh = 0 (all channels)
  EEPROM.commit();
  Serial.println("EEPROM initialised (v2.x defaults)");
}

#endif
