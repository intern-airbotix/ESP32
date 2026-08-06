#include <sys/cdefs.h>
/*
 *   This file is part of DroneBridge: https://github.com/DroneBridge/ESP32
 *
 *   Copyright 2018 Wolfgang Christl
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */
#include <sys/fcntl.h>
#include <sys/param.h>
#include <string.h>
#include <esp_task_wdt.h>
#include <esp_vfs_dev.h>
#include <lwip/inet.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <stdint-gcc.h>
#include <sys/types.h>
#include <lwip/netdb.h>
#include "esp_log.h"
#include "lwip/sockets.h"
#include "driver/uart.h"
#include "globals.h"
#include "msp_ltm_serial.h"
#include "db_protocol.h"
#include "tcp_server.h"
#include "db_esp32_control.h"
#ifdef CONFIG_BT_ENABLED
#include "db_ble.h"
#endif
#include <db_parameters.h>
#include "main.h"
#include "db_serial.h"
#include "db_esp_now.h"

#define TAG "DB_CONTROL"

uint16_t app_port_proxy = APP_PORT_PROXY;

udp_conn_list_t *udp_conn_list;
int connected_tcp_clients[CONFIG_LWIP_MAX_ACTIVE_TCP];
int8_t num_connected_tcp_clients = 0;
volatile bool db_udp_reinit_pending = false;
volatile uint32_t db_sta_subnet_broadcast_ip = 0; // network byte order; 0 = unknown/not connected. Set on IP_EVENT_STA_GOT_IP.

/**
 * Destination UDP port for all telemetry to the GCS in STA mode - mavesp8266-style fixed "host port".
 * Used for the discovery broadcast and for unicast once a GCS IP is learned. We never reply to the
 * sender's source port: a GCS may use several sockets (e.g. Skybrush broadcasts swarm commands from an
 * ephemeral port) and answering each socket would create duplicate telemetry streams.
 * @return configured udp_client_port if set, default MAVLink UDP port otherwise
 */
static uint16_t db_sta_gcs_port(void) {
    if (db_param_udp_client_port.value.db_param_u16.value != 0) {
        return db_param_udp_client_port.value.db_param_u16.value;
    }
    return APP_PORT_PROXY_UDP;
}

/**
 * Opens non-blocking UDP socket used for WiFi to UART communication. Does also accept broadcast packets just in case.
 * Binds to the user configured local UDP listen port (db_param_udp_listen_port, default 14550).
 * @return returns socket file descriptor
 */
int db_open_serial_udp_socket() {
    struct sockaddr_in server_addr;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DB_PARAM_UDP_LISTEN_PORT);
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    char addr_str[128];
    inet_ntoa_r(server_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

    int udp_socket = socket(addr_family, SOCK_DGRAM, ip_protocol);
    if (udp_socket < 0) {
        ESP_LOGE(TAG, "Unable to create socket for serial communication: errno %d", errno);
        return -1;
    }
    int broadcastEnable = 1;
    int err = setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to set udp socket to accept broadcast messages: errno %d", errno);
    }
    err = bind(udp_socket, (struct sockaddr *) &server_addr, sizeof(server_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind to %i errno %d", DB_PARAM_UDP_LISTEN_PORT, errno);
        close(udp_socket);
        return -1;
    }
    // Request the destination address of received datagrams (IP_PKTINFO via recvmsg). Needed to ignore broadcast
    // packets from other drones on the same network in STA mode (see control_module_udp_tcp).
    int pktinfo_on = 1;
    if (setsockopt(udp_socket, IPPROTO_IP, IP_PKTINFO, &pktinfo_on, sizeof(pktinfo_on)) < 0) {
        ESP_LOGW(TAG, "Could not enable IP_PKTINFO on UDP socket, errno %d - broadcast filtering unavailable", errno);
    }
    fcntl(udp_socket, F_SETFL, O_NONBLOCK);
    ESP_LOGI(TAG, "Opened UDP socket on port %i", DB_PARAM_UDP_LISTEN_PORT);
    return udp_socket;
}

/**
 * (Re-)registers the UDP host configured via the udp_client_ip/udp_client_port parameters (saved in NVM)
 * in the list of known UDP clients. The entry is marked as pinned so it is never auto-removed on send failures.
 * Safe to call multiple times - duplicates are not added but upgraded to pinned.
 * Called during boot after reading the settings and again after every Wi-Fi reconnect in STA mode, so the
 * configured GCS (e.g. Mission Planner listening on UDP) always receives MAVLink without having to send first.
 */
void db_register_saved_udp_host(void) {
    if (strlen((char *) db_param_udp_client_ip.value.db_param_str.value) > 0 &&
        db_param_udp_client_port.value.db_param_u16.value != 0) {
        ESP_LOGI(TAG, "Registering configured UDP host %s:%i as pinned UDP client.",
                 (char *) db_param_udp_client_ip.value.db_param_str.value,
                 db_param_udp_client_port.value.db_param_u16.value);
        struct sockaddr_in new_sockaddr;
        memset(&new_sockaddr, 0, sizeof(new_sockaddr));
        new_sockaddr.sin_family = AF_INET;
        if (inet_pton(AF_INET, (char *) db_param_udp_client_ip.value.db_param_str.value, &new_sockaddr.sin_addr) != 1) {
            ESP_LOGW(TAG, "Configured UDP host IP '%s' is not a valid IPv4 address - not registering",
                     (char *) db_param_udp_client_ip.value.db_param_str.value);
            return;
        }
        new_sockaddr.sin_port = htons(db_param_udp_client_port.value.db_param_u16.value);
        struct db_udp_client_t new_udp_client = {
                .udp_client = new_sockaddr,
                .mac = {0, 0, 0, 0, 0, 0},   // dummy MAC
                .pinned = true,
        };
        add_to_known_udp_clients(udp_conn_list, new_udp_client, false); // already in NVM - do not save again
    } else {
        // no UDP host configured - nothing to register
    }
}

/**
 * Removes auto-learned (unpinned) UDP clients that have not sent us anything for DB_UDP_CLIENT_IDLE_TIMEOUT_MS.
 * Called periodically from the control task in STA mode. Every GCS keeps sending heartbeats while connected, so a
 * silent client is one that closed its session (e.g. a Mission Planner UDPCI connection on an ephemeral port).
 * Without this, such a stale client would keep receiving the (unicast) telemetry stream forever and the discovery
 * broadcast on port 14550 would never resume. Pinned (user-configured) clients are never expired.
 */
