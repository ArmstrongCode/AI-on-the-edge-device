# Parameter `PulsesPerUnit`
Default Value: `75`

Number of pulses per unit of the meter value, as printed on the nameplate of the meter.

- Ferraris electricity meter: revolutions of the disc per kWh, printed as e.g. `75 U/kWh`, `375 r/kWh` or `600 rev/kWh`
- Modern electricity meter with pulse LED: e.g. `1000 imp/kWh`
- Water/gas meter with a pulse output: e.g. `100 imp/m³`

Decimal values are allowed (e.g. `166.66`). The unit of the value is the same as the unit of the camera reading (e.g. kWh), the rate is calculated as units per hour (e.g. kW).

The pulse derived value is `Pulses / PulsesPerUnit + Offset`. The offset gets set when you set the value (overview page, REST API `/pulsecounter?set=1234.5`, MQTT topic `pulse/set_value`) or by the alignment with the camera reading (see `AlignSequence`).
