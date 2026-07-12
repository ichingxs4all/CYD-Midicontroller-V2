#ifndef EFFECTS_MODE_H
#define EFFECTS_MODE_H

// ------------------------------------------------------------------
//  SAM2695 effects settings sub-menu — Reverb & Chorus
//
//  The SAM2695 synth (M5Stack Synth Unit / compatible) is driven over
//  the DIN MIDI OUT (UART2 / MIDISerial).  This screen edits the chip's
//  built-in GS reverb and chorus processors and stores the values in
//  EEPROM so they survive a reboot.
//
//  Parameter map (from the SAM2695 datasheet, sections REVERB DEVICE /
//  CHORUS DEVICE — sent here as standard serial-MIDI messages):
//
//    REVERB
//      Program   -> CC 0x50 (80)        (0-7: room1..hall2..plate/delay)
//      Send lvl  -> CC 0x5B (91)        per-channel reverb send, 0-127
//      Time      -> GS SysEx 40 01 34   0-127
//      Feedback  -> GS SysEx 40 01 35   0-127  (Delay/PanDelay types only)
//
//    CHORUS
//      Program   -> CC 0x51 (81)        (0-7: chorus1..4/flanger/delays)
//      Send lvl  -> CC 0x5D (93)        per-channel chorus send, 0-127
//      Feedback  -> GS SysEx 40 01 3B   0-127
//      Delay     -> GS SysEx 40 01 3C   0-127
//      Rate      -> GS SysEx 40 01 3D   0-127
//      Depth     -> GS SysEx 40 01 3E   0-127
//
//  Program/time/feedback/... are global to the chip's single reverb and
//  chorus unit, so they go out once as GS SysEx.  The per-channel SEND
//  levels (CC 91 / 93) are broadcast to all 16 channels so every mode
//  (melody, drums, ...) is heard through the effects.
//
//  EEPROM layout (bytes 81-90):
//    81 reverbProgram   (0-7)     84 reverbFeedback (0-127)
//    82 reverbLevel     (0-127)   85 chorusProgram  (0-7)
//    83 reverbTime      (0-127)   86 chorusLevel    (0-127)
//                                 87 chorusFeedback (0-127)
//                                 88 chorusDelay    (0-127)
//                                 89 chorusRate     (0-127)
//                                 90 chorusDepth    (0-127)
// ------------------------------------------------------------------

#include "common_definitions.h"
#include "ui_elements.h"
#include "EEPROM.h"

extern HardwareSerial MIDISerial;   // DIN MIDI OUT (defined in the main .ino)
extern bool inFxSubMenu;            // defined in settings_mode.h

// ------------------------------------------------------------------
//  Settings (persisted in EEPROM bytes 81-90).  Defaults follow the
//  SAM2695 datasheet: reverb=Hall2, chorus=Chorus3 with that program's
//  documented delay/feedback/rate/depth values.
// ------------------------------------------------------------------
byte reverbProgram  = 4;    // 81 — Hall 2 (chip default)
byte reverbLevel    = 40;   // 82 — CC91 send
byte reverbTime     = 127;  // 83 — Hall2 default time (0x7F)
byte reverbFeedback = 0;    // 84 — delay types only

byte chorusProgram  = 2;    // 85 — Chorus 3 (chip default)
byte chorusLevel    = 0;    // 86 — CC93 send (opt-in)
byte chorusFeedback = 9;    // 87 — Chorus3 default (0x09)
byte chorusDelay    = 64;   // 88 — Chorus3 default (0x40)
byte chorusRate     = 3;    // 89 — Chorus3 default (0x03)
byte chorusDepth    = 19;   // 90 — Chorus3 default (0x13)

static const char* fxReverbProgNames[8] = {
  "Room1", "Room2", "Room3", "Hall1", "Hall2", "Plate", "Delay", "PanDelay"};
static const char* fxChorusProgNames[8] = {
  "Chorus1", "Chorus2", "Chorus3", "Chorus4", "Feedbck", "Flanger", "ShrtDly", "FBDelay"};

static int fxPage = 0;   // 0 = reverb tab, 1 = chorus tab

// ------------------------------------------------------------------
//  Low-level MIDI helpers — target the SAM2695 on DIN MIDI OUT
// ------------------------------------------------------------------

// Broadcast a Control Change to all 16 channels.
static void fxSendCCAllChannels(uint8_t cc, uint8_t value) {
  for (uint8_t c = 0; c < 16; c++) {
    MIDISerial.write((uint8_t)(0xB0 | c));
    MIDISerial.write((uint8_t)(cc & 0x7F));
    MIDISerial.write((uint8_t)(value & 0x7F));
  }
}

// Send a single-parameter Roland-GS SysEx (address 40 01 pp, one data byte).
// The trailing 0x00 is the checksum placeholder, matching the M5_SAM2695
// library — the SAM2695 does not validate it.
static void fxSendGsSysex(uint8_t param, uint8_t value) {
  uint8_t sx[] = {0xF0, 0x41, 0x00, 0x42, 0x12, 0x40, 0x01,
                  param, (uint8_t)(value & 0x7F), 0x00, 0xF7};
  MIDISerial.write(sx, sizeof(sx));
}

