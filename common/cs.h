/*
 * cs.h — bare-metal phase-based ranging on nRF54L15, shared by initiator and reflector.
 *
 * Register names checked against nRF54L15/L10/L05 Datasheet v1.0, section 8.17 (RADIO).
 * Items still marked CHECK are NOT described in the datasheet (it lists the CS registers
 * but has no functional description of the CS sequence) and must be found by experiment.
 *
 * What the datasheet confirms:
 *   - Unmodulated tone = TXEN without START: RADIO sends a carrier in TXIDLE (8.17.6).
 *   - CSTONES block: MODE (TPM bit0, TFM bit1), NUMSAMPLES (<=160 @ 2 MHz),
 *     NUMSAMPLESCOEFF = 2^16/(NUMSAMPLES/16), PHASESHIFT, FAEPEER (31.25 ppb),
 *     FFOEST (62.5 ppb), IQRAWMEAN (I bits 0-15, Q bits 16-31), MAGPHASEMEAN, PCT16.
 *   - TASKS_CSTONESSTART -> EVENTS_CSTONESEND.
 *   - TASKS_PLLEN / EVENTS_PLLREADY: "RADIO in PLL mode, standby for either TX or RX".
 *   - FREQFINETUNE: two's complement, 488.28125 Hz steps, +/-1 MHz.
 *   - RADIO, TIMER10, DPPIC10 share the same domain; TIMER10 PCLK = 32 MHz.
 *   - No raw-IQ DMA mode: AUXDATA ACQMODE only lists RTT data. You get one averaged
 *     phasor per tone, which is all phase-based ranging needs.
 */
#pragma once
#include <stdint.h>
#include <nrf.h>

#define N_CH          72
#define SYNC_FREQ     2       /* 2402 MHz */

/* Step schedule, microseconds from the sync packet's ADDRESS event.
 * Generous on purpose; tighten once it works. Keep the A->B / B->A spacing CONSTANT
 * on every channel: the crystal-offset phase term is then the same on all channels,
 * so it only adds a constant phase and barely touches the slope. */
#define T_FIRST       1000
#define T_STEP        300
#define T_A_TX        0       /* initiator tone starts */
#define T_B_RX        5       /* reflector starts measuring */
#define T_B_TX        120     /* reflector tone starts */
#define T_A_RX        125     /* initiator starts measuring */
#define T_TONE        80      /* tone length */

#define CS_NSAMPLES   64      /* 32 us at 2 MHz, fits inside an 80 us tone with margin */

typedef struct { int16_t i, q; } iq_t;

/* 72 frequencies, 2404..2478 MHz minus 2423..2425 (CS skips the adv-adjacent ones). */
static const uint8_t ch_freq[N_CH] = {
   4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,
  26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,
  50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78
};

/* ---------------------------------------------------------------- clocks / timer */

static inline void hfxo_start(void) {
  NRF_CLOCK->EVENTS_XOSTARTED = 0;
  NRF_CLOCK->TASKS_XOSTART = 1;
  while (!NRF_CLOCK->EVENTS_XOSTARTED) {}
}

/* TIMER10 at 1 MHz, started by RADIO ADDRESS through DPPIC10 channel 0. */
static inline void timer_arm_on_address(void) {
  NRF_TIMER10->TASKS_STOP  = 1;
  NRF_TIMER10->TASKS_CLEAR = 1;
  NRF_TIMER10->MODE      = 0;          /* timer */
  NRF_TIMER10->BITMODE   = 3;          /* 32 bit */
  NRF_TIMER10->PRESCALER = 5;          /* 32 MHz / 2^5 = 1 MHz */
  NRF_RADIO->PUBLISH_ADDRESS   = 0 | (1u << 31);
  NRF_TIMER10->SUBSCRIBE_START = 0 | (1u << 31);
  NRF_DPPIC10->CHENSET = 1u << 0;
}
static inline void timer_unhook(void) {
  NRF_RADIO->PUBLISH_ADDRESS = 0;
  NRF_TIMER10->SUBSCRIBE_START = 0;
}
static inline uint32_t t_now(void) {
  NRF_TIMER10->TASKS_CAPTURE[7] = 1;
  return NRF_TIMER10->CC[7];
}
static inline void wait_until(uint32_t t) { while ((int32_t)(t_now() - t) < 0) {} }

/* ---------------------------------------------------------------- packets */

