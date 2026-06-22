#ifndef CV_OUTPUT_H
#define CV_OUTPUT_H

// ------------------------------------------------------------------
//  CV / Gate output via MCP4728 quad 12-bit I2C DAC
//
//  Hardware:
//    MCP4728 at I2C address 0x60 (A2=A1=A0=GND)
//    SDA = GPIO21  (shared with TFT backlight enable)
//    SCL = GPIO22
//
//  GPIO21 dual use:
//    The TFT backlight is driven HIGH by GPIO21.  I2C operates in
//    open-drain mode — idle state is HIGH (backlight on).  The brief
//    LOW pulses during I2C writes are ~10 µs per bit at 100 kHz and
//    are completely imperceptible to the eye.
//
//  Voltage reference:
//    Internal Vref = 2.048 V, Gain = 2× → full scale = 4.096 V
//    Resolution: 4096 steps → 1 mV per step
//
//  Channel mapping:
//    A — Pitch CV  : 1 V/octave, 0 V at cvBaseNote
//    B — Gate      : 0 V = off, 4.095 V = note-on
//    C — Velocity  : 0–127 → 0–4.095 V
//    D — CC value  : tracks CC# cvCCNumber, 0–127 → 0–4.095 V
//
//  EEPROM layout (bytes 77-80):
//    77 — cvEnabled  (0/1)
//    78 — cvBaseNote (MIDI note at 0 V output, default 36 = C2)
//    79 — cvCCNumber (CC# to route to channel D, default 1 = Mod Wheel)
//    80 — cvMidiCh   (0 = all channels, 1-16 = specific channel)
// ------------------------------------------------------------------

#include "common_definitions.h"
#include "ui_elements.h"
#include <Wire.h>

// ------------------------------------------------------------------
//  Settings (persisted in EEPROM bytes 77-80)
// ------------------------------------------------------------------
bool  cvEnabled  = false;   // EEPROM 77
byte  cvBaseNote = 36;      // EEPROM 78 — C2
byte  cvCCNumber = 1;       // EEPROM 79 — Mod Wheel
byte  cvMidiCh   = 0;       // EEPROM 80 — 0 = all channels

// ------------------------------------------------------------------
//  Runtime DAC values (12-bit, 0-4095)
// ------------------------------------------------------------------
uint16_t cvDacA = 0;   // Pitch CV
uint16_t cvDacB = 0;   // Gate
uint16_t cvDacC = 0;   // Velocity
uint16_t cvDacD = 0;   // CC value

#define MCP4728_ADDR   0x60
#define CV_FULL_SCALE  4095   // 12-bit max

// ------------------------------------------------------------------
//  Low-level: write all four DAC channels in one I2C transaction.
//  Multi-write command per channel:
//    Byte 0: 0x40 | (ch<<1) | UDAC   — 0100 DAC1 DAC0 UDAC
//    Byte 1: 0x90 | (val>>8)          — VREF=1 PD=00 Gx=1 D11-D8
//    Byte 2: val & 0xFF               — D7-D0
//  VREF=1 → internal 2.048V reference; Gx=1 → ×2 gain = 4.096V FS
// ------------------------------------------------------------------
static void cvWriteDAC() {
  Wire.beginTransmission(MCP4728_ADDR);
  // Channel A — Pitch CV
  Wire.write(0x40);
  Wire.write(0x90 | (cvDacA >> 8));
  Wire.write(cvDacA & 0xFF);
  // Channel B — Gate
  Wire.write(0x42);
  Wire.write(0x90 | (cvDacB >> 8));
  Wire.write(cvDacB & 0xFF);
  // Channel C — Velocity
  Wire.write(0x44);
  Wire.write(0x90 | (cvDacC >> 8));
  Wire.write(cvDacC & 0xFF);
  // Channel D — CC value
  Wire.write(0x46);
  Wire.write(0x90 | (cvDacD >> 8));
  Wire.write(cvDacD & 0xFF);
  Wire.endTransmission();
}

