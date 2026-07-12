#ifndef EFFECTS_MODE_H
#define EFFECTS_MODE_H

// ------------------------------------------------------------------
//  SAM2695 effects settings sub-menu — Reverb / Chorus / Spatial / EQ
//
//  The SAM2695 synth (M5Stack Synth Unit / compatible) is driven over
//  the DIN MIDI OUT (UART2 / MIDISerial).  This screen edits the chip's
//  built-in GS reverb, chorus, spatial (surround) and 4-band equaliser
//  and stores the values in EEPROM so they survive a reboot.
//
//  Parameter map (from the SAM2695 datasheet — sent here as standard
//  serial-MIDI messages):
//
//    REVERB
//      Program   -> CC 0x50 (80)        (0-7: room1..hall2..plate/delay)
//      Send lvl  -> CC 0x5B (91)        per-channel reverb send, 0-127
//      Gen vol   -> GS SysEx 40 01 33   overall reverb return, 0-127
//      Time      -> GS SysEx 40 01 34   0-127
//      Feedback  -> GS SysEx 40 01 35   0-127  (Delay/PanDelay types only)
//
//    CHORUS
//      Program   -> CC 0x51 (81)        (0-7: chorus1..4/flanger/delays)
//      Send lvl  -> CC 0x5D (93)        per-channel chorus send, 0-127
//      Gen vol   -> GS SysEx 40 01 3A   overall chorus return, 0-127
//      Feedback  -> GS SysEx 40 01 3B   0-127
//      Delay     -> GS SysEx 40 01 3C   0-127
//      Rate      -> GS SysEx 40 01 3D   0-127
//      Depth     -> GS SysEx 40 01 3E   0-127
//
//    SPATIAL (stereo-widening / surround, global)
//      Volume    -> NRPN 3720h (14-bit) 0..255 (0=no effect .. FFh=max)
//      Delay     -> NRPN 372Ch  (7-bit) 0=shortest .. 127=longest
//      Input     -> NRPN 372Dh  (7-bit) 0=stereo, 127=mono/pseudo-stereo
//      On GM     -> NRPN 3718h  (GM_POST)  0=off / 127=on
//      On Rev/Ch -> NRPN 371Ah  (EFF_POST) 0=off / 127=on
//        (post effects = spatial + EQ; these route them onto each module)
//
//    EQUALISER (4-band, global — a "post effect" like spatial)
//      Band level -> NRPN 3700h/01/02/03  0-7Fh (00=-12dB,40h=0dB,7Fh=+12dB)
//                    (Low / Med-Low / Med-High / High)
//      Band freq  -> NRPN 3708h/09/0A/0B   0-7Fh linear, shown in Hz/kHz.
//                    Full-scale: Low 4.7kHz, Med-Low/High 4.2kHz, High 18.75kHz
//      EQ is applied wherever the spatial routing toggles are on.
//
//  Program/gen-vol/time/... are global to the chip's single reverb and
//  chorus unit, so they go out once as GS SysEx.  The per-channel SEND
//  levels (CC 91 / 93) are broadcast to all 16 channels so every mode
//  (melody, drums, ...) is heard through the effects.  The spatial and
//  EQ controls are global and use the chip's 37xx NRPN block (the same
//  scheme the M5_SAM2695 library uses for the equaliser).
//
//  Note: the GS SysEx data byte is 7-bit, so the reverb/chorus general
//  volumes (documented 0-FFh on the chip's parallel port) are controlled
//  here across their 0-127 SysEx range.
//
//  EEPROM layout (bytes 81-105):
//    81 reverbProgram   (0-7)     94 surPostGM      (0/127 routing GM)
//    82 reverbLevel     (0-127)   95 surPostRevCho  (0/127 routing rev/cho)
//    83 reverbTime      (0-127)   96 reverbVolume   (0-127 general volume)
//    84 reverbFeedback  (0-127)   97 chorusVolume   (0-127 general volume)
//    85 chorusProgram   (0-7)     98 eqLowLvl       (0-127)
//    86 chorusLevel     (0-127)   99 eqMLowLvl      (0-127)
//    87 chorusFeedback  (0-127)  100 eqMHighLvl     (0-127)
//    88 chorusDelay     (0-127)  101 eqHighLvl      (0-127)
//    89 chorusRate      (0-127)  102 eqLowFreq      (0-127)
//    90 chorusDepth     (0-127)  103 eqMLowFreq     (0-127)
//    91 surVolume       (0-255)  104 eqMHighFreq    (0-127)
//    92 surDelay        (0-127)  105 eqHighFreq     (0-127)
//    93 surInput        (0/127)
// ------------------------------------------------------------------

