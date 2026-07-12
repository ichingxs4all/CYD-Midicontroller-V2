#ifndef PGMCHANGE_MODE_H
#define PGMCHANGE_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// ------------------------------------------------------------------
//  Program Change mode
//  Sends MIDI Program Change (0xC0) on a selectable channel.
//  Program numbers 0-127, channels 1-16.
// ------------------------------------------------------------------

int pgmNumber  = 0;   // 0-127
int pgmChannel = 1;   // 1-16
bool pgmSendFlash = false;
unsigned long pgmFlashTime = 0;

// GM instrument names (128 entries, groups of 8)
const char* gmNames[] = {
  // Piano
  "Acoustic Grand","Bright Acoustic","Electric Grand","Honky-Tonk",
  "Electric Piano1","Electric Piano2","Harpsichord","Clavi",
  // Chromatic Perc
  "Celesta","Glockenspiel","Music Box","Vibraphone",
  "Marimba","Xylophone","Tubular Bells","Dulcimer",
  // Organ
  "Drawbar Organ","Percussive Org","Rock Organ","Church Organ",
  "Reed Organ","Accordion","Harmonica","Tango Accordion",
  // Guitar
  "Nylon Guitar","Steel Guitar","Jazz Guitar","Clean Guitar",
  "Muted Guitar","Overdriven","Distortion","Harmonics",
  // Bass
  "Acoustic Bass","Finger Bass","Pick Bass","Fretless Bass",
  "Slap Bass 1","Slap Bass 2","Synth Bass 1","Synth Bass 2",
  // Strings
  "Violin","Viola","Cello","Contrabass",
  "Tremolo Str","Pizzicato Str","Orchestral Hp","Timpani",
  // Ensemble
  "String Ens 1","String Ens 2","Synth Strings1","Synth Strings2",
  "Choir Aahs","Voice Oohs","Synth Voice","Orchestra Hit",
  // Brass
  "Trumpet","Trombone","Tuba","Muted Trumpet",
  "French Horn","Brass Section","Synth Brass 1","Synth Brass 2",
  // Reed
  "Soprano Sax","Alto Sax","Tenor Sax","Baritone Sax",
  "Oboe","English Horn","Bassoon","Clarinet",
  // Pipe
  "Piccolo","Flute","Recorder","Pan Flute",
  "Blown Bottle","Shakuhachi","Whistle","Ocarina",
  // Synth Lead
  "Square Lead","Sawtooth Lead","Calliope Lead","Chiff Lead",
  "Charang Lead","Voice Lead","Fifths Lead","Bass+Lead",
  // Synth Pad
  "New Age Pad","Warm Pad","Polysynth Pad","Choir Pad",
  "Bowed Pad","Metallic Pad","Halo Pad","Sweep Pad",
  // Synth FX
  "Rain FX","Soundtrack FX","Crystal FX","Atmosphere FX",
  "Brightness FX","Goblins FX","Echoes FX","Sci-fi FX",
  // Ethnic
  "Sitar","Banjo","Shamisen","Koto",
  "Kalimba","Bagpipe","Fiddle","Shanai",
  // Percussive
  "Tinkle Bell","Agogo","Steel Drums","Woodblock",
  "Taiko Drum","Melodic Tom","Synth Drum","Reverse Cymbal",
  // Sound FX
  "Guitar Fret","Breath Noise","Seashore","Bird Tweet",
  "Telephone","Helicopter","Applause","Gunshot"
};

// ------------------------------------------------------------------
//  Send Program Change — proper 2-byte MIDI message
// ------------------------------------------------------------------
void sendProgramChange(int prog, int ch) {
  byte status = 0xC0 | ((ch - 1) & 0x0F);

  // BLE MIDI — 4-byte packet: [header, timestamp, status, program]
  if (deviceConnected) {
    unsigned long now = millis();
    uint8_t hdr = 0x80 | ((now >> 7) & 0x3F);
    uint8_t ts  = 0x80 | (now & 0x7F);
    uint8_t pkt[4] = { hdr, ts, status, (uint8_t)prog };
    pCharacteristic->setValue(pkt, 4);
    pCharacteristic->notify();
  }

  // DIN MIDI — 2 bytes
  MIDISerial.write(status);
  MIDISerial.write((byte)prog);

  Serial.printf("PGM CHG ch%d prog%d (%s)\n", ch, prog, gmNames[prog]);
}

