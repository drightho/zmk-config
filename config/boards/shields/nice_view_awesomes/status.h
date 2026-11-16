#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>
#include "util.h"

struct zmk_widget_status {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_color_t cbuf[CANVAS_SIZE * CANVAS_SIZE];  /* battery / link, top of the screen */
    lv_color_t cbuf4[CANVAS_SIZE * CANVAS_SIZE]; /* Caps Lock, bottom of the screen */
    lv_color_t cbuf2[NAME_CANVAS_W * NAME_CANVAS_H]; /* "The Awesomes!" as shown (rotated) */
    lv_color_t cbuf3[NAME_W * NAME_H];           /* "The Awesomes!" drawn upright, hidden */
    struct status_state state;
};

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget);