#include "common_definitions.h"
#include "ui_elements.h"
#include "EEPROM.h"

extern HardwareSerial MIDISerial;   // DIN MIDI OUT (defined in the main .ino)
extern bool inFxSubMenu;            // defined in settings_mode.h

// ------------------------------------------------------------------
//  Settings (persisted in EEPROM bytes 81-105).  Defaults follow the
//  SAM2695 datasheet.
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

byte surVolume = 0;    // 91 — Spatial Effect volume (0-255, 0 = off, FFh = max)
byte surDelay  = 29;   // 92 — Spatial Effect delay  (datasheet default 0x1D)
byte surInput  = 0;    // 93 — Spatial Effect input  (0 = stereo, 127 = mono)
byte surPostGM     = 127;  // 94 — route post-fx (spatial+EQ) onto GM      (0/127)
byte surPostRevCho = 127;  // 95 — route post-fx (spatial+EQ) onto rev/cho (0/127)

byte reverbVolume = 127;   // 96 — reverb general (return) volume, 0-127
byte chorusVolume = 127;   // 97 — chorus general (return) volume, 0-127

byte eqLowLvl    = 96;   // 98  — EQ_LB   0x60 (+6dB)
byte eqMLowLvl   = 64;   // 99  — EQ_MLB  0x40 (0dB)
byte eqMHighLvl  = 64;   // 100 — EQ_MHB  0x40 (0dB)
byte eqHighLvl   = 96;   // 101 — EQ_HB   0x60 (+6dB)
byte eqLowFreq   = 12;   // 102 — EQF_LB  0x0C
byte eqMLowFreq  = 27;   // 103 — EQF_MLB 0x1B
byte eqMHighFreq = 114;  // 104 — EQF_MHB 0x72
byte eqHighFreq  = 64;   // 105 — EQF_HB  0x40

static const char* fxReverbProgNames[8] = {
  "Room1", "Room2", "Room3", "Hall1", "Hall2", "Plate", "Delay", "PanDelay"};
static const char* fxChorusProgNames[8] = {
  "Chorus1", "Chorus2", "Chorus3", "Chorus4", "Feedbck", "Flanger", "ShrtDly", "FBDelay"};

static int fxPage = 0;   // 0 = reverb, 1 = chorus, 2 = spatial, 3 = EQ

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

// Send a 7-bit NRPN with MSB 0x37 (the SAM2695 parameter block) on channel 1.
// Layout: CC99=MSB, CC98=LSB, CC6=Data Entry MSB (7-bit value).
// Same message shape the M5_SAM2695 library uses for the equaliser — correct
// for the 0-7Fh spatial/EQ parameters and the routing switches.
static void fxSendNRPN(uint8_t lsb, uint8_t value) {
  uint8_t cmd[] = {0xB0, 0x63, 0x37, 0x62, lsb, 0x06, (uint8_t)(value & 0x7F)};
  MIDISerial.write(cmd, sizeof(cmd));
}

// Send a 14-bit NRPN (adds CC38 = Data Entry LSB) so an 8-bit 0-FFh parameter
// can reach its full range.  The device value is left-aligned in the 14-bit
// data-entry field: the 7-bit params put their value in CC6 alone, so an 8-bit
// value's high 7 bits go in CC6 and its lowest bit in the top of CC38.
static void fxSendNRPN14(uint8_t lsb, uint8_t deviceVal /* 0-255 */) {
  uint8_t entMSB = deviceVal >> 1;                     // high 7 bits
  uint8_t entLSB = (uint8_t)((deviceVal & 0x01) << 6); // low bit, left-aligned
  uint8_t cmd[] = {0xB0, 0x63, 0x37, 0x62, lsb, 0x06, entMSB, 0x26, entLSB};
  MIDISerial.write(cmd, sizeof(cmd));
}

