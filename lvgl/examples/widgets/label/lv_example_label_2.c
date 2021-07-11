#include "../../../lvgl.h"
#if LV_USE_LABEL && LV_BUILD_EXAMPLES



extern uint8_t debug[200];
extern uint8_t EDID[256];

/**
 * Create a fake text shadow
 */
void lv_example_label_2(void)
{
    /*Create a style for the shadow*/
    static lv_style_t style_shadow;
    lv_style_init(&style_shadow);
    lv_style_set_text_opa(&style_shadow, LV_OPA_30);
    lv_style_set_text_color(&style_shadow, lv_color_black());

    /*Create a label for the shadow first (it's in the background)*/
    lv_obj_t * shadow_label = lv_label_create(lv_scr_act(), NULL);
    lv_obj_add_style(shadow_label, LV_PART_MAIN, LV_STATE_DEFAULT, &style_shadow);

    /*Create the main label*/
    lv_obj_t * main_label = lv_label_create(lv_scr_act(), NULL);
    /*
    lv_label_set_text(main_label, "A simple method to create\n"
                                  "shadows on a text.\n"
                                  "It even works with\n\n"
                                  "newlines     and spaces.");
  */

    uint16_t ts_cal1, ts_cal2;

 ts_cal1 = *(uint16_t*)(0x1FFFF7B8);

 
 ts_cal2 = *(uint16_t*)(0x1FFFF7C2);

    lv_label_set_text_fmt(main_label, "TDA9983B (REG_VERSION 0x%.2x)\n\n EDID (bytes 0 to 9) 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x\nhello", debug[0], EDID[0], EDID[1], EDID[2], EDID[3], EDID[4], EDID[5], EDID[6], EDID[7], EDID[8], EDID[9]);

    /*Set the same text for the shadow label*/
    lv_label_set_text(shadow_label, lv_label_get_text(main_label));

    /*Position the main label*/
    lv_obj_align(main_label, NULL, LV_ALIGN_IN_TOP_LEFT, 0, 0);

    /*Shift the second label down and to the right by 2 pixel*/
    lv_obj_align(shadow_label, main_label, LV_ALIGN_IN_TOP_LEFT, 2, 2);
}

#endif
