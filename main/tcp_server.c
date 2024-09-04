/*
This file holds the code for running the tcp server used to connect to the
Access point esp32 on the drop module

Author:         James Sorber
Contact:        jrsorber@ncsu.edu
Created:        8/31/2024
Modified:       -
Last Built With ESP-IDF v5.2.2
*/

/*
Need to improve error handeling. Should reset and try again if possible
Currently, just shuts down entire tcp

Init functions - idk about these. Just shut down and turn on error led
Recieve error - ignore, log on sd if I ever implement that
Send error - idk. Shut down socket maybe? it would then be on the client to request the connection again
Socket accept error - close socket. Clients should disconnect from socket and reconnect
if heartbeat not echoed back from server within ?# seconds
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys/socket.h"
#include "netdb.h"
#include "errno.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "functions.h"
#include "tcp_server.h"
#include "process_tcp_data.h"

// Static global variables
static int sock[MAX_LAELAPS];      // An array of socket handles used to keep track of connected clients



/**
 * @brief Indicates that the file descriptor represents an invalid (uninitialized or closed) socket
 *
 * Used in the TCP server structure `sock[]` which holds list of active clients we serve.
 */
#define INVALID_SOCK (-1)

/**
 * @brief Time in ms to yield to all tasks when a non-blocking socket would block
 *
 * Non-blocking socket operations are typically executed in a separate task validating
 * the socket status. Whenever the socket returns `EAGAIN` (idle status, i.e. would block)
 * we have to yield to all tasks to prevent lower priority tasks from starving.
 */
#define YIELD_TO_ALL_MS 50

/**
 * @brief Utility to log socket errors
 *
 * @param[in] tag Logging tag
 * @param[in] sock Socket number
 * @param[in] err Socket errno
 * @param[in] message Message to print
 */
static void log_socket_error(const char *tag, const int sock, const int err, const char *message)
{
    ESP_LOGE(tag, "[sock=%d]: %s\n"
                  "error=%d: %s", sock, message, err, strerror(err));
}

/**
 * @brief Tries to receive data from specified sockets in a non-blocking way,
 *        i.e. returns immediately if no data.
 *
 * @param[in] tag Logging tag
 * @param[in] sock Socket for reception
 * @param[out] data Data pointer to write the received data
 * @param[in] max_len Maximum size of the allocated space for receiving data
 * @return
 *          >0 : Size of received data
 *          =0 : No data available
 *          -1 : Error occurred during socket read operation
 *          -2 : Socket is not connected, to distinguish between an actual socket error and active disconnection
 */
static int try_receive(const char *tag, const int sock, char * data, size_t max_len)
{
    int len = recv(sock, data, max_len, 0);
    if (len < 0) {
        if (errno == EINPROGRESS || errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;   // Not an error
        }
        if (errno == ENOTCONN) {
            ESP_LOGW(tag, "[sock=%d]: Connection closed", sock);
            return -2;  // Socket has been disconnected
        }
        log_socket_error(tag, sock, errno, "Error occurred during receiving");
        return -1;
    }

    return len;
}

/**
 * @brief Sends the specified data to the socket. This function blocks until all bytes got sent.
 *
 * @param[in] tag Logging tag
 * @param[in] sock Socket to write data
 * @param[in] data Data to be written
 * @param[in] len Length of the data
 * @return
 *          >0 : Size the written data
 *          -1 : Error occurred during socket write operation
 */
static int socket_send(const char *tag, const int sock, const char * data, const size_t len)
{
    int to_write = len;
    while (to_write > 0) {
        int written = send(sock, data + (len - to_write), to_write, 0);
        if (written < 0 && errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) {
            log_socket_error(tag, sock, errno, "Error occurred during sending");
            return -1;
        }
        to_write -= written;
    }
    return len;
}

/*
TCP_Send_Index
This function sends a message to the client on a specified socket
*/
void TCP_Send_Index(const char *TAG, uint8_t socket_idx, const char *data, const size_t len){
    int bytes_written = socket_send(TAG, sock[socket_idx], data, len);
    if (bytes_written < 0) {
        // Error occurred on write to this socket -> close it and mark invalid
        ESP_LOGI(TAG, "[sock=%d]: socket_send() returned %d -> closing the socket", sock[socket_idx], bytes_written);
        Close_Socket(socket_idx);
    } 
    else {
        // Successfully echoed to this socket
        ESP_LOGI(TAG, "[sock=%d]: Written %.*s", sock[socket_idx], len, data);
    }
}


/**
 * @brief Returns the string representation of client's address (accepted on this server)
 */
static inline char* get_clients_address(struct sockaddr_storage *source_addr)
{
    static char address_str[128];
    char *res = NULL;
    // Convert ip address to string
    if (source_addr->ss_family == PF_INET) {
        res = inet_ntoa_r(((struct sockaddr_in *)source_addr)->sin_addr, address_str, sizeof(address_str) - 1);
    }
    if (!res) {
        address_str[0] = '\0'; // Returns empty string if conversion didn't succeed
    }
    return address_str;
}

/*
Close_Socket
This function closes a socket and handles asscioated housekeeping
The function takes the index in the socket storage array of the socket to close
The function returns void
*/
void Close_Socket(uint8_t socket_idx){
    close(sock[socket_idx]);
    sock[socket_idx] = INVALID_SOCK;
    Update_Tube_Association(socket_idx, NoSocket);
    return;
}

