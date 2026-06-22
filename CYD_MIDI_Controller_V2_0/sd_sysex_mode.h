#ifndef SD_SYSEX_MODE_H
#define SD_SYSEX_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "sd_card.h"

// ------------------------------------------------------------------
//  SysEx Librarian
//  • Receive: captures F0..F7 sysex from BLE or DIN into a buffer,
//             then lets the user save it to /SYSX/ on SD.
//  • Send:    loads a .syx file and transmits it byte-by-byte on
//             both BLE MIDI and DIN MIDI.
//  • Browse:  scrollable file list with load / send / delete.
//
//  Sysex reception is always-on (like the recorder); the global
//  sysexReceiveByte() is called from the BLE callback and DIN
//  central reader whenever sysex bytes arrive.
// ------------------------------------------------------------------

#define SYSX_DIR         "/SYSX"
#define SYSX_MAX_FILES    30
#define SYSX_VIS_ROWS      7
#define SYSX_BUF_SIZE   4096    // max incoming sysex (4 kB)
#define SYSX_SEND_CHUNK   16    // bytes per BLE packet when sending

// ------------------------------------------------------------------
//  Global sysex receive buffer (assembly across multiple callbacks)
// ------------------------------------------------------------------
uint8_t  sysexRxBuf[SYSX_BUF_SIZE];
uint16_t sysexRxLen   = 0;
bool     sysexRxActive = false;   // currently inside F0..F7
bool     sysexRxReady  = false;   // complete message waiting
unsigned long sysexRxLastByteMs = 0;

// ------------------------------------------------------------------
//  Called byte-by-byte from BLE callback and DIN central reader.
//  Handles the F0...F7 assembly regardless of packet boundaries.
// ------------------------------------------------------------------
void sysexReceiveByte(byte b) {
  if (b == 0xF0) {
    // Start of sysex
    sysexRxLen    = 0;
    sysexRxActive = true;
    sysexRxReady  = false;
    sysexRxBuf[sysexRxLen++] = b;
    sysexRxLastByteMs = millis();
    return;
  }

  if (!sysexRxActive) return;

  // Real-time messages (0xF8-0xFF) are allowed inside sysex — ignore them here,
  // they are handled by the main dispatcher separately.
  if (b >= 0xF8) return;

  // Any other status byte that is not 0xF7 aborts the sysex
  if (b != 0xF7 && (b & 0x80)) {
    sysexRxActive = false;
    sysexRxLen    = 0;
    return;
  }

  if (sysexRxLen < SYSX_BUF_SIZE) {
    sysexRxBuf[sysexRxLen++] = b;
  }
  sysexRxLastByteMs = millis();

  if (b == 0xF7) {
    sysexRxActive = false;
    sysexRxReady  = (sysexRxLen >= 2);  // at least F0 F7
    Serial.printf("SysEx received: %d bytes\n", sysexRxLen);
  }
}

// ------------------------------------------------------------------
//  File list state  (entries ending "/" are directories)
// ------------------------------------------------------------------
String sysexFiles[SYSX_MAX_FILES];
int    sysexFileCount   = 0;
int    sysexSelected    = 0;
int    sysexScrollTop   = 0;
String sysexCurrentPath = SYSX_DIR;   // browsed directory; always rooted at SYSX_DIR
String sysexStatus      = "";
bool   sysexSending     = false;
uint32_t sysexSendPos   = 0;
uint8_t* sysexSendBuf   = nullptr;
uint32_t sysexSendLen   = 0;
unsigned long sysexSendLastMs = 0;
#define SYSX_SEND_INTERVAL_MS  2   // ms between chunks to avoid BLE overflow

char   sysexSaveName[24] = "sysex";
bool   sysexNaming       = false;

// ------------------------------------------------------------------
//  Helpers
// ------------------------------------------------------------------
void sysexRefreshList() {
  int offset = 0;
  if (sysexCurrentPath != String(SYSX_DIR)) {
    sysexFiles[0] = "..";
    offset = 1;
  }
  sysexFileCount = offset + sdListDirEntries(sysexCurrentPath.c_str(),
                                             sysexFiles + offset,
                                             SYSX_MAX_FILES - offset);
  if (sysexSelected >= sysexFileCount) sysexSelected = max(0, sysexFileCount - 1);
}

