# Parameter `GPIO`
Default Value: `13`

The GPIO pin to which the signal output of the pulse sensor is connected.

The pulse counter is meant for meters which give one pulse per unit of consumption, e.g. an IR reflectance sensor (TCRT5000)
watching the rotating disc of an old Ferraris electricity meter (one pulse per revolution) or a photo sensor on the pulse LED of a modern meter.
The camera keeps reading the meter register for the exact total, the pulse counter provides a responsive rate (power, flow rate).

Wiring of a TCRT5000 module: `VCC` to `3.3V`, `GND` to `GND`, `DO` (digital output) to this GPIO. Leave `AO` unconnected.
Adjust the potentiometer of the module so that its signal LED only switches while the dark mark of the disc passes the sensor.

Usable pins on the ESP32-CAM:

- `GPIO13` (recommended): usable without restrictions.
- `GPIO12`: strapping pin! It must be **low** while the device boots, otherwise the ESP32 does not start. Most sensor modules pull their output high, so prefer `GPIO13`.
- `GPIO3` / `GPIO1`: the serial console (RX/TX) can no longer be used.

A pin which is already used in the `GPIO` section can not be used for the pulse counter.
