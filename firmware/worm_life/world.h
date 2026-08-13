// world.h — the worm's world: a 2-D arena with food patches, worms each driven
// by their own (simulated) 302-neuron nervous system, and the life cycle:
// hunger, eating, egg-laying, aging, death, rebirth.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "brain.h"

#define WORLD_W 128
#define WORLD_H 56

#define MAX_FOOD     6
#define MAX_EGGS    14
#define MAX_WORMS    2            // CPU-limited: each worm runs its own brain at 1 kHz
#define FOOD_AMOUNT 100.0f

// ---- worm ----------------------------------------------------------------
typedef struct {
    Brain brain;         // this worm's own nervous system
    float x, y;          // head position (px)
    float heading;       // radians
    float body_phase;    // travelling wave phase
    float speed;         // px/s along heading (+forward, -backward)
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
#define BASE_SPEED   22.0f   // px/s at full forward drive
#define TURN_GAIN    2.6f    // rad/s per unit turn drive
#define STEER_GAIN   0.35f   // rad/s per unit (L-R) odour difference
#define REV_TURN     4.2f    // extra turning while reversing