static inline void radio_packet_init(void) {
  NRF_RADIO->MODE    = 3;              /* Ble_1Mbit */
  NRF_RADIO->TIMING  = 1;              /* fast ramp-up */
  NRF_RADIO->TXPOWER = 0x18;           /* 0 dBm */
  NRF_RADIO->PCNF0   = (8u << RADIO_PCNF0_LFLEN_Pos);
  NRF_RADIO->PCNF1   = (255u << RADIO_PCNF1_MAXLEN_Pos) | (3u << RADIO_PCNF1_BALEN_Pos);
  NRF_RADIO->BASE0   = 0x89BED600;
  NRF_RADIO->PREFIX0 = 0x8E;
  NRF_RADIO->TXADDRESS   = 0;
  NRF_RADIO->RXADDRESSES = 1;
  NRF_RADIO->CRCCNF  = 3;
  NRF_RADIO->CRCPOLY = 0x00065B;
  NRF_RADIO->CRCINIT = 0x555555;
}

static inline void radio_disable(void) {
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_DISABLE = 1;
  while (!NRF_RADIO->EVENTS_DISABLED) {}
}

/* One packet, TX or RX, on SYNC_FREQ. Returns 1 on CRC ok (RX) or always 1 (TX). */
static inline int radio_packet(uint8_t *buf, int tx) {
  NRF_RADIO->FREQUENCY = SYNC_FREQ;
  NRF_RADIO->PACKETPTR = (uint32_t)buf;
  NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_PHYEND_DISABLE_Msk;
  NRF_RADIO->EVENTS_DISABLED = 0;
  if (tx) NRF_RADIO->TASKS_TXEN = 1; else NRF_RADIO->TASKS_RXEN = 1;
  while (!NRF_RADIO->EVENTS_DISABLED) {}
  NRF_RADIO->SHORTS = 0;
  return tx ? 1 : (NRF_RADIO->CRCSTATUS & 1);
}

/* ---------------------------------------------------------------- tones */

static inline void cstones_init(void) {
  NRF_RADIO->CSTONES.MODE            = 1;       /* TPM only for now; TFM in calibrate() */
  NRF_RADIO->CSTONES.NUMSAMPLES      = CS_NSAMPLES;
  NRF_RADIO->CSTONES.NUMSAMPLESCOEFF = (uint32_t)((65536u * 16u) / CS_NSAMPLES);
  NRF_RADIO->CSTONES.PHASESHIFT      = 0;       /* CHECK: meaning undocumented */
  NRF_RADIO->CSTONES.DOWNSAMPLE      = 0;       /* filter off, BLE1M */
}

/* Bring the synthesizer up on f and leave it there.
 * CHECK (the big one): whether going TXIDLE -> RXIDLE on the same channel keeps the
 * PLL phase. The plan is to use TASKS_PLLEN as the "parking" state between the two
 * tones instead of DISABLE, which would restart the PLL with a random phase. */
static inline void pll_park(uint8_t f) {
  NRF_RADIO->FREQUENCY = f;
  NRF_RADIO->EVENTS_PLLREADY = 0;
  NRF_RADIO->TASKS_PLLEN = 1;
  while (!NRF_RADIO->EVENTS_PLLREADY) {}
}

static inline void tone_tx_on(void) {           /* carrier in TXIDLE: TXEN, no START */
  NRF_RADIO->EVENTS_TXREADY = 0;
  NRF_RADIO->TASKS_TXEN = 1;
  while (!NRF_RADIO->EVENTS_TXREADY) {}
}

static inline void rx_on(void) {
  NRF_RADIO->EVENTS_RXREADY = 0;
  NRF_RADIO->TASKS_RXEN = 1;
  while (!NRF_RADIO->EVENTS_RXREADY) {}
  /* CHECK: whether CSTONES needs the radio in RX (TASKS_START) or is fed in RXIDLE. */
}

static inline iq_t tone_measure(void) {
  NRF_RADIO->EVENTS_CSTONESEND = 0;
  NRF_RADIO->TASKS_CSTONESSTART = 1;
  while (!NRF_RADIO->EVENTS_CSTONESEND) {}
  uint32_t r = NRF_RADIO->CSTONES.IQRAWMEAN;
  iq_t s = { (int16_t)(r & 0xFFFF), (int16_t)(r >> 16) };
  return s;
}

/* Back to PLL standby after a tone or a measurement, not to DISABLED. CHECK. */
static inline void back_to_pll(void) {
  NRF_RADIO->EVENTS_PLLREADY = 0;
  NRF_RADIO->TASKS_PLLEN = 1;
  while (!NRF_RADIO->EVENTS_PLLREADY) {}
}

/* Hz offset from FFOEST (62.5 ppb units, 12-bit signed) at frequency 2400+f MHz. */
static inline int32_t ffoest_hz(uint8_t f) {
  int32_t v = (int32_t)(NRF_RADIO->CSTONES.FFOEST << 20) >> 20;  /* sign-extend 12 bits */
  return (int32_t)(((int64_t)v * 625 * (2400 + f)) / 10000);   /* ppb*MHz -> Hz */
}
