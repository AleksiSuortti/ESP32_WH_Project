#include "Arduino.h" 
#include <stdio.h>
#include <SPI.h>
#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include <TFT_eSPI.h>

#include "../include/wifi_test.h"

#define TFT_GREY 0x5AEB
#define TAG "main"
#define ENCODER_CLICK 1
#define ENCODER_CW 2
#define ENCODER_C_CW 3
#define ENCODER_PIN_CW (gpio_num_t)GPIO_NUM_32
#define ENCODER_PIN_C_CW (gpio_num_t)GPIO_NUM_33
#define ENCODER_PIN_CLICK (gpio_num_t)GPIO_NUM_25

// Enter the Wi-Fi credentials here
const char*  WIFI_SSID = "suosuoverkko";
const char* WIFI_PASSWORD = "Tampesteri37!";

TFT_eSPI tft = TFT_eSPI();
volatile int encoder_state = 0;
portMUX_TYPE encoder_mux = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t encoder_event_queue;
SemaphoreHandle_t ui_ready;

enum Wifi_command {
    WIFI_CMD_ON,
    WIFI_CMD_OFF
};

QueueHandle_t wifi_command_queue;

struct Menu {

    int items;
    int active_item;
    char item_tags[3][21];
};

/*
IRAM_ATTR void encoder_isr_handler(void *arg) {

    static uint32_t last_interrupt_time = 0;
    uint32_t current_time = xTaskGetTickCountFromISR();

    if (current_time - last_interrupt_time < pdMS_TO_TICKS(2)) {
        return;
    }
    last_interrupt_time = current_time;

    static uint8_t last_state = 0;

    uint8_t a = gpio_get_level(ENCODER_PIN_CW);
    uint8_t b = gpio_get_level(ENCODER_PIN_C_CW);
    uint8_t c = gpio_get_level(ENCODER_PIN_CLICK);
    uint8_t state = (a << 1) | b;

        if(!c) {
        encoder_state =+ 1000;
    }

    portENTER_CRITICAL_ISR(&encoder_mux);

    switch (last_state) {
        case 0b00:
            if (state == 0b01) encoder_state++;
            else if (state == 0b10) encoder_state--;
            break;
        case 0b01:
            if (state == 0b11) encoder_state++;
            else if (state == 0b00) encoder_state--;
            break;
        case 0b11:
            if (state == 0b10) encoder_state++;
            else if (state == 0b01) encoder_state--;
            break;
        case 0b10:
            if (state == 0b00) encoder_state++;
            else if (state == 0b11) encoder_state--;
            break;
    }

    last_state = state;

    portEXIT_CRITICAL_ISR(&encoder_mux);
}
*/

IRAM_ATTR void encoder_isr_handler(void *arg) {
    static uint32_t last_interrupt_time = 0;
    uint32_t current_time = xTaskGetTickCountFromISR();
    
    // More robust debouncing
    if (current_time - last_interrupt_time < pdMS_TO_TICKS(5)) {
        return;
    }
    last_interrupt_time = current_time;

    // Use separate static variables for each instance
    static uint8_t last_encoder_state = 0;
    static int8_t encoder_position = 0;
    
    portENTER_CRITICAL_ISR(&encoder_mux);
    
    // Read current state of both pins
    uint8_t a = gpio_get_level(ENCODER_PIN_CW);
    uint8_t b = gpio_get_level(ENCODER_PIN_C_CW);
    uint8_t current_state = (a << 1) | b;
    
    /* Handle button click separately
    uint8_t click = gpio_get_level(ENCODER_PIN_CLICK);
    if (!click) {
        encoder_state =+ 1000;
        portEXIT_CRITICAL_ISR(&encoder_mux);
        return;
    }
    */
    
    // Improved quadrature decoding using state transition table
    // States: 00, 01, 11, 10 (Gray code sequence)
    static const int8_t state_transition_table[4][4] = {
        // current: 00, 01, 11, 10
        { 0, -1,  0,  1}, // last: 00
        { 1,  0, -1,  0}, // last: 01  
        { 0,  1,  0, -1}, // last: 11
        {-1,  0,  1,  0}  // last: 10
    };
    
    int8_t direction = state_transition_table[last_encoder_state][current_state];
    
    if (direction != 0) {
        encoder_state += direction;
    }
    
    last_encoder_state = current_state;
    portEXIT_CRITICAL_ISR(&encoder_mux);
}

