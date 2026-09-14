#include "PulseCounter.h"

#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"

#include "../../include/defines.h"

#include "ClassLogFile.h"
#include "configFile.h"
#include "Helper.h"
#include "time_sntp.h"
#include "basic_auth.h"

#ifdef ENABLE_MQTT
#include "interface_mqtt.h"
#include "server_mqtt.h"
#endif // ENABLE_MQTT

static const char *TAG = "PULSE";

static PulseCounter *pulseCounter = NULL;

/* ------------------------------------------------------------------------------------------------
 * Trampolines
 * ------------------------------------------------------------------------------------------------ */

static void IRAM_ATTR pulsecounter_isr_handler(void *arg)
{
    ((PulseCounter *) arg)->isrPulse();
}

static void pulsecounter_task(void *arg)
{
    ((PulseCounter *) arg)->taskLoop();
}

static esp_err_t pulsecounter_http_handler(httpd_req_t *req)
{
    return ((PulseCounter *) req->user_ctx)->handleHttpRequest(req);
}

static std::string formatDouble(double value, int decimals)
{
    return RundeOutput(value, decimals);
}

/* ------------------------------------------------------------------------------------------------
 * Construction / configuration
 * ------------------------------------------------------------------------------------------------ */

PulseCounter::PulseCounter(std::string configFile, httpd_handle_t httpServer)
{
    _configFile = configFile;
    _httpServer = httpServer;

    _enabled = false;
    _gpio = GPIO_NUM_13;
    _edge = GPIO_INTR_POSEDGE;
    _pullUp = true;
    _pullDown = false;
    _pulsesPerUnit = 75;
    _debounceMs = 500;
    _rateTimeoutS = 300;
    _alignSequence = "";
    _decimals = 3;

    _isrCount = 0;
    _isrLastPulseUs = -1;
    _debounceUs = (int64_t) _debounceMs * 1000;

    _mutex = xSemaphoreCreateMutex();
    _countBase = 0;
    _offset = 0;
    _lastPulseUs = -1;
    _lastIntervalS = 0;
    _lastPulseTime = 0;
    _dirty = false;
    _lastSaveUs = 0;
    _pulseHistoryHead = 0;
    _pulseHistoryStored = 0;
    _lastPublishUs = 0;
    _lastPublishedRate = "";
    _publishPending = false;

    _taskHandle = NULL;
    _queue = NULL;
    _stopRequested = false;
    _isrInstalled = false;
    _mqttRegistered = false;
    _uriRegistered = false;

    registerUri();
}

PulseCounter::~PulseCounter()
{
    deinit();

    if (_queue != NULL) {
        vQueueDelete(_queue);
        _queue = NULL;
    }

    if (_mutex != NULL) {
        vSemaphoreDelete(_mutex);
        _mutex = NULL;
    }
}

void PulseCounter::lock()
{
    if (_mutex != NULL) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
    }
}

void PulseCounter::unlock()
{
    if (_mutex != NULL) {
        xSemaphoreGive(_mutex);
    }
}

