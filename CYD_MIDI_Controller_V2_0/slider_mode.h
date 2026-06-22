#ifndef SLIDER_MODE_H
#define SLIDER_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// ------------------------------------------------------------------
//  6-Slider MIDI Controller
//  Each slider sends CC, Modulation (CC1), or Pitch Bend on a
//  configurable channel. Tap the label to configure a slider.
// ------------------------------------------------------------------

#define SLIDER_COUNT     6
#define SLIDER_H        27
#define SLIDER_SPACING   3
#define SLIDER_START_Y  48
#define SLIDER_STRIDE   (SLIDER_H + SLIDER_SPACING)
#define SLIDER_LABEL_W  78   // left label+channel column
#define SLIDER_TRACK_X  82   // track start x
#define SLIDER_TRACK_W 178   // track width (x=82..260)
#define SLIDER_VAL_X   264   // value display x

enum SliderType { SL_CC, SL_MOD, SL_PITCH };

struct SliderConfig {
  SliderType type;
  int        ccNumber;   // used when type == SL_CC
  int        channel;    // 1-16
  int        value;      // 0-127 (or 0-16383 for pitch)
  int        lastSent;   // to suppress duplicate sends
  String     label;      // custom display name
};

SliderConfig sliders[SLIDER_COUNT] = {
  { SL_CC,    1,  1, 64, -1, "MOD"   },
  { SL_CC,    7,  1, 64, -1, "VOL"   },
  { SL_CC,   10,  1, 64, -1, "PAN"   },
  { SL_CC,   74,  1, 64, -1, "CUTOFF"},
  { SL_CC,   71,  1, 64, -1, "RESO"  },
  { SL_PITCH, 0,  1,  0, -1, "PITCH" },
};

int  sliderEditing   = -1;   // index of slider being configured (-1 = none)
bool sliderDragging  = false;
int  sliderDragIdx   = -1;

// ------------------------------------------------------------------
//  MIDI send helpers
// ------------------------------------------------------------------
void sliderSendCC(int ch, int cc, int val) {
  byte status = 0xB0 | ((ch - 1) & 0x0F);
  // BLE
  if (deviceConnected) {
    unsigned long now = millis();
    uint8_t hdr = 0x80 | ((now >> 7) & 0x3F);
    uint8_t ts  = 0x80 | (now & 0x7F);
    uint8_t pkt[5] = { hdr, ts, status, (uint8_t)cc, (uint8_t)val };
    pCharacteristic->setValue(pkt, 5);
    pCharacteristic->notify();
  }
  // DIN
  MIDISerial.write(status);
  MIDISerial.write((byte)cc);
  MIDISerial.write((byte)val);
}

void sliderSendPitch(int ch, int val14) {
  // val14: 0-16383, centre=8192
  byte status = 0xE0 | ((ch - 1) & 0x0F);
  byte lsb    = val14 & 0x7F;
  byte msb    = (val14 >> 7) & 0x7F;
  if (deviceConnected) {
    unsigned long now = millis();
    uint8_t hdr = 0x80 | ((now >> 7) & 0x3F);
    uint8_t ts  = 0x80 | (now & 0x7F);
    uint8_t pkt[5] = { hdr, ts, status, lsb, msb };
    pCharacteristic->setValue(pkt, 5);
    pCharacteristic->notify();
  }
  MIDISerial.write(status);
  MIDISerial.write(lsb);
  MIDISerial.write(msb);
}

void sliderTransmit(int idx) {
  SliderConfig& s = sliders[idx];
  switch (s.type) {
    case SL_MOD:
      if (s.value != s.lastSent) {
        sliderSendCC(s.channel, 1, s.value);
        s.lastSent = s.value;
      }
      break;
    case SL_CC:
      if (s.value != s.lastSent) {
        sliderSendCC(s.channel, s.ccNumber, s.value);
        s.lastSent = s.value;
      }
      break;
    case SL_PITCH: {
      // Map 0-127 → 0-16383 (centre at 64 → 8192)
      int val14 = (s.value * 128);  // 0..16256, close enough
      if (val14 != s.lastSent) {
        sliderSendPitch(s.channel, val14);
        s.lastSent = val14;
      }
      break;
    }
  }
}

