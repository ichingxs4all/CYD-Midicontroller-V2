#ifndef SD_RECORDER_MODE_H
#define SD_RECORDER_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "sd_card.h"

// ------------------------------------------------------------------
//  MIDI Recorder
//  Captures all incoming MIDI (BLE + DIN) to a simple event log file.
//  Format: CSV text  →  timestamp_ms, status_hex, d1, d2
//  Files saved to /recordings/ on SD.
// ------------------------------------------------------------------

#define SDREC_DIR        "/recordings"
#define SDREC_MAX_DIRS    16     // max entries in the folder picker

bool          sdRecRecording   = false;
File          sdRecFile;
unsigned long sdRecStartMs     = 0;
unsigned long sdRecLastEventMs = 0;
uint32_t      sdRecEventCount  = 0;
char          sdRecFilename[32] = "";
String        sdRecStatus      = "";
bool          sdRecNeedsRedraw = false;

// Target directory for new recordings (default: /recordings)
String        sdRecCurrentPath = SDREC_DIR;

// Folder-picker overlay state
bool          sdRecPickingDir  = false;
String        sdRecPickPath    = SDREC_DIR;   // path being navigated in picker
String        sdRecPickEntries[SDREC_MAX_DIRS];
int           sdRecPickCount   = 0;
int           sdRecPickScroll  = 0;
#define SDREC_PICK_ROWS  4
#define SDREC_PICK_ROW_H 26

// Populate picker entries for sdRecPickPath
void sdRecPickRefresh() {
  int offset = 0;
  if (sdRecPickPath != String(SDREC_DIR)) {
    sdRecPickEntries[0] = "..";
    offset = 1;
  }
  // List subdirectories only (dirs that can hold recordings)
  File root = sdMounted ? SD.open(sdRecPickPath.c_str()) : File();
  sdRecPickCount = offset;
  if (root && root.isDirectory()) {
    File f = root.openNextFile();
    while (f && sdRecPickCount < SDREC_MAX_DIRS) {
      if (f.isDirectory()) {
        const char* raw = f.name();
        String sraw(raw);
        int ls = sraw.lastIndexOf('/');
        String base = (ls >= 0) ? sraw.substring(ls + 1) : sraw;
        if (base.length() > 0 && base[0] != '.') {
          sdRecPickEntries[sdRecPickCount++] = base + "/";
        }
      }
      f.close();
      f = root.openNextFile();
    }
    root.close();
  }
}

void drawSDRecorderDirPicker() {
  // Semi-transparent overlay
  tft.fillRoundRect(8, 48, 304, 160, 6, THEME_SURFACE);
  tft.drawRoundRect(8, 48, 304, 160, 6, THEME_PRIMARY);
  tft.setTextColor(THEME_TEXT, THEME_SURFACE);
  tft.drawCentreString("RECORD TO FOLDER", 160, 53, 1);
  // Current nav path
  tft.setTextColor(THEME_ACCENT, THEME_SURFACE);
  String pl = sdRecPickPath;
  if (pl.length() > 30) pl = "..." + pl.substring(pl.length()-27);
  tft.drawString(pl, 12, 63, 1);
  // Entries
  for (int i = 0; i < SDREC_PICK_ROWS; i++) {
    int idx = sdRecPickScroll + i;
    int ry  = 74 + i * SDREC_PICK_ROW_H;
    bool isParent = (idx < sdRecPickCount && sdRecPickEntries[idx] == "..");
    bool isDir    = (idx < sdRecPickCount && !isParent && sdRecPickEntries[idx].endsWith("/"));
    tft.fillRect(12, ry, 214, SDREC_PICK_ROW_H - 2,
                 idx < sdRecPickCount ? THEME_BG : THEME_SURFACE);
    if (idx < sdRecPickCount) {
      tft.setTextColor(isParent ? THEME_TEXT_DIM : THEME_ACCENT, THEME_BG);
      String label = sdRecPickEntries[idx];
      if (isParent)   label = "< ..";
      else if (isDir) label = "> " + label.substring(0, label.length() - 1);
      tft.drawString(label, 16, ry + 4, 1);
    }
  }
  drawRoundButton(232, 74,  78, 25, "UP",     THEME_SECONDARY);
  drawRoundButton(232, 103, 78, 25, "DOWN",   THEME_SECONDARY);
  drawRoundButton(232, 132, 78, 25, "SELECT", THEME_SUCCESS);
  drawRoundButton(232, 161, 78, 25, "CANCEL", THEME_WARNING);
}