bool PulseCounter::readConfig()
{
    ConfigFile configFile = ConfigFile(_configFile);

    if (!configFile.ConfigFileExists()) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Config file " + _configFile + " not found");
        return false;
    }

    std::string line = "";
    bool disabledLine = false;
    bool eof = false;
    bool found = false;

    while (!eof && configFile.GetNextParagraph(line, disabledLine, eof)) {
        std::string paragraph = toUpper(line);

        if ((paragraph == "[PULSECOUNTER]") || (paragraph == ";[PULSECOUNTER]")) {
            found = true;
            break;
        }
    }

    if (!found) {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "No [PulseCounter] section in config, pulse counter is disabled");
        return false;
    }

    if (disabledLine) {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "[PulseCounter] section is disabled");
        return false;
    }

    while (configFile.getNextLine(&line, disabledLine, eof) && !configFile.isNewParagraph(line)) {
        std::vector<std::string> splitted = ZerlegeZeile(line);

        if (splitted.size() < 2) {
            continue;
        }

        std::string param = toUpper(splitted[0]);
        std::string value = trim(splitted[1]);

        if (param == "GPIO") {
            int pin = atoi(value.c_str());

            if ((pin == 1) || (pin == 3) || (pin == 12) || (pin == 13)) {
                _gpio = (gpio_num_t) pin;
            }
            else {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "GPIO" + value + " can not be used for the pulse counter (use 13, 12, 3 or 1)");
                return false;
            }
        }
        else if (param == "EDGE") {
            std::string edge = toLower(value);

            if ((edge == "falling-edge") || (edge == "falling")) {
                _edge = GPIO_INTR_NEGEDGE;
            }
            else if ((edge == "rising-edge") || (edge == "rising")) {
                _edge = GPIO_INTR_POSEDGE;
            }
            else {
                LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Unknown Edge '" + value + "', using rising-edge");
                _edge = GPIO_INTR_POSEDGE;
            }
        }
        else if (param == "PULLMODE") {
            std::string pull = toLower(value);

            if (pull == "pullup") {
                _pullUp = true;
                _pullDown = false;
            }
            else if (pull == "pulldown") {
                _pullUp = false;
                _pullDown = true;
            }
            else if (pull == "none") {
                _pullUp = false;
                _pullDown = false;
            }
            else {
                LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Unknown PullMode '" + value + "', using pullup");
                _pullUp = true;
                _pullDown = false;
            }
        }
        else if (param == "PULSESPERUNIT") {
            double pulsesPerUnit = strtod(value.c_str(), NULL);

            if (pulsesPerUnit > 0) {
                _pulsesPerUnit = pulsesPerUnit;
            }
            else {
                LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Invalid PulsesPerUnit '" + value + "', using " + formatDouble(_pulsesPerUnit, 2));
            }
        }
        else if (param == "DEBOUNCETIME") {
            int debounce = atoi(value.c_str());

            if (debounce >= 0) {
                _debounceMs = debounce;
            }
        }
        else if (param == "RATETIMEOUT") {
            int timeout = atoi(value.c_str());

            if (timeout > 0) {
                _rateTimeoutS = timeout;
            }
        }
        else if (param == "ALIGNSEQUENCE") {
            _alignSequence = value;
        }
    }

    return true;
}

bool PulseCounter::init(PulseCounterPinReservedCheck isPinReserved)
{
    if (_enabled || (_taskHandle != NULL)) {
        deinit(); // Re-init, e.g. after the configuration was changed
    }

    _enabled = false;
    _alignSequence = "";

    if (!readConfig()) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Pulse counter is disabled");
        return false;
    }

    if (isPinReserved && isPinReserved(_gpio)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "GPIO" + std::to_string((int) _gpio) + " is already used in the [GPIO] section, pulse counter stays disabled");
        return false;
    }

    _decimals = PulseCounterMath::decimalsForResolution(_pulsesPerUnit);
    _debounceUs = (int64_t) _debounceMs * 1000;

    lock();
    _isrCount = 0;
    _isrLastPulseUs = -1;
    _countBase = 0;
    _offset = 0;
    _lastPulseUs = -1;
    _lastIntervalS = 0;
    _lastPulseTime = 0;
    _dirty = false;
    _pulseHistoryHead = 0;
    _pulseHistoryStored = 0;
    _lastPublishUs = 0;
    _lastPublishedRate = "";
    _publishPending = false;
    unlock();

    loadState();
    _lastSaveUs = esp_timer_get_time();

    if (_queue == NULL) {
        _queue = xQueueCreate(32, sizeof(int64_t));

        if (_queue == NULL) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to create pulse queue");
            return false;
        }
    }
    else {
        xQueueReset(_queue);
    }

    gpio_config_t io_conf = {};
    io_conf.intr_type = _edge;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << _gpio);
    io_conf.pull_up_en = _pullUp ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = _pullDown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to configure GPIO" + std::to_string((int) _gpio) + " (error " + std::to_string(err) + ")");
        return false;
    }

    /* The ISR service might already be installed by the camera driver or the GPIO handler */
    err = gpio_install_isr_service(ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM);
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to install GPIO ISR service (error " + std::to_string(err) + ")");
        return false;
    }

    _stopRequested = false;
    BaseType_t xReturned = xTaskCreate(&pulsecounter_task, "pulse_counter", 6 * 1024, (void *) this, tskIDLE_PRIORITY + 4, &_taskHandle);
    if (xReturned != pdPASS) {
        _taskHandle = NULL;
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to create the pulse counter task");
        return false;
    }

    err = gpio_isr_handler_add(_gpio, pulsecounter_isr_handler, (void *) this);
    if (err != ESP_OK) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to add ISR handler for GPIO" + std::to_string((int) _gpio) + " (error " + std::to_string(err) + ")");
        _stopRequested = true;
        return false;
    }
    _isrInstalled = true;