void db_remove_expired_udp_clients(void) {
    if (udp_conn_list == NULL) return;
    uint32_t now = (uint32_t) xTaskGetTickCount();
    for (int i = 0; i < udp_conn_list->size; i++) {
        if (udp_conn_list->db_udp_clients[i].pinned) continue;
        if ((now - udp_conn_list->db_udp_clients[i].last_recv_tick) >= pdMS_TO_TICKS(DB_UDP_CLIENT_IDLE_TIMEOUT_MS)) {
            char *client_ip = inet_ntoa(((struct sockaddr_in *) &udp_conn_list->db_udp_clients[i].udp_client)->sin_addr);
            ESP_LOGI(TAG, "UDP - Removing client %s:%i - idle for >%i ms",
                     client_ip, ntohs(udp_conn_list->db_udp_clients[i].udp_client.sin_port),
                     DB_UDP_CLIENT_IDLE_TIMEOUT_MS);
            for (int j = i; j < udp_conn_list->size - 1; j++) {
                udp_conn_list->db_udp_clients[j] = udp_conn_list->db_udp_clients[j + 1];
            }
            udp_conn_list->size--;
            i--;
        }
    }
}

/**
 * Opens non-blocking UDP socket used for internal DroneBridge telemetry. Socket shall only be opened when local ESP32
 * is in client mode. Socket is used to receive internal Wifi telemetry from the ESP32 access point we are connected to.
 * @return returns socket file descriptor
 */
int db_open_int_telemetry_udp_socket() {
    // Create a socket for sending to the multicast address
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket. Error %d", errno);
        return -1;
    }
    struct sockaddr_in saddr = {0};
    // Bind the socket to any address
    saddr.sin_family = PF_INET;
    saddr.sin_port = htons(DB_ESP32_INTERNAL_TELEMETRY_PORT);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    esp_err_t err = bind(sock, (struct sockaddr *) &saddr, sizeof(struct sockaddr_in));
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to bind socket. Error %d", errno);
        return -1;
    }

    // Set the time-to-live of messages to 1 so they do not go past the local network
    int ttl = MULTICAST_TTL;
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        ESP_LOGE(TAG, "Failed to set IP_MULTICAST_TTL. Error %d", errno);
        close(sock);
        return -1;
    }

    fcntl(sock, F_SETFL, O_NONBLOCK);

    if (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_STA) {
        // Configure for listening when in station mode
        struct ip_mreq imreq = {0};
        struct in_addr iaddr = {0};
        // Configure source interface
        imreq.imr_interface.s_addr = IPADDR_ANY;
        // Configure multicast address to listen to
        err = inet_aton(MULTICAST_IPV4_ADDR, &imreq.imr_multiaddr.s_addr);
        if (err != 1) {
            ESP_LOGE(TAG, "Configured IPV4 multicast address '%s' is invalid.", MULTICAST_IPV4_ADDR);
            // Errors in the return value have to be negative
            close(sock);
            return -1;
        }
        ESP_LOGI(TAG, "Configured internal Multicast address %s", inet_ntoa(imreq.imr_multiaddr.s_addr));
        if (!IP_MULTICAST(ntohl(imreq.imr_multiaddr.s_addr))) {
            ESP_LOGW(TAG,
                     "Configured IPV4 multicast address '%s' is not a valid multicast address. This will probably not work.",
                     MULTICAST_IPV4_ADDR);
        }

        err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &iaddr, sizeof(struct in_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Failed to set IP_MULTICAST_IF. Error %d", errno);
            close(sock);
            return -1;
        }

        err = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imreq, sizeof(struct ip_mreq));
        if (err < 0) {
            ESP_LOGE(TAG, "Failed to set IP_ADD_MEMBERSHIP. Error %d", errno);
            close(sock);
            return -1;
        }
    }
    ESP_LOGI(TAG, "Opened internal telemetry socket on port: %i", DB_ESP32_INTERNAL_TELEMETRY_PORT);
    return sock;
}

/**
 * Sends data to all clients that are part of the udp connection list. No resending of packets in case of failure.
 * Special behavior in DroneShow Edition:
 *  - If there is no UDP client known (none learned, none configured via udp_client_ip), packets are broadcast on
 *    port 14550 so a GCS can auto-detect the drone without per-drone configuration (mavesp8266-style discovery):
 *    AP mode uses the limited broadcast address, STA mode uses the subnet-directed broadcast of the joined network.
 *  - The first GCS that replies is registered as a UDP client and the stream switches to unicast. If all clients
 *    are lost again (e.g. GCS gone), discovery broadcasting resumes automatically.
 *
 * @param data Buffer with the data to send
 * @param data_length Length of the data in the buffer
 */
void db_send_to_all_udp_clients(const uint8_t *data, uint data_length) {
    if (udp_conn_list == NULL || udp_conn_list->udp_socket < 0) {
        return;
    }
    static bool discovery_broadcast_announced = false;
    if (udp_conn_list->size == 0) {
        // No registered UDP clients yet — broadcast so a GCS (e.g. Mission Planner, Skybrush) can auto-detect the
        // MAVLink stream without needing to send a packet first. As soon as any GCS replies it gets registered as a
        // UDP client (size > 0) and we switch to pure unicast - same discovery scheme as ArduPilot's mavesp8266.
        // (SO_BROADCAST is already enabled on this socket at creation in db_open_serial_udp_socket)
        struct sockaddr_in broadcast_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(db_sta_gcs_port()),
            .sin_addr.s_addr = htonl(INADDR_BROADCAST),
        };
        if (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_AP || DB_PARAM_RADIO_MODE == DB_WIFI_MODE_AP_LR) {
            // AP mode: we own the network - limited broadcast is fine
        } else if (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_STA && db_sta_subnet_broadcast_ip != 0) {
            // STA mode: use the subnet-directed broadcast of the network we joined (e.g. 192.168.2.255) so packets
            // stay inside the drone/GCS network and are never routed upstream.
            broadcast_addr.sin_addr.s_addr = db_sta_subnet_broadcast_ip;
        } else {
            return; // no valid broadcast target (e.g. STA not connected yet)
        }
        if (!discovery_broadcast_announced) {
            ESP_LOGI(TAG, "No UDP client known - broadcasting telemetry to %s:%i until a GCS replies",
                     inet_ntoa(broadcast_addr.sin_addr), db_sta_gcs_port());
            discovery_broadcast_announced = true;
        }
        sendto(udp_conn_list->udp_socket, data, data_length, 0,
               (struct sockaddr *) &broadcast_addr, sizeof(broadcast_addr));
        return;
    }
    discovery_broadcast_announced = false; // clients known - re-announce if we ever fall back to discovery
    for (int i = 0; i < udp_conn_list->size; i++) {  // send to all UDP clients
        int sent = sendto(udp_conn_list->udp_socket, data, data_length, 0,
                          (struct sockaddr *) &udp_conn_list->db_udp_clients[i].udp_client,
                          sizeof(udp_conn_list->db_udp_clients[i].udp_client));
        if (sent != data_length) {
            int err = errno;
            char *client_ip = inet_ntoa(((struct sockaddr_in *)&udp_conn_list->db_udp_clients[i].udp_client)->sin_addr);
            udp_conn_list->db_udp_clients[i].send_fail_count++;
            if (udp_conn_list->db_udp_clients[i].send_fail_count >= 5) {
                if (udp_conn_list->db_udp_clients[i].pinned) {
                    // User-configured UDP host (saved in NVM) must survive Wi-Fi drops/reconnects.
                    // Never remove it - just keep trying. Log throttled via the fail counter wrap.
                    ESP_LOGW(TAG, "UDP - Configured host %s unreachable (errno: %d) - keeping it, will retry",
                             client_ip, err);
                    udp_conn_list->db_udp_clients[i].send_fail_count = 0;
                } else {
                    ESP_LOGW(TAG, "UDP - Removing unreachable client %s after repeated send failures (last errno: %d)",
                             client_ip, err);
                    for (int j = i; j < udp_conn_list->size - 1; j++) {
                        udp_conn_list->db_udp_clients[j] = udp_conn_list->db_udp_clients[j + 1];
                    }
                    udp_conn_list->size--;
                    i--;
                }
            } else {
                ESP_LOGE(TAG, "UDP - Error sending (%i/%i) to %s because of %s", sent, data_length, client_ip, strerror(err));
            }
        } else {
            udp_conn_list->db_udp_clients[i].send_fail_count = 0;
        }
    }
}

