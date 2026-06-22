#ifndef WIFI_APPLEMIDI_H
#define WIFI_APPLEMIDI_H

// ------------------------------------------------------------------
//  WiFi + AppleMIDI (RTP-MIDI) transport layer
//
//  Required libraries (install via Arduino Library Manager):
//    - "AppleMIDI" by lathoub  (also called rtpMIDI)
//    - "MIDI Library" by Francois Best
//
//  AppleMIDI uses WiFi UDP on port 5004/5005.
//  When enabled, all MIDI output is sent on WiFi in addition to
//  BLE and DIN. Incoming RTP-MIDI is routed to the monitor and
//  all other consumers via the existing monitorPushEvent() path.
// ------------------------------------------------------------------

#include <WiFi.h>
#include <AppleMIDI.h>

// ------------------------------------------------------------------
//  Persisted settings (populated from EEPROM in wifi_applemidi.h)
// ------------------------------------------------------------------
char   wifiSSID[33]     = "";
char   wifiPassword[33] = "";
bool   appleMidiEnabled = false;
bool   wifiAutoConnect  = false;

// ------------------------------------------------------------------
//  Runtime state
// ------------------------------------------------------------------
bool   wifiConnected    = false;
bool   appleMidiRunning = false;
String wifiStatusText   = "Not connected";
String wifiIPText       = "";

APPLEMIDI_CREATE_DEFAULTSESSION_INSTANCE();

// ------------------------------------------------------------------
//  Connect / disconnect
// ------------------------------------------------------------------
bool wifiConnecting = false;   // async connect in progress
int  wifiConnectTries = 0;

void wifiConnectStart() {
  Serial.println("[WIFI] wifiConnectStart() called");
  if (strlen(wifiSSID) == 0) { Serial.println("[WIFI] No SSID set"); wifiStatusText = "No SSID set"; return; }
  Serial.printf("[WIFI] SSID: %s  PW len: %d\n", wifiSSID, strlen(wifiPassword));
  wifiStatusText   = "Connecting...";
  wifiConnecting   = true;
  wifiConnectTries = 0;
  Serial.printf("[WIFI] Free heap before WiFi.mode: %d\n", ESP.getFreeHeap());
  Serial.println("[WIFI] Calling WiFi.mode(WIFI_STA)...");
  WiFi.mode(WIFI_STA);
  Serial.println("[WIFI] WiFi.mode done. Calling WiFi.begin...");
  WiFi.begin(wifiSSID, wifiPassword);
  Serial.println("[WIFI] WiFi.begin done. Async polling started.");
}

// Called from wifiConnect() for backward compatibility with auto-connect
void wifiConnect() {
  wifiConnectStart();
}

void wifiOnConnected() {
  Serial.println("[WIFI] wifiOnConnected() called");
  wifiConnected  = true;
  wifiConnecting = false;
  wifiIPText     = WiFi.localIP().toString();
  wifiStatusText = "Connected";
  Serial.printf("[WIFI] IP: %s\n", wifiIPText.c_str());
  if (currentMode == MENU) drawMenu();
  if (inWifiSubMenu)       drawWifiSettingsMode();  // show IP immediately when user is watching
  Serial.println("[WIFI] Calling MIDI.begin...");
  MIDI.begin(MIDI_CHANNEL_OMNI);
  Serial.printf("[WIFI] AppleMIDI session: %s\n", AppleMIDI.getName());
  Serial.println("[WIFI] wifiOnConnected() done");
}

void wifiOnFailed() {
  Serial.printf("[WIFI] wifiOnFailed() after %d tries\n", wifiConnectTries);
  wifiConnected  = false;
  wifiConnecting = false;
  wifiStatusText = "Failed (check SSID/PW)";
  WiFi.disconnect(true);
  Serial.println("[WIFI] WiFi.disconnect done");
}

void wifiDisconnect() {
  WiFi.disconnect(true);
  wifiConnected    = false;
  wifiConnecting   = false;
  appleMidiRunning = false;
  wifiStatusText   = "Disconnected";
  wifiIPText       = "";
  if (currentMode == MENU) drawMenu();
}

// ------------------------------------------------------------------
//  Send via AppleMIDI — called from sendMIDI() and sendRealTime()
// ------------------------------------------------------------------
void appleMidiSend(byte status, byte d1, byte d2) {
  // Require: feature enabled + WiFi up + peer has connected (appleMidiRunning)
  if (!appleMidiEnabled || !wifiConnected || !appleMidiRunning) return;
  byte cmd = status & 0xF0;
  byte ch  = (status & 0x0F) + 1;  // AppleMIDI uses 1-based channels

  switch (cmd) {
    case 0x80: MIDI.sendNoteOff(d1, d2, ch);          break;
    case 0x90: MIDI.sendNoteOn(d1, d2, ch);           break;
    case 0xA0: MIDI.sendAfterTouch(d1, d2, ch);       break;
    case 0xB0: MIDI.sendControlChange(d1, d2, ch);    break;
    case 0xC0: MIDI.sendProgramChange(d1, ch);        break;
    case 0xD0: MIDI.sendAfterTouch(d1, 0, ch);        break;
    case 0xE0: {
      int bend = (int)(d1 | (d2 << 7)) - 8192;
      MIDI.sendPitchBend(bend, ch);
      break;
    }
    default:
      if (status >= 0xF8) MIDI.sendRealTime((midi::MidiType)status);
      break;
  }
}

