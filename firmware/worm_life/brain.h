// brain.h — one C. elegans nervous system running as GRADED neurons over the
// full 302-neuron connectome. Each worm owns one Brain instance; the
// simulation advances it once per millisecond.
//
// Model (stylised but biologically motivated):
//   * Real C. elegans neurons are mostly NON-SPIKING: they use graded
//     potentials. We model each neuron's activity a in [0,1] with
//       tau * da/dt = -a + sigmoid( G * (J + tonic) - bias )
//     where J is the total input current (chemical synapses, gap junctions,
//     injected sensor drive, noise).
//   * chemical synapses: fan-in normalised so each neuron's total incoming
//     strength is comparable; sign from the presynaptic neurotransmitter
//     (GABAergic -> inhibitory).
//   * gap junctions: electrical coupling on the activity difference.
//   * muscles: the same graded integrator with a shorter time constant.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "connectome.h"

// ---- tunables ------------------------------------------------------------
#define K_GAP       0.05f    // gap junction coupling scale

// Runtime-tunable brain parameters (set before brain_init; defaults inside).
typedef struct {
    float sigmoid_gain;   // G: sigmoid steepness
    float tonic;          // background input to every neuron
    float bias;           // subtracted from G*J (sets resting activity)
    float noise_amp;      // uniform noise on neuron input, all neurons
    float sens_noise;     // extra noise on sensory neurons
    float tau_neuron;     // ms
    float tau_muscle;     // ms
} BrainParams;

void brain_set_params(BrainParams p);

// ---- a brain -------------------------------------------------------------
typedef struct {
    float a[N_NODES];       // activity 0..1 (neurons and muscles)
    float J_in[N_NODES];    // input current to integrate this ms
    uint32_t rng;
    uint32_t last_step_us;  // esp_timer us spent in the most recent step
} Brain;

void brain_init(Brain* b, uint32_t seed);
void brain_step(Brain* b);                       // advance one ms
uint32_t brain_last_step_us(void);               // us in most recent step
void brain_inject(Brain* b, int node, float amp);
float brain_rate(const Brain* b, int node);      // == activity
float brain_v(const Brain* b, int node);         // alias of rate

// group readouts (average activity of a class, 0..1)
float brain_body_wall_activity(const Brain* b);
float brain_pharyngeal_activity(const Brain* b);
float brain_vulval_activity(const Brain* b);

// node index by canonical name, -1 if absent
int node_index_by_name(const char* name);
