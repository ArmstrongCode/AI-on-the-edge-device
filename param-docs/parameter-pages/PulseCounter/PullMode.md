# Parameter `PullMode`
Default Value: `pullup`

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

Internal pull resistor of the GPIO:

- `pullup`: internal pull-up resistor enabled (recommended, keeps the input defined if the sensor is disconnected)
- `pulldown`: internal pull-down resistor enabled
- `none`: no internal pull resistor (use it if the sensor module has its own pull resistor and you get wrong readings)
