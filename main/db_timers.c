/*
*   This file is part of DroneBridge: https://github.com/DroneBridge/ESP32
 *
 *   Copyright 2026 Wolfgang Christl
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

#include "db_timers.h"
#include "db_mavlink_msgs.h"
#include "db_parameters.h"
#include "esp_log.h"
#include "globals.h"
#include <esp_wifi.h>
#include "db_led_indicator.h"

#define TAG "DB_TIMERS"

/**
 * Callback that reads the RSSI that the ESP32 is having to the AP it is connected to
 * @param pxTimer
 */
void db_timer_wifi_rssi_callback(TimerHandle_t pxTimer) {
  // This function is called periodically by the FreeRTOS timer
  if (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_STA) {
    // update rssi variable - set to -127 when not available
    int rssi_temp = -127;
    if (esp_wifi_sta_get_rssi(&rssi_temp) != ESP_OK) {
      db_esp_signal_quality.air_rssi = -127;
      ESP_LOGD(TAG, "Failed to get RSSI (likely scanning/connecting)");
    } else {
      db_esp_signal_quality.air_rssi = rssi_temp;
#if CONFIG_SOC_WIFI_SUPPORT_5G
        wifi_band_mode_t cur_band = WIFI_BAND_MODE_AUTO;
        esp_wifi_get_band_mode(&cur_band);
        
        static int dbg_log = 0;
        if (dbg_log++ % 5 == 0) {
            ESP_LOGI(TAG, "ROAM DEBUG: rssi=%i, config_band=%i, current_radio_band=%i", db_esp_signal_quality.air_rssi, DB_PARAM_WIFI_BAND, cur_band);
        }

        if (DB_PARAM_WIFI_BAND == DB_WIFI_BAND_AUTO) {
            // Proactively switch to 2.4 GHz if 5 GHz drops below -70 dBm
            if (cur_band == WIFI_BAND_MODE_5G_ONLY && db_esp_signal_quality.air_rssi < -70) {
                ESP_LOGW(TAG, "5 GHz signal is poor (%i dBm). Proactively switching to 2.4 GHz...", db_esp_signal_quality.air_rssi);
                ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY));
                // Driver restarts instantly; STA_START will fire and esp_wifi_connect will find the stronger 2.4 GHz
            }
        }
#endif
    }
  } else if (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_AP || DB_PARAM_RADIO_MODE == DB_WIFI_MODE_AP_LR) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_ap_get_sta_list(&wifi_sta_list)); // update list of connected stations
  }
}

/**
 * Reads the RSSI that the ESP32 is having to the AP it is connected to every DB_TIMER_RSSI_PERIOD_MS
 * Or updated the client list in AP mode to get the latest RSSI of every client
 */
void db_timer_start_wifi_rssi_timer() {
  static TimerHandle_t xRssiTimer;
  xRssiTimer = xTimerCreate(
          "RSSI_Timer",
          pdMS_TO_TICKS(DB_TIMER_RSSI_PERIOD_MS),
          pdTRUE,
          (void *) 0,
          db_timer_wifi_rssi_callback
  );

  if (xRssiTimer == NULL) {
    ESP_LOGE(TAG, "Failed to create RSSI timer.");
  }
  if (xRssiTimer != NULL) {
    ESP_LOGI(TAG, "Starting RSSI timer.");
    xTimerStart(xRssiTimer, 0);
  }
}

/**
 * Injects a MAVLink heartbeat if MAVLink is selected as serial telemetry protocol. If necessary, a fake heartbeat for the FC as well.
 * @param pxTimer
 */
void db_timer_mavlink_heartbeat_callback(TimerHandle_t pxTimer) {
    static uint8_t buff[296];
    if (DB_PARAM_SERIAL_PROTO != DB_SERIAL_PROTOCOL_MAVLINK) {
        return; // Do not send heartbeat in non mavlink modes
    }
    if (db_get_mav_sys_id() != 0) {
        if (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_ESPNOW_GND || DB_PARAM_RADIO_MODE == DB_WIFI_MODE_AP_LR) {
            // In AP LR mode and in ESP-NOW GND mode the heartbeat has to be emitted via serial directly to the GCS
            uint16_t length = db_mav_create_heartbeat(buff, &fmav_status_serial);
            write_to_serial(buff, length);
        } else if (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_STA || DB_PARAM_RADIO_MODE == DB_WIFI_MODE_AP) {
            // mavesp8266-style: the radio does not emit its own heartbeat over Wi-Fi. The FC's heartbeat
            // flows through anyway; a radio heartbeat per drone only adds airtime in large fleets.
        } else {
            // Send heartbeat via radio interface (ESP-NOW AIR, BLE)
            uint16_t length = db_mav_create_heartbeat(buff, &fmav_status_radio);
            db_send_to_all_radio_clients(buff, length);
        }
    } else {
        // haven't seen a system ID from the FC yet so do not send any heartbeat or heartbeats disabled
    }
}

/**
 * Starts a periodic MAVLink heartbeat sending timer.
 */
void db_timer_start_mavlink_heartbeat() {
  static TimerHandle_t xHeartBeatTimerHandle;
  xHeartBeatTimerHandle = xTimerCreate(
          "MAV_Heartbeat_Timer",
          pdMS_TO_TICKS(DB_TIMER_MAVLINK_HEARTBEAT_MS),
          pdTRUE,
          (void *) 0,
          db_timer_mavlink_heartbeat_callback
  );

  if (xHeartBeatTimerHandle == NULL) {
    ESP_LOGE(TAG, "Failed to create heartbeat timer.");
  }
  if (xHeartBeatTimerHandle != NULL) {
    ESP_LOGI(TAG, "Starting to send heartbeats.");
    xTimerStart(xHeartBeatTimerHandle, 0);
  }
}

