# CSI-Visualization Tool on ESP32
Real-time visualization of CSI on ESP32

## Getting started
Before starting, install Espressif IoT Development Framework (ESP-IDF) by following their [step by step installation guide](https://docs.espressif.com/projects/esp-idf/en/v4.3/esp32/get-started/index.html#installation-step-by-step). Notice, this project requires version (v4.3) of ESP-IDF.

To use this tool you will need to build and run an Active Access Point on a separate ESP32, which you can find in the [ESP32 CSI Toolkit](https://github.com/StevenMHernandez/ESP32-CSI-Tool).

### Build and run the Active Station
1. Clone this repository and navigate to the `CSI-Visualization-on-ESP32` repository:

```
git clone --recurse-submodules https://github.com/huyenngn/CSI-Visualization-on-ESP32.git
cd CSI-Visualization-on-ESP32
```
Make sure you're on the "release/v7" branch of the [lvgl](https://github.com/huyenngn/lvgl/tree/release/v7) submodule and the "master" branch of the [lvgl_esp32_drivers](https://github.com/huyenngn/lvgl_esp32_drivers/tree/master) submodule.

2. Build the project by running:
```
idf.py build
```

3. If the build didn't throw any errors, flash your ESP32 with:
```
idf.py -p (YOUR SERIAL PORT) flash
```