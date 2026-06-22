#ifndef MIDI_UTILS_H
#define MIDI_UTILS_H

#include "common_definitions.h"
extern HardwareSerial MIDISerial;
extern int drumChannel;  // defined in settings_mode.h

// Scale definitions
Scale scales[] = {
  {"Major", {0, 2, 4, 5, 7, 9, 11, 0}, 7},
  {"Minor", {0, 2, 3, 5, 7, 8, 10, 0}, 7},
  {"Dorian", {0, 2, 3, 5, 7, 9, 10, 0}, 7},
  {"Penta", {0, 2, 4, 7, 9, 0, 0, 0}, 5},
  {"Blues", {0, 3, 5, 6, 7, 10, 0, 0}, 6},
  {"Chrome", {0, 1, 2, 3, 4, 5, 6, 7}, 8}
};
const int NUM_SCALES = 6;

// Forward declaration — appleMidiSend defined in wifi_applemidi.h
#ifdef ENABLE_WIFI
void appleMidiSend(byte status, byte d1, byte d2);
#endif

// MIDI utility functions
void sendMIDI(byte cmd, byte note, byte vel) {
  byte status = cmd + channel - 1;

  // --- BLE MIDI ---
  if (deviceConnected) {
    midiPacket[2] = status;
    midiPacket[3] = note;
    midiPacket[4] = vel;
    pCharacteristic->setValue(midiPacket, 5);
    pCharacteristic->notify();
  }

  // --- DIN MIDI OUT (UART2) ---
  MIDISerial.write(status);
  MIDISerial.write(note);
  MIDISerial.write(vel);

#ifdef ENABLE_WIFI
  // --- AppleMIDI / RTP-MIDI ---
  appleMidiSend(status, note, vel);
#endif

  // --- CV / Gate output via MCP4728 ---
  cvProcessMIDI(status, note, vel);
}


int getNoteInScale(int scaleIndex, int degree, int octave) {
  if (scaleIndex >= NUM_SCALES) return 60;
  
  // If degree exceeds scale notes, wrap to next octave
  int actualDegree = degree % scales[scaleIndex].numNotes;
  int octaveOffset = degree / scales[scaleIndex].numNotes;
  
  int rootNote = 60; // C4
  return rootNote + scales[scaleIndex].intervals[actualDegree] + ((octave - 4 + octaveOffset) * 12);
}

String getNoteNameFromMIDI(int midiNote) {
  static const char* noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  return String(noteNames[midiNote % 12]) + String((midiNote / 12) - 1);
}

void stopAllModes() {
  // All Sound Off (CC 120) + All Notes Off (CC 123): two messages replace 128 note-offs
  sendMIDI(0xB0, 120, 0);
  sendMIDI(0xB0, 123, 0);
  // Also silence drum channel if it differs from the melody channel
  if (drumChannel != channel) {
    byte dc = 0xB0 | (byte)(drumChannel - 1);
    if (deviceConnected) {
      midiPacket[2] = dc; midiPacket[3] = 120; midiPacket[4] = 0;
      pCharacteristic->setValue(midiPacket, 5); pCharacteristic->notify();
      midiPacket[3] = 123;
      pCharacteristic->setValue(midiPacket, 5); pCharacteristic->notify();
    }
    MIDISerial.write(dc);  MIDISerial.write((byte)120); MIDISerial.write((byte)0);
    MIDISerial.write(dc);  MIDISerial.write((byte)123); MIDISerial.write((byte)0);
  }
}

#endif