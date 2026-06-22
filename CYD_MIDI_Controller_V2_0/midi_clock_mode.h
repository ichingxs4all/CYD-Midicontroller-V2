#ifndef MIDI_CLOCK_MODE_H
#define MIDI_CLOCK_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// ---------------------------------------------------------------
//  MIDI Clock – master generator or slave tap-tempo display
//  Sends/receives on both BLE MIDI and DIN MIDI (UART2)
// ---------------------------------------------------------------

#define MIDI_CLOCK  0xF8
#define MIDI_START  0xFA
#define MIDI_CONT   0xFB
#define MIDI_STOP   0xFC
#define PPQN        24

// ------------------------------------------------------------------
//  Layout constants  (screen = 320 x 240, header occupies 0-45)
// ------------------------------------------------------------------
#define CLK_Y_DOTS      48   // beat indicator dots (h=16)
#define CLK_Y_BADGES    68   // MASTER/SLAVE + RUNNING/STOPPED (h=25)
#define CLK_Y_BPMLABEL  98   // "BPM" or "INCOMING BPM" text (h=12)
#define CLK_Y_BPMNUM   112   // large BPM number, font 6 ~36px tall → bottom 148
#define CLK_Y_NUDGE    152   // BPM nudge buttons (h=25) → bottom 177
#define CLK_Y_TRANSPORT 182  // START / STOP / CONT / →SLAVE (h=25) → bottom 207

struct ClockState {
  bool    isMaster    = true;
  bool    isRunning   = false;
  float   bpm         = 120.0f;

  // Master timing
  unsigned long lastPulseMicros = 0;
  unsigned long pulseIntervalUs = 0;
  uint32_t      pulseCount      = 0;
  uint8_t       beatPulse       = 0;   // 0-23 within one quarter note

  // Slave BPM measurement
  unsigned long lastSlaveClockUs    = 0;
  unsigned long slaveIntervals[8];
  int           slaveWindowIdx      = 0;
  int           slaveWindowFill     = 0;
  float         slaveBpm            = 0.0f;
  bool          slaveClockActive    = false;
  unsigned long lastSlaveActivityMs = 0;

  bool needsRedraw = false;
};

ClockState clk;

// ------------------------------------------------------------------
//  Timing helper
// ------------------------------------------------------------------
void clockRecalcInterval() {
  clk.pulseIntervalUs = (unsigned long)(60000000.0f / (clk.bpm * PPQN));
}

// ------------------------------------------------------------------
//  Send a single-byte real-time message on BLE + DIN
// ------------------------------------------------------------------
void sendRealTime(byte msg) {
  MIDISerial.write(msg);
  if (deviceConnected) {
    unsigned long now = millis();
    uint8_t hdr = 0x80 | ((now >> 7) & 0x3F);
    uint8_t ts  = 0x80 | (now & 0x7F);
    uint8_t pkt[3] = { hdr, ts, msg };
    pCharacteristic->setValue(pkt, 3);
    pCharacteristic->notify();
  }
}

// ------------------------------------------------------------------
//  Called by the shared BLE/DIN receive dispatcher
// ------------------------------------------------------------------
void clockReceiveByte(byte b) {
  if (clk.isMaster) return;

  if (b == MIDI_CLOCK) {
    unsigned long now = micros();
    if (clk.lastSlaveClockUs != 0) {
      unsigned long interval = now - clk.lastSlaveClockUs;
      if (interval > 1000 && interval < 5000000) {
        clk.slaveIntervals[clk.slaveWindowIdx] = interval;
        clk.slaveWindowIdx = (clk.slaveWindowIdx + 1) % 8;
        if (clk.slaveWindowFill < 8) clk.slaveWindowFill++;
        unsigned long sum = 0;
        for (int i = 0; i < clk.slaveWindowFill; i++) sum += clk.slaveIntervals[i];
        clk.slaveBpm = 60000000.0f / ((float)sum / clk.slaveWindowFill * PPQN);
        clk.slaveClockActive    = true;
        clk.lastSlaveActivityMs = millis();
        clk.needsRedraw         = true;
      }
    }
    clk.lastSlaveClockUs = now;
  }
  else if (b == MIDI_START || b == MIDI_CONT) {
    clk.isRunning   = true;
    clk.needsRedraw = true;
  }
  else if (b == MIDI_STOP) {
    clk.isRunning   = false;
    clk.needsRedraw = true;
  }
}

