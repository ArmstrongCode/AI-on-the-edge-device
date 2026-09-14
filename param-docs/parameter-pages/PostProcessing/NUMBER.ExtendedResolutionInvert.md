# Parameter `ExtendedResolutionInvert`
Default Value: `false`

Inverts the direction of the additional decimal place provided by `ExtendedResolution`.

Enable this if the digits of your meter roll in the opposite direction, i.e. the next digit enters the digit window from the **top** instead of from the bottom.
The models are trained on the usual direction (the next digit comes from the bottom). On meters with the opposite direction the additional decimal place counts backwards during a digit transition (eg. `6.0`, `6.9`, `6.8`, ..., `6.1`, `7.0`), which leads to wrong intermediate values and negative rates.
With this parameter enabled the additional decimal place gets mirrored (`x.1` becomes `x.9`, `x.2` becomes `x.8`, ..., `x.0` and `x.5` stay unchanged), so the value counts upwards again.

!!! Note
    This parameter only has an effect if `ExtendedResolution` is enabled and the number sequence consists of digit ROIs only (the additional decimal place is taken from the last digit).
    For analog pointers turning counterclockwise use the `CCW` option of the analog ROI instead.

!!! Note
    If you edit the config file manually, you must prefix this parameter with `<NUMBER>` followed by a dot (eg. `main.ExtendedResolutionInvert`). The reason is that this parameter is specific for each `<NUMBER>` (`<NUMBER>` is the name of the number sequence defined in the ROI's).
