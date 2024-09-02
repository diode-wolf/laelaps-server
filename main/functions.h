/*
This file holds the function prototypes for the entire project
functions.h should be included in all .c source files

Author:         James Sorber
Contact:        jrsorber@ncsu.edu
Created:        8/31/2024
Modified:       -
Last Built With ESP-IDF v5.2.2
*/

#ifndef FUNCTIONS_H
#define FUNCTIONS_H

// SOFTAP.C
void wifi_init_softap(void);

// TCP_SERVER.C
// More functions then these, but declared static
void tcp_server_task(void *pvParameters);

// PROCESS_DATA.C
void Update_Tube_ID(uint8_t socket_idx, uint8_t status);
void Write_Rx_Storage(uint8_t socket_idx, char* data, uint16_t len);
void Process_Rx_Data_Task(void *pvParameters);

#endif