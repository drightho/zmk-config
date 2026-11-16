/*
 * Right-half (peripheral) Nice!View screen. Layout is drawn upright into two
 * 68x68 canvases and rotated 90 degrees, the same way ZMK's nice_view widgets do:
 *   canvas 1 (top):    battery and link status
 *   canvas 3 (bottom): CAPS indicator
 *   canvas 2 (middle): "The" / "Awesomes!" in (faux) bold italic, turned 55 degrees clockwise
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

/* HID LED report: bit (usage - 1). Caps Lock = bit 1. */
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

/* Child indexes of the widget, bottom of the z-order first (see zmk_widget_status_init). */
enum { CHILD_INDICATORS, CHILD_BATTERY, CHILD_NAME, CHILD_SCRATCH };

static void draw_battery_link(lv_obj_t *widget, lv_color_t cbuf[],
                              const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, CHILD_BATTERY);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    draw_battery(canvas, state);
    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc,
                        state->connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    rotate_canvas(canvas, cbuf);
}

/* The canvas sits at the bottom of the screen; the CAPS row is the lower part of it. */
static void draw_indicators(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, CHILD_INDICATORS);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    draw_indicator(canvas, 46, "CAPS", state->caps_lock);

    rotate_canvas(canvas, cbuf);
}

static void draw_status(struct zmk_widget_status *widget) {
    draw_battery_link(widget->obj, widget->cbuf, &widget->state);
    draw_indicators(widget->obj, widget->cbuf4, &widget->state);
}

/* LVGL's built-in fonts have no bold face, so fake it: draw the text twice, 1px apart. */
static void draw_bold_line(lv_obj_t *canvas, int x, int y, const lv_font_t *font,
                           const char *text) {
    lv_draw_label_dsc_t dsc;
    init_label_dsc(&dsc, LVGL_FOREGROUND, font, LV_TEXT_ALIGN_CENTER);
    lv_canvas_draw_text(canvas, x, y, NAME_W - 1, &dsc, text);
    lv_canvas_draw_text(canvas, x + 1, y, NAME_W - 1, &dsc, text);
}

/*
 * No italic face either, so fake it: shear the rows y0..y1-1 sideways. The top row moves right
 * the most, one pixel less every ITALIC_STEP rows, and the bottom row stays put. Works on the raw
 * canvas buffer (one lv_color_t per pixel); pixels shifted off the edge are dropped.
 */
#define ITALIC_STEP 4

static void shear_rows(lv_color_t cbuf[], int y0, int y1) {
    for (int y = y0; y < y1; y++) {
        int shift = (y1 - 1 - y) / ITALIC_STEP;
        lv_color_t *row = &cbuf[y * NAME_W];
        for (int x = NAME_W - 1; x >= 0; x--) {
            row[x] = (x - shift >= 0) ? row[x - shift] : LVGL_BACKGROUND;
        }
    }
}

/* Rotation of the name, clockwise as seen by the user: NAME_TURN degrees on top of the 90 that every
 * canvas gets. */
#define NAME_TURN 55
#define NAME_ANGLE_DEG (90 + NAME_TURN)

/* Fonts (Montserrat 12 and 14 come from this shield's Kconfig.defconfig) and the top of each line
 * on the scratch canvas. */
#define NAME_FONT_THE lv_font_montserrat_12
#define NAME_FONT_AWESOMES lv_font_montserrat_16
#define NAME_Y_THE 2
#define NAME_Y_AWESOMES 18

/* Move the rotated name this many pixels to the right as seen on screen; it nearly fills the width. */
#define NAME_SHIFT_X 6

/* A destination pixel is dark when at least this many of its 3x3 sub-samples are. */
#define NAME_DARK_MIN 4

/*
 * Turn the scratch canvas (upright text, NAME_W x NAME_H) clockwise by NAME_ANGLE_DEG into the
 * name canvas, around the centre of both. LVGL's own lv_canvas_transform() is not used: on a
 * 1-bit display its anti-aliased result is speckle, and its non-anti-aliased path dereferences a
 * NULL display outside a refresh. Each destination pixel is mapped back into the scratch canvas
 * (3x3 sub-samples, integer maths only) and made dark if enough of them hit dark pixels.
 * Whatever falls outside the name canvas is trimmed.
 */
