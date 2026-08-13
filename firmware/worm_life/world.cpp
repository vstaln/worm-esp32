// world.cpp — the worm's life.
#include "world.h"

#include <esp_random.h>
#include <math.h>
#include <string.h>

World g_world;

// ---- helpers -------------------------------------------------------------
static uint32_t wrng;
static inline float frandf(void) {
    wrng ^= wrng << 13; wrng ^= wrng >> 17; wrng ^= wrng << 5;
    return (float)(wrng & 0xFFFFFF) * (1.0f / 16777216.0f);
}

// ---- neuron handles (resolved once at init) ------------------------------
static int N_ASEL, N_ASER, N_AWCL, N_AWCR, N_ASHL, N_ASHR;   // sensors
static int N_AVBL, N_AVBR, N_PVCL, N_PVCR;                   // forward command
static int N_AVAL, N_AVAR, N_AVDL, N_AVDR, N_AVEL, N_AVER;   // backward command
static int N_RIML, N_RIMR, N_RIVL, N_RIVR;                   // turns
static int N_HSNL, N_HSNR;                                   // egg laying
static int N_NSML, N_NSMR, N_M4, N_M1, N_M5;                 // pharynx (pump)

// ---- odour field ---------------------------------------------------------
float world_conc(float x, float y) {
    float c = 0.0f;
    for (int i = 0; i < g_world.n_food; i++) {
        const Food* f = &g_world.food[i];
        float dx = x - f->x, dy = y - f->y;
        float d2 = dx * dx + dy * dy;
        float r2 = f->radius * f->radius;
        c += f->amount * expf(-d2 / (2.0f * r2));
    }
    return c;
}

static void spawn_food(void) {
    if (g_world.n_food >= MAX_FOOD) return;
    Food* f = &g_world.food[g_world.n_food++];
    f->x = 12 + frandf() * (WORLD_W - 24);
    f->y = 8 + frandf() * (WORLD_H - 16);
    f->radius = 3.0f + frandf() * 3.0f;
    f->amount = FOOD_AMOUNT * (0.6f + frandf() * 0.8f);
    f->grow = 0.4f;
}


static void birth_worm(Worm* w) {
    brain_init(&w->brain, esp_random());
    w->x = 8 + frandf() * (WORLD_W - 16);
    w->y = 8 + frandf() * (WORLD_H - 16);
    w->heading = frandf() * 6.2832f;
    w->body_phase = 0.0f;
    w->speed = 0.0f;
    w->fwd_act = w->bwd_act = 0.0f;
    w->conc_smooth = 0.0f;
    w->pirouette_t = w->pirouette_cd = 0.0f;
    w->hunger = 45 + frandf() * 30;       // born hungry
    w->age = 0.0f;
    w->lifespan = 150 + frandf() * 150;   // 2.5 - 5 minutes of worm life
    w->starve_t = 0.0f;
    w->alive = true;
    w->dying = false;
    w->death_t = 0.0f;
}

void world_init(void) {
    memset(&g_world, 0, sizeof(g_world));
    wrng = esp_random();

    N_ASEL = node_index_by_name("ASEL"); N_ASER = node_index_by_name("ASER");
    N_AWCL = node_index_by_name("AWCL"); N_AWCR = node_index_by_name("AWCR");
    N_ASHL = node_index_by_name("ASHL"); N_ASHR = node_index_by_name("ASHR");
    N_AVBL = node_index_by_name("AVBL"); N_AVBR = node_index_by_name("AVBR");
    N_PVCL = node_index_by_name("PVCL"); N_PVCR = node_index_by_name("PVCR");
    N_AVAL = node_index_by_name("AVAL"); N_AVAR = node_index_by_name("AVAR");
    N_AVDL = node_index_by_name("AVDL"); N_AVDR = node_index_by_name("AVDR");
    N_AVEL = node_index_by_name("AVEL"); N_AVER = node_index_by_name("AVER");
    N_RIML = node_index_by_name("RIML"); N_RIMR = node_index_by_name("RIMR");
    N_RIVL = node_index_by_name("RIVL"); N_RIVR = node_index_by_name("RIVR");
    N_HSNL = node_index_by_name("HSNL"); N_HSNR = node_index_by_name("HSNR");
    N_NSML = node_index_by_name("NSML"); N_NSMR = node_index_by_name("NSMR");
    N_M4   = node_index_by_name("M4");
    N_M1   = node_index_by_name("M1");
    N_M5   = node_index_by_name("M5");

    for (int i = 0; i < 3; i++) spawn_food();
    g_world.n_worms = 1;
    birth_worm(&g_world.worms[0]);
}