void fxApplyReverb() {
  fxSendCCAllChannels(0x50, reverbProgram & 0x07);  // reverb program (CC80 = REV_TYPE)
  fxSendCCAllChannels(0x5B, reverbLevel);           // reverb send level (CC91)
  fxSendGsSysex(0x33, reverbVolume);                // reverb general volume (GS 40 01 33)
  fxSendGsSysex(0x34, reverbTime);                  // reverb time     (GS 40 01 34)
  fxSendGsSysex(0x35, reverbFeedback);              // reverb feedback (GS 40 01 35)
}

void fxApplyChorus() {
  fxSendCCAllChannels(0x51, chorusProgram & 0x07);  // chorus program (CC81 = CHR_TYPE)
  fxSendCCAllChannels(0x5D, chorusLevel);           // chorus send level (CC93)
  fxSendGsSysex(0x3A, chorusVolume);                // chorus general volume (GS 40 01 3A)
  fxSendGsSysex(0x3B, chorusFeedback);              // chorus feedback (GS 40 01 3B)
  fxSendGsSysex(0x3C, chorusDelay);                 // chorus delay    (GS 40 01 3C)
  fxSendGsSysex(0x3D, chorusRate);                  // chorus rate     (GS 40 01 3D)
  fxSendGsSysex(0x3E, chorusDepth);                 // chorus depth    (GS 40 01 3E)
}

void fxApplySpatial() {
  fxSendNRPN14(0x20, surVolume);  // SUR_VOL  (NRPN 3720, 0-FFh) 14-bit data entry
  fxSendNRPN(0x2C, surDelay);     // SUR_DEL  (NRPN 372C, 0-7Fh)
  fxSendNRPN(0x2D, surInput);     // SUR_INP  (NRPN 372D, 0=stereo 127=mono)
}

// Post-effect routing: switch the spatial (+EQ) processor onto each module.
void fxApplyRouting() {
  fxSendNRPN(0x18, surPostGM);      // GM_POST  (NRPN 3718, 0=off 127=on)
  fxSendNRPN(0x1A, surPostRevCho);  // EFF_POST (NRPN 371A, 0=off 127=on)
}

// 4-band equaliser: levels (3700-03) + band frequencies (3708-0B).
void fxApplyEQ() {
  fxSendNRPN(0x00, eqLowLvl);     // EQ_LB   3700
  fxSendNRPN(0x01, eqMLowLvl);    // EQ_MLB  3701
  fxSendNRPN(0x02, eqMHighLvl);   // EQ_MHB  3702
  fxSendNRPN(0x03, eqHighLvl);    // EQ_HB   3703
  fxSendNRPN(0x08, eqLowFreq);    // EQF_LB  3708
  fxSendNRPN(0x09, eqMLowFreq);   // EQF_MLB 3709
  fxSendNRPN(0x0A, eqMHighFreq);  // EQF_MHB 370A
  fxSendNRPN(0x0B, eqHighFreq);   // EQF_HB  370B
}

void fxApplyAll() {
  fxApplyReverb();
  fxApplyChorus();
  fxApplySpatial();
  fxApplyRouting();
  fxApplyEQ();
}

