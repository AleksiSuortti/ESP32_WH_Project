#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

    esp_err_t wifi_init(void);

    esp_err_t wifi_connect(const char* wifi_ssid, const char* wifi_password);

    esp_err_t wifi_disconnect(void);

    esp_err_t test_wifi_deinit(void);
    
    const char* wifi_get_ip(void);

    const char* wifi_get_ssid(void);

    void wifi_set_manual_disconnect(bool value);

    bool wifi_get_manual_disconnect(void);

    #ifdef __cplusplus
}
#endif