// Called from BLEMidiReceiveCallbacks and DIN reader in loop()
void sdRecordEvent(byte status, byte d1, byte d2) {
  if (!sdRecRecording || !sdRecFile) return;
  unsigned long ts = millis() - sdRecStartMs;
  char line[40];
  snprintf(line, sizeof(line), "%lu,%02X,%d,%d\n", ts, status, d1, d2);
  sdRecFile.print(line);
  sdRecEventCount++;
  sdRecLastEventMs = millis();
  sdRecNeedsRedraw = true;
}

void sdRecStart() {
  if (!sdMounted) { sdRecStatus = "No SD card"; return; }
  SD.mkdir(sdRecCurrentPath.c_str());
  // Filename = rec_NNNN.csv, auto-increment within current directory
  for (int n = 1; n < 1000; n++) {
    snprintf(sdRecFilename, sizeof(sdRecFilename), "%s/rec_%04d.csv",
             sdRecCurrentPath.c_str(), n);
    if (!SD.exists(sdRecFilename)) break;
  }
  sdRecFile = SD.open(sdRecFilename, FILE_WRITE);
  if (!sdRecFile) { sdRecStatus = "Create failed"; return; }
  sdRecFile.println("time_ms,status,d1,d2");
  sdRecStartMs    = millis();
  sdRecEventCount = 0;
  sdRecRecording  = true;
  sdRecStatus     = "REC: " + String(sdRecFilename);
}

void sdRecStop() {
  if (sdRecFile) {
    sdRecFile.flush();
    sdRecFile.close();
  }
  sdRecRecording = false;
  sdRecStatus    = "Saved: " + String(sdRecFilename) + " (" + String(sdRecEventCount) + " events)";
}

// ------------------------------------------------------------------
//  Draw
// ------------------------------------------------------------------
void drawSDRecorderMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("MIDI REC", sdRecRecording ? "● RECORDING" : "Ready");

  // Target-folder row (only shown when not recording)
  tft.fillRect(0, 36, 320, 22, THEME_SURFACE);
  tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
  String dirLabel = sdRecCurrentPath;
  if (dirLabel.length() > 32) dirLabel = "..." + dirLabel.substring(dirLabel.length()-29);
  tft.drawString(dirLabel, 6, 42, 1);
  if (!sdRecRecording) {
    drawRoundButton(265, 37, 50, 20, "CHG DIR", THEME_SECONDARY);
  }

  // Big REC indicator
  if (sdRecRecording) {
    tft.fillCircle(160, 105, 35, THEME_ERROR);
    tft.drawCircle(160, 105, 35, THEME_TEXT);
    tft.setTextColor(THEME_TEXT, THEME_ERROR);
    tft.drawCentreString("REC", 160, 97, 4);
  } else {
    tft.drawCircle(160, 105, 35, THEME_TEXT_DIM);
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawCentreString("REC", 160, 97, 4);
  }

  // Event counter
  if (sdRecRecording) {
    tft.setTextColor(THEME_TEXT, THEME_BG);
    unsigned long elapsed = (millis() - sdRecStartMs) / 1000;
    char buf[30];
    snprintf(buf, sizeof(buf), "%lu events  %02lu:%02lu",
             sdRecEventCount, elapsed / 60, elapsed % 60);
    tft.drawCentreString(buf, 160, 150, 2);
  }

  // Transport buttons
  if (!sdRecRecording) {
    drawRoundButton(80, 175, 160, 35, "START REC", THEME_ERROR);
  } else {
    drawRoundButton(80, 175, 160, 35, "STOP REC", THEME_SUCCESS);
  }

  // Status
  tft.fillRect(0, 215, 320, 13, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  String s = sdRecStatus;
  if (s.length() > 42) s = s.substring(0, 39) + "...";
  tft.drawString(s, 4, 216, 1);

  drawSDStatus(sdMounted);
}

