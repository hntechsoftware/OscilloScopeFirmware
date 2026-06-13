# OscilloScopeFirmware
Firmware for ESP32 CYD Based Dual Channel Oscilloscope. 

# How it Works
At a 1kHz sample rate, the ESP32 MCU samples its GPIO through its own onboard ADC, stores it within a circular buffer, and then utilises TFT_eSPI to create a visual line graph of the oscilloscope probe's voltage records. The two oscilloscope channels are displayed separately, alongside their Y-Gain (displayed as a MAX value instead of a scale) and Time-Base using verticial divider lines, creating 58ms non-adjustable time-base divisions on the visual graph.

For circuitry, the probe uses a 100nF ceramic capacitor to filter noise, a voltage clamping setup using two Schottky Diodes to prevent the voltage going above/below 3.3V and damaging the GPIO pin, and a 2.2:1 voltage divider to enable the probe to be used with 5V devices (voltage values are scaled up in software to account for the divider).

# Steps To Program CYD
- Download the libraries TFT_eSPI and XPT2046_Touchscreen through the Library Manager within the sidebar of Arduino IDE
- Add ESP32 boards to the Arduino IDE Boards Manager
- User_Setup.h is a Configuration file for the TFT-eSPI and XPT2046 touchscreens, the file on YOUR computer in `C:\Users\USERNAME\Documents\Arduino\libraries\TFT_eSPI` called User_Setup.h should be replaced with the one in this repository.
- Connect the CYD to the computer, select **ESP32 Dev Module** once the IDE recognises an UNKNOWN device on your COM port, and click Upload Sketch.
- Following successful programming, assemble the probe circuitry as per the Instructables article. Optionally, 3D print a casing.

Read the Full Article on Instructables [HERE.](https://www.instructables.com/Mini-Lab-Portable-Oscilloscope/)
