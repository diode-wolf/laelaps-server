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

extern SemaphoreHandle_t association_array_mutex;
extern SemaphoreHandle_t tcp_rx_array_mutex;

// Static global variables
static tube_id_t laelaps_association[MAX_LAELAPS];      // -1 because one socket will be used for listening for connections
static uint8_t rx_data_row_idx = 0;
static char rx_data_storage[RX_DATA_ROWS][RX_DATA_COLUMNS];
static uint8_t rx_data_column_idx = 0;


/*
Update_Tube_ID
This function updates the value of the global resource laelaps association array
The function takes the index of the socket to associate, and the new association
*/
void Update_Tube_Association(uint8_t socket_idx, tube_id_t new_association){
    xSemaphoreTake(association_array_mutex, portMAX_DELAY);
    laelaps_association[socket_idx] = new_association;
    xSemaphoreGive(association_array_mutex);
    return;
}

tube_id_t Lookup_Socket_Association(uint8_t socket_idx){
    xSemaphoreTake(association_array_mutex, portMAX_DELAY);
    tube_id_t return_val;
    return_val = laelaps_association[socket_idx];
    xSemaphoreGive(association_array_mutex);
    return return_val;
}

/*
int Lookup_Tube_Association
This function is used to find which socket a particular laelaps is connected to
The function takes the number of the desired laelaps and returns the index of the socket
or -1 if the specified laelaps is not connected
*/
int Lookup_Tube_Association(tube_id_t laelaps_id){
    xSemaphoreTake(association_array_mutex, portMAX_DELAY);
    int return_val = -1;
    for(uint8_t i = 0; i < MAX_LAELAPS; i++){
        if(laelaps_association[i] == laelaps_id){
            return_val = i;
            break;
        }
    }
    xSemaphoreGive(association_array_mutex);
    return return_val;
}




/*
TCP_Send_Laelaps
This function sends a string to a specific Laelaps module
The function takes the destination Laelaps module (1 - 5), a pointer to the message, and the length of the message
The function returns void
*/
void TCP_Send_Laelaps(tube_id_t laelaps, const char * data, uint16_t len){
    const char *TAG = "TCP_Send_Laelaps";
    int socket_address;
    
    // Find which socket the target laelaps is on
    socket_address = Lookup_Tube_Association(laelaps);
    if(socket_address < 0){
        ESP_LOGW(TAG, "Laelaps %d not found", laelaps);
    }
    else{
        TCP_Send_Index(TAG, socket_address, data, len);
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
    xSemaphoreTake(tcp_rx_array_mutex, portMAX_DELAY);
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
    xSemaphoreGive(tcp_rx_array_mutex);
    return;
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
        if(rx_row_idx_read != rx_data_row_idx){
            xSemaphoreTake(tcp_rx_array_mutex, portMAX_DELAY);
            // Add null to end of array for safety
            rx_data_storage[rx_row_idx_read][RX_DATA_COLUMNS - 1] = '\0';

            // Process data
            ESP_LOGI(PROCESS_DATA_TAG, "%s", rx_data_storage[rx_row_idx_read]);

            // If the line is an identification from the laelaps client, update tube_socket_number
            sub_str_ptr = strstr(rx_data_storage[rx_row_idx_read], "$id-ack-");
            if(sub_str_ptr != NULL){
                tube_socket_idx = rx_data_storage[rx_row_idx_read][0] - ASCII_OFFSET;

                // Double check that this is a valid index!
                if(tube_socket_idx < MAX_LAELAPS){
                    Update_Tube_Association(tube_socket_idx, (tube_id_t) (sub_str_ptr[8] - ASCII_OFFSET));
                    // 8 is the position of the identifier in the ack string "$id-ack-#" where # is the number of the laelaps module (not the socket the module is connected to)
                    ESP_LOGI(PROCESS_DATA_TAG, "Associated Laelaps %c with socket %d", sub_str_ptr[8], tube_socket_idx);
                }
                // If there was a bad read, need to request id again. Don't know which socket failed read so do them all
                // This is an error case that should not run so inefficient function calls don't really mater
                else{
                    ESP_LOGW(PROCESS_DATA_TAG, "Bad ID Read");
                    for(uint8_t i = 0; i < MAX_LAELAPS; i++){
                        if(Lookup_Socket_Association(i) == Requested){
                            Update_Tube_Association(i, Unknown);
                        }
                    }
                }
            }

            // Increment read index
            rx_row_idx_read++;
            if(rx_row_idx_read >= RX_DATA_ROWS) rx_row_idx_read = 0;
            xSemaphoreGive(tcp_rx_array_mutex);
        }

        // Scan for socket handles that need to be asscioated with a tube number
        for(uint8_t i = 0; i < MAX_LAELAPS; i++){
            if(Lookup_Socket_Association(i) == Unknown){
                ESP_LOGI(PROCESS_DATA_TAG, "Sent ID request to client on socket %d", i);
                TCP_Send_Index(PROCESS_DATA_TAG, i, "$id-req\r\n", 9);
                Update_Tube_Association(i, Requested);
            }
        }

        // Run this loop every 200 ms
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}



