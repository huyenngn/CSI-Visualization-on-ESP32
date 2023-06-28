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

/* Littlevgl specific */
#include "lvgl/lvgl.h"
#include "lvgl_helpers.h"

/*********************
 *      DEFINES
 *********************/
#define LV_TICK_PERIOD_MS 1
#define LEFT_BUTTON_PIN GPIO_NUM_0
#define RIGHT_BUTTON_PIN GPIO_NUM_35

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_tick_task(void *arg);
static void guiTask(void *pvParameter);
static void show_menu(lv_obj_t *screen);
static bool keyboard_read(lv_indev_drv_t *drv, lv_indev_data_t *data);

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t *amp_chart, *phase_chart;
static bool switch_tab;
static uint8_t current_tab = 0;
static lv_obj_t *tabview;
static lv_group_t *g;
static lv_obj_t *plot_label, *subc_label, *interval_label;

/**********************
 *   APPLICATION MAIN
 **********************/
void app_main()
{
    /* If you want to use a task to create the graphic, you NEED to create a Pinned task
     * Otherwise there can be problem such as memory corruption and so on.
     * NOTE: When not using Wi-Fi nor Bluetooth you can pin the guiTask to core 0 */
    xTaskCreatePinnedToCore(guiTask, "gui", 4096 * 2, NULL, 0, NULL, 1);
}

/* Creates a semaphore to handle concurrent call to lvgl stuff
 * If you wish to call *any* lvgl function from other threads/tasks
 * you should lock on the very same semaphore! */
SemaphoreHandle_t xGuiSemaphore;

static void guiTask(void *pvParameter)
{
    (void)pvParameter;
    xGuiSemaphore = xSemaphoreCreateMutex();

    lv_init();

    /* Initialize SPI or I2C bus used by the drivers */
    lvgl_driver_init();

    lv_color_t *buf1 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1 != NULL);

    /* Use double buffered when not working with monochrome displays */
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    lv_color_t *buf2 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
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

    // Create a screen
    lv_obj_t *screen = lv_scr_act();

    // Render UI
    show_menu(screen);

    // Amp chart
    amp_chart = lv_3d_chart_create(screen, NULL);

    lv_3d_chart_add_cursor(amp_chart, 0, 0, 0);
    lv_3d_chart_add_cursor(amp_chart, 0, 100, 0);
    lv_3d_chart_add_cursor(amp_chart, 100, 100, 0);
    lv_3d_chart_add_cursor(amp_chart, 100, 0, 0);

    lv_3d_chart_add_cursor(amp_chart, 0, 0, 100);
    lv_3d_chart_add_cursor(amp_chart, 0, 100, 100);
    lv_3d_chart_add_cursor(amp_chart, 100, 100, 100);
    lv_3d_chart_add_cursor(amp_chart, 100, 0, 100);
    lv_3d_chart_add_cursor(amp_chart, 150, 0, 0);
    lv_3d_chart_add_cursor(amp_chart, 150, 0, 25);
    lv_3d_chart_add_cursor(amp_chart, 150, 0, 50);
    lv_3d_chart_add_cursor(amp_chart, 150, 0, 75);
    lv_3d_chart_add_cursor(amp_chart, 150, 0, 100);
    lv_3d_chart_add_cursor(amp_chart, 150, 0, 125);
    lv_3d_chart_add_cursor(amp_chart, 150, 0, 150);
    lv_3d_chart_add_cursor(amp_chart, 150, 0, 175);
    lv_3d_chart_add_cursor(amp_chart, 150, 0, 200);

    // Phase chart
    phase_chart = lv_3d_chart_create(screen, NULL);

    lv_coord_t y_array[9] = {0, 25, 50, 75, 100, 125, 150, 175, 200};
    lv_coord_t z_array[9] = {170, 150, 130, 140, 170, 150, 130, 140, 150};

    lv_3d_chart_series_t *x1 = lv_3d_chart_add_series(phase_chart);
    lv_3d_chart_set_points(phase_chart, x1, &y_array, &z_array, 9);

    lv_3d_chart_series_t *x2 = lv_3d_chart_add_series(phase_chart);
    lv_3d_chart_set_points(phase_chart, x2, &y_array, &z_array, 9);

    lv_3d_chart_series_t *x3 = lv_3d_chart_add_series(phase_chart);
    lv_3d_chart_set_points(phase_chart, x3, &y_array, &z_array, 9);

    lv_3d_chart_series_t *x4 = lv_3d_chart_add_series(phase_chart);
    lv_3d_chart_set_points(phase_chart, x4, &y_array, &z_array, 9);

    lv_3d_chart_series_t *x5 = lv_3d_chart_add_series(phase_chart);
    lv_3d_chart_set_points(phase_chart, x5, &y_array, &z_array, 9);

    lv_3d_chart_series_t *x6 = lv_3d_chart_add_series(phase_chart);
    lv_3d_chart_set_points(phase_chart, x6, &y_array, &z_array, 9);

    lv_3d_chart_series_t *x7 = lv_3d_chart_add_series(phase_chart);
    lv_3d_chart_set_points(phase_chart, x7, &y_array, &z_array, 9);

    lv_3d_chart_series_t *x8 = lv_3d_chart_add_series(phase_chart);
    lv_3d_chart_set_points(phase_chart, x8, &y_array, &z_array, 9);

    lv_3d_chart_series_t *x9 = lv_3d_chart_add_series(phase_chart);
    lv_3d_chart_set_points(phase_chart, x9, &y_array, &z_array, 9);

    lv_3d_chart_series_t *x10 = lv_3d_chart_add_series(phase_chart);
    lv_3d_chart_set_points(phase_chart, x10, &y_array, &z_array, 9);

    lv_obj_set_hidden(phase_chart, true);

    while (1) {
        /* Delay 1 tick (assumes FreeRTOS tick is 10ms */
        vTaskDelay(pdMS_TO_TICKS(10));

        /* Try to take the semaphore, call lvgl related function on success */
        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY)) {
            lv_task_handler();
            xSemaphoreGive(xGuiSemaphore);
        }
    }

    /* A task should NEVER return */
    free(buf1);
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    free(buf2);
#endif
    vTaskDelete(NULL);
}