// ------------------------------------------------------------------
//  MIDI event processor — called from sendMIDI() and sendDrumMIDI()
// ------------------------------------------------------------------
void cvProcessMIDI(byte status, byte data1, byte data2) {
  if (!cvEnabled) return;

  byte cmd = status & 0xF0;
  byte ch  = (status & 0x0F) + 1;   // convert to 1-based

  // Channel filter — 0 accepts all channels
  if (cvMidiCh != 0 && ch != cvMidiCh) return;

  bool changed = false;

  if (cmd == 0x90 && data2 > 0) {
    // Note On: update Pitch, Gate, Velocity
    int semitones = (int)data1 - (int)cvBaseNote;
    int pitch     = (int)roundf(semitones * 1000.0f / 12.0f);
    cvDacA = (uint16_t)constrain(pitch, 0, CV_FULL_SCALE);
    cvDacB = CV_FULL_SCALE;                             // Gate ON
    cvDacC = (uint16_t)((data2 * (uint32_t)CV_FULL_SCALE) / 127);
    changed = true;

  } else if (cmd == 0x80 || (cmd == 0x90 && data2 == 0)) {
    // Note Off: gate low (pitch and velocity hold their last value)
    cvDacB = 0;
    changed = true;

  } else if (cmd == 0xB0 && data1 == cvCCNumber) {
    // CC matching cvCCNumber → channel D
    cvDacD = (uint16_t)((data2 * (uint32_t)CV_FULL_SCALE) / 127);
    changed = true;
  }

  if (changed) cvWriteDAC();
}

// ------------------------------------------------------------------
//  EEPROM persistence (bytes 77-80)
// ------------------------------------------------------------------
void cvSaveSettings() {
  EEPROM.write(77, cvEnabled  ? 1 : 0);
  EEPROM.write(78, cvBaseNote);
  EEPROM.write(79, cvCCNumber);
  EEPROM.write(80, cvMidiCh);
  EEPROM.commit();
  Serial.println("[CV] Settings saved");
}

void cvLoadSettings() {
  cvEnabled  = (EEPROM.read(77) == 1);
  cvBaseNote = EEPROM.read(78);
  cvCCNumber = EEPROM.read(79);
  cvMidiCh   = EEPROM.read(80);
  // Guard against uninitialised flash (0xFF)
  if (cvBaseNote > 127) cvBaseNote = 36;
  if (cvCCNumber > 127) cvCCNumber = 1;
  if (cvMidiCh   >  16) cvMidiCh  = 0;
}

// ------------------------------------------------------------------
//  Initialisation — called from setup() after cvLoadSettings()
// ------------------------------------------------------------------
void initializeCVOutput() {
  // Initialise I2C on GPIO21 (SDA) and GPIO22 (SCL).
  // GPIO21 is also the TFT backlight; open-drain idle = HIGH = backlight on.
 // Wire.begin(21, 22);
  Wire.begin(16, 17);

  Wire.setClock(100000);   // 100 kHz standard mode

  // Zero all DAC channels at startup
  cvDacA = cvDacB = cvDacC = cvDacD = 0;
  if (cvEnabled) cvWriteDAC();

  Serial.printf("[CV] MCP4728 init  enabled=%d  baseNote=%d  CC#=%d  ch=%d\n",
                cvEnabled, cvBaseNote, cvCCNumber, cvMidiCh);
}

// ------------------------------------------------------------------
//  Settings sub-menu UI
//  Forward declarations needed from earlier-compiled headers:
//    drawToggleRow()   — settings_mode.h (compiled before this file)
//    drawSettingsMode()— settings_mode.h (compiled before this file)
//    getNoteNameFromMIDI() — midi_utils.h (compiled before this file)
//    inCvSubMenu       — settings_mode.h (compiled before this file)
// ------------------------------------------------------------------

// Button column positions for this sub-menu
#define CVROW_LABEL_X    10
#define CVROW_VAL_X     165
#define CVROW_BTN_M     220
#define CVROW_BTN_P     266

static void cvDrawSettingsRow(int y, const char* label, const String& val) {
  tft.fillRect(0, y, 320, 28, THEME_BG);
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString(label, CVROW_LABEL_X, y + 6, 2);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawString(val, CVROW_VAL_X, y + 6, 2);
  drawRoundButton(CVROW_BTN_M, y + 2, 40, 24, "-", THEME_SECONDARY);
  drawRoundButton(CVROW_BTN_P, y + 2, 40, 24, "+", THEME_SECONDARY);
}

