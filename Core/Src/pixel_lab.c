#include "pixel_lab.h"
#include "lv_port.h"
#include "lvgl.h"

#include <stdio.h>

static lv_style_t card_style;
static lv_style_t title_style;
static lv_style_t muted_style;
static lv_style_t chip_style;

static lv_obj_t *mk_label(lv_obj_t *p, const char *txt, const lv_font_t *font)
{
    lv_obj_t *o = lv_label_create(p);
    lv_label_set_text(o, txt);
    if(font) lv_obj_set_style_text_font(o, font, 0);
    return o;
}

static lv_obj_t *mk_card(lv_obj_t *p)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_add_style(o, &card_style, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *mk_chip(lv_obj_t *p, const char *txt)
{
    lv_obj_t *o = lv_label_create(p);
    lv_label_set_text(o, txt);
    lv_obj_add_style(o, &chip_style, 0);
    return o;
}

static void test_btn_cb(lv_event_t *e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        uintptr_t mode = (uintptr_t)lv_event_get_user_data(e);
        PixelTest_Start((uint8_t)mode);
    }
}

static lv_obj_t *mk_test_btn(lv_obj_t *p, const char *txt, uint8_t mode)
{
    lv_obj_t *b = lv_btn_create(p);
    lv_obj_set_height(b, 42);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_add_event_cb(b, test_btn_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)mode);
    lv_obj_t *l = mk_label(b, txt, &lv_font_montserrat_14);
    lv_obj_center(l);
    return b;
}

static void build_chart(lv_obj_t *parent)
{
    lv_obj_t *chart = lv_chart_create(parent);
    lv_obj_set_size(chart, lv_pct(100), 170);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 32);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_obj_set_style_bg_opa(chart, LV_OPA_20, 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_chart_series_t *a = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_CYAN), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_series_t *b = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
    for(uint32_t i = 0; i < 32; ++i) {
        lv_chart_set_next_value(chart, a, 45 + (int32_t)((i * 17U + 23U) % 43U));
        lv_chart_set_next_value(chart, b, 15 + (int32_t)((i * 29U + 11U) % 62U));
    }
}

static void add_metric(lv_obj_t *p, const char *name, const char *value, const char *unit, lv_palette_t pal)
{
    lv_obj_t *row = lv_obj_create(p);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 50);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 32);
    lv_obj_set_style_bg_color(dot, lv_palette_main(pal), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, 3, 0);

    lv_obj_t *n = mk_label(row, name, &lv_font_montserrat_14);
    lv_obj_set_width(n, 145);
    lv_obj_add_style(n, &muted_style, 0);

    lv_obj_t *v = mk_label(row, value, &lv_font_montserrat_20);
    lv_obj_set_width(v, 95);

    lv_obj_t *u = mk_label(row, unit, &lv_font_montserrat_12);
    lv_obj_add_style(u, &muted_style, 0);
}

