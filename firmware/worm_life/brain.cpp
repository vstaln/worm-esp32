// brain.cpp — Izhikevich network over the full C. elegans connectome.
#include "brain.h"

#include <esp_timer.h>
#include <string.h>

// ---- shared parameter tables (node-dependent only) -----------------------
float BRAIN_A[N_NODES], BRAIN_B[N_NODES], BRAIN_C[N_NODES], BRAIN_D[N_NODES];
int8_t BRAIN_SIGN[N_NODES];
bool BRAIN_IS_MUSCLE[N_NODES];

static float step_us = 0.0f;

int node_index_by_name(const char* name) {
    for (int i = 0; i < N_NODES; i++)
        if (strcmp(node_meta[i].name, name) == 0) return i;
    return -1;
}

static inline float randf(uint32_t* s) {   // uniform in [0,1)
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return (float)(*s & 0xFFFFFF) * (1.0f / 16777216.0f);
}

void brain_init(Brain* b, uint32_t seed) {
    static bool tables_ready = false;
    if (!tables_ready) {
        for (int i = 0; i < N_NODES; i++) {
            const NodeMeta* m = &node_meta[i];
            BRAIN_IS_MUSCLE[i] = (m->cls != NODE_NEURON);
            BRAIN_SIGN[i] = m->sign;
            if (BRAIN_IS_MUSCLE[i]) {
                BRAIN_A[i] = BRAIN_B[i] = BRAIN_C[i] = BRAIN_D[i] = 0.0f;
            } else {
                bool inhibitory = (m->nt == NT_GABA);
                if (inhibitory) {          // fast spiking
                    BRAIN_A[i] = 0.10f; BRAIN_B[i] = 0.20f;
                    BRAIN_C[i] = -65.0f; BRAIN_D[i] = 2.0f;
                } else {                   // regular spiking
                    BRAIN_A[i] = 0.02f; BRAIN_B[i] = 0.20f;
                    BRAIN_C[i] = -65.0f; BRAIN_D[i] = 8.0f;
                }
            }
        }
        tables_ready = true;
    }

    b->rng = seed ? seed : 0x9E3779B9u;
    for (int i = 0; i < N_NODES; i++) {
        if (BRAIN_IS_MUSCLE[i]) {
            b->v[i] = 0.0f;
            b->u[i] = 0.0f;
        } else {
            b->v[i] = BRAIN_C[i] + randf(&b->rng) * 10.0f;
            b->u[i] = BRAIN_B[i] * b->v[i];
        }
        b->rate[i] = 0.0f;
        b->spike[i] = 0;
        b->I_in[i] = 0.0f;
    }
}

void brain_inject(Brain* b, int node, float amp) {
    if (node >= 0 && node < N_NODES) b->I_in[node] += amp;
}

float brain_rate(const Brain* b, int node) { return b->rate[node]; }
float brain_v(const Brain* b, int node) { return b->v[node]; }

void brain_step(Brain* b) {
    uint32_t t0 = esp_timer_get_time();
    uint32_t* rng = &b->rng;

    // 1) integrate all nodes ------------------------------------------------
    for (int i = 0; i < N_NODES; i++) {
        float I = b->I_in[i];
        if (BRAIN_IS_MUSCLE[i]) {
            // graded muscle: low-pass of input, tau ~ 4 ms
            b->v[i] += (I - b->v[i]) * 0.25f;
        } else {
            // noise: sensory neurons more than others
            float namp = (node_meta[i].func & F_SENSORY) ? SENS_NOISE : NOISE_AMP;
            I += namp * (2.0f * randf(rng) - 1.0f);

            // Izhikevich
            float vv = b->v[i];
            vv += 0.04f * vv * vv + 5.0f * vv + 140.0f - b->u[i] + I;
            b->v[i] = vv;
            b->u[i] += BRAIN_A[i] * (BRAIN_B[i] * vv - b->u[i]);
            if (vv >= 30.0f) {
                b->v[i] = BRAIN_C[i];
                b->u[i] += BRAIN_D[i];
                b->spike[i] = 1;
                b->rate[i] += (1.0f - b->rate[i]) * 0.10f;   // ~10 ms window
            } else {
                b->spike[i] = 0;
                b->rate[i] *= 0.90f;
            }
        }
    }

    // 2) deliver synapses ---------------------------------------------------
    memset(b->I_in, 0, sizeof(b->I_in));

    // chemical: presynaptic spike -> postsynaptic current (sign from pre)
    const float* v = b->v;
    float* I_in = b->I_in;
    for (int e = 0; e < N_EDGES_CHEM; e++) {
        const SynEdge* ed = &chem_edges[e];
        if (b->spike[ed->src])
            I_in[ed->dst] += (float)ed->w * K_CHEM * (float)BRAIN_SIGN[ed->src];
    }

    // gap junctions: bidirectional electrical coupling
    for (int e = 0; e < N_EDGES_GAP; e++) {
        const SynEdge* ed = &gap_edges[e];
        float diff = (v[ed->dst] - v[ed->src]) * K_GAP;
        I_in[ed->src] += diff;
        I_in[ed->dst] -= diff;
    }

    step_us = (float)(esp_timer_get_time() - t0);
}

float brain_last_step_us(void) { return step_us; }

// ---- group activity readouts ---------------------------------------------
static float group_activity(const Brain* b, uint8_t cls) {
    float acc = 0.0f;
    int n = 0;
    for (int i = 0; i < N_NODES; i++)
        if (node_meta[i].cls == cls) { acc += b->v[i]; n++; }
    return n ? acc / n : 0.0f;
}

float brain_body_wall_activity(const Brain* b) { return group_activity(b, NODE_BODY_MUSCLE); }
float brain_pharyngeal_activity(const Brain* b) { return group_activity(b, NODE_PHAR_MUSCLE); }
float brain_vulval_activity(const Brain* b) { return group_activity(b, NODE_VULVAL_MUSCLE); }
