#ifndef SEQUENCER_MODE_H
#define SEQUENCER_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "midi_utils.h"

// drumNotes[] and drumChannel are defined in settings_mode.h
// (included before this file in the .ino)
extern int drumNotes[4];
extern int drumChannel;

// Sequencer mode variables
#define SEQ_STEPS  16
#define SEQ_TRACKS  4

bool sequencePattern[SEQ_TRACKS][SEQ_STEPS];
int currentStep = 0;
unsigned long lastStepTime = 0;
unsigned long noteOffTime[SEQ_TRACKS] = {0};
int bpm = 120;
int stepInterval;
bool sequencerPlaying = false;

// Function declarations
void initializeSequencerMode();
void drawSequencerMode();
void handleSequencerMode();
void drawSequencerGrid();
void toggleSequencerStep(int track, int step);
void updateSequencer();
void playSequencerStep();

// ------------------------------------------------------------------
//  Helper: send MIDI on the drum channel (ignores global channel)
// ------------------------------------------------------------------
void sendDrumMIDI(byte cmd, byte note, byte vel) {
  // BLE
  if (deviceConnected) {
    uint8_t pkt[5];
    pkt[0] = midiPacket[0];
    pkt[1] = midiPacket[1];
    pkt[2] = cmd | (drumChannel - 1);  // force drum channel
    pkt[3] = note;
    pkt[4] = vel;
    pCharacteristic->setValue(pkt, 5);
    pCharacteristic->notify();
  }
  // DIN
  MIDISerial.write(cmd | (drumChannel - 1));
  MIDISerial.write(note);
  MIDISerial.write(vel);

  // CV / Gate output via MCP4728
  cvProcessMIDI(cmd | (byte)(drumChannel - 1), note, vel);
}

// ------------------------------------------------------------------
//  Implementations
// ------------------------------------------------------------------
void initializeSequencerMode() {
  bpm = 120;
  stepInterval = 60000 / bpm / 4; // 16th notes
  sequencerPlaying = false;
  currentStep = 0;

  for (int t = 0; t < SEQ_TRACKS; t++)
    for (int s = 0; s < SEQ_STEPS; s++)
      sequencePattern[t][s] = false;
}

void drawSequencerMode() {
  tft.fillScreen(THEME_BG);
  drawHeader("BEATS", String(bpm) + " BPM");

  drawSequencerGrid();

  drawRoundButton(10,  200, 50, 25, sequencerPlaying ? "STOP" : "PLAY",
                 sequencerPlaying ? THEME_ERROR : THEME_SUCCESS);
  drawRoundButton(70,  200, 50, 25, "CLEAR",  THEME_WARNING);
  drawRoundButton(130, 200, 40, 25, "BPM-",   THEME_SECONDARY);
  drawRoundButton(180, 200, 40, 25, "BPM+",   THEME_SECONDARY);

  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString(String(bpm), 240, 207, 2);
}

void drawSequencerGrid() {
  int gridX   = 10;
  int gridY   = 50;
  int cellW   = 15;
  int cellH   = 28;
  int spacing = 1;

  // Track labels derived from note names so they reflect user settings
  String trackLabels[SEQ_TRACKS];
  trackLabels[0] = "KICK";
  trackLabels[1] = "SNRE";
  trackLabels[2] = "HHAT";
  trackLabels[3] = "OPEN";

  uint16_t trackColors[] = {THEME_ERROR, THEME_WARNING, THEME_PRIMARY, THEME_ACCENT};

  for (int track = 0; track < SEQ_TRACKS; track++) {
    int y = gridY + track * (cellH + spacing + 3);

    // Show note number alongside label so user can see current mapping
    tft.setTextColor(trackColors[track], THEME_BG);
    tft.drawString(trackLabels[track], gridX, y + 6, 1);
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString(String(drumNotes[track]), gridX, y + 16, 1);

    for (int step = 0; step < SEQ_STEPS; step++) {
      int x = gridX + 35 + step * (cellW + spacing);

      bool active  = sequencePattern[track][step];
      bool current = (sequencerPlaying && step == currentStep);

      uint16_t color;
      if (current && active) color = THEME_TEXT;
      else if (current)      color = trackColors[track];
      else if (active)       color = trackColors[track];
      else                   color = THEME_SURFACE;

      if (step % 4 == 0)
        tft.drawRect(x - 1, y - 1, cellW + 2, cellH + 2, THEME_TEXT_DIM);

      tft.fillRect(x, y, cellW, cellH, color);
      tft.drawRect(x, y, cellW, cellH, THEME_TEXT_DIM);
    }
  }
}