#ifdef ENABLE_MQTT
    if (!_mqttRegistered) {
        std::function<void()> connectFunction = std::bind(&PulseCounter::handleMQTTconnect, this);
        MQTTregisterConnectFunction("pulsecounter", connectFunction);

        std::function<bool(int)> discoveryProvider = std::bind(&PulseCounter::publishHomeAssistantDiscovery, this, std::placeholders::_1);
        mqttServer_registerDiscoveryProvider("pulsecounter", discoveryProvider);

        _mqttRegistered = true;
    }
#endif // ENABLE_MQTT

    _enabled = true;

    std::string pull = _pullUp ? "pullup" : (_pullDown ? "pulldown" : "none");
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Pulse counter enabled: GPIO" + std::to_string((int) _gpio) +
                                           ", " + std::string((_edge == GPIO_INTR_NEGEDGE) ? "falling-edge" : "rising-edge") +
                                           ", " + pull +
                                           ", " + formatDouble(_pulsesPerUnit, 2) + " pulses/unit" +
                                           ", debounce " + std::to_string(_debounceMs) + "ms" +
                                           ", rate timeout " + std::to_string(_rateTimeoutS) + "s" +
                                           (_alignSequence.empty() ? "" : (", aligned with sequence '" + _alignSequence + "'")) +
                                           ", count " + std::to_string(getCount()) + ", value " + getValueString());

    return true;
}

void PulseCounter::deinit()
{
    if (_isrInstalled) {
        gpio_isr_handler_remove(_gpio);
        gpio_intr_disable(_gpio);
        _isrInstalled = false;
    }

    if (_taskHandle != NULL) {
        _stopRequested = true;

        /* The task ends itself (it may be in the middle of a MQTT publish, so it is not killed) */
        for (int i = 0; (i < 50) && (_taskHandle != NULL); ++i) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        if (_taskHandle != NULL) {
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Pulse counter task did not stop in time, killing it");
            vTaskDelete(_taskHandle);
            _taskHandle = NULL;
        }
    }

    if (_enabled) {
        saveState();
    }

#ifdef ENABLE_MQTT
    if (_mqttRegistered) {
        MQTTunregisterConnectFunction("pulsecounter");
        mqttServer_unregisterDiscoveryProvider("pulsecounter");
        _mqttRegistered = false;
    }
#endif // ENABLE_MQTT

    _enabled = false;
}

void PulseCounter::registerUri()
{
    if (_uriRegistered || (_httpServer == NULL)) {
        return;
    }

    ESP_LOGI(TAG, "server_pulsecounter - Registering URI handlers");

    httpd_uri_t uri = { };
    uri.method = HTTP_GET;
    uri.uri = "/pulsecounter";
    uri.handler = APPLY_BASIC_AUTH_FILTER(pulsecounter_http_handler);
    uri.user_ctx = (void *) this;
    httpd_register_uri_handler(_httpServer, &uri);

    _uriRegistered = true;
}

/* ------------------------------------------------------------------------------------------------
 * Persistence
 * ------------------------------------------------------------------------------------------------ */

bool PulseCounter::loadState()
{
    FILE *pFile = fopen(PULSECOUNTER_STATE_FILE, "r");

    if (pFile == NULL) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "No saved pulse counter state found, starting at 0");
        return false;
    }

    uint32_t count = 0;
    double offset = 0;
    time_t lastPulseTime = 0;
    char zw[256];

    while (fgets(zw, sizeof(zw), pFile)) {
        std::vector<std::string> splitted = ZerlegeZeile(trim(std::string(zw)));

        if (splitted.size() < 2) {
            continue;
        }

        std::string key = toUpper(splitted[0]);

        if (key == "COUNT") {
            count = (uint32_t) strtoul(splitted[1].c_str(), NULL, 10);
        }
        else if (key == "OFFSET") {
            offset = strtod(splitted[1].c_str(), NULL);
        }
        else if (key == "LASTPULSETIME") {
            lastPulseTime = (time_t) strtoll(splitted[1].c_str(), NULL, 10);
        }
    }

    fclose(pFile);

    lock();
    _countBase = count;
    _offset = offset;
    _lastPulseTime = lastPulseTime;
    unlock();

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Loaded pulse counter state: count " + std::to_string(count) +
                                           ", offset " + formatDouble(offset, 6) +
                                           ", last pulse " + getLastPulseTimeString());

    return true;
}

