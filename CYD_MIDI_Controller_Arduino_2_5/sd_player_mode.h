#ifndef SD_PLAYER_MODE_H
#define SD_PLAYER_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "sd_card.h"

// ------------------------------------------------------------------
//  MIDI File Player — supports SMF Type 0 AND Type 1
//
//  Type 0: single track, played directly from file (low memory).
//  Type 1: multiple tracks loaded into a merged event array,
//          sorted by absolute tick, then played as one stream.
//          This is the standard approach for embedded SMF players.
//
//  Max merged events (Type 1): SDPLAY_MAX_EVENTS × 7 bytes ≈ 28KB heap.
// ------------------------------------------------------------------

#define SDPLAY_DIR          "/midi"
#define SDPLAY_MAX_FILES     30
#define SDPLAY_VIS_ROWS       5
#define SDPLAY_MAX_EVENTS  4000   // max merged events for Type 1

// ------------------------------------------------------------------
//  File list  (includes dirs — entries ending "/" are directories)
// ------------------------------------------------------------------
String sdPlayFiles[SDPLAY_MAX_FILES];
int    sdPlayFileCount  = 0;
int    sdPlaySelected   = 0;
int    sdPlayScrollTop  = 0;
String sdPlayCurrentPath = SDPLAY_DIR;   // browsed directory; always rooted at SDPLAY_DIR

// ------------------------------------------------------------------
//  Playback state
// ------------------------------------------------------------------
bool          sdPlayPlaying      = false;
bool          sdPlayPaused       = false;
bool          sdPlayLoop         = false;
File          sdPlayFile;
unsigned long sdPlayTempo        = 500000;
uint32_t      sdPlayTicksPerBeat = 480;
double        sdPlayMicroPerTick = 0.0;
unsigned long sdPlayNextEventUs  = 0;
String        sdPlayStatus       = "";
String        sdPlayCurrentFile  = "";
int           sdPlayBpm          = 120;

// ------------------------------------------------------------------
//  Type 1 merged event buffer
// ------------------------------------------------------------------
struct MidiPlayEvent {
  uint32_t absTick;   // absolute tick from file start
  uint8_t  status;
  uint8_t  d1;
  uint8_t  d2;
};

MidiPlayEvent* sdPlayEvents   = nullptr;  // heap-allocated on load
int        sdPlayEventCount = 0;
int        sdPlayEventIdx   = 0;       // current playback position
bool       sdPlayType1      = false;   // true = using merged buffer

// ------------------------------------------------------------------
//  Type 0 streaming state (used when sdPlayType1 == false)
// ------------------------------------------------------------------
uint32_t sdPlayTrackStart = 0;
uint32_t sdPlayTrackEnd   = 0;
static uint8_t sdRunStatus = 0;

// ------------------------------------------------------------------
//  SMF read helpers
// ------------------------------------------------------------------
uint32_t sdPlayReadVLQ(File& f) {
  uint32_t val = 0; uint8_t b;
  do { b = f.read(); val = (val << 7) | (b & 0x7F); } while (b & 0x80);
  return val;
}
uint16_t sdPlayRead16(File& f) { return ((uint16_t)f.read() << 8) | f.read(); }
uint32_t sdPlayRead32(File& f) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) v = (v << 8) | f.read();
  return v;
}

// ------------------------------------------------------------------
//  MIDI output helper (sends on DIN, BLE, AppleMIDI)
// ------------------------------------------------------------------
void sdPlaySendEvent(uint8_t status, uint8_t d1, uint8_t d2) {
  uint8_t cmd = status & 0xF0;
  if (cmd == 0xC0 || cmd == 0xD0) {
    // 2-byte message
    MIDISerial.write(status); MIDISerial.write(d1);
    if (deviceConnected) {
      unsigned long now = millis();
      uint8_t pkt[4] = {(uint8_t)(0x80|((now>>7)&0x3F)),(uint8_t)(0x80|(now&0x7F)),status,d1};
      pCharacteristic->setValue(pkt, 4); pCharacteristic->notify();
    }
#ifdef ENABLE_WIFI
    appleMidiSend(status, d1, 0);
#endif
  } else {
    sendMIDI(status & 0xF0, d1, d2);  // sendMIDI adds channel, so pass raw status
    // Actually sendMIDI adds global channel — for playback we need exact channel:
    MIDISerial.write(status); MIDISerial.write(d1); MIDISerial.write(d2);
#ifdef ENABLE_WIFI
    appleMidiSend(status, d1, d2);
#endif
  }
}