/**
 * Timer callback to send a Radio Status Message to the GND in case MAVLink is set as serial protocol
 * @param pxTimer
 */
void db_timer_mavlink_radiostatus_callback(TimerHandle_t pxTimer) {
    if (DB_PARAM_SERIAL_PROTO != DB_SERIAL_PROTOCOL_MAVLINK) {
        return; // Do not send heartbeat in transparent mode
    }
    static uint8_t buff[296];
    // ESP32s that are connected to a flight controller via UART will send RADIO_STATUS messages to the GND
    if (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_STA && udp_conn_list != NULL && udp_conn_list->size == 0 &&
        num_connected_tcp_clients == 0) {
        return; // mavesp8266-style: no RADIO_STATUS while no GCS is known (discovery phase)
    }
    if (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_STA || DB_PARAM_RADIO_MODE == DB_WIFI_MODE_ESPNOW_AIR ||
        DB_PARAM_RADIO_MODE == DB_BLUETOOTH_MODE) {
        // ToDo: For BLE only the last connected client is considered.
        fmav_radio_status_t payload_r = {.fixed = 0, .txbuf=0,
                .noise = db_esp_signal_quality.gnd_noise_floor,
                .remnoise = db_esp_signal_quality.air_noise_floor,
                .remrssi = db_format_rssi(db_esp_signal_quality.air_rssi, db_esp_signal_quality.air_noise_floor),
                .rssi = db_format_rssi(db_esp_signal_quality.gnd_rssi, db_esp_signal_quality.gnd_noise_floor),
                .rxerrors = db_esp_signal_quality.gnd_rx_packets_lost};
        uint16_t len = fmav_msg_radio_status_encode_to_frame_buf(buff, db_get_mav_sys_id(),
                                                                 db_get_mav_comp_id(), &payload_r,
                                                                 &fmav_status_radio);
        db_send_to_all_radio_clients(buff, len);
    } else if (DB_PARAM_RADIO_MODE == DB_WIFI_MODE_AP && wifi_sta_list.num > 0) {
        // We assume ESP32 is not used in DB_WIFI_MODE_AP on the ground but only on the drone side
        // -> We are in WiFi AP mode and connected to the drone
        // Send each connected client a radio status packet.
        // ToDo: Only the RSSI of the first (Wi-Fi) is considered.
        //  Easier for UDP since we have a nice list with mac addresses to use for mapping.
        //  Harder for TCP -> no MAC addresses available of connected clients
      fmav_radio_status_t payload_r = {.fixed = UINT8_MAX, .noise = UINT8_MAX, .remnoise = UINT8_MAX, .remrssi=UINT8_MAX, .rssi=db_format_rssi(
              wifi_sta_list.sta[0].rssi, -88), .rxerrors=0, .txbuf=0};
        uint16_t len = fmav_msg_radio_status_encode_to_frame_buf(buff, db_get_mav_sys_id(),
                                                                 db_get_mav_comp_id(), &payload_r,
                                                                 &fmav_status_radio);
        db_send_to_all_radio_clients(buff, len);
    } else {
        // In AP LR or ESPNOW GND mode the clients will send the info to the GCS directly, no need for the GND ESP32 to do anything
    }
}

/**
 * Starts a periodic MAVLink radio status sending timer.
 */
void db_timer_start_mavlink_radio_status() {
    static TimerHandle_t xRadioStatusTimerHandle;;
    xRadioStatusTimerHandle = xTimerCreate(
            "MAV_Radiostatus_Timer",
            pdMS_TO_TICKS(DB_TIMER_MAVLINK_RADIOSTATUS_MS),
            pdTRUE,
            (void *) 0,
            db_timer_mavlink_radiostatus_callback
    );

    if (xRadioStatusTimerHandle == NULL) {
        ESP_LOGE(TAG, "Failed to create Radio Status timer.");
    }
    if (xRadioStatusTimerHandle != NULL) {
        ESP_LOGI(TAG, "Starting to send radio status packets.");
        xTimerStart(xRadioStatusTimerHandle, 0);
    }
}

/**
 * Timer callback that periodically updates the status LED state.
 * Uses db_status_led_process() for all mode-specific logic and timeout handling.
 */
void db_timer_status_led_callback(TimerHandle_t pxTimer) {
    db_status_led_process();
}

/**
 * Starts the periodic status LED processing timer.
 */
void db_timer_start_status_led() {
    static TimerHandle_t xStatusLedTimerHandle;
    xStatusLedTimerHandle = xTimerCreate(
            "Status_LED_Timer",
            pdMS_TO_TICKS(DB_TIMER_STATUS_LED_MS),
            pdTRUE,
            (void *) 0,
            db_timer_status_led_callback
    );

    if (xStatusLedTimerHandle == NULL) {
        ESP_LOGE(TAG, "Failed to create status LED timer.");
    }
    if (xStatusLedTimerHandle != NULL) {
        ESP_LOGI(TAG, "Starting status LED timer.");
        xTimerStart(xStatusLedTimerHandle, 0);
    }
}
