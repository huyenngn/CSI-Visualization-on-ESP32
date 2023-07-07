# CSI-Visualization-on-ESP32
Real-time visualization of CSI on ESP32

## Getting started
Before starting, install Espressif IoT Development Framework (ESP-IDF) by following their [step by step installation guide](https://docs.espressif.com/projects/esp-idf/en/v4.3/esp32/get-started/index.html#installation-step-by-step). Notice, this project requires version (v4.3) of ESP-IDF.

### Build and run the demo
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

3. If the build didn't throw any errors, flash the demo with:
```
idf.py -p (YOUR SERIAL PORT) flash
```

## How to Contribute
Read [CONTRIBUTING.md](CONTRIBUTING.md) to find out how to start working on the project.