void sysexSaveToSD(const char* name) {
  if (!sdMounted) { sysexStatus = "No SD card"; return; }
  if (!sysexRxReady || sysexRxLen == 0) { sysexStatus = "No data to save"; return; }
  SD.mkdir(sysexCurrentPath.c_str());
  String path = sysexCurrentPath + "/" + name + ".syx";
  File f = SD.open(path.c_str(), FILE_WRITE);
  if (!f) { sysexStatus = "Save failed"; return; }
  f.write(sysexRxBuf, sysexRxLen);
  f.close();
  sysexStatus = "Saved " + String(sysexRxLen) + "B → " + String(name) + ".syx";
  sysexRefreshList();
}

void sysexDeleteFile(const String& filename) {
  if (!sdMounted) return;
  SD.remove((sysexCurrentPath + "/" + filename).c_str());
  sysexStatus = "Deleted: " + filename;
  sysexRefreshList();
}

// Load a file into a heap buffer ready for sending
bool sysexLoadForSend(const String& filename) {
  if (!sdMounted) { sysexStatus = "No SD card"; return false; }
  String path = sysexCurrentPath + "/" + filename;
  File f = SD.open(path.c_str());
  if (!f) { sysexStatus = "Open failed"; return false; }
  uint32_t sz = f.size();
  if (sz == 0 || sz > 65536) { f.close(); sysexStatus = "File too large"; return false; }
  if (sysexSendBuf) { free(sysexSendBuf); sysexSendBuf = nullptr; }
  sysexSendBuf = (uint8_t*)malloc(sz);
  if (!sysexSendBuf) { f.close(); sysexStatus = "Out of memory"; return false; }
  f.read(sysexSendBuf, sz);
  f.close();
  sysexSendLen = sz;
  sysexSendPos = 0;
  sysexStatus  = "Loaded " + String(sz) + "B: " + filename;
  return true;
}

// Send next chunk of the sysex buffer
void sysexSendTick() {
  if (!sysexSending || !sysexSendBuf || sysexSendPos >= sysexSendLen) {
    sysexSending = false;
    if (sysexSendBuf) { free(sysexSendBuf); sysexSendBuf = nullptr; }
    return;
  }

  unsigned long now = millis();
  if (now - sysexSendLastMs < SYSX_SEND_INTERVAL_MS) return;
  sysexSendLastMs = now;

  // Send up to SYSX_SEND_CHUNK bytes
  uint32_t remaining = sysexSendLen - sysexSendPos;
  uint32_t toSend    = min((uint32_t)SYSX_SEND_CHUNK, remaining);

  // DIN: write raw bytes
  for (uint32_t i = 0; i < toSend; i++) {
    MIDISerial.write(sysexSendBuf[sysexSendPos + i]);
  }

  // BLE: wrap in a BLE-MIDI sysex packet
  // BLE-MIDI sysex: [header] [timestamp] [F0] [data...] [timestamp] [F7]
  // For multi-packet sysex: first packet starts with F0, continuation packets
  // have no status byte after timestamp, final packet ends with [timestamp][F7].
  if (deviceConnected) {
    uint8_t pkt[3 + SYSX_SEND_CHUNK];
    unsigned long t = millis();
    uint8_t hdr = 0x80 | ((t >> 7) & 0x3F);
    uint8_t ts  = 0x80 | (t & 0x7F);
    uint8_t idx = 0;
    pkt[idx++] = hdr;
    pkt[idx++] = ts;
    for (uint32_t i = 0; i < toSend; i++) {
      byte b = sysexSendBuf[sysexSendPos + i];
      // Insert timestamp before F7 per BLE-MIDI spec
      if (b == 0xF7) pkt[idx++] = ts;
      pkt[idx++] = b;
    }
    pCharacteristic->setValue(pkt, idx);
    pCharacteristic->notify();
  }

  sysexSendPos += toSend;

  if (sysexSendPos >= sysexSendLen) {
    sysexSending = false;
    free(sysexSendBuf);
    sysexSendBuf = nullptr;
    sysexStatus  = "Send complete (" + String(sysexSendLen) + "B)";
    Serial.printf("SysEx sent: %d bytes\n", sysexSendLen);
  }
}