// Correct: send raw MIDI bytes directly preserving original channel
void sdPlaySendRaw(uint8_t status, uint8_t d1, uint8_t d2) {
  uint8_t cmd = status & 0xF0;
  bool twoBytes = (cmd == 0xC0 || cmd == 0xD0);

  // DIN
  MIDISerial.write(status);
  MIDISerial.write(d1);
  if (!twoBytes) MIDISerial.write(d2);

  // BLE
  if (deviceConnected) {
    unsigned long now = millis();
    uint8_t hdr = 0x80 | ((now >> 7) & 0x3F);
    uint8_t ts  = 0x80 | (now & 0x7F);
    if (twoBytes) {
      uint8_t pkt[4] = {hdr, ts, status, d1};
      pCharacteristic->setValue(pkt, 4);
    } else {
      uint8_t pkt[5] = {hdr, ts, status, d1, d2};
      pCharacteristic->setValue(pkt, 5);
    }
    pCharacteristic->notify();
  }

#ifdef ENABLE_WIFI
  appleMidiSend(status, d1, d2);
#endif
}

// ------------------------------------------------------------------
//  Transport helpers
// ------------------------------------------------------------------
void sdPlayAllNotesOff() {
  for (int ch = 0; ch < 16; ch++) {
    MIDISerial.write(0xB0 | ch); MIDISerial.write(123); MIDISerial.write(0);
  }
}

void sdPlayFreeBuffer() {
  if (sdPlayEvents) { free(sdPlayEvents); sdPlayEvents = nullptr; }
  sdPlayEventCount = 0;
  sdPlayEventIdx   = 0;
}

void sdPlayStop() {
  if (sdPlayFile) sdPlayFile.close();
  sdPlayPlaying = false;
  sdPlayPaused  = false;
  sdPlayFreeBuffer();
  sdPlayAllNotesOff();
}

// ------------------------------------------------------------------
//  Type 1: parse all tracks into merged sorted event buffer
// ------------------------------------------------------------------
bool sdPlayLoadType1(File& f, uint16_t nTracks) {
  sdPlayFreeBuffer();
  sdPlayEvents = ( MidiPlayEvent*)malloc(SDPLAY_MAX_EVENTS * sizeof(MidiPlayEvent));
  if (!sdPlayEvents) { sdPlayStatus = "Out of memory"; return false; }
  sdPlayEventCount = 0;

  for (uint16_t t = 0; t < nTracks; t++) {
    // Find MTrk header
    char tag[5] = {0};
    f.read((uint8_t*)tag, 4);
    if (strncmp(tag, "MTrk", 4) != 0) break;
    uint32_t trkLen = sdPlayRead32(f);
    uint32_t trkEnd = f.position() + trkLen;

    uint32_t absTick = 0;
    uint8_t  runSt   = 0;

    while (f.position() < trkEnd && sdPlayEventCount < SDPLAY_MAX_EVENTS) {
      uint32_t delta = sdPlayReadVLQ(f);
      absTick += delta;
      uint8_t b = f.read();

      if (b == 0xFF) {
        // Meta event
        uint8_t type = f.read();
        uint32_t len = sdPlayReadVLQ(f);
        if (type == 0x51 && len == 3) {
          // Tempo — store as special event (status=0xFF, d1=hi, d2=mid, and we need 3 bytes)
          // Pack tempo into event: use status=0xFF type=0x51
          // Store tempo bytes in d1/d2 and a separate tempo table would be ideal,
          // but for simplicity: store as meta event with a sentinel
          uint32_t tempo = 0;
          for (int i = 0; i < 3; i++) tempo = (tempo << 8) | f.read();
          // Encode: status=0xFF, d1=tempo>>8, d2=tempo&0xFF (losing top byte would be wrong)
          // Use a dedicated storage: pack as 3 bytes using status=0xFE as sentinel
          sdPlayEvents[sdPlayEventCount++] = {
            absTick, 0xFE,
            (uint8_t)((tempo >> 16) & 0xFF),
            (uint8_t)((tempo >> 8)  & 0xFF)
          };
          // store low byte separately — just pack all 3 into d1/d2 using uint32 absTick field trick
          // Actually: reuse the event slightly differently -- store tempo in a parallel array
          // Simpler: store two events, first carries hi/mid bytes, but that's messy.
          // BEST: use absTick to store tempo in tempo events via a union approach.
          // We already stored hi and mid; store low byte in a second event:
          sdPlayEventCount--; // undo above
          // Encode full 24-bit tempo using only available fields:
          // status field = 0xFE (sentinel), d1 = (tempo>>16), d2 = (tempo>>8)&0xFF
          // and we pack the low byte into the HIGH BITS of absTick (which we know won't be >0xFFFFFF for any real file)
          sdPlayEvents[sdPlayEventCount++] = {
            absTick | ((uint32_t)(tempo & 0xFF) << 24),
            0xFE,
            (uint8_t)((tempo >> 16) & 0xFF),
            (uint8_t)((tempo >> 8)  & 0xFF)
          };
        } else {
          for (uint32_t i = 0; i < len; i++) f.read();
        }
      } else if (b == 0xF0 || b == 0xF7) {
        uint32_t len = sdPlayReadVLQ(f);
        for (uint32_t i = 0; i < len; i++) f.read();
      } else {
        uint8_t d1, d2;
        if (b & 0x80) { runSt = b; d1 = f.read(); }
        else           { d1 = b; }
        uint8_t cmd = runSt & 0xF0;
        d2 = (cmd != 0xC0 && cmd != 0xD0) ? f.read() : 0;
        sdPlayEvents[sdPlayEventCount++] = { absTick, runSt, d1, d2 };
      }
    }
    // Skip any remaining bytes in this track
    f.seek(trkEnd);
  }

  // Sort by absolute tick (insertion sort — small enough for embedded)
  for (int i = 1; i < sdPlayEventCount; i++) {
    MidiPlayEvent key = sdPlayEvents[i];
    uint32_t keyTick = key.absTick & 0x00FFFFFF;  // mask off packed low byte
    int j = i - 1;
    while (j >= 0 && (sdPlayEvents[j].absTick & 0x00FFFFFF) > keyTick) {
      sdPlayEvents[j + 1] = sdPlayEvents[j];
      j--;
    }
    sdPlayEvents[j + 1] = key;
  }

  Serial.printf("Type 1: merged %d events from %d tracks\n", sdPlayEventCount, nTracks);
  return true;
}

