// brain.cpp — graded-potential network over the full C. elegans connectome.
// The hot loops below dominate the whole simulation's CPU budget; force
// aggressive optimisation on THIS translation unit only (the rest of the
// sketch stays at the platform default), so builds stay fast.
#pragma GCC optimize("O2", "fast")

#include "brain.h"

#include <esp_timer.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// brain step period in ms is BRAIN_DT_MS (see brain.h).

// fast sigmoid: 256-entry LUT over x in [-8, 8] + linear interpolation
// (~5 cycles vs ~100+ for libm expf) — the hot loop's biggest win.
#define SIG_LUT_N    256
#define SIG_X_MIN    (-8.0f)
#define SIG_X_MAX    (8.0f)
static float g_sig_lut[SIG_LUT_N + 1];

static inline float fast_sigmoid(float x) {
    if (x <= SIG_X_MIN) return 0.000335f;
    if (x >= SIG_X_MAX) return 0.999665f;
    float t = (x - SIG_X_MIN) * (SIG_LUT_N / (SIG_X_MAX - SIG_X_MIN));
    int i = (int)t;
    float frac = t - (float)i;
    return g_sig_lut[i] + frac * (g_sig_lut[i + 1] - g_sig_lut[i]);
}

// ---- defaults ------------------------------------------------------------
#define GAIN_DEF   4.0f
#define TONIC_DEF  0.5f
#define BIAS_DEF   2.0f
#define NOISE_DEF  0.15f
#define SENS_DEF   0.30f
#define TAU_N_DEF  14.0f
#define TAU_M_DEF  6.0f

// per-node chemical output sign (+1 excitatory / -1 inhibitory)
static int8_t sign[N_NODES];

// packed edge arrays: single contiguous stream in RAM (built once at init).
// The raw SynEdge lives in flash; we repack with the fan-in-normalised float
// weight so the hot loop reads one 8-byte struct per edge (no separate
// weight array, no branch). ~58 KB total, fits in the ~270 KB free SRAM.
typedef struct { uint16_t src, dst; float w; } PackedEdge;
static PackedEdge* g_chem = NULL;   // N_EDGES_CHEM entries
static PackedEdge* g_gap = NULL;    // N_EDGES_GAP entries

// per-neuron time constants (ms) and muscle flag
static float tau_ms[N_NODES];
static bool is_muscle[N_NODES];

static BrainParams g_p = {GAIN_DEF, TONIC_DEF, BIAS_DEF, NOISE_DEF, SENS_DEF,
                          TAU_N_DEF, TAU_M_DEF};
static uint32_t g_last_step_us = 0;
void brain_set_params(BrainParams p) { g_p = p; }

static inline float randf(uint32_t* s) {   // uniform in [0,1)
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return (float)(*s & 0xFFFFFF) * (1.0f / 16777216.0f);
}

int node_index_by_name(const char* name) {
    for (int i = 0; i < N_NODES; i++)
        if (strcmp(node_meta[i].name, name) == 0) return i;
    return -1;
}

void brain_init(Brain* b, uint32_t seed) {
    static bool tables_ready = false;
    if (!tables_ready) {
        // build the sigmoid LUT (one-time, single-threaded during setup)
        for (int i = 0; i <= SIG_LUT_N; i++) {
            float x = SIG_X_MIN + (SIG_X_MAX - SIG_X_MIN) * (float)i / (float)SIG_LUT_N;
            g_sig_lut[i] = 1.0f / (1.0f + expf(-x));
        }
        for (int i = 0; i < N_NODES; i++) {
            is_muscle[i] = (node_meta[i].cls != NODE_NEURON);
            tau_ms[i] = is_muscle[i] ? g_p.tau_muscle : g_p.tau_neuron;
            sign[i] = node_meta[i].sign;
        }

        // fan-in normalisation: total incoming strength comparable per neuron
        static uint32_t total_in[N_NODES];
        memset(total_in, 0, sizeof(total_in));
        for (int e = 0; e < N_EDGES_CHEM; e++)
            total_in[chem_edges[e].dst] += chem_edges[e].w;
        g_chem = (PackedEdge*)malloc(N_EDGES_CHEM * sizeof(PackedEdge));
        for (int e = 0; e < N_EDGES_CHEM; e++) {
            const SynEdge* ed = &chem_edges[e];
            float norm = (float)ed->w /
                         (float)(total_in[ed->dst] ? total_in[ed->dst] : 1);
            g_chem[e].src = ed->src;
            g_chem[e].dst = ed->dst;
            g_chem[e].w = norm * (float)sign[ed->src];
        }
        g_gap = (PackedEdge*)malloc(N_EDGES_GAP * sizeof(PackedEdge));
        for (int e = 0; e < N_EDGES_GAP; e++) {
            g_gap[e].src = gap_edges[e].src;
            g_gap[e].dst = gap_edges[e].dst;
            g_gap[e].w = K_GAP;
        }
        tables_ready = true;
    }

    b->rng = seed ? seed : 0x9E3779B9u;
    for (int i = 0; i < N_NODES; i++) {
        b->a[i] = randf(&b->rng) * 0.2f;
        b->J_in[i] = 0.0f;
    }
}

