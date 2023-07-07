#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

/* Littlevgl specific */
#include "lvgl/lvgl.h"
#include "lvgl_helpers.h"

/* CSI-Tool specific */
#include "csi_tool/csi_tool.h"

/*********************
 *      DEFINES
 *********************/
#define LV_TICK_PERIOD_MS 1
#define LEFT_BUTTON_PIN GPIO_NUM_0
#define RIGHT_BUTTON_PIN GPIO_NUM_35
#define MAX_TABS 2

/*
 * The examples use WiFi configuration that you can set via 'idf.py menuconfig'.
 *
 * If you'd rather not, just change the below entries to strings with
 * the config you want - ie #define ESP_WIFI_SSID "mywifissid"
 */
#define ESP_WIFI_SSID "csicsicsi"    // CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASS "csipassword"  // CONFIG_ESP_WIFI_PASSWORD

#ifdef CONFIG_WIFI_CHANNEL
#define WIFI_CHANNEL CONFIG_WIFI_CHANNEL
#else
#define WIFI_CHANNEL 6
#endif

#ifdef CONFIG_SHOULD_COLLECT_CSI
#define SHOULD_COLLECT_CSI 1
#else
#define SHOULD_COLLECT_CSI 0
#endif

#ifdef CONFIG_SHOULD_COLLECT_ONLY_LLTF
#define SHOULD_COLLECT_ONLY_LLTF 1
#else
#define SHOULD_COLLECT_ONLY_LLTF 0
#endif

#ifdef CONFIG_SEND_CSI_TO_SERIAL
#define SEND_CSI_TO_SERIAL 1
#else
#define SEND_CSI_TO_SERIAL 0
#endif

#ifdef CONFIG_SEND_CSI_TO_SD
#define SEND_CSI_TO_SD 1
#else
#define SEND_CSI_TO_SD 0
#endif

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_tick_task(void *arg);
static void show_menu(lv_obj_t *screen);
static bool keyboard_read(lv_indev_drv_t *drv, lv_indev_data_t *data);

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t *chart;
static bool switch_tab, plot_type;
static int16_t update_interval, current_tab;
static lv_obj_t *tabview;
static lv_group_t *g;
static lv_obj_t *plot_label, *interval_label;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

static const char *TAG = "Active CSI collection (Station)";

/* The event group allows multiple bits for each event, but we only care about one event
 * - are we connected to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;

/* Creates a semaphore to handle concurrent call to lvgl stuff
 * If you wish to call *any* lvgl function from other threads/tasks
 * you should lock on the very same semaphore! */
SemaphoreHandle_t xGuiSemaphore;

/**********************
 *    VISUALIZE CSI
 **********************/

extern "C" void guiTask(void *pvParameter) {
    (void)pvParameter;
    xGuiSemaphore = xSemaphoreCreateMutex();

    lv_init();

    /* Initialize SPI or I2C bus used by the drivers */
    lvgl_driver_init();

    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1 != NULL);

    /* Use double buffered when not working with monochrome displays */
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf2 != NULL);
#else
    static lv_color_t *buf2 = NULL;
#endif

    static lv_disp_buf_t disp_buf;

    uint32_t size_in_px = DISP_BUF_SIZE;

#if defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_IL3820 || defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_JD79653A || defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_UC8151D || defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_SSD1306

    /* Actual size in pixels, not bytes. */
    size_in_px *= 8;
#endif

    /* Initialize the working buffer depending on the selected display.
     * NOTE: buf2 == NULL when using monochrome displays. */
    lv_disp_buf_init(&disp_buf, buf1, buf2, size_in_px);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = disp_driver_flush;

#if defined CONFIG_DISPLAY_ORIENTATION_PORTRAIT || defined CONFIG_DISPLAY_ORIENTATION_PORTRAIT_INVERTED
    disp_drv.rotated = 1;
#endif

    /* When using a monochrome display we need to register the callbacks:
     * - rounder_cb
     * - set_px_cb */
#ifdef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    disp_drv.rounder_cb = disp_driver_rounder;
    disp_drv.set_px_cb = disp_driver_set_px;
#endif

    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    /* Register an input device when enabled on the menuconfig */
