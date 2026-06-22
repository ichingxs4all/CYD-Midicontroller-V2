#ifndef MONITOR_MODE_H
#define MONITOR_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// ---------------------------------------------------------------
//  MIDI Monitor – displays incoming MIDI events from BLE and DIN
// ---------------------------------------------------------------

#define MONITOR_MAX_EVENTS  10   // rows visible on screen
#define MONITOR_LOG_SIZE    64   // ring-buffer depth

struct MidiEvent {
  byte   status;   // full status byte (cmd | channel)
  byte   data1;
  byte   data2;
  bool   valid;    // false = empty slot
};

// Ring buffer
MidiEvent monitorLog[MONITOR_LOG_SIZE];
int       monitorHead        = 0;   // next write position
int       monitorCount       = 0;   // total events logged (capped at LOG_SIZE)
bool      monitorPaused      = false;
bool      monitorNeedsRedraw = false;

// ------------------------------------------------------------------
//  Helper: push one event into the ring buffer
// ------------------------------------------------------------------
// Forward declaration - sdRecordEvent defined in sd_recorder_mode.h (included after this)
void sdRecordEvent(byte status, byte data1, byte data2);

void monitorPushEvent(byte status, byte data1, byte data2) {
  if (monitorPaused) return;
  monitorLog[monitorHead] = { status, data1, data2, true };
  monitorHead = (monitorHead + 1) % MONITOR_LOG_SIZE;
  if (monitorCount < MONITOR_LOG_SIZE) monitorCount++;
  monitorNeedsRedraw = true;
  sdRecordEvent(status, data1, data2);  // recorder receives fully assembled events
}

// NOTE: BLE MIDI receive is handled by BLEMidiReceiveCallbacks in the main .ino,
// which calls monitorPushEvent() directly. No callback class needed here.

// ------------------------------------------------------------------
//  Decode status byte → human-readable type string (8 chars wide)
// ------------------------------------------------------------------
String monitorEventType(byte status) {
  byte cmd = status & 0xF0;
  switch (cmd) {
    case 0x80: return "NOTE OFF";
    case 0x90: return "NOTE ON ";
    case 0xA0: return "POLY AT ";
    case 0xB0: return "CC      ";
    case 0xC0: return "PROG CHG";
    case 0xD0: return "CHAN AT ";
    case 0xE0: return "PITCH   ";
    default:
      switch (status) {
        case 0xF0: return "SYSEX   ";
        case 0xF2: return "SONG POS";
        case 0xF3: return "SONG SEL";
        case 0xF6: return "TUNE REQ";
        case 0xF8: return "CLOCK   ";
        case 0xFA: return "START   ";
        case 0xFB: return "CONTINUE";
        case 0xFC: return "STOP    ";
        case 0xFE: return "ACT SNS ";
        case 0xFF: return "RESET   ";
        default:   return "SYS     ";
      }
  }
}

// Decode data bytes into a readable value string
String monitorEventData(MidiEvent& ev) {
  byte cmd = ev.status & 0xF0;
  char buf[24];
  switch (cmd) {
    case 0x80:
    case 0x90: {
      String note = getNoteNameFromMIDI(ev.data1);
      snprintf(buf, sizeof(buf), "%-4s vel%3d", note.c_str(), ev.data2);
      return String(buf);
    }
    case 0xB0:
      snprintf(buf, sizeof(buf), "CC%3d = %3d", ev.data1, ev.data2);
      return String(buf);
    case 0xC0:
      snprintf(buf, sizeof(buf), "Prog %3d", ev.data1);
      return String(buf);
    case 0xD0:
      snprintf(buf, sizeof(buf), "Press %3d", ev.data1);
      return String(buf);
    case 0xE0: {
      int bend = (int)(ev.data1 | ((int)ev.data2 << 7)) - 8192;
      snprintf(buf, sizeof(buf), "%+6d", bend);
      return String(buf);
    }
    case 0xA0:
      snprintf(buf, sizeof(buf), "%-3s = %3d", getNoteNameFromMIDI(ev.data1).c_str(), ev.data2);
      return String(buf);
    default:
      if (ev.data1 == 0 && ev.data2 == 0) return "";
      snprintf(buf, sizeof(buf), "%02X %02X", ev.data1, ev.data2);
      return String(buf);
  }
}

// Colour per message type
uint16_t monitorEventColor(byte status) {
  byte cmd = status & 0xF0;
  switch (cmd) {
    case 0x90: return THEME_SUCCESS;    // Note On   – green
    case 0x80: return THEME_ERROR;      // Note Off  – red
    case 0xB0: return THEME_PRIMARY;    // CC        – blue
    case 0xE0: return THEME_ACCENT;     // Pitch     – cyan
    case 0xC0: return THEME_WARNING;    // Prog Chg  – yellow
    case 0xD0: return THEME_SECONDARY;  // Chan AT   – orange
    case 0xA0: return THEME_SECONDARY;  // Poly AT   – orange
    default:   return THEME_TEXT_DIM;   // System    – grey
  }
}

// ------------------------------------------------------------------
//  Draw the event log table
// ------------------------------------------------------------------
#define MON_ROW_H   14
#define MON_TABLE_Y 50

