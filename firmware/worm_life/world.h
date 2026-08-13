// world.h — the worm's world: a 2-D arena with food patches, worms each driven
// by their own (simulated) 302-neuron nervous system, and the life cycle:
// hunger, eating, egg-laying, aging, death, rebirth.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "brain.h"

#define WORLD_W 128
#define WORLD_H 56

#define MAX_FOOD     4
#define MAX_EGGS     8
#define MAX_WORMS    2            // CPU-limited: each worm runs its own brain at 250 Hz
#define FOOD_AMOUNT 60.0f

// ---- worm ----------------------------------------------------------------
typedef struct {
    Brain brain;         // this worm's own nervous system
    float x, y;          // head position (px)
    float heading;       // radians
    float body_phase;    // travelling wave phase
    float speed;         // px/s along heading (+forward, -backward)
    float fwd_act, bwd_act;   // smoothed command-neuron drive (EMA)
    float conc_smooth;   // smoothed odour at head (~200 ms window)
    float pirouette_t;   // seconds remaining in reversal
    float pirouette_cd;  // cooldown between reversals
    float dbg_steer, dbg_turn, dbg_noise, dbg_drive;
    float hunger;        // 0 = starving, 100 = full
    float age;           // seconds
    float lifespan;      // seconds (randomised at birth)
    float starve_t;      // seconds at zero hunger
    bool alive;
    bool dying;
    float death_t;       // seconds spent dying
    int  death_reason;   // 0 starvation, 1 old age
} Worm;

typedef struct {
    float x, y, radius, amount;
    float grow;          // amount regrown per second while uneaten
} Food;

typedef struct {
    float x, y;
    float age;           // seconds since laid
    float hatch_at;      // seconds until hatch
} Egg;

typedef struct {
    Worm worms[MAX_WORMS];
    Food food[MAX_FOOD];
    Egg  eggs[MAX_EGGS];
    int  n_worms, n_food, n_eggs;
    float sim_time;
} World;

extern World g_world;

void world_init(void);
// one millisecond of world + brain simulation for every worm
void world_step(void);

// odour concentration at a point (sum of food Gaussians)
float world_conc(float x, float y);

// ---- tunables ------------------------------------------------------------
#define BASE_SPEED   26.0f   // px/s at full forward drive
#define TURN_GAIN    0.15f   // rad/s per unit turn drive (subtle exploration)
#define STEER_GAIN   1.3f    // rad/s per unit (L-R) odour difference
#define REV_TURN     2.2f    // extra turning while reversing
#define SENS_ANG     0.6f    // sensor cone half-angle from heading (rad)
#define SENS_DIST    6.0f    // sensor distance ahead of head (px)