// ------------------------------------------------------------------
//  Start playback
// ------------------------------------------------------------------
bool sdPlayStart(const String& fullPath) {
  sdPlayStop();
  if (!sdMounted) { sdPlayStatus = "No SD card"; return false; }
  sdPlayFile = SD.open(fullPath.c_str());
  if (!sdPlayFile) { sdPlayStatus = "Open failed"; return false; }

  // Parse MThd
  char hdr[5] = {0};
  sdPlayFile.read((uint8_t*)hdr, 4);
  if (strncmp(hdr, "MThd", 4) != 0) {
    sdPlayStatus = "Not a MIDI file"; sdPlayFile.close(); return false;
  }
  sdPlayRead32(sdPlayFile);  // chunk length = 6
  uint16_t format   = sdPlayRead16(sdPlayFile);
  uint16_t nTracks  = sdPlayRead16(sdPlayFile);
  uint16_t division = sdPlayRead16(sdPlayFile);

  if (division & 0x8000) {
    sdPlayStatus = "SMPTE unsupported"; sdPlayFile.close(); return false;
  }
  sdPlayTicksPerBeat = division;
  sdPlayTempo        = 500000;
  sdPlayMicroPerTick = (double)sdPlayTempo / sdPlayTicksPerBeat;
  sdPlayBpm          = 120;
  sdRunStatus        = 0;

  Serial.printf("MIDI: fmt=%d trk=%d tpb=%d\n", format, nTracks, sdPlayTicksPerBeat);

  if (format == 1 && nTracks > 1) {
    // Type 1: load and merge all tracks
    sdPlayType1 = true;
    if (!sdPlayLoadType1(sdPlayFile, nTracks)) {
      sdPlayFile.close(); return false;
    }
    sdPlayFile.close();  // no longer needed after merge
    sdPlayEventIdx    = 0;
    // Schedule first event
    uint32_t firstTick = sdPlayEventCount > 0 ? (sdPlayEvents[0].absTick & 0x00FFFFFF) : 0;
    sdPlayNextEventUs = micros() + (unsigned long)(firstTick * sdPlayMicroPerTick);
  } else {
    // Type 0 (or Type 1 with single track): stream directly from file
    sdPlayType1 = false;
    char trk[5] = {0};
    sdPlayFile.read((uint8_t*)trk, 4);
    uint32_t trkLen  = sdPlayRead32(sdPlayFile);
    sdPlayTrackEnd   = sdPlayFile.position() + trkLen;
    sdPlayTrackStart = sdPlayFile.position();
    uint32_t delta   = sdPlayReadVLQ(sdPlayFile);
    sdPlayNextEventUs = micros() + (unsigned long)(delta * sdPlayMicroPerTick);
  }

  sdPlayPlaying     = true;
  sdPlayPaused      = false;
  // Store just the basename for display
  sdPlayCurrentFile = fullPath.substring(fullPath.lastIndexOf('/') + 1);
  sdPlayStatus      = (format == 1 ? "Type1: " : "Type0: ") + sdPlayCurrentFile;
  return true;
}