/**
 * Adds a payload to be sent via ESP-NOW to the ESP-NOW queue (where the esp-now task will pick it up, encrypt, package
 * and finally send it over the air)
 *
 * @param data Pointer to the payload buffer
 * @param data_length Length of the payload data. Must not be bigger than DB_ESPNOW_PAYLOAD_MAXSIZE - fails otherwise
 */
void db_send_to_all_espnow(uint8_t data[], const uint16_t *data_length) {
    db_espnow_queue_event_t evt;
    evt.data = malloc(*data_length);
    memcpy(evt.data, data, *data_length);
    evt.data_len = *data_length;
    evt.packet_type = DB_ESP_NOW_PACKET_TYPE_DATA;
    if (xQueueSend(db_espnow_send_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send to db_espnow_send_queue queue fail");
        free(evt.data);
    } else {
        // all good
    }
}

/**
 * Main call for sending anything over the air.
 * Send to all connected TCP & UDP clients or broadcast via ESP-NOW depending on the mode (DB_WIFI_MODE) we are currently in.
 * Typically called by a function that read from UART.
 *
 * When in ESP-NOW mode the packets will be split if they are bigger than DB_ESPNOW_PAYLOAD_MAXSIZE.
 *
 * @param tcp_clients Array of socket IDs for the TCP clients
 * @param udp_conn Structure handling the UDP connection
 * @param data payload to send
 * @param data_length Length of payload to send
 */
void db_send_to_all_clients(int tcp_clients[], udp_conn_list_t *n_udp_conn_list, uint8_t data[], uint16_t data_length) {
    db_ble_queue_event_t bleData;
    switch (DB_PARAM_RADIO_MODE) {
        case DB_WIFI_MODE_ESPNOW_AIR:
        case DB_WIFI_MODE_ESPNOW_GND:
            // ESP-NOW mode
            if (data_length > DB_ESPNOW_PAYLOAD_MAXSIZE) {
                // Data not properly sized, split into multiple packets
                uint16_t sent_bytes = 0;
                uint16_t next_chunk_len = 0;
                do {
                    next_chunk_len = data_length - sent_bytes;
                    if (next_chunk_len > DB_ESPNOW_PAYLOAD_MAXSIZE) {
                        next_chunk_len = DB_ESPNOW_PAYLOAD_MAXSIZE;
                    }
                    db_send_to_all_espnow(&data[sent_bytes], &next_chunk_len);
                    sent_bytes += next_chunk_len;
                } while (sent_bytes < data_length);
            } else {
                // Packet is properly sized - send to ESP-NOW outbound queue
                db_send_to_all_espnow(data, &data_length);
            }
            break;

        case DB_BLUETOOTH_MODE:
#ifdef CONFIG_BT_ENABLED
            bleData.data = malloc(data_length);
            bleData.data_len = data_length;
            memcpy(bleData.data, data, bleData.data_len);
            if (xQueueSend(db_uart_read_queue_ble, &bleData, portMAX_DELAY) != pdPASS) {
                ESP_LOGE(TAG, "Failed to send BLE data to queue");
                free(bleData.data);
            }
#endif
            break;

        default:
            // Other modes (WiFi Modes using TCP/UDP)
            db_send_to_all_tcp_clients(tcp_clients, data, data_length);
            db_send_to_all_udp_clients(data, data_length);
            break;
    }
}

/**
 * New function to replace the legacy db_send_to_all_clients(). No need to pass tcp & udp clients
 * Sends the message to all connected clients using the radio (WiFi, ESP-NOW or BLE) link.
 * @param data
 * @param data_length
 */
void db_send_to_all_radio_clients(uint8_t data[], uint16_t data_length) {
#ifdef CONFIG_BT_ENABLED
    db_ble_queue_event_t bleData;
#endif
    switch (DB_PARAM_RADIO_MODE) {
        case DB_WIFI_MODE_ESPNOW_AIR:
        case DB_WIFI_MODE_ESPNOW_GND:
            // ESP-NOW mode
            if (data_length > DB_ESPNOW_PAYLOAD_MAXSIZE) {
                // Data not properly sized, split into multiple packets
                uint16_t sent_bytes = 0;
                uint16_t next_chunk_len = 0;
                do {
                    next_chunk_len = data_length - sent_bytes;
                    if (next_chunk_len > DB_ESPNOW_PAYLOAD_MAXSIZE) {
                        next_chunk_len = DB_ESPNOW_PAYLOAD_MAXSIZE;
                    }
                    db_send_to_all_espnow(&data[sent_bytes], &next_chunk_len);
                    sent_bytes += next_chunk_len;
                } while (sent_bytes < data_length);
            } else {
                // Packet is properly sized - send to ESP-NOW outbound queue
                db_send_to_all_espnow(data, &data_length);
            }
            break;
        case DB_BLUETOOTH_MODE:
#ifdef CONFIG_BT_ENABLED
            bleData.data = malloc(data_length);
            bleData.data_len = data_length;
            memcpy(bleData.data, data, bleData.data_len);
            if (xQueueSend(db_uart_read_queue_ble, &bleData, portMAX_DELAY) != pdPASS) {
                ESP_LOGE(TAG, "Failed to send BLE data to queue");
                free(bleData.data);
            }
#endif
            break;

        default:
            // Other modes (WiFi Modes using TCP/UDP)
            db_send_to_all_tcp_clients(connected_tcp_clients, data, data_length);
            db_send_to_all_udp_clients(data, data_length);
            break;
    }
}

/**
 * Check for incoming connections on TCP server
 *
 * @param tcp_master_socket Main open TCP socket to accept TCP connections/clients
 * @param tcp_clients List of active TCP client connections (socket IDs)
 */
void handle_tcp_master(const int tcp_master_socket, int tcp_clients[]) {
    struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
    uint32_t addr_len = sizeof(source_addr);
    int new_tcp_client = accept(tcp_master_socket, (struct sockaddr *) &source_addr, &addr_len);
    if (new_tcp_client > 0) {
        for (int i = 0; i < CONFIG_LWIP_MAX_ACTIVE_TCP; i++) {
            if (tcp_clients[i] <= 0) {
                tcp_clients[i] = new_tcp_client;
                fcntl(tcp_clients[i], F_SETFL, O_NONBLOCK);
                char addr_str[128];
                inet_ntoa_r(((struct sockaddr_in *) &source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
                ESP_LOGI(TAG, "TCP: New client connected: %s", addr_str);
                num_connected_tcp_clients++;
                return;
            }
        }
        ESP_LOGW(TAG, "TCP: Could not accept connection — all %d slots full, closing fd=%d", CONFIG_LWIP_MAX_ACTIVE_TCP, new_tcp_client);
        close(new_tcp_client);  // must close or the FD leaks
    }
}

/**
 *  Init/Create structure containing all UDP connection information
 * @return Structure containing all UDP connection information
 */
udp_conn_list_t *udp_client_list_create() {
    udp_conn_list_t *n_udp_conn_list = malloc(sizeof(udp_conn_list_t)); // Allocate memory for the list
    if (n_udp_conn_list == NULL) { // Check if the allocation failed
        return NULL; // Return NULL to indicate an error
    }
    memset(n_udp_conn_list, 0, sizeof(udp_conn_list_t));
    n_udp_conn_list->size = 0; // Initialize the size to 0
    n_udp_conn_list->udp_socket = -1; // Mark the socket invalid until the control task opens it
    return n_udp_conn_list; // Return the pointer to the list
}

/**
 *  Destroy structure containing all UDP connection information
 * @param n_udp_conn_list Structure containing all UDP connection information
 */
void udp_client_list_destroy(udp_conn_list_t *n_udp_conn_list) {
    if (n_udp_conn_list == NULL) { // Check if the list is NULL
        return; // Do nothing
    }
    free(n_udp_conn_list); // Free the list
}

/**
 * Add a new UDP client to the list of known UDP clients. Checks if client is already known based on IP and port.
 * Added client will receive UDP packets with serial info and will be able to send UDP packets to the serial interface
 * of the ESP32.
 * PORT, MAC & IP should be set inside new_db_udp_client. If MAC is not set then the device cannot be removed later on.
 *
 * @param n_udp_conn_list Structure containing all UDP connection information
 * @param new_db_udp_client New client to add to the UDP list. PORT, MAC & IP must be set. If MAC is not set then the
 *                          device cannot be automatically removed later on. To remove it, the user must clear the entire list.
 * @param save_to_nvm Set to 1 (true) in case you want the UDP client to survive the reboot. Set to 0 (false) if client is temporary for this session.
 *                    It will then be saved to NVM and added to the udp_conn_list_t on startup. Only one client can be saved to NVM.
 * @return true if the client is in the list after the call (newly added or already known) - false on error (list NULL or full)
 */
bool
add_to_known_udp_clients(udp_conn_list_t *n_udp_conn_list, struct db_udp_client_t new_db_udp_client, bool save_to_nvm) {
    if (n_udp_conn_list == NULL) { // Check if the list is NULL
        return false; // Do nothing
    }
    for (int i = 0; i < n_udp_conn_list->size; i++) {
        if ((n_udp_conn_list->db_udp_clients[i].udp_client.sin_port == new_db_udp_client.udp_client.sin_port) &&
            (n_udp_conn_list->db_udp_clients[i].udp_client.sin_addr.s_addr ==
             new_db_udp_client.udp_client.sin_addr.s_addr)) {
            // client existing - do not add again, but upgrade to pinned if requested & give it a fresh start
            if (new_db_udp_client.pinned) {
                n_udp_conn_list->db_udp_clients[i].pinned = true;
                n_udp_conn_list->db_udp_clients[i].send_fail_count = 0;
            }
            // we hear from this client right now - refresh its idle-expiry timestamp
            n_udp_conn_list->db_udp_clients[i].last_recv_tick = (uint32_t) xTaskGetTickCount();
            // The user may save a client to NVM that was already learned dynamically (e.g. it sent us a packet
            // first). The NVM write must not be skipped in that case or the client would be lost after reboot.
            if (save_to_nvm) {
                save_udp_client_to_nvm(&new_db_udp_client, false);
            }
            return true; // client is in the list - report success
        }
    }
    if (n_udp_conn_list->size == MAX_UDP_CLIENTS) { // Check if the list is full
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(new_db_udp_client.udp_client.sin_addr), ip_str, INET_ADDRSTRLEN);
        ESP_LOGW(TAG, "UDP client list full (%i entries) - not adding %s:%i", MAX_UDP_CLIENTS,
                 ip_str, ntohs(new_db_udp_client.udp_client.sin_port));
        return false;
    }
    new_db_udp_client.last_recv_tick = (uint32_t) xTaskGetTickCount(); // start the idle-expiry clock
    n_udp_conn_list->db_udp_clients[n_udp_conn_list->size] = new_db_udp_client; // Copy the element data to the end of the array
    n_udp_conn_list->size++; // Increment the size of the list
    // some logging
    char ip_port_string[INET_ADDRSTRLEN + 10];
    char ip_string[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(new_db_udp_client.udp_client.sin_addr), ip_string, INET_ADDRSTRLEN);
    sprintf(ip_port_string, "%s:%d", ip_string, htons (new_db_udp_client.udp_client.sin_port));
    ESP_LOGI(TAG, "Added %s to udp client distribution list - save to NVM: %i", ip_port_string, save_to_nvm);
    // save to memory
    if (save_to_nvm) {
        save_udp_client_to_nvm(&new_db_udp_client, false);
    } else {
        // do not save to NVM
    }
    return true;
}

/**
 * Remove a client from the sending list. Client will no longer receive UDP packets. MAC address must be given.
 * Usually called in AP-Mode when a station disconnects. In any other case we will not know since UDP is a connection-less
 * protocol
 *
 * @param n_udp_conn_list Structure containing all UDP connection information
 * @param new_db_udp_client The UDP client to remove based on its MAC address
 * @return true if removed - false if nothing was removed
 */
bool remove_from_known_udp_clients(udp_conn_list_t *n_udp_conn_list, struct db_udp_client_t new_db_udp_client) {
    if (n_udp_conn_list == NULL) { // Check if the list is NULL
        return false; // Do nothing
    }
    for (int i = 0; i < n_udp_conn_list->size; i++) { // Loop through the array
        if (memcmp(n_udp_conn_list->db_udp_clients[i].mac, new_db_udp_client.mac,
                   sizeof(n_udp_conn_list->db_udp_clients[i].mac)) ==
            0) { // Compare the current array element with the element
            // Found a match
            for (int j = i; j < n_udp_conn_list->size - 1; j++) { // Loop from the current index to the end of the array
                n_udp_conn_list->db_udp_clients[j] = n_udp_conn_list->db_udp_clients[j +
                                                                                     1]; // Shift the array elements to the left
            }
            n_udp_conn_list->size--; // Decrement the size of the list
            return true; // Exit the function
        }
    }
    // No match found
    return false;
}

/**
 * Reads serial (UART/USB/JTAG) transparently or parsing MAVLink/MSP/LTM protocol.
 * Then sends read data to all connected clients via TCP/UDP or ESP-NOW.
 * Non-Blocking function
 *
 * @param tcp_clients Array of connected TCP client sockets
 * @param transparent_buff_pos Counter variable for total read UART bytes
 * @param msp_ltm_buff_pos Pointer position/data length of destination-buffer for read MSP messages
 * @param msp_message_buffer Destination-buffer for read MSP messages
 * @param serial_buffer Destination-buffer for the serial data
 * @param db_msp_ltm_port Pointer to structure containing MSP/LTM parser information
 */
void read_process_serial_link(int *tcp_clients, uint *transparent_buff_pos, uint *msp_ltm_buff_pos,
                              uint8_t *msp_message_buffer,
                              uint8_t *serial_buffer, msp_ltm_port_t *db_msp_ltm_port) {
    switch (DB_PARAM_SERIAL_PROTO) {
        case 0:
        case 2:
        case DB_SERIAL_PROTOCOL_MSPLTM:
            db_parse_msp_ltm(tcp_clients, udp_conn_list, msp_message_buffer, msp_ltm_buff_pos, db_msp_ltm_port);
            break;
        case 3:
        case DB_SERIAL_PROTOCOL_MAVLINK:
            db_read_serial_parse_mavlink(tcp_clients, udp_conn_list, msp_message_buffer, transparent_buff_pos);
            break;
        case DB_SERIAL_PROTOCOL_TRANSPARENT:
        default:
            db_read_serial_parse_transparent(tcp_clients, udp_conn_list, serial_buffer, transparent_buff_pos);
            break;
    }
}

/**
 * Thread that manages all incoming and outgoing ESP-NOW and serial (UART) connections.
 * Called only when ESP-NOW mode is selected
 */
_Noreturn void control_module_esp_now() {
    ESP_LOGI(TAG, "Starting control module (ESP-NOW)");
    esp_err_t serial_socket = ESP_FAIL;
    // open serial socket for comms with FC or GCS
    serial_socket = open_serial_socket();
    if (serial_socket == ESP_FAIL) {
        ESP_LOGE(TAG, "UART socket not opened. Aborting start of control module.");
        vTaskDelete(NULL);
    } else {
#ifdef CONFIG_DB_SERIAL_OPTION_JTAG
        db_jtag_serial_info_print();
#endif
    }

    uint transparent_buff_pos = 0;
    uint msp_ltm_buff_pos = 0;
    uint8_t msp_message_buffer[UART_BUF_SIZE];
    uint8_t serial_buffer[DB_PARAM_SERIAL_PACK_SIZE];
    msp_ltm_port_t db_msp_ltm_port;
    db_espnow_queue_event_t db_espnow_uart_evt;
    uint delay_timer_cnt = 0;

    ESP_LOGI(TAG, "Started control module (ESP-NOW)");
    while (1) {
        // read UART (and split into packets & process MAVLink if desired); send to ESP-NOW queue to be processed by esp-now task
        read_process_serial_link(NULL, &transparent_buff_pos, &msp_ltm_buff_pos, msp_message_buffer, serial_buffer,
                                 &db_msp_ltm_port);
        // read queue that was filled by esp-now task to check for data that needs to be sent via serial link
        if (db_uart_write_queue != NULL && xQueueReceive(db_uart_write_queue, &db_espnow_uart_evt, 1) == pdTRUE) {
            if (DB_PARAM_SERIAL_PROTO == DB_SERIAL_PROTOCOL_MAVLINK) {
                // Parse, so we can listen in and react to certain messages - function will send parsed messages to serial link.
                // We can not write to serial first since we might inject packets and do not know when to do so to not "destroy" an existing packet
                db_parse_mavlink_from_radio(NULL, NULL, db_espnow_uart_evt.data, db_espnow_uart_evt.data_len);
            } else {
                // no parsing with any other protocol - transparent here - just pass through
                write_to_serial(db_espnow_uart_evt.data, db_espnow_uart_evt.data_len);
            }
            free(db_espnow_uart_evt.data);
        } else {
            if (db_uart_write_queue == NULL) ESP_LOGE(TAG, "db_uart_write_queue is NULL!");
            // no new data available to be sent via serial link do nothing
        }
        if (delay_timer_cnt == 5000) {
            /* all actions are non-blocking so allow some delay so that the IDLE task of FreeRTOS and the watchdog can run
            read: https://esp32developer.com/programming-in-c-c/tasks/tasks-vs-co-routines for reference */
            vTaskDelay(10 / portTICK_PERIOD_MS);
            delay_timer_cnt = 0;
        } else {
            delay_timer_cnt++;
        }
    }
    vTaskDelete(NULL);
}

/**
 * Sends DroneBridge internal telemetry to tell every connected WiFi station how well we receive their data (rssi).
 * Uses UDP multicast message. Format: [NUM_Entries - (MAC + RSSI) - (MAC + RSSI) - ...]
 * Internal telemetry uses DB_ESP32_INTERNAL_TELEMETRY_PORT port
 *
 * @param sta_list
 */
void db_send_internal_telemetry_to_stations(int tel_sock, wifi_sta_list_t *sta_list, udp_conn_list_t *udp_conns) {
    if (tel_sock < 0) {
        return; // socket not open in this mode - nothing to send
    }
    if ((DB_PARAM_RADIO_MODE == DB_WIFI_MODE_AP_LR || DB_PARAM_RADIO_MODE == DB_WIFI_MODE_AP)
        && udp_conns->size > 0 && sta_list->num > 0) {
        char addr_buf[32] = {0};
        struct addrinfo hints = {
                .ai_flags = AI_PASSIVE,
                .ai_socktype = SOCK_DGRAM,
        };
        struct addrinfo *res;

        // Send an IPv4 multicast packet
        hints.ai_family = AF_INET; // For an IPv4 socket
        int err = getaddrinfo(MULTICAST_IPV4_ADDR, NULL, &hints, &res);
        if (err < 0) {
            ESP_LOGE(TAG, "getaddrinfo() failed for IPV4 destination address. error: %d", err);
            return;
        }
        if (res == 0) {
            ESP_LOGE(TAG, "getaddrinfo() did not return any addresses");
            return;
        }
        ((struct sockaddr_in *) res->ai_addr)->sin_port = htons(DB_ESP32_INTERNAL_TELEMETRY_PORT);
        inet_ntoa_r(((struct sockaddr_in *) res->ai_addr)->sin_addr, addr_buf, sizeof(addr_buf) - 1);
        static uint8_t buffer[1280] = {0};
        uint16_t buffer_pos = 1;    // we start at 1 since we want to put the count in position 0
        uint8_t already_sent = 0;
        for (int i = 0; i < sta_list->num; i++) {
            if ((buffer_pos + 7) < 1280) {
                memcpy(&buffer[buffer_pos], sta_list->sta[i].mac, 6);
                buffer_pos += 6;
                buffer[buffer_pos] = sta_list->sta[i].rssi;
                buffer_pos++;
            } else {
                // packet would get too long. Sent this chunk already
                buffer[0] = (uint8_t) i;    // first byte shall be the number of entries in the packet
                sendto(tel_sock, buffer, buffer_pos, 0, res->ai_addr, res->ai_addrlen);
                already_sent += i;
                buffer_pos = 1;
            }
        }
        buffer[0] = (uint8_t) sta_list->num - already_sent;
        err = sendto(tel_sock, buffer, buffer_pos + 1, 0, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
        if (err < 0) {
            ESP_LOGE(TAG, "Internal telemetry sendto failed. errno: %d", errno);
            return;
        }
    } else {
        // in other modes we cannot do that. ESP-NOW uses a different function and way of telling
    }
}

/**
 * Receive and process internal telemetry (ESP32 AP to ESP32 Station) sent by ESP32 LR access point.
 * Matches with db_send_internal_telemetry_to_stations()
 * Sets station_rssi_ap based on the received value
 * Packet format: [NUM_Entries, (MAC + RSSI), (MAC + RSSI), (MAC + RSSI), ...]
 *
 * @param tel_sock Socket listening for internal telemetry
 */
void handle_internal_telemetry(int tel_sock, uint8_t *udp_buffer, socklen_t *sock_len, struct sockaddr_in *udp_client) {
    if (tel_sock > 0) {
        ssize_t recv_length = recvfrom(tel_sock, udp_buffer, UDP_BUF_SIZE, 0,
                                       (struct sockaddr *) udp_client, sock_len);
        if (recv_length > 0) {
            ESP_LOGD(TAG, "Got internal telem. frame containing %i entries", udp_buffer[0]);
            for (int i = 1; i < (udp_buffer[0] * 7); i += 7) {
                if (memcmp(LOCAL_MAC_ADDRESS, &udp_buffer[i], ESP_NOW_ETH_ALEN) == 0) {
                    // found us in the list (this local ESP32 AIR unit) -> update internal telemetry buffer,
                    // so it gets sent with next Mavlink RADIO STATUS in case MAVLink radio status is enabled
                    db_esp_signal_quality.gnd_rssi = (int8_t) udp_buffer[i + 6];
                    ESP_LOGD(TAG, "AP receives our packets with RSSI: %i", db_esp_signal_quality.gnd_rssi);
                    break;
                } else {
                    // keep on looking for our MAC
                }
            }
        } else {/* received nothing - socket is non-blocking */}
    } else {/* socket failed to init or was never inited */}
}

/**
 * Thread that manages all incoming and outgoing TCP, UDP and serial (UART) connections.
 * Executed when Wi-Fi modes are set - ESP-NOW has its own thread
 */
_Noreturn void control_module_udp_tcp() {
    ESP_LOGI(TAG, "Starting control module (Wi-Fi)");
    esp_err_t serial_socket_status = open_serial_socket();
    if (serial_socket_status == ESP_FAIL) {
        ESP_LOGE(TAG, "JTAG serial socket not opened. Aborting start of control module.");
        vTaskDelete(NULL);
    } else {
#ifdef CONFIG_DB_SERIAL_OPTION_JTAG
        db_jtag_serial_info_print();
#endif
    }

    int tcp_master_socket = open_tcp_server(app_port_proxy);
    if (tcp_master_socket == ESP_FAIL) {
        ESP_LOGE(TAG, "TCP master socket failed to open. Aborting start of control module.");
        vTaskDelete(NULL);
    }
    fcntl(tcp_master_socket, F_SETFL, O_NONBLOCK);
    for (int i = 0; i < CONFIG_LWIP_MAX_ACTIVE_TCP; i++) {
        connected_tcp_clients[i] = -1;
    }

    udp_conn_list->udp_socket = db_open_serial_udp_socket();
    int db_internal_telem_udp_sock = -1;
    if (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_AP || DB_PARAM_RADIO_MODE == DB_WIFI_MODE_AP_LR ||
        DB_PARAM_RADIO_MODE == DB_WIFI_MODE_STA) {
        // AP & AP_LR send the per-station RSSI multicast, STA receives it. Plain AP was missing here
        // while db_send_internal_telemetry_to_stations() explicitly handles it -> sendto() on fd -1
        // spammed "Internal telemetry sendto failed. errno: 9" whenever a station was connected.
        db_internal_telem_udp_sock = db_open_int_telemetry_udp_socket();
    } else {
        // ESP-NOW uses different sockets/systems
    }
    uint8_t udp_buffer[UDP_BUF_SIZE];
    struct db_udp_client_t new_db_udp_client = {0};
    socklen_t udp_socklen = sizeof(new_db_udp_client.udp_client);

    uint transparent_buff_pos = 0;
    uint msp_ltm_buff_pos = 0;
    uint8_t tcp_client_buffer[TCP_BUFF_SIZ];
    memset(tcp_client_buffer, 0, TCP_BUFF_SIZ);
    uint8_t msp_message_buffer[UART_BUF_SIZE];
    uint8_t serial_buffer[DB_PARAM_SERIAL_PACK_SIZE];
    msp_ltm_port_t db_msp_ltm_port;
    int delay_timer_cnt = 0;

    ESP_LOGI(TAG, "Started control module (Wi-Fi)");
    while (1) {
        // Wi-Fi (re)connected (IP_EVENT_STA_GOT_IP) -> recreate the UDP socket and re-register the configured
        // UDP host so the telemetry stream resumes automatically after a Wi-Fi drop in STA mode.
        if (db_udp_reinit_pending) {
            db_udp_reinit_pending = false;
            if (udp_conn_list->udp_socket >= 0) {
                close(udp_conn_list->udp_socket);
                udp_conn_list->udp_socket = -1;
            }
            udp_conn_list->udp_socket = db_open_serial_udp_socket();
            db_register_saved_udp_host();
            ESP_LOGI(TAG, "Wi-Fi (re)connect: UDP socket reinitialized & configured UDP host re-registered");
        }
        // Self-heal a failed UDP socket. db_open_serial_udp_socket() can return -1 (transient socket()/bind()
        // failure, e.g. racing an interface change or ENOMEM under load). Without this retry the socket stays
        // -1 forever: the drone is associated on the AP but silently deaf and mute. Retry ~once per second.
        if (udp_conn_list->udp_socket < 0) {
            static TickType_t last_udp_reopen_tick = 0;
            TickType_t now_tick = xTaskGetTickCount();
            if (last_udp_reopen_tick == 0 || (now_tick - last_udp_reopen_tick) >= pdMS_TO_TICKS(1000)) {
                last_udp_reopen_tick = now_tick;
                udp_conn_list->udp_socket = db_open_serial_udp_socket();
                if (udp_conn_list->udp_socket >= 0) {
                    db_register_saved_udp_host();
                    ESP_LOGW(TAG, "UDP socket was down - successfully reopened and re-registered configured host");
                }
            }
        }
        // Read incoming wireless data (Wi-Fi)
        // Wi-Fi based modes that use TCP and UDP communication
        bool data_processed = false;
        uint32_t prev_serial_count = serial_total_byte_count;
        int8_t prev_tcp_clients = num_connected_tcp_clients;

        handle_tcp_master(tcp_master_socket, connected_tcp_clients);
        if (num_connected_tcp_clients != prev_tcp_clients) data_processed = true;
        for (int i = 0; i < CONFIG_LWIP_MAX_ACTIVE_TCP; i++) {  // handle TCP clients
            if (connected_tcp_clients[i] > 0) {
                ssize_t recv_length = recv(connected_tcp_clients[i], tcp_client_buffer, TCP_BUFF_SIZ, 0);
                if (recv_length > 0) {
                    data_processed = true;
                    if (DB_PARAM_SERIAL_PROTO == DB_SERIAL_PROTOCOL_MAVLINK) {
                        // Parse, so we can listen in and react to certain messages - function will send parsed messages to serial link.
                        // We can not write to serial first since we might inject packets and do not know when to do so to not "destroy" an existign packet
                        db_parse_mavlink_from_radio(connected_tcp_clients, udp_conn_list, tcp_client_buffer, recv_length);
                    } else {
                        // no parsing with any other protocol - transparent here
                        write_to_serial(tcp_client_buffer, recv_length);
                    }
                } else if (recv_length == 0) {
                    shutdown(connected_tcp_clients[i], 0);
                    close(connected_tcp_clients[i]);
                    connected_tcp_clients[i] = -1;
                    ESP_LOGI(TAG, "TCP client disconnected");
                    num_connected_tcp_clients--;
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    ESP_LOGE(TAG, "Error receiving from TCP client %i (fd: %i): %d", i, connected_tcp_clients[i], errno);
                    shutdown(connected_tcp_clients[i], 0);
                    close(connected_tcp_clients[i]);
                    num_connected_tcp_clients--;
                    connected_tcp_clients[i] = -1;
                }
            }
        }
        // handle incoming UDP data on main port 14550 - Read UDP and forward to UART.
        // recvmsg with IP_PKTINFO instead of recvfrom: we need the DESTINATION address of each datagram to filter
        // out certain broadcasts in STA mode. Many drones share one network in a drone show - every drone's discovery
        // broadcast is relayed to all other drones by the AP. Those must never be forwarded to the FC or register
        // the other drone as a UDP client. However, GCS software (e.g. Skybrush) also uses broadcast for swarm-wide
        // commands (ARM, RTK, START, TIMESYNC). To distinguish: discovery broadcasts come FROM the DroneBridge
        // MAVLink port (14550), while GCS broadcasts come from ephemeral source ports.
        struct iovec udp_iov = {.iov_base = udp_buffer, .iov_len = UDP_BUF_SIZE};
        uint8_t udp_cmsg_buf[CMSG_SPACE(sizeof(struct in_pktinfo))];
        struct msghdr udp_msg = {
                .msg_name = &new_db_udp_client.udp_client,
                .msg_namelen = sizeof(new_db_udp_client.udp_client),
                .msg_iov = &udp_iov,
                .msg_iovlen = 1,
                .msg_control = udp_cmsg_buf,
                .msg_controllen = sizeof(udp_cmsg_buf),
        };
        ssize_t recv_length = recvmsg(udp_conn_list->udp_socket, &udp_msg, 0);
        if (recv_length > 0 && DB_PARAM_RADIO_MODE == DB_WIFI_MODE_STA) {
            for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&udp_msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&udp_msg, cmsg)) {
                if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_PKTINFO) {
                    uint32_t dest_ip = ((struct in_pktinfo *) CMSG_DATA(cmsg))->ipi_addr.s_addr;
                    if (dest_ip == htonl(INADDR_BROADCAST) ||
                        (db_sta_subnet_broadcast_ip != 0 && dest_ip == db_sta_subnet_broadcast_ip)) {
                        // Broadcast: distinguish GCS from other drones by the MAVLink source sysid of
                        // the first frame. Drones use sysid 1-250 (Skybrush convention), GCS software
                        // uses 251-255 (Skybrush server, QGC & Mission Planner: 255). Source ports are
                        // not reliable for this: the Skybrush server broadcasts from the same socket it
                        // listens on (source port 14550), other tools use ephemeral source ports.
                        bool is_gcs_broadcast;
                        if (DB_PARAM_SERIAL_PROTO == DB_SERIAL_PROTOCOL_MAVLINK) {
                            uint8_t src_sysid = 0;
                            if (recv_length > 5 && udp_buffer[0] == 0xFD) {
                                src_sysid = udp_buffer[5];  // MAVLink v2: sysid at byte 5
                            } else if (recv_length > 3 && udp_buffer[0] == 0xFE) {
                                src_sysid = udp_buffer[3];  // MAVLink v1: sysid at byte 3
                            }
                            is_gcs_broadcast = src_sysid > 250;
                        } else {
                            // non-MAVLink protocols: fall back to the source-port heuristic
                            // (every DroneBridge broadcasts from its bound listen port)
                            is_gcs_broadcast = new_db_udp_client.udp_client.sin_port != htons(DB_PARAM_UDP_LISTEN_PORT);
                        }
                        if (!is_gcs_broadcast) {
                            // another drone's discovery broadcast (or junk) - ignore it entirely
                            data_processed = true;
                            recv_length = 0;
                        }
                    }
                    break;
                }
            }
        }
        if (recv_length > 0) {
            data_processed = true;
            if (DB_PARAM_SERIAL_PROTO == DB_SERIAL_PROTOCOL_MAVLINK) {
                // Parse, so we can listen in and react to certain messages - function will send parsed messages to serial link.
                // We can not write to serial first since we might inject packets and do not know when to do so to not "destroy" an existing packet
                db_parse_mavlink_from_radio(connected_tcp_clients, udp_conn_list, udp_buffer, recv_length);
            } else {
                // no parsing with any other protocol - transparent here
                write_to_serial(udp_buffer, recv_length);
            }
            // all devices that send us UDP data will be added to the list of UDP receivers
            // In AP-Mode clients register with their source port and can be removed based on the IP/MAC address.
            if (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_STA) {
                // mavesp8266-style: learn only the GCS IP - telemetry always goes to the fixed GCS port,
                // never back to the sender's source port. All sockets of one GCS host collapse into a
                // single client entry, so no duplicate streams to ephemeral ports (e.g. Skybrush's
                // broadcast socket). Idle entries expire via db_remove_expired_udp_clients().
                new_db_udp_client.udp_client.sin_port = htons(db_sta_gcs_port());
            }
            add_to_known_udp_clients(udp_conn_list, new_db_udp_client, false);
        } else {
            // received nothing, keep on going
        }
        if (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_STA) {
            handle_internal_telemetry(db_internal_telem_udp_sock, udp_buffer, &udp_socklen,
                                      &new_db_udp_client.udp_client);
        } else {
            // internal telemetry only received when in STA mode. Coming from the ESP32 AP. Nothing to do here
        }

        // Second check for incoming UART data and send it to TCP/UDP
        read_process_serial_link(connected_tcp_clients, &transparent_buff_pos, &msp_ltm_buff_pos, msp_message_buffer,
                                 serial_buffer,
                                 &db_msp_ltm_port);
        if (serial_total_byte_count != prev_serial_count) data_processed = true;

        if (delay_timer_cnt >= 6000) {
            // all actions are non-blocking so allow some delay so that the IDLE task of FreeRTOS and the watchdog can run
            // read: https://esp32developer.com/programming-in-c-c/tasks/tasks-vs-co-routines for reference
            vTaskDelay(10 / portTICK_PERIOD_MS);
            delay_timer_cnt = 0;
            // Use the opportunity to get some regular status information like rssi and send them via internal telemetry
            if (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_STA) {
                // update rssi variable - set to -127 when not available
                if (esp_wifi_sta_get_rssi((int *) &db_esp_signal_quality.air_rssi) != ESP_OK) {
                    db_esp_signal_quality.air_rssi = -127;
                } else {/* all good */}
                db_check_sta_link_timeout();   // runtime STA->AP fallback after sustained disconnect
                db_remove_expired_udp_clients(); // drop auto-learned clients that went silent (stale UDPCI sessions)
            } else if (!DB_RADIO_IS_OFF &&
                       (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_AP || DB_PARAM_RADIO_MODE == DB_WIFI_MODE_AP_LR)) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(
                        esp_wifi_ap_get_sta_list(&wifi_sta_list)); // update list of connected stations
                db_send_internal_telemetry_to_stations(db_internal_telem_udp_sock, &wifi_sta_list, udp_conn_list);
            } else {
                // no way of getting RSSI here. Do nothing
            }
//            size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
//            size_t lowmark_dram = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
//            ESP_LOGI(TAG, "Free heap: %i, low mark: %i", free_dram, lowmark_dram);
        } else {
            delay_timer_cnt++;
        }

        if (!data_processed) {
            vTaskDelay(pdMS_TO_TICKS(1));   // Add a delay of 1 ms since we did not read anything in the last cycle
        }
    }
    vTaskDelete(NULL);
}