void PixelLab_Create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x10151D), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    lv_style_init(&card_style);
    lv_style_set_bg_color(&card_style, lv_color_hex(0x18212C));
    lv_style_set_bg_opa(&card_style, LV_OPA_COVER);
    lv_style_set_border_color(&card_style, lv_color_hex(0x344454));
    lv_style_set_border_width(&card_style, 1);
    lv_style_set_radius(&card_style, 8);
    lv_style_set_pad_all(&card_style, 16);

    lv_style_init(&title_style);
    lv_style_set_text_font(&title_style, &lv_font_montserrat_20);
    lv_style_set_text_color(&title_style, lv_color_white());

    lv_style_init(&muted_style);
    lv_style_set_text_color(&muted_style, lv_color_hex(0x94A3B8));

    lv_style_init(&chip_style);
    lv_style_set_text_font(&chip_style, &lv_font_montserrat_12);
    lv_style_set_text_color(&chip_style, lv_color_hex(0xD8F3FF));
    lv_style_set_bg_color(&chip_style, lv_color_hex(0x164E63));
    lv_style_set_bg_opa(&chip_style, LV_OPA_COVER);
    lv_style_set_radius(&chip_style, 6);
    lv_style_set_pad_left(&chip_style, 10);
    lv_style_set_pad_right(&chip_style, 10);
    lv_style_set_pad_top(&chip_style, 6);
    lv_style_set_pad_bottom(&chip_style, 6);

    /* Header: intentionally compact. At 1920 pixels this should look like a desktop UI,
       not a tablet UI. Every dimension below is in literal framebuffer pixels. */
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_pos(hdr, 24, 18);
    lv_obj_set_size(hdr, 1872, 74);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, 14, 0);

    lv_obj_t *brand = mk_label(hdr, "PIXEL INTEGRITY LAB", &lv_font_montserrat_24);
    lv_obj_set_width(brand, 340);
    mk_chip(hdr, "1920 x 1080 ACTIVE");
    mk_chip(hdr, "148.500 MHz");
    mk_chip(hdr, "CTA VIC 16");
    mk_chip(hdr, "RGB332 -> L8");
    mk_chip(hdr, "PIXEL REP 0");
    mk_chip(hdr, "DIRECT VCLK");

    lv_obj_t *sub = mk_label(scr,
        "Native-pixel verification dashboard | click a test below; in full-screen test mode click anywhere to advance, fifth click returns to GUI",
        &lv_font_montserrat_14);
    lv_obj_set_pos(sub, 26, 90);
    lv_obj_add_style(sub, &muted_style, 0);

    /* Four dense desktop-style columns, 448 px each. */
    const int x0 = 24, gap = 16, cw = 456, y0 = 124, ch = 754;
    lv_obj_t *c1 = mk_card(scr); lv_obj_set_pos(c1, x0 + 0*(cw+gap), y0); lv_obj_set_size(c1, cw, ch);
    lv_obj_t *c2 = mk_card(scr); lv_obj_set_pos(c2, x0 + 1*(cw+gap), y0); lv_obj_set_size(c2, cw, ch);
    lv_obj_t *c3 = mk_card(scr); lv_obj_set_pos(c3, x0 + 2*(cw+gap), y0); lv_obj_set_size(c3, cw, ch);
    lv_obj_t *c4 = mk_card(scr); lv_obj_set_pos(c4, x0 + 3*(cw+gap), y0); lv_obj_set_size(c4, cw, ch);

    lv_obj_set_flex_flow(c1, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(c1, 10, 0);
    lv_obj_add_style(mk_label(c1, "HDMI transmitter", NULL), &title_style, 0);
    add_metric(c1, "Active pixels", "1920x1080", "px", LV_PALETTE_CYAN);
    add_metric(c1, "Pixel clock", "148.500", "MHz", LV_PALETTE_GREEN);
    add_metric(c1, "Frame rate", "60.000", "Hz", LV_PALETTE_ORANGE);
    add_metric(c1, "TMDS repeat", "0", "x", LV_PALETTE_BLUE);
    add_metric(c1, "TDA scaler", "OFF", "", LV_PALETTE_GREEN);
    add_metric(c1, "CSC matrix", "BYPASS", "", LV_PALETTE_GREEN);
    lv_obj_t *note1 = mk_label(c1, "TDA9983B receives direct VCLK with REFDIV2=0 and SEL_CLK1=0; no prefilter, interpolation or pixel repetition.", &lv_font_montserrat_12);
    lv_label_set_long_mode(note1, LV_LABEL_LONG_WRAP); lv_obj_set_width(note1, lv_pct(100)); lv_obj_add_style(note1, &muted_style, 0);

    lv_obj_set_flex_flow(c2, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(c2, 12, 0);
    lv_obj_add_style(mk_label(c2, "Pixel clock activity", NULL), &title_style, 0);
    build_chart(c2);
    lv_obj_t *bar = lv_bar_create(c2); lv_obj_set_size(bar, lv_pct(100), 18); lv_bar_set_range(bar, 0, 100); lv_bar_set_value(bar, 88, LV_ANIM_OFF);
    mk_label(c2, "Framebuffer scan bandwidth", &lv_font_montserrat_12);
    lv_obj_t *sl = lv_slider_create(c2); lv_obj_set_size(sl, lv_pct(100), 18); lv_slider_set_value(sl, 68, LV_ANIM_OFF);
    mk_label(c2, "Interactive LVGL load", &lv_font_montserrat_12);
    lv_obj_t *sw = lv_switch_create(c2); lv_obj_add_state(sw, LV_STATE_CHECKED);
    mk_label(c2, "Mouse interaction enabled", &lv_font_montserrat_12);
    lv_obj_t *dd = lv_dropdown_create(c2); lv_obj_set_width(dd, lv_pct(100)); lv_dropdown_set_options(dd, "1080p60 native\n1px verification\nTiming diagnostics");

    lv_obj_set_flex_flow(c3, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(c3, 9, 0);
    lv_obj_add_style(mk_label(c3, "Signal geometry", NULL), &title_style, 0);
    const char *geo[] = {
        "H active   1920", "H front      88", "H sync       44", "H back      148", "H total    2200",
        "V active   1080", "V front       4", "V sync        5", "V back       36", "V total    1125"
    };
    for(unsigned i=0;i<10;i++) {
        lv_obj_t *l = mk_label(c3, geo[i], &lv_font_montserrat_14);
        if((i==0)||(i==5)) lv_obj_set_style_text_color(l, lv_palette_main(LV_PALETTE_CYAN), 0);
    }
    lv_obj_t *line = lv_obj_create(c3); lv_obj_set_size(line, lv_pct(100), 1); lv_obj_set_style_bg_color(line, lv_color_hex(0x41566B), 0); lv_obj_set_style_border_width(line,0,0);
    lv_obj_t *n3 = mk_label(c3, "A 1-pixel pattern is generated directly in the L8 framebuffer. It bypasses LVGL anti-aliasing and bitmap scaling.", &lv_font_montserrat_12);
    lv_label_set_long_mode(n3, LV_LABEL_LONG_WRAP); lv_obj_set_width(n3, lv_pct(100)); lv_obj_add_style(n3, &muted_style, 0);

    lv_obj_set_flex_flow(c4, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(c4, 12, 0);
    lv_obj_add_style(mk_label(c4, "Interactive controls", NULL), &title_style, 0);
    lv_obj_t *cb1 = lv_checkbox_create(c4); lv_checkbox_set_text(cb1, "Strict native timing"); lv_obj_add_state(cb1, LV_STATE_CHECKED);
    lv_obj_t *cb2 = lv_checkbox_create(c4); lv_checkbox_set_text(cb2, "Full RGB range"); lv_obj_add_state(cb2, LV_STATE_CHECKED);
    lv_obj_t *cb3 = lv_checkbox_create(c4); lv_checkbox_set_text(cb3, "No pixel repetition"); lv_obj_add_state(cb3, LV_STATE_CHECKED);
    lv_obj_t *cb4 = lv_checkbox_create(c4); lv_checkbox_set_text(cb4, "No TDA test generator"); lv_obj_add_state(cb4, LV_STATE_CHECKED);
    mk_label(c4, "Mouse test", &lv_font_montserrat_14);
    lv_obj_t *ta = lv_textarea_create(c4); lv_obj_set_width(ta, lv_pct(100)); lv_textarea_set_one_line(ta, true); lv_textarea_set_placeholder_text(ta, "Click and type target");
    lv_obj_t *btn = lv_btn_create(c4); lv_obj_set_size(btn, lv_pct(100), 44); lv_obj_t *bl = mk_label(btn, "1080p CONTROL", &lv_font_montserrat_14); lv_obj_center(bl);

    /* Dedicated exact-pixel test strip. */
    lv_obj_t *tests = mk_card(scr);
    lv_obj_set_pos(tests, 24, 896);
    lv_obj_set_size(tests, 1872, 154);
    lv_obj_set_flex_flow(tests, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tests, 10, 0);
    lv_obj_t *tl = mk_label(tests, "FULL-SCREEN 1:1 TESTS — patterns are written directly to framebuffer, not scaled images", &lv_font_montserrat_16);
    lv_obj_set_style_text_color(tl, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_t *row = lv_obj_create(tests); lv_obj_remove_style_all(row); lv_obj_set_size(row, lv_pct(100), 48);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(row, 10, 0);
    mk_test_btn(row, "1 PX CHECKERBOARD", PIXEL_TEST_CHECKER);
    mk_test_btn(row, "1 PX VERTICAL", PIXEL_TEST_VSTRIPES);
    mk_test_btn(row, "1 PX HORIZONTAL", PIXEL_TEST_HSTRIPES);
    mk_test_btn(row, "PIXEL CROSSHAIR", PIXEL_TEST_CROSSHAIR);
    mk_test_btn(row, "RGB332 RAMP", PIXEL_TEST_RGB_RAMP);
}