// ------------------------------------------------------------------
//  Seek to start (loop support)
// ------------------------------------------------------------------
void sdPlaySeekTrackStart() {
  if (!sdPlayType1) {
    if (!sdPlayFile) return;
    sdPlayFile.seek(sdPlayTrackStart);
    sdPlayTempo        = 500000;
    sdPlayMicroPerTick = (double)sdPlayTempo / sdPlayTicksPerBeat;
    sdPlayBpm          = 120;
    uint32_t delta     = sdPlayReadVLQ(sdPlayFile);
    sdPlayNextEventUs  = micros() + (unsigned long)(delta * sdPlayMicroPerTick);
  } else {
    sdPlayEventIdx     = 0;
    sdPlayTempo        = 500000;
    sdPlayMicroPerTick = (double)sdPlayTempo / sdPlayTicksPerBeat;
    sdPlayBpm          = 120;
    uint32_t firstTick = sdPlayEventCount > 0 ? (sdPlayEvents[0].absTick & 0x00FFFFFF) : 0;
    sdPlayNextEventUs  = micros() + (unsigned long)(firstTick * sdPlayMicroPerTick);
  }
}

// ------------------------------------------------------------------
//  Process next event (Type 1 from buffer)
// ------------------------------------------------------------------
void sdPlayProcessEventType1() {
  if (sdPlayEventIdx >= sdPlayEventCount) {
    if (sdPlayLoop) {
      sdPlayAllNotesOff();
      sdPlaySeekTrackStart();
      sdPlayStatus = "Loop: " + sdPlayCurrentFile;
    } else {
      sdPlayStop();
      sdPlayStatus = "Done: " + sdPlayCurrentFile;
    }
    return;
  }

  MidiPlayEvent& ev = sdPlayEvents[sdPlayEventIdx++];

  if (ev.status == 0xFE) {
    // Tempo change event
    uint32_t lo    = (ev.absTick >> 24) & 0xFF;
    uint32_t tempo = ((uint32_t)ev.d1 << 16) | ((uint32_t)ev.d2 << 8) | lo;
    if (tempo > 0) {
      sdPlayTempo        = tempo;
      sdPlayMicroPerTick = (double)tempo / sdPlayTicksPerBeat;
      sdPlayBpm          = (int)(60000000.0 / tempo);
    }
  } else if (ev.status >= 0x80 && ev.status < 0xF0) {
    sdPlaySendRaw(ev.status, ev.d1, ev.d2);
  }

  // Schedule next event
  if (sdPlayEventIdx < sdPlayEventCount) {
    uint32_t curTick  = ev.absTick        & 0x00FFFFFF;
    uint32_t nextTick = sdPlayEvents[sdPlayEventIdx].absTick & 0x00FFFFFF;
    uint32_t deltaTick = nextTick - curTick;
    sdPlayNextEventUs = micros() + (unsigned long)(deltaTick * sdPlayMicroPerTick);
  } else {
    sdPlayNextEventUs = micros();  // fire end-of-track immediately
  }
}