void fxApplyReverb() {
  fxSendCCAllChannels(0x50, reverbProgram & 0x07);  // reverb program (CC80 = REV_TYPE)
  fxSendCCAllChannels(0x5B, reverbLevel);           // reverb send level (CC91)
  fxSendGsSysex(0x34, reverbTime);                  // reverb time     (GS 40 01 34)
  fxSendGsSysex(0x35, reverbFeedback);              // reverb feedback (GS 40 01 35)
}

void fxApplyChorus() {
  fxSendCCAllChannels(0x51, chorusProgram & 0x07);  // chorus program (CC81 = CHR_TYPE)
  fxSendCCAllChannels(0x5D, chorusLevel);           // chorus send level (CC93)
  fxSendGsSysex(0x3B, chorusFeedback);              // chorus feedback (GS 40 01 3B)
  fxSendGsSysex(0x3C, chorusDelay);                 // chorus delay    (GS 40 01 3C)
  fxSendGsSysex(0x3D, chorusRate);                  // chorus rate     (GS 40 01 3D)
  fxSendGsSysex(0x3E, chorusDepth);                 // chorus depth    (GS 40 01 3E)
}

void fxApplyAll() {
  fxApplyReverb();
  fxApplyChorus();
}

// ------------------------------------------------------------------
//  EEPROM persistence (bytes 81-90)
// ------------------------------------------------------------------
void fxSaveSettings() {
  EEPROM.write(81, reverbProgram);
  EEPROM.write(82, reverbLevel);
  EEPROM.write(83, reverbTime);
  EEPROM.write(84, reverbFeedback);
  EEPROM.write(85, chorusProgram);
  EEPROM.write(86, chorusLevel);
  EEPROM.write(87, chorusFeedback);
  EEPROM.write(88, chorusDelay);
  EEPROM.write(89, chorusRate);
  EEPROM.write(90, chorusDepth);
  EEPROM.commit();
  Serial.println("[FX] Settings saved");
}

void fxLoadSettings() {
  reverbProgram  = EEPROM.read(81);
  reverbLevel    = EEPROM.read(82);
  reverbTime     = EEPROM.read(83);
  reverbFeedback = EEPROM.read(84);
  chorusProgram  = EEPROM.read(85);
  chorusLevel    = EEPROM.read(86);
  chorusFeedback = EEPROM.read(87);
  chorusDelay    = EEPROM.read(88);
  chorusRate     = EEPROM.read(89);
  chorusDepth    = EEPROM.read(90);
  // Guard against uninitialised flash (0xFF) — fall back to datasheet defaults
  if (reverbProgram  > 7)   reverbProgram  = 4;
  if (reverbLevel    > 127) reverbLevel    = 40;
  if (reverbTime     > 127) reverbTime     = 127;
  if (reverbFeedback > 127) reverbFeedback = 0;
  if (chorusProgram  > 7)   chorusProgram  = 2;
  if (chorusLevel    > 127) chorusLevel    = 0;
  if (chorusFeedback > 127) chorusFeedback = 9;
  if (chorusDelay    > 127) chorusDelay    = 64;
  if (chorusRate     > 127) chorusRate     = 3;
  if (chorusDepth    > 127) chorusDepth    = 19;
}

// ------------------------------------------------------------------
//  Initialisation — called from setup() after fxLoadSettings()
// ------------------------------------------------------------------
void initializeEffectsMode() {
  fxApplyAll();   // push stored effects to the SAM2695 on boot
  Serial.printf("[FX] init  reverb=%s lvl=%d time=%d fb=%d  chorus=%s lvl=%d fb=%d dly=%d rate=%d dep=%d\n",
                fxReverbProgNames[reverbProgram & 0x07], reverbLevel, reverbTime, reverbFeedback,
                fxChorusProgNames[chorusProgram & 0x07], chorusLevel, chorusFeedback, chorusDelay,
                chorusRate, chorusDepth);
}

// ------------------------------------------------------------------
//  Settings sub-menu UI — two tabs (REVERB / CHORUS)
// ------------------------------------------------------------------
#define FXROW_BTN_M   222
#define FXROW_BTN_P   268
#define FXROW_BTN_W    42
#define FXROW_BTN_H    22

// Parameter row: label + value + [-]/[+].  Buttons sit at y+1.
static void fxDrawRow(int y, const char* label, const String& val, uint16_t valColor) {
  tft.fillRect(0, y, 320, 25, THEME_BG);
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString(label, 8, y + 5, 2);
  tft.setTextColor(valColor, THEME_BG);
  tft.drawString(val, 150, y + 5, 2);
  drawRoundButton(FXROW_BTN_M, y + 1, FXROW_BTN_W, FXROW_BTN_H, "-", THEME_SECONDARY);
  drawRoundButton(FXROW_BTN_P, y + 1, FXROW_BTN_W, FXROW_BTN_H, "+", THEME_SECONDARY);
}

