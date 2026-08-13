// worm_life.ino — a living C. elegans on an ESP32 + 128x64 OLED.
//
// The worm's real 302-neuron connectome (Cook et al. 2019) runs as Izhikevich
// spiking neurons at 1 kHz on core 0; the OLED renderer runs at ~25 fps on
// core 1. Worms hunt food by chemotaxis (ASE sensory neurons), get hungry,
// eat (pharyngeal pumping), lay eggs (HSN), age, die and are reborn.
//
// Wiring (ESP32 classic): OLED SDA -> GPIO21, SCL -> GPIO22, VCC 3V3, GND.

#include "brain.h"
#include "render.h"
#include "world.h"

// ---- tasks ---------------------------------------------------------------
static TaskHandle_t brainTaskH, renderTaskH;

static void brain_task(void*) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        world_step();                          // BRAIN_DT_MS ms of life
        vTaskDelayUntil(&last, pdMS_TO_TICKS(BRAIN_DT_MS));
    }
}

static void render_task(void*) {
    uint32_t last_stats = 0;
    for (;;) {
        render_frame();
        // diagnostics over serial once per second
        uint32_t now = millis();
        if (now - last_stats > 1000) {
            last_stats = now;
            const Worm* w = &g_world.worms[0];
            Serial.printf(
                "t=%.0fs worm0: x=%.0f y=%.0f h=%.2f spd=%.1f hungry=%.0f age=%.0f/%.0f "
                "food=%d eggs=%d brains_us=%lu\n",
                g_world.sim_time, w->x, w->y, w->heading, w->speed, w->hunger,
                w->age, w->lifespan, g_world.n_food, g_world.n_eggs,
                (unsigned long)brain_last_step_us());
        }
        vTaskDelay(pdMS_TO_TICKS(40));         // 25 fps
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("🪱 worm life boot");

    world_init();
    render_init();

    xTaskCreatePinnedToCore(brain_task, "brain", 8192, NULL, 2, &brainTaskH, 0);
    xTaskCreatePinnedToCore(render_task, "render", 8192, NULL, 1, &renderTaskH, 1);
    Serial.println("tasks started");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));           // idle; everything runs in tasks
}