// ---- sensory input -------------------------------------------------------
static void sense_food(Worm* w) {
    // attraction gain: starving worms hunt hard, full worms roam
    float gain = 0.25f + 1.75f * (1.0f - w->hunger / 100.0f);
    if (w->dying) gain = 0.0f;

    float s = 5.0f;                       // sensor separation (px)
    float lx = w->x + s * cosf(w->heading + SENS_ANG);
    float ly = w->y + s * sinf(w->heading + SENS_ANG);
    float rx = w->x + s * cosf(w->heading - SENS_ANG);
    float ry = w->y + s * sinf(w->heading - SENS_ANG);
    float cl = world_conc(lx, ly);
    float cr = world_conc(rx, ry);
    float c0 = world_conc(w->x, w->y);

    float k = 0.020f * gain;              // current per conc unit (graded J)
    brain_inject(&w->brain, N_ASEL, cl * k);
    brain_inject(&w->brain, N_ASER, cr * k);
    brain_inject(&w->brain, N_AWCL, c0 * k * 0.5f);
    brain_inject(&w->brain, N_AWCR, c0 * k * 0.5f);
    // ASH: a mild slow "mood" baseline so the avoidance circuit stays alive
    brain_inject(&w->brain, N_ASHL, 0.5f + 0.3f * sinf(g_world.sim_time * 0.13f));
    brain_inject(&w->brain, N_ASHR, 0.5f + 0.3f * sinf(g_world.sim_time * 0.11f));

    // food present at mouth -> pharyngeal pumping. NSM is the serotonergic
    // food-in-pharynx sensor; M4/M1/M5 are the pump motor neurons that drive
    // the pharyngeal muscles (the real eating circuit in this connectome).
    bool at_food = c0 > 2.0f;
    float pump_drive = at_food ? 2.5f : 0.0f;
    brain_inject(&w->brain, N_NSML, pump_drive * 0.8f);
    brain_inject(&w->brain, N_NSMR, pump_drive * 0.8f);
    brain_inject(&w->brain, N_M4, pump_drive);
    brain_inject(&w->brain, N_M1, pump_drive * 0.6f);
    brain_inject(&w->brain, N_M5, pump_drive * 0.6f);
}