// Reverb rows at 84/109/134/159; chorus rows at 84/109/134/159/184/209.
static const int FX_ROW_Y[6] = {84, 109, 134, 159, 184, 209};

static void fxDrawReverbPage() {
  const uint16_t c = THEME_ACCENT;
  bool delayType = (reverbProgram == 6 || reverbProgram == 7);
  fxDrawRow(FX_ROW_Y[0], "Program",  fxReverbProgNames[reverbProgram & 0x07], c);
  fxDrawRow(FX_ROW_Y[1], "Send Lvl", String(reverbLevel),    c);
  fxDrawRow(FX_ROW_Y[2], "Time",     String(reverbTime),     c);
  // Feedback only applies to Delay / PanDelay programs — auto-hidden otherwise
  if (delayType) {
    fxDrawRow(FX_ROW_Y[3], "Feedback", String(reverbFeedback), c);
  } else {
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString("Feedback available on Delay / PanDelay", 8, FX_ROW_Y[3] + 5, 1);
  }
}

static void fxDrawChorusPage() {
  const uint16_t c = THEME_SUCCESS;
  fxDrawRow(FX_ROW_Y[0], "Program",  fxChorusProgNames[chorusProgram & 0x07], c);
  fxDrawRow(FX_ROW_Y[1], "Send Lvl", String(chorusLevel),    c);
  fxDrawRow(FX_ROW_Y[2], "Feedback", String(chorusFeedback), c);
  fxDrawRow(FX_ROW_Y[3], "Delay",    String(chorusDelay),    c);
  fxDrawRow(FX_ROW_Y[4], "Rate",     String(chorusRate),     c);
  fxDrawRow(FX_ROW_Y[5], "Depth",    String(chorusDepth),    c);
}

void drawEffectsSettingsMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("EFFECTS", "SAM2695 Reverb / Chorus");
  // Tabs
  drawRoundButton(8,   50, 140, 26, "REVERB", THEME_ACCENT,  fxPage == 0);
  drawRoundButton(172, 50, 140, 26, "CHORUS", THEME_SUCCESS, fxPage == 1);
  if (fxPage == 0) fxDrawReverbPage();
  else             fxDrawChorusPage();
}

// Adjust a 0-127 parameter by delta (clamped) then re-apply + redraw.
static void fxBump(byte& param, int delta) {
  int v = (int)param + delta;
  if (v < 0)   v = 0;
  if (v > 127) v = 127;
  param = (byte)v;
}

// Adjust a program value (wraps 0-7).
static void fxBumpProgram(byte& param, int delta) {
  param = (byte)(((int)param + delta + 8) & 0x07);
}

void handleEffectsSettingsMode() {
  // BACK button
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    fxSaveSettings();
    inFxSubMenu = false;
    drawSettingsMode();
    return;
  }
  if (!touch.justPressed) return;

  // Tab switching
  if (isButtonPressed(8,   50, 140, 26)) { if (fxPage != 0) { fxPage = 0; drawEffectsSettingsMode(); } return; }
  if (isButtonPressed(172, 50, 140, 26)) { if (fxPage != 1) { fxPage = 1; drawEffectsSettingsMode(); } return; }

  const int STEP = 5;   // step size for the 0-127 parameters

  if (fxPage == 0) {
    // --- REVERB tab ---
    int y;
    y = FX_ROW_Y[0] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBumpProgram(reverbProgram, -1); fxApplyReverb(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBumpProgram(reverbProgram, +1); fxApplyReverb(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[1] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(reverbLevel, -STEP); fxApplyReverb(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(reverbLevel, +STEP); fxApplyReverb(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[2] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(reverbTime, -STEP); fxApplyReverb(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(reverbTime, +STEP); fxApplyReverb(); drawEffectsSettingsMode(); return; }
    // Feedback row is only shown/active for Delay / PanDelay programs
    if (reverbProgram == 6 || reverbProgram == 7) {
      y = FX_ROW_Y[3] + 1;
      if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(reverbFeedback, -STEP); fxApplyReverb(); drawEffectsSettingsMode(); return; }
      if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(reverbFeedback, +STEP); fxApplyReverb(); drawEffectsSettingsMode(); return; }
    }
  } else {
    // --- CHORUS tab ---
    int y;
    y = FX_ROW_Y[0] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBumpProgram(chorusProgram, -1); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBumpProgram(chorusProgram, +1); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[1] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusLevel, -STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusLevel, +STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[2] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusFeedback, -STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusFeedback, +STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[3] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusDelay, -STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusDelay, +STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[4] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusRate, -STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusRate, +STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[5] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusDepth, -STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusDepth, +STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
  }
}

#endif