void tcp_server_task(void *pvParameters){
    static char rx_buffer[128];
    static const char *TAG = "tcp-server";
    SemaphoreHandle_t *server_ready_access = pvParameters;
    struct addrinfo hints = { .ai_socktype = SOCK_STREAM };
    struct addrinfo *address_info;
    int listen_sock = INVALID_SOCK;
    const size_t max_socks = MAX_LAELAPS;

    // Prepare a list of file descriptors to hold client's sockets, mark all of them as invalid, i.e. available
    for (int i=0; i<max_socks; ++i) {
        sock[i] = INVALID_SOCK;
    }

    // Translating the hostname or a string representation of an IP to address_info
    int res = getaddrinfo(TCP_SERVER_BIND_ADDRESS, TCP_SERVER_BIND_PORT, &hints, &address_info);
    if (res != 0 || address_info == NULL) {
        ESP_LOGE(TAG, "couldn't get hostname for `%s` "
                      "getaddrinfo() returns %d, addrinfo=%p", TCP_SERVER_BIND_ADDRESS, res, address_info);
        goto error;
    }

    // Creating a listener socket
    listen_sock = socket(address_info->ai_family, address_info->ai_socktype, address_info->ai_protocol);

    if (listen_sock < 0) {
        log_socket_error(TAG, listen_sock, errno, "Unable to create socket");
        goto error;
    }

    // Marking the socket as non-blocking
    int flags = fcntl(listen_sock, F_GETFL);
    if (fcntl(listen_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        log_socket_error(TAG, listen_sock, errno, "Unable to set socket non blocking");
        goto error;
    }

    // Binding socket to the given address
    int err = bind(listen_sock, address_info->ai_addr, address_info->ai_addrlen);
    if (err != 0) {
        log_socket_error(TAG, listen_sock, errno, "Socket unable to bind");
        goto error;
    }

    // Set queue (backlog) of pending connections to one (can be more)
    err = listen(listen_sock, MAX_SOCK_REQUEST_BACKLOG);
    if (err != 0) {
        log_socket_error(TAG, listen_sock, errno, "Error occurred during listen");
        goto error;
    }
    xSemaphoreGive(*server_ready_access);      // socket setup done, release semaphore

    // Main loop for accepting new connections and serving all connected clients
    while (1) {
        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);

        // Find a free socket
        int new_sock_index = 0;
        for (new_sock_index=0; new_sock_index < max_socks; ++new_sock_index) {
            if (sock[new_sock_index] == INVALID_SOCK) {
                break;
            }
        }

        // We accept a new connection only if we have a free socket
        if (new_sock_index < max_socks) {
            // Try to accept a new connections
            sock[new_sock_index] = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);

            if (sock[new_sock_index] < 0) {
                if (errno == EWOULDBLOCK) { // The listener socket did not accepts any connection
                                            // continue to serve open connections and try to accept again upon the next iteration
                } 
                else {
                    log_socket_error(TAG, listen_sock, errno, "Error when accepting connection");
                    Close_Socket(new_sock_index);
                }
            } 
            else{
                // We have a new client connected -> print it's address
                ESP_LOGI(TAG, "[sock=%d]: Connection accepted from IP:%s", sock[new_sock_index], get_clients_address(&source_addr));
                Update_Tube_Association(new_sock_index, Unknown);

                // ...and set the client's socket non-blocking
                flags = fcntl(sock[new_sock_index], F_GETFL);
                if (fcntl(sock[new_sock_index], F_SETFL, flags | O_NONBLOCK) == -1) {
                    log_socket_error(TAG, sock[new_sock_index], errno, "Unable to set socket non blocking");
                    Close_Socket(new_sock_index);
                }
            }
        }

        // Serve all the connected clients in this loop
        for (int i=0; i<max_socks; ++i) {
            if (sock[i] != INVALID_SOCK) {

                // This is an open socket -> try to serve it
                int len = try_receive(TAG, sock[i], rx_buffer, sizeof(rx_buffer));
                if (len < 0) {
                    // Error occurred within this client's socket -> close and mark invalid
                    ESP_LOGW(TAG, "[sock=%d]: try_receive() returned %d -> closing the socket", sock[i], len);
                    Close_Socket(i);
                } 
                else if (len > 0) {
                    // Received some data -> echo back
                    //ESP_LOGI(TAG, "[sock=%d]: Received %.*s", sock[i], len, rx_buffer);
                    Write_Rx_Storage(i, rx_buffer, len);        // Save data to array for processing

                    // Echo back to client on current socket
                    //TCP_Send_Index(TAG, i, rx_buffer, len);
                }

            } // one client's socket
        } // for all sockets

        // Yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(YIELD_TO_ALL_MS));
    }

error:
    if (listen_sock != INVALID_SOCK) {
        close(listen_sock);
    }

    for (int i=0; i<max_socks; ++i) {
        if (sock[i] != INVALID_SOCK) {
            // Don't check mutex here as this is already a fatal error case and don't want to prevent task shutdown
            close(sock[i]);
            sock[i] = INVALID_SOCK;
        }
    }

    free(address_info);
    vTaskDelete(NULL);
}