#ifdef CONFIG_BT_ENABLED

_Noreturn void control_module_ble() {
    ESP_LOGI(TAG, "Starting control module (Bluetooth)");

    /* Initialize error code as failed, because UART is not initialized*/
    esp_err_t serial_socket = ESP_FAIL;

    /* open serial socket for comms with FC or GCS */
    serial_socket = open_serial_socket();
    if (serial_socket == ESP_FAIL) {
        ESP_LOGE(TAG, "UART socket not opened. Aborting start of control module.");
        vTaskDelete(NULL);
    }
#ifdef CONFIG_DB_SERIAL_OPTION_JTAG
    else {
      db_jtag_serial_info_print();
    }
#endif

    uint8_t msp_message_buffer[UART_BUF_SIZE];
    uint8_t serial_buffer[DB_PARAM_SERIAL_PACK_SIZE];
    msp_ltm_port_t db_msp_ltm_port;
    db_ble_queue_event_t bleData;
    uint transparent_buff_pos = 0;
    uint msp_ltm_buff_pos = 0;
    uint delay_timer_cnt = 0;

    /* Event Loop */
    while (1) {
        /* Read UART and send data to BLE */
        read_process_serial_link(NULL,                  // NULL, not using TCP
                                 &transparent_buff_pos, // transparent buffer position
                                 &msp_ltm_buff_pos,     // msp buffer position
                                 msp_message_buffer,    // msp buffer
                                 serial_buffer,         // serial buffer
                                 &db_msp_ltm_port       // msp port
        );

        if (db_uart_write_queue_ble != NULL && xQueueReceive(db_uart_write_queue_ble, &bleData, 1) == pdTRUE) {
            if (DB_PARAM_SERIAL_PROTO == DB_SERIAL_PROTOCOL_MAVLINK) {
                // Parse, so we can listen in and react to certain messages - function will send parsed messages to serial link.
                // We cannot write to serial first since we might inject packets and do not know when to do so to not "destroy" an
                // existing packet
                db_parse_mavlink_from_radio(NULL, NULL, bleData.data, bleData.data_len);
            } else {
                // no parsing with any other protocol - transparent here - just pass through
                write_to_serial(bleData.data, bleData.data_len);
            }
            free(bleData.data);
        } else {
            if (db_uart_write_queue_ble == NULL)
                ESP_LOGE(TAG, "db_uart_write_queue is NULL!");
            // no new data available to be sent via serial link do nothing
        }

        /**Yield to the scheduler if delay_timer_cnt reaches 5000,allowing other
         * tasks to execute and preventing starvation.
         */
        if (delay_timer_cnt == 5000) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            delay_timer_cnt = 0;
        } else {
            delay_timer_cnt++;
        }
    }
    vTaskDelete(NULL);
}

