/*
This file holds the code for reading data from the usb port
This file is also used to process the usb rx data and make decisions accordingly

Author:         James Sorber
Contact:        jrsorber@ncsu.edu
Created:        9/02/2024
Modified:       -
Last Built With ESP-IDF v5.2.2
*/

// Header Libraries
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "functions.h"
#include "process_usb_data.h"

// Static global variables
static char usb_rx_data_storage[UART0_RX_STORAGE_ROWS][UART0_RX_STORAGE_COLUMNS];
static uint8_t usb_rx_row_idx = 0;
static uint8_t usb_rx_column_idx = 0;

// Init UART0
// USB Port - Shared with ESP Logging functions
void Init_UART0(void){
    uart_config_t uart0_config_params = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    // Set parameters
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart0_config_params));
    // Install resources and drivers
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, UART0_RX_BUF_LEN, UART0_TX_BUF_LEN, 0, NULL, 0));
}

/*
Write_USB_Rx_Storage
This function writes data to the usb rx storage buffer
It takes a pointer to the data and the length of the dat
It returns void
*/
static void Write_USB_Rx_Storage(char* data, uint16_t len){
     for(uint16_t i = 0; i < len; i++){
        // Place next character of data into storage array
        usb_rx_data_storage[usb_rx_row_idx][usb_rx_column_idx] = data[i];

        // If newline received, go to the next line
        if(data[i] == '\n'){
            usb_rx_column_idx = 0;
            usb_rx_row_idx++;
            if(usb_rx_row_idx >= UART0_RX_STORAGE_ROWS) usb_rx_row_idx = 0;
            Clear_Array(usb_rx_data_storage[usb_rx_row_idx], UART0_RX_STORAGE_COLUMNS);
            continue;
        }

        // If run out of space on current row, reset row index and wrap to next row
        usb_rx_column_idx++;
        if(usb_rx_column_idx >= UART0_RX_STORAGE_COLUMNS - 1){      // stop 1 character early bc last must be a null
            usb_rx_column_idx = 0;
            usb_rx_row_idx++;
            if(usb_rx_row_idx >= UART0_RX_STORAGE_ROWS) usb_rx_row_idx = 0;
            Clear_Array(usb_rx_data_storage[usb_rx_row_idx], UART0_RX_STORAGE_COLUMNS);
        }
    }
}


/*
Process_USB_Rx_Data_Task
This is an RTOS thread that reads data from the USB port and writes it to an array
If new data in array, processes the data
*/
void Process_USB_Rx_Data_Task(void *args){
    const char * USB_RX_TAG = "Read USB";
    char* sub_str_ptr;
    char usb_rx_buffer[UART0_RX_BUF_LEN];
    uint8_t len_data_read = 0;
    uint8_t usb_rx_read_row_idx = 0;
    uint8_t addressed_laelaps = 0;

    while(1){
        // Try to read data from the usb port
        len_data_read = uart_read_bytes(UART_NUM_0, usb_rx_buffer, UART0_RX_BUF_LEN, pdMS_TO_TICKS(20));
        if(len_data_read){
            // If got data, write it to the storage array
            Write_USB_Rx_Storage(usb_rx_buffer, len_data_read);
        }

        // If a new row has been completed in the storage array, process the data
        if(usb_rx_read_row_idx != usb_rx_row_idx){
            // Add a null to the end of the row for safety!
            usb_rx_data_storage[usb_rx_read_row_idx][UART0_RX_STORAGE_COLUMNS - 1] = '\0';

            // Echo to pc
            ESP_LOGI(USB_RX_TAG, "%s", usb_rx_data_storage[usb_rx_read_row_idx]);

            // Do stuff with the data
            // Command to forward data to laelaps is of form $fwd-laelaps-#:xxxxxxxxxx
            // Where # is the number of the laelaps to send the data to and xxxx repersents the data to send
            sub_str_ptr = strstr(usb_rx_data_storage[usb_rx_read_row_idx], "$fwd-laelaps-");
            if(sub_str_ptr != NULL){
                addressed_laelaps = sub_str_ptr[13] - 0x30;
                if(addressed_laelaps < 5){
                    TCP_Send_Laelaps(addressed_laelaps, &sub_str_ptr[15], strlen(&sub_str_ptr[15]));
                }
                else{
                    ESP_LOGI(USB_RX_TAG, "Invalid laelaps address %d", addressed_laelaps);
                }
            }

            // Increment index of last row read
            usb_rx_read_row_idx++;
            if(usb_rx_read_row_idx >= UART0_RX_STORAGE_ROWS) usb_rx_read_row_idx = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

