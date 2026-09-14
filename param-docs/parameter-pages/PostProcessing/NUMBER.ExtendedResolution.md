# Parameter `ExtendedResolution`
Default Value: `false`

Use the decimal place of the last analog counter for increased accuracy.

!!! Note
    This parameter is only supported on the `*-class*` and `*-const` models! See [Choosing-the-Model](../Choosing-the-Model) for details.

!!! Note
    If the digits of your meter roll in the opposite direction (the next digit enters the digit window from the top), additionally enable [`ExtendedResolutionInvert`](../Parameters/#PostProcessing-NUMBER.ExtendedResolutionInvert).

!!! Note
    If you edit the config file manually, you must prefix this parameter with `<NUMBER>` followed by a dot (eg. `main.ExtendedResolution`). The reason is that this parameter is specific for each `<NUMBER>` (`<NUMBER>` is the name of the number sequence defined in the ROI's).
