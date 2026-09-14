# Parameter `AlignSequence`
Default Value: `main`

Name of the number sequence (camera reading) with which the pulse derived value gets aligned. Disable the parameter to keep the pulse derived value independent from the camera reading.

If enabled, the pulse counter value is compared with the camera reading after every round. The pulse count at the time the image was taken is used for the comparison.
If the pulse value drifted away from the reading (more than one digit below or more than two digits above the reading), it is set to the reading.
Small deviations within the resolution of the camera reading are ignored, so the pulse value keeps its fine resolution between the readings.

A missed pulse (e.g. the sensor did not see the mark) or an additional pulse (e.g. noise) therefore gets corrected automatically with the next valid camera reading. Readings with an error (e.g. `Rate too high`) are not used.

!!! Note
    Because the camera resolution is coarser than a single pulse, an alignment can lower the pulse value by up to two digits of the camera reading.
    Therefore the pulse value is announced to Home Assistant with the state class `total` when the alignment is enabled and with `total_increasing` otherwise.