// ------------------------------------------------------------------
//  Draw helpers
// ------------------------------------------------------------------
void drawPgmInfo() {
  // Clear info area
  tft.fillRect(0, 50, 320, 90, THEME_BG);

  // "PROGRAM" label
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString("PROGRAM", 160, 52, 2);

  // Program number – font 4 (~26px tall), centred in remaining space
  char nbuf[4];
  snprintf(nbuf, sizeof(nbuf), "%d", pgmNumber);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString(nbuf, 160, 76, 4);

  // GM instrument name
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawCentreString(gmNames[pgmNumber], 160, 109, 2);
}

void drawPgmChannelRow() {
  tft.fillRect(0, 140, 320, 28, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("Channel:", 10, 147, 2);
  tft.setTextColor(THEME_TEXT, THEME_BG);
  tft.drawString(String(pgmChannel), 105, 147, 2);
  drawRoundButton(140, 140, 40, 28, "CH-", THEME_SECONDARY);
  drawRoundButton(188, 140, 40, 28, "CH+", THEME_SECONDARY);
}

void drawPgmNudgeRow() {
  tft.fillRect(0, 172, 320, 28, THEME_BG);
  drawRoundButton(5,   172, 55, 28, "-10",  THEME_SECONDARY);
  drawRoundButton(65,  172, 55, 28, "-1",   THEME_SECONDARY);
  drawRoundButton(195, 172, 55, 28, "+1",   THEME_SECONDARY);
  drawRoundButton(255, 172, 55, 28, "+10",  THEME_SECONDARY);
}

void drawPgmSendButton(bool flash) {
  tft.fillRect(0, 204, 320, 32, THEME_BG);
  uint16_t col = flash ? THEME_SUCCESS : THEME_PRIMARY;
  drawRoundButton(20, 205, 280, 30, flash ? "SENT!" : "SEND", col, flash);
}

// ------------------------------------------------------------------
//  Full screen draw
// ------------------------------------------------------------------
void drawPgmChangeMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("PROG CHANGE", "GM Instrument");
  drawPgmInfo();
  drawPgmChannelRow();
  drawPgmNudgeRow();
  drawPgmSendButton(false);
}

// ------------------------------------------------------------------
//  Init
// ------------------------------------------------------------------
void initializePgmChangeMode() {
  pgmNumber    = 0;
  pgmChannel   = 1;
  pgmSendFlash = false;
}

// ------------------------------------------------------------------
//  Handle
// ------------------------------------------------------------------
void handlePgmChangeMode() {
  // Back button
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    exitToMenu();
    return;
  }

  // Clear SEND flash after 300ms
  if (pgmSendFlash && millis() - pgmFlashTime > 300) {
    pgmSendFlash = false;
    drawPgmSendButton(false);
  }

  if (touch.justPressed) {
    bool pgmChanged = false;

    // Channel controls
    if (isButtonPressed(140, 140, 40, 28)) { pgmChannel = max(1,   pgmChannel - 1); drawPgmChannelRow(); return; }
    if (isButtonPressed(188, 140, 40, 28)) { pgmChannel = min(16,  pgmChannel + 1); drawPgmChannelRow(); return; }

    // Nudge buttons
    if (isButtonPressed(5,   172, 55, 28)) { pgmNumber = max(0,   pgmNumber - 10); pgmChanged = true; }
    if (isButtonPressed(65,  172, 55, 28)) { pgmNumber = max(0,   pgmNumber -  1); pgmChanged = true; }
    if (isButtonPressed(195, 172, 55, 28)) { pgmNumber = min(127, pgmNumber +  1); pgmChanged = true; }
    if (isButtonPressed(255, 172, 55, 28)) { pgmNumber = min(127, pgmNumber + 10); pgmChanged = true; }

    if (pgmChanged) {
      drawPgmInfo();
      return;
    }

    // SEND button
    if (isButtonPressed(20, 205, 280, 30)) {
      sendProgramChange(pgmNumber, pgmChannel);
      pgmSendFlash = true;
      pgmFlashTime = millis();
      drawPgmSendButton(true);
      return;
    }
  }
}

#endif