// ------------------------------------------------------------------
//  Simple on-screen keyboard for naming
// ------------------------------------------------------------------
const char sysexKeyChars[] = "abcdefghijklmnopqrstuvwxyz0123456789_-";
#define SYSX_KEY_COLS 13
#define SYSX_KEY_ROWS  3

void drawSysexNaming() {
  tft.fillRoundRect(10, 60, 300, 155, 8, THEME_SURFACE);
  tft.drawRoundRect(10, 60, 300, 155, 8, THEME_PRIMARY);
  tft.setTextColor(THEME_TEXT, THEME_SURFACE);
  tft.drawCentreString("Save as: " + String(sysexSaveName), 160, 67, 2);
  tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
  tft.drawCentreString("Tap to name, SAVE to confirm", 160, 84, 1);
  int kx = 14, ky = 93, kw = 21, kh = 22, kgap = 1;
  int idx = 0;
  for (int r = 0; r < SYSX_KEY_ROWS; r++) {
    for (int c = 0; c < SYSX_KEY_COLS && idx < (int)strlen(sysexKeyChars); c++, idx++) {
      char ch[2] = {sysexKeyChars[idx], 0};
      drawRoundButton(kx + c*(kw+kgap), ky + r*(kh+kgap), kw, kh, ch, THEME_SECONDARY);
    }
  }
  drawRoundButton(14,  175, 55, 28, "DEL",  THEME_WARNING);
  drawRoundButton(140, 175, 75, 28, "SAVE", THEME_SUCCESS);
  drawRoundButton(225, 175, 75, 28, "CANCEL", THEME_ERROR);
}

// ------------------------------------------------------------------
//  Draw
// ------------------------------------------------------------------
void drawSDSysexMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("SYSEX", sysexSending ? "Sending..." : (sysexRxReady ? "Ready" : "Waiting"));

  // Receive status box
  tft.fillRoundRect(4, 50, 185, 35, 4, THEME_SURFACE);
  tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
  tft.drawString("RECEIVED:", 8, 53, 1);
  if (sysexRxReady) {
    tft.setTextColor(THEME_SUCCESS, THEME_SURFACE);
    char rbuf[24];
    snprintf(rbuf, sizeof(rbuf), "%d bytes  F0..F7", sysexRxLen);
    tft.drawString(rbuf, 8, 65, 1);
  } else if (sysexRxActive) {
    tft.setTextColor(THEME_WARNING, THEME_SURFACE);
    char rbuf[24];
    snprintf(rbuf, sizeof(rbuf), "Receiving... %dB", sysexRxLen);
    tft.drawString(rbuf, 8, 65, 1);
  } else {
    tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
    tft.drawString("None", 8, 65, 1);
  }

  // Save button (only enabled when data is ready)
  uint16_t saveCol = sysexRxReady ? THEME_SUCCESS : THEME_TEXT_DIM;
  drawRoundButton(195, 50, 60, 16, "SAVE", saveCol);
  drawRoundButton(260, 50, 55, 16, "CLEAR", THEME_WARNING);

  // Save name display
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("Name: " + String(sysexSaveName), 195, 70, 1);
  drawRoundButton(260, 68, 55, 14, "NAME", THEME_ACCENT);

  // File list
  tft.drawFastHLine(0, 90, 320, THEME_SURFACE);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.fillRect(0, 91, 320, 10, THEME_BG);
  tft.drawString("FILES IN " + sysexCurrentPath + "/", 8, 92, 1);
  drawFileList(sysexFiles, sysexFileCount, sysexScrollTop,
               SYSX_VIS_ROWS, 4, 102, 215, sysexSelected);

  // Scroll buttons
  drawRoundButton(222, 102, 45, 25, "UP",   THEME_SECONDARY);
  drawRoundButton(222, 130, 45, 25, "DOWN", THEME_SECONDARY);

  // SEND and DELETE buttons
  uint16_t sendCol = (sysexFileCount > 0) ? THEME_PRIMARY : THEME_TEXT_DIM;
  drawRoundButton(271, 102, 45, 25, "SEND", sendCol);
  drawRoundButton(271, 130, 45, 25, "DEL",  THEME_ERROR);

  // Progress bar when sending
  if (sysexSending && sysexSendLen > 0) {
    int barW = 310;
    int pct  = (sysexSendPos * barW) / sysexSendLen;
    tft.drawRect(5, 260, barW, 8, THEME_TEXT_DIM);
    tft.fillRect(5, 260, pct, 8, THEME_PRIMARY);
  }

  // Status line
  tft.fillRect(0, 215, 320, 13, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  String s = sysexStatus;
  if (s.length() > 45) s = s.substring(0, 42) + "...";
  tft.drawString(s, 4, 216, 1);

  drawSDStatus(sdMounted);
}