#if CONFIG_LV_TOUCH_CONTROLLER != TOUCH_CONTROLLER_NONE
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.read_cb = touch_driver_read;
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    lv_indev_drv_register(&indev_drv);
#endif

    /* Create and start a periodic timer interrupt to call lv_tick_inc */
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "periodic_gui"};
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));

    /* Initialize buttons */
    gpio_set_direction(LEFT_BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(RIGHT_BUTTON_PIN, GPIO_MODE_INPUT);

    /* Render UI */
    lv_obj_t *screen = lv_scr_act();
    show_menu(screen);

    chart = lv_3d_chart_create(screen, NULL);

    // lv_3d_chart_add_cursor(chart, 0, 0, 0);

    wifi_csi_info_t *d;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));

        /* Try to take the semaphore, call lvgl related function on success */
        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY)) {
            lv_task_handler();
            xSemaphoreGive(xGuiSemaphore);

            /* Get raw data from queue */
            if (xQueueReceive(data_queue, &d, pdMS_TO_TICKS(update_interval)) == pdTRUE) {
                uint16_t csi_len = (d->len) / 2;
                int8_t *csi_data = d->buf;

                lv_coord_t subc[csi_len];
                lv_coord_t ret[csi_len] = {0};

                printf("******************************\n");

                /* Caculate CSI from raw data. First and last 3 are null subcarrier. */
                int16_t i = 3;
                while (i < csi_len - 3) {
                    subc[i] = i;

                    if (plot_type) {
                        /* Calculate amplitude */
                        ret[i] = sqrt(pow(csi_data[i * 2], 2) + pow(csi_data[(i * 2) + 1], 2));
                    } else {
                        /* Calculate phase */
                        ret[i] = 200 * (atan2(csi_data[i * 2], csi_data[(i * 2) + 1]) + 3.2) / 6;
                        printf("%d, ", ret[i]);
                    }

                    i++;
                }

                printf("\n");

                /* Plot CSI */
                lv_3d_chart_set_points(chart, lv_3d_chart_add_series(chart), (lv_coord_t *)&subc, (lv_coord_t *)&ret, csi_len);

                // free(d->buf;);
                free(d);
            }
        }
    }
    /* A task should NEVER return */
    free(buf1);
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    free(buf2);
#endif
    vTaskDelete(NULL);
}

static void lv_tick_task(void *arg) {
    (void)arg;

    lv_tick_inc(LV_TICK_PERIOD_MS);
}

static void plot_handler(lv_obj_t *obj, lv_event_t event) {
    if (event == LV_EVENT_VALUE_CHANGED) {
        static char buf[20];
        int16_t val = lv_slider_get_value(obj);
        if (val == 0) {
            plot_type = true;
            snprintf(buf, 20, "amplitude");
        } else {
            plot_type = false;
            snprintf(buf, 20, "phase");
        }

        lv_label_set_text(plot_label, buf);
    }
}

static void interval_handler(lv_obj_t *obj, lv_event_t event) {
    if (event == LV_EVENT_VALUE_CHANGED) {
        static char buf[10];
        int16_t val = lv_slider_get_value(obj)*10;
        snprintf(buf, 10, "%u Hz", val);
        lv_label_set_text(interval_label, buf);

        update_interval = 1000 / val;
    }
}

static bool keyboard_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_PR;

    if (gpio_get_level(LEFT_BUTTON_PIN) && gpio_get_level(RIGHT_BUTTON_PIN)) {
        switch_tab = true;
        data->state = LV_INDEV_STATE_REL;
    } else if (!gpio_get_level(LEFT_BUTTON_PIN) && !gpio_get_level(RIGHT_BUTTON_PIN)) {
        if (switch_tab) {
            current_tab = (current_tab == MAX_TABS - 1) ? 0 : current_tab + 1;
            lv_tabview_set_tab_act(tabview, current_tab, LV_ANIM_ON);
            lv_group_focus_next(g);
            switch_tab = false;
        }
    } else if (!gpio_get_level(LEFT_BUTTON_PIN)) {
        data->key = LV_KEY_LEFT;
    } else if (!gpio_get_level(RIGHT_BUTTON_PIN)) {
        data->key = LV_KEY_RIGHT;
    }

    return false; /*No buffering now so no more data read*/
}