// ------------------------------------------------------------------
//  EEPROM persistence (bytes 81-105)
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
  EEPROM.write(91, surVolume);
  EEPROM.write(92, surDelay);
  EEPROM.write(93, surInput);
  EEPROM.write(94, surPostGM);
  EEPROM.write(95, surPostRevCho);
  EEPROM.write(96, reverbVolume);
  EEPROM.write(97, chorusVolume);
  EEPROM.write(98,  eqLowLvl);
  EEPROM.write(99,  eqMLowLvl);
  EEPROM.write(100, eqMHighLvl);
  EEPROM.write(101, eqHighLvl);
  EEPROM.write(102, eqLowFreq);
  EEPROM.write(103, eqMLowFreq);
  EEPROM.write(104, eqMHighFreq);
  EEPROM.write(105, eqHighFreq);
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
  surVolume      = EEPROM.read(91);
  surDelay       = EEPROM.read(92);
  surInput       = EEPROM.read(93);
  surPostGM      = EEPROM.read(94);
  surPostRevCho  = EEPROM.read(95);
  reverbVolume   = EEPROM.read(96);
  chorusVolume   = EEPROM.read(97);
  eqLowLvl       = EEPROM.read(98);
  eqMLowLvl      = EEPROM.read(99);
  eqMHighLvl     = EEPROM.read(100);
  eqHighLvl      = EEPROM.read(101);
  eqLowFreq      = EEPROM.read(102);
  eqMLowFreq     = EEPROM.read(103);
  eqMHighFreq    = EEPROM.read(104);
  eqHighFreq     = EEPROM.read(105);
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
  if (reverbVolume   > 127) reverbVolume   = 127;
  if (chorusVolume   > 127) chorusVolume   = 127;
  if (eqLowLvl       > 127) eqLowLvl       = 96;
  if (eqMLowLvl      > 127) eqMLowLvl      = 64;
  if (eqMHighLvl     > 127) eqMHighLvl     = 64;
  if (eqHighLvl      > 127) eqHighLvl      = 96;
  if (eqLowFreq      > 127) eqLowFreq      = 12;
  if (eqMLowFreq     > 127) eqMLowFreq     = 27;
  if (eqMHighFreq    > 127) eqMHighFreq    = 114;
  if (eqHighFreq     > 127) eqHighFreq     = 64;
  // surDelay is a 7-bit (0-127) parameter, so 0xFF here means the spatial
  // EEPROM block was never written (upgrade from a pre-spatial build) — fall
  // back to defaults.  Otherwise surVolume is a genuine 0-255 value.
  if (surDelay > 127) {
    surVolume     = 0;
    surDelay      = 29;
    surInput      = 0;
    surPostGM     = 127;   // routing defaults: post-fx on (datasheet 7Fh)
    surPostRevCho = 127;
  } else {
    surInput      = (surInput      >= 64) ? 127 : 0;   // normalise stereo / mono
    surPostGM     = (surPostGM     >= 64) ? 127 : 0;   // normalise on / off
    surPostRevCho = (surPostRevCho >= 64) ? 127 : 0;
  }
}

// ------------------------------------------------------------------
//  Initialisation — called from setup() after fxLoadSettings()
// ------------------------------------------------------------------
void initializeEffectsMode() {
  fxApplyAll();   // push stored effects to the SAM2695 on boot
  Serial.printf("[FX] init  reverb=%s send=%d vol=%d  chorus=%s send=%d vol=%d  spatial vol=%d %s  eq=%d/%d/%d/%d\n",
                fxReverbProgNames[reverbProgram & 0x07], reverbLevel, reverbVolume,
                fxChorusProgNames[chorusProgram & 0x07], chorusLevel, chorusVolume,
                surVolume, (surInput >= 64 ? "mono" : "stereo"),
                eqLowLvl, eqMLowLvl, eqMHighLvl, eqHighLvl);
}

// ------------------------------------------------------------------
//  Settings sub-menu UI — four tabs (REVERB / CHORUS / SPATIAL / EQ)
// ------------------------------------------------------------------
#define FXROW_BTN_M   222
#define FXROW_BTN_P   268
#define FXROW_BTN_W    42
#define FXROW_BTN_H    20
#define FXROW_H        22

// Tab bar geometry (four equal tabs)
#define FXTAB_Y        50
#define FXTAB_W        76
#define FXTAB_H        26
#define FXTAB0_X        4
#define FXTAB1_X       82
#define FXTAB2_X      160
#define FXTAB3_X      238

// Toggle-pill geometry (right-hand side of a toggle row)
#define FXTOG_X       222
#define FXTOG_W        88

