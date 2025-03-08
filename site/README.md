ESP32 Radio Streaming Project
This project is an audio streaming application for the ESP32 microcontroller that connects to an internet radio station and plays audio through a connected audio codec (I2S). The radio station list is managed via a configuration file and can be changed dynamically at runtime. The application also handles interruptions in the stream (e.g., due to Wi-Fi disconnection) and attempts to reconnect automatically.

Features
Internet Radio Streaming: Connects to HTTP-based radio stations using stream URLs stored in a text file.
Dynamic Station Management: The user can switch between different radio stations in real-time using a simple command interface.
Error Handling: Automatically detects and recovers from interruptions, such as Wi-Fi disconnections, by restarting the stream.
Audio Playback: Decodes and plays the audio stream using a codec chip and I2S.
Display Integration: Shows current station information on an OLED display, such as the station name.
Station List: The list of stations is stored in a text file (stations.txt), which can be easily edited to add new stations.
Hardware Requirements
ESP32 Development Board: A development board with Wi-Fi support.
I2S Audio Codec: An audio codec chip (e.g., I2S, MP3 decoder) for audio output.
OLED Display (Optional): A small display to show information like the current station name and status.
Speakers/Headphones: To listen to the stream via the audio codec.
Software Requirements
ESP-IDF: The Espressif IoT Development Framework (ESP-IDF) is used to develop this project for the ESP32.
Libraries: The project uses several libraries provided by Espressif, including the audio components for I2S, MP3, AAC, and HTTP streaming.
Getting Started
1. Setup ESP32 Environment
Ensure that you have the ESP-IDF set up on your machine. Follow the official guide to get started with ESP32 development:
ESP-IDF Setup Guide

2. Clone the Repository
Clone the repository containing the project to your local machine:

bash
Kopiuj
Edytuj
git clone https://github.com/yourusername/esp32-radio.git
cd esp32-radio
3. Configure the Project
In the sdkconfig file or using menuconfig, set the necessary options:

Wi-Fi credentials (SSID and password).
Audio codec configuration.
OLED display configuration (if applicable).
4. Add Radio Stations
Edit the stations.txt file to add your desired radio stations. Each station should be in the format:

arduino
Kopiuj
Edytuj
Station Name | Stream URL | Genre
For example:

nginx
Kopiuj
Edytuj
Cool FM | http://stream.coolfm.com:8000 | Pop
Classic Rock | http://stream.classicrock.com:8000 | Rock
5. Build and Flash the Application
Use the following commands to build and flash the application to the ESP32:

bash
Kopiuj
Edytuj
idf.py build
idf.py flash
idf.py monitor
6. Control the Radio
Once the radio is running, the system will automatically play the first station in the list. You can change stations by calling the change_radio_station() function and passing the station index.

How It Works
Loading Stations: The application loads the list of radio stations from the stations.txt file at startup.
Audio Pipeline: The audio pipeline is created with the following components:
HTTP Stream Reader: Reads audio data from the internet stream.
MP3/AAC Decoder: Decodes the audio stream.
I2S Stream Writer: Sends the decoded audio data to the I2S audio codec for playback.
Error Handling: If there is an issue with the stream (e.g., due to Wi-Fi disconnection), the system will attempt to reconnect by stopping and resetting the audio pipeline, and then restarting it.
Display: The current station is shown on an OLED display, along with a "playing" or "paused" message.
Error Handling
Stream Failures: If the stream URL is unreachable or the connection is lost, the application will attempt to reconnect automatically.
Wi-Fi Disconnection: If Wi-Fi is disconnected, the system will retry to reconnect to the stream after the Wi-Fi connection is restored.
Station Change: The user can switch stations dynamically using the change_radio_station() function. The system will stop the current stream, load the new station, and resume playback.
Advanced Features
Automatic Station Switching: The application can be modified to support automatic switching between stations based on a timer or user input.
Multiple Audio Formats: The system supports multiple audio formats like MP3 and AAC through the decoder components.
Contributing
Feel free to contribute by submitting issues or pull requests. If you have any feature requests or bug fixes, open an issue and we'll be happy to review it
