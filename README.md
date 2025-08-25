# Storm Radio Project

Storm Radio is an ESP32-based internet radio system with a modern Flutter mobile app and a React web frontend for controlling playback and stations. The project is designed for easy use on both Android devices and the web, with a stylish, storm-themed UI.

## Features
- ESP32 firmware for streaming internet radio stations
- REST API for remote control (station select, play/pause, etc.)
- Flutter app for Android (with persistent IP, auto-connect, and modern UI)
- React web frontend (storm-themed, responsive)
- Easy station management via text file

## Project Structure
```
inetRadio/
├── build/                # Build outputs (ESP32, Flutter, etc.)
├── components/           # ESP32 component sources
├── flutter/              # Flutter mobile app
│   ├── assets/           # App icons and images
│   ├── lib/              # Dart source code
│   └── ...
├── main/                 # ESP32 main firmware
├── managed_components/   # ESP-IDF managed components
├── site/                 # React web frontend
│   ├── src/              # React source code
│   └── public/           # Static assets
├── stations.txt          # List of radio stations
├── README.md             # This file
└── ...
```

## ESP32 Firmware
- Written in C/C++ using ESP-IDF
- Reads stations from `stations.txt`
- Exposes REST API endpoints:
  - `/stations` (GET): List all stations
  - `/set-station` (POST): Set current station
  - `/current-station` (GET): Get current station index
  - `/start` (GET): Start playback
  - `/pause` (GET): Pause playback

## Flutter App
- Located in `flutter/`
- Modern UI, storm theme, persistent IP
- Auto-connects to ESP32 on launch
- Allows station selection, play/pause, refresh
- Custom app icon and logo
- To build and install:
  1. `cd flutter`
  2. `flutter pub get`
  3. `flutter build apk --release`
  4. Install `build/app/outputs/flutter-apk/app-release.apk` on your phone

## React Web Frontend
- Located in `site/`
- Modern, storm-themed design
- Controls ESP32 via REST API
- To run locally:
  1. `cd site`
  2. `npm install`
  3. `npm run dev`

## Station Management
- Edit `stations.txt` to add/remove stations
- Format: one station per line, e.g.:
  ```
  Station Name 1,http://stream.url/1
  Station Name 2,http://stream.url/2
  ```

## Setup Notes
- ESP32 and phone/web must be on the same network
- If using HTTP, AndroidManifest.xml must allow cleartext traffic
- For Android, ensure INTERNET permission is set
- If using emulator, network access to ESP32 may not work—use a real device

## Credits
- Project by SebastianS85
- Flutter, React, and ESP-IDF open source communities

---
For more details, see the README files in each subproject.