static void rotate_name(lv_color_t dst[], const lv_color_t src[]) {
    const int32_t c = lv_trigo_sin(NAME_ANGLE_DEG + 90); /* cos, scaled by 2^15 */
    const int32_t s = lv_trigo_sin(NAME_ANGLE_DEG);      /* sin, scaled by 2^15 */

    for (int dy = 0; dy < NAME_CANVAS_H; dy++) {
        for (int dx = 0; dx < NAME_CANVAS_W; dx++) {
            int dark = 0;
            for (int j = 0; j < 3; j++) {
                for (int i = 0; i < 3; i++) {
                    /* sub-sample position relative to the canvas centre, in sixths of a pixel */
                    int32_t px = 6 * dx + 2 * i + 1 - 6 * (NAME_CANVAS_W / 2);
                    int32_t py = 6 * dy + 2 * j + 1 - 6 * (NAME_CANVAS_H / 2 + NAME_SHIFT_X);
                    /* inverse rotation, then back to the scratch canvas's pixel grid */
                    int32_t sx = ((px * c + py * s) >> 15) + 6 * (NAME_W / 2);
                    int32_t sy = ((py * c - px * s) >> 15) + 6 * (NAME_H / 2);
                    if (sx < 0 || sy < 0 || sx >= 6 * NAME_W || sy >= 6 * NAME_H) {
                        continue;
                    }
                    if (src[(sy / 6) * NAME_W + (sx / 6)].full == LVGL_FOREGROUND.full) {
                        dark++;
                    }
                }
            }
            dst[dy * NAME_CANVAS_W + dx] = (dark >= NAME_DARK_MIN) ? LVGL_FOREGROUND : LVGL_BACKGROUND;
        }
    }
}

/*
 * Draw "The Awesomes!" upright on the hidden scratch canvas (child CHILD_SCRATCH), then turn it
 * into the visible name canvas (child CHILD_NAME), whose buffer is `name_buf`.
 */
static void draw_name(lv_obj_t *widget, lv_color_t name_buf[], lv_color_t scratch_buf[]) {
    lv_obj_t *name = lv_obj_get_child(widget, CHILD_NAME);
    lv_obj_t *scratch = lv_obj_get_child(widget, CHILD_SCRATCH);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_canvas_draw_rect(scratch, 0, 0, NAME_W, NAME_H, &rect_black_dsc);

    draw_bold_line(scratch, 0, NAME_Y_THE, &NAME_FONT_THE, "The");
    /* Start 2px left: the shear below pushes the top of the line right by up to 4px. */
    draw_bold_line(scratch, -2, NAME_Y_AWESOMES, &NAME_FONT_AWESOMES, "Awesomes!");
    shear_rows(scratch_buf, NAME_Y_AWESOMES, NAME_Y_AWESOMES + NAME_FONT_AWESOMES.line_height);

    rotate_name(name_buf, scratch_buf);
    lv_obj_invalidate(name);
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

    draw_status(widget);
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
        draw_status(widget);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_connection_status, struct connection_state,
                            connection_update_cb, connection_get_state)
ZMK_SUBSCRIPTION(widget_connection_status, zmk_split_peripheral_status_changed);

/* ---- Caps Lock (HID indicators forwarded by the central) --------------------------------- */

struct indicator_state {
    bool caps_lock;
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
    };
}

static void indicator_update_cb(struct indicator_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.caps_lock = state.caps_lock;
        draw_status(widget);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_indicator_status, struct indicator_state, indicator_update_cb,
                            indicator_get_state)
ZMK_SUBSCRIPTION(widget_indicator_status, zmk_hid_indicators_changed);

/* ---- init --------------------------------------------------------------------------------- */

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);

    /*
     * Canvases are 68x68 and opaque, and the screen is 160 tall, so they overlap; creation order
     * is z-order. Each canvas only has content in part of its area, and nothing lies under that
     * part: indicators (bottom), battery/link (top), then the name over both. The child order
     * must match the CHILD_* enum, which draw_*() use.
     *
     * Positions are in the widget's 160x68 frame, which the display shows turned 90 degrees:
     * x counts from the BOTTOM of the screen, so x = 160 - 68 - y_from_top.
     */
    lv_obj_t *indicators = lv_canvas_create(widget->obj);
    lv_obj_align(indicators, LV_ALIGN_TOP_LEFT, 0, 0); /* screen y 92..160 */
    lv_canvas_set_buffer(indicators, widget->cbuf4, CANVAS_SIZE, CANVAS_SIZE,
                         LV_IMG_CF_TRUE_COLOR);

    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0); /* screen y 0..68 */
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    lv_obj_t *name = lv_canvas_create(widget->obj);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 26, 0); /* screen y 22..134, between battery and CAPS */
    lv_canvas_set_buffer(name, widget->cbuf2, NAME_CANVAS_W, NAME_CANVAS_H, LV_IMG_CF_TRUE_COLOR);

    /* Hidden scratch canvas the name is drawn on before it is rotated into `name`. */
    lv_obj_t *scratch = lv_canvas_create(widget->obj);
    lv_canvas_set_buffer(scratch, widget->cbuf3, NAME_W, NAME_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_add_flag(scratch, LV_OBJ_FLAG_HIDDEN);

    sys_slist_append(&widgets, &widget->node);

    draw_name(widget->obj, widget->cbuf2, widget->cbuf3);
    draw_status(widget);

    widget_battery_status_init();
    widget_connection_status_init();
    widget_indicator_status_init();

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
