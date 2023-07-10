#ifndef ESP32_CSI_CSI_COMPONENT_H
#define ESP32_CSI_CSI_COMPONENT_H

#include "time_component.h"
#include "math.h"
#include <sstream>
#include <iostream>

#define QUEUE_LEN 1
#define MAC_AP "7C:9E:BD:65:B2:3D"
#define USE_MAC_FILTER true

char *project_type;
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

const QueueHandle_t data_queue = xQueueCreate(QUEUE_LEN, sizeof(wifi_csi_info_t));  // Queue erstellen

void _wifi_csi_cb(void *ctx, wifi_csi_info_t *data)
{  // wird jedes Mal beim Erhalt eines CSI Paketes aufgerufen
    xSemaphoreTake(mutex, portMAX_DELAY);

    char mac_sta[20];
    sprintf(mac_sta, "%02X:%02X:%02X:%02X:%02X:%02X", data[0].mac[0], data[0].mac[1], data[0].mac[2], data[0].mac[3], data[0].mac[4], data[0].mac[5]);

    int mac_filter = 0;
    if (USE_MAC_FILTER) {
        mac_filter = strcmp(mac_sta, MAC_AP);
    }

    if (uxQueueSpacesAvailable(data_queue) > 0 && mac_filter == 0) {  // wennn platz platz in Queue
        wifi_csi_info_t *csi = (wifi_csi_info_t *)malloc(sizeof(wifi_csi_info_t));

        if (csi != NULL || csi->buf != NULL) {  // speicher konnte alloziiert werden

            // kopieren der daten in den zu beginn alloziierten speicher
            csi->buf = data->buf;
            memcpy(csi, data, sizeof(wifi_csi_info_t));

            // Pointer auf daten in Queuelegen
            if (xQueueSend(data_queue, &csi, portMAX_DELAY) == pdPASS) {
                printf("CSI data was placed into queue.. \n");
            }
            else {
                free(csi);
            }
        }
        else {
            free(csi);
        }
    }
    xSemaphoreGive(mutex);
}

void _print_csi_csv_header()
{
    char *header_str = (char *)"type,role,mac,rssi,rate,sig_mode,mcs,bandwidth,smoothing,not_sounding,aggregation,stbc,fec_coding,sgi,noise_floor,ampdu_cnt,channel,secondary_channel,local_timestamp,ant,sig_len,rx_state,real_time_set,real_timestamp,len,CSI_DATA\n";
    outprintf(header_str);
}

void csi_init(char *type)
{
    project_type = type;

#ifdef CONFIG_SHOULD_COLLECT_CSI
    ESP_ERROR_CHECK(esp_wifi_set_csi(1));

    // @See: https://github.com/espressif/esp-idf/blob/master/components/esp_wifi/include/esp_wifi_types.h#L401
    wifi_csi_config_t configuration_csi;
    configuration_csi.lltf_en = 1;
    configuration_csi.htltf_en = 1;
    configuration_csi.stbc_htltf2_en = 1;
    configuration_csi.ltf_merge_en = 1;
    configuration_csi.channel_filter_en = 0;
    configuration_csi.manu_scale = 0;

    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&configuration_csi));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(&_wifi_csi_cb, NULL));

    _print_csi_csv_header();
#endif
}

#endif  // ESP32_CSI_CSI_COMPONENT_H
