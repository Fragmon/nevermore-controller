#include "ui.hpp"
#include "config.hpp"
#include "display.hpp"
#include "gatt/fan.hpp"
#include "lvgl.h"
#include "sdk/ble_data_types.hpp"
#include "sensors.hpp"
#include "settings.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>

#if LV_COLOR_DEPTH != 16 || LV_COLOR_16_SWAP != 1
#error "logo images assume LV_COLOR_DEPTH 16 w/ LV_COLOR_16_SWAP"
#endif
#if !LV_FONT_MONTSERRAT_48
#error "modern UI needs `LV_FONT_MONTSERRAT_48`"
#endif

extern "C" {
LV_IMG_DECLARE(img_logo_small);
LV_IMG_DECLARE(img_logo_large);
}

using namespace std;

namespace nevermore::ui::modern {

namespace {

constexpr uint32_t COLOUR_TEXT = 0xFFFFFF;
constexpr uint32_t COLOUR_TEXT_SECONDARY = 0xCFD8D9;
constexpr uint32_t COLOUR_TEXT_MUTED = 0x7F9497;
constexpr uint32_t COLOUR_ARC_TRACK = 0x1B3033;
constexpr uint32_t COLOUR_VOC_GOOD = 0x5ED08A;
constexpr uint32_t COLOUR_VOC_FAIR = 0xF0B93A;
constexpr uint32_t COLOUR_VOC_POOR = 0xEF5B5B;
constexpr uint32_t COLOUR_SPLASH_TOP = 0x7DBFC3;
constexpr uint32_t COLOUR_SPLASH_BOTTOM = 0x1F7C82;

// VOC index <= this reads as 'good'. The VOC index baseline is 100.
constexpr uint16_t VOC_GOOD_MAX = 150;

constexpr auto PLOT_LENGTH = 1h;
constexpr uint16_t PLOT_POINTS = 80;
constexpr lv_coord_t PLOT_RANGE_MIN = 150;  // keep the baseline from looking like noise

constexpr auto LABELS_INTERVAL = 1s;
constexpr auto PLOT_INTERVAL = PLOT_LENGTH / PLOT_POINTS;

struct Widgets {
    lv_obj_t* voc = nullptr;
    lv_obj_t* voc_status = nullptr;
    lv_obj_t* temperature = nullptr;
    lv_obj_t* humidity = nullptr;
    lv_obj_t* pressure = nullptr;
    lv_obj_t* fan = nullptr;
    lv_obj_t* plot = nullptr;
    lv_chart_series_t* plot_voc = nullptr;
} g_widgets;

uint32_t ms(auto duration) {
    return uint32_t(chrono::duration_cast<chrono::milliseconds>(duration).count());
}

lv_obj_t* mk_container(lv_obj_t* parent, lv_flex_flow_t flow) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(obj, flow);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t* mk_label(lv_obj_t* parent, lv_font_t const& font, uint32_t colour, char const* text = "") {
    auto* obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, &font, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(colour), LV_PART_MAIN);
    lv_label_set_text(obj, text);
    return obj;
}

// Montserrat's built-in subset lacks '°', so draw the degree sign as a tiny ring.
void mk_degree_sign(lv_obj_t* parent) {
    auto* ring = lv_obj_create(parent);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 5, 5);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(ring, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ring, lv_color_hex(COLOUR_TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_set_style_border_opa(ring, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_translate_y(ring, -5, LV_PART_MAIN);
    lv_obj_set_style_pad_left(ring, 1, LV_PART_MAIN);
}

// One column of the readings row: value on top, caption below.
lv_obj_t* mk_reading(lv_obj_t* parent, char const* caption, bool degree_sign = false) {
    auto* column = mk_container(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(column, 60);

    auto* value_row = mk_container(column, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(value_row, 1, LV_PART_MAIN);
    auto* value = mk_label(value_row, lv_font_montserrat_16, COLOUR_TEXT_SECONDARY, "--");
    if (degree_sign) mk_degree_sign(value_row);

    mk_label(column, lv_font_montserrat_10, COLOUR_TEXT_MUTED, caption);
    return value;
}

void plot_draw_fade(lv_event_t* e) {
    auto* obj = lv_event_get_target(e);
    auto* dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part != LV_PART_ITEMS || !dsc->p1 || !dsc->p2 || !dsc->line_dsc) return;

    // shade the area under the line, fading out towards the bottom
    lv_draw_mask_line_param_t line_mask{};
    lv_draw_mask_line_points_init(
            &line_mask, dsc->p1->x, dsc->p1->y, dsc->p2->x, dsc->p2->y, LV_DRAW_MASK_LINE_SIDE_BOTTOM);
    auto line_mask_id = lv_draw_mask_add(&line_mask, nullptr);

    lv_draw_mask_fade_param_t fade_mask{};
    lv_draw_mask_fade_init(&fade_mask, &obj->coords, LV_OPA_COVER, obj->coords.y1, LV_OPA_TRANSP, obj->coords.y2);
    auto fade_mask_id = lv_draw_mask_add(&fade_mask, nullptr);

    lv_draw_rect_dsc_t rect{};
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_30;
    rect.bg_color = dsc->line_dsc->color;
    lv_area_t area{
            .x1 = dsc->p1->x,
            .y1 = min(dsc->p1->y, dsc->p2->y),
            .x2 = lv_coord_t(dsc->p2->x - 1),
            .y2 = obj->coords.y2,
    };
    lv_draw_rect(dsc->draw_ctx, &rect, &area);

    lv_draw_mask_free_param(&line_mask);
    lv_draw_mask_free_param(&fade_mask);
    lv_draw_mask_remove_id(line_mask_id);
    lv_draw_mask_remove_id(fade_mask_id);
}

lv_obj_t* mk_plot(lv_obj_t* parent) {
    auto* chart = lv_chart_create(parent);
    lv_obj_remove_style_all(chart);  // no bg, border, padding, or division lines
    lv_obj_set_size(chart, 168, 46);
    lv_obj_align(chart, LV_ALIGN_CENTER, 0, 24);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(chart, PLOT_POINTS);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, PLOT_RANGE_MIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_opa(chart, LV_OPA_70, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);  // no point markers

    g_widgets.plot_voc = lv_chart_add_series(chart, lv_color_hex(COLOUR_ACCENT), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart, g_widgets.plot_voc, LV_CHART_POINT_NONE);
    lv_obj_add_event_cb(chart, plot_draw_fade, LV_EVENT_DRAW_PART_BEGIN, nullptr);
    return chart;
}

lv_obj_t* mk_fan_arc(lv_obj_t* parent) {
    auto* arc = lv_arc_create(parent);
    lv_obj_set_size(arc, lv_pct(100), lv_pct(100));
    lv_obj_center(arc);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_ADV_HITTEST);
    lv_arc_set_range(arc, 0, 200);
    lv_arc_set_bg_angles(arc, 115, 65);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_pad_all(arc, 4, LV_PART_MAIN);

    lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COLOUR_ARC_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);

    lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COLOUR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    // knob only visible while dragging
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_color(arc, lv_color_hex(COLOUR_TEXT), int(LV_PART_KNOB) | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(arc, LV_OPA_COVER, int(LV_PART_KNOB) | LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(arc, 3, int(LV_PART_KNOB) | LV_STATE_PRESSED);
    return arc;
}

lv_obj_t* mk_splash() {
    auto* screen = lv_obj_create(nullptr);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOUR_SPLASH_TOP), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(COLOUR_SPLASH_BOTTOM), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    auto* logo = lv_img_create(screen);
    lv_img_set_src(logo, &img_logo_large);
    lv_obj_center(logo);
    return screen;
}

// Single sensor setups (or `DISPLAY_HIDE_INTAKE`) only have meaningful exhaust readings.
template <typename A>
A pick(A const& intake, A const& exhaust) {
    if (DISPLAY_HIDE_INTAKE) return exhaust;
    return intake != BLE::NOT_KNOWN ? intake : exhaust;
}

void label_set(lv_obj_t* obj, char const* fmt, auto const& value, double scale = 1) {
    if (value == BLE::NOT_KNOWN) {
        lv_label_set_text(obj, "--");
        return;
    }

    char buffer[16];
    snprintf(buffer, size(buffer), fmt, double(value) / scale);
    lv_label_set_text(obj, buffer);
}

void update_labels(lv_timer_t*) {
    auto const& state = sensors::g_sensors.with_fallbacks();

    auto voc = pick(state.voc_index_intake, state.voc_index_exhaust);
    label_set(g_widgets.voc, "%.0f", voc);

    auto [status, colour] = ([&]() -> pair<char const*, uint32_t> {
        if (voc == BLE::NOT_KNOWN) return {"Startet", COLOUR_TEXT_MUTED};
        auto const value = voc.value_or(0);
        if (value <= VOC_GOOD_MAX) return {"Gut", COLOUR_VOC_GOOD};
        if (value < settings::g_active.fan_policy_env.voc_passive_max.value_or(250)) return {"Mittel", COLOUR_VOC_FAIR};
        return {"Hoch", COLOUR_VOC_POOR};
    })();
    lv_label_set_text(g_widgets.voc_status, status);
    lv_obj_set_style_text_color(g_widgets.voc_status, lv_color_hex(colour), LV_PART_MAIN);

    label_set(g_widgets.temperature, "%.1f", pick(state.temperature_intake, state.temperature_exhaust));
    label_set(g_widgets.humidity, "%.0f%%", pick(state.humidity_intake, state.humidity_exhaust));
    label_set(g_widgets.pressure, "%.0f", pick(state.pressure_intake, state.pressure_exhaust), 1e2);

    auto const overridden = gatt::fan::fan_power_override() != BLE::NOT_KNOWN;
    char buffer[48];
    snprintf(buffer, size(buffer), "%.0f%%  %.0f rpm%s", ceil(double(gatt::fan::fan_power())),
            double(gatt::fan::fan_rpm()), overridden ? "  manuell" : "");
    lv_label_set_text(g_widgets.fan, buffer);
    lv_obj_set_style_text_color(
            g_widgets.fan, lv_color_hex(overridden ? COLOUR_OVERRIDE : COLOUR_ACCENT), LV_PART_MAIN);
}

void update_plot(lv_timer_t*) {
    auto const& state = sensors::g_sensors.with_fallbacks();
    auto voc = pick(state.voc_index_intake, state.voc_index_exhaust);
    lv_chart_set_next_value(g_widgets.plot, g_widgets.plot_voc, voc.value_or(LV_CHART_POINT_NONE));

    lv_coord_t top = 0;
    auto const* ys = lv_chart_get_y_array(g_widgets.plot, g_widgets.plot_voc);
    for (uint16_t i = 0; i < PLOT_POINTS; ++i)  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        if (ys[i] != LV_CHART_POINT_NONE) top = max(top, ys[i]);

    lv_chart_set_range(g_widgets.plot, LV_CHART_AXIS_PRIMARY_Y, 0, max<lv_coord_t>(PLOT_RANGE_MIN, top * 6 / 5));
}

}  // namespace

NevermoreDisplayUI create() {
    auto* disp = lv_disp_get_default();
    auto* theme = lv_theme_default_init(disp, lv_color_hex(COLOUR_ACCENT), lv_color_hex(COLOUR_OVERRIDE), true,
            &lv_font_montserrat_14);
    lv_disp_set_theme(disp, theme);

    NevermoreDisplayUI ui{};
    ui.screen = lv_obj_create(nullptr);
    lv_obj_clear_flag(ui.screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui.screen, lv_color_hex(COLOUR_BACKGROUND), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui.screen, LV_OPA_COVER, LV_PART_MAIN);

    // back to front
    g_widgets.plot = mk_plot(ui.screen);

    auto* logo = lv_img_create(ui.screen);
    lv_img_set_src(logo, &img_logo_small);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 30);

    g_widgets.voc = mk_label(ui.screen, lv_font_montserrat_48, COLOUR_TEXT, "--");
    lv_obj_align(g_widgets.voc, LV_ALIGN_CENTER, 0, -10);

    g_widgets.voc_status = mk_label(ui.screen, lv_font_montserrat_14, COLOUR_TEXT_MUTED);
    lv_obj_align(g_widgets.voc_status, LV_ALIGN_CENTER, 0, 26);

    auto* readings = mk_container(ui.screen, LV_FLEX_FLOW_ROW);
    lv_obj_align(readings, LV_ALIGN_CENTER, 0, 60);
    g_widgets.temperature = mk_reading(readings, "TEMP", true);
    g_widgets.humidity = mk_reading(readings, "FEUCHTE");
    g_widgets.pressure = mk_reading(readings, "hPa");

    g_widgets.fan = mk_label(ui.screen, lv_font_montserrat_12, COLOUR_ACCENT);
    lv_obj_align(g_widgets.fan, LV_ALIGN_CENTER, 0, 92);

    // touch overlay must be below the arc, otherwise it eats the arc's touches
    ui.touch_overlay = lv_obj_create(ui.screen);
    lv_obj_remove_style_all(ui.touch_overlay);
    lv_obj_set_size(ui.touch_overlay, lv_pct(100), lv_pct(100));
    lv_obj_center(ui.touch_overlay);
    lv_obj_clear_flag(ui.touch_overlay, LV_OBJ_FLAG_SCROLLABLE);

    ui.fan_power_arc = mk_fan_arc(ui.screen);

    lv_timer_create(update_labels, ms(LABELS_INTERVAL), nullptr);
    lv_timer_create(update_plot, ms(PLOT_INTERVAL), nullptr);
    update_labels(nullptr);

    if (DISPLAY_SPLASH_DURATION <= 0s) {
        lv_disp_load_scr(ui.screen);
    } else {
        lv_disp_load_scr(mk_splash());
        auto* timer = lv_timer_create(
                [](lv_timer_t* timer) {
                    auto* screen = static_cast<lv_obj_t*>(timer->user_data);
                    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_ON, 400, 0, true);  // deletes splash
                },
                ms(DISPLAY_SPLASH_DURATION), ui.screen);
        lv_timer_set_repeat_count(timer, 1);
    }

    return ui;
}

}  // namespace nevermore::ui::modern
