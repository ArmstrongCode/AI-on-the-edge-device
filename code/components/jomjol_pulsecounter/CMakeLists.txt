FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.*)

idf_component_register(SRCS ${app_sources}
                    INCLUDE_DIRS "." "../../include"
                    REQUIRES driver esp_timer esp_http_server jomjol_logfile jomjol_configfile jomjol_helper jomjol_time_sntp jomjol_mqtt jomjol_wlan)
