#ifndef SD_MENU_MODE_H
#define SD_MENU_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "sd_card.h"

// ------------------------------------------------------------------
//  SD Card sub-menu — 6 apps in a 2×3 grid
// ------------------------------------------------------------------

struct SDSubApp {
  const char* name;
  const char* symbol;
  uint16_t    color;
  AppMode     mode;
};

SDSubApp sdSubApps[] = {
  { "SEQ",     "♫", 0x07E0, SD_SEQ_MODE      },  // green
  { "PLAY",    "▶", 0x001F, SD_PLAYER_MODE   },  // blue
  { "REC",     "●", 0xF800, SD_RECORDER_MODE },  // red
  { "SETLIST", "≡", 0xFD20, SD_SETLIST_MODE  },  // orange
  { "INFO",    "i", 0x7BEF, SD_INFO_MODE     },  // grey
  { "SYSX",    "S", 0xF81F, SD_SYSEX_MODE    },  // magenta
};
const int SD_NUM_SUBAPPS = 6;

// Grid: 2 rows × 3 cols, iconSize=44, spacing=8, labelH=14
#define SDMENU_ICON_SIZE  44
#define SDMENU_SPACING     8
#define SDMENU_COLS        3
#define SDMENU_ROWS        2
#define SDMENU_STRIDE_X   (SDMENU_ICON_SIZE + SDMENU_SPACING)
#define SDMENU_STRIDE_Y   (SDMENU_ICON_SIZE + SDMENU_SPACING + 14)  // 14px for label

static int sdMenuStartX() {
  return (320 - SDMENU_COLS * SDMENU_ICON_SIZE - (SDMENU_COLS - 1) * SDMENU_SPACING) / 2;
}

// Forward declarations — defined in sd_info_mode.h (included after this file)
void sdFormatAndInit();
int  sdDeleteDotFiles();

// Overlay for destructive confirmations inside the SD menu
enum SDMenuOverlay { SDMENU_NONE, SDMENU_CONFIRM_FORMAT, SDMENU_CONFIRM_DOTDEL };
SDMenuOverlay sdMenuOverlay = SDMENU_NONE;
String        sdMenuStatus  = "";

void drawSDMenuOverlay() {
  tft.fillRoundRect(25, 80, 270, 110, 8, THEME_SURFACE);
  if (sdMenuOverlay == SDMENU_CONFIRM_FORMAT) {
    tft.drawRoundRect(25, 80, 270, 110, 8, THEME_ERROR);
    tft.setTextColor(THEME_ERROR, THEME_SURFACE);
    tft.drawCentreString("FORMAT SD CARD?", 160, 88, 2);
    tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
    tft.drawCentreString("ALL files deleted.", 160, 110, 1);
    tft.drawCentreString("Standard dirs recreated.", 160, 123, 1);
    drawRoundButton(40,  148, 90, 30, "CANCEL", THEME_SECONDARY);
    drawRoundButton(190, 148, 90, 30, "FORMAT", THEME_ERROR);
  } else if (sdMenuOverlay == SDMENU_CONFIRM_DOTDEL) {
    tft.drawRoundRect(25, 80, 270, 110, 8, THEME_WARNING);
    tft.setTextColor(THEME_WARNING, THEME_SURFACE);
    tft.drawCentreString("Delete macOS dot-files?", 160, 88, 2);
    tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
    tft.drawCentreString("Removes ._files and .DS_Store", 160, 110, 1);
    tft.drawCentreString("from all directories.", 160, 123, 1);
    drawRoundButton(40,  148, 90, 30, "CANCEL", THEME_SECONDARY);
    drawRoundButton(190, 148, 90, 30, "DELETE", THEME_WARNING);
  }
}

