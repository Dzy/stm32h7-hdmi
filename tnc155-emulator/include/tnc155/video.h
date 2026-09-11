#ifndef TNC155_VIDEO_H
#define TNC155_VIDEO_H

#include "tnc155/machine.h"

#include <stdbool.h>
#include <stdint.h>

/* The GDC scans 32 16-bit words per active line.  Pure graphics therefore
   produces 512 pixels.  P2 programs 468 active lines for the normal mixed
   text screen and 490 active lines for the visualization graphics screen.
   The backing frame is sized for the larger mode; tnc155_video_active_height()
   reports which portion is currently active. */
enum {
    TNC155_VIDEO_WIDTH = 512,
    TNC155_VIDEO_TEXT_HEIGHT = 468,
    TNC155_VIDEO_GRAPHICS_HEIGHT = 490,
    TNC155_VIDEO_HEIGHT = TNC155_VIDEO_GRAPHICS_HEIGHT,
    TNC155_DEBUG_WIDTH = 512,
    TNC155_DEBUG_HEIGHT = 490
};

unsigned tnc155_video_active_height(const tnc155_machine *machine);
void tnc155_video_render(const tnc155_machine *machine, uint32_t *argb,
                         unsigned pitch_pixels, bool debug_overlay);
void tnc155_video_render_debug(const tnc155_machine *machine, uint32_t *argb,
                               unsigned pitch_pixels);

#endif
