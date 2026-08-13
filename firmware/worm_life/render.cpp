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

#define VIEW_W 128
#define VIEW_H 64

// ---- camera: follows the worm, clamped to the world -----------------------
static float cam_x = 0.0f, cam_y = 0.0f;

static void camera_follow(const Worm* w) {
    // keep the head near the middle of the screen
    float tx = w->x - VIEW_W * 0.5f;
    float ty = w->y - VIEW_H * 0.5f;
    cam_x += (tx - cam_x) * 0.14f;
    cam_y += (ty - cam_y) * 0.14f;
    float max_x = (float)(WORLD_W - VIEW_W);
    float max_y = (float)(WORLD_H - VIEW_H);
    if (cam_x < 0.0f) cam_x = 0.0f;
    if (cam_y < 0.0f) cam_y = 0.0f;
    if (cam_x > max_x) cam_x = max_x;
    if (cam_y > max_y) cam_y = max_y;
}

// ---- worm body ------------------------------------------------------------
// The body is the path the head has recently taken (a ring buffer of head
// positions), drawn as a smooth Catmull-Rom spline with an undulation that
// is zero at the head and grows toward the tail.  Turns and reversals read
// as a real body following the head.
#define TRACK_N 14
#define TRACK_STEP 2.0f          // px between recorded head positions
static float track_x[TRACK_N], track_y[TRACK_N];
static float last_hx = -1e9f, last_hy = -1e9f;
static bool  track_ok = false;

static void track_reset(const Worm* w) {
    // lay the body out straight behind the head
    for (int k = 0; k < TRACK_N; k++) {
        float d = (float)(TRACK_N - 1 - k) * TRACK_STEP;
        track_x[k] = w->x - d * cosf(w->heading);
        track_y[k] = w->y - d * sinf(w->heading);
    }
    last_hx = w->x; last_hy = w->y;
    track_ok = true;
}

static void track_update(const Worm* w) {
    if (!track_ok) { track_reset(w); return; }
    float dx = w->x - last_hx, dy = w->y - last_hy;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist > 12.0f) { track_reset(w); return; }   // safety: big jump
    if (dist < TRACK_STEP) return;
    for (int k = 0; k < TRACK_N - 1; k++) {
        track_x[k] = track_x[k + 1];
        track_y[k] = track_y[k + 1];
    }
    track_x[TRACK_N - 1] = w->x;
    track_y[TRACK_N - 1] = w->y;
    last_hx = w->x; last_hy = w->y;
}

// sample a Catmull-Rom spline segment (P0..P3) at t in [0,1]
static void spline_pt(float p0x, float p0y, float p1x, float p1y,
                      float p2x, float p2y, float p3x, float p3y,
                      float t, float* ox, float* oy) {
    float t2 = t * t, t3 = t2 * t;
    *ox = 0.5f * ((2.0f * p1x) + (-p0x + p2x) * t +
                  (2.0f * p0x - 5.0f * p1x + 4.0f * p2x - p3x) * t2 +
                  (-p0x + 3.0f * p1x - 3.0f * p2x + p3x) * t3);
    *oy = 0.5f * ((2.0f * p1y) + (-p0y + p2y) * t +
                  (2.0f * p0y - 5.0f * p1y + 4.0f * p2y - p3y) * t2 +
                  (-p0y + 3.0f * p1y - 3.0f * p2y + p3y) * t3);
}

