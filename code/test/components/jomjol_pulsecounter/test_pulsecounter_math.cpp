#include <unity.h>
#include "PulseCounterMath.h"

using namespace PulseCounterMath;

void test_pulsecounter_decimals()
{
    TEST_ASSERT_EQUAL_INT(3, decimalsForResolution(75));     // 1/75 = 0.0133
    TEST_ASSERT_EQUAL_INT(3, decimalsForResolution(1000));   // 1/1000 = 0.001
    TEST_ASSERT_EQUAL_INT(2, decimalsForResolution(100));
    TEST_ASSERT_EQUAL_INT(4, decimalsForResolution(600));
    TEST_ASSERT_EQUAL_INT(1, decimalsForResolution(1));
    TEST_ASSERT_EQUAL_INT(1, decimalsForResolution(0));      // invalid input
}

void test_pulsecounter_rate()
{
    // 75 pulses per kWh, 48 seconds between the pulses => 1 kW
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 1.0, ratePerHour(75, 48, 10, 300));
    // No new pulse for 96 seconds => the rate decays to 0.5 kW
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.5, ratePerHour(75, 48, 96, 300));
    // Timeout => 0
    TEST_ASSERT_EQUAL_FLOAT(0, ratePerHour(75, 48, 301, 300));
    // Interval not known yet / no pulse yet / invalid configuration => 0
    TEST_ASSERT_EQUAL_FLOAT(0, ratePerHour(75, 0, 10, 300));
    TEST_ASSERT_EQUAL_FLOAT(0, ratePerHour(75, 48, -1, 300));
    TEST_ASSERT_EQUAL_FLOAT(0, ratePerHour(0, 48, 10, 300));
}

void test_pulsecounter_value()
{
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 1002.0, valueFromCount(150, 1000.0, 75));
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 1000.0, offsetForValue(1002.0, 150, 75));
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 12345.6, valueFromCount(150, offsetForValue(12345.6, 150, 75), 75));
}

void test_pulsecounter_alignment()
{
    // Reading 12345.6 with one decimal: the pulse value may be within [12345.5, 12345.8]
    TEST_ASSERT_FALSE(needsAlignment(12345.6, 12345.6, 1));
    TEST_ASSERT_FALSE(needsAlignment(12345.5, 12345.6, 1));
    TEST_ASSERT_FALSE(needsAlignment(12345.8, 12345.6, 1));
    TEST_ASSERT_TRUE(needsAlignment(12345.49, 12345.6, 1));
    TEST_ASSERT_TRUE(needsAlignment(12345.81, 12345.6, 1));
    TEST_ASSERT_TRUE(needsAlignment(0.0, 12345.6, 1));

    // Reading without decimals
    TEST_ASSERT_FALSE(needsAlignment(99.4, 100, 0));
    TEST_ASSERT_TRUE(needsAlignment(98.9, 100, 0));
    TEST_ASSERT_TRUE(needsAlignment(102.1, 100, 0));
}

void test_pulsecounter_math()
{
    test_pulsecounter_decimals();
    test_pulsecounter_rate();
    test_pulsecounter_value();
    test_pulsecounter_alignment();
}
