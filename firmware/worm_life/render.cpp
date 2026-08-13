// render.cpp — SSD1306 128x64 rendering of the worm world with U8g2.
#include "render.h"

#include <U8g2lib.h>
#include <Wire.h>
#include <math.h>

#include "brain.h"
#include "world.h"

// display wiring (ESP32 classic DevKit default I2C pins)
#define OLED_SDA 21
#define OLED_SCL 22

// full-buffer 128x64, hardware I2C
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE,
                                          /*clock=*/OLED_SCL, /*data=*/OLED_SDA);

#define STATUS_H 8
#define WORLD_H_PX (64 - STATUS_H)   // 56

// ---- drawing helpers -----------------------------------------------------
static void draw_worm(const Worm* w, uint8_t alpha) {
    // alpha: 255 = alive, lower = dying (fewer segments drawn)
    int n_seg = 9;
    if (alpha < 255) n_seg = 9 * alpha / 255;
    if (n_seg < 2) n_seg = 2;

    // wiggle amplitude from body-wall muscle activity
    float amp = 1.0f + 3.0f * brain_body_wall_activity(&w->brain);
    float sp = 2.8f;                      // segment spacing (px)
    bool reversing = w->speed < -1.0f;

    int prev_x = (int)w->x, prev_y = (int)w->y;
    for (int i = 1; i <= n_seg; i++) {
        float d = i * sp;
        float lat = amp * sinf(w->body_phase - i * 0.7f) * (reversing ? -1.0f : 1.0f);
        float px = w->x - d * cosf(w->heading) + lat * cosf(w->heading + 1.5708f);
        float py = w->y - d * sinf(w->heading) + lat * sinf(w->heading + 1.5708f);
        u8g2.drawLine(prev_x, prev_y, (int)px, (int)py);
        prev_x = (int)px; prev_y = (int)py;
    }
    // head
    u8g2.drawDisc((int)w->x, (int)w->y, 2);
}

static void draw_food(void) {
    for (int i = 0; i < g_world.n_food; i++) {
        const Food* f = &g_world.food[i];
        float r = f->radius;
        // bacterial lawn: cluster of dots whose brightness tracks amount
        int dots = 2 + (int)(3.0f * (f->amount / FOOD_AMOUNT));
        if (dots < 1) dots = 1;
        for (int k = 0; k < dots; k++) {
            float a = 6.2832f * (float)k / dots;
            float rr = r * 0.5f;
            u8g2.drawDisc((int)(f->x + rr * cosf(a)), (int)(f->y + rr * sinf(a)), 2);
        }
        u8g2.drawCircle((int)f->x, (int)f->y, (int)(r * 0.8f));
    }
}

static void draw_eggs(void) {
    for (int i = 0; i < g_world.n_eggs; i++) {
        const Egg* e = &g_world.eggs[i];
        // pulse as it approaches hatching
        float p = 1.0f - (e->age / e->hatch_at);
        if (p > 0.15f) u8g2.drawPixel((int)e->x, (int)e->y);
        else           u8g2.drawBox((int)e->x - 1, (int)e->y - 1, 3, 3);
    }
}

static void draw_status(void) {
    int y = WORLD_H_PX + 1;

    // hunger bar (worm 0)
    const Worm* w0 = &g_world.worms[0];
    int hw = (int)(40.0f * w0->hunger / 100.0f);
    u8g2.drawFrame(0, y, 42, 6);
    if (hw > 0) u8g2.drawBox(1, y + 1, hw, 4);

    // age bar
    int aw = (int)(40.0f * w0->age / w0->lifespan);
    u8g2.drawFrame(44, y, 42, 6);
    if (aw > 0) u8g2.drawBox(45, y + 1, aw, 4);

    // population + eggs
    u8g2.setFont(u8g2_font_tom_thumb_4x6_mf);
    char buf[16];
    snprintf(buf, sizeof(buf), "%dw %de", g_world.n_worms, g_world.n_eggs);
    u8g2.drawStr(88, y + 5, buf);
}

// ---- public --------------------------------------------------------------
void render_init(void) {
    Wire.begin(OLED_SDA, OLED_SCL);
    u8g2.begin();
}

void render_frame(void) {
    u8g2.clearBuffer();

    // faint border around the arena
    u8g2.drawFrame(0, 0, 128, WORLD_H_PX);

    draw_food();
    draw_eggs();
    for (int i = 0; i < g_world.n_worms; i++) {
        const Worm* w = &g_world.worms[i];
        uint8_t alpha = 255;
        if (w->dying) alpha = (uint8_t)(255 * (1.0f - w->death_t / 2.5f));
        draw_worm(w, alpha);
    }
    draw_status();

    u8g2.sendBuffer();
}