// ---- motor readout -------------------------------------------------------
static void drive_body(Worm* w, float dt) {
    Brain* b = &w->brain;
    // mean activity of the command populations (0..1 each)
    float fwd = 0.25f * (brain_rate(b, N_AVBL) + brain_rate(b, N_AVBR)
                       + brain_rate(b, N_PVCL) + brain_rate(b, N_PVCR));
    float bwd = (1.0f / 6.0f) * (brain_rate(b, N_AVAL) + brain_rate(b, N_AVAR)
                               + brain_rate(b, N_AVDL) + brain_rate(b, N_AVDR)
                               + brain_rate(b, N_AVEL) + brain_rate(b, N_AVER));
    float turn = 0.25f * (brain_rate(b, N_RIML) + brain_rate(b, N_RIMR)
                        + brain_rate(b, N_RIVL) + brain_rate(b, N_RIVR));

    // behavioural-state smoothing: ~140 ms EMA so the worm runs in sustained
    // bouts instead of jittering at the 1 ms time scale
    w->fwd_act += (fwd - w->fwd_act) * 0.007f;
    w->bwd_act += (bwd - w->bwd_act) * 0.007f;
    float drive = w->fwd_act - w->bwd_act;   // in [-1, 1]

    // hunger bias: starving worms search forward, full worms drift
    float hunger_bias = 1.0f - w->hunger / 100.0f;
    drive += 0.55f * hunger_bias - 0.05f;

    // pirouette: when the odour at the head drops relative to its recent level,
    // the real worm stops and reverses, then tries a new direction
    float conc_now = world_conc(w->x, w->y);
    w->conc_smooth += (conc_now - w->conc_smooth) * 0.005f;   // ~200 ms window
    if (w->pirouette_t > 0.0f) {
        w->pirouette_t -= dt;
        drive = -0.75f;
    } else {
        if (w->hunger < 80.0f && conc_now < w->conc_smooth * 0.90f &&
            w->pirouette_cd <= 0.0f && w->conc_smooth > 1.0f) {
            w->pirouette_t = 0.35f + frandf() * 0.4f;
            w->pirouette_cd = 2.5f + frandf() * 3.0f;
        }
    }
    if (w->pirouette_cd > 0.0f) w->pirouette_cd -= dt;
    if (drive > 1.0f) drive = 1.0f;
    if (drive < -1.0f) drive = -1.0f;

    // ageing worms slow down
    float vigour = 1.0f - 0.5f * (w->age / w->lifespan);
    if (vigour < 0.3f) vigour = 0.3f;
    if (w->dying) vigour = 0.0f;

    // steering: RELATIVE head-sensor differential (scale-invariant weathervane)
    float s = SENS_DIST;
    float lx = w->x + s * cosf(w->heading + SENS_ANG);
    float ly = w->y + s * sinf(w->heading + SENS_ANG);
    float rx = w->x + s * cosf(w->heading - SENS_ANG);
    float ry = w->y + s * sinf(w->heading - SENS_ANG);
    float cl = world_conc(lx, ly), cr = world_conc(rx, ry);
    float rel = (cl - cr) / (cl + cr + 2.0f);   // ~ -1..1, scale-free
    if (rel > 1.0f) rel = 1.0f;
    if (rel < -1.0f) rel = -1.0f;
    float steer = rel * STEER_GAIN;
    float rev_turn = (drive < -0.15f) ? REV_TURN * turn : TURN_GAIN * turn;

    // satiated worms wander: sensory steering fades out
    float wander = 0.5f + 0.5f * (w->hunger / 100.0f);
    float noise_turn = (frandf() - 0.5f) * 0.6f * wander;
    // exploration drive: full worms cruise off to roam; hunger drives search
    drive += 0.35f * (w->hunger / 100.0f);
    if (drive > 1.0f) drive = 1.0f;
    if (drive < -1.0f) drive = -1.0f;

    w->heading += (steer * (1.0f - 0.7f * wander) + rev_turn + noise_turn) * dt;
    // hungry worms move faster (active search); a HUNGRY worm slows and
    // settles to eat (real "dwelling" behaviour)
    float vigour2 = 0.55f + 0.60f * (1.0f - w->hunger / 100.0f);
    bool on_food = world_conc(w->x, w->y) > 2.0f;
    if (on_food && w->hunger < 85.0f) vigour2 *= 0.35f;  // dwell to eat
    w->speed = drive * BASE_SPEED * vigour * vigour2;
    // debug: expose components
    w->dbg_steer = steer * (1.0f - 0.7f * wander);
    w->dbg_turn = rev_turn;
    w->dbg_noise = noise_turn;
    w->dbg_drive = drive;
    w->x += w->speed * cosf(w->heading) * dt;
    w->y += w->speed * sinf(w->heading) * dt;

    // wrap around the arena edges
    if (w->x < -4) w->x = WORLD_W + 4;
    if (w->x > WORLD_W + 4) w->x = -4;
    if (w->y < -4) w->y = WORLD_H + 4;
    if (w->y > WORLD_H + 4) w->y = -4;

    // body wave: phase advances with forward speed, reverses when backing up
    float wave_speed = 0.10f + 0.004f * fabsf(w->speed);    w->body_phase += wave_speed * (drive >= -0.05f ? 1.0f : -1.0f);
}