void appleMidiSendSysex(const uint8_t* data, uint16_t len) {
  if (!appleMidiEnabled || !wifiConnected || !appleMidiRunning) return;
  // Strip F0/F7 wrapper — AppleMIDI library adds them
  uint16_t start = (data[0] == 0xF0) ? 1 : 0;
  uint16_t end   = (data[len-1] == 0xF7) ? len - 1 : len;
  if (end > start)
    MIDI.sendSysEx(end - start, data + start, false);
}

// ------------------------------------------------------------------
//  Receive callbacks — route incoming RTP-MIDI into the system
//  Registered once at init, not on every reconnect.
// ------------------------------------------------------------------
void appleMidiSetupReceive() {
  AppleMIDI.setHandleConnected([](const APPLEMIDI_NAMESPACE::ssrc_t& ssrc, const char* name) {
    Serial.printf("AppleMIDI peer connected: %s\n", name);
    appleMidiRunning = true;
    if (currentMode == MENU) drawMenu();  // refresh header dot
  });
  AppleMIDI.setHandleDisconnected([](const APPLEMIDI_NAMESPACE::ssrc_t& ssrc) {
    Serial.println("AppleMIDI peer disconnected");
    appleMidiRunning = false;
    if (currentMode == MENU) drawMenu();  // refresh header dot
  });
  MIDI.setHandleNoteOn([](byte ch, byte note, byte vel) {
    monitorPushEvent(0x90 | (ch-1), note, vel);
  });
  MIDI.setHandleNoteOff([](byte ch, byte note, byte vel) {
    monitorPushEvent(0x80 | (ch-1), note, vel);
  });
  MIDI.setHandleControlChange([](byte ch, byte num, byte val) {
    monitorPushEvent(0xB0 | (ch-1), num, val);
  });
  MIDI.setHandleProgramChange([](byte ch, byte pgm) {
    monitorPushEvent(0xC0 | (ch-1), pgm, 0);
  });
  MIDI.setHandlePitchBend([](byte ch, int bend) {
    int val14 = bend + 8192;
    monitorPushEvent(0xE0 | (ch-1), val14 & 0x7F, (val14 >> 7) & 0x7F);
  });
  MIDI.setHandleAfterTouchChannel([](byte ch, byte pressure) {
    monitorPushEvent(0xD0 | (ch-1), pressure, 0);
  });
  MIDI.setHandleClock([]() { clockReceiveByte(0xF8); });
  MIDI.setHandleStart([]()    { clockReceiveByte(0xFA); });
  MIDI.setHandleContinue([]() { clockReceiveByte(0xFB); });
  MIDI.setHandleStop([]()     { clockReceiveByte(0xFC); });
}

// ------------------------------------------------------------------
//  Call from main loop() — services the AppleMIDI/RTP-MIDI stack.
//  Must run every loop when WiFi is up so the host can connect and
//  so incoming messages are processed. Does not require appleMidiEnabled
//  to be true — the session must keep running to accept connections.
// ------------------------------------------------------------------
void appleMidiTick() {
  unsigned long now = millis();

  // WiFi connect polling: every 500ms, up to 20 tries (10 second timeout)
  static unsigned long lastConnectPoll = 0;
  if (wifiConnecting && now - lastConnectPoll >= 500) {
    lastConnectPoll = now;
    int status = WiFi.status();
    Serial.printf("[WIFI] connect poll try=%d  WiFi.status=%d\n", wifiConnectTries, status);
    if (status == WL_CONNECTED) {
      wifiOnConnected();
    } else {
      wifiConnectTries++;
      if (wifiConnectTries >= 20) {
        wifiOnFailed();
      }
    }
    return;
  }

  if (!wifiConnected) return;

  // AppleMIDI/RTP-MIDI packet processing: every 10ms for prompt handshake and MIDI clock
  static unsigned long lastMidiPoll = 0;
  if (now - lastMidiPoll < 10) return;
  lastMidiPoll = now;

  static bool tickLoggedOnce = false;
  if (!tickLoggedOnce) {
    Serial.println("[WIFI] First AppleMIDI.read() + MIDI.read() call");
    tickLoggedOnce = true;
  }
  AppleMIDI.read();
  MIDI.read();
}

// ------------------------------------------------------------------
//  Init — called from setup() after EEPROM is loaded
// ------------------------------------------------------------------
void initWifiAppleMidi() {
  Serial.println("[WIFI] initWifiAppleMidi() called");
  Serial.println("[WIFI] Setting up receive callbacks...");
  appleMidiSetupReceive();
  Serial.println("[WIFI] Callbacks done");
  Serial.printf("[WIFI] autoConnect=%d  SSID=%s\n", wifiAutoConnect, wifiSSID);
  if (wifiAutoConnect && strlen(wifiSSID) > 0) {
    Serial.println("[WIFI] Auto-connecting...");
    wifiConnect();
  }
  Serial.println("[WIFI] initWifiAppleMidi() done");
}

#endif
