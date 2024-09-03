/*
This file holds the code for running the tcp server used to connect to the
Access point esp32 on the drop module

Author:         James Sorber
Contact:        jrsorber@ncsu.edu
Created:        9/01/2024
Modified:       -
Last Built With ESP-IDF v5.2.2
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "errno.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "functions.h"
#include "tcp_server.h"
#include "process_tcp_data.h"

typedef enum socket_tube{NoSocket, Tube1, Tube2, Tube3, Tube4, Tube5, Unknown, Requested} tube_id_t;

// Static global variables
static tube_id_t tube_socket_number[LWIP_MAX_SOCKETS];
static uint8_t rx_data_row_idx = 0;
static char rx_data_storage[RX_DATA_ROWS][RX_DATA_COLUMNS];
static uint8_t rx_data_column_idx = 0;

/* 
Semaphores for thread safety
The arrays tube_socket_number and rx_data_storage can be accessed from multiple threads
Aquire respective semaphore before use
*/
extern SemaphoreHandle_t rx_data_storage_access;

/*
Update_Tube_ID
This function takes the index of a newly connected socket and sets the corresponding
tube number to either unknown or nosocket.
The function takes the index of the socket in the array of socket handles (in tcp_server.c),
it also takes the status of that socket (either newly connected, or disconnected)
Function returns void
*/
void Update_Tube_ID(uint8_t socket_idx, uint8_t status){
    switch(status){
        case SOCKET_CONNECT:
            tube_socket_number[socket_idx] = Unknown;
        break;
        case SOCKET_DISCONNECT:
            tube_socket_number[socket_idx] = NoSocket;
        break;
        default:
        break;
    }
}

/*
TCP_Send_Laelaps
This function sends a string to a specific Laelaps module
The function takes the destination Laelaps module (1 - 5), a pointer to the message, and the length of the message
The function returns void
*/
void TCP_Send_Laelaps(uint8_t laelaps, const char * data, uint16_t len){
    const char *TAG = "TCP_Send_Laelaps";
    uint8_t i;
    for(i = 0; i < LWIP_MAX_SOCKETS; i++){
        if(tube_socket_number[i] == laelaps){
            TCP_Send_Index(TAG, i, data, len);
            return;
        }
    }
    if(i >= LWIP_MAX_SOCKETS){
        ESP_LOGW(TAG, "Laelaps %d not found", laelaps);
    }
}

/*
Clear_Array
This function takes a pointer to an array and a length of the array
It sets all array elements to zero (null character)
Returns void
*/
 void Clear_Array(char* array, uint16_t len){
    for(uint16_t i = 0; i < len; i++){
        array[i] = 0;
    }
}


/*
Write_Rx_Storage
This function takes the index of the socket data received from,
a pointer to the start of the rx data, and the length of the data
The function places the data in a row of the rx_data_storage array
It moves to the next row of the array when a newline character is received
Returns void
*/
void Write_Rx_Storage(uint8_t socket_idx, char* data, uint16_t len){
     for(uint16_t i = 0; i < len; i++){
        // If this is the start of a new row, add the socket identifier to the first element of the row
        if(rx_data_column_idx == 0){
            rx_data_storage[rx_data_row_idx][rx_data_column_idx] = socket_idx + ASCII_OFFSET;
            rx_data_column_idx++;
        }

        // Place next character of data into storage array
        rx_data_storage[rx_data_row_idx][rx_data_column_idx] = data[i];

        // If newline received, go to the next line
        if(data[i] == '\n'){
            rx_data_column_idx = 0;
            rx_data_row_idx++;
            if(rx_data_row_idx >= RX_DATA_ROWS) rx_data_row_idx = 0;
            Clear_Array(rx_data_storage[rx_data_row_idx], RX_DATA_COLUMNS);
            continue;
        }

        // If run out of space on current row, reset row index and wrap to next row
        rx_data_column_idx++;
        if(rx_data_column_idx >= RX_DATA_COLUMNS - 1){      // stop 1 character early bc last must be a null
            rx_data_column_idx = 0;
            rx_data_row_idx++;
            if(rx_data_row_idx >= RX_DATA_ROWS) rx_data_row_idx = 0;
            Clear_Array(rx_data_storage[rx_data_row_idx], RX_DATA_COLUMNS);
        }
    }
}

/*
process_rx_data_task
This is a rtos thread that looks at the data received from the tcp server and processes it as needed
*/
void Process_TCP_Rx_Data_Task(void *pvParameters){
    const char* PROCESS_DATA_TAG = "Process Data";
    uint8_t rx_row_idx_read = 0;
    char* sub_str_ptr;
    uint8_t tube_socket_idx;

    while(1){
        // If there is new data in the rx data storage buffer, print it to the pc
        // Later added functionality to search for particular messages if needed
        if(rx_row_idx_read != rx_data_row_idx){
            // Add null to end of array for safety
            rx_data_storage[rx_row_idx_read][RX_DATA_COLUMNS - 1] = '\0';

            // Process data
            ESP_LOGI(PROCESS_DATA_TAG, "%s", rx_data_storage[rx_row_idx_read]);

            // If the line is an identification from the laelaps client, update tube_socket_number
            sub_str_ptr = strstr(rx_data_storage[rx_row_idx_read], "$id-ack-");
            if(sub_str_ptr != NULL){
                tube_socket_idx = rx_data_storage[rx_row_idx_read][0] - ASCII_OFFSET;

                // Double check that this is a valid index!
                if(tube_socket_idx < LWIP_MAX_SOCKETS){
                    tube_socket_number[tube_socket_idx] = sub_str_ptr[8] - ASCII_OFFSET; // 8 is the position of the identifier in the ack string "$id-ack-#" where # is the number of the laelaps module (not the socket the module is connected to)
                    ESP_LOGI(PROCESS_DATA_TAG, "Associated Laelaps %c with socket %d", sub_str_ptr[8], tube_socket_idx);
                }
                // If there was a bad read, need to request id again. Don't know which socket failed read so do them all
                // This is an error case that should not run
                else{
                    ESP_LOGI(PROCESS_DATA_TAG, "Bad ID Read");
                    for(uint8_t i = 0; i < LWIP_MAX_SOCKETS; i++){
                        if(tube_socket_number[i] == Requested){
                            tube_socket_number[i] = Unknown;
                        }
                    }
                }
            }

            // Increment read index
            rx_row_idx_read++;
            if(rx_row_idx_read >= RX_DATA_ROWS) rx_row_idx_read = 0;
        }

        // Scan for socket handles that need to be asscioated with a tube number
        for(uint8_t i = 0; i < LWIP_MAX_SOCKETS; i++){
            if(tube_socket_number[i] == Unknown){
                ESP_LOGI(PROCESS_DATA_TAG, "Sent ID request to client on socket %d", i);
                TCP_Send_Index(PROCESS_DATA_TAG, i, "$id-req\r\n", 9);
                tube_socket_number[i] = Requested;
            }
        }

        // Run this loop every 200 ms
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}



