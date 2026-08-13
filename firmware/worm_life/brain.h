// brain.h — one C. elegans 302-neuron connectome running as Izhikevich spiking
// neurons, plus graded (non-spiking) muscle cells. Each worm owns one Brain
// instance; the simulation advances it once per millisecond.
//
// Model choices (stylised, not electrophysiology):
//   * Izhikevich RS (excitatory) / FS (inhibitory) parameters
//   * chemical synapses: weight = raw synapse count * K_CHEM * sign, delivered
//     to the postsynaptic neuron on the ms AFTER the presynaptic spike
//   * gap junctions: bidirectional electrical coupling using membrane potential
//   * muscles: graded low-pass integrators of synaptic input (no spikes); they
//     still participate in gap junctions via their "potential"
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "connectome.h"

// ---- tunables ------------------------------------------------------------
#define K_CHEM      0.020f   // chemical weight scale (count -> current)
#define K_GAP       0.012f   // gap junction coupling scale
#define NOISE_AMP   0.60f    // uniform noise on every neuron each ms
#define SENS_NOISE  1.50f    // extra noise on sensory neurons

// ---- a brain -------------------------------------------------------------
// Parameter arrays (a,b,c,d), sign and muscle flags depend only on the node
// table, so they are shared; the per-brain state below is what makes each
// worm's nervous system independent.
typedef struct {
    float v[N_NODES];       // membrane potential / muscle activity
    float u[N_NODES];       // Izhikevich recovery variable
    float I_in[N_NODES];    // current to integrate this ms
    float rate[N_NODES];    // smoothed firing rate
    uint8_t spike[N_NODES];
    uint32_t rng;
} Brain;

extern float BRAIN_A[N_NODES], BRAIN_B[N_NODES];
extern float BRAIN_C[N_NODES], BRAIN_D[N_NODES];
extern int8_t BRAIN_SIGN[N_NODES];
extern bool BRAIN_IS_MUSCLE[N_NODES];

void brain_init(Brain* b, uint32_t seed);
void brain_step(Brain* b);                       // advance one ms
void brain_inject(Brain* b, int node, float amp);
float brain_rate(const Brain* b, int node);      // smoothed firing rate
float brain_v(const Brain* b, int node);         // potential / activity
float brain_last_step_us(void);                  // diagnostic

// group readouts (average activity of a muscle class)
float brain_body_wall_activity(const Brain* b);
float brain_pharyngeal_activity(const Brain* b);
float brain_vulval_activity(const Brain* b);

// node index by canonical name, -1 if absent
int node_index_by_name(const char* name);
