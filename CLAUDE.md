# Bare-metal phase-based ranging on nRF54L15

## Goal
Re-implement Bluetooth Channel Sounding style **phase-based ranging (PBR)** on two
nRF54L15 DKs **at register level**: no Zephyr, no nRF Connect SDK, no SoftDevice
Controller. Only the nRF54L15 MDK headers (`nrf.h`) and our own startup/linker.

Why: the reference evaluation (Wieme et al., IEEE Access 2025, same chip, Nordic SDK)
treats CS as a black box and reports up to ~500 ms to ship IQ samples over BLE.
Owning the whole chain gives a fast turnaround and raw per-channel phasors for our own
estimators. Nordic confirmed on DevZone that PBR in proprietary mode is possible
using only the registers, but they publish no example. As far as we know, nobody else has
published one either.

Target paper: IEEE WoWMoM 2027.

## Layout
```
common/cs.h          shared radio/timer/CSTONES helpers + 72-channel table
initiator/main.c     button -> sync packet -> 72-channel sweep -> CSV over UART
reflector/main.c     waits for sync, mirrors the sweep, sends its phasors back
host/range.py        PC side: combines phasors, estimates distance (slope + IFFT)
```

## How one measurement works
1. Initiator sends a BLE 1M sync packet on 2402 MHz. On both boards the RADIO
   `ADDRESS` event starts `TIMER10` (1 MHz) through `DPPIC10` channel 0, so both
   share one time base. RADIO, TIMER10 and DPPIC10 are in the same domain.
2. Calibration tone: initiator sends a carrier; reflector measures it with
   `CSTONES.MODE = TPM|TFM`, reads `CSTONES.FFOEST` (62.5 ppb units) and corrects its
   synthesizer with `FREQFINETUNE` (488.28125 Hz steps).
3. For each of 72 channels (fixed schedule, `T_STEP` = 300 µs):
   initiator tone → reflector measures with CSTONES, then reflector tone →
   initiator measures. Each measurement = one averaged phasor from
   `CSTONES.IQRAWMEAN` (I = bits 0–15, Q = bits 16–31).
4. Reflector sends its 72 phasors back in one packet; initiator prints CSV:
   `freq_MHz, A_I, A_Q, B_I, B_Q` between `BEGIN` / `END`.
5. On the PC: `h = A * B` per channel cancels both local-oscillator phases and
   leaves 2× propagation phase. Distance = slope of phase vs frequency × c/(4π),
   or the first peak of the IFFT (channel impulse response).

## Facts confirmed from the datasheet (nRF54L15 v1.0, §8.17)
- Unmodulated tone = `TASKS_TXEN` without `TASKS_START` (radio sends a carrier in TXIDLE).
- `TASKS_PLLEN` / `EVENTS_PLLREADY`: "RADIO in PLL mode, standby for either TX or RX".
- `TASKS_CSTONESSTART` → `EVENTS_CSTONESEND`.
- `CSTONES.NUMSAMPLES` ≤ 160 at 2 MHz; `CSTONES.NUMSAMPLESCOEFF = 2^16 / (NUMSAMPLES/16)`.
- `CSTONES.MODE`: bit0 TPM, bit1 TFM. Reset value 3.
- No raw-IQ DMA mode (`AUXDATA.CNF.ACQMODE` only lists RTT data).
- `TIMER10` PCLK is 32 MHz → `PRESCALER = 5` for 1 MHz.
- `FREQUENCY`: 2400 + value (MHz), MAP bit selects 2360 base.

## Open questions, in priority order (marked `CHECK` in the code)
1. **Phase coherence across the TX↔RX turnaround.** The code parks the
   synthesizer with `TASKS_PLLEN` between the two tones of a channel instead of
   `DISABLE`, which would restart the PLL with a random phase. Is going
   TXIDLE → PLL → RXIDLE on the same frequency phase-continuous? Everything depends on this.
   Test: fixed distance, repeat sweeps. If `|A*B|` is stable and the slope gives the same
   distance sweep after sweep, coherence works.
2. Does CSTONES need the receiver in RX (`TASKS_START`) or is it fed in RXIDLE?
3. Meaning of `CSTONES.PHASESHIFT` (undocumented, left at 0).
4. Sign conventions: FFOEST vs FREQFINETUNE correction; phase-slope sign;
   IFFT conjugation. Flip if results go the wrong way.
5. `TASKS_PLLEN` from TXIDLE/RXIDLE: allowed, or must the radio go through DISABLED?

## Bring-up plan (do these in order, don't skip)
1. Build system: CMake or Makefile + `arm-none-eabi-gcc`, MDK startup and linker
   script for nRF54L15, flash with `nrfutil` / `nrfjprog`. Get a blinky and a UART
   print on the DK's VCOM (UARTE20, check the pins against the DK user guide). `initiator/main.c`
   declares `uart_init`, `uart_printf`, `button_init`, `button_wait`, which still
   need to be implemented.
2. Sync packet only: confirm both TIMER10s start within a few µs (toggle a GPIO on
   a compare event on each board, scope both).
3. One channel, one direction: initiator tone, reflector CSTONES. Print IQRAWMEAN
   and MAGPHASEMEAN; check magnitude is high and phase is stable *relative* across
   repeats once the frequency offset is corrected.
4. One channel, both directions: check that `A*B` phase is stable across repeats → coherence (Q1).
5. Full 72-channel sweep → `host/range.py capture.txt`.
6. Calibrate a constant offset at a known distance (`--offset`).

## Experiments for the paper (after bring-up)
- Baseline accuracy vs distance (LOS), slope vs IFFT vs super-resolution (MUSIC).
- Channel count vs accuracy vs sweep time, across distances (Wieme only did 2 m).
- Multipath / NLOS; outlier detection and rejection.
- Benchmark our estimators on Wieme's public dataset (Zenodo 10.5281/zenodo.17347695),
  same CSV structure: per step `quality_i;quality_r;channel;antenna;I_i;Q_i;I_r;Q_r`.

## Conventions
- Register-level C only; use MDK `*_Pos` / `*_Msk` macros, never magic bit positions.
- Keep the A→B / B→A spacing identical on every channel. Then the residual crystal
  offset adds the same phase on every channel and barely biases the slope.
- US English in code and docs. Dates as `DD-Mmm-YYYY`.
- Don't add libraries or an RTOS without asking.
