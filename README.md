# OscilloScopeFirmware
Firmware for ESP32 CYD Based Dual Channel Oscilloscope. 

# How it Works
At a 1kHz sample rate, the ESP32 MCU samples its GPIO through its own onboard ADC, stores it within a circular buffer, and then utilises TFT_eSPI to create a visual line graph of the oscilloscope probe's voltage records. The two oscilloscope channels are displayed separately, alongside their Y-Gain (displayed as a MAX value instead of a scale) and Time-Base using verticial divider lines, creating 58ms non-adjustable time-base divisions on the visual graph.

For circuitry, the probe uses a 100nF ceramic capacitor to filter noise, a voltage clamping setup using two Schottky Diodes to prevent the voltage going above/below 3.3V and damaging the GPIO pin, and a 2.2:1 voltage divider to enable the probe to be used with 5V devices (voltage values are scaled up in software to account for the divider).

FULL TUTOTRIAL TO BE ON INSTRUCTABLES
README IS CURRENTLY WORK IN PROGRESS