// ------------------------------------------------------------------
//  Process next event (Type 0: stream from file)
// ------------------------------------------------------------------
void sdPlayProcessEventType0() {
  if (!sdPlayFile || sdPlayFile.position() >= sdPlayTrackEnd) {
    if (sdPlayLoop) {
      sdPlayAllNotesOff();
      sdPlaySeekTrackStart();
      sdPlayStatus = "Loop: " + sdPlayCurrentFile;
    } else {
      sdPlayStop();
      sdPlayStatus = "Done: " + sdPlayCurrentFile;
    }
    return;
  }

  uint8_t b = sdPlayFile.read();

  if (b == 0xFF) {
    uint8_t type  = sdPlayFile.read();
    uint32_t len  = sdPlayReadVLQ(sdPlayFile);
    if (type == 0x51 && len == 3) {
      uint32_t t = 0;
      for (int i = 0; i < 3; i++) t = (t << 8) | sdPlayFile.read();
      sdPlayTempo        = t;
      sdPlayMicroPerTick = (double)t / sdPlayTicksPerBeat;
      sdPlayBpm          = (int)(60000000.0 / t);
    } else {
      for (uint32_t i = 0; i < len; i++) sdPlayFile.read();
    }
  } else if (b == 0xF0 || b == 0xF7) {
    uint32_t len = sdPlayReadVLQ(sdPlayFile);
    for (uint32_t i = 0; i < len; i++) sdPlayFile.read();
  } else {
    uint8_t d1, d2;
    if (b & 0x80) { sdRunStatus = b; d1 = sdPlayFile.read(); }
    else           { d1 = b; }
    uint8_t cmd = sdRunStatus & 0xF0;
    d2 = (cmd != 0xC0 && cmd != 0xD0) ? sdPlayFile.read() : 0;
    sdPlaySendRaw(sdRunStatus, d1, d2);
  }

  if (sdPlayFile.position() < sdPlayTrackEnd) {
    uint32_t delta    = sdPlayReadVLQ(sdPlayFile);
    sdPlayNextEventUs = micros() + (unsigned long)(delta * sdPlayMicroPerTick);
  } else {
    sdPlayNextEventUs = micros();
  }
}

// ------------------------------------------------------------------
//  Draw helpers
// ------------------------------------------------------------------
void sdPlayDrawTransport() {
  tft.fillRect(210, 105, 110, 98, THEME_BG);
  uint16_t playCol; String playLabel;
  if (sdPlayPlaying && !sdPlayPaused) { playCol = THEME_ERROR;   playLabel = "STOP"; }
  else if (sdPlayPaused)              { playCol = THEME_WARNING; playLabel = "RESUME"; }
  else                                { playCol = THEME_SUCCESS; playLabel = "PLAY"; }
  drawRoundButton(210, 110, 105, 30, playLabel, playCol);
  if (sdPlayPlaying && !sdPlayPaused)
    drawRoundButton(210, 144, 105, 25, "PAUSE", THEME_WARNING);
  else
    tft.fillRect(210, 144, 105, 25, THEME_BG);
  drawRoundButton(210, 173, 105, 25, sdPlayLoop ? "LOOP: ON" : "LOOP: OFF",
                  sdPlayLoop ? THEME_SUCCESS : THEME_SURFACE, sdPlayLoop);
}