// ---- eating / life cycle -------------------------------------------------
static void live_worm(Worm* w, float dt) {
    float c0 = world_conc(w->x, w->y);
    float phar = brain_pharyngeal_activity(&w->brain);
    float pump = (c0 > 2.0f && phar > 0.35f) ? phar : 0.0f;

    // eat: pumping converts food into hunger
    w->hunger += 12.0f * pump * dt;
    if (w->hunger > 100.0f) w->hunger = 100.0f;

    // deplete the food patch under the mouth
    for (int i = 0; i < g_world.n_food; i++) {
        Food* f = &g_world.food[i];
        float dx = w->x - f->x, dy = w->y - f->y;
        if (dx * dx + dy * dy < (f->radius * 0.8f) * (f->radius * 0.8f)) {
            f->amount -= 18.0f * pump * dt;
            if (f->amount <= 0.0f) {     // patch exhausted
                // swap-remove, then a new patch appears elsewhere
                g_world.food[i] = g_world.food[g_world.n_food - 1];
                g_world.n_food--;
                spawn_food();
                break;
            }
        }
    }

    // metabolism
    w->hunger -= 2.0f * dt;   // metabolism: ~50 s from full to empty
    if (w->hunger < 0.0f) w->hunger = 0.0f;

    // age / starvation
    w->age += dt;
    if (w->hunger <= 0.0f) {
        w->starve_t += dt;
        if (w->starve_t > 8.0f && w->alive) {
            w->alive = false; w->dying = true; w->death_t = 0.0f;
            w->death_reason = 0;
        }
    } else {
        w->starve_t = 0.0f;
    }
    if (w->age >= w->lifespan && w->alive) {
        w->alive = false; w->dying = true; w->death_t = 0.0f;
        w->death_reason = 1;
    }

    // egg laying: satiated + HSN active
    if (w->hunger > 85.0f && g_world.n_eggs < MAX_EGGS) {
        float hsn = brain_rate(&w->brain, N_HSNL) + brain_rate(&w->brain, N_HSNR);
        static float lay_timer = 0.0f;
        lay_timer += dt;
        if (hsn > 0.4f && lay_timer > 6.0f) {
            lay_timer = 0.0f;
            Egg* e = &g_world.eggs[g_world.n_eggs++];
            e->x = w->x + (frandf() - 0.5f) * 6.0f;
            e->y = w->y + (frandf() - 0.5f) * 6.0f;
            e->age = 0.0f;
            e->hatch_at = 25.0f + frandf() * 20.0f;
        }
    }

    // dying: fade out, then rebirth
    if (w->dying) {
        w->death_t += dt;
        if (w->death_t > 2.5f) {
            w->dying = false;
            g_world.n_worms--;            // death
            if (g_world.n_eggs > 0) {     // rebirth from an egg
                Egg* e = &g_world.eggs[g_world.n_eggs - 1];
                birth_worm(w);
                w->x = e->x; w->y = e->y;
                g_world.n_eggs--;
                g_world.n_worms++;
            } else if (g_world.n_worms < 1) {
                birth_worm(w);            // or a fresh worm
                g_world.n_worms++;
            }
        }
    }
}

// ---- hatch eggs, grow food ----------------------------------------------
static void world_background(float dt) {
    g_world.sim_time += dt;

    for (int i = 0; i < g_world.n_eggs; i++) {
        Egg* e = &g_world.eggs[i];
        e->age += dt;
        if (e->age > e->hatch_at && g_world.n_worms < MAX_WORMS) {
            birth_worm(&g_world.worms[g_world.n_worms++]);
            g_world.worms[g_world.n_worms - 1].x = e->x;
            g_world.worms[g_world.n_worms - 1].y = e->y;
            g_world.eggs[i] = g_world.eggs[g_world.n_eggs - 1];
            g_world.n_eggs--;
            i--;
        }
    }

    // keep the arena stocked
    if (g_world.n_food < MAX_FOOD && frandf() < 0.002f) spawn_food();
    for (int i = 0; i < g_world.n_food; i++) {
        Food* f = &g_world.food[i];
        f->amount += f->grow * dt;
        if (f->amount > FOOD_AMOUNT * 1.5f) f->amount = FOOD_AMOUNT * 1.5f;
    }
}

void world_step(void) {
    const float dt = 0.001f * (float)BRAIN_DT_MS;   // e.g. 2 ms

    for (int i = 0; i < g_world.n_worms; i++) {
        Worm* w = &g_world.worms[i];
        if (!w->dying) {
            sense_food(w);
            brain_step(&w->brain);
            drive_body(w, dt);
        } else {
            brain_step(&w->brain);        // brain keeps going while dying
            w->speed = 0.0f;
        }
        live_worm(w, dt);
    }

    if (g_world.n_worms == 0) {           // apocalypse fallback
        birth_worm(&g_world.worms[0]);
        g_world.n_worms = 1;
    }

    world_background(dt);
}
