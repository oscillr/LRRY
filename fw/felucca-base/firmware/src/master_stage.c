/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* L/RRY: the MASTER STAGE, moved out of fx.c unchanged (phase 4.1): the limiter and soft clip, the DC blocker, the MASTER knob law and the level the mix is made at.
 * fx.c includes this file where the block used to be (one translation unit, so the code is byte for byte what it was). It is the one part of fx.c L/RRY keeps whole. */

/* master: peak limiter in front of the soft clipper. Fast attack (~0.1 ms),
 * ~150 ms release, threshold where tanh is still nearly linear, so chords
 * get quieter instead of crushed. */
#define LIM_T 18000
static int32_t lim_env = LIM_T;
/* The mix is always made at the reference level MASTER_USB_REF (the default knob position, song.master_q12 = 2048, gain 0.5), whatever the MASTER
 * knob says: the limiter and the soft clip see the same signal, and USB audio takes it (audio.c uac_tap) at that fixed level. MASTER is a volume on what
 * the DAC gets, after the USB tap: audio.c master_dac (a per-sample glide, 24-bit result). (Up to DUELL 0.2.24 MENU > USB LEVEL chose between this
 * "FIXED" and MASTER before master_out, where MASTER's level stepped once per block; the row is removed, FIXED is the only way.) */
/* DUELL 1.1 BIT DEPTH (release_drafts/BIT_DEPTH_AUDIT_V1.md): the level stages used to cut the signal's two low bits before the multiplication, ((x >> 2) * g) >> 10. The cut moves to the gain, whose low bits only
 * make a gain error of at most 0.05 %: (x * LV4(g)) >> 10 is the same gain with every bit of the signal kept; the product is as large as before, so nothing new can overflow */
#define LV4(g) (((g) + 2) >> 2)
#define MASTER_FULL 4096                /* main.c: the MASTER knob's top, Q12 */
#define MASTER_USB_REF 2048             /* the level master_out gets (Q12), the default master_q12 */
/* The level mix_block makes the mix at (Q12). The device: always MASTER_USB_REF. HOST TESTS ONLY (-DHOST_MIX_FOLLOWS_MASTER=1, host_tests.sh): the level is song.master_q12 unless
 * a test clears host_mix_follows_master, as every host test rendered before 0.2.25 (hostsim.c sets master_q12 = 4096, the renders and their pinned fingerprints are at that level;
 * a few tests set another value on purpose). tests/audio_test.c, click_test.c and master_hunt.c clear it: they run the device's own path (audio_block). The device build
 * never defines the macro: no variable, no branch, the constant. */
#ifdef HOST_MIX_FOLLOWS_MASTER
int host_mix_follows_master = 1;
#define MIX_MG() (host_mix_follows_master ? (int32_t)song.master_q12 : (int32_t)MASTER_USB_REF)
#else
#define MIX_MG() ((int32_t)MASTER_USB_REF)
#endif
/* the MASTER pot (ADC 0..1023, smoothed in main.c) to its level, square law: 0 .. 2048 (Q12). DUELL 0.2.30 (owner decision 2026-10-20): the TOP of the knob is unity (0 dB, 2048 = the
 * level the knob's default position gave until 0.2.29, MASTER_USB_REF); there is no gain above it (no boost, so no push into the limiter and no clip after it). The old law (k^2 >> 8, 0 .. 4088,
 * 2048 at 71 % of the knob) is halved and lifted by 1/64 so the top reaches 2048 a hair below the pot's physical end (pot 1016 of 1023: a pot that does not quite reach 1023 still gets 0 dB),
 * and held there. The quiet end keeps the square law's fine steps (master_g16 below has 4 more bits) and the 11.6 ms glide of audio.c master_dac */
#define MASTER_TOP 2048u
static inline uint32_t master_of_pot(uint32_t k10)
{
    uint32_t g = (k10 * k10) >> 8;
    g = (g + (g >> 6)) >> 1;
    return g > MASTER_TOP ? MASTER_TOP : g;
}
/* DUELL 0.2.25: the same law at 4 more bits, from the smoothed knob (main loop: master.c, the pot x16): the level in Q16 (0 .. 32,768 since 0.2.30; was 65,412). song.master_q12 is its upper 12 bits (the
 * whole-step value everything else reads); master_g16 is the whole value, which only audio.c master_dac uses: at the quiet end of the knob one whole Q12 step is 33 % (3 -> 4), a
 * step of 10 dB in the volume that no glide can make fine; with the fraction the volume moves in 1/16 steps of that. The main loop writes master_g16 first and song.master_q12
 * after it, one 32-bit store each; the ISR uses master_g16 only while its upper 12 bits equal song.master_q12 (otherwise the whole-step value alone: a torn pair, or a test
 * or another writer that set song.master_q12 only, is read as the whole step, never as a mix of the two) */
static inline uint32_t master_of_knob16(uint32_t k16)
{
    uint32_t g = (k16 * k16) >> 12;
    g = (g + (g >> 6)) >> 1;                            /* (0.2.30: the top of the knob is unity, see master_of_pot) */
    return g > MASTER_TOP * 16u ? MASTER_TOP * 16u : g;
}
static volatile uint32_t master_g16;
static int32_t dc_l, dc_r, dce_l, dce_r;

/* DC blocker (~2 Hz), always on: a leaky integrator of the input (Q6 state) subtracted from it.
 * The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next one, so no
 * part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (A rounded step of (x - dc) / 4096 would stop moving at |x - dc| < 2048 and leave an offset of up
 * to +-31 at the output after silence.) */
static inline int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = (x << 6) - *dc + *err, d = e >> 12;
    *err = e - (d << 12);
    *dc += d;
    return x - ((*dc + 32) >> 6);
}

static inline void master_out(int32_t *l, int32_t *r)
{
    int32_t al, ar, a;
    *l = dc_block(*l, &dc_l, &dce_l);
    *r = dc_block(*r, &dc_r, &dce_r);
    al = *l < 0 ? -*l : *l;
    ar = *r < 0 ? -*r : *r;
    a = al > ar ? al : ar;
    if (a > lim_env)
        lim_env += (a - lim_env) >> 2;
    else if (lim_env > LIM_T)
        lim_env -= ((lim_env - LIM_T) >> 12) + 1;
    if (lim_env > LIM_T) {
        int32_t g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)lim_env);   /* < 32768 */
        *l = ((*l >> 4) * g) >> 11;                      /* >> 4 first: |l| may be far above Q15 */
        *r = ((*r >> 4) * g) >> 11;
    }
    *l = softclip(*l);
    *r = softclip(*r);
}

/* SPLIT (DUELL step 5): master_out with each channel its own limiter (a linked one would duck part 2 on part 1's peaks: part 1's
 * envelope on the right). The DC blocker and the soft clip are per channel already */
static int32_t lim_env_r = LIM_T;
static inline int32_t lim_one(int32_t x, int32_t *env)
{
    int32_t a = x < 0 ? -x : x;
    if (a > *env)
        *env += (a - *env) >> 2;
    else if (*env > LIM_T)
        *env -= ((*env - LIM_T) >> 12) + 1;
    if (*env > LIM_T) {
        int32_t g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)*env);
        x = ((x >> 4) * g) >> 11;
    }
    return x;
}