// ------------------------------------------------------------------
//  Draw one slider row
// ------------------------------------------------------------------
void drawSliderRow(int idx, bool highlight) {
  SliderConfig& s = sliders[idx];
  int y = SLIDER_START_Y + idx * SLIDER_STRIDE;

  // Background
  uint16_t bgLabel = highlight ? THEME_PRIMARY : THEME_SURFACE;
  tft.fillRect(0, y, SLIDER_LABEL_W, SLIDER_H, bgLabel);

  // Label (type + cc info)
  tft.setTextColor(highlight ? THEME_BG : THEME_TEXT, bgLabel);
  tft.drawString(s.label, 4, y + 4, 1);

  // Channel + type hint
  String typeHint;
  switch (s.type) {
    case SL_CC:    typeHint = "CC" + String(s.ccNumber); break;
    case SL_MOD:   typeHint = "MOD";                     break;
    case SL_PITCH: typeHint = "PTCH";                    break;
  }
  tft.setTextColor(highlight ? THEME_BG : THEME_TEXT_DIM, bgLabel);
  tft.drawString("Ch" + String(s.channel) + " " + typeHint, 4, y + 16, 1);

  // Clear full area the thumb can reach (1px above/below the outline) then redraw outline on top
  tft.fillRect(SLIDER_TRACK_X, y + 2, SLIDER_TRACK_W, SLIDER_H - 4, THEME_BG);
  tft.drawRect(SLIDER_TRACK_X, y + 3, SLIDER_TRACK_W, SLIDER_H - 6, THEME_TEXT_DIM);

  // Fill
  int fillW = (s.value * SLIDER_TRACK_W) / 127;
  if (fillW > 0) {
    // Colour by type
    uint16_t col;
    switch (s.type) {
      case SL_CC:    col = THEME_PRIMARY;   break;
      case SL_MOD:   col = THEME_ACCENT;    break;
      case SL_PITCH: col = THEME_WARNING;   break;
    }
    tft.fillRect(SLIDER_TRACK_X + 1, y + 4, fillW - 1, SLIDER_H - 8, col);
  }

  // Thumb
  int thumbX = SLIDER_TRACK_X + fillW;
  thumbX = constrain(thumbX, SLIDER_TRACK_X + 3, SLIDER_TRACK_X + SLIDER_TRACK_W - 3);
  tft.fillRect(thumbX - 2, y + 2, 4, SLIDER_H - 4, THEME_TEXT);

  // Value
  tft.fillRect(SLIDER_VAL_X, y, 320 - SLIDER_VAL_X, SLIDER_H, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  if (s.type == SL_PITCH) {
    int centred = s.value - 64;
    tft.drawString((centred >= 0 ? "+" : "") + String(centred), SLIDER_VAL_X, y + 9, 1);
  } else {
    tft.drawString(String(s.value), SLIDER_VAL_X, y + 9, 1);
  }
}

void drawAllSliders() {
  for (int i = 0; i < SLIDER_COUNT; i++)
    drawSliderRow(i, i == sliderEditing);
}

// ------------------------------------------------------------------
//  Common CC names for display in config overlay
// ------------------------------------------------------------------
String sliderCCName(int cc) {
  switch (cc) {
    case 0:   return "Bank Select";
    case 1:   return "Modulation";
    case 2:   return "Breath";
    case 4:   return "Foot Ctrl";
    case 5:   return "Portamento";
    case 7:   return "Volume";
    case 8:   return "Balance";
    case 10:  return "Pan";
    case 11:  return "Expression";
    case 12:  return "FX Ctrl 1";
    case 13:  return "FX Ctrl 2";
    case 64:  return "Sustain";
    case 65:  return "Portamento";
    case 66:  return "Sostenuto";
    case 67:  return "Soft Pedal";
    case 68:  return "Legato";
    case 71:  return "Resonance";
    case 72:  return "Release";
    case 73:  return "Attack";
    case 74:  return "Cutoff Freq";
    case 75:  return "Decay";
    case 76:  return "Vibrato Rate";
    case 77:  return "Vibrato Depth";
    case 91:  return "Reverb";
    case 93:  return "Chorus";
    case 94:  return "Detune";
    default:  return "CC " + String(cc);
  }
}

// ------------------------------------------------------------------
//  Config overlay — slides up from y=100
// ------------------------------------------------------------------
#define CFG_Y  100

void drawSliderConfig() {
  if (sliderEditing < 0) return;
  SliderConfig& s = sliders[sliderEditing];

  // Overlay background
  tft.fillRect(0, CFG_Y, 320, 240 - CFG_Y, THEME_SURFACE);
  tft.drawFastHLine(0, CFG_Y, 320, THEME_PRIMARY);

  // ---- Title ----
  tft.setTextColor(THEME_TEXT, THEME_SURFACE);
  tft.drawCentreString("Slider " + String(sliderEditing + 1) + " Configure", 160, CFG_Y + 4, 2);

  // ---- Type selector ----
  int ty = CFG_Y + 20;
  tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
  tft.drawString("Type:", 6, ty + 4, 1);
  drawRoundButton( 50, ty, 70, 22, "CC",    s.type == SL_CC    ? THEME_PRIMARY : THEME_SECONDARY, s.type == SL_CC);
  drawRoundButton(124, ty, 70, 22, "MOD",   s.type == SL_MOD   ? THEME_ACCENT  : THEME_SECONDARY, s.type == SL_MOD);
  drawRoundButton(198, ty, 80, 22, "PITCH", s.type == SL_PITCH ? THEME_WARNING : THEME_SECONDARY, s.type == SL_PITCH);

  // ---- Channel — compact: label + [ - ] value [ + ] ----
  int cy = CFG_Y + 46;
  tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
  tft.drawString("Channel:", 6, cy + 4, 1);
  drawRoundButton( 88, cy, 35, 22, "-",  THEME_SECONDARY);
  tft.setTextColor(THEME_TEXT, THEME_SURFACE);
  tft.fillRect(127, cy, 50, 22, THEME_BG);
  tft.drawCentreString(String(s.channel), 152, cy + 4, 2);
  drawRoundButton(181, cy, 35, 22, "+",  THEME_SECONDARY);
  // Visual channel bar: 16 thin coloured dots
  for (int ch = 1; ch <= 16; ch++) {
    int dx = 222 + (ch - 1) * 5;
    tft.fillRect(dx, cy + 6, 4, 10, s.channel == ch ? THEME_PRIMARY : THEME_SURFACE);
    tft.drawRect(dx, cy + 6, 4, 10, THEME_TEXT_DIM);
  }

  // ---- CC number row ----
  int ny = CFG_Y + 72;
  if (s.type == SL_CC) {
    tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
    tft.drawString("CC#:", 6, ny + 4, 1);
    // Show number large
    tft.fillRect(44, ny, 52, 22, THEME_BG);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString(String(s.ccNumber), 70, ny + 3, 2);
    // Nudge buttons
    drawRoundButton(100, ny, 40, 22, "-10", THEME_SECONDARY);
    drawRoundButton(144, ny, 35, 22, "-1",  THEME_SECONDARY);
    drawRoundButton(183, ny, 35, 22, "+1",  THEME_SECONDARY);
    drawRoundButton(222, ny, 40, 22, "+10", THEME_SECONDARY);
    // CC name
    tft.fillRect(0, CFG_Y + 98, 320, 12, THEME_SURFACE);
    tft.setTextColor(THEME_ACCENT, THEME_SURFACE);
    tft.drawCentreString(sliderCCName(s.ccNumber), 160, CFG_Y + 98, 1);
  } else {
    tft.fillRect(0, ny, 320, 36, THEME_SURFACE);
    tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
    String desc = (s.type == SL_MOD) ? "Modulation Wheel  (CC 1)" : "Pitch Bend  (-64 .. +63)";
    tft.drawCentreString(desc, 160, ny + 4, 1);
    tft.drawCentreString("No CC number needed", 160, ny + 17, 1);
  }

  // ---- CLOSE ----
  drawRoundButton(110, CFG_Y + 112, 100, 24, "CLOSE", THEME_ERROR);
}

// ------------------------------------------------------------------
//  Full draw
// ------------------------------------------------------------------
void drawSliderMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("CC SLIDERS", "Tap label to configure");
  drawAllSliders();
  if (sliderEditing >= 0) drawSliderConfig();
}

