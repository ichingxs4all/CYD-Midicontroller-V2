#ifndef SD_FILETRANSFER_MODE_H
#define SD_FILETRANSFER_MODE_H

// ------------------------------------------------------------------
//  USB Serial File Transfer Mode  (v2.9)
//
//  Exposes the SD card as a file system over USB Serial to a Chrome
//  browser using the Web Serial API (cyd_filetransfer.html).
//
//  Hardware: CYD connects via USB-to-Serial bridge (CH340 / CP2102).
//  Web Serial API works with any CDC/ACM serial device — it is the
//  browser-side equivalent of WebUSB for serial-class USB devices.
//
//  Baud rate: 115200  (set in cyd_filetransfer.html)
//
//  ─── Protocol ────────────────────────────────────────────────────
//  All text commands/responses are newline-terminated (\n).
//  Binary data is always length-delimited — the ESP32 never scans
//  binary for text markers.
//
//  PC→ESP  FT:PING                  ESP→PC  FT:PONG:CYD_MIDI_V2.9
//  PC→ESP  FT:LIST[:<path>]         ESP→PC  FT:F:<path>:<bytes>\n ...
//                                           FT:D:<path>\n ...
//                                           FT:OK
//  PC→ESP  FT:GET:<path>            ESP→PC  FT:SIZE:<n>\n <n bytes> FT:OK
//  PC→ESP  FT:PUT:<path>:<size>     ESP→PC  FT:READY
//    loop:
//      PC→ESP  FT:BLOCK:<n>         ESP→PC  FT:GO
//      PC→ESP  <n raw bytes>        ESP→PC  FT:ACK:<total_rx>
//    PC→ESP  FT:END                 ESP→PC  FT:OK
//  PC→ESP  FT:DEL:<path>            ESP→PC  FT:OK  or  FT:ERR:<msg>
//  PC→ESP  FT:MKDIR:<path>          ESP→PC  FT:OK  or  FT:ERR:<msg>
//  PC→ESP  FT:EXIT                  ESP→PC  FT:BYE  (returns to FILES)
// ------------------------------------------------------------------

#include "common_definitions.h"
#include "ui_elements.h"
#include "sd_card.h"

// ------------------------------------------------------------------
//  State machine
// ------------------------------------------------------------------
enum FTPhase {
  FTP_IDLE,            // waiting for any FT: command
  FTP_PUT_WAIT_BLOCK,  // PUT started, file open — expect FT:BLOCK or FT:END
  FTP_PUT_RECV_BLOCK,  // receiving binary block bytes
};

static FTPhase  ftPhase         = FTP_IDLE;
static File     ftFile;
static int32_t  ftPutTotal      = 0;   // total bytes expected for current PUT
static int32_t  ftPutReceived   = 0;   // bytes received so far
static int      ftBlockExpected = 0;   // bytes in current FT:BLOCK
static int      ftBlockReceived = 0;

static char     ftCmdBuf[300];
static int      ftCmdLen = 0;

// Display state
static String   ftLine1   = "Waiting for browser...";
static String   ftLine2   = "Open cyd_filetransfer.html in Chrome";
static String   ftLastOp  = "";
static bool     ftBusy    = false;
static int      ftProgress = 0;        // 0-100

// ------------------------------------------------------------------
//  Serial helpers — keep all protocol output through these
// ------------------------------------------------------------------
static void ftSend(const char* s)    { Serial.print(s); }
static void ftSendLn(const char* s)  { Serial.print(s); Serial.print('\n'); }
static void ftSendStr(const String& s){ Serial.print(s); Serial.print('\n'); }

