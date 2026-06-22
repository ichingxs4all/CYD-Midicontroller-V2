#ifndef SD_SETLIST_MODE_H
#define SD_SETLIST_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "sd_card.h"
#include "pgmchange_mode.h"

// ------------------------------------------------------------------
//  Setlist Player
//  Reads a plain-text setlist file from /setlists/ on SD.
//  Each line: <channel>,<program>,<name>
//  Example:   1,0,Grand Piano
//             1,40,Violin
//  Step through entries with PREV/NEXT; SEND fires the program change.
// ------------------------------------------------------------------

#define SDSET_DIR       "/setlists"
#define SDSET_MAX_FILES  10
#define SDSET_MAX_ITEMS  64

struct SetlistItem {
  int    channel;
  int    program;
  String name;
};

String      sdSetFiles[SDSET_MAX_FILES];
int         sdSetFileCount = 0;
int         sdSetFileSelected = 0;
int         sdSetFileScrollTop = 0;
String      sdSetCurrentPath = SDSET_DIR;  // browsed directory

SetlistItem sdSetItems[SDSET_MAX_ITEMS];
int         sdSetItemCount = 0;
int         sdSetCurrent   = 0;     // current position in loaded setlist
bool        sdSetLoaded    = false;
String      sdSetStatus    = "";
String      sdSetLoadedName = "";

void sdSetRefreshFiles() {
  int offset = 0;
  if (sdSetCurrentPath != String(SDSET_DIR)) {
    sdSetFiles[0] = "..";
    offset = 1;
  }
  sdSetFileCount = offset + sdListDirEntries(sdSetCurrentPath.c_str(),
                                             sdSetFiles + offset,
                                             SDSET_MAX_FILES - offset);
}

void sdSetLoad(const String& filename) {
  if (!sdMounted) { sdSetStatus = "No SD card"; return; }
  String path = sdSetCurrentPath + "/" + filename;
  File f = SD.open(path.c_str());
  if (!f) { sdSetStatus = "Open failed"; return; }

  sdSetItemCount = 0;
  while (f.available() && sdSetItemCount < SDSET_MAX_ITEMS) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line[0] == '#') continue;
    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) continue;
    sdSetItems[sdSetItemCount].channel = line.substring(0, c1).toInt();
    sdSetItems[sdSetItemCount].program = line.substring(c1+1, c2).toInt();
    sdSetItems[sdSetItemCount].name    = line.substring(c2+1);
    sdSetItemCount++;
  }
  f.close();
  sdSetCurrent    = 0;
  sdSetLoaded     = sdSetItemCount > 0;
  sdSetLoadedName = filename;
  sdSetStatus     = sdSetLoaded ? "Loaded " + String(sdSetItemCount) + " entries" : "Empty file";
}

void sdSetSendCurrent() {
  if (!sdSetLoaded || sdSetItemCount == 0) return;
  SetlistItem& item = sdSetItems[sdSetCurrent];
  sendProgramChange(item.program, item.channel);
  sdSetStatus = "Sent: " + item.name + " (ch" + String(item.channel) + " pgm" + String(item.program) + ")";
}

// ------------------------------------------------------------------
//  Draw helpers
// ------------------------------------------------------------------
void drawSDSetlistEntry(int idx, int y, bool current, bool highlight) {
  tft.fillRect(4, y, 312, 28, highlight ? THEME_PRIMARY : (current ? THEME_SURFACE : THEME_BG));
  if (idx >= sdSetItemCount) return;
  SetlistItem& item = sdSetItems[idx];
  tft.setTextColor(highlight ? THEME_BG : (current ? THEME_TEXT : THEME_TEXT_DIM),
                   highlight ? THEME_PRIMARY : (current ? THEME_SURFACE : THEME_BG));
  char buf[40];
  snprintf(buf, sizeof(buf), "%2d  Ch%-2d  Pgm%-3d  %s",
           idx+1, item.channel, item.program, item.name.c_str());
  String s = buf;
  if (s.length() > 30) s = s.substring(0, 27) + "...";
  tft.drawString(s, 8, y + 7, 1);
}

#define SDSET_VIS_ROWS   4
#define SDSET_LIST_Y    55
#define SDSET_FILES_VIS  3