// ------------------------------------------------------------------
//  Init
// ------------------------------------------------------------------
void initializeSliderMode() {
  sliderEditing  = -1;
  sliderDragging = false;
  sliderDragIdx  = -1;
  // Reset lastSent so values transmit on first touch
  for (int i = 0; i < SLIDER_COUNT; i++) sliders[i].lastSent = -1;
}

// ------------------------------------------------------------------
//  Touch → slider value (0-127)
// ------------------------------------------------------------------
int sliderValueFromTouch(int touchX) {
  int rel = constrain(touchX - SLIDER_TRACK_X, 0, SLIDER_TRACK_W);
  return (rel * 127) / SLIDER_TRACK_W;
}

// ------------------------------------------------------------------
//  Handle
// ------------------------------------------------------------------
void handleSliderMode() {
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    sliderEditing  = -1;
    sliderDragging = false;
    exitToMenu();
    return;
  }

  // ---- Config overlay interaction ----
  if (sliderEditing >= 0) {
    SliderConfig& s = sliders[sliderEditing];

    if (touch.justPressed) {
      // CLOSE
      if (isButtonPressed(110, CFG_Y + 112, 100, 24)) {
        sliderEditing = -1;
        drawSliderMode();
        return;
      }

      // Type buttons
      int ty = CFG_Y + 20;
      if (isButtonPressed( 50, ty, 70, 22)) { s.type = SL_CC;    if (s.ccNumber == 0) s.ccNumber = 1; drawSliderConfig(); drawSliderRow(sliderEditing, true); return; }
      if (isButtonPressed(124, ty, 70, 22)) { s.type = SL_MOD;   drawSliderConfig(); drawSliderRow(sliderEditing, true); return; }
      if (isButtonPressed(198, ty, 80, 22)) { s.type = SL_PITCH; s.value = 64; drawSliderConfig(); drawSliderRow(sliderEditing, true); return; }

      // Channel compact controls
      int cy = CFG_Y + 46;
      if (isButtonPressed( 88, cy, 35, 22)) { s.channel = max(1,  s.channel - 1); s.lastSent = -1; drawSliderConfig(); return; }
      if (isButtonPressed(181, cy, 35, 22)) { s.channel = min(16, s.channel + 1); s.lastSent = -1; drawSliderConfig(); return; }

      // CC number nudge buttons
      if (s.type == SL_CC) {
        int ny = CFG_Y + 72;
        if (isButtonPressed(100, ny, 40, 22)) { s.ccNumber = max(0,   s.ccNumber - 10); drawSliderConfig(); return; }
        if (isButtonPressed(144, ny, 35, 22)) { s.ccNumber = max(0,   s.ccNumber -  1); drawSliderConfig(); return; }
        if (isButtonPressed(183, ny, 35, 22)) { s.ccNumber = min(127, s.ccNumber +  1); drawSliderConfig(); return; }
        if (isButtonPressed(222, ny, 40, 22)) { s.ccNumber = min(127, s.ccNumber + 10); drawSliderConfig(); return; }
      }
    }
    return;  // eat all other touches while overlay is open
  }

  // ---- Slider dragging ----
  if (touch.justPressed) {
    for (int i = 0; i < SLIDER_COUNT; i++) {
      int y = SLIDER_START_Y + i * SLIDER_STRIDE;

      // Tap on label → open config
      if (touch.x < SLIDER_LABEL_W && touch.y >= y && touch.y < y + SLIDER_H) {
        sliderEditing = i;
        drawSliderRow(i, true);
        drawSliderConfig();
        return;
      }

      // Tap on track → start drag
      if (touch.x >= SLIDER_TRACK_X && touch.x < SLIDER_TRACK_X + SLIDER_TRACK_W &&
          touch.y >= y && touch.y < y + SLIDER_H) {
        sliderDragging = true;
        sliderDragIdx  = i;
        sliders[i].value = sliderValueFromTouch(touch.x);
        sliderTransmit(i);
        drawSliderRow(i, false);
        return;
      }
    }
  }

  if (touch.isPressed && sliderDragging && sliderDragIdx >= 0) {
    int newVal = sliderValueFromTouch(touch.x);
    if (newVal != sliders[sliderDragIdx].value) {
      sliders[sliderDragIdx].value = newVal;
      sliderTransmit(sliderDragIdx);
      drawSliderRow(sliderDragIdx, false);
    }
  }

  if (touch.justReleased) {
    sliderDragging = false;
    sliderDragIdx  = -1;
  }
}

#endif
