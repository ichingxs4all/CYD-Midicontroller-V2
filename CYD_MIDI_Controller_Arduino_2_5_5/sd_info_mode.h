#ifndef SD_INFO_MODE_H
#define SD_INFO_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "sd_card.h"

// ------------------------------------------------------------------
//  SD Info & File Browser
//  Shows card info, lets you browse/delete files.
//  FORMAT: recursively removes all files, recreates standard dirs.
//  DEL .-FILES: removes all files whose name starts with '.' (macOS).
// ------------------------------------------------------------------

#define SDINF_MAX_FILES  40
#define SDINF_VIS_ROWS    8

// Standard subdirectory list — used here and by sdCreateDirs()
const char* SD_DIRS[] = {
  "/midi", "/SYSX", "/patterns", "/recordings", "/setlists"
};
const int SD_NUM_DIRS = 5;

String sdInfFiles[SDINF_MAX_FILES];
int    sdInfFileCount  = 0;
int    sdInfSelected   = 0;
int    sdInfScrollTop  = 0;
String sdInfStatus     = "";

// Overlay state
enum SDInfOverlay { SDINF_NONE, SDINF_CONFIRM_DEL, SDINF_CONFIRM_FORMAT, SDINF_CONFIRM_DOTDEL };
SDInfOverlay sdInfOverlay = SDINF_NONE;

// ------------------------------------------------------------------
//  Helpers
// ------------------------------------------------------------------
void sdCreateDirs() {
  for (int i = 0; i < SD_NUM_DIRS; i++) {
    if (!SD.exists(SD_DIRS[i])) {
      SD.mkdir(SD_DIRS[i]);
      Serial.printf("SD: created %s\n", SD_DIRS[i]);
    }
  }
}

// Recursively delete all files (and empty dirs) inside a directory
void sdDeleteDirContents(const char* path) {
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return;
  File f = dir.openNextFile();
  while (f) {
    String fpath = String(path) + "/" + f.name();
    if (f.isDirectory()) {
      sdDeleteDirContents(fpath.c_str());
      SD.rmdir(fpath.c_str());
    } else {
      SD.remove(fpath.c_str());
    }
    f = dir.openNextFile();
  }
}

// Delete all dot-files recursively across all our dirs
int sdDeleteDotFiles() {
  int count = 0;
  for (int d = 0; d < SD_NUM_DIRS; d++) {
    File dir = SD.open(SD_DIRS[d]);
    if (!dir || !dir.isDirectory()) continue;
    File f = dir.openNextFile();
    while (f) {
      String fname = f.name();
      // fname from openNextFile() is the bare filename without path
      if (!f.isDirectory() && fname.length() > 0 && fname[0] == '.') {
        String fpath = String(SD_DIRS[d]) + "/" + fname;
        SD.remove(fpath.c_str());
        Serial.printf("Deleted dot-file: %s\n", fpath.c_str());
        count++;
      }
      f = dir.openNextFile();
    }
  }
  return count;
}

// Wipe everything and recreate clean directory structure
void sdFormatAndInit() {
  Serial.println("SD: formatting (deleting all files)...");
  // Delete contents of each known dir
  for (int d = 0; d < SD_NUM_DIRS; d++) {
    sdDeleteDirContents(SD_DIRS[d]);
    SD.rmdir(SD_DIRS[d]);
  }
  // Also sweep root for any stray files
  sdDeleteDirContents("/");
  // Recreate standard dirs
  sdCreateDirs();
  Serial.println("SD: format complete");
}

void sdInfRefresh() {
  sdInfFileCount = 0;
  if (!sdMounted) return;
  for (int d = 0; d < SD_NUM_DIRS; d++) {
    File root = SD.open(SD_DIRS[d]);
    if (!root || !root.isDirectory()) continue;
    File f = root.openNextFile();
    while (f && sdInfFileCount < SDINF_MAX_FILES) {
      if (!f.isDirectory())
        sdInfFiles[sdInfFileCount++] = String(SD_DIRS[d]) + "/" + f.name();
      f = root.openNextFile();
    }
  }
}

