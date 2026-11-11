/*
 * common/io.c — minimal console + button helpers for the nRF54L15 initiator.
 *
 * UART : UARTE30, P0.20 TX / P0.21 RX, 115200 8N1  (preferred on nRF54L15-DK)
 *        nRF54L UARTE is the new DMA-based peripheral, so TX is one EasyDMA
 *        transfer from a RAM buffer (DMA.TX.PTR/MAXCNT -> TASKS_DMA.TX.START
 *        -> wait EVENTS_DMA.TX.END). Verified register names against
 *        nrf54l15_types.h and the shipped nrfx_uarte HAL.
 * Button: P0.23 (DK BUTTON0), active-low to GND, pulled up.
 *
 * Build is secure-mode (no NRF_TRUSTZONE_NONSECURE), so NRF_UARTE30 / NRF_P0
 * resolve to the _S instances from nrf54l15_global.h.
 */
#include <nrf.h>
#include <stdarg.h>
#include <stdio.h>

#define UART_TX_PIN  20   /* P0.20  */
#define UART_RX_PIN  21   /* P0.21  */
#define BUTTON_PIN   23   /* P0.23, DK BUTTON0, active-low */

void uart_init(void)
{
    NRF_UARTE30->ENABLE   = UARTE_ENABLE_ENABLE_Enabled;
    NRF_UARTE30->PSEL.TXD = UART_TX_PIN;   /* CONNECT=Connected (bit31=0) */
    NRF_UARTE30->PSEL.RXD = UART_RX_PIN;
    NRF_UARTE30->BAUDRATE = UARTE_BAUDRATE_BAUDRATE_Baud115200;
    NRF_UARTE30->CONFIG   = 0;             /* no parity, no hw flow ctrl, 8N1 */
}

void uart_printf(const char *fmt, ...)
{
    static char buf[128];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    if (n <= 0) return;
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;

    /* One blocking EasyDMA TX of the whole formatted string. */
    NRF_UARTE30->EVENTS_DMA.TX.END = 0;
    NRF_UARTE30->DMA.TX.PTR        = (uint32_t)buf;
    NRF_UARTE30->DMA.TX.MAXCNT     = (uint32_t)n;
    NRF_UARTE30->TASKS_DMA.TX.START = 1;
    while (!NRF_UARTE30->EVENTS_DMA.TX.END) {}
}

void button_init(void)
{
    /* Input, connected buffer, pull-up (button shorts pin to GND when pressed). */
    NRF_P0->PIN_CNF[BUTTON_PIN] =
        (GPIO_PIN_CNF_DIR_Input      << GPIO_PIN_CNF_DIR_Pos)     |
        (GPIO_PIN_CNF_INPUT_Connect  << GPIO_PIN_CNF_INPUT_Pos)   |
        (GPIO_PIN_CNF_PULL_Pullup    << GPIO_PIN_CNF_PULL_Pos);
}

void button_wait(void)
{
    /* Wait for press (pin reads low), then a short debounce. */
    while ((NRF_P0->IN >> BUTTON_PIN) & 1u) {}
    for (volatile uint32_t d = 0; d < 20000; d++) {}
    while ((NRF_P0->IN >> BUTTON_PIN) & 1u) {}
}
