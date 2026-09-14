# Parameter `DebounceTime`
Default Value: `500`

Minimum time in milliseconds between two pulses. Pulses which follow the previous one faster than this are ignored.

This filters the bouncing and noise of the sensor signal. Choose a value which is clearly shorter than the shortest possible pulse interval of your meter at maximum consumption, e.g. for a Ferraris meter with 75 revolutions per kWh at 10 kW one revolution takes 4.8 seconds, so `500` ms is safe.
Meters with many pulses per unit (e.g. 1000 imp/kWh) need a shorter time (e.g. `50`).

If you see far too high rates or a growing pulse count although nothing is consumed, increase this value and adjust the sensitivity of the sensor. Set it to `0` to disable the filter.