void drawSDSetlistMode() {
  tft.fillScreen(THEME_BG);

  if (!sdSetLoaded) {
    // File picker view
    drawHeader("SETLIST", "Choose a file");
    sdSetRefreshFiles();
    for (int i = 0; i < SDSET_FILES_VIS; i++) {
      int idx = sdSetFileScrollTop + i;
      int rowY = 55 + i * 30;
      bool sel = (idx == sdSetFileSelected);
      bool isParent = (idx < sdSetFileCount && sdSetFiles[idx] == "..");
      bool isDir    = (idx < sdSetFileCount && !isParent && sdSetFiles[idx].endsWith("/"));
      uint16_t bg   = sel ? THEME_PRIMARY : THEME_SURFACE;
      uint16_t fg   = sel ? THEME_BG : (isParent ? THEME_TEXT_DIM : (isDir ? THEME_ACCENT : THEME_TEXT));
      tft.fillRect(4, rowY, 255, 27, bg);
      tft.setTextColor(fg, bg);
      if (idx < sdSetFileCount) {
        String label = sdSetFiles[idx];
        if (isParent)      label = "< ..";
        else if (isDir)    label = "> " + label.substring(0, label.length() - 1);
        tft.drawString(label, 8, rowY + 7, 2);
      }
    }
    drawRoundButton(265, 55,  50, 27, "UP",   THEME_SECONDARY);
    drawRoundButton(265, 86,  50, 27, "DOWN", THEME_SECONDARY);
    drawRoundButton(265, 120, 50, 27, "LOAD", THEME_SUCCESS);
  } else {
    // Setlist playback view
    drawHeader("SETLIST", sdSetLoadedName);
    for (int i = 0; i < SDSET_VIS_ROWS; i++) {
      int idx = max(0, sdSetCurrent - 1) + i;
      int rowY = SDSET_LIST_Y + i * 30;
      drawSDSetlistEntry(idx, rowY, idx == sdSetCurrent, false);
    }
    drawRoundButton(4,   180, 70, 30, "< PREV", THEME_SECONDARY);
    drawRoundButton(125, 180, 70, 30, "SEND",   THEME_SUCCESS);
    drawRoundButton(246, 180, 70, 30, "NEXT >", THEME_SECONDARY);
    drawRoundButton(265, 55,  50, 27, "CLOSE",  THEME_WARNING);

    // Current entry big display
    if (sdSetItemCount > 0) {
      SetlistItem& cur = sdSetItems[sdSetCurrent];
      tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
      tft.drawString(String(sdSetCurrent + 1) + "/" + String(sdSetItemCount), 4, 215, 1);
    }
  }

  tft.fillRect(0, 228, 320, 12, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  String s = sdSetStatus;
  if (s.length() > 42) s = s.substring(0, 39) + "...";
  tft.drawString(s, 4, 229, 1);
}

void initializeSDSetlistMode() {
  sdSetCurrentPath   = SDSET_DIR;
  sdSetLoaded        = false;
  sdSetItemCount     = 0;
  sdSetCurrent       = 0;
  sdSetStatus        = "";
  sdSetFileSelected  = 0;
  sdSetFileScrollTop = 0;
}

void handleSDSetlistMode() {
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    exitToMenu();
    return;
  }
  if (!touch.justPressed) return;

  if (!sdSetLoaded) {
    // File picker
    if (isButtonPressed(265, 55, 50, 27)) {
      if (sdSetFileScrollTop > 0) sdSetFileScrollTop--;
      drawSDSetlistMode(); return;
    }
    if (isButtonPressed(265, 86, 50, 27)) {
      if (sdSetFileScrollTop + SDSET_FILES_VIS < sdSetFileCount) sdSetFileScrollTop++;
      drawSDSetlistMode(); return;
    }
    if (isButtonPressed(265, 120, 50, 27)) {
      // LOAD: only load if selected entry is an actual file
      sdInit();
      if (sdSetFileCount > 0) {
        String sel = sdSetFiles[sdSetFileSelected];
        if (!sel.endsWith("/") && sel != "..") {
          sdSetLoad(sel);
        }
      }
      drawSDSetlistMode(); return;
    }
    for (int i = 0; i < SDSET_FILES_VIS; i++) {
      if (isButtonPressed(4, 55 + i * 30, 255, 27)) {
        int idx = sdSetFileScrollTop + i;
        if (idx < sdSetFileCount) {
          String sel = sdSetFiles[idx];
          if (sel == "..") {
            int sl = sdSetCurrentPath.lastIndexOf('/');
            sdSetCurrentPath = (sl > 0) ? sdSetCurrentPath.substring(0, sl)
                                        : String(SDSET_DIR);
            sdSetFileSelected = 0; sdSetFileScrollTop = 0;
            sdSetRefreshFiles();
          } else if (sel.endsWith("/")) {
            sdSetCurrentPath = sdSetCurrentPath + "/" + sel.substring(0, sel.length() - 1);
            sdSetFileSelected = 0; sdSetFileScrollTop = 0;
            sdSetRefreshFiles();
          } else {
            sdSetFileSelected = idx;
          }
          drawSDSetlistMode();
        }
        return;
      }
    }
  } else {
    // Playback controls
    if (isButtonPressed(265, 55, 50, 27)) {
      sdSetLoaded = false;
      sdSetItemCount = 0;
      sdInit();
      sdSetRefreshFiles();
      drawSDSetlistMode(); return;
    }
    if (isButtonPressed(4, 180, 70, 30)) {
      if (sdSetCurrent > 0) sdSetCurrent--;
      drawSDSetlistMode(); return;
    }
    if (isButtonPressed(246, 180, 70, 30)) {
      if (sdSetCurrent < sdSetItemCount - 1) sdSetCurrent++;
      drawSDSetlistMode(); return;
    }
    if (isButtonPressed(125, 180, 70, 30)) {
      sdSetSendCurrent();
      drawSDSetlistMode(); return;
    }
  }
}

#endif