// Parameter row: label + value + [-]/[+].  Buttons sit at y+1.
static void fxDrawRow(int y, const char* label, const String& val, uint16_t valColor) {
  tft.fillRect(0, y, 320, FXROW_H, THEME_BG);
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString(label, 8, y + 3, 2);
  tft.setTextColor(valColor, THEME_BG);
  tft.drawString(val, 150, y + 3, 2);
  drawRoundButton(FXROW_BTN_M, y + 1, FXROW_BTN_W, FXROW_BTN_H, "-", THEME_SECONDARY);
  drawRoundButton(FXROW_BTN_P, y + 1, FXROW_BTN_W, FXROW_BTN_H, "+", THEME_SECONDARY);
}

// Toggle row: label on the left + a filled ON/OFF pill on the right.
static void fxDrawToggleRow(int y, const char* label, bool state) {
  tft.fillRect(0, y, 320, FXROW_H, THEME_BG);
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString(label, 8, y + 3, 2);
  uint16_t col = state ? THEME_SUCCESS : THEME_ERROR;
  drawRoundButton(FXTOG_X, y + 1, FXTOG_W, FXROW_BTN_H, state ? "ON" : "OFF", col, true);
}

// Seven evenly-spaced rows (22 px) from y=80 down to y=212 — reverb/chorus/spatial.
static const int FX_ROW_Y[7] = {80, 102, 124, 146, 168, 190, 212};
// Eight tighter rows (20 px) from y=78 down to y=218 — EQ page.
static const int EQ_ROW_Y[8] = {78, 98, 118, 138, 158, 178, 198, 218};

// Format an EQ band level as a signed dB string (00h=-12dB, 40h=0dB, 7Fh=+12dB).
// Rounded to the nearest dB so the endpoints read -12 / +12 exactly.
static String fxEqDbStr(byte v) {
  int num = ((int)v - 64) * 12;
  int db  = (num >= 0) ? (num + 32) / 64 : (num - 32) / 64;
  String s = String(db) + "dB";
  if (db > 0) s = String("+") + s;
  return s;
}

// Format an EQ band cutoff (0-7Fh linear) as a real frequency.  maxHz is the
// band's top of range (datasheet): Low 4700, Med-Low/High 4200, High 18750.
static String fxEqFreqStr(byte v, int maxHz) {
  long hz = (long)v * maxHz / 127;
  if (hz >= 1000) {
    long k10 = (hz + 50) / 100;   // tenths of a kHz, rounded
    return String(k10 / 10) + "." + String(k10 % 10) + "kHz";
  }
  return String((int)hz) + "Hz";
}

static void fxDrawReverbPage() {
  const uint16_t c = THEME_ACCENT;
  bool delayType = (reverbProgram == 6 || reverbProgram == 7);
  fxDrawRow(FX_ROW_Y[0], "Program",  fxReverbProgNames[reverbProgram & 0x07], c);
  fxDrawRow(FX_ROW_Y[1], "Send Lvl", String(reverbLevel),  c);
  fxDrawRow(FX_ROW_Y[2], "Gen Vol",  String(reverbVolume), c);
  fxDrawRow(FX_ROW_Y[3], "Time",     String(reverbTime),   c);
  // Feedback only applies to Delay / PanDelay programs — auto-hidden otherwise
  if (delayType) {
    fxDrawRow(FX_ROW_Y[4], "Feedback", String(reverbFeedback), c);
  } else {
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString("Feedback available on Delay / PanDelay", 8, FX_ROW_Y[4] + 4, 1);
  }
}

static void fxDrawChorusPage() {
  const uint16_t c = THEME_SUCCESS;
  fxDrawRow(FX_ROW_Y[0], "Program",  fxChorusProgNames[chorusProgram & 0x07], c);
  fxDrawRow(FX_ROW_Y[1], "Send Lvl", String(chorusLevel),    c);
  fxDrawRow(FX_ROW_Y[2], "Gen Vol",  String(chorusVolume),   c);
  fxDrawRow(FX_ROW_Y[3], "Feedback", String(chorusFeedback), c);
  fxDrawRow(FX_ROW_Y[4], "Delay",    String(chorusDelay),    c);
  fxDrawRow(FX_ROW_Y[5], "Rate",     String(chorusRate),     c);
  fxDrawRow(FX_ROW_Y[6], "Depth",    String(chorusDepth),    c);
}