// ------------------------------------------------------------------
//  Display helpers
// ------------------------------------------------------------------
static void ftRedrawProgress() {
  tft.fillRect(0, 128, 320, 72, THEME_BG);

  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString(ftLine1, 10, 132, 2);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString(ftLine2, 10, 154, 1);

  if (ftBusy) {
    int barW = 300, barH = 14;
    tft.fillRect(10, 168, (ftProgress * barW) / 100, barH, THEME_PRIMARY);
    tft.fillRect(10 + (ftProgress * barW) / 100, 168,
                 barW - (ftProgress * barW) / 100, barH, THEME_SURFACE);
    tft.drawRect(10, 168, barW, barH, THEME_TEXT_DIM);
    tft.setTextColor(THEME_TEXT, THEME_BG);
    String pctStr = String(ftProgress) + "%  " + ftLastOp;
    tft.drawString(pctStr, 10, 185, 1);
  }
}

static void ftSetStatus(const String& l1, const String& l2 = "",
                        bool busy = false, int pct = 0) {
  ftLine1    = l1;
  ftLine2    = l2;
  ftBusy     = busy;
  ftProgress = pct;
  ftRedrawProgress();
}

// ------------------------------------------------------------------
//  Command handlers
// ------------------------------------------------------------------
static void ftDoList(const char* path) {
  // Normalise: ensure leading '/', strip trailing '/' except for root
  String p(path[0] ? path : "/");
  if (p[0] != '/') p = "/" + p;
  if (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);

  File dir = SD.open(p.c_str());
  if (!dir || !dir.isDirectory()) {
    ftSendLn("FT:ERR:Not a directory");
    ftSetStatus("LIST error", p);
    return;
  }

  ftSetStatus("Listing: " + p, "");
  char buf[320];
  // Prefix used to build absolute paths from bare basenames
  const String pfx = (p == "/") ? "/" : p + "/";

  File entry = dir.openNextFile();
  while (entry) {
    const char* raw = entry.name();

    // Always produce an absolute path regardless of SD library version.
    // Arduino-ESP32 >= 2.x returns the full path; older versions return
    // just the basename.  We handle both cases.
    String full;
    if (raw[0] == '/') {
      full = String(raw);       // already absolute  e.g.  /MIDI/SONG.MID
    } else {
      full = pfx + String(raw); // make absolute      e.g.  /MIDI/SONG.MID
    }

    // Skip hidden / dot-file entries (macOS ._ files, .DS_Store, System Volume Information, …)
    String base = full.substring(full.lastIndexOf('/') + 1);
    if (base.length() == 0 || base[0] == '.') {
      entry.close();
      entry = dir.openNextFile();
      continue;
    }

    if (entry.isDirectory()) {
      snprintf(buf, sizeof(buf), "FT:D:%s", full.c_str());
    } else {
      snprintf(buf, sizeof(buf), "FT:F:%s:%u", full.c_str(), (unsigned)entry.size());
    }
    ftSendLn(buf);
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  ftSendLn("FT:OK");
  ftSetStatus("Listed: " + p, "");
}

static void ftDoGet(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) { ftSendLn("FT:ERR:File not found"); return; }

  uint32_t sz = f.size();
  char hdr[48];
  snprintf(hdr, sizeof(hdr), "FT:SIZE:%u", sz);
  ftSendLn(hdr);

  ftLastOp = "GET " + String(path);
  ftSetStatus("Sending: " + String(path),
              String(sz / 1024) + " KB", true, 0);

  uint8_t buf[512];
  uint32_t sent = 0;
  while (f.available()) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    Serial.write(buf, n);
    sent += n;
    ftProgress = (sz > 0) ? (int)((sent * 100UL) / sz) : 100;
    ftRedrawProgress();
  }
  f.close();
  ftSendLn("FT:OK");
  ftSetStatus("Sent: " + String(path), String(sz / 1024) + " KB", false, 100);
}