static void lv_tick_task(void *arg)
{
    (void)arg;

    lv_tick_inc(LV_TICK_PERIOD_MS);
}

static void plot_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_VALUE_CHANGED) {
        static char buf[20];
        int16_t val = lv_slider_get_value(obj);
        switch (val) {
            case 0:
                snprintf(buf, 20, "amplitude");
                lv_obj_set_hidden(amp_chart, false);
                lv_obj_set_hidden(phase_chart, true);
                break;
            case 1:
                snprintf(buf, 20, "phase");
                lv_obj_set_hidden(amp_chart, true);
                lv_obj_set_hidden(phase_chart, false);
                break;
            default:
                break;
        }

        lv_label_set_text(plot_label, buf);
    }
}

static void subc_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_VALUE_CHANGED) {
        static char buf[11];
        snprintf(buf, 11, "%d kHz", lv_slider_get_value(obj));
        lv_label_set_text(subc_label, buf);
    }
}

static void interval_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_VALUE_CHANGED) {
        static char buf[8];
        snprintf(buf, 8, "%u Hz", lv_slider_get_value(obj));
        lv_label_set_text(interval_label, buf);
    }
}

static bool keyboard_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_PR;

    if (gpio_get_level(LEFT_BUTTON_PIN) && gpio_get_level(RIGHT_BUTTON_PIN)) {
        switch_tab = true;
        data->state = LV_INDEV_STATE_REL;
    }
    else if (!gpio_get_level(LEFT_BUTTON_PIN) && !gpio_get_level(RIGHT_BUTTON_PIN)) {
        if (switch_tab) {
            current_tab = (current_tab == 2) ? 0 : current_tab + 1;
            lv_tabview_set_tab_act(tabview, current_tab, LV_ANIM_ON);
            lv_group_focus_next(g);
            switch_tab = false;
        }
    }
    else if (!gpio_get_level(LEFT_BUTTON_PIN)) {
        data->key = LV_KEY_LEFT;
    }
    else if (!gpio_get_level(RIGHT_BUTTON_PIN)) {
        data->key = LV_KEY_RIGHT;
    }

    return false; /*No buffering now so no more data read*/
}

static void show_menu(lv_obj_t *screen)
{
    lv_coord_t width = LV_HOR_RES;
    lv_coord_t height = LV_VER_RES - MAX_VER;

    // Menu controller
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

    lv_obj_t *tab1 = lv_tabview_add_tab(tabview, "Tab 1");
    lv_page_set_scrlbar_mode(tab1, LV_SCRLBAR_MODE_OFF);
    lv_obj_t *tab2 = lv_tabview_add_tab(tabview, "Tab 2");
    lv_page_set_scrlbar_mode(tab2, LV_SCRLBAR_MODE_OFF);
    lv_obj_t *tab3 = lv_tabview_add_tab(tabview, "Tab 3");
    lv_page_set_scrlbar_mode(tab3, LV_SCRLBAR_MODE_OFF);

    // Configure Plot
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

    // Configure OFDM subcarrier
    lv_obj_t *subc_slider = lv_slider_create(tab2, plot_slider);
    lv_slider_set_type(subc_slider, true);
    lv_slider_set_range(subc_slider, -27, 27);
    lv_obj_set_event_cb(subc_slider, subc_handler);
    lv_group_add_obj(g, subc_slider);

    subc_label = lv_label_create(tab2, plot_label);
    lv_obj_align(subc_label, subc_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_label_set_text(subc_label, "0 kHz");

    lv_obj_t *subc_info = lv_label_create(tab2, plot_info);
    lv_label_set_text(subc_info, "Configure OFDM subcarrier");

    // Configure update interval
    lv_obj_t *interval_slider = lv_slider_create(tab3, plot_slider);
    lv_slider_set_range(interval_slider, 0, 10);
    lv_obj_set_event_cb(interval_slider, interval_handler);
    lv_group_add_obj(g, interval_slider);

    interval_label = lv_label_create(tab3, plot_label);
    lv_obj_align(interval_label, interval_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_label_set_text(interval_label, "0 Hz");

    lv_obj_t *interval_info = lv_label_create(tab3, plot_info);
    lv_label_set_text(interval_info, "Configure update interval");
}