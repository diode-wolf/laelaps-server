/*
This file holds the function prototypes for the entire project
functions.h should be included in all .c source files
The files named in the comments contain more functions than the prototypes 
listed here because functions declared static are not included in this file

Author:         James Sorber
Contact:        jrsorber@ncsu.edu
Created:        8/31/2024
Modified:       -
Last Built With ESP-IDF v5.2.2
*/

#ifndef FUNCTIONS_H
#define FUNCTIONS_H

// Forward define this data type so it can be recognized in this file
enum socket_tube{NoSocket, Tube1, Tube2, Tube3, Tube4, Tube5, Unknown, Requested};
typedef enum socket_tube tube_id_t;

// SOFTAP.C
void wifi_init_softap(void);

// TCP_SERVER.C
void tcp_server_task(void *pvParameters);
void TCP_Send_Index(const char *TAG, uint8_t socket_idx, const char *data, const size_t len);
void Close_Socket(uint8_t socket_idx);

// PROCESS_TCP_DATA.C
void Update_Tube_Association(uint8_t socket_idx, tube_id_t new_association);
int Lookup_Tube_Association(tube_id_t laelaps_id);
tube_id_t Lookup_Socket_Association(uint8_t socket_idx);
void Write_Rx_Storage(uint8_t socket_idx, char* data, uint16_t len);
void Clear_Array(char* array, uint16_t len);
void TCP_Send_Laelaps(tube_id_t laelaps, const char * data, uint16_t len);
void Process_TCP_Rx_Data_Task(void *pvParameters);

// PROCESS_USB_DATA.C
void Process_USB_Rx_Data_Task(void *args);
void Init_UART0(void);

#endif