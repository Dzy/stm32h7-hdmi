# STM32H7 HDMI / LVGL reference

STM32H743IIT6 + IS42S32800B + LTDC + TDA9983B HDMI reference project.

Current board reference:
- 1920x1080p60, CTA VIC 16, 148.5 MHz LTDC pixel clock
- LTDC L8 framebuffer with RGB332 CLUT
- TDA9983B direct-VCLK path: `CCIR_DIV.REFDIV2 = 0`, `SEL_CLK1 = 0`
- no TDA scaler and no pixel repetition
- IS42S32800B-6 commercial SDRAM at 108 MHz
- FMC timings 2/7/5/7/2/2/2, CAS 2, BL=1, read burst enabled
- 4096 refreshes / 64 ms, FMC refresh count 1667
- LVGL v8.4.0 pinned by the Makefile

`make` downloads the pinned LVGL source into `Middlewares/Third_Party/lvgl` on the first build.
The old vendored LVGL 8.0 tree is no longer used.