bool PulseCounter::saveState()
{
    lock();
    uint32_t count = currentCount();
    double offset = _offset;
    time_t lastPulseTime = _lastPulseTime;
    _dirty = false;
    _lastSaveUs = esp_timer_get_time();
    unlock();

    FILE *pFile = fopen(PULSECOUNTER_STATE_FILE, "w");

    if (pFile == NULL) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to write " + std::string(PULSECOUNTER_STATE_FILE));
        lock();
        _dirty = true;
        unlock();
        return false;
    }

    fprintf(pFile, "Count = %lu\n", (unsigned long) count);
    fprintf(pFile, "Offset = %.6f\n", offset);
    fprintf(pFile, "LastPulseTime = %lld\n", (long long) lastPulseTime);
    fclose(pFile);

    ESP_LOGD(TAG, "Saved state: count %lu, offset %.6f", (unsigned long) count, offset);

    return true;
}

/* ------------------------------------------------------------------------------------------------
 * Counting
 * ------------------------------------------------------------------------------------------------ */

void IRAM_ATTR PulseCounter::isrPulse()
{
    int64_t now = esp_timer_get_time();

    /* Software debounce: ignore edges which follow the previous one too quickly */
    if ((_isrLastPulseUs >= 0) && ((now - _isrLastPulseUs) < _debounceUs)) {
        return;
    }

    _isrLastPulseUs = now;
    _isrCount = _isrCount + 1;

    BaseType_t higherPriorityTaskWoken = pdFALSE;
    xQueueSendToBackFromISR(_queue, (void *) &now, &higherPriorityTaskWoken);

    if (higherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

void PulseCounter::taskLoop()
{
    int64_t pulseUs;

    ESP_LOGD(TAG, "Pulse counter task started");

    while (!_stopRequested) {
        bool pulseReceived = (xQueueReceive(_queue, &pulseUs, pdMS_TO_TICKS(1000)) == pdTRUE);
        int64_t now = esp_timer_get_time();

        if (pulseReceived) {
            processPulse(pulseUs);
            _publishPending = true;
        }

        if (_publishPending) {
            /* Publish the new state, but not more often than every few seconds (meters with many pulses per unit) */
            if ((now - _lastPublishUs) >= ((int64_t) PULSECOUNTER_PUBLISH_MIN_INTERVAL_S * 1000000)) {
                publish(true);
                _publishPending = false;
            }
        }
        else if (!pulseReceived) {
            /* No pulse: the rate decays, re-publish it from time to time */
            publish(false);
        }

        bool save = false;

        lock();
        if (_dirty && ((now - _lastSaveUs) >= ((int64_t) PULSECOUNTER_SAVE_INTERVAL_S * 1000000))) {
            save = true;
        }
        unlock();

        if (save) {
            saveState();
        }
    }

    ESP_LOGD(TAG, "Pulse counter task stopped");
    _taskHandle = NULL;
    vTaskDelete(NULL);
}

void PulseCounter::processPulse(int64_t pulseUs)
{
    time_t now;
    time(&now);
    int64_t nowUs = esp_timer_get_time();
    time_t pulseTime = now - (time_t) ((nowUs - pulseUs) / 1000000);

    lock();

    if (_lastPulseUs >= 0) {
        _lastIntervalS = (double) (pulseUs - _lastPulseUs) / 1000000.0;
    }
    else if ((_lastPulseTime > 0) && getTimeIsSet() && (pulseTime > _lastPulseTime) &&
             (difftime(pulseTime, _lastPulseTime) <= _rateTimeoutS)) {
        /* First pulse after a restart: the time of the last pulse before the restart is known */
        _lastIntervalS = difftime(pulseTime, _lastPulseTime);
    }
    else {
        _lastIntervalS = 0;
    }

    _lastPulseUs = pulseUs;
    _lastPulseTime = pulseTime;

    _pulseHistory[_pulseHistoryHead] = pulseTime;
    _pulseHistoryHead = (_pulseHistoryHead + 1) % PULSECOUNTER_PULSE_HISTORY;
    if (_pulseHistoryStored < PULSECOUNTER_PULSE_HISTORY) {
        _pulseHistoryStored++;
    }

    _dirty = true;

    uint32_t count = currentCount();
    double interval = _lastIntervalS;
    double rate = rateAt(nowUs);

    unlock();

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Pulse #" + std::to_string(count) +
                                            ", interval " + formatDouble(interval, 1) + "s" +
                                            ", rate " + formatDouble(rate, 4) + " units/h" +
                                            ", value " + getValueString());
}

uint32_t PulseCounter::currentCount()
{
    return _countBase + _isrCount;
}

/** Count at a point in time in the past (best effort, based on the pulse history). Call with the mutex held. */
uint32_t PulseCounter::countAt(time_t when)
{
    uint32_t count = currentCount();
    uint32_t newer = 0;

    for (uint32_t i = 0; i < _pulseHistoryStored; ++i) {
        int index = (_pulseHistoryHead - 1 - (int) i + (2 * PULSECOUNTER_PULSE_HISTORY)) % PULSECOUNTER_PULSE_HISTORY;

        if (_pulseHistory[index] > when) {
            newer++;
        }
        else {
            break;
        }
    }

    if ((newer >= _pulseHistoryStored) && (_pulseHistoryStored == PULSECOUNTER_PULSE_HISTORY)) {
        return count; // History too short to know, use the current count
    }

    return count - newer;
}

/** Current rate in units per hour. Call with the mutex held. */
double PulseCounter::rateAt(int64_t nowUs)
{
    double elapsed = -1;

    if (_lastPulseUs >= 0) {
        elapsed = (double) (nowUs - _lastPulseUs) / 1000000.0;
    }

    return PulseCounterMath::ratePerHour(_pulsesPerUnit, _lastIntervalS, elapsed, _rateTimeoutS);
}

/* ------------------------------------------------------------------------------------------------
 * Readouts
 * ------------------------------------------------------------------------------------------------ */

uint32_t PulseCounter::getCount()
{
    lock();
    uint32_t count = currentCount();
    unlock();

    return count;
}

double PulseCounter::getValue()
{
    lock();
    double value = PulseCounterMath::valueFromCount(currentCount(), _offset, _pulsesPerUnit);
    unlock();

    return value;
}

double PulseCounter::getRatePerHour()
{
    lock();
    double rate = rateAt(esp_timer_get_time());
    unlock();

    return rate;
}

double PulseCounter::getRatePerMinute()
{
    return getRatePerHour() / 60.0;
}

double PulseCounter::getRatePerTimeUnit()
{
#ifdef ENABLE_MQTT
    if (getTimeUnit() == "h") {
        return getRatePerHour();
    }
#endif // ENABLE_MQTT

    return getRatePerMinute();
}

double PulseCounter::getLastInterval()
{
    lock();
    double interval = _lastIntervalS;
    unlock();

    return interval;
}

time_t PulseCounter::getLastPulseTime()
{
    lock();
    time_t lastPulseTime = _lastPulseTime;
    unlock();

    return lastPulseTime;
}

std::string PulseCounter::getValueString()
{
    return formatDouble(getValue(), _decimals);
}

std::string PulseCounter::getRateString()
{
    return formatDouble(getRatePerMinute(), 6);
}

std::string PulseCounter::getRatePerTimeUnitString()
{
    return formatDouble(getRatePerTimeUnit(), 4);
}

std::string PulseCounter::getLastIntervalString()
{
    double interval = getLastInterval();

    if (interval <= 0) {
        return "";
    }

    return formatDouble(interval, 1);
}

std::string PulseCounter::getLastPulseTimeString()
{
    time_t lastPulseTime = getLastPulseTime();

    if (lastPulseTime <= 0) {
        return "";
    }

    return ConvertTimeToString(lastPulseTime, PREVALUE_TIME_FORMAT_OUTPUT);
}

std::string PulseCounter::getReadout(std::string type)
{
    type = toLower(type);

    if (type == "value") {
        return getValueString();
    }
    if (type == "count") {
        return std::to_string(getCount());
    }
    if (type == "rate") {
        return getRateString();
    }
    if (type == "rate_per_hour") {
        return formatDouble(getRatePerHour(), 4);
    }
    if (type == "rate_per_time_unit") {
        return getRatePerTimeUnitString();
    }
    if (type == "interval") {
        return getLastIntervalString();
    }
    if (type == "last_pulse") {
        return getLastPulseTimeString();
    }

    return getJSON();
}

std::string PulseCounter::getJSON(std::string lineEnd)
{
    std::string timeUnit = "min";
#ifdef ENABLE_MQTT
    if (getTimeUnit() == "h") {
        timeUnit = "h";
    }
#endif // ENABLE_MQTT

    std::string json = "{" + lineEnd;

    if (!_enabled) {
        json += "  \"enabled\": false" + lineEnd;
        json += "}";
        return json;
    }

    char pulsesPerUnit[32];
    snprintf(pulsesPerUnit, sizeof(pulsesPerUnit), "%g", _pulsesPerUnit);

    json += "  \"enabled\": true," + lineEnd;
    json += "  \"count\": " + std::to_string(getCount()) + "," + lineEnd;
    json += "  \"value\": \"" + getValueString() + "\"," + lineEnd;
    json += "  \"rate\": \"" + getRateString() + "\"," + lineEnd;
    json += "  \"rate_per_hour\": \"" + formatDouble(getRatePerHour(), 4) + "\"," + lineEnd;
    json += "  \"rate_per_time_unit\": \"" + getRatePerTimeUnitString() + "\"," + lineEnd;
    json += "  \"time_unit\": \"" + timeUnit + "\"," + lineEnd;
    json += "  \"interval\": \"" + getLastIntervalString() + "\"," + lineEnd;
    json += "  \"last_pulse\": \"" + getLastPulseTimeString() + "\"," + lineEnd;
    json += "  \"pulses_per_unit\": \"" + std::string(pulsesPerUnit) + "\"," + lineEnd;
    json += "  \"gpio\": " + std::to_string((int) _gpio) + "," + lineEnd;
    json += "  \"align_sequence\": \"" + _alignSequence + "\"" + lineEnd;
    json += "}";

    return json;
}

/* ------------------------------------------------------------------------------------------------
 * Setting / aligning the value
 * ------------------------------------------------------------------------------------------------ */

bool PulseCounter::setValue(double value)
{
    if (!_enabled) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "setValue: pulse counter is disabled");
        return false;
    }

    if (!(value >= 0) || isinf(value)) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "setValue: invalid value");
        return false;
    }

    lock();
    std::string oldValue = formatDouble(PulseCounterMath::valueFromCount(currentCount(), _offset, _pulsesPerUnit), _decimals);
    _offset = PulseCounterMath::offsetForValue(value, currentCount(), _pulsesPerUnit);
    _dirty = true;
    unlock();

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Pulse counter value set from " + oldValue + " to " + getValueString());

    saveState();
    publish(true);

    return true;
}

