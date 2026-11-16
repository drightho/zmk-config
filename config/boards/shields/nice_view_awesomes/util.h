/*
 * Based on ZMK's nice_view widgets (Copyright (c) 2023 The ZMK Contributors, MIT).
 */

#pragma once

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#define CANVAS_SIZE 68

/* Hidden scratch canvas for the name: wider than the screen, since it is turned 45 degrees. */
#define NAME_W 96
#define NAME_H 40

#define LVGL_BACKGROUND lv_color_white()
#define LVGL_FOREGROUND lv_color_black()

struct status_state {
    uint8_t battery;
    bool charging;
    bool connected;
    bool caps_lock;
    bool num_lock;
};

void rotate_canvas(lv_obj_t *canvas, lv_color_t cbuf[]);
void rotate_canvas_angle(lv_obj_t *canvas, lv_color_t cbuf[], int16_t angle, lv_coord_t offset_x,
                         lv_coord_t offset_y);
void draw_battery(lv_obj_t *canvas, const struct status_state *state);
void init_label_dsc(lv_draw_label_dsc_t *label_dsc, lv_color_t color, const lv_font_t *font,
                    lv_text_align_t align);
void init_rect_dsc(lv_draw_rect_dsc_t *rect_dsc, lv_color_t bg_color);
