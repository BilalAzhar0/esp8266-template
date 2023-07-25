#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "../../data.h"

#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_RETRY_BIT     BIT1
#define WIFI_FAIL_BIT      BIT2

static EventGroupHandle_t s_wifi_event_group;
static TaskHandle_t wifi_retry_task_handle;
bool wifi_task_flag;
static const char *wifi_TAG = "wifi station";
static int s_retry_num = 0;

//********************************** WIFI RETRY TASK **********************************//
void retry_wifi_task()
{   
    xEventGroupSetBits(s_wifi_event_group,WIFI_RETRY_BIT);
    while (1) {
        ESP_LOGI(wifi_TAG, "Attempting to reconnect...");
        xEventGroupClearBits(s_wifi_event_group,WIFI_FAIL_BIT);
        esp_wifi_connect();
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        if(bits & WIFI_CONNECTED_BIT){
            ESP_LOGI(wifi_TAG, "connected to ap SSID:%s password:%s", ESP_WIFI_SSID, ESP_WIFI_PASS);
            xEventGroupClearBits(s_wifi_event_group,WIFI_RETRY_BIT);
            vTaskDelete(NULL);
        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGI(wifi_TAG, "Failed to connect to SSID:%s, password:%s", ESP_WIFI_SSID, ESP_WIFI_PASS);
        }
    }     
}
//********************************** WIFI EVENT HANDLER **********************************//
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED){
        EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
        if(bits & WIFI_RETRY_BIT){
            xEventGroupSetBits(s_wifi_event_group,WIFI_FAIL_BIT);
        }
        else{
            xEventGroupClearBits(s_wifi_event_group,WIFI_CONNECTED_BIT);
            xTaskCreate(&retry_wifi_task,"Wifi retry task", 4096, NULL, 24, &wifi_retry_task_handle);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(wifi_TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

    }
}
//********************************** WIFI CONFIGURATION METHOD **********************************//
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&event_handler,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,&event_handler,NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASS,
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(wifi_TAG, "wifi_init_sta finished.");
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_LOGI(wifi_TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
}