bool PulseCounter::alignWithReading(double reading, int readingDecimals, time_t readingTime)
{
    if (!_enabled) {
        return false;
    }

    lock();
    uint32_t countAtReading = countAt(readingTime);
    double pulseValue = PulseCounterMath::valueFromCount(countAtReading, _offset, _pulsesPerUnit);
    bool needed = PulseCounterMath::needsAlignment(pulseValue, reading, readingDecimals);

    if (needed) {
        _offset = PulseCounterMath::offsetForValue(reading, countAtReading, _pulsesPerUnit);
        _dirty = true;
    }
    unlock();

    std::string pulseValueStr = formatDouble(pulseValue, _decimals);
    std::string readingStr = formatDouble(reading, readingDecimals);

    if (!needed) {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Pulse counter value " + pulseValueStr + " matches reading " + readingStr +
                                                " of sequence '" + _alignSequence + "', no alignment needed");
        return false;
    }

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Pulse counter value aligned from " + pulseValueStr + " to reading " + readingStr +
                                           " of sequence '" + _alignSequence + "' (now " + getValueString() + ")");

    saveState();
    publish(true);

    return true;
}

/* ------------------------------------------------------------------------------------------------
 * MQTT
 * ------------------------------------------------------------------------------------------------ */

void PulseCounter::publish(bool full)
{
#ifdef ENABLE_MQTT
    if (!_enabled || !getMQTTisConnected()) {
        return;
    }

    std::string ratePerTimeUnit = getRatePerTimeUnitString();
    int64_t now = esp_timer_get_time();

    lock();
    if (!full) {
        /* Without a new pulse only the decaying rate changes: publish it at most every few seconds and only if it changed */
        if (((now - _lastPublishUs) < ((int64_t) PULSECOUNTER_PUBLISH_INTERVAL_S * 1000000)) || (ratePerTimeUnit == _lastPublishedRate)) {
            unlock();
            return;
        }
    }
    _lastPublishUs = now;
    _lastPublishedRate = ratePerTimeUnit;
    unlock();

    std::string topic = mqttServer_getMainTopic() + "/pulse/";
    bool retain = mqttServer_getRetainFlag();
    int qos = 1;

    if (full) {
        MQTTPublish(topic + "count", std::to_string(getCount()), qos, retain);
        MQTTPublish(topic + "value", getValueString(), qos, retain);

        std::string interval = getLastIntervalString();
        if (interval.length() > 0) {
            MQTTPublish(topic + "interval", interval, qos, retain);
        }

        std::string lastPulse = getLastPulseTimeString();
        if (lastPulse.length() > 0) {
            MQTTPublish(topic + "last_pulse", lastPulse, qos, retain);
        }
    }

    MQTTPublish(topic + "rate", getRateString(), qos, retain);
    MQTTPublish(topic + "rate_per_time_unit", ratePerTimeUnit, qos, retain);
    MQTTPublish(topic + "json", getJSON("\n"), qos, retain);
#endif // ENABLE_MQTT
}

