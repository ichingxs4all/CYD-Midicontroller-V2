#ifndef WIFI_SETTINGS_MODE_H
#define WIFI_SETTINGS_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"

// ------------------------------------------------------------------
//  WiFi / AppleMIDI settings sub-menu
//  Entered from the main Settings page via the WIFI button.
// ------------------------------------------------------------------

// Which field is being keyboard-edited
enum WifiEditField { WIFI_EDIT_NONE, WIFI_EDIT_SSID, WIFI_EDIT_PASS };
WifiEditField wifiEditField = WIFI_EDIT_NONE;

// Temporary edit buffers
char wifiEditBuf[33] = "";

// Keyboard layout
const char wifiKeyRows[4][11] = {
  "qwertyuiop",
  "asdfghjkl ",   // trailing space used as spacebar
  "zxcvbnm   ",
  "1234567890"
};
const int WIFI_KEY_ROWS = 4;
const int WIFI_KEY_COLS = 10;

// ------------------------------------------------------------------
//  On-screen keyboard overlay
// ------------------------------------------------------------------
void drawWifiKeyboard(const char* fieldName, const char* currentVal) {
  tft.fillRect(0, 80, 320, 160, THEME_SURFACE);
  tft.drawFastHLine(0, 80, 320, THEME_PRIMARY);

  // Field name + current value
  tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
  tft.drawString(fieldName, 6, 83, 1);
  // Value with cursor
  tft.fillRect(60, 80, 255, 18, THEME_BG);
  tft.setTextColor(THEME_TEXT, THEME_BG);
  String display = String(currentVal) + "_";
  if (display.length() > 26) display = display.substring(display.length() - 26);
  tft.drawString(display, 62, 83, 1);

  // Keyboard rows
  int kx0 = 4, kw = 29, kh = 22, kgap = 2;
  int ky   = 101;
  for (int r = 0; r < WIFI_KEY_ROWS; r++) {
    for (int c = 0; c < WIFI_KEY_COLS; c++) {
      char ch = wifiKeyRows[r][c];
      char label[2] = {ch == ' ' ? '_' : ch, 0};
      drawRoundButton(kx0 + c*(kw+kgap), ky + r*(kh+kgap), kw, kh, label, THEME_SECONDARY);
    }
  }

  // Action buttons at bottom
  drawRoundButton(4,   200, 60, 28, "DEL",    THEME_WARNING);
  drawRoundButton(68,  200, 60, 28, "SPACE",  THEME_SECONDARY);
  drawRoundButton(132, 200, 60, 28, "CLEAR",  THEME_ERROR);
  drawRoundButton(200, 200, 55, 28, "SHIFT",  THEME_SECONDARY);
  drawRoundButton(260, 200, 55, 28, "DONE",   THEME_SUCCESS);
}

bool wifiKeyShift = false;

void handleWifiKeyboard() {
  if (!touch.justPressed) return;

  int kx0 = 4, kw = 29, kh = 22, kgap = 2, ky = 101;

  // Action buttons
  if (isButtonPressed(4, 200, 60, 28)) {   // DEL
    int len = strlen(wifiEditBuf);
    if (len > 0) wifiEditBuf[len-1] = '\0';
    drawWifiKeyboard(wifiEditField == WIFI_EDIT_SSID ? "SSID:" : "Password:", wifiEditBuf);
    return;
  }
  if (isButtonPressed(68, 200, 60, 28)) {  // SPACE
    int len = strlen(wifiEditBuf);
    if (len < 32) { wifiEditBuf[len] = ' '; wifiEditBuf[len+1] = '\0'; }
    drawWifiKeyboard(wifiEditField == WIFI_EDIT_SSID ? "SSID:" : "Password:", wifiEditBuf);
    return;
  }
  if (isButtonPressed(132, 200, 60, 28)) { // CLEAR
    wifiEditBuf[0] = '\0';
    drawWifiKeyboard(wifiEditField == WIFI_EDIT_SSID ? "SSID:" : "Password:", wifiEditBuf);
    return;
  }
  if (isButtonPressed(200, 200, 55, 28)) { // SHIFT
    wifiKeyShift = !wifiKeyShift;
    drawWifiKeyboard(wifiEditField == WIFI_EDIT_SSID ? "SSID:" : "Password:", wifiEditBuf);
    return;
  }
  if (isButtonPressed(260, 200, 55, 28)) { // DONE
    if (wifiEditField == WIFI_EDIT_SSID) {
      strncpy(wifiSSID, wifiEditBuf, sizeof(wifiSSID));
    } else {
      strncpy(wifiPassword, wifiEditBuf, sizeof(wifiPassword));
    }
    wifiEditField = WIFI_EDIT_NONE;
    wifiKeyShift  = false;
    drawWifiSettingsMode();
    return;
  }

  // Key grid
  for (int r = 0; r < WIFI_KEY_ROWS; r++) {
    for (int c = 0; c < WIFI_KEY_COLS; c++) {
      if (isButtonPressed(kx0 + c*(kw+kgap), ky + r*(kh+kgap), kw, kh)) {
        char ch = wifiKeyRows[r][c];
        if (ch == ' ') return;  // spacebar handled above
        if (wifiKeyShift && ch >= 'a' && ch <= 'z') ch -= 32;
        int len = strlen(wifiEditBuf);
        if (len < 32) { wifiEditBuf[len] = ch; wifiEditBuf[len+1] = '\0'; }
        drawWifiKeyboard(wifiEditField == WIFI_EDIT_SSID ? "SSID:" : "Password:", wifiEditBuf);
        return;
      }
    }
  }
}

