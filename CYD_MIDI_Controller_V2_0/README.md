# CYD MIDI Controller V2

Touchscreen Hardware serial , Bluetooth and Wifi MIDI controller for the ESP32-2432S028R "Cheap Yellow Display" (CYD).

### New features added in version 2

**  Settings menu for setting:
    - default midi channel
    - gate length ( delay between note-on and off )
    - BLE2SERIAL on/off ( gateway between hardware serial and BLE MIDI)
    - MIDI THRU on/off
    - Drum note assignments and MIDI channel
    - MIDI 2 CV assignments ( experimental and needs i2c and a MCP4728)
    - WIFI settings for Apple MIDI or RTP MIDI ( experimental)

** MONITOR app : MIDI monitor
** CLOCK app: MIDI Clock / start / stop BPM ; master /slave 
** PGMCH app: Program change sender ( GM patch names )
** SLIDERS app: Control Change sliders ( mod,vol,pan,cutoff, reso, pitch) (1,7,10,74,71,pitchbend);

** Files menu

   - Format SD card
   - Delete dot-files
   - USB XFER: Transfer files to and from the SD card by using the cyd_filetransfer.html webpage in Chrome
   - SEQ: Loading, saving sequences and patterns
   - PLAY: Play MIDI files
   - REC: Record MIDI files
   - SETLIST: Lists pattern files
   - INFO: SD tools , format , delete dot-files
   - SYSEX: Send SYSEX files

## Original Features

### 10 Interactive Modes

- **KEYS** - Virtual piano keyboard with scale and key controls
- **BEATS** - 16-step sequencer with 4 tracks and tempo control
- **ZEN** - Ambient bouncing ball mode for generative music
- **DROP** - Physics-based ball drop with customizable platforms
- **RNG** - Random music generator for creative exploration
- **XY PAD** - Touch-controlled X/Y pad for real-time parameter control
- **ARP** - Arpeggiator with chord-based patterns
- **GRID** - Grid piano with 4ths layout for unique playing style
- **CHORD** - Auto-chord mode with diatonic chord progressions
- **LFO** - Low-frequency oscillator for modulation effects

### Core Features

- **Serial MIDI** - Classic serial MIDI over CN1, pin 27 and 22
- **Bluetooth MIDI** - Wireless connection to DAWs and music software
- **Touchscreen Interface** - Intuitive visual controls optimized for the CYD display
- **Real-time Control** - Low-latency MIDI output
- **Visual Feedback** - Responsive graphics


## What You Need

- **ESP32-2432S028R (CYD)** - ~$15 from AliExpress/Amazon
- Arduino IDE with ESP32 support

## Installation

### 1. Add ESP32 Board Support
1. Go to `File` → `Preferences`
2. Add to "Additional Boards Manager URLs":
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. Go to `Tools` → `Board` → `Boards Manager`
4. Search "ESP32" and install "ESP32 by Espressif Systems"

### 2. Install Libraries
In Arduino IDE Library Manager, install:
- `TFT_eSPI` by Bodmer
- `XPT2046_Touchscreen` by Paul Stoffregen

### 3. Configure TFT_eSPI
Replace the `libraries/TFT_eSPI/User_Setup.h` with the `User_Setup.h` from the repo.

### 4. Upload Code
1. Clone this repo and open `CYD_MIDI_Controller.ino`
2. Select board: `ESP32 Dev Module`
3. Connect CYD and upload
(Lower Upload Speed to `115200` if the sketch isn't uploading)

### 5. Connect
1. Pair "CYD MIDI" via Bluetooth
2. Select as MIDI input in your DAW

## Troubleshooting

- **Upload Speed**: Lower it to `115200` if the sketch isn't uploading
- **Blank screen**: Check TFT_eSPI pin configuration
- **No touch**: Verify touchscreen library installation
- **No Bluetooth**: Restart device and re-pair

## License

Open source - see MIT license file for details.
