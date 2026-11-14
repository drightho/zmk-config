/*
 * Right-half (peripheral) Nice!View screen. Layout is drawn upright into two
 * 68x68 canvases and rotated 90 degrees, the same way ZMK's nice_view widgets do:
 *   canvas 1 (top):    battery, link status, CAPS and NUM indicators
 *   canvas 2 (middle): "The" / "Awesomes!" in (faux) bold
 *
 * Based on ZMK's nice_view peripheral_status.c (Copyright (c) 2023 The ZMK Contributors, MIT).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <dt-bindings/zmk/hid_usage.h>
#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#include "status.h"

/* HID LED report: bit (usage - 1). Num Lock = bit 0, Caps Lock = bit 1. */
#define LED_NUM_LOCK_MASK BIT(HID_USAGE_LED_NUM_LOCK - 1)
#define LED_CAPS_LOCK_MASK BIT(HID_USAGE_LED_CAPS_LOCK - 1)

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/* ---- drawing ------------------------------------------------------------------------------ */

/* A 68x20 row: label on the left, ON/OFF on the right. Inverted (filled) when ON. */
static void draw_indicator(lv_obj_t *canvas, int y, const char *name, bool on) {
    lv_draw_rect_dsc_t rect_fg, rect_bg;
    init_rect_dsc(&rect_fg, LVGL_FOREGROUND);
    init_rect_dsc(&rect_bg, LVGL_BACKGROUND);

    lv_draw_label_dsc_t left_dsc, right_dsc;
    lv_color_t text_color = on ? LVGL_BACKGROUND : LVGL_FOREGROUND;
    init_label_dsc(&left_dsc, text_color, &lv_font_montserrat_12, LV_TEXT_ALIGN_LEFT);
    init_label_dsc(&right_dsc, text_color, &lv_font_montserrat_12, LV_TEXT_ALIGN_RIGHT);

    lv_canvas_draw_rect(canvas, 0, y, CANVAS_SIZE, 20, &rect_fg);
    if (!on) {
        lv_canvas_draw_rect(canvas, 1, y + 1, CANVAS_SIZE - 2, 18, &rect_bg);
    }
    lv_canvas_draw_text(canvas, 4, y + 3, CANVAS_SIZE - 8, &left_dsc, name);
    lv_canvas_draw_text(canvas, 4, y + 3, CANVAS_SIZE - 8, &right_dsc, on ? "ON" : "OFF");
}

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    draw_battery(canvas, state);
    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc,
                        state->connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    draw_indicator(canvas, 22, "CAPS", state->caps_lock);
    draw_indicator(canvas, 46, "NUM", state->num_lock);

    rotate_canvas(canvas, cbuf);
}

/* LVGL's built-in fonts have no bold face, so fake it: draw the text twice, 1px apart. */
static void draw_bold_line(lv_obj_t *canvas, int y, const lv_font_t *font, const char *text) {
    lv_draw_label_dsc_t dsc;
    init_label_dsc(&dsc, LVGL_FOREGROUND, font, LV_TEXT_ALIGN_CENTER);
    lv_canvas_draw_text(canvas, 0, y, CANVAS_SIZE - 1, &dsc, text);
    lv_canvas_draw_text(canvas, 1, y, CANVAS_SIZE - 1, &dsc, text);
}

static void draw_name(lv_obj_t *widget, lv_color_t cbuf[]) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 1);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    draw_bold_line(canvas, 16, &lv_font_montserrat_10, "The");
    draw_bold_line(canvas, 30, &lv_font_montserrat_12, "Awesomes!");

    rotate_canvas(canvas, cbuf);
}

/* ---- battery ------------------------------------------------------------------------------ */

struct battery_status_state {
    uint8_t level;
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    bool usb_present;
#endif
};

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif
    widget->state.battery = state.level;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif

/* ---- link to the central (left half) ------------------------------------------------------ */

struct connection_state {
    bool connected;
};

static struct connection_state connection_get_state(const zmk_event_t *_eh) {
    return (struct connection_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void connection_update_cb(struct connection_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.connected = state.connected;
        draw_top(widget->obj, widget->cbuf, &widget->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_connection_status, struct connection_state,
                            connection_update_cb, connection_get_state)
ZMK_SUBSCRIPTION(widget_connection_status, zmk_split_peripheral_status_changed);

/* ---- Caps / Num Lock (HID indicators forwarded by the central) ---------------------------- */

struct indicator_state {
    bool caps_lock;
    bool num_lock;
};

/* The peripheral only learns the indicators through the changed event, so remember the last one. */
static zmk_hid_indicators_t last_indicators;

static struct indicator_state indicator_get_state(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ev = eh ? as_zmk_hid_indicators_changed(eh) : NULL;
    if (ev) {
        last_indicators = ev->indicators;
    }
    return (struct indicator_state){
        .caps_lock = last_indicators & LED_CAPS_LOCK_MASK,
        .num_lock = last_indicators & LED_NUM_LOCK_MASK,
    };
}

static void indicator_update_cb(struct indicator_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.caps_lock = state.caps_lock;
        widget->state.num_lock = state.num_lock;
        draw_top(widget->obj, widget->cbuf, &widget->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_indicator_status, struct indicator_state, indicator_update_cb,
                            indicator_get_state)
ZMK_SUBSCRIPTION(widget_indicator_status, zmk_hid_indicators_changed);

/* ---- init --------------------------------------------------------------------------------- */

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);

    /* Child order matters: draw_top() uses child 0, draw_name() uses child 1. */
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    lv_obj_t *name = lv_canvas_create(widget->obj);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 24, 0);
    lv_canvas_set_buffer(name, widget->cbuf2, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    sys_slist_append(&widgets, &widget->node);

    draw_name(widget->obj, widget->cbuf2);

    widget_battery_status_init();
    widget_connection_status_init();
    widget_indicator_status_init();

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