// ------------------------------------------------------------------
//  Draw: beat indicator (4 quarter-note dots)
// ------------------------------------------------------------------
void drawClockBeatIndicator() {
  int spacing = 40;
  int startX  = 160 - (3 * spacing) / 2;
  int currentBeat = (clk.pulseCount / PPQN) % 4;

  for (int b = 0; b < 4; b++) {
    int cx = startX + b * spacing;
    bool active = clk.isRunning && (b == currentBeat);
    tft.fillCircle(cx, CLK_Y_DOTS + 8, 7, active ? THEME_SUCCESS : THEME_SURFACE);
    tft.drawCircle(cx, CLK_Y_DOTS + 8, 7, active ? THEME_SUCCESS : THEME_TEXT_DIM);
  }
}

// ------------------------------------------------------------------
//  Draw: mode/status badges + BPM area
// ------------------------------------------------------------------
void drawClockInfo() {
  // --- Row 1: MASTER/SLAVE badge + RUNNING/STOPPED badge + pulse counter ---
  tft.fillRoundRect(5, CLK_Y_BADGES, 95, 25, 5,
                    clk.isMaster ? THEME_PRIMARY : THEME_ACCENT);
  tft.setTextColor(THEME_BG, clk.isMaster ? THEME_PRIMARY : THEME_ACCENT);
  tft.drawCentreString(clk.isMaster ? "MASTER" : "SLAVE", 52, CLK_Y_BADGES + 7, 2);

  tft.fillRoundRect(110, CLK_Y_BADGES, 100, 25, 5,
                    clk.isRunning ? THEME_SUCCESS : THEME_SURFACE);
  tft.setTextColor(clk.isRunning ? THEME_BG : THEME_TEXT_DIM,
                   clk.isRunning ? THEME_SUCCESS : THEME_SURFACE);
  tft.drawCentreString(clk.isRunning ? "RUNNING" : "STOPPED", 160, CLK_Y_BADGES + 7, 2);

  // Pulse counter (top-right, small)
  tft.fillRect(220, CLK_Y_BADGES, 95, 25, THEME_BG);
  if (clk.isRunning && clk.isMaster) {
    char pbuf[14];
    snprintf(pbuf, sizeof(pbuf), "%lu p", clk.pulseCount % 1000000);
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString(pbuf, 222, CLK_Y_BADGES + 8, 1);
  }

  // --- Row 2: BPM label ---
  tft.fillRect(0, CLK_Y_BPMLABEL, 320, 12, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString(clk.isMaster ? "BPM" : "INCOMING BPM", 160, CLK_Y_BPMLABEL, 1);

  // --- Row 3: Large BPM number ---
  tft.fillRect(0, CLK_Y_BPMNUM, 320, 38, THEME_BG);

  if (clk.isMaster) {
    char bpmbuf[8];
    snprintf(bpmbuf, sizeof(bpmbuf), "%.1f", clk.bpm);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString(bpmbuf, 160, CLK_Y_BPMNUM, 6);
  } else {
    if (clk.slaveClockActive) {
      char sbuf[10];
      snprintf(sbuf, sizeof(sbuf), "%.1f", clk.slaveBpm);
      tft.setTextColor(THEME_ACCENT, THEME_BG);
      tft.drawCentreString(sbuf, 160, CLK_Y_BPMNUM, 6);
    } else {
      tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
      tft.drawCentreString("---", 160, CLK_Y_BPMNUM, 6);
    }
  }

  // --- Row 4: BPM nudge buttons (master) or hint text (slave) ---
  tft.fillRect(0, CLK_Y_NUDGE, 320, 25, THEME_BG);

  if (clk.isMaster) {
    drawRoundButton(5,   CLK_Y_NUDGE, 50, 25, "-10", THEME_SECONDARY);
    drawRoundButton(58,  CLK_Y_NUDGE, 50, 25, "-1",  THEME_SECONDARY);
    drawRoundButton(111, CLK_Y_NUDGE, 50, 25, "-.1", THEME_SECONDARY);
    drawRoundButton(164, CLK_Y_NUDGE, 50, 25, "+.1", THEME_SECONDARY);
    drawRoundButton(217, CLK_Y_NUDGE, 50, 25, "+1",  THEME_SECONDARY);
    drawRoundButton(270, CLK_Y_NUDGE, 45, 25, "+10", THEME_SECONDARY);
  } else {
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawCentreString("Listening on BLE + DIN IN", 160, CLK_Y_NUDGE + 7, 1);
  }
}

// ------------------------------------------------------------------
//  Draw: transport buttons (bottom row)
// ------------------------------------------------------------------
void drawClockControls() {
  drawRoundButton(5,   CLK_Y_TRANSPORT, 70, 25, "START",   THEME_SUCCESS);
  drawRoundButton(80,  CLK_Y_TRANSPORT, 65, 25, "STOP",    THEME_ERROR);
  drawRoundButton(150, CLK_Y_TRANSPORT, 75, 25, "CONT",    THEME_WARNING);
  drawRoundButton(230, CLK_Y_TRANSPORT, 85, 25,
                  clk.isMaster ? "-> SLAVE" : "-> MASTER",
                  clk.isMaster ? THEME_ACCENT : THEME_PRIMARY);
}

// ------------------------------------------------------------------
//  Full screen draw
// ------------------------------------------------------------------
void drawMidiClockMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("MIDI CLOCK", clk.isMaster ? "Master" : "Slave");
  drawClockBeatIndicator();
  drawClockInfo();
  drawClockControls();
}