#endif

/**
 * @brief DroneBridge control module implementation for a ESP32 device. Bidirectional link between FC and ground. Can
 * handle MSPv1, MSPv2, LTM and MAVLink.
 * MSP & LTM is parsed and sent packet/frame by frame to ground
 * MAVLink is passed through (fully transparent). Can be used with any protocol.
 */
void db_start_control_module() {
    switch (DB_PARAM_RADIO_MODE) {
        case DB_WIFI_MODE_ESPNOW_GND:
        case DB_WIFI_MODE_ESPNOW_AIR:
            xTaskCreate(&control_module_esp_now, /**< Task function for ESP-NOW communication */
                        "control_espnow",           /**< Task name (for debugging) */
                        40960,                  /**< Stack size (in bytes) */
                        NULL,                   /**< Task parameters (unused) */
                        5,                         /**< Task priority */
                        NULL                   /**< Task handle (unused) */
            );
            break;

        case DB_BLUETOOTH_MODE:
#ifdef CONFIG_BT_ENABLED
            xTaskCreate(&control_module_ble,   /**< Task function for Bluetooth BLE communication */
                        "control_bluetooth",      /**< Task name (for debugging) */
                        40960,                /**< Stack size (in bytes) */
                        NULL,                 /**< Task parameters (unused) */
                        5,                       /**< Task priority */
                        NULL                 /**< Task handle (unused) */
            );
#else
            ESP_LOGE(TAG, "Bluetooth is not enabled. Aborting start of control module.");
#endif
            break;

        default:
            xTaskCreate(&control_module_udp_tcp,  /**< Task function for UDP/TCP communication */
                        "control_wifi",              /**< Task name (for debugging) */
                        46080,                   /**< Stack size (in bytes) */
                        NULL,                    /**< Task parameters (unused) */
                        5,                          /**< Task priority */
                        NULL                    /**< Task handle (unused) */
            );
            break;
    }
}