#ifdef ENABLE_MQTT
void PulseCounter::handleMQTTconnect()
{
    if (!_enabled) {
        return;
    }

    std::string topic = mqttServer_getMainTopic() + "/pulse/set_value";
    std::function<bool(std::string, char *, int)> handler = std::bind(&PulseCounter::handleMQTTSetValue, this,
                                                                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    MQTTregisterSubscribeFunction(topic, handler);

    publish(true);
}

bool PulseCounter::handleMQTTSetValue(std::string topic, char *data, int data_len)
{
    std::string payload = trim(std::string(data, data_len));
    replaceAll(payload, ",", ".");

    char *end = NULL;
    double value = strtod(payload.c_str(), &end);

    if (payload.empty() || (end == payload.c_str()) || (*end != '\0') || !(value >= 0) || isinf(value)) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "MQTT set_value: '" + payload + "' is not a valid value");
        return false;
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "MQTT set_value: " + payload);

    return setValue(value);
}

bool PulseCounter::publishHomeAssistantDiscovery(int qos)
{
    if (!_enabled) {
        return true;
    }

    std::string meterType = mqttServer_getMeterType();
    std::string valueUnit = mqttServer_getValueUnit();
    std::string rateUnit = mqttServer_getRateUnit();
    std::string timeUnit = getTimeUnit();

    if (timeUnit.empty()) {
        timeUnit = "min";
    }

    std::string valueDeviceClass = meterType;
    if (meterType == "temperature") {
        valueDeviceClass = "";
    }

    std::string rateDeviceClass = "";
    if (meterType == "energy") {
        rateDeviceClass = "power";
    }
    else if ((meterType == "water") || (meterType == "gas")) {
        rateDeviceClass = "volume_flow_rate";
    }

    /* The value can decrease slightly when it gets aligned with the camera reading */
    std::string valueStateClass = _alignSequence.empty() ? "total_increasing" : "total";

    bool success = true;
    HomeAssistantEntity entity;

    entity = HomeAssistantEntity();
    entity.objectId = "pulse_value";
    entity.name = "Pulse Counter Value";
    entity.stateTopic = "pulse/value";
    entity.icon = "counter";
    entity.unit = valueUnit;
    entity.deviceClass = valueDeviceClass;
    entity.stateClass = valueStateClass;
    success &= mqttServer_publishHomeAssistantDiscovery(entity, qos);

    entity = HomeAssistantEntity();
    entity.objectId = "pulse_rate_per_time_unit";
    entity.name = "Pulse Counter Rate (" + (rateUnit.empty() ? ("Unit/" + timeUnit) : rateUnit) + ")";
    entity.stateTopic = "pulse/rate_per_time_unit";
    entity.icon = "pulse";
    entity.unit = rateUnit;
    entity.deviceClass = rateDeviceClass;
    entity.stateClass = "measurement";
    success &= mqttServer_publishHomeAssistantDiscovery(entity, qos);

    entity = HomeAssistantEntity();
    entity.objectId = "pulse_count";
    entity.name = "Pulse Counter Pulses";
    entity.stateTopic = "pulse/count";
    entity.icon = "counter";
    entity.stateClass = "total_increasing";
    entity.entityCategory = "diagnostic";
    success &= mqttServer_publishHomeAssistantDiscovery(entity, qos);

    entity = HomeAssistantEntity();
    entity.objectId = "pulse_interval";
    entity.name = "Pulse Counter Interval";
    entity.stateTopic = "pulse/interval";
    entity.icon = "timer-outline";
    entity.unit = "s";
    entity.deviceClass = "duration";
    entity.stateClass = "measurement";
    entity.entityCategory = "diagnostic";
    success &= mqttServer_publishHomeAssistantDiscovery(entity, qos);

    entity = HomeAssistantEntity();
    entity.objectId = "pulse_last_pulse";
    entity.name = "Pulse Counter Last Pulse";
    entity.stateTopic = "pulse/last_pulse";
    entity.icon = "clock-time-eight-outline";
    entity.deviceClass = "timestamp";
    entity.entityCategory = "diagnostic";
    success &= mqttServer_publishHomeAssistantDiscovery(entity, qos);

    entity = HomeAssistantEntity();
    entity.component = "number";
    entity.objectId = "pulse_set_value";
    entity.name = "Pulse Counter Set Value";
    entity.stateTopic = "pulse/value";
    entity.commandTopic = "pulse/set_value";
    entity.icon = "counter";
    entity.unit = valueUnit;
    entity.entityCategory = "config";
    entity.extraJson = "\"min\": 0, \"max\": 1000000000, \"step\": 0.001, \"mode\": \"box\"";
    success &= mqttServer_publishHomeAssistantDiscovery(entity, qos);

    return success;
}
#endif // ENABLE_MQTT