// ------------------------------------------------------------------
//  Init
// ------------------------------------------------------------------
void initializeMidiClockMode() {
  clk.isMaster          = true;
  clk.isRunning         = false;
  clk.bpm               = 120.0f;
  clk.lastPulseMicros   = 0;
  clk.pulseCount        = 0;
  clk.beatPulse         = 0;
  clk.lastSlaveClockUs  = 0;
  clk.slaveWindowIdx    = 0;
  clk.slaveWindowFill   = 0;
  clk.slaveBpm          = 0.0f;
  clk.slaveClockActive  = false;
  clk.lastSlaveActivityMs = 0;
  clk.needsRedraw       = false;
  clockRecalcInterval();
}

// ------------------------------------------------------------------
//  Handle – called every loop() while in MIDI_CLOCK mode
// ------------------------------------------------------------------
void handleMidiClockMode() {
  // Back button
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    if (clk.isRunning) {
      sendRealTime(MIDI_STOP);
      clk.isRunning = false;
    }
    exitToMenu();
    return;
  }

  if (touch.justPressed) {
    // START
    if (isButtonPressed(5, CLK_Y_TRANSPORT, 70, 25)) {
      if (clk.isMaster && !clk.isRunning) {
        clk.isRunning       = true;
        clk.pulseCount      = 0;
        clk.beatPulse       = 0;
        clk.lastPulseMicros = micros();
        sendRealTime(MIDI_START);
        clk.needsRedraw = true;
      }
      return;
    }
    // STOP
    if (isButtonPressed(80, CLK_Y_TRANSPORT, 65, 25)) {
      if (clk.isRunning) {
        clk.isRunning = false;
        if (clk.isMaster) sendRealTime(MIDI_STOP);
        clk.needsRedraw = true;
      }
      return;
    }
    // CONTINUE
    if (isButtonPressed(150, CLK_Y_TRANSPORT, 75, 25)) {
      if (clk.isMaster && !clk.isRunning) {
        clk.isRunning       = true;
        clk.lastPulseMicros = micros();
        sendRealTime(MIDI_CONT);
        clk.needsRedraw = true;
      }
      return;
    }
    // MASTER ↔ SLAVE toggle
    if (isButtonPressed(230, CLK_Y_TRANSPORT, 85, 25)) {
      if (clk.isRunning) {
        sendRealTime(MIDI_STOP);
        clk.isRunning = false;
      }
      clk.isMaster         = !clk.isMaster;
      clk.slaveWindowFill  = 0;
      clk.slaveWindowIdx   = 0;
      clk.slaveClockActive = false;
      clk.lastSlaveClockUs = 0;
      clk.slaveBpm         = 0.0f;
      drawMidiClockMode();
      return;
    }

    // BPM nudge buttons (master only)
    if (clk.isMaster) {
      bool changed = false;
      if (isButtonPressed(5,   CLK_Y_NUDGE, 50, 25)) { clk.bpm = max(20.0f,  clk.bpm - 10.0f); changed = true; }
      if (isButtonPressed(58,  CLK_Y_NUDGE, 50, 25)) { clk.bpm = max(20.0f,  clk.bpm -  1.0f); changed = true; }
      if (isButtonPressed(111, CLK_Y_NUDGE, 50, 25)) { clk.bpm = max(20.0f,  clk.bpm -  0.1f); changed = true; }
      if (isButtonPressed(164, CLK_Y_NUDGE, 50, 25)) { clk.bpm = min(300.0f, clk.bpm +  0.1f); changed = true; }
      if (isButtonPressed(217, CLK_Y_NUDGE, 50, 25)) { clk.bpm = min(300.0f, clk.bpm +  1.0f); changed = true; }
      if (isButtonPressed(270, CLK_Y_NUDGE, 45, 25)) { clk.bpm = min(300.0f, clk.bpm + 10.0f); changed = true; }
      if (changed) {
        clockRecalcInterval();
        clk.needsRedraw = true;
        return;
      }
    }
  }

  // Master clock tick (micros-accurate, accumulating to avoid drift)
  if (clk.isMaster && clk.isRunning) {
    unsigned long now = micros();
    if (now - clk.lastPulseMicros >= clk.pulseIntervalUs) {
      clk.lastPulseMicros += clk.pulseIntervalUs;
      sendRealTime(MIDI_CLOCK);
      clk.pulseCount++;
      clk.beatPulse = (clk.beatPulse + 1) % PPQN;
      if (clk.beatPulse == 0) {
        drawClockBeatIndicator();
        // Refresh pulse counter in-place
        tft.fillRect(220, CLK_Y_BADGES, 95, 25, THEME_BG);
        char pbuf[14];
        snprintf(pbuf, sizeof(pbuf), "%lu p", clk.pulseCount % 1000000);
        tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
        tft.drawString(pbuf, 222, CLK_Y_BADGES + 8, 1);
      }
    }
  }

  // DIN bytes handled by central reader in loop(); clockReceiveByte() called there.
  // Detect clock loss in slave mode (no pulse for >2 seconds)
  if (!clk.isMaster) {
    if (clk.slaveClockActive && millis() - clk.lastSlaveActivityMs > 2000) {
      clk.slaveClockActive = false;
      clk.slaveWindowFill  = 0;
      clk.slaveBpm         = 0.0f;
      clk.needsRedraw      = true;
    }
  }

  // Partial redraw on state change
  if (clk.needsRedraw) {
    clk.needsRedraw = false;
    drawClockInfo();
    drawClockControls();
  }
}

#endif
