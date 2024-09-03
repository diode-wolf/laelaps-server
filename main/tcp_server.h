/*
This file holds the macro definitions for tcp.c

Author:         James Sorber
Contact:        jrsorber@ncsu.edu
Created:        9/01/2024
Modified:       -
Last Built With ESP-IDF v5.2.2
*/

#ifndef TCP_H
#define TCP_H

#define PORT                        7983
#define KEEPALIVE_IDLE              5
#define KEEPALIVE_INTERVAL          5
#define KEEPALIVE_COUNT             5
#define TRUE                        1

#define LWIP_MAX_SOCKETS            6
#define MAX_SOCK_REQUEST_BACKLOG    3
#define TCP_SERVER_BIND_ADDRESS     "192.168.4.1"
#define TCP_SERVER_BIND_PORT        "7983"

#define SOCKET_CONNECT              1
#define SOCKET_DISCONNECT           2


#endif