// ------------------------------------------------------------------
//  Main WiFi settings screen
// ------------------------------------------------------------------
void drawWifiSettingsMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("WIFI / MIDI NET", FW_VERSION);

  // WiFi status
  tft.fillRect(0, 50, 320, 22, wifiConnected ? THEME_SUCCESS : THEME_SURFACE);
  tft.setTextColor(wifiConnected ? THEME_BG : THEME_TEXT_DIM,
                   wifiConnected ? THEME_SUCCESS : THEME_SURFACE);
  if (wifiConnected) {
    tft.drawString("● " + wifiStatusText + "  " + wifiIPText, 6, 54, 1);
  } else {
    tft.drawString("○ " + wifiStatusText, 6, 54, 1);
    if (wifiConnected == false && WiFi.status() == WL_CONNECTED) {
      tft.drawString(WiFi.localIP().toString(), 200, 54, 1);
    }
  }

  // SSID row
  int y = 78;
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("SSID:", 6, y + 6, 1);
  tft.fillRoundRect(55, y, 185, 24, 4, THEME_SURFACE);
  tft.setTextColor(THEME_TEXT, THEME_SURFACE);
  String ssidDisplay = (strlen(wifiSSID) == 0) ? "(not set)" : String(wifiSSID);
  if (ssidDisplay.length() > 20) ssidDisplay = ssidDisplay.substring(0, 17) + "...";
  tft.drawString(ssidDisplay, 59, y + 6, 1);
  drawRoundButton(246, y, 68, 24, "EDIT", THEME_PRIMARY);

  // Password row
  y = 108;
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("Pass:", 6, y + 6, 1);
  tft.fillRoundRect(55, y, 185, 24, 4, THEME_SURFACE);
  tft.setTextColor(THEME_TEXT, THEME_SURFACE);
  String passDisplay = (strlen(wifiPassword) == 0) ? "(not set)" : String(strlen(wifiPassword)) + " chars  ••••••••";
  tft.drawString(passDisplay, 59, y + 6, 1);
  drawRoundButton(246, y, 68, 24, "EDIT", THEME_PRIMARY);

  // Separator
  tft.drawFastHLine(0, 138, 320, THEME_SURFACE);

  // AppleMIDI toggle
  drawToggleRow(142, "APPLE MIDI", "RTP-MIDI over WiFi network", appleMidiEnabled);

  // Auto-connect toggle
  drawToggleRow(178, "AUTO CONNECT", "Connect WiFi on power-on", wifiAutoConnect);

  // Separator
  tft.drawFastHLine(0, 213, 320, THEME_SURFACE);

  // Connect / Disconnect button
  if (wifiConnected) {
    drawRoundButton(10, 218, 140, 20, "DISCONNECT", THEME_ERROR);
  } else {
    drawRoundButton(10, 218, 140, 20, "CONNECT", THEME_SUCCESS);
  }
  drawRoundButton(160, 218, 150, 20, "SCAN NETWORKS", THEME_ACCENT);
}

// ------------------------------------------------------------------
//  Network scan overlay
// ------------------------------------------------------------------
bool wifiShowScan = false;
String wifiScanResults[8];
int    wifiScanCount = 0;
int    wifiScanSelected = 0;

bool wifiScanning = false;   // async scan in progress

void doWifiScan() {
  Serial.println("[SCAN] doWifiScan() called");
  wifiStatusText = "Scanning...";
  wifiShowScan   = false;
  wifiScanning   = true;
  wifiScanCount  = 0;

  int currentMode = WiFi.getMode();
  Serial.printf("[SCAN] WiFi mode before scan: %d\n", currentMode);
  if (currentMode == WIFI_OFF || currentMode == WIFI_MODE_NULL) {
    Serial.printf("[SCAN] Free heap before WiFi.mode: %d\n", ESP.getFreeHeap());
    Serial.println("[SCAN] WiFi off, calling WiFi.mode(WIFI_STA)...");
    WiFi.mode(WIFI_STA);
    delay(100);
    Serial.println("[SCAN] WiFi.mode done");
  }

  Serial.println("[SCAN] Calling WiFi.scanDelete()...");
  WiFi.scanDelete();
  Serial.println("[SCAN] Calling WiFi.scanNetworks(async)...");
  int ret = WiFi.scanNetworks(true, true);
  Serial.printf("[SCAN] WiFi.scanNetworks returned: %d\n", ret);

  tft.fillRect(0, 50, 320, 22, THEME_SURFACE);
  tft.setTextColor(THEME_WARNING, THEME_SURFACE);
  tft.drawString("○ Scanning...", 6, 54, 1);
  Serial.println("[SCAN] doWifiScan() done");
}

