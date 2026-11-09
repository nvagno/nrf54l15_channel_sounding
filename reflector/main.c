/* reflector/main.c — waits for the sync packet, mirrors the sweep, sends phasors back. */
#include "../common/cs.h"

static uint8_t pkt[2 + 4 * N_CH];
static iq_t b_meas[N_CH];

int main(void) {
  hfxo_start(); radio_packet_init(); cstones_init();

  for (;;) {
    /* 1. Wait for sync. Its ADDRESS event starts TIMER10. */
    timer_arm_on_address();
    if (!radio_packet(pkt, 0) || pkt[1] != 0xC5) continue;
    timer_unhook();

    /* 1b. Frequency calibration: measure the initiator's carrier with TFM, then pull
     *     our synthesizer onto it. Sign convention of FFOEST vs FREQFINETUNE: CHECK
     *     (if ranging gets worse after this, flip the sign). */
    NRF_RADIO->FREQFINETUNE = 0;
    pll_park(SYNC_FREQ);
    rx_on();
    wait_until(T_FIRST / 2 + 50);
    NRF_RADIO->CSTONES.MODE = 3;                      /* TPM + TFM */
    (void)tone_measure();
    NRF_RADIO->CSTONES.MODE = 1;
    int32_t df = ffoest_hz(SYNC_FREQ);
    NRF_RADIO->FREQFINETUNE = (uint32_t)(-(df * 1000 / 488281)) & 0x1FFF;
    radio_disable();

    /* 2. Sweep: measure A's tone, then answer with ours on the same PLL. */
    for (int k = 0; k < N_CH; k++) {
      uint32_t t0 = T_FIRST + k * T_STEP;
      pll_park(ch_freq[k]);
      rx_on();
      wait_until(t0 + T_B_RX);  b_meas[k] = tone_measure();   /* A -> B */
      back_to_pll();
      wait_until(t0 + T_B_TX);  tone_tx_on();                 /* B -> A */
      wait_until(t0 + T_B_TX + T_TONE);
      radio_disable();
    }

    /* 3. Send phasors back, little-endian I,Q per channel. */
    pkt[0] = 1 + 4 * N_CH; pkt[1] = 0xA5;
    for (int k = 0; k < N_CH; k++) {
      uint8_t *p = &pkt[2 + 4 * k];
      p[0] = b_meas[k].i; p[1] = b_meas[k].i >> 8;
      p[2] = b_meas[k].q; p[3] = b_meas[k].q >> 8;
    }
    NRF_RADIO->FREQFINETUNE = 0;
    for (volatile int d = 0; d < 20000; d++) {}       /* let the initiator turn to RX */
    radio_packet(pkt, 1);
  }
}