static void draw_worm(const Worm* w, uint8_t alpha) {
    track_update(w);

    // dying: keep only the head-side portion of the body
    int count = TRACK_N;
    if (alpha < 255) count = 2 + (count - 2) * alpha / 255;
    int start = TRACK_N - count;

    // undulation amplitude from body-wall muscle activity
    float amp = 0.9f + 1.6f * brain_body_wall_activity(&w->brain);
    float nx = cosf(w->heading + 1.5708f), ny = sinf(w->heading + 1.5708f);

    // spline control points: wiggled track + live head
    float pts_x[TRACK_N + 1], pts_y[TRACK_N + 1];
    int npts = 0;
    for (int k = start; k < TRACK_N; k++) {
        float t = (float)(k - start) / (float)(count - 1);
        float lat = amp * (1.0f - t) * sinf(w->body_phase - t * 4.5f);
        pts_x[npts] = track_x[k] + lat * nx;
        pts_y[npts] = track_y[k] + lat * ny;
        npts++;
    }
    pts_x[npts] = w->x; pts_y[npts] = w->y;   // live head
    npts++;

    // trace the spline; overlapping radius-1 discs make a smooth 3 px body
    const int SUB = 3;   // samples per segment
    for (int k = 0; k < npts - 1; k++) {
        int i0 = (k > 0) ? k - 1 : k;
        int i3 = (k + 2 < npts) ? k + 2 : k + 1;
        for (int s = 0; s < SUB; s++) {
            float t = (float)s / SUB;
            float wx, wy;
            spline_pt(pts_x[i0], pts_y[i0],
                      pts_x[k], pts_y[k],
                      pts_x[k + 1], pts_y[k + 1],
                      pts_x[i3], pts_y[i3], t, &wx, &wy);
            u8g2.drawDisc((int)(wx - cam_x), (int)(wy - cam_y), 1);
        }
    }
    // tail cap + head
    u8g2.drawDisc((int)(pts_x[0] - cam_x), (int)(pts_y[0] - cam_y), 1);
    u8g2.drawDisc((int)(w->x - cam_x), (int)(w->y - cam_y), 2);
}

static void draw_food(void) {
    for (int i = 0; i < g_world.n_food; i++) {
        const Food* f = &g_world.food[i];
        float fx = f->x - cam_x, fy = f->y - cam_y;
        if (fx < -8.0f || fx > VIEW_W + 8.0f || fy < -8.0f || fy > VIEW_H + 8.0f) continue;
        float r = f->radius;
        // bacterial lawn: cluster of dots whose brightness tracks amount
        int dots = 1 + (int)(2.0f * (f->amount / FOOD_AMOUNT));
        if (dots < 1) dots = 1;
        for (int k = 0; k < dots; k++) {
            float a = 6.2832f * (float)k / dots;
            float rr = r * 0.4f;
            u8g2.drawDisc((int)(fx + rr * cosf(a)), (int)(fy + rr * sinf(a)), 1);
        }
        u8g2.drawCircle((int)fx, (int)fy, (int)(r * 0.6f));
    }
}

static void draw_eggs(void) {
    for (int i = 0; i < g_world.n_eggs; i++) {
        const Egg* e = &g_world.eggs[i];
        float ex = e->x - cam_x, ey = e->y - cam_y;
        if (ex < -4.0f || ex > VIEW_W + 4.0f || ey < -4.0f || ey > VIEW_H + 4.0f) continue;
        // hollow pixel circle; pulses bigger as it approaches hatching
        float p = 1.0f - (e->age / e->hatch_at);
        int r = (p > 0.15f) ? 1 : 2;
        u8g2.drawCircle((int)ex, (int)ey, r);
    }
}

// ---- public --------------------------------------------------------------
void render_init(void) {
    Wire.begin(OLED_SDA, OLED_SCL);
    u8g2.begin();
}

void render_frame(void) {
    u8g2.clearBuffer();

    if (g_world.n_worms > 0) camera_follow(&g_world.worms[0]);

    draw_food();
    draw_eggs();
    for (int i = 0; i < g_world.n_worms; i++) {
        const Worm* w = &g_world.worms[i];
        uint8_t alpha = 255;
        if (w->dying) alpha = (uint8_t)(255 * (1.0f - w->death_t / 2.5f));
        draw_worm(w, alpha);
    }

    u8g2.sendBuffer();
}
