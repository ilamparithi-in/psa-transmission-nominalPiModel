# Transmission Line Nominal Pi Model

This repo contains the data acquisition and presentation part of the hardware project. 

## Planned Features

- RMS and Waveform: Sending End Voltage, Sending End Current, Receiving End Voltage, Receiving End Current acquisition (Voltage: ZMPT101B; Current: ACS712)

- Protection Relay: Try to sense over-current and trip the relay within 5 cycles (is this even possible with an ESP32 and an ACS712?)

- Export data through Serial out (USB) and Bluetooth to Web Serial API