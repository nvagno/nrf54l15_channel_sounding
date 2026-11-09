/* initiator/main.c — press BUTTON0: one 72-channel sweep, prints CSV over UART. */
#include "../common/cs.h"

/* Your own minimal helpers (you have these from other nRF54 projects). */
void uart_init(void);
void uart_printf(const char *fmt, ...);
void button_init(void);
void button_wait(void);

static uint8_t pkt[2 + 4 * N_CH];
static iq_t a_meas[N_CH], b_meas[N_CH];

int main(void) {
  hfxo_start(); radio_packet_init(); cstones_init();
  uart_init(); button_init();

  for (;;) {
    button_wait();

    /* 0. Calibration tone: the reflector measures our carrier with TFM and pulls its
     *    crystal onto ours with FREQFINETUNE. Without this, a few ppm of offset rotates
     *    the phasor during the 32 us average and kills its magnitude. */

    /* 1. Sync packet. Its ADDRESS event starts TIMER10 on both boards. */
    timer_arm_on_address();
    pkt[0] = 1; pkt[1] = 0xC5;
    radio_packet(pkt, 1);
    timer_unhook();

    /* 1b. Calibration tone on the sync frequency, T_FIRST/2 .. T_FIRST/2+200 us. */
    pll_park(SYNC_FREQ);
    wait_until(T_FIRST / 2);
    tone_tx_on();
    wait_until(T_FIRST / 2 + 200);
    radio_disable();

    /* 2. Sweep. */
    for (int k = 0; k < N_CH; k++) {
      uint32_t t0 = T_FIRST + k * T_STEP;
      pll_park(ch_freq[k]);
      wait_until(t0 + T_A_TX);  tone_tx_on();                 /* A -> B */
      wait_until(t0 + T_A_TX + T_TONE); back_to_pll();
      rx_on();                                                 /* B -> A */
      wait_until(t0 + T_A_RX);  a_meas[k] = tone_measure();
      radio_disable();
    }

    /* 3. Reflector's phasors. */
    if (!radio_packet(pkt, 0) || pkt[1] != 0xA5) { uart_printf("NO_REPLY\n"); continue; }
    for (int k = 0; k < N_CH; k++) {
      uint8_t *p = &pkt[2 + 4 * k];
      b_meas[k].i = (int16_t)(p[0] | p[1] << 8);
      b_meas[k].q = (int16_t)(p[2] | p[3] << 8);
    }

    /* 4. CSV: freq_MHz, A_I, A_Q, B_I, B_Q */
    uart_printf("BEGIN\n");
    for (int k = 0; k < N_CH; k++)
      uart_printf("%u,%d,%d,%d,%d\n", 2400 + ch_freq[k],
                  a_meas[k].i, a_meas[k].q, b_meas[k].i, b_meas[k].q);
    uart_printf("END\n");
  }
}
