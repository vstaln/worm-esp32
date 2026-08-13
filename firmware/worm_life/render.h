// render.h — SSD1306 128x64 rendering of the worm world.
#pragma once

void render_init(void);
// draw one frame; call at ~20-30 Hz from the render task
void render_frame(void);
