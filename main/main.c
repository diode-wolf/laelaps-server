/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "functions.h"

// Mutexes
SemaphoreHandle_t association_array_mutex;
SemaphoreHandle_t tcp_rx_array_mutex;


void app_main(void){
    const char* MAIN_TAG = "main.c";
    //Initialize NVS, netif, create event loop handler
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(MAIN_TAG, "NVS Done");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Init Semaphores
    ESP_LOGI(MAIN_TAG, "Free heap size: %lu", esp_get_free_heap_size());
    SemaphoreHandle_t server_ready = xSemaphoreCreateBinary();
    association_array_mutex = xSemaphoreCreateMutex();
    tcp_rx_array_mutex = xSemaphoreCreateMutex();

    // Create Task Handles
    TaskHandle_t Tcp_Server_Handle;
    TaskHandle_t Process_TCP_Handle;
    TaskHandle_t Process_USB_Handle;

    // Init UART0
    Init_UART0();

    // Start wifi
    wifi_init_softap();

    // Start TCP server
    assert(server_ready);
    xTaskCreate(tcp_server_task, "tcp_server", 3072, &server_ready, 5, &Tcp_Server_Handle);
    xSemaphoreTake(server_ready, portMAX_DELAY);
    vSemaphoreDelete(server_ready);

    // Start Data processing task
    xTaskCreate(Process_TCP_Rx_Data_Task, "process_tcp", 3072, NULL, 4, &Process_TCP_Handle);

    // Start USB RX task
    xTaskCreate(Process_USB_Rx_Data_Task, "process_usb", 3072, NULL, 3, &Process_USB_Handle);

    // Uncomment to print log of stack high water marks
    // while(1){
    //     vTaskDelay(pdMS_TO_TICKS(15000));
    //     ESP_LOGI(MAIN_TAG, "TCP_Server HWM: %d", uxTaskGetStackHighWaterMark(Tcp_Server_Handle));
    //     ESP_LOGI(MAIN_TAG, "TCP_Process HWM: %d", uxTaskGetStackHighWaterMark(Process_TCP_Handle));
    //     ESP_LOGI(MAIN_TAG, "USB_Process HWM: %d", uxTaskGetStackHighWaterMark(Process_USB_Handle));
    // }    
    return;
}
