# Toolchain
This project uses the esp-idf framework v5.4.4 / v5.5.x - it compiles for the ESP32 (Classic), ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6 & ESP32-C5

# Development Rules
For every function there must also be a doc string explaining it.
Keep a focus on safety, security, reliability and performance of the implementation.
String parameters (configuration parameters) must not be available via MAVLink parameters, it is enought if they are available via the REST:API and the web-interface.
The web-interface (frontend) is compiled to a single file with everything included except for some images. Keep it that way. Loading/Requesting additional resources from the ESP32 webserver while loading the web-interface may lead to requests not being served. The webserver may not be able to handle multiple requests (like for loading additional files).



# Build & Execution Environment Setup
Setup a terminal with the correct environment variables and esp-idf version in order to be able to run commands. Run inside a powershell `C:\Espressif\tools\Microsoft.v5.4.4.PowerShell_profile.ps1`.
Once you ran the command inside the terminal you can also re-use the terminal for different commands.

# Build & Flash
To compile for a specific ESP32 chip/board you mus configure the project for that board first (if not configured for it already). Run `idf.py set-target esp32` inside the project root to configure it for the ESP32-Classic. Run `idf.py set-target esp32c3` inside the project root to configure it for the ESP32-C3. Run `idf.py set-target esp32c6` inside the project root to configure it for the ESP32-C6. Run `idf.py set-target esp32c5` inside the project root to configure it for the ESP32-C5. 
Build and flash the firmware to a connected ESP32 using `idf.py build flash`. Run the commands from within the root working directory of the project. No need to monitor the full output of the commands. Just check the last bit of the output for any errors.
To just build run `idf.py build`. To just flash run `idf.py flash`

# Create a release
The version number and build index must be updated inside `main/parameters.h` and `CMakeLists.txt`.
The `$release_foldername` and `$release_name_zip` of `create_release_zip.ps1` need to be adjusted.
Run `create_release_zip.ps1`. It uses pre-defined configurations from the `config-defaults` folder. 

# How to Debug
Monitor the correct execution of the firmware by running `idf.py monitor`. Run the command from within the root working directory of the project.
You can run `idf.py fullclean` to clean the build directory. Sometimes that is not working, then manually delete the `build` folder.
If DroneBridge is running in access point mode and the computer is connected to the access point, you can access the REST:API and web-interface using the address `dronebridge.local` or `192.168.2.1`. There is no support for HTTPS.

# MCP & Documentation for esp-idf
There is an espressif-docs MCP available for using the esp-idf framework.