void drawWifiScanOverlay() {
  tft.fillRoundRect(10, 55, 300, 182, 8, THEME_SURFACE);
  tft.drawRoundRect(10, 55, 300, 182, 8, THEME_ACCENT);
  tft.setTextColor(THEME_TEXT, THEME_SURFACE);
  tft.drawCentreString("Networks found:", 160, 62, 2);
  tft.drawFastHLine(10, 78, 300, THEME_TEXT_DIM);

  // Max 6 rows to leave room for buttons (rows end at 78+6*18=186)
  int showCount = min(wifiScanCount, 6);
  for (int i = 0; i < showCount; i++) {
    int ry = 80 + i * 18;
    bool sel = (i == wifiScanSelected);
    tft.fillRect(12, ry, 296, 17, sel ? THEME_PRIMARY : THEME_SURFACE);
    tft.setTextColor(sel ? THEME_BG : THEME_TEXT, sel ? THEME_PRIMARY : THEME_SURFACE);
    String s = wifiScanResults[i];
    if (s.length() > 35) s = s.substring(0, 32) + "...";
    tft.drawString(s, 15, ry + 3, 1);
  }

  // Separator above buttons
  tft.drawFastHLine(10, 191, 300, THEME_TEXT_DIM);

  // Buttons clearly below all rows
  drawRoundButton(20,  195, 90, 25, "SELECT", THEME_SUCCESS);
  drawRoundButton(120, 195, 80, 25, "CANCEL", THEME_SECONDARY);
}