// ------------------------------------------------------------------
//  Init / Handle
// ------------------------------------------------------------------
void initializeSDSysexMode() {
  sysexCurrentPath = SYSX_DIR;
  sysexSelected    = 0;
  sysexScrollTop   = 0;
  sysexStatus      = "";
  sysexSending     = false;
  sysexNaming      = false;
  sysexRxReady     = false;
  sysexRxActive    = false;
  sysexRxLen       = 0;
  if (sysexSendBuf) { free(sysexSendBuf); sysexSendBuf = nullptr; }
  strncpy(sysexSaveName, "sysex", sizeof(sysexSaveName));
}

void handleSDSysexMode() {
  // Back
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    sysexSending = false;
    if (sysexSendBuf) { free(sysexSendBuf); sysexSendBuf = nullptr; }
    exitToMenu();
    return;
  }

  // Naming overlay
  if (sysexNaming) {
    if (!touch.justPressed) return;
    if (isButtonPressed(225, 175, 75, 28)) { sysexNaming = false; drawSDSysexMode(); return; } // CANCEL
    if (isButtonPressed(14,  175, 55, 28)) {  // DEL
      int len = strlen(sysexSaveName);
      if (len > 0) sysexSaveName[len-1] = '\0';
      drawSysexNaming(); return;
    }
    if (isButtonPressed(140, 175, 75, 28)) {  // SAVE
      sysexSaveToSD(sysexSaveName);
      sysexNaming = false;
      drawSDSysexMode(); return;
    }
    int kx = 14, ky = 93, kw = 21, kh = 22, kgap = 1;
    int idx = 0;
    for (int r = 0; r < SYSX_KEY_ROWS; r++) {
      for (int c = 0; c < SYSX_KEY_COLS && idx < (int)strlen(sysexKeyChars); c++, idx++) {
        if (isButtonPressed(kx + c*(kw+kgap), ky + r*(kh+kgap), kw, kh)) {
          int len = strlen(sysexSaveName);
          if (len < 22) { sysexSaveName[len] = sysexKeyChars[idx]; sysexSaveName[len+1] = '\0'; }
          drawSysexNaming(); return;
        }
      }
    }
    return;
  }

  // Tick send
  if (sysexSending) {
    sysexSendTick();
    // Update progress bar in-place without full redraw
    if (sysexSendLen > 0) {
      int barW = 310;
      int pct  = (sysexSendPos * barW) / sysexSendLen;
      tft.fillRect(5, 260, pct, 8, THEME_PRIMARY);
      tft.fillRect(5 + pct, 260, barW - pct, 8, THEME_BG);
      tft.drawRect(5, 260, barW, 8, THEME_TEXT_DIM);
    }
    if (!sysexSending) {
      // Send just finished — redraw status
      tft.fillRect(0, 215, 320, 13, THEME_BG);
      tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
      tft.drawString(sysexStatus, 4, 216, 1);
    }
  }

  // Refresh receive box if data arriving
  if (sysexRxActive || sysexRxReady) {
    static unsigned long lastRxDraw = 0;
    if (millis() - lastRxDraw > 200) {
      lastRxDraw = millis();
      tft.fillRoundRect(4, 50, 185, 35, 4, THEME_SURFACE);
      tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
      tft.drawString("RECEIVED:", 8, 53, 1);
      if (sysexRxReady) {
        tft.setTextColor(THEME_SUCCESS, THEME_SURFACE);
        char rbuf[24];
        snprintf(rbuf, sizeof(rbuf), "%d bytes  F0..F7", sysexRxLen);
        tft.drawString(rbuf, 8, 65, 1);
        // Enable save button
        drawRoundButton(195, 50, 60, 16, "SAVE", THEME_SUCCESS);
      } else {
        tft.setTextColor(THEME_WARNING, THEME_SURFACE);
        char rbuf[24];
        snprintf(rbuf, sizeof(rbuf), "Receiving... %dB", sysexRxLen);
        tft.drawString(rbuf, 8, 65, 1);
      }
    }
  }

  // Timeout: abort incomplete sysex after 2 seconds of silence
  if (sysexRxActive && millis() - sysexRxLastByteMs > 2000) {
    sysexRxActive = false;
    sysexRxLen    = 0;
    sysexStatus   = "Receive timeout";
    tft.fillRect(0, 215, 320, 13, THEME_BG);
    tft.setTextColor(THEME_ERROR, THEME_BG);
    tft.drawString(sysexStatus, 4, 216, 1);
  }

  if (!touch.justPressed) return;

  // SAVE button (only if data ready)
  if (isButtonPressed(195, 50, 60, 16) && sysexRxReady) {
    sysexNaming = true;
    drawSysexNaming(); return;
  }
  // CLEAR receive buffer
  if (isButtonPressed(260, 50, 55, 16)) {
    sysexRxReady  = false;
    sysexRxActive = false;
    sysexRxLen    = 0;
    sysexStatus   = "Buffer cleared";
    drawSDSysexMode(); return;
  }
  // NAME button
  if (isButtonPressed(260, 68, 55, 14)) {
    sysexNaming = true;
    drawSysexNaming(); return;
  }

  // Scroll
  if (isButtonPressed(222, 102, 45, 25)) {
    if (sysexScrollTop > 0) { sysexScrollTop--; drawSDSysexMode(); } return;
  }
  if (isButtonPressed(222, 130, 45, 25)) {
    if (sysexScrollTop + SYSX_VIS_ROWS < sysexFileCount) { sysexScrollTop++; drawSDSysexMode(); } return;
  }

  // File list tap — navigate into dirs, select files
  for (int i = 0; i < SYSX_VIS_ROWS; i++) {
    if (isButtonPressed(4, 102 + i * FILELIST_ROW_H, 215, FILELIST_ROW_H - 2)) {
      int idx = sysexScrollTop + i;
      if (idx < sysexFileCount) {
        String sel = sysexFiles[idx];
        if (sel == "..") {
          int sl = sysexCurrentPath.lastIndexOf('/');
          sysexCurrentPath = (sl > 0) ? sysexCurrentPath.substring(0, sl)
                                      : String(SYSX_DIR);
          sysexSelected = 0; sysexScrollTop = 0;
          sysexRefreshList();
        } else if (sel.endsWith("/")) {
          sysexCurrentPath = sysexCurrentPath + "/" + sel.substring(0, sel.length() - 1);
          sysexSelected = 0; sysexScrollTop = 0;
          sysexRefreshList();
        } else {
          sysexSelected = idx;
        }
        drawSDSysexMode();
      }
      return;
    }
  }

  // SEND file (skip if a dir or ".." is selected)
  if (isButtonPressed(271, 102, 45, 25) && sysexFileCount > 0) {
    String sel = sysexFiles[sysexSelected];
    if (!sel.endsWith("/") && sel != "..") {
      sdInit();
      if (sysexLoadForSend(sel)) {
        sysexSending    = true;
        sysexSendLastMs = millis();
      }
      drawSDSysexMode();
    }
    return;
  }
  // DELETE file (skip dirs)
  if (isButtonPressed(271, 130, 45, 25) && sysexFileCount > 0) {
    String sel = sysexFiles[sysexSelected];
    if (!sel.endsWith("/") && sel != "..") {
      sysexDeleteFile(sel);
      drawSDSysexMode();
    }
    return;
  }
}

#endif
