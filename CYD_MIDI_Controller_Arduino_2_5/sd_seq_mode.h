#ifndef SD_SEQ_MODE_H
#define SD_SEQ_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "sd_card.h"

// ------------------------------------------------------------------
//  SD Sequencer Save/Load
//  Saves/loads the BEATS 4×16 pattern to /patterns/ on SD.
//  File format: 4×16 = 64 bytes, one byte per step (0=off, 1=on).
// ------------------------------------------------------------------

extern bool sequencePattern[4][16];  // from sequencer_mode.h

#define SDSEQ_DIR      "/patterns"
#define SDSEQ_MAX_FILES 20
#define SDSEQ_VIS_ROWS   6

String sdSeqFiles[SDSEQ_MAX_FILES];
int    sdSeqFileCount = 0;
int    sdSeqSelected  = 0;
int    sdSeqScrollTop = 0;
char   sdSeqNewName[20] = "pattern1";
bool   sdSeqNaming = false;  // true = name-entry mode
String sdSeqStatus = "";

// Simple on-screen keyboard characters
const char sdSeqKeyChars[] = "abcdefghijklmnopqrstuvwxyz0123456789_-";
#define SDSEQ_KEY_COLS 13
#define SDSEQ_KEY_ROWS  3

void sdSeqRefreshList() {
  sdSeqFileCount = sdListFiles(SDSEQ_DIR, sdSeqFiles, SDSEQ_MAX_FILES);
  if (sdSeqSelected >= sdSeqFileCount) sdSeqSelected = max(0, sdSeqFileCount - 1);
}

void sdSeqSave(const char* name) {
  if (!sdMounted) { sdSeqStatus = "No SD card"; return; }
  SD.mkdir(SDSEQ_DIR);
  String path = String(SDSEQ_DIR) + "/" + name + ".pat";
  File f = SD.open(path.c_str(), FILE_WRITE);
  if (!f) { sdSeqStatus = "Save failed"; return; }
  for (int t = 0; t < 4; t++)
    for (int s = 0; s < 16; s++)
      f.write(sequencePattern[t][s] ? 1 : 0);
  f.close();
  sdSeqStatus = "Saved: " + String(name);
  sdSeqRefreshList();
}

void sdSeqLoad(const String& filename) {
  if (!sdMounted) { sdSeqStatus = "No SD card"; return; }
  String path = String(SDSEQ_DIR) + "/" + filename;
  File f = SD.open(path.c_str());
  if (!f) { sdSeqStatus = "Load failed"; return; }
  for (int t = 0; t < 4; t++)
    for (int s = 0; s < 16; s++)
      sequencePattern[t][s] = (f.read() == 1);
  f.close();
  sdSeqStatus = "Loaded: " + filename;
}

void sdSeqDelete(const String& filename) {
  if (!sdMounted) return;
  String path = String(SDSEQ_DIR) + "/" + filename;
  SD.remove(path.c_str());
  sdSeqStatus = "Deleted: " + filename;
  sdSeqRefreshList();
}

// ------------------------------------------------------------------
//  Draw
// ------------------------------------------------------------------
void drawSDSeqMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("SEQ FILES", "Save / Load Patterns");

  sdInit();
  sdSeqRefreshList();

  // File list area  y=50..193
  drawFileList(sdSeqFiles, sdSeqFileCount, sdSeqScrollTop,
               SDSEQ_VIS_ROWS, 4, 50, 200, sdSeqSelected);

  // Scroll arrows
  drawRoundButton(210, 50,  50, 28, "UP",   THEME_SECONDARY);
  drawRoundButton(210, 82,  50, 28, "DOWN", THEME_SECONDARY);

  // Action buttons
  drawRoundButton(265, 50,  50, 28, "LOAD", THEME_SUCCESS);
  drawRoundButton(265, 82,  50, 28, "DEL",  THEME_ERROR);
  drawRoundButton(265, 114, 50, 28, "SAVE", THEME_PRIMARY);

  // Name field
  tft.fillRect(4, 185, 200, 20, THEME_SURFACE);
  tft.setTextColor(THEME_TEXT, THEME_SURFACE);
  tft.drawString(String(sdSeqNewName), 8, 188, 1);
  drawRoundButton(210, 183, 105, 22, "EDIT NAME", THEME_ACCENT);

  // Status
  tft.fillRect(0, 208, 320, 18, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString(sdSeqStatus, 4, 210, 1);

  drawSDStatus(sdMounted);
}