// ------------------------------------------------------------------
//  Handle
// ------------------------------------------------------------------
void handleWifiSettingsMode() {
  // Show connecting animation (wifiConnecting polled in appleMidiTick)
  if (wifiConnecting) {
    static unsigned long lastAnim = 0;
    static int dotCount = 0;
    if (millis() - lastAnim > 400) {
      lastAnim = millis();
      dotCount = (dotCount + 1) % 4;
      tft.fillRect(0, 50, 320, 22, THEME_SURFACE);
      tft.setTextColor(THEME_WARNING, THEME_SURFACE);
      tft.drawString("○ Connecting" + String(".").substring(0, dotCount), 6, 54, 1);
    }
    if (!touch.justPressed) return;
    // Allow cancel while connecting
    if (isButtonPressed(10, 218, 140, 20)) {
      wifiDisconnect();
      drawWifiSettingsMode();
    }
    return;
  }

  // Poll async WiFi scan result
  if (wifiScanning) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
      // Still scanning — animate dots in status bar
      static unsigned long lastDot = 0;
      static int dotCount = 0;
      if (millis() - lastDot > 400) {
        lastDot = millis();
        dotCount = (dotCount + 1) % 4;
        String dots = "";
        for (int d = 0; d < dotCount; d++) dots += ".";
        tft.fillRect(0, 50, 320, 22, THEME_SURFACE);
        tft.setTextColor(THEME_WARNING, THEME_SURFACE);
        tft.drawString("○ Scanning" + dots, 6, 54, 1);
      }
      return;  // don't process touches while scan is running
    }
    // Scan finished or failed — always clean up
    Serial.printf("[SCAN] scanComplete returned: %d\n", n);
    wifiScanning  = false;
    wifiScanCount = 0;
    if (n > 0) {
      wifiScanCount = min(n, 6);
      Serial.printf("[SCAN] Found %d networks, showing %d\n", n, wifiScanCount);
      for (int i = 0; i < wifiScanCount; i++) {
        wifiScanResults[i] = WiFi.SSID(i) + "  (" + String(WiFi.RSSI(i)) + "dBm)";
        Serial.printf("[SCAN]   %d: %s\n", i, wifiScanResults[i].c_str());
      }
      wifiShowScan     = true;
      wifiScanSelected = 0;
    } else {
      wifiStatusText = (n == WIFI_SCAN_FAILED) ? "Scan failed" : "No networks found";
      Serial.printf("[SCAN] No results: %s\n", wifiStatusText.c_str());
    }
    Serial.println("[SCAN] Calling WiFi.scanDelete()...");
    WiFi.scanDelete();
    Serial.println("[SCAN] Redrawing settings...");
    drawWifiSettingsMode();
    if (wifiShowScan) drawWifiScanOverlay();
    Serial.println("[SCAN] Scan handling complete");
    return;
  }
  // Keyboard overlay active
  if (wifiEditField != WIFI_EDIT_NONE) {
    handleWifiKeyboard();
    return;
  }

  // Scan overlay active
  if (wifiShowScan) {
    if (!touch.justPressed) return;

    // Check buttons FIRST before rows to avoid overlap conflicts
    if (isButtonPressed(20, 195, 90, 25)) {   // SELECT
      String ssid = wifiScanResults[wifiScanSelected];
      int parenIdx = ssid.indexOf("  (");
      if (parenIdx > 0) ssid = ssid.substring(0, parenIdx);
      ssid.toCharArray(wifiSSID, sizeof(wifiSSID));
      wifiShowScan = false;
      drawWifiSettingsMode();
      return;
    }
    if (isButtonPressed(120, 195, 80, 25)) {  // CANCEL
      wifiShowScan = false;
      drawWifiSettingsMode();
      return;
    }

    // Tap a network row (max 6 rows shown)
    int showCount = min(wifiScanCount, 6);
    for (int i = 0; i < showCount; i++) {
      if (isButtonPressed(12, 80 + i*18, 296, 17)) {
        wifiScanSelected = i;
        drawWifiScanOverlay();
        return;
      }
    }
    return;
  }

  // Back
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    storeWifiSettings();
    inWifiSubMenu = false;
    drawSettingsMode();
    return;
  }

  if (!touch.justPressed) return;

  // SSID EDIT
  if (isButtonPressed(246, 78, 68, 24)) {
    strncpy(wifiEditBuf, wifiSSID, sizeof(wifiEditBuf));
    wifiEditField = WIFI_EDIT_SSID;
    wifiKeyShift  = false;
    drawWifiKeyboard("SSID:", wifiEditBuf);
    return;
  }
  // Password EDIT
  if (isButtonPressed(246, 108, 68, 24)) {
    strncpy(wifiEditBuf, wifiPassword, sizeof(wifiEditBuf));
    wifiEditField = WIFI_EDIT_PASS;
    wifiKeyShift  = false;
    drawWifiKeyboard("Password:", wifiEditBuf);
    return;
  }

  // AppleMIDI toggle
  if (isButtonPressed(240, 144, 70, 28)) {
    appleMidiEnabled = !appleMidiEnabled;
    drawWifiSettingsMode();
    return;
  }
  // Auto-connect toggle
  if (isButtonPressed(240, 180, 70, 28)) {
    wifiAutoConnect = !wifiAutoConnect;
    drawWifiSettingsMode();
    return;
  }

  // Connect / Disconnect
  if (isButtonPressed(10, 218, 140, 20)) {
    if (wifiConnected) {
      wifiDisconnect();
    } else {
      storeWifiSettings();
      wifiConnectStart();   // non-blocking — polls in appleMidiTick
    }
    drawWifiSettingsMode();
    return;
  }

  // Scan
  if (isButtonPressed(160, 218, 150, 20)) {
    doWifiScan();
    drawWifiScanOverlay();
    return;
  }
}

// ------------------------------------------------------------------
//  Init
// ------------------------------------------------------------------
void initializeWifiSettingsMode() {
  wifiEditField   = WIFI_EDIT_NONE;
  wifiShowScan    = false;
  wifiScanning    = false;
  wifiScanCount   = 0;
  wifiKeyShift    = false;
}

// ------------------------------------------------------------------
//  EEPROM persistence for WiFi settings
//  Addresses 11..76
// ------------------------------------------------------------------
void storeWifiSettings() {
  // SSID: 32 bytes at address 11
  for (int i = 0; i < 32; i++) EEPROM.write(11 + i, i < (int)strlen(wifiSSID) ? wifiSSID[i] : 0);
  // Password: 32 bytes at address 43
  for (int i = 0; i < 32; i++) EEPROM.write(43 + i, i < (int)strlen(wifiPassword) ? wifiPassword[i] : 0);
  EEPROM.write(75, appleMidiEnabled ? 1 : 0);
  EEPROM.write(76, wifiAutoConnect  ? 1 : 0);
  EEPROM.commit();
  Serial.println("WiFi settings saved");
}

void loadWifiSettings() {
  for (int i = 0; i < 32; i++) wifiSSID[i]     = (char)EEPROM.read(11 + i);
  for (int i = 0; i < 32; i++) wifiPassword[i] = (char)EEPROM.read(43 + i);
  wifiSSID[32]     = '\0';
  wifiPassword[32] = '\0';
  appleMidiEnabled = (EEPROM.read(75) == 1);
  wifiAutoConnect  = (EEPROM.read(76) == 1);
}

#endif
