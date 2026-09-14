#pragma once

#ifndef PULSECOUNTER_H
#define PULSECOUNTER_H

#include <string>
#include <functional>
#include <time.h>

#include <esp_http_server.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

#include "PulseCounterMath.h"

/* File on the SD card in which the pulse count and the offset are persisted */
#define PULSECOUNTER_STATE_FILE "/sdcard/config/pulsecounter.ini"

/* Number of recent pulse timestamps which are kept to determine the count at the time an image was taken */
#define PULSECOUNTER_PULSE_HISTORY 128

/* The state is written to the SD card at most once per this interval (only if something changed) */
#define PULSECOUNTER_SAVE_INTERVAL_S 60

/* Without a new pulse, the (decaying) rate is re-published at most once per this interval */
#define PULSECOUNTER_PUBLISH_INTERVAL_S 30

/* After a pulse, the topics are published at most once per this interval (pulses arriving faster get combined) */
#define PULSECOUNTER_PUBLISH_MIN_INTERVAL_S 2

/* Callback to check if a GPIO is already reserved by another module (e.g. the [GPIO] section) */
typedef std::function<bool(gpio_num_t)> PulseCounterPinReservedCheck;

/**
 * Counts pulses of an external sensor (e.g. an IR reflectance sensor watching the rotating disc of a
 * Ferraris electricity meter or the LED of a modern meter) and derives a meter value and the current
 * rate (power, flow) from them.
 *
 * The camera based digitization is slow (one reading per round) and coarse (resolution of the last
 * digit), so it is well suited for the total consumption, but not for the current rate.
 * The pulse counter fills this gap: every pulse is time stamped, the rate is calculated from the
 * interval between the pulses and published immediately.
 *
 * Optionally the pulse derived value gets aligned with the camera reading of a number sequence,
 * so both values do not drift apart.
 */
class PulseCounter {
public:
    PulseCounter(std::string configFile, httpd_handle_t httpServer);
    ~PulseCounter();

    /** Reads the [PulseCounter] config section and starts counting (if enabled). Can be called again to re-init. */
    bool init(PulseCounterPinReservedCheck isPinReserved = NULL);
    /** Stops counting and saves the state. */
    void deinit();
    bool isEnabled() { return _enabled; }

    uint32_t getCount();
    double getValue();
    double getRatePerHour();
    double getRatePerMinute();
    double getRatePerTimeUnit();
    double getLastInterval();
    time_t getLastPulseTime();
    int getDecimals() { return _decimals; }
    double getPulsesPerUnit() { return _pulsesPerUnit; }
    gpio_num_t getGPIO() { return _gpio; }
    std::string getAlignSequence() { return _alignSequence; }

    std::string getValueString();
    std::string getRateString();
    std::string getRatePerTimeUnitString();
    std::string getLastIntervalString();
    std::string getLastPulseTimeString();
    std::string getJSON(std::string lineEnd = "\n");
    std::string getReadout(std::string type);

    /** Sets the meter value (the pulse count is kept, the offset is adjusted) */
    bool setValue(double value);
    /** Aligns the pulse derived value with a reading of the meter register taken at readingTime */
    bool alignWithReading(double reading, int readingDecimals, time_t readingTime);

    /* Used by the FreeRTOS task, the ISR and the HTTP server; not meant to be called directly */
    void taskLoop();
    void isrPulse();
    esp_err_t handleHttpRequest(httpd_req_t *req);

#ifdef ENABLE_MQTT
    void handleMQTTconnect();
    bool handleMQTTSetValue(std::string topic, char *data, int data_len);
    bool publishHomeAssistantDiscovery(int qos);
#endif // ENABLE_MQTT

private:
    std::string _configFile;
    httpd_handle_t _httpServer;

    /* Configuration */
    bool _enabled;
    gpio_num_t _gpio;
    gpio_int_type_t _edge;
    bool _pullUp;
    bool _pullDown;
    double _pulsesPerUnit;
    int _debounceMs;
    int _rateTimeoutS;
    std::string _alignSequence;
    int _decimals;

    /* Owned by the ISR */
    volatile uint32_t _isrCount;
    volatile int64_t _isrLastPulseUs;
    int64_t _debounceUs;

    /* State, protected by _mutex */
    SemaphoreHandle_t _mutex;
    uint32_t _countBase;        // Pulses counted before this boot (persisted)
    double _offset;             // Value = _offset + count / _pulsesPerUnit (persisted)
    int64_t _lastPulseUs;       // esp_timer time of the last pulse, -1 if none since boot
    double _lastIntervalS;      // Time between the two most recent pulses, 0 if not known
    time_t _lastPulseTime;      // Wall clock time of the last pulse (persisted)
    bool _dirty;                // State changed since it was saved the last time
    int64_t _lastSaveUs;
    time_t _pulseHistory[PULSECOUNTER_PULSE_HISTORY];
    int _pulseHistoryHead;
    uint32_t _pulseHistoryStored;
    int64_t _lastPublishUs;
    std::string _lastPublishedRate;
    bool _publishPending;       // A pulse arrived, the topics have to be published as soon as the minimum interval allows it

    /* Runtime */
    TaskHandle_t _taskHandle;
    QueueHandle_t _queue;
    volatile bool _stopRequested;
    bool _isrInstalled;
    bool _mqttRegistered;
    bool _uriRegistered;

    bool readConfig();
    bool loadState();
    bool saveState();
    void processPulse(int64_t pulseUs);
    double rateAt(int64_t nowUs);
    uint32_t currentCount();
    uint32_t countAt(time_t when);
    void registerUri();
    void publish(bool pulseReceived);
    void lock();
    void unlock();
};

void pulsecounter_create(httpd_handle_t server);
void pulsecounter_init(PulseCounterPinReservedCheck isPinReserved = NULL);
void pulsecounter_deinit();
void pulsecounter_destroy();
PulseCounter *pulsecounter_get();

#endif // PULSECOUNTER_H
