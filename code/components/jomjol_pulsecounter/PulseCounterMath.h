#pragma once

#ifndef PULSECOUNTERMATH_H
#define PULSECOUNTERMATH_H

#include <cmath>
#include <cstdint>

/**
 * Pure helper functions of the pulse counter.
 * They have no dependency on ESP-IDF, so they can be unit tested on a host machine.
 */
namespace PulseCounterMath {

/**
 * Number of decimal places needed to display a value whose resolution is one pulse (1 / pulsesPerUnit).
 * If one pulse is not an exact multiple of the displayed resolution, one more decimal place is used.
 * Examples: 75 pulses/kWh -> 3 decimals, 1000 pulses/kWh -> 3 decimals, 600 pulses/kWh -> 4 decimals.
 */
inline int decimalsForResolution(double pulsesPerUnit) {
    if (!(pulsesPerUnit > 0)) {
        return 1;
    }

    int decimals = (int) std::ceil(std::log10(pulsesPerUnit));
    if (decimals < 0) {
        decimals = 0;
    }

    double stepsPerDigit = std::pow(10.0, decimals) / pulsesPerUnit;
    if (std::fabs(stepsPerDigit - std::round(stepsPerDigit)) > 1e-9) {
        decimals += 1;
    }

    if (decimals < 1) {
        decimals = 1;
    }
    if (decimals > 6) {
        decimals = 6;
    }

    return decimals;
}

/**
 * Rate in value units per hour.
 *
 * intervalSeconds: time between the two most recent pulses (<= 0: not known yet)
 * elapsedSeconds:  time since the most recent pulse (< 0: no pulse yet)
 * timeoutSeconds:  after this long without a pulse, the rate is reported as 0 (<= 0: never)
 *
 * While no new pulse arrives, the rate can not be higher than one pulse per elapsed time.
 * Therefore the longer of the two durations is used, which lets the rate decay smoothly
 * when the consumption stops instead of reporting the last (now too high) rate.
 */
inline double ratePerHour(double pulsesPerUnit, double intervalSeconds, double elapsedSeconds, double timeoutSeconds) {
    if (!(pulsesPerUnit > 0) || (intervalSeconds <= 0) || (elapsedSeconds < 0)) {
        return 0;
    }

    if ((timeoutSeconds > 0) && (elapsedSeconds > timeoutSeconds)) {
        return 0;
    }

    double effectiveInterval = (intervalSeconds > elapsedSeconds) ? intervalSeconds : elapsedSeconds;

    return 3600.0 / (effectiveInterval * pulsesPerUnit);
}

/** Meter value derived from the pulse count: offset + count / pulsesPerUnit */
inline double valueFromCount(uint32_t count, double offset, double pulsesPerUnit) {
    if (!(pulsesPerUnit > 0)) {
        return offset;
    }

    return offset + ((double) count / pulsesPerUnit);
}

/** Offset needed so that the given count corresponds to the given value */
inline double offsetForValue(double value, uint32_t count, double pulsesPerUnit) {
    if (!(pulsesPerUnit > 0)) {
        return value;
    }

    return value - ((double) count / pulsesPerUnit);
}

/** One digit of a reading with the given number of decimal places (e.g. 1 decimal -> 0.1) */
inline double readingResolution(int readingDecimals) {
    if (readingDecimals < 0) {
        readingDecimals = 0;
    }
    if (readingDecimals > 9) {
        readingDecimals = 9;
    }

    return std::pow(10.0, -readingDecimals);
}

/**
 * Decides if the pulse derived value has drifted away from a reading of the meter register.
 *
 * A register showing R with a resolution r means the true value is somewhere in [R, R + r).
 * The pulse value is left alone as long as it is within one digit below the reading and
 * within two digits above it. Anything outside this band is a real drift (missed or
 * additional pulses) and gets corrected.
 */
inline bool needsAlignment(double pulseValue, double reading, int readingDecimals) {
    double r = readingResolution(readingDecimals);
    const double epsilon = 1e-9;

    return (pulseValue < (reading - r - epsilon)) || (pulseValue > (reading + (2 * r) + epsilon));
}

} // namespace PulseCounterMath

#endif // PULSECOUNTERMATH_H
