#ifdef ENABLE_MQTT

#pragma once

#ifndef SERVERMQTT_H
#define SERVERMQTT_H

#include <functional>
#include "ClassFlowDefineTypes.h"

/**
 * Describes one Home Assistant entity which gets announced via MQTT discovery.
 * Other modules (e.g. the pulse counter) use it to announce their own topics.
 */
struct HomeAssistantEntity {
    std::string component = "sensor";  // sensor, binary_sensor, button, number, ...
    std::string objectId;              // Unique within the device, used for unique_id/object_id (e.g. "pulse_value")
    std::string name;                  // User friendly name
    std::string stateTopic;            // Relative to the main topic (e.g. "pulse/value"), empty if none
    std::string commandTopic;          // Relative to the main topic, empty if none
    std::string icon;                  // Material design icon name without the "mdi:" prefix
    std::string unit;                  // unit_of_measurement, empty if none
    std::string deviceClass;           // empty if none
    std::string stateClass;            // empty if none
    std::string entityCategory;        // "diagnostic", "config" or empty
    std::string extraJson;             // Additional raw JSON members (e.g. "\"min\": 0, \"max\": 100"), empty if none
};

void SetHomeassistantDiscoveryEnabled(bool enabled);
void mqttServer_setParameter(std::vector<NumberPost*>* _NUMBERS, int interval, float roundInterval);
void mqttServer_setMeterType(std::string meterType, std::string valueUnit, std::string timeUnit,std::string rateUnit);
void setMqtt_Server_Retain(bool SetRetainFlag);
void mqttServer_setMainTopic( std::string maintopic);
void mqttServer_setDmoticzInTopic( std::string domoticzintopic);


std::string mqttServer_getMainTopic();
std::string mqttServer_getMeterType();
std::string mqttServer_getValueUnit();
std::string mqttServer_getRateUnit();
bool mqttServer_getRetainFlag();

/* Publishes the Home Assistant discovery topic of one entity */
bool mqttServer_publishHomeAssistantDiscovery(const HomeAssistantEntity &entity, int qos);

/* Modules which announce their own entities register a provider. It gets called whenever the discovery topics are (re-)sent. */
void mqttServer_registerDiscoveryProvider(std::string name, std::function<bool(int)> provider);
void mqttServer_unregisterDiscoveryProvider(std::string name);

void register_server_mqtt_uri(httpd_handle_t server);

bool publishSystemData(int qos);

std::string getTimeUnit(void);
void GotConnected(std::string maintopic, bool SetRetainFlag);
esp_err_t sendDiscovery_and_static_Topics(void);

std::string createNodeId(std::string &topic);

#endif //SERVERMQTT_H
#endif //ENABLE_MQTT