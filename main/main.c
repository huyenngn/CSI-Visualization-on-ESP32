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

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_tick_task(void *arg);
static void guiTask(void *pvParameter);
static void lv_ex_list_1(void);
static void draw_grid(lv_obj_t *obj, const lv_area_t *clip_area);

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
    lv_obj_t *grid_widget = lv_obj_create(screen, NULL);
    lv_obj_set_size(grid_widget, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(grid_widget, 0, 0);
    grid_widget->design_cb = draw_grid;

    lv_ex_list_1();

    while (1)
    {
        /* Delay 1 tick (assumes FreeRTOS tick is 10ms */
        vTaskDelay(pdMS_TO_TICKS(10));

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
static void event_handler(lv_obj_t * obj, lv_event_t event)
{
    if(event == LV_EVENT_CLICKED) {
        printf("Clicked: %s\n", lv_list_get_btn_text(obj));
    }
}

static void lv_ex_list_1(void)
{
    /*Create a list*/
    lv_obj_t * list1 = lv_list_create(lv_scr_act(), NULL);
    lv_obj_set_size(list1, 100, LV_VER_RES-10);
    lv_obj_align(list1, NULL, LV_ALIGN_IN_LEFT_MID, 5, 0);

    /*Add buttons to the list*/
    lv_obj_t * list_btn;

    list_btn = lv_list_add_btn(list1, LV_SYMBOL_CLOSE, "Close");
    lv_obj_set_event_cb(list_btn, event_handler);

    list_btn = lv_list_add_btn(list1, LV_SYMBOL_SETTINGS, "Settings");
    lv_obj_set_event_cb(list_btn, event_handler);

    list_btn = lv_list_add_btn(list1, LV_SYMBOL_REFRESH, "Refresh");
    lv_obj_set_event_cb(list_btn, event_handler);

    lv_obj_set_hidden(list1, true);
}

// (255,0,0) -> (255,255,0) -> (0,255,0) -> (0,255,255) -> 
// (0,0,255) -> (255,0,255) -> (255,0,0)
typedef struct {
    lv_coord_t x;
    lv_coord_t y;
    lv_color_t color;
} lv_3d_chart_point_t;

typedef struct {
    /*No inherited ext*/ /*Ext. of ancestor*/
    /*New data for this type */
    lv_ll_t points_ll;
} lv_3d_chart_ext_t;

lv_obj_t * lv_3d_chart_create(lv_obj_t * par, const lv_obj_t * copy) {
    // Create a custom widget to draw the grid
    lv_obj_t *chart = lv_obj_create(par, copy);
    LV_ASSERT_MEM(chart);
    if(chart == NULL) return NULL;

    /*Allocate the object type specific extended data*/
    lv_3d_chart_ext_t * ext = lv_obj_allocate_ext_attr(chart, sizeof(lv_3d_chart_ext_t));
    LV_ASSERT_MEM(ext);
    if(ext == NULL) {
        lv_obj_del(chart);
        return NULL;
    }

    _lv_ll_init(&ext->points_ll, sizeof(lv_3d_chart_point_t));

    chart->design_cb = lv_3d_chart_design;

    LV_LOG_INFO("chart created");

    return chart;
}

void lv_3d_chart_set_next(lv_obj_t * chart, lv_coord_t x, lv_coord_t z) {
    LV_ASSERT_OBJ(chart, LV_OBJX_NAME);

    lv_3d_chart_ext_t * ext    = lv_obj_get_ext_attr(chart);
    lv_3d_chart_point_t * point = _lv_ll_ins_head(&ext->points_ll);
    LV_ASSERT_MEM(point);
    if(point == NULL) return NULL;

    point->x = x;
    point->y = z;
    point->color = LV_COLOR_MAKE(255,0,0);
}

static lv_design_res_t lv_3d_chart_design(lv_obj_t * chart, const lv_area_t * clip_area) {
    draw_grid(chart, clip_area);
    draw_points(chart, clip_area);

    return LV_DESIGN_RES_OK;
}

static void draw_points(lv_obj_t * chart, const lv_area_t *clip_area) {
    lv_3d_chart_ext_t * ext = lv_obj_get_ext_attr(chart);
    if(_lv_ll_is_empty(&ext->points_ll)) return;

    lv_point_t p1;
    lv_point_t p2;
    lv_3d_chart_point_t * point;

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    lv_obj_init_draw_line_dsc(chart, LV_CHART_PART_CURSOR, &line_dsc);

    lv_draw_rect_dsc_t point_dsc;
    lv_draw_rect_dsc_init(&point_dsc);
    point_dsc.bg_opa = line_dsc.opa;
    point_dsc.radius = LV_RADIUS_CIRCLE;

    lv_coord_t point_radius = lv_obj_get_style_size(chart, LV_CHART_PART_CURSOR);

    /*Do not bother with line ending is the point will over it*/
    if(point_radius > line_dsc.width / 2) line_dsc.raw_end = 1;

    /*Go through all cursor lines*/
    _LV_LL_READ_BACK(ext->points_ll, point) {
        line_dsc.color = point->color;
        point_dsc.bg_color = point->color;

        if(point_radius) {
            lv_area_t point_area;

            point_area.x1 = clip_area->x1 + point->x - point_radius;
            point_area.x2 = clip_area->x1 + point->x + point_radius;

            point_area.y1 = clip_area->y1 + point->y - point_radius;
            point_area.y2 = clip_area->y1 + point->y + point_radius;

            /*Don't limit to `series_mask` to get full circles on the ends*/
            lv_draw_rect(&point_area, clip_area, &point_dsc);
        }

    }
}

static void draw_grid(lv_obj_t *obj, const lv_area_t *clip_area)
{
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    lv_obj_init_draw_line_dsc(obj, LV_CHART_PART_BG, &line_dsc);

    double mid_y = LV_VER_RES / 3;
    double mid_x = LV_HOR_RES / 2;
    double left_x = 1.73 * (mid_y);
    double m = mid_y / left_x;

    double x, y, n;

    for (x = 0; x < mid_x + 20; x += 10)
    {
        lv_point_t p1;
        lv_point_t p2;
        p1.x = x + mid_x;
        p1.y = 0;
        p2.x = x + mid_x;
        p2.y = m * x + mid_y;
        lv_draw_line(&p1, &p2, clip_area, &line_dsc);
        p2.x = x + mid_x;
        p2.y = m * x + mid_y;
        p1.y = LV_HOR_RES;
        n = p2.y + (m * p2.x);
        p1.x = (p1.y - n) / -m;
        lv_draw_line(&p1, &p2, clip_area, &line_dsc);
        p1.x = mid_x - x;
        p1.y = 0;
        p2.x = mid_x - x;
        p2.y = m * x + mid_y;
        lv_draw_line(&p1, &p2, clip_area, &line_dsc);
        p2.x = mid_x - x;
        p2.y = m * x + mid_y;
        p1.y = LV_HOR_RES;
        n = p2.y - (m * p2.x);
        p1.x = (p1.y - n) / m;
        lv_draw_line(&p1, &p2, clip_area, &line_dsc);
    }

    // Draw horizontal lines
    for (y = 5; y < LV_VER_RES - 10; y += 10)
    {
        lv_point_t p1;
        lv_point_t p2;
        p1.x = 0;
        p1.y = y;
        p2.x = mid_x;
        p2.y = -m * mid_x + y;
        lv_draw_line(&p1, &p2, clip_area, &line_dsc);
        p1.x = mid_x;
        p1.y = -m * mid_x + y;
        p2.x = LV_HOR_RES;
        p2.y = y;
        lv_draw_line(&p1, &p2, clip_area, &line_dsc);
    }
}

static void lv_tick_task(void *arg)
{
    (void)arg;

    lv_tick_inc(LV_TICK_PERIOD_MS);
}