void drawMonitorTable() {
  tft.fillRect(0, MON_TABLE_Y, 320, MONITOR_MAX_EVENTS * MON_ROW_H + 14, THEME_BG);

  // Column headers
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("#",    4,   MON_TABLE_Y, 1);
  tft.drawString("CH",   18,  MON_TABLE_Y, 1);
  tft.drawString("TYPE", 42,  MON_TABLE_Y, 1);
  tft.drawString("DATA", 120, MON_TABLE_Y, 1);
  tft.drawString("RAW",  225, MON_TABLE_Y, 1);
  tft.drawFastHLine(0, MON_TABLE_Y + 10, 320, THEME_SURFACE);

  int rowY = MON_TABLE_Y + 13;

  // Walk backwards from head → newest event at top
  int shown = 0;
  for (int i = 0; i < MONITOR_LOG_SIZE && shown < MONITOR_MAX_EVENTS; i++) {
    int idx = ((monitorHead - 1 - i) + MONITOR_LOG_SIZE) % MONITOR_LOG_SIZE;
    MidiEvent& ev = monitorLog[idx];
    if (!ev.valid) break;

    uint16_t col = monitorEventColor(ev.status);

    // Row index
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString(String(shown + 1), 4, rowY, 1);

    // MIDI channel (1-based) or "–" for system messages
    if ((ev.status & 0xF0) < 0xF0) {
      tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
      tft.drawString(String((ev.status & 0x0F) + 1), 18, rowY, 1);
    } else {
      tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
      tft.drawString("-", 20, rowY, 1);
    }

    // Event type (colour-coded)
    tft.setTextColor(col, THEME_BG);
    tft.drawString(monitorEventType(ev.status), 38, rowY, 1);

    // Decoded data
    tft.setTextColor(THEME_TEXT, THEME_BG);
    tft.drawString(monitorEventData(ev), 120, rowY, 1);

    // Raw hex
    char raw[12];
    if (ev.data1 == 0 && ev.data2 == 0)
      snprintf(raw, sizeof(raw), "%02X", ev.status);
    else
      snprintf(raw, sizeof(raw), "%02X %02X %02X", ev.status, ev.data1, ev.data2);
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString(raw, 225, rowY, 1);

    rowY += MON_ROW_H;
    shown++;
  }
}

// ------------------------------------------------------------------
//  Full screen redraw
// ------------------------------------------------------------------
void drawMonitorMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("MIDI MONITOR", deviceConnected ? "BLE+DIN IN" : "BLE Waiting...");

  drawMonitorTable();

  // Bottom control bar
  drawRoundButton(10,  207, 65, 28, monitorPaused ? "RESUME" : "PAUSE",
                  monitorPaused ? THEME_SUCCESS : THEME_WARNING);
  drawRoundButton(85,  207, 60, 28, "CLEAR", THEME_ERROR);

  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("Events: " + String(monitorCount), 200, 214, 1);
}

// ------------------------------------------------------------------
//  DIN MIDI parser – simple 3-byte state machine
// ------------------------------------------------------------------
static byte dinStatus  = 0;
static byte dinData1   = 0;
static int  dinExpect  = 0;   // data bytes still needed for current message

void monitorParseDIN(byte b) {
  if (b & 0x80) {
    // Status byte
    dinStatus = b;
    byte cmd  = b & 0xF0;
    if (b >= 0xF8)                          { monitorPushEvent(b, 0, 0); dinExpect = 0; return; }
    if (cmd == 0xC0 || cmd == 0xD0 || b == 0xF3) { dinExpect = 1; return; }
    dinExpect = 2;
  } else {
    // Data byte
    if (dinExpect == 2) {
      dinData1 = b;
      dinExpect = 1;
    } else if (dinExpect == 1) {
      byte cmd = dinStatus & 0xF0;
      bool twoByteMsg = (cmd == 0xC0 || cmd == 0xD0 || dinStatus == 0xF3);
      monitorPushEvent(dinStatus, twoByteMsg ? b : dinData1, twoByteMsg ? 0 : b);
      dinExpect = 0; // reset; running status reuses dinStatus automatically
    }
  }
}

// ------------------------------------------------------------------
//  Init
// ------------------------------------------------------------------
void initializeMonitorMode() {
  for (int i = 0; i < MONITOR_LOG_SIZE; i++) monitorLog[i].valid = false;
  monitorHead        = 0;
  monitorCount       = 0;
  monitorPaused      = false;
  monitorNeedsRedraw = false;
  dinStatus          = 0;
  dinData1           = 0;
  dinExpect          = 0;
}

// ------------------------------------------------------------------
//  Handle – called every loop() iteration while in MONITOR mode
// ------------------------------------------------------------------
void handleMonitorMode() {
  // Back button
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    exitToMenu();
    return;
  }

  if (touch.justPressed) {
    // Pause / Resume
    if (isButtonPressed(10, 207, 65, 28)) {
      monitorPaused = !monitorPaused;
      drawRoundButton(10, 207, 65, 28, monitorPaused ? "RESUME" : "PAUSE",
                      monitorPaused ? THEME_SUCCESS : THEME_WARNING);
      return;
    }
    // Clear log
    if (isButtonPressed(85, 207, 60, 28)) {
      initializeMonitorMode();
      drawMonitorMode();
      return;
    }
  }

  // DIN MIDI bytes are drained by the central reader in loop().
  // monitorParseDIN() is called there for every incoming byte.

  // Refresh only when new data arrived (avoids constant flicker)
  if (monitorNeedsRedraw) {
    monitorNeedsRedraw = false;
    drawMonitorTable();
    // Update event counter in-place
    tft.fillRect(148, 210, 170, 14, THEME_BG);
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString("Events: " + String(monitorCount), 200, 214, 1);
  }
}

#endif