static void fxDrawSpatialPage() {
  const uint16_t c = THEME_WARNING;
  fxDrawRow(FX_ROW_Y[0], "Volume", String(surVolume), c);
  fxDrawRow(FX_ROW_Y[1], "Delay",  String(surDelay),  c);
  fxDrawRow(FX_ROW_Y[2], "Input",  surInput >= 64 ? "Mono" : "Stereo", c);
  fxDrawToggleRow(FX_ROW_Y[3], "Apply to GM",     surPostGM     >= 64);
  fxDrawToggleRow(FX_ROW_Y[4], "Apply to Rev/Ch", surPostRevCho >= 64);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("Routes spatial+EQ onto each module", 8, FX_ROW_Y[5] + 4, 1);
}

static void fxDrawEqPage() {
  const uint16_t c = THEME_PRIMARY;
  fxDrawRow(EQ_ROW_Y[0], "Low Gain",   fxEqDbStr(eqLowLvl),   c);
  fxDrawRow(EQ_ROW_Y[1], "LoMid Gain", fxEqDbStr(eqMLowLvl),  c);
  fxDrawRow(EQ_ROW_Y[2], "HiMid Gain", fxEqDbStr(eqMHighLvl), c);
  fxDrawRow(EQ_ROW_Y[3], "High Gain",  fxEqDbStr(eqHighLvl),  c);
  fxDrawRow(EQ_ROW_Y[4], "Low Freq",   fxEqFreqStr(eqLowFreq,    4700),  c);
  fxDrawRow(EQ_ROW_Y[5], "LoMid Freq", fxEqFreqStr(eqMLowFreq,   4200),  c);
  fxDrawRow(EQ_ROW_Y[6], "HiMid Freq", fxEqFreqStr(eqMHighFreq,  4200),  c);
  fxDrawRow(EQ_ROW_Y[7], "High Freq",  fxEqFreqStr(eqHighFreq,  18750),  c);
}

void drawEffectsSettingsMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("EFFECTS", "Reverb / Chorus / Spatial / EQ");
  // Tabs
  drawRoundButton(FXTAB0_X, FXTAB_Y, FXTAB_W, FXTAB_H, "REVERB",  THEME_ACCENT,  fxPage == 0);
  drawRoundButton(FXTAB1_X, FXTAB_Y, FXTAB_W, FXTAB_H, "CHORUS",  THEME_SUCCESS, fxPage == 1);
  drawRoundButton(FXTAB2_X, FXTAB_Y, FXTAB_W, FXTAB_H, "SPATIAL", THEME_WARNING, fxPage == 2);
  drawRoundButton(FXTAB3_X, FXTAB_Y, FXTAB_W, FXTAB_H, "EQ",      THEME_PRIMARY, fxPage == 3);
  if      (fxPage == 0) fxDrawReverbPage();
  else if (fxPage == 1) fxDrawChorusPage();
  else if (fxPage == 2) fxDrawSpatialPage();
  else                  fxDrawEqPage();
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

