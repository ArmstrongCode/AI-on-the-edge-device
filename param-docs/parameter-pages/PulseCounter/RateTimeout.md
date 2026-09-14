# Parameter `RateTimeout`
Default Value: `300`

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

Time in seconds after which the rate is reported as `0` if no new pulse arrives.

While no new pulse arrives, the rate can not be higher than one pulse per elapsed time. Therefore the reported rate decays automatically after the last pulse. After this timeout it is reported as `0`, so a stopped consumption does not leave a small rate hanging around.
Choose it longer than the pulse interval at your lowest consumption (e.g. for a Ferraris meter with 75 revolutions per kWh at 100 W one revolution takes 8 minutes).