/* ------------------------------------------------------------------------------------------------
 * REST API
 * ------------------------------------------------------------------------------------------------ */

esp_err_t PulseCounter::handleHttpRequest(httpd_req_t *req)
{
    ESP_LOGD(TAG, "handleHttpRequest: %s", req->uri);

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char query[200];
    char parameter[40];
    bool haveQuery = (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK);

    if (haveQuery && (httpd_query_key_value(query, "set", parameter, sizeof(parameter)) == ESP_OK)) {
        if (!_enabled) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "E1: Pulse counter is disabled");
            return ESP_FAIL;
        }

        std::string valueStr = trim(UrlDecode(std::string(parameter)));
        replaceAll(valueStr, ",", ".");

        char *end = NULL;
        double value = strtod(valueStr.c_str(), &end);

        if (valueStr.empty() || (end == valueStr.c_str()) || (*end != '\0') || !(value >= 0) || isinf(value)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "E2: Invalid value, use e.g. /pulsecounter?set=1234.5");
            return ESP_FAIL;
        }

        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "REST API set value: " + valueStr);

        if (!setValue(value)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "E3: Value rejected, please check the device log");
            return ESP_FAIL;
        }

        std::string response = "Pulse counter value set to " + getValueString();
        httpd_resp_send(req, response.c_str(), response.length());
        return ESP_OK;
    }

    if (haveQuery && (httpd_query_key_value(query, "type", parameter, sizeof(parameter)) == ESP_OK)) {
        std::string type = toLower(std::string(parameter));

        if (type == "json") {
            httpd_resp_set_type(req, "application/json");
            std::string json = getJSON();
            httpd_resp_send(req, json.c_str(), json.length());
            return ESP_OK;
        }

        if ((type != "value") && (type != "count") && (type != "rate") && (type != "rate_per_hour") &&
            (type != "rate_per_time_unit") && (type != "interval") && (type != "last_pulse")) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "E4: Unknown type, use value, count, rate, rate_per_hour, rate_per_time_unit, interval, last_pulse or json");
            return ESP_FAIL;
        }

        if (!_enabled) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "E1: Pulse counter is disabled");
            return ESP_FAIL;
        }

        std::string response = getReadout(type);
        httpd_resp_send(req, response.c_str(), response.length());
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    std::string json = getJSON();
    httpd_resp_send(req, json.c_str(), json.length());

    return ESP_OK;
}

/* ------------------------------------------------------------------------------------------------
 * Global instance
 * ------------------------------------------------------------------------------------------------ */

void pulsecounter_create(httpd_handle_t server)
{
    if (pulseCounter == NULL) {
        pulseCounter = new PulseCounter(CONFIG_FILE, server);
    }
}

void pulsecounter_init(PulseCounterPinReservedCheck isPinReserved)
{
    if (pulseCounter != NULL) {
        pulseCounter->init(isPinReserved);
    }
}

void pulsecounter_deinit()
{
    if (pulseCounter != NULL) {
        pulseCounter->deinit();
    }
}

void pulsecounter_destroy()
{
    if (pulseCounter != NULL) {
        pulsecounter_deinit();
        delete pulseCounter;
        pulseCounter = NULL;
    }
}

PulseCounter *pulsecounter_get()
{
    return pulseCounter;
}