// ------------------------------------------------------------------
//  Overlay drawing
// ------------------------------------------------------------------
void drawSDInfOverlay() {
  tft.fillRoundRect(25, 85, 270, 105, 8, THEME_SURFACE);

  switch (sdInfOverlay) {
    case SDINF_CONFIRM_DEL:
      tft.drawRoundRect(25, 85, 270, 105, 8, THEME_ERROR);
      tft.setTextColor(THEME_TEXT, THEME_SURFACE);
      tft.drawCentreString("Delete file?", 160, 93, 2);
      if (sdInfSelected < sdInfFileCount) {
        String name = sdInfFiles[sdInfSelected];
        int sl = name.lastIndexOf('/');
        if (sl >= 0) name = name.substring(sl + 1);
        if (name.length() > 30) name = name.substring(0, 27) + "...";
        tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
        tft.drawCentreString(name, 160, 114, 1);
      }
      drawRoundButton(40,  148, 90, 30, "CANCEL", THEME_SECONDARY);
      drawRoundButton(190, 148, 90, 30, "DELETE", THEME_ERROR);
      break;

    case SDINF_CONFIRM_FORMAT:
      tft.drawRoundRect(25, 85, 270, 105, 8, THEME_ERROR);
      tft.setTextColor(THEME_ERROR, THEME_SURFACE);
      tft.drawCentreString("FORMAT SD CARD?", 160, 93, 2);
      tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
      tft.drawCentreString("All files will be deleted!", 160, 114, 1);
      tft.drawCentreString("Dirs will be recreated.", 160, 127, 1);
      drawRoundButton(40,  148, 90, 30, "CANCEL", THEME_SECONDARY);
      drawRoundButton(190, 148, 90, 30, "FORMAT", THEME_ERROR);
      break;

    case SDINF_CONFIRM_DOTDEL:
      tft.drawRoundRect(25, 85, 270, 105, 8, THEME_WARNING);
      tft.setTextColor(THEME_WARNING, THEME_SURFACE);
      tft.drawCentreString("Delete macOS dot-files?", 160, 93, 2);
      tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
      tft.drawCentreString("Removes all files starting with '.'", 160, 114, 1);
      tft.drawCentreString("(.DS_Store, ._filename, etc.)", 160, 127, 1);
      drawRoundButton(40,  148, 90, 30, "CANCEL", THEME_SECONDARY);
      drawRoundButton(190, 148, 90, 30, "DELETE", THEME_WARNING);
      break;

    default: break;
  }
}

// ------------------------------------------------------------------
//  Full draw
// ------------------------------------------------------------------
void drawSDInfoMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("SD INFO", "Card & Files");

  if (!sdMounted) {
    tft.setTextColor(THEME_ERROR, THEME_BG);
    tft.drawCentreString("No SD card inserted", 160, 100, 2);
    drawRoundButton(80, 140, 160, 35, "RETRY", THEME_PRIMARY);
    drawSDStatus(false);
    return;
  }

  // Card stats
  uint64_t usedMB  = SD.usedBytes()  / (1024*1024);
  uint64_t totalMB = SD.totalBytes() / (1024*1024);
  char cbuf[60];
  snprintf(cbuf, sizeof(cbuf), "Used:%lluMB  Free:%lluMB  Total:%lluMB",
           usedMB, totalMB - usedMB, totalMB);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString(cbuf, 4, 50, 1);

  // File list header
  tft.drawString("Files", 8, 60, 1);
  tft.drawFastHLine(0, 68, 320, THEME_SURFACE);

  // File list — narrower to leave room for buttons column
  drawFileList(sdInfFiles, sdInfFileCount, sdInfScrollTop,
               SDINF_VIS_ROWS, 4, 70, 248, sdInfSelected);

  // Right-column buttons
  drawRoundButton(258, 70,  58, 24, "UP",     THEME_SECONDARY);
  drawRoundButton(258, 97,  58, 24, "DOWN",   THEME_SECONDARY);
  drawRoundButton(258, 130, 58, 24, "DEL",    THEME_ERROR);
  drawRoundButton(258, 157, 58, 24, "RELOAD", THEME_PRIMARY);

  // Separator before action buttons
  tft.drawFastHLine(0, 187, 320, THEME_SURFACE);

  // Action buttons row: FORMAT  |  DEL .-FILES
  drawRoundButton(4,   191, 150, 28, "FORMAT CARD", THEME_ERROR);
  drawRoundButton(162, 191, 155, 28, "DEL .-FILES",  THEME_WARNING);

  // Status
  tft.fillRect(0, 222, 320, 13, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  String s = sdInfStatus;
  if (s.length() > 45) s = s.substring(0, 42) + "...";
  tft.drawString(s, 4, 223, 1);

  drawSDStatus(sdMounted);

  // Draw overlay on top if active
  if (sdInfOverlay != SDINF_NONE) drawSDInfOverlay();
}