void initializeSDRecorderMode() {
  sdRecCurrentPath = SDREC_DIR;
  sdRecPickingDir  = false;
  sdRecRecording   = false;
  sdRecEventCount  = 0;
  sdRecStatus      = "";
  sdRecNeedsRedraw = false;
}

void handleSDRecorderMode() {
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    if (sdRecPickingDir) { sdRecPickingDir = false; drawSDRecorderMode(); return; }
    if (sdRecRecording) sdRecStop();
    exitToMenu();
    return;
  }

  // ---- Folder picker overlay ----
  if (sdRecPickingDir) {
    if (!touch.justPressed) return;
    // UP scroll
    if (isButtonPressed(232, 74, 78, 25)) {
      if (sdRecPickScroll > 0) { sdRecPickScroll--; drawSDRecorderDirPicker(); } return;
    }
    // DOWN scroll
    if (isButtonPressed(232, 103, 78, 25)) {
      if (sdRecPickScroll + SDREC_PICK_ROWS < sdRecPickCount) { sdRecPickScroll++; drawSDRecorderDirPicker(); } return;
    }
    // SELECT — use current picker path as the target recording directory
    if (isButtonPressed(232, 132, 78, 25)) {
      sdRecCurrentPath = sdRecPickPath;
      sdRecPickingDir  = false;
      drawSDRecorderMode(); return;
    }
    // CANCEL
    if (isButtonPressed(232, 161, 78, 25)) {
      sdRecPickPath   = sdRecCurrentPath;
      sdRecPickingDir = false;
      drawSDRecorderMode(); return;
    }
    // Entry tap — navigate
    for (int i = 0; i < SDREC_PICK_ROWS; i++) {
      if (isButtonPressed(12, 74 + i * SDREC_PICK_ROW_H, 214, SDREC_PICK_ROW_H - 2)) {
        int idx = sdRecPickScroll + i;
        if (idx < sdRecPickCount) {
          String sel = sdRecPickEntries[idx];
          if (sel == "..") {
            int sl = sdRecPickPath.lastIndexOf('/');
            sdRecPickPath = (sl > 0) ? sdRecPickPath.substring(0, sl) : String(SDREC_DIR);
          } else if (sel.endsWith("/")) {
            sdRecPickPath = sdRecPickPath + "/" + sel.substring(0, sel.length() - 1);
          }
          sdRecPickScroll = 0;
          sdRecPickRefresh();
          drawSDRecorderDirPicker();
        }
        return;
      }
    }
    return;
  }

  // CHG DIR button (only when not recording)
  if (touch.justPressed && !sdRecRecording && isButtonPressed(265, 37, 50, 20)) {
    sdInit();
    sdRecPickPath   = sdRecCurrentPath;
    sdRecPickScroll = 0;
    sdRecPickRefresh();
    sdRecPickingDir = true;
    drawSDRecorderDirPicker();
    return;
  }

  // Periodic display refresh while recording
  if (sdRecRecording && sdRecNeedsRedraw) {
    static unsigned long lastDraw = 0;
    if (millis() - lastDraw > 500) {
      sdRecNeedsRedraw = false;
      // Update counter only, avoid full redraw flicker
      tft.fillRect(0, 145, 320, 22, THEME_BG);
      tft.setTextColor(THEME_TEXT, THEME_BG);
      unsigned long elapsed = (millis() - sdRecStartMs) / 1000;
      char buf[30];
      snprintf(buf, sizeof(buf), "%lu events  %02lu:%02lu",
               sdRecEventCount, elapsed / 60, elapsed % 60);
      tft.drawCentreString(buf, 160, 150, 2);
      lastDraw = millis();
    }
  }

  if (!touch.justPressed) return;

  if (isButtonPressed(80, 175, 160, 35)) {
    sdInit();
    if (sdRecRecording) {
      sdRecStop();
    } else {
      sdRecStart();
    }
    drawSDRecorderMode();
    return;
  }
}

#endif