void handleSequencerMode() {
  if (touch.justPressed && isButtonPressed(10, 10, 50, 25)) {
    sequencerPlaying = false;
    for (int track = 0; track < SEQ_TRACKS; track++) {
      if (noteOffTime[track] > 0) {
        sendDrumMIDI(0x80, drumNotes[track], 0);
        noteOffTime[track] = 0;
      }
    }
    exitToMenu();
    return;
  }

  if (touch.justPressed) {
    if (isButtonPressed(10, 200, 50, 25)) {
      sequencerPlaying = !sequencerPlaying;
      if (sequencerPlaying) {
        currentStep   = 0;
        lastStepTime  = millis();
      }
      drawSequencerMode();
      return;
    }

    if (isButtonPressed(70, 200, 50, 25)) {
      for (int t = 0; t < SEQ_TRACKS; t++)
        for (int s = 0; s < SEQ_STEPS; s++)
          sequencePattern[t][s] = false;
      drawSequencerGrid();
      return;
    }

    if (isButtonPressed(130, 200, 40, 25)) {
      bpm = max(60, bpm - 1);
      stepInterval = 60000 / bpm / 4;
      drawSequencerMode();
      return;
    }

    if (isButtonPressed(180, 200, 40, 25)) {
      bpm = min(200, bpm + 1);
      stepInterval = 60000 / bpm / 4;
      drawSequencerMode();
      return;
    }

    // Grid interaction
    int gridX   = 45;
    int gridY   = 50;
    int cellW   = 15;
    int cellH   = 28;
    int spacing = 1;

    for (int track = 0; track < SEQ_TRACKS; track++) {
      for (int step = 0; step < SEQ_STEPS; step++) {
        int x = gridX + step * (cellW + spacing);
        int y = gridY + track * (cellH + spacing + 3);
        if (isButtonPressed(x, y, cellW, cellH)) {
          toggleSequencerStep(track, step);
          drawSequencerGrid();
          return;
        }
      }
    }
  }

  updateSequencer();
}

void toggleSequencerStep(int track, int step) {
  sequencePattern[track][step] = !sequencePattern[track][step];
}

void updateSequencer() {
  if (!sequencerPlaying) return;

  unsigned long now = millis();

  // Note-offs
  for (int track = 0; track < SEQ_TRACKS; track++) {
    if (noteOffTime[track] > 0 && now >= noteOffTime[track]) {
      sendDrumMIDI(0x80, drumNotes[track], 0);
      noteOffTime[track] = 0;
    }
  }

  if (now - lastStepTime >= (unsigned long)stepInterval) {
    playSequencerStep();
    currentStep  = (currentStep + 1) % SEQ_STEPS;
    lastStepTime = now;
    drawSequencerGrid();
  }
}

void playSequencerStep() {
  // Note lengths in ms – fixed musical values independent of gateLength
  int noteLengths[] = {200, 150, 50, 300};

  unsigned long now = millis();

  for (int track = 0; track < SEQ_TRACKS; track++) {
    if (sequencePattern[track][currentStep]) {
      sendDrumMIDI(0x90, drumNotes[track], 100);
      noteOffTime[track] = now + noteLengths[track];
    }
  }
}

#endif
