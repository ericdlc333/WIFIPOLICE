# Police — ESP32-S3 Desk Buddy & Passive Wi‑Fi Monitor

Police is a small animated desk buddy for the **Waveshare ESP32-S3-LCD-1.47** board. It patrols the display, reacts to the BOOT button with red-and-blue lights, and passively counts selected nearby Wi‑Fi management frames.

It can also write bounded PCAP capture logs to the board’s built-in microSD card.

## Hardware

- Waveshare ESP32-S3-LCD-1.47
- FAT32-formatted microSD card in the built-in rear card slot (optional, required for on-card logging)
- USB connection to a Windows computer for power, deployment, and optional live PCAP output

No external jumper wires or modules are required.

## What Police Does

### Desk buddy animation

Police walks across a plain display background and cycles through three actions after completing each pass:

1. Plain pacing with swinging arms  
2. A slow hand-tapped, thick black baton animation  
3. A spinning classic key animation  

Police also blinks naturally.

### BOOT button and RGB LED

Press the built-in **BOOT** button to activate a five-second patrol-light event:

- the LCD’s red/blue lights alternate;
- the built-in RGB LED alternates red and blue.

At other times, the built-in RGB LED stays white.

### Passive Wi‑Fi observation

Police uses the ESP32-S3 Wi‑Fi receiver in receive-only promiscuous mode and hops through Wi‑Fi channels 1–11.

It counts:

- **BEACONS** — access-point advertisement frames;
- **PROBES** — probe requests and probe responses used for Wi‑Fi discovery;
- **DEAUTH** — deauthentication management frames.

A deauthentication frame starts an eight-second red/blue RGB alert and makes the on-screen deauthentication line red.

> These are observed frame totals, not confirmed attacks or a count of individual devices. Deauthentication frames can occur normally during reconnects, roaming, weak signal, or router restarts.

## SD Card Logging

When a FAT32 microSD card is inserted in the board’s rear slot, Police creates a new PCAP file at startup:

```text
POLICE00.PCAP
POLICE01.PCAP
POLICE02.PCAP
...
The file contains up to the first 128 bytes of selected received management frames:

beacon frames;
probe request frames;
probe response frames;
deauthentication frames.
Each file is limited to 16 MB so one session does not fill the card. When the limit is reached, the display shows:

text


SD: LOG LIMIT 16MB
When logging is active, the display shows the current log size. If the card cannot be mounted, it shows:

text


SD: CARD NOT READY
To examine a saved capture:

Unplug the board from USB.
Wait a few seconds so the last data is flushed to the card.
Remove the microSD card.
Open the .PCAP file in Wireshark.
USB PCAP Stream
Police also sends the same PCAP data over its native USB serial connection while running.

This output is binary PCAP data, not readable text. A normal serial monitor will show garbled characters and is not appropriate for saving the capture. Use a capture tool that can save the raw USB stream to a .pcap file.

The USB connection is also used by Schematik to deploy firmware.

Screen Status
The bottom of the display shows:

text


WATCH  CH <channel>
BEACONS: <total>
PROBES:  <total>
DEAUTH:  <total>
SD: LOG <size> KB
The active channel changes as Police hops across channels 1 through 11.

Privacy and Responsible Use
This project is designed for passive observation on networks and radio environments you own or are authorized to assess.

Captured management-frame excerpts can contain information such as Wi‑Fi network names and device/access-point MAC addresses. Police does not:

join Wi‑Fi networks;
transmit Wi‑Fi frames;
inject packets;
disconnect devices;
collect passwords;
decrypt protected Wi‑Fi traffic;
capture more than the configured 128-byte excerpt per matching frame.
Protect the PCAP files like other network records, and delete captures you no longer need.

Firmware
The firmware is written in C++ with the Arduino framework and built by Schematik for the ESP32-S3.

Libraries used:

Adafruit GFX Library
Adafruit ST7735 and ST7789 Library
Adafruit NeoPixel
The project enables the ESP32-S3’s native USB CDC connection for deployment and binary PCAP output.

Deploying
Insert a FAT32 microSD card if you want on-card logging.
Plug the Waveshare board into your Windows computer over USB.
Press Deploy in Schematik.
After startup, Police begins its animation, passive channel hopping, and optional SD-card logging automatically.
Notes
The counters reset whenever the board restarts.
The monitor changes channels every 350 ms, so it only counts frames heard while listening on each channel.
The card should only be removed after the board is unplugged.




The firmware is written in C++ using the Arduino framework (arduino-cpp runtime). Its main source file is src/main.cpp.

It uses these libraries:

Adafruit GFX Library — basic drawing functions
Adafruit ST7735 and ST7789 Library — drives the built-in LCD
Adafruit NeoPixel — controls the built-in RGB LED
Built-in ESP32 Arduino libraries for Wi‑Fi monitoring, SD-card access, USB serial output, and SPI display communication
