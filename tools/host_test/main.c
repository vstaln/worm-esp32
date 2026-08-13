// Host-side verification + tuning harness for the worm brain + life sim.
// Compiles brain.cpp + world.cpp + connectome_data.c with stub ESP-IDF headers
// so the whole simulation can be exercised on a desktop/phone CPU without
// hardware.
//
// Usage:
//   worm_test <ms>              run one sim and print detailed trace
//   worm_test sweep             sweep brain params, pick the liveliest
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "world.h"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void run_sim(long steps, BrainParams p, float* out_path_len,
                    float* out_min_food, float* out_avg_speed,
                    float* out_max_fwd, float* out_max_bwd, float* out_ate) {
    brain_set_params(p);
    world_init();

    int idx_avb = node_index_by_name("AVBL");
    int idx_ava = node_index_by_name("AVAL");

    float path = 0.0f, min_food = 1e9f, speed_acc = 0.0f, max_fwd = 0, max_bwd = 0;
    float max_hunger = -1.0f;
    float px = 0, py = 0;
    for (long t = 0; t < steps; t++) {
        world_step();
        Worm* w = &g_world.worms[0];
        if (t > 0) path += hypotf(w->x - px, w->y - py);
        px = w->x; py = w->y;
        speed_acc += fabsf(w->speed);
        float f = brain_rate(&w->brain, idx_avb);
        float b = brain_rate(&w->brain, idx_ava);
        if (f > max_fwd) max_fwd = f;
        if (b > max_bwd) max_bwd = b;
        if (w->hunger > max_hunger) max_hunger = w->hunger;
        for (int i = 0; i < g_world.n_food; i++) {
            float dx = w->x - g_world.food[i].x, dy = w->y - g_world.food[i].y;
            float d = hypotf(dx, dy);
            if (d < min_food) min_food = d;
        }
    }
    *out_path_len = path;
    *out_min_food = min_food;
    *out_avg_speed = speed_acc / (float)steps;      // px/s
    *out_max_fwd = max_fwd;
    *out_max_bwd = max_bwd;
    *out_ate = (max_hunger > 70.0f) ? 1.0f : 0.0f;
}

static void sweep(void) {
    const long steps = 20000;    // 20 s per combo
    float gain[]  = {4.0f, 6.0f, 8.0f};
    float tonic[] = {0.0f, 0.5f, 1.0f};
    float bias[]  = {1.5f, 2.0f, 2.5f};
    float noise[] = {0.10f, 0.25f};

    printf("=== param sweep (20s each): gain x tonic x bias x noise ===\n");
    printf("%6s %6s %6s %6s | %8s %7s %8s %6s %6s %4s\n",
           "gain", "tonic", "bias", "noise", "path_px", "minfood", "avgspd", "fwd", "bwd", "ate");
    float best_score = -1;
    BrainParams best = {0};
    float best_min_food = 1e9;

    for (float g : gain)
        for (float ton : tonic)
            for (float bi : bias)
                for (float noi : noise) {
                    BrainParams p = {g, ton, bi, noi, 2.5f * noi, 14.0f, 6.0f};
                    float path, mf, spd, fw, bw, ate;
                    run_sim(steps, p, &path, &mf, &spd, &fw, &bw, &ate);
                    // score: move + find food + don't sit still
                    float score = spd * (1.0f / (1.0f + mf)) * (1.0f + 0.5f * ate);
                    printf("%6.1f %6.1f %6.1f %6.2f | %8.1f %7.1f %8.1f %6.2f %6.2f %4.0f  score=%5.1f\n",
                           g, ton, bi, noi, path, mf, spd, fw, bw, ate, score);
                    if (score > best_score) { best_score = score; best = p; best_min_food = mf; }
                }

    printf("\n=== best: gain=%.1f tonic=%.1f bias=%.1f noise=%.2f (minfood=%.1f) ===\n",
           best.sigmoid_gain, best.tonic, best.bias, best.noise_amp, best_min_food);

    // long run with the best params
    float path, mf, spd, fw, bw, ate;
    run_sim(60000, best, &path, &mf, &spd, &fw, &bw, &ate);
    printf("60s with best: path=%.0fpx minfood=%.1f avgspd=%.1fpx/s fwd=%.2f bwd=%.2f ate=%d\n",
           path, mf, spd, fw, bw, (int)ate);
}

static void trace(long steps) {
    brain_set_params((BrainParams){6.0f, 0.3f, 2.0f, 0.20f, 0.4f, 14.0f, 6.0f});
    world_init();
    double t0 = now_ms();

    int idx_ase = node_index_by_name("ASEL");
    int idx_ava = node_index_by_name("AVAL");
    int idx_avb = node_index_by_name("AVBL");
    int idx_hsn = node_index_by_name("HSNL");
    int idx_mc = node_index_by_name("MC");

    int report_every = (int)(steps / 20);
    if (report_every < 1) report_every = 1;
    printf("=== worm life host test: %ld ms (%ld s) ===\n", steps, steps / 1000);

    for (long t = 0; t < steps; t++) {
        world_step();
        Worm* w = &g_world.worms[0];
        if (t % report_every == 0) {
            printf(
                "t=%5ldms  worm: x=%6.1f y=%6.1f h=%5.2f spd=%5.1f hungry=%4.0f "
                "age=%4.0f food=%d eggs=%d | ASEL=%5.2f AVA=%5.2f AVB=%5.2f "
                "HSN=%5.2f MC=%5.2f | body=%4.1f phar=%4.1f | dbg: st=%.2f tu=%.2f nz=%.2f dr=%.2f\n",
                t, w->x, w->y, w->heading, w->speed, w->hunger, w->age,
                g_world.n_food, g_world.n_eggs,
                brain_rate(&w->brain, idx_ase), brain_rate(&w->brain, idx_ava),
                brain_rate(&w->brain, idx_avb), brain_rate(&w->brain, idx_hsn),
                brain_rate(&w->brain, idx_mc),
                brain_body_wall_activity(&w->brain),
                brain_pharyngeal_activity(&w->brain),
                w->dbg_steer, w->dbg_turn, w->dbg_noise, w->dbg_drive);
        }
    }

    Worm* w = &g_world.worms[0];
    double dt_s = (now_ms() - t0) / 1000.0;
    printf("\n=== summary ===\n");
    printf("worm alive=%d dying=%d reason=%d  n_worms=%d eggs=%d\n",
           w->alive, w->dying, w->death_reason, g_world.n_worms, g_world.n_eggs);
    printf("final pos (%.1f, %.1f) heading=%.2f\n", w->x, w->y, w->heading);
    printf("sim wall time: %.1f s -> %.2f x realtime (host ~4-core aarch64)\n",
           dt_s, dt_s / (steps / 1000.0));
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "sweep") == 0) { sweep(); return 0; }
    long steps = (argc > 1) ? atol(argv[1]) : 60000L;
    trace(steps);
    return 0;
}