void sdPlayDrawStatus() {
  tft.fillRect(0, 200, 320, 22, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  String s = sdPlayStatus;
  if (s.length() > 45) s = s.substring(0, 42) + "...";
  tft.drawString(s, 4, 202, 1);
}

void sdPlayDrawBpm() {
  tft.fillRect(0, 28, 320, 17, THEME_SURFACE);
  tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
  tft.drawCentreString(sdPlayPlaying ? ("BPM: " + String(sdPlayBpm)) : "SMF Playback",
                        160, 30, 2);
}

void sdPlayRefreshList() {
  int offset = 0;
  // Prepend ".." when inside a subdirectory of the base MIDI dir
  if (sdPlayCurrentPath != String(SDPLAY_DIR)) {
    sdPlayFiles[0] = "..";
    offset = 1;
  }
  sdPlayFileCount = offset + sdListDirEntries(sdPlayCurrentPath.c_str(),
                                              sdPlayFiles + offset,
                                              SDPLAY_MAX_FILES - offset);
}

void drawSDPlayerMode() {
  tft.fillScreen(THEME_BG);
  String sub = sdPlayPlaying ? ("BPM: " + String(sdPlayBpm)) : sdPlayCurrentPath;
  drawHeader("MIDI PLAYER", sub);
  sdInit();
  sdPlayRefreshList();
  drawFileList(sdPlayFiles, sdPlayFileCount, sdPlayScrollTop,
               SDPLAY_VIS_ROWS, 4, 50, 200, sdPlaySelected);
  drawRoundButton(210, 50, 50, 25, "UP",   THEME_SECONDARY);
  drawRoundButton(210, 79, 50, 25, "DOWN", THEME_SECONDARY);
  sdPlayDrawTransport();
  sdPlayDrawStatus();
  drawSDStatus(sdMounted);
}

// ------------------------------------------------------------------
//  Init
// ------------------------------------------------------------------
void initializeSDPlayerMode() {
  sdPlayCurrentPath = SDPLAY_DIR;
  sdPlaySelected   = 0;
  sdPlayScrollTop  = 0;
  sdPlayStatus     = "";
  sdPlayPlaying    = false;
  sdPlayPaused     = false;
  sdPlayLoop       = false;
  sdRunStatus      = 0;
  sdPlayType1      = false;
  sdPlayEvents     = nullptr;
  sdPlayEventCount = 0;
  sdPlayEventIdx   = 0;
}

// ------------------------------------------------------------------
//  Handle
// ------------------------------------------------------------------
void handleSDPlayerMode() {
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    sdPlayStop(); exitToMenu(); return;
  }

  // Playback tick
  if (sdPlayPlaying && !sdPlayPaused) {
    if ((long)(micros() - sdPlayNextEventUs) >= 0) {
      if (sdPlayType1) sdPlayProcessEventType1();
      else             sdPlayProcessEventType0();

      static unsigned long lastBpmDraw = 0;
      if (millis() - lastBpmDraw > 1000) {
        sdPlayDrawBpm(); lastBpmDraw = millis();
      }
      if (!sdPlayPlaying) {
        sdPlayDrawTransport(); sdPlayDrawStatus();
      }
    }
  }

  if (!touch.justPressed) return;

  if (isButtonPressed(210, 50, 50, 25)) {
    if (sdPlayScrollTop > 0) { sdPlayScrollTop--; drawSDPlayerMode(); } return;
  }
  if (isButtonPressed(210, 79, 50, 25)) {
    if (sdPlayScrollTop + SDPLAY_VIS_ROWS < sdPlayFileCount) { sdPlayScrollTop++; drawSDPlayerMode(); } return;
  }

  for (int i = 0; i < SDPLAY_VIS_ROWS; i++) {
    if (isButtonPressed(4, 50 + i * FILELIST_ROW_H, 200, FILELIST_ROW_H - 2)) {
      int idx = sdPlayScrollTop + i;
      if (idx < sdPlayFileCount) {
        String sel = sdPlayFiles[idx];
        if (sel == "..") {
          // Go up one level (never above SDPLAY_DIR)
          int sl = sdPlayCurrentPath.lastIndexOf('/');
          sdPlayCurrentPath = (sl > 0) ? sdPlayCurrentPath.substring(0, sl)
                                       : String(SDPLAY_DIR);
          sdPlaySelected = 0; sdPlayScrollTop = 0;
        } else if (sel.endsWith("/")) {
          // Navigate into subdirectory
          sdPlayCurrentPath = sdPlayCurrentPath + "/" + sel.substring(0, sel.length() - 1);
          sdPlaySelected = 0; sdPlayScrollTop = 0;
        } else {
          sdPlaySelected = idx;
        }
        drawSDPlayerMode();
      }
      return;
    }
  }

  if (isButtonPressed(210, 110, 105, 30)) {
    if (sdPlayPlaying && !sdPlayPaused) {
      sdPlayStop(); sdPlayStatus = "Stopped";
    } else if (sdPlayPaused) {
      sdPlayPaused = false; sdPlayNextEventUs = micros();
    } else if (sdPlayFileCount > 0) {
      String sel = sdPlayFiles[sdPlaySelected];
      if (!sel.endsWith("/") && sel != "..") {
        sdRunStatus = 0;
        sdPlayStart(sdPlayCurrentPath + "/" + sel);
      }
    }
    sdPlayDrawTransport(); sdPlayDrawStatus(); sdPlayDrawBpm(); return;
  }

  if (sdPlayPlaying && !sdPlayPaused && isButtonPressed(210, 144, 105, 25)) {
    sdPlayPaused = true; sdPlayAllNotesOff(); sdPlayDrawTransport(); return;
  }

  if (isButtonPressed(210, 173, 105, 25)) {
    sdPlayLoop = !sdPlayLoop; sdPlayDrawTransport(); return;
  }
}

#endif