static void ftDoPutStart(const char* path, int32_t size) {
  // Auto-create parent directory
  String sp(path);
  int sl = sp.lastIndexOf('/');
  if (sl > 0) {
    String dir = sp.substring(0, sl);
    if (!SD.exists(dir.c_str())) SD.mkdir(dir.c_str());
  }

  ftFile = SD.open(path, FILE_WRITE);
  if (!ftFile) { ftSendLn("FT:ERR:Cannot create file"); return; }

  ftPutTotal    = size;
  ftPutReceived = 0;
  ftPhase       = FTP_PUT_WAIT_BLOCK;
  ftLastOp      = "PUT " + String(path);
  ftSetStatus("Receiving: " + String(path),
              String(size / 1024) + " KB", true, 0);
  ftSendLn("FT:READY");
}

static void ftDoDel(const char* path) {
  if (SD.remove(path)) {
    ftSendLn("FT:OK");
    ftSetStatus("Deleted", String(path));
  } else {
    ftSendLn("FT:ERR:Remove failed");
    ftSetStatus("Del failed", String(path));
  }
}

static void ftDoMkdir(const char* path) {
  if (SD.mkdir(path)) {
    ftSendLn("FT:OK");
    ftSetStatus("Created dir", String(path));
  } else {
    ftSendLn("FT:ERR:mkdir failed");
    ftSetStatus("mkdir failed", String(path));
  }
}

// ------------------------------------------------------------------
//  Text command dispatcher
// ------------------------------------------------------------------
static void ftDispatch() {
  char* cmd = ftCmdBuf;

  if (strncmp(cmd, "FT:PING", 7) == 0) {
    ftSendLn("FT:PONG:CYD_MIDI_V2.9");
    ftSetStatus("Browser connected", "PING OK");

  } else if (strncmp(cmd, "FT:LIST", 7) == 0) {
    const char* path = (cmd[7] == ':') ? cmd + 8 : "";
    ftDoList(path);

  } else if (strncmp(cmd, "FT:GET:", 7) == 0) {
    ftDoGet(cmd + 7);

  } else if (strncmp(cmd, "FT:PUT:", 7) == 0) {
    // FT:PUT:<path>:<size>  — size is after the LAST colon
    char* lastColon = strrchr(cmd + 7, ':');
    if (lastColon) {
      *lastColon = '\0';
      int32_t sz = atol(lastColon + 1);
      ftDoPutStart(cmd + 7, sz);
    } else {
      ftSendLn("FT:ERR:Bad PUT syntax");
    }

  // ---- PUT sub-commands (only valid in FTP_PUT_WAIT_BLOCK) ----
  } else if (strncmp(cmd, "FT:BLOCK:", 9) == 0 && ftPhase == FTP_PUT_WAIT_BLOCK) {
    ftBlockExpected = atoi(cmd + 9);
    ftBlockReceived = 0;
    ftPhase = FTP_PUT_RECV_BLOCK;
    ftSendLn("FT:GO");

  } else if (strncmp(cmd, "FT:END", 6) == 0 && ftPhase == FTP_PUT_WAIT_BLOCK) {
    ftFile.flush();
    ftFile.close();
    ftPhase = FTP_IDLE;
    ftSendLn("FT:OK");
    ftSetStatus("Saved: " + ftLastOp.substring(4),
                String(ftPutTotal / 1024) + " KB", false, 100);

  } else if (strncmp(cmd, "FT:DEL:", 7) == 0) {
    ftDoDel(cmd + 7);

  } else if (strncmp(cmd, "FT:MKDIR:", 9) == 0) {
    ftDoMkdir(cmd + 9);

  } else if (strncmp(cmd, "FT:EXIT", 7) == 0) {
    if (ftPhase == FTP_PUT_WAIT_BLOCK || ftPhase == FTP_PUT_RECV_BLOCK) {
      ftFile.close();
    }
    ftPhase = FTP_IDLE;
    ftSendLn("FT:BYE");
    exitToMenu();

  } else {
    // Unknown / out-of-sequence command — ignore silently in binary phases
    if (ftPhase == FTP_IDLE) {
      char resp[64];
      snprintf(resp, sizeof(resp), "FT:ERR:Unknown cmd");
      ftSendLn(resp);
    }
  }
}