void tft_setup() {

    char* tag = "TFT_setup";

    ESP_LOGI(tag, "Initializing TFT display");

    tft.init();
    tft.setRotation(3);
    tft.setTextColor(TFT_WHITE);
    tft.setTextFont(2);
    tft.setCursor(10,10);
    tft.fillScreen(TFT_BLUE);
    tft.fillRect(0, 140, 320, 40, TFT_DARKGREY);
    
}

IRAM_ATTR void encoder_button_click(void *arg) {
    uint8_t event  = ENCODER_CLICK;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (xSemaphoreTakeFromISR(ui_ready, &xHigherPriorityTaskWoken) == pdTRUE) {
        xQueueSendFromISR(encoder_event_queue, &event, &xHigherPriorityTaskWoken);
    }

    if(xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void draw_encoder_graph(int current_state, int &start_x, int &start_y) {

    if(current_state > 15) {
        current_state = 15;
    } else if(current_state < -15) {
        current_state = -15;
    }

    if(current_state == 0) {
        tft.drawLine(start_x, start_y, start_x + 3, start_y, TFT_WHITE);
    } else if(current_state >= 1000) {
        tft.drawLine(start_x, start_y, start_x + 3, start_y, TFT_RED);
    } else if(current_state > 0) {
        tft.fillRect(start_x, start_y - (current_state * 2), 3, current_state * 2, TFT_WHITE);
    } else {
        tft.fillRect(start_x, start_y, 3, -(current_state * 2), TFT_WHITE);
    }
    start_x += 4;
    if(start_x >= 310) {
        tft.fillRect(0, 130, 320, 60, TFT_DARKGREY);
        start_x = 10;
    }
}

void menu_update(Menu menu) {

    for(int i = 0; i < menu.items; i++) {

        int x = 20 + i * ((TFT_HEIGHT - 20) / menu.items);
        int w = (TFT_HEIGHT - 20)/menu.items - 20;
        int y = 20;
        
        uint16_t color;
        if(i == menu.active_item) {
            color = TFT_RED;
        } else {
            color = TFT_WHITE;
        }

        tft.drawRoundRect(x, y, w, 40, 5, color);
        tft.setCursor(x + 3, y + 3);
        tft.print(menu.item_tags[i]);
    }
};

void draw_ip() {

    tft.fillRect(20, 70, 200, 50, TFT_BLUE);

    if(!wifi_get_manual_disconnect()) {
        tft.setCursor(20, 70);
        char ssid_str[40];
        char ip_str[20];

        snprintf(ssid_str, 20, "SSID: %s", wifi_get_ssid());
        tft.print(ssid_str);

        tft.setCursor(20, 85);
        snprintf(ip_str, 40, "IPv4: %s", wifi_get_ip());
        tft.print(ip_str);
    } else {
        tft.setCursor(20, 70);

        tft.print("SSID: Not connected");

        tft.setCursor(20, 85);
        tft.print("IPv4: ---");
    }
}

void ui_task(void *pvParameters) {
    
    int last_encoder_state = 0;
    uint32_t last_check_time = millis();
    int start_x = 10;
    int start_y = 160;

    Menu menu = {
        .items = 3,
        .active_item = 0,
        .item_tags = {"WiFi\n   on / off", "ITEM 1", "ITEM 2"}
    };

    static bool wifi_enabled = true;

    while(1) {

        uint32_t now = millis();
        int current_state;
        int current_active_item = menu.active_item;

        if(now - last_check_time >= 33) {

            last_check_time = now;

            portENTER_CRITICAL(&encoder_mux);
            current_state = encoder_state;
            encoder_state = 0;
            portEXIT_CRITICAL(&encoder_mux);
            
            if(current_state >= 1000) {
                ESP_LOGI("UI","Encoder CLICK: %d", current_state);
                if(menu.active_item == 0) {
                    wifi_enabled = !wifi_enabled;
                    Wifi_command cmd = wifi_enabled ? WIFI_CMD_ON : WIFI_CMD_OFF;
                    xQueueSend(wifi_command_queue, &cmd, portMAX_DELAY);
                }
                
            } else if(current_state > 0) {
                ESP_LOGI("UI","Encoder CW: %d", current_state);
                if(menu.active_item == menu.items - 1) {
                    menu.active_item = 0;
                } else {
                    menu.active_item++;
                }

            } else if(current_state < 0) {
                ESP_LOGI("UI","Encoder C_CW: %d", current_state);
                if(menu.active_item == 0) {
                    menu.active_item = menu.items - 1;
                } else {
                    menu.active_item--;
                }
            }
            draw_encoder_graph(current_state, start_x, start_y);
            draw_ip();
        }

        if(current_active_item != menu.active_item) {
            menu_update(menu);
        }


        vTaskDelay(1);
    }
}

extern "C" void system_wifi_start() {

    char* tag = "system_wifi_start";

    ESP_LOGI(tag, "Starting WiFi on system");
    ESP_ERROR_CHECK(wifi_init());

    esp_err_t ret = wifi_connect(WIFI_SSID, WIFI_PASSWORD);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi network");
    }

    wifi_ap_record_t ap_info;
    ret = esp_wifi_sta_get_ap_info(&ap_info);
    if(ret == ESP_ERR_WIFI_CONN) {
        ESP_LOGE(TAG, "WiFi station interface not initialized");
    }

    else if(ret == ESP_ERR_WIFI_NOT_CONNECT) {
        ESP_LOGE(TAG, "WiFi station is not connected");
    } else {
        ESP_LOGI(TAG, "--- Access Point Information ---");
        ESP_LOG_BUFFER_HEX("MAC Address", ap_info.bssid, sizeof(ap_info.bssid));
        ESP_LOG_BUFFER_CHAR("SSID", ap_info.ssid, sizeof(ap_info.ssid));
        ESP_LOGI(TAG, "Primary Channel: %d", ap_info.primary);
        ESP_LOGI(TAG, "RSSI: %d", ap_info.rssi);
    }
}

extern "C" void system_wifi_stop() {

    char* tag = "system_wifi_stop";

    ESP_ERROR_CHECK(wifi_disconnect());

    ESP_ERROR_CHECK(test_wifi_deinit());

    ESP_LOGI(tag, "WiFi terminated on the system");
}

void wifi_control_task(void *pvParameters) {

    Wifi_command cmd;

    while(1) {
        if(xQueueReceive(wifi_command_queue, &cmd, portMAX_DELAY)) {
            if(cmd == WIFI_CMD_ON) {
                wifi_set_manual_disconnect(false);
                esp_wifi_connect();
            } else if(cmd == WIFI_CMD_OFF) {
                wifi_set_manual_disconnect(true);
                esp_wifi_disconnect();
            }
        }
    }
}

extern "C" void app_main()
{
    initArduino();
    tft_setup();
    system_wifi_start();

    encoder_event_queue = xQueueCreate(10, sizeof(uint8_t));
    ui_ready = xSemaphoreCreateBinary();

    xSemaphoreGive(ui_ready);

    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);

    gpio_config_t cfg = {};

    cfg.intr_type = GPIO_INTR_DISABLE;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pin_bit_mask = (1ULL << GPIO_NUM_32) | (1ULL << GPIO_NUM_33) | (1ULL << GPIO_NUM_25);
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;

    ESP_ERROR_CHECK(gpio_config(&cfg));

    gpio_isr_handler_add(GPIO_NUM_25, encoder_button_click, (void*)GPIO_NUM_25);
    gpio_isr_handler_add(GPIO_NUM_32, encoder_isr_handler, (void*)GPIO_NUM_32);
    gpio_isr_handler_add(GPIO_NUM_33, encoder_isr_handler, (void*)GPIO_NUM_33);

    gpio_set_intr_type(ENCODER_PIN_CW, GPIO_INTR_NEGEDGE);
    gpio_set_intr_type(ENCODER_PIN_C_CW, GPIO_INTR_DISABLE);
    gpio_set_intr_type(ENCODER_PIN_CLICK, GPIO_INTR_NEGEDGE);

    wifi_command_queue = xQueueCreate(1, sizeof(Wifi_command));
    xTaskCreate(wifi_control_task, "wifi_ctrl", 4096, NULL, 2, NULL);

    xTaskCreate(ui_task, "UI_task", 4096, NULL, 1, NULL);
}