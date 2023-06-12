/* LVGL Example project
 *
 * Basic project to test LVGL on ESP32 based projects.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */
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
#define TAG "demo"
#define LV_TICK_PERIOD_MS 1
#define LEFT_BUTTON_PIN  GPIO_NUM_0
#define RIGHT_BUTTON_PIN  GPIO_NUM_35
#define BUTTON_PIN  GPIO_NUM_36


/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_tick_task(void *arg);
static void guiTask(void *pvParameter);
static void show_menu(void);

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

    // Create a screen
    lv_obj_t *screen = lv_scr_act();

    // Create a custom widget to draw the grid
    lv_obj_t *chart = lv_3d_chart_create(screen, NULL);

    lv_3d_chart_add_cursor(chart, 0, 0, 0);
    lv_3d_chart_add_cursor(chart, 0, 100, 0);
    lv_3d_chart_add_cursor(chart, 100, 100, 0);
    lv_3d_chart_add_cursor(chart, 100, 0, 0);

    lv_3d_chart_add_cursor(chart, 0, 0, 100);
    lv_3d_chart_add_cursor(chart, 0, 100, 100);
    lv_3d_chart_add_cursor(chart, 100, 100, 100);
    lv_3d_chart_add_cursor(chart, 100, 0, 100);

    lv_3d_chart_add_cursor(chart, 150, 0, 0);
    lv_3d_chart_add_cursor(chart, 150, 0, 25);
    lv_3d_chart_add_cursor(chart, 150, 0, 50);
    lv_3d_chart_add_cursor(chart, 150, 0, 75);
    lv_3d_chart_add_cursor(chart, 150, 0, 100);
    lv_3d_chart_add_cursor(chart, 150, 0, 125);
    lv_3d_chart_add_cursor(chart, 150, 0, 150);
    lv_3d_chart_add_cursor(chart, 150, 0, 175);
    lv_3d_chart_add_cursor(chart, 150, 0, 200);

    gpio_set_direction(LEFT_BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(RIGHT_BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);


    show_menu();

    while (1)
    {
        /* Delay 1 tick (assumes FreeRTOS tick is 10ms */
        vTaskDelay(pdMS_TO_TICKS(10));

        if (gpio_get_level(BUTTON_PIN) == 0)
        {
        } 
        else if (gpio_get_level(LEFT_BUTTON_PIN) == 0)
        {
        }
        else if (gpio_get_level(RIGHT_BUTTON_PIN) == 0)
        {
        }

        /* Try to take the semaphore, call lvgl related function on success */
        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY))
        {
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


static void plot_handler(lv_obj_t * obj, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) {
        char buf[32];
        lv_roller_get_selected_str(obj, buf, sizeof(buf));
        printf("Selected plot: %s\n", buf);
    }
}

static void subc_handler(lv_obj_t * obj, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) {
        char buf[32];
        lv_roller_get_selected_str(obj, buf, sizeof(buf));
        printf("Selected sub-carrier: %s\n", buf);
    }
}


static void interval_handler(lv_obj_t * obj, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) {
        char buf[32];
        lv_roller_get_selected_str(obj, buf, sizeof(buf));
        printf("Selected update interval: %s\n", buf);
    }
}

static void show_menu(void)
{
    uint16_t width = LV_HOR_RES;
    uint16_t height = LV_VER_RES-MAX_VER;
    lv_obj_t *screen = lv_scr_act();

    // Configure Plot
    lv_obj_t *plot_roller = lv_roller_create(screen, NULL);
    lv_obj_set_size(plot_roller, width, height/4);
    lv_obj_align(plot_roller, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 0, -(2*height/3));
    lv_roller_set_options(plot_roller,
                        "amplitude\n"
                        "phase",
                        LV_ROLLER_MODE_INFINITE);

    lv_roller_set_visible_row_count(plot_roller, 1);
    lv_roller_set_anim_time(plot_roller, 0);
    lv_obj_set_event_cb(plot_roller, plot_handler);

    // Configure OFDM subcarrier
    lv_obj_t *subc_roller = lv_roller_create(screen, NULL);
    lv_obj_set_size(subc_roller, width, (height/4));
    lv_obj_align(subc_roller, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 0, -(height/3));
    lv_roller_set_options(subc_roller,
                        "-21\n"
                        "-7\n"
                        "+7\n"
                        "+21",
                        LV_ROLLER_MODE_INFINITE);

    lv_roller_set_visible_row_count(subc_roller, 1);
    lv_roller_set_anim_time(subc_roller, 0);
    lv_obj_set_event_cb(subc_roller, subc_handler);

    // Configure update interval
    lv_obj_t *interval_roller = lv_roller_create(screen, NULL);
    lv_obj_set_size(interval_roller, width, (height/4));
    lv_obj_align(interval_roller, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
    lv_roller_set_options(interval_roller,
                        "10\n"
                        "20\n"
                        "30\n"
                        "40",
                        LV_ROLLER_MODE_INFINITE);

    lv_roller_set_visible_row_count(interval_roller, 1);
    lv_roller_set_anim_time(interval_roller, 0);
    lv_obj_set_event_cb(interval_roller, interval_handler);
}