// Adjust a 0-255 parameter by delta (clamped) — used for spatial Volume.
static void fxBump255(byte& param, int delta) {
  int v = (int)param + delta;
  if (v < 0)   v = 0;
  if (v > 255) v = 255;
  param = (byte)v;
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
  if (isButtonPressed(FXTAB0_X, FXTAB_Y, FXTAB_W, FXTAB_H)) { if (fxPage != 0) { fxPage = 0; drawEffectsSettingsMode(); } return; }
  if (isButtonPressed(FXTAB1_X, FXTAB_Y, FXTAB_W, FXTAB_H)) { if (fxPage != 1) { fxPage = 1; drawEffectsSettingsMode(); } return; }
  if (isButtonPressed(FXTAB2_X, FXTAB_Y, FXTAB_W, FXTAB_H)) { if (fxPage != 2) { fxPage = 2; drawEffectsSettingsMode(); } return; }
  if (isButtonPressed(FXTAB3_X, FXTAB_Y, FXTAB_W, FXTAB_H)) { if (fxPage != 3) { fxPage = 3; drawEffectsSettingsMode(); } return; }

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
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(reverbVolume, -STEP); fxApplyReverb(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(reverbVolume, +STEP); fxApplyReverb(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[3] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(reverbTime, -STEP); fxApplyReverb(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(reverbTime, +STEP); fxApplyReverb(); drawEffectsSettingsMode(); return; }
    // Feedback row is only shown/active for Delay / PanDelay programs
    if (reverbProgram == 6 || reverbProgram == 7) {
      y = FX_ROW_Y[4] + 1;
      if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(reverbFeedback, -STEP); fxApplyReverb(); drawEffectsSettingsMode(); return; }
      if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(reverbFeedback, +STEP); fxApplyReverb(); drawEffectsSettingsMode(); return; }
    }
  } else if (fxPage == 1) {
    // --- CHORUS tab ---
    int y;
    y = FX_ROW_Y[0] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBumpProgram(chorusProgram, -1); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBumpProgram(chorusProgram, +1); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[1] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusLevel, -STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusLevel, +STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[2] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusVolume, -STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusVolume, +STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[3] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusFeedback, -STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusFeedback, +STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[4] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusDelay, -STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusDelay, +STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[5] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusRate, -STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusRate, +STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[6] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusDepth, -STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(chorusDepth, +STEP); fxApplyChorus(); drawEffectsSettingsMode(); return; }
  } else if (fxPage == 2) {
    // --- SPATIAL tab ---
    const int VOL_STEP = 10;   // larger step: Volume spans 0-255
    int y;
    y = FX_ROW_Y[0] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump255(surVolume, -VOL_STEP); fxApplySpatial(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump255(surVolume, +VOL_STEP); fxApplySpatial(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[1] + 1;
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(surDelay, -STEP); fxApplySpatial(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(surDelay, +STEP); fxApplySpatial(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[2] + 1;
    // Input is a two-state toggle (Stereo <-> Mono); either button flips it.
    if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { surInput = (surInput >= 64) ? 0 : 127; fxApplySpatial(); drawEffectsSettingsMode(); return; }
    if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { surInput = (surInput >= 64) ? 0 : 127; fxApplySpatial(); drawEffectsSettingsMode(); return; }
    // Routing toggles: apply spatial (+EQ) onto GM / Reverb-Chorus
    y = FX_ROW_Y[3] + 1;
    if (isButtonPressed(FXTOG_X, y, FXTOG_W, FXROW_BTN_H)) { surPostGM = (surPostGM >= 64) ? 0 : 127; fxApplyRouting(); drawEffectsSettingsMode(); return; }
    y = FX_ROW_Y[4] + 1;
    if (isButtonPressed(FXTOG_X, y, FXTOG_W, FXROW_BTN_H)) { surPostRevCho = (surPostRevCho >= 64) ? 0 : 127; fxApplyRouting(); drawEffectsSettingsMode(); return; }
  } else {
    // --- EQ tab ---  (4 band levels + 4 band frequencies)
    byte* eqParams[8] = {&eqLowLvl, &eqMLowLvl, &eqMHighLvl, &eqHighLvl,
                         &eqLowFreq, &eqMLowFreq, &eqMHighFreq, &eqHighFreq};
    for (int i = 0; i < 8; i++) {
      int y = EQ_ROW_Y[i] + 1;
      if (isButtonPressed(FXROW_BTN_M, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(*eqParams[i], -STEP); fxApplyEQ(); drawEffectsSettingsMode(); return; }
      if (isButtonPressed(FXROW_BTN_P, y, FXROW_BTN_W, FXROW_BTN_H)) { fxBump(*eqParams[i], +STEP); fxApplyEQ(); drawEffectsSettingsMode(); return; }
    }
  }
}

#endif