// ------------------------------------------------------------------
//  Binary block receiver — drains Serial into the open file.
//  Called every loop when ftPhase == FTP_PUT_RECV_BLOCK.
//  Non-blocking: reads only what's currently available.
// ------------------------------------------------------------------
static void ftDrainBlock() {
  static uint8_t blkBuf[128];

  while (Serial.available() > 0 && ftBlockReceived < ftBlockExpected) {
    int avail = Serial.available();
    int need  = ftBlockExpected - ftBlockReceived;
    int take  = min(avail, min(need, (int)sizeof(blkBuf)));

    for (int i = 0; i < take; i++) blkBuf[i] = (uint8_t)Serial.read();
    ftFile.write(blkBuf, take);
    ftBlockReceived += take;
    ftPutReceived   += take;
  }

  if (ftBlockReceived >= ftBlockExpected) {
    // Block complete — acknowledge and wait for next block command
    ftPhase  = FTP_PUT_WAIT_BLOCK;
    ftProgress = (ftPutTotal > 0) ? (int)((ftPutReceived * 100LL) / ftPutTotal) : 100;
    char ack[32];
    snprintf(ack, sizeof(ack), "FT:ACK:%d", ftPutReceived);
    ftSendLn(ack);
    ftRedrawProgress();
  }
}

// ------------------------------------------------------------------
//  Mode lifecycle
// ------------------------------------------------------------------
void initializeSDFiletransferMode() {
  ftPhase         = FTP_IDLE;
  ftLine1         = "Waiting for browser...";
  ftLine2         = "Open cyd_filetransfer.html in Chrome";
  ftLastOp        = "";
  ftBusy          = false;
  ftProgress      = 0;
  ftCmdLen        = 0;
  ftBlockExpected = 0;
  ftBlockReceived = 0;
}

void drawSDFiletransferMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("USB XFER", "Web Serial @ 115200 baud");

  // USB plug icon area
  tft.fillRoundRect(134, 52, 52, 70, 6, THEME_SURFACE);
  tft.drawRoundRect(134, 52, 52, 70, 6, THEME_PRIMARY);
  // plug body
  tft.fillRect(149, 58, 22, 38, THEME_PRIMARY);
  tft.fillRect(144, 64, 6, 8,  THEME_PRIMARY);
  tft.fillRect(170, 64, 6, 8,  THEME_PRIMARY);
  // cable
  tft.fillRect(157, 96, 6, 20, THEME_PRIMARY);
  tft.drawCentreString("USB", 160, 108, 1);

  tft.drawFastHLine(0, 126, 320, THEME_SURFACE);

  ftRedrawProgress();

  tft.drawFastHLine(0, 200, 320, THEME_SURFACE);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("115200 8N1  SD card required",   10, 204, 1);
  tft.drawString("Chrome \xe2\x89\xa5 89  cyd_filetransfer.html", 10, 214, 1);
  tft.drawString("BACK exits and aborts transfer", 10, 224, 1);
}

void handleSDFiletransferMode() {
  // BACK button — abort cleanly
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    if (ftPhase != FTP_IDLE && ftFile) ftFile.close();
    ftPhase = FTP_IDLE;
    ftSendLn("FT:BYE");
    exitToMenu();
    return;
  }

  // Binary receive phase — drain serial as fast as possible
  if (ftPhase == FTP_PUT_RECV_BLOCK) {
    ftDrainBlock();
    return;
  }

  // Text command reader — accumulate bytes until newline
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (ftCmdLen > 0) {
        ftCmdBuf[ftCmdLen] = '\0';
        ftCmdLen = 0;
        ftDispatch();  // may change ftPhase to PUT_RECV_BLOCK
      }
      return;  // process one command per loop tick
    }
    if (ftCmdLen < (int)sizeof(ftCmdBuf) - 1) {
      ftCmdBuf[ftCmdLen++] = c;
    }
  }
}

#endif