static void cvDrawMonitorBars() {
  const int barX = 10, barY = 192, barW = 62, barH = 12, gap = 9;
  const char*  labels[4] = {"PITCH", "GATE",    "VEL",     "CC"};
  uint16_t     colors[4] = {THEME_PRIMARY, THEME_SUCCESS, THEME_WARNING, THEME_ACCENT};
  uint16_t     vals[4]   = {cvDacA, cvDacB, cvDacC, cvDacD};

  for (int i = 0; i < 4; i++) {
    int x    = barX + i * (barW + gap);
    int fill = (int)((vals[i] / (float)CV_FULL_SCALE) * (barW - 2));
    // bar interior
    tft.fillRect(x + 1, barY + 1, fill, barH - 2, colors[i]);
    tft.fillRect(x + 1 + fill, barY + 1, barW - 2 - fill, barH - 2, THEME_SURFACE);
    tft.drawRect(x, barY, barW, barH, THEME_TEXT_DIM);
    // label below bar
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawCentreString(labels[i], x + barW / 2, barY + barH + 2, 1);
  }
}

void drawCVSettingsMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("CV OUT", "MCP4728  SDA=21  SCL=22");

  // Enable / disable toggle (row height 32 px)
  drawToggleRow(48, "ENABLE", cvEnabled ? "DAC active on I2C 0x60" : "No output — DAC silent", cvEnabled);

  tft.drawFastHLine(0, 83, 320, THEME_SURFACE);

  // Base note (0 V reference pitch)
  String baseStr = getNoteNameFromMIDI(cvBaseNote) + "  (MIDI " + String(cvBaseNote) + ")";
  cvDrawSettingsRow(86, "0V note (A)", baseStr);

  // CC number for channel D
  cvDrawSettingsRow(118, "CC# (ch D)", "CC " + String(cvCCNumber));

  // MIDI channel filter
  String chStr = (cvMidiCh == 0) ? "ALL" : "Ch " + String(cvMidiCh);
  cvDrawSettingsRow(150, "MIDI filter", chStr);

  tft.drawFastHLine(0, 182, 320, THEME_SURFACE);

  // Live monitor header
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("Live DAC  (0-4095 = 0-4.095V)", CVROW_LABEL_X, 184, 1);

  cvDrawMonitorBars();
}

void handleCVSettingsMode() {
  // Refresh live monitor bars every 250 ms
  static unsigned long lastBarRefresh = 0;
  unsigned long now = millis();
  if (now - lastBarRefresh >= 250) {
    lastBarRefresh = now;
    cvDrawMonitorBars();
  }

  // BACK button
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    cvSaveSettings();
    inCvSubMenu = false;
    drawSettingsMode();
    return;
  }
  if (!touch.justPressed) return;

  // Enable toggle pill at (240, 50, 70, 28) — matches drawToggleRow(48, ...)
  if (isButtonPressed(240, 50, 70, 28)) {
    cvEnabled = !cvEnabled;
    if (cvEnabled) {
      // Zero all channels when first enabled
      cvDacA = cvDacB = cvDacC = cvDacD = 0;
      cvWriteDAC();
    }
    drawCVSettingsMode();
    return;
  }

  // Base note − / +
  if (isButtonPressed(CVROW_BTN_M, 88, 40, 24)) {
    if (cvBaseNote > 0) { cvBaseNote--; drawCVSettingsMode(); }
    return;
  }
  if (isButtonPressed(CVROW_BTN_P, 88, 40, 24)) {
    if (cvBaseNote < 127) { cvBaseNote++; drawCVSettingsMode(); }
    return;
  }

  // CC number − / +
  if (isButtonPressed(CVROW_BTN_M, 120, 40, 24)) {
    if (cvCCNumber > 0) { cvCCNumber--; drawCVSettingsMode(); }
    return;
  }
  if (isButtonPressed(CVROW_BTN_P, 120, 40, 24)) {
    if (cvCCNumber < 127) { cvCCNumber++; drawCVSettingsMode(); }
    return;
  }

  // MIDI channel − / +  (0 = all)
  if (isButtonPressed(CVROW_BTN_M, 152, 40, 24)) {
    if (cvMidiCh > 0) { cvMidiCh--; drawCVSettingsMode(); }
    return;
  }
  if (isButtonPressed(CVROW_BTN_P, 152, 40, 24)) {
    if (cvMidiCh < 16) { cvMidiCh++; drawCVSettingsMode(); }
    return;
  }
}

#endif
