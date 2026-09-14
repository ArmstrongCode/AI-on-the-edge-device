# Parameter `Edge`
Default Value: `rising-edge`

Signal edge which is counted as a pulse:

- `rising-edge`: the signal changes from low to high
- `falling-edge`: the signal changes from high to low

Every revolution (or LED flash) must produce exactly one edge of the selected type. Which edge you choose does not matter for the result,
as long as only one edge per pulse is counted. Typical TCRT5000 modules pull their output high while the dark mark of the disc passes, so both edges occur once per revolution.