// ------------------------------------------------------------------
//  Init / Handle
// ------------------------------------------------------------------
void initializeSDInfoMode() {
  sdInfSelected  = 0;
  sdInfScrollTop = 0;
  sdInfStatus    = "";
  sdInfOverlay   = SDINF_NONE;
}

void handleSDInfoMode() {
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    sdInfOverlay = SDINF_NONE;
    exitToMenu();
    return;
  }
  if (!touch.justPressed) return;

  if (!sdMounted) {
    if (isButtonPressed(80, 140, 160, 35)) {
      sdInit();
      if (sdMounted) { sdCreateDirs(); sdInfRefresh(); }
      drawSDInfoMode();
    }
    return;
  }

  // Handle overlay confirm dialogs first
  if (sdInfOverlay != SDINF_NONE) {
    // CANCEL (always same position)
    if (isButtonPressed(40, 148, 90, 30)) {
      sdInfOverlay = SDINF_NONE;
      drawSDInfoMode();
      return;
    }
    // CONFIRM action
    if (isButtonPressed(190, 148, 90, 30)) {
      switch (sdInfOverlay) {
        case SDINF_CONFIRM_DEL:
          SD.remove(sdInfFiles[sdInfSelected].c_str());
          sdInfStatus = "Deleted: " + sdInfFiles[sdInfSelected];
          sdInfRefresh();
          if (sdInfSelected >= sdInfFileCount) sdInfSelected = max(0, sdInfFileCount - 1);
          break;
        case SDINF_CONFIRM_FORMAT:
          sdInfStatus = "Formatting...";
          drawSDInfoMode();
          sdFormatAndInit();
          sdInfRefresh();
          sdInfStatus = "Format complete. Dirs recreated.";
          break;
        case SDINF_CONFIRM_DOTDEL: {
          int n = sdDeleteDotFiles();
          sdInfRefresh();
          sdInfStatus = "Deleted " + String(n) + " dot-file(s)";
          break;
        }
        default: break;
      }
      sdInfOverlay = SDINF_NONE;
      drawSDInfoMode();
      return;
    }
    return;  // eat all other touches while overlay is up
  }

  // Scroll
  if (isButtonPressed(258, 70, 58, 24)) {
    if (sdInfScrollTop > 0) { sdInfScrollTop--; drawSDInfoMode(); } return;
  }
  if (isButtonPressed(258, 97, 58, 24)) {
    if (sdInfScrollTop + SDINF_VIS_ROWS < sdInfFileCount) { sdInfScrollTop++; drawSDInfoMode(); } return;
  }

  // DEL single file
  if (isButtonPressed(258, 130, 58, 24)) {
    if (sdInfFileCount > 0) { sdInfOverlay = SDINF_CONFIRM_DEL; drawSDInfoMode(); } return;
  }
  // RELOAD
  if (isButtonPressed(258, 157, 58, 24)) {
    sdInfRefresh(); drawSDInfoMode(); return;
  }

  // FORMAT
  if (isButtonPressed(4, 191, 150, 28)) {
    sdInfOverlay = SDINF_CONFIRM_FORMAT; drawSDInfoMode(); return;
  }
  // DEL .-FILES
  if (isButtonPressed(162, 191, 155, 28)) {
    sdInfOverlay = SDINF_CONFIRM_DOTDEL; drawSDInfoMode(); return;
  }

  // File list tap
  for (int i = 0; i < SDINF_VIS_ROWS; i++) {
    if (isButtonPressed(4, 70 + i * FILELIST_ROW_H, 248, FILELIST_ROW_H - 2)) {
      sdInfSelected = sdInfScrollTop + i;
      if (sdInfSelected >= sdInfFileCount) sdInfSelected = sdInfFileCount - 1;
      drawSDInfoMode(); return;
    }
  }
}

#endif