void brain_inject(Brain* b, int node, float amp) {
    if (node >= 0 && node < N_NODES) b->J_in[node] += amp;
}

float brain_rate(const Brain* b, int node) { return b->a[node]; }
float brain_v(const Brain* b, int node) { return b->a[node]; }

void brain_step(Brain* b) {
    uint32_t t0 = esp_timer_get_time();
    const float G = g_p.sigmoid_gain;
    const float bias = g_p.bias;
    const float ton = g_p.tonic;
    uint32_t* rng = &b->rng;

    // 1) integrate activities ----------------------------------------------
    float a_mean = 0.0f;
    for (int i = 0; i < N_NODES; i++) a_mean += b->a[i];
    a_mean /= (float)N_NODES;

    for (int i = 0; i < N_NODES; i++) {
        float J = b->J_in[i] + ton;
        float namp = (node_meta[i].func & F_SENSORY) ? g_p.sens_noise : g_p.noise_amp;
        J += namp * (2.0f * randf(rng) - 1.0f);

        // sigmoid transfer with bias: rest (J=0) -> sigmoid(-bias)
        float x = G * J - bias;
        float sig = fast_sigmoid(x);
        float tau = tau_ms[i];
        b->a[i] += (sig - b->a[i]) * ((float)BRAIN_DT_MS / tau);  // Euler
    }

    // 2) deliver synapses ---------------------------------------------------
    memset(b->J_in, 0, sizeof(b->J_in));

    // chemical: graded (continuous) — weight * (pre activity MINUS population
    // mean): mean-centred drive creates relative dynamics (winners/losers)
    // instead of all-or-nothing saturation.
    const float* a = b->a;
    float* J_in = b->J_in;
    const PackedEdge* chem = g_chem;
    const float am = a_mean;
    for (int e = 0; e < N_EDGES_CHEM; e++) {
        const PackedEdge* ed = &chem[e];
        J_in[ed->dst] += ed->w * (a[ed->src] - am);
    }

    // gap junctions: electrical coupling on the activity difference
    const PackedEdge* gap = g_gap;
    for (int e = 0; e < N_EDGES_GAP; e++) {
        const PackedEdge* ed = &gap[e];
        float diff = (a[ed->dst] - a[ed->src]) * ed->w;
        J_in[ed->src] += diff;
        J_in[ed->dst] -= diff;
    }

    b->last_step_us = (uint32_t)(esp_timer_get_time() - t0);
    g_last_step_us = b->last_step_us;
}

// microseconds spent in the most recent brain_step (real-time headroom check)
uint32_t brain_last_step_us(void) { return g_last_step_us; }

// ---- group activity readouts ---------------------------------------------
static float group_activity(const Brain* b, uint8_t cls) {
    float acc = 0.0f;
    int n = 0;
    for (int i = 0; i < N_NODES; i++)
        if (node_meta[i].cls == cls) { acc += b->a[i]; n++; }
    return n ? acc / n : 0.0f;
}

float brain_body_wall_activity(const Brain* b) { return group_activity(b, NODE_BODY_MUSCLE); }
float brain_pharyngeal_activity(const Brain* b) { return group_activity(b, NODE_PHAR_MUSCLE); }
float brain_vulval_activity(const Brain* b) { return group_activity(b, NODE_VULVAL_MUSCLE); }