// Simple letter picker overlay for naming
void drawSDSeqNaming() {
  tft.fillRoundRect(10, 60, 300, 160, 8, THEME_SURFACE);
  tft.drawRoundRect(10, 60, 300, 160, 8, THEME_PRIMARY);
  tft.setTextColor(THEME_TEXT, THEME_SURFACE);
  tft.drawCentreString("Name: " + String(sdSeqNewName), 160, 66, 2);
  tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
  tft.drawCentreString("Tap letters. DONE to confirm.", 160, 83, 1);
  // Keyboard
  int kx = 14, ky = 95, kw = 21, kh = 22, kgap = 1;
  int idx = 0;
  for (int r = 0; r < SDSEQ_KEY_ROWS; r++) {
    for (int c = 0; c < SDSEQ_KEY_COLS && idx < (int)strlen(sdSeqKeyChars); c++, idx++) {
      char ch[2] = {sdSeqKeyChars[idx], 0};
      drawRoundButton(kx + c*(kw+kgap), ky + r*(kh+kgap), kw, kh, ch, THEME_SECONDARY);
    }
  }
  drawRoundButton(14,  180, 60, 26, "DEL",  THEME_WARNING);
  drawRoundButton(246, 180, 60, 26, "DONE", THEME_SUCCESS);
}

// ------------------------------------------------------------------
//  Init / Handle
// ------------------------------------------------------------------
void initializeSDSeqMode() {
  sdSeqSelected  = 0;
  sdSeqScrollTop = 0;
  sdSeqStatus    = "";
  sdSeqNaming    = false;
  strncpy(sdSeqNewName, "pattern1", sizeof(sdSeqNewName));
}

void handleSDSeqMode() {
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    sdSeqNaming = false;
    exitToMenu();
    return;
  }

  if (sdSeqNaming) {
    if (!touch.justPressed) return;
    // DONE
    if (isButtonPressed(246, 180, 60, 26)) { sdSeqNaming = false; drawSDSeqMode(); return; }
    // DEL
    if (isButtonPressed(14,  180, 60, 26)) {
      int len = strlen(sdSeqNewName);
      if (len > 0) sdSeqNewName[len-1] = '\0';
      drawSDSeqNaming(); return;
    }
    // Letter keys
    int kx = 14, ky = 95, kw = 21, kh = 22, kgap = 1;
    int idx = 0;
    for (int r = 0; r < SDSEQ_KEY_ROWS; r++) {
      for (int c = 0; c < SDSEQ_KEY_COLS && idx < (int)strlen(sdSeqKeyChars); c++, idx++) {
        if (isButtonPressed(kx + c*(kw+kgap), ky + r*(kh+kgap), kw, kh)) {
          int len = strlen(sdSeqNewName);
          if (len < 18) { sdSeqNewName[len] = sdSeqKeyChars[idx]; sdSeqNewName[len+1] = '\0'; }
          drawSDSeqNaming(); return;
        }
      }
    }
    return;
  }

  if (!touch.justPressed) return;

  // Scroll
  if (isButtonPressed(210, 50, 50, 28)) {
    if (sdSeqScrollTop > 0) { sdSeqScrollTop--; drawSDSeqMode(); } return;
  }
  if (isButtonPressed(210, 82, 50, 28)) {
    if (sdSeqScrollTop + SDSEQ_VIS_ROWS < sdSeqFileCount) { sdSeqScrollTop++; drawSDSeqMode(); } return;
  }

  // File list tap
  for (int i = 0; i < SDSEQ_VIS_ROWS; i++) {
    if (isButtonPressed(4, 50 + i * FILELIST_ROW_H, 200, FILELIST_ROW_H - 2)) {
      int idx = sdSeqScrollTop + i;
      if (idx < sdSeqFileCount) { sdSeqSelected = idx; drawSDSeqMode(); }
      return;
    }
  }

  // LOAD
  if (isButtonPressed(265, 50, 50, 28)) {
    if (sdSeqFileCount > 0) sdSeqLoad(sdSeqFiles[sdSeqSelected]);
    drawSDSeqMode(); return;
  }
  // DEL
  if (isButtonPressed(265, 82, 50, 28)) {
    if (sdSeqFileCount > 0) sdSeqDelete(sdSeqFiles[sdSeqSelected]);
    drawSDSeqMode(); return;
  }
  // SAVE
  if (isButtonPressed(265, 114, 50, 28)) {
    sdSeqSave(sdSeqNewName);
    drawSDSeqMode(); return;
  }
  // EDIT NAME
  if (isButtonPressed(210, 183, 105, 22)) {
    sdSeqNaming = true;
    drawSDSeqNaming(); return;
  }
}

#endif
