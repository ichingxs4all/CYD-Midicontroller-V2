#ifndef SD_CARD_H
#define SD_CARD_H

#include "common_definitions.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// ------------------------------------------------------------------
//  SD Card shared utilities
//  CYD uses default VSPI pins: SCK=18, MISO=19, MOSI=23, CS(SS)=5
// ------------------------------------------------------------------

bool sdMounted = false;

// Standard directories — defined here so all SD modes can reference them
extern const char* SD_DIRS[];
extern const int   SD_NUM_DIRS;

void sdCreateDirs();   // forward — implemented in sd_info_mode.h

bool sdInit() {
  if (sdMounted) return true;

  // The CYD touchscreen uses VSPI (mySpi, pins 25/39/32/33).
  // SD card uses HSPI routed to the SD slot's physical pins 18/19/23/5.
  // The ESP32 GPIO matrix allows HSPI to use any GPIO, so this is valid.
  static SPIClass spi(HSPI);
  spi.begin(18, 19, 23, 5);  // SCK=18, MISO=19, MOSI=23, CS=5

  SD.end();
  if (!SD.begin(5, spi, 40000000)) {
    Serial.println("SD: mount failed");
    sdMounted = false;
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    Serial.println("SD: no card");
    sdMounted = false;
    return false;
  }
  sdMounted = true;
  Serial.printf("SD: mounted, %lluMB\n", SD.cardSize() / (1024*1024));
  sdCreateDirs();
  if (currentMode == MENU) drawMenu();  // refresh SD status in header
  return true;
}

void sdUnmount() {
  SD.end();
  sdMounted = false;
  if (currentMode == MENU) drawMenu();  // refresh SD status in header
}

// List files in a directory into a String array, return count
int sdListFiles(const char* dir, String* names, int maxFiles) {
  if (!sdMounted) return 0;
  File root = SD.open(dir);
  if (!root || !root.isDirectory()) return 0;
  int count = 0;
  File f = root.openNextFile();
  while (f && count < maxFiles) {
    if (!f.isDirectory()) {
      names[count++] = String(f.name());
    }
    f = root.openNextFile();
  }
  return count;
}

// List BOTH files and subdirectories into a String array.
// Directory entries are stored with a trailing "/" (e.g. "BEATS/").
// Dot-files and hidden entries are skipped.
// entry.name() may return the full absolute path on Arduino-ESP32 >= 2.x;
// we always extract the basename so callers get just the entry name.
// Returns total count (dirs + files combined).
int sdListDirEntries(const char* dir, String* names, int maxEntries) {
  if (!sdMounted) return 0;
  File root = SD.open(dir);
  if (!root || !root.isDirectory()) return 0;
  int count = 0;
  File f = root.openNextFile();
  while (f && count < maxEntries) {
    const char* raw = f.name();
    // Extract bare filename — Arduino-ESP32 >= 2.x returns the full absolute
    // path ("/midi/BEATS"), older versions return just the basename ("BEATS").
    String sraw(raw);
    int lastSlash = sraw.lastIndexOf('/');
    String base = (lastSlash >= 0) ? sraw.substring(lastSlash + 1) : sraw;
    // Skip hidden / macOS dot-files / System Volume Information
    if (base.length() > 0 && base[0] != '.') {
      names[count++] = f.isDirectory() ? (base + "/") : base;
    }
    f.close();
    f = root.openNextFile();
  }
  root.close();
  return count;
}

// Draw a standard SD status bar at the bottom of a screen
void drawSDStatus(bool mounted) {
  tft.fillRect(0, 228, 320, 12, THEME_BG);
  tft.setTextColor(mounted ? THEME_SUCCESS : THEME_ERROR, THEME_BG);
  if (mounted) {
    char buf[40];
    uint64_t used  = SD.usedBytes()  / (1024*1024);
    uint64_t total = SD.totalBytes() / (1024*1024);
    snprintf(buf, sizeof(buf), "SD: %lluMB / %lluMB", used, total);
    tft.drawString(buf, 6, 229, 1);
  } else {
    tft.drawString("SD: No card", 6, 229, 1);
  }
}

// Draw a scrollable file list. Returns index of tapped item (-1 if none).
// files[]: array of filenames, count: number of files
// Directory entries end with "/" (e.g. "BEATS/"); ".." is the go-up entry.
// startIdx: first visible index, visRows: how many rows fit
#define FILELIST_ROW_H 22
int drawFileList(String* files, int count, int startIdx, int visRows,
                 int listX, int listY, int listW,
                 int selectedIdx) {
  for (int i = 0; i < visRows; i++) {
    int fileIdx = startIdx + i;
    int rowY = listY + i * FILELIST_ROW_H;
    bool sel = (fileIdx == selectedIdx);

    bool isParent = (fileIdx < count && files[fileIdx] == "..");
    bool isDir    = (fileIdx < count && !isParent && files[fileIdx].endsWith("/"));

    uint16_t bg   = (fileIdx < count) ? (sel ? THEME_PRIMARY : THEME_SURFACE) : THEME_BG;
    tft.fillRect(listX, rowY, listW, FILELIST_ROW_H - 2, bg);

    if (fileIdx < count) {
      uint16_t fg;
      if      (sel)      fg = THEME_BG;
      else if (isParent) fg = THEME_TEXT_DIM;
      else if (isDir)    fg = THEME_ACCENT;
      else               fg = THEME_TEXT;
      tft.setTextColor(fg, bg);

      // Build display label
      String name = files[fileIdx];
      if (isParent) {
        name = "< ..";
      } else if (isDir) {
        name = "> " + name.substring(0, name.length() - 1);  // strip trailing /
      }
      if (name.length() > 22) name = name.substring(0, 19) + "...";
      tft.drawString(name, listX + 4, rowY + 4, 1);
    }
  }
  return -1; // touch handled by caller
}

#endif