void drawSDMenuStatus() {
  tft.fillRect(0, 215, 320, 13, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString(sdMenuStatus, 4, 216, 1);
}

void drawSDMenuMode() {
  tft.fillScreen(THEME_BG);

  // Header
  tft.fillRect(0, 0, 320, 48, THEME_SURFACE);
  tft.drawFastHLine(0, 48, 320, THEME_PRIMARY);
  tft.setTextColor(THEME_PRIMARY, THEME_SURFACE);
  tft.drawCentreString("FILES", 160, 5, 4);
  if (sdMounted) {
    tft.setTextColor(THEME_SUCCESS, THEME_SURFACE);
    tft.drawCentreString("● Card ready", 160, 30, 2);
  } else {
    tft.setTextColor(THEME_ERROR, THEME_SURFACE);
    tft.drawCentreString("○ No card", 160, 30, 2);
  }
  drawRoundButton(10, 10, 50, 25, "BACK", THEME_ERROR);

  int startX = sdMenuStartX();
  int startY = 52;

  for (int i = 0; i < SD_NUM_SUBAPPS; i++) {
    int col = i % SDMENU_COLS;
    int row = i / SDMENU_COLS;
    int x = startX + col * SDMENU_STRIDE_X;
    int y = startY + row * SDMENU_STRIDE_Y;

    tft.fillRoundRect(x, y, SDMENU_ICON_SIZE, SDMENU_ICON_SIZE, 8, sdSubApps[i].color);
    tft.drawRoundRect(x, y, SDMENU_ICON_SIZE, SDMENU_ICON_SIZE, 8, THEME_TEXT);

    tft.setTextColor(THEME_BG, sdSubApps[i].color);
    tft.drawCentreString(sdSubApps[i].symbol,
                         x + SDMENU_ICON_SIZE / 2,
                         y + SDMENU_ICON_SIZE / 2 - 8, 4);

    tft.setTextColor(THEME_TEXT, THEME_BG);
    tft.drawCentreString(sdSubApps[i].name,
                         x + SDMENU_ICON_SIZE / 2,
                         y + SDMENU_ICON_SIZE + 3, 1);
  }

  // Separator + action buttons below grid
  int sepY  = startY + SDMENU_ROWS * SDMENU_STRIDE_Y - 2;
  int btnY  = sepY + 4;
  tft.drawFastHLine(0, sepY, 320, THEME_SURFACE);
  drawRoundButton(  4, btnY,  96, 28, "FORMAT",    THEME_ERROR);
  drawRoundButton(104, btnY,  96, 28, "DEL .FILES", THEME_WARNING);
  drawRoundButton(204, btnY, 112, 28, "USB XFER",  THEME_PRIMARY);

  drawSDMenuStatus();
  drawSDStatus(sdMounted);
  if (sdMenuOverlay != SDMENU_NONE) drawSDMenuOverlay();
}

void initializeSDMenuMode() {
  sdMenuOverlay = SDMENU_NONE;
  sdMenuStatus  = "";
  sdInit();
}

void handleSDMenuMode() {
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    sdMenuOverlay = SDMENU_NONE;
    exitToMenu();
    return;
  }
  if (!touch.justPressed) return;

  // Handle overlay confirms
  if (sdMenuOverlay != SDMENU_NONE) {
    if (isButtonPressed(40, 148, 90, 30)) {           // CANCEL
      sdMenuOverlay = SDMENU_NONE;
      drawSDMenuMode();
      return;
    }
    if (isButtonPressed(190, 148, 90, 30)) {           // CONFIRM
      if (sdMenuOverlay == SDMENU_CONFIRM_FORMAT) {
        sdMenuStatus = "Formatting...";
        drawSDMenuStatus();
        sdFormatAndInit();
        sdMenuStatus = "Format done. Dirs recreated.";
      } else if (sdMenuOverlay == SDMENU_CONFIRM_DOTDEL) {
        int n = sdDeleteDotFiles();
        sdMenuStatus = "Deleted " + String(n) + " dot-file(s)";
      }
      sdMenuOverlay = SDMENU_NONE;
      drawSDMenuMode();
      return;
    }
    return;  // eat all other touches
  }

  // Calculate button positions (must match drawSDMenuMode)
  int startX = sdMenuStartX();
  int startY = 52;
  int sepY   = startY + SDMENU_ROWS * SDMENU_STRIDE_Y - 2;
  int btnY   = sepY + 4;

  // FORMAT button
  if (isButtonPressed(4, btnY, 96, 28)) {
    sdMenuOverlay = SDMENU_CONFIRM_FORMAT;
    drawSDMenuMode();
    return;
  }
  // DEL .FILES button
  if (isButtonPressed(104, btnY, 96, 28)) {
    sdMenuOverlay = SDMENU_CONFIRM_DOTDEL;
    drawSDMenuMode();
    return;
  }
  // USB XFER button
  if (isButtonPressed(204, btnY, 112, 28)) {
    enterMode(SD_FILETRANSFER_MODE);
    return;
  }

  // Icon grid
  for (int i = 0; i < SD_NUM_SUBAPPS; i++) {
    int col = i % SDMENU_COLS;
    int row = i / SDMENU_COLS;
    int x = startX + col * SDMENU_STRIDE_X;
    int y = startY + row * SDMENU_STRIDE_Y;
    if (isButtonPressed(x, y, SDMENU_ICON_SIZE, SDMENU_ICON_SIZE)) {
      enterMode(sdSubApps[i].mode);
      return;
    }
  }
}

#endif