static void show_menu(lv_obj_t *screen) {
    lv_coord_t width = LV_HOR_RES;
    lv_coord_t height = LV_VER_RES - MAX_VER;

    /* Input device drivers */
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = keyboard_read;
    lv_indev_t *indev = lv_indev_drv_register(&indev_drv);
    g = lv_group_create();
    lv_indev_set_group(indev, g);

    tabview = lv_tabview_create(screen, NULL);
    lv_obj_set_size(tabview, width, height);
    lv_obj_align(tabview, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
    lv_tabview_set_btns_pos(tabview, LV_TABVIEW_TAB_POS_NONE);
    current_tab = 0;

    lv_obj_t *tab1 = lv_tabview_add_tab(tabview, "Tab 1");
    lv_page_set_scrlbar_mode(tab1, LV_SCRLBAR_MODE_OFF);
    lv_obj_t *tab2 = lv_tabview_add_tab(tabview, "Tab 2");
    lv_page_set_scrlbar_mode(tab2, LV_SCRLBAR_MODE_OFF);

    /* Configure Plot */
    plot_type = true;
    lv_obj_t *plot_slider = lv_slider_create(tab1, NULL);
    lv_obj_set_width(plot_slider, width - 10);
    lv_obj_align(plot_slider, NULL, LV_ALIGN_IN_LEFT_MID, 5, 0);

    lv_slider_set_range(plot_slider, 0, 1);
    lv_obj_set_event_cb(plot_slider, plot_handler);
    lv_group_add_obj(g, plot_slider);

    plot_label = lv_label_create(tab1, NULL);
    lv_obj_set_auto_realign(plot_label, true);

    lv_obj_align(plot_label, plot_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_label_set_text(plot_label, "amplitude");

    lv_obj_t *plot_info = lv_label_create(tab1, NULL);
    lv_label_set_long_mode(plot_info, LV_LABEL_LONG_SROLL_CIRC);
    lv_obj_set_width(plot_info, width - 10);
    lv_obj_align(plot_info, NULL, LV_ALIGN_IN_TOP_LEFT, 5, 10);

    lv_label_set_text(plot_info, "Choose plot");

    /* Configure update interval */
    lv_obj_t *interval_slider = lv_slider_create(tab2, plot_slider);
    lv_slider_set_range(interval_slider, 1, 10);
    lv_obj_set_event_cb(interval_slider, interval_handler);
    lv_slider_set_value(interval_slider, 10, LV_ANIM_OFF);
    update_interval = 100;
    lv_group_add_obj(g, interval_slider);

    interval_label = lv_label_create(tab2, plot_label);
    lv_obj_align(interval_label, interval_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_label_set_text(interval_label, "0 Hz");

    lv_obj_t *interval_info = lv_label_create(tab2, plot_info);
    lv_label_set_text(interval_info, "Configure update interval");
}

/**********************
 *    COLLECT CSI
 **********************/

esp_err_t _http_event_handle(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (!real_time_set) {
                    char *data = (char *)malloc(evt->data_len + 1);
                    strncpy(data, (char *)evt->data, evt->data_len);
                    data[evt->data_len + 1] = '\0';
                    time_set(data);
                    free(data);
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

//// en_sys_seq: see https://github.com/espressif/esp-idf/blob/master/docs/api-guides/wifi.rst#wi-fi-80211-packet-send for details
esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq);

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Retry connecting to the AP");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool is_wifi_connected() {
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT);
}

void station_init() {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_sta_config_t wifi_sta_config = {};
    wifi_sta_config.channel = WIFI_CHANNEL;
    wifi_config_t wifi_config = {
        .sta = wifi_sta_config,
    };

    strlcpy((char *)wifi_config.sta.ssid, ESP_WIFI_SSID, sizeof(ESP_WIFI_SSID));
    strlcpy((char *)wifi_config.sta.password, ESP_WIFI_PASS, sizeof(ESP_WIFI_PASS));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s", ESP_WIFI_SSID, ESP_WIFI_PASS);
}

void vTask_socket_transmitter_sta_loop(void *pvParameters) {
    for (;;) {
        socket_transmitter_sta_loop(&is_wifi_connected);
    }
}

void config_print() {
    printf("\n\n\n\n\n\n\n\n");
    printf("-----------------------\n");
    printf("ESP32 CSI Tool Settings\n");
    printf("-----------------------\n");
    printf("PROJECT_NAME: %s\n", "ACTIVE_STA");
    printf("CONFIG_ESPTOOLPY_MONITOR_BAUD: %d\n", CONFIG_ESPTOOLPY_MONITOR_BAUD);
    printf("CONFIG_ESP_CONSOLE_UART_BAUDRATE: %d\n", CONFIG_ESP_CONSOLE_UART_BAUDRATE);
    printf("IDF_VER: %s\n", IDF_VER);
    printf("-----------------------\n");
    printf("WIFI_CHANNEL: %d\n", WIFI_CHANNEL);
    printf("ESP_WIFI_SSID: %s\n", ESP_WIFI_SSID);
    printf("ESP_WIFI_PASSWORD: %s\n", ESP_WIFI_PASS);
    printf("PACKET_RATE: %i\n", CONFIG_PACKET_RATE);
    printf("SHOULD_COLLECT_CSI: %d\n", SHOULD_COLLECT_CSI);
    printf("SHOULD_COLLECT_ONLY_LLTF: %d\n", SHOULD_COLLECT_ONLY_LLTF);
    printf("SEND_CSI_TO_SERIAL: %d\n", SEND_CSI_TO_SERIAL);
    printf("SEND_CSI_TO_SD: %d\n", SEND_CSI_TO_SD);
    printf("-----------------------\n");
    printf("\n\n\n\n\n\n\n\n");
}

/**********************
 *   APPLICATION MAIN
 **********************/
extern "C" void app_main() {
    config_print();
    nvs_init();
    sd_init();
    station_init();
    csi_init((char *)"STA");

#if !(SHOULD_COLLECT_CSI)
    printf("CSI will not be collected. Check `idf.py menuconfig  # > ESP32 CSI Tool Config` to enable CSI");
#endif

    TaskHandle_t xHandle = NULL;

    xTaskCreatePinnedToCore(&vTask_socket_transmitter_sta_loop, "socket_transmitter_sta_loop",
                            10000, (void *)&is_wifi_connected, 100, &xHandle, 0);

    xTaskCreatePinnedToCore(guiTask, "gui", 20000, NULL, 100, NULL, 1);
}
