# nRF54L15 Channel Sounding — Build & Flash

Bare-metal phase-based ranging on two nRF54L15 DKs, driven at register level
(no Zephyr, no nRF Connect SDK). This README covers **building each board's
firmware and flashing it**. For toolchain installation on Windows see
`INSTALL.md`; for the ranging design and bring-up plan see `CLAUDE.md`.

> **Status warning — read before flashing.**
> The sources in this repo compile to an ELF, but the binary is **not yet
> bootable** on hardware: it is missing the nRF54L15 startup/vector-table file
> (`gcc_startup_nrf54l15_application.S`) and an nRF54L15 **linker script**
> (there is none in the repo yet). Until those are wired in, flashing the
> current output will not start the MCU correctly. See "Known gaps" at the end.

## Layout

```
common/cs.h           shared radio / TIMER10 / DPPIC10 / CSTONES helpers + 72-channel table
common/io.c           UARTE30 console + GPIO button helpers (used by the initiator)
initiator/main.c      button -> sync packet -> 72-channel sweep -> CSV over UART
reflector/main.c      waits for sync, mirrors the sweep, sends phasors back
ext/nrfx/             Nordic nRFx repository (git submodule): MDK headers, startup
host/range.py         PC side: combines phasors, estimates distance
```

## Prerequisites (Linux/macOS or WSL)

- `arm-none-eabi-gcc` (Cortex-M33 toolchain, Arm GNU Embedded)
- `arm-none-eabi-objcopy` (same toolchain)
- `nrfutil` with the `device` module: `nrfutil install device`
- (optional) `JLinkExe` / `Ozone` for debug
- Python + `pyserial` for reading UART: `pip install pyserial`

## Build

Both boards include `../common/cs.h`. Only the **initiator** uses the console
helpers, so only the initiator compiles `../common/io.c`.

> The MDK device symbol on the nRF54L15 is **`NRF54L15_XXAA`** (not `NRF54L15`),
> and the build targets **secure mode** (no `NRF_TRUSTZONE_NONSECURE`), which is
> how the peripheral registers (`NRF_RADIO`, `NRF_UARTE30`, …) are reachable.

### Initiator

```sh
cd initiator

arm-none-eabi-gcc \
  -mcpu=cortex-m33 -mthumb \
  -mfloat-abi=hard -mfpu=fpv5-sp-d16 \
  -O3 -ffunction-sections -fdata-sections \
  -Wall -Wextra \
  -I ../common \
  -I ../ext/nrfx/bsp/stable/mdk \
  -D NRF54L15_XXAA \
  main.c ../common/io.c -o initiator.elf
```

### Reflector

```sh
cd reflector

arm-none-eabi-gcc \
  -mcpu=cortex-m33 -mthumb \
  -mfloat-abi=hard -mfpu=fpv5-sp-d16 \
  -O3 -ffunction-sections -fdata-sections \
  -Wall -Wextra \
  -I ../common \
  -I ../ext/nrfx/bsp/stable/mdk \
  -D NRF54L15_XXAA \
  main.c -o reflector.elf
```

## Flash

### 1. List the boards

```sh
nrfutil device list
```

Note the **serial number** of each DK, and mark which one is the
**initiator** and which the **reflector**.

### 2. Convert to HEX

`nrfutil` expects HEX (or a signed ZIP), so convert the ELF:

```sh
arm-none-eabi-objcopy -O ihex initiator.elf initiator.hex
arm-none-eabi-objcopy -O ihex reflector.elf reflector.hex
```

### 3. Program each board

Always pass `--serial-number` so both DKs don't receive the same image:

```sh
nrfutil device program --hexfile initiator.hex --serial-number <SN_INITIATOR>
nrfutil device program --hexfile reflector.hex --serial-number <SN_REFLECTOR>
```

### 4. Read the console

The initiator prints CSV on UARTE30 at **115200 8N1** (P0.20 TX / P0.21 RX).
Find the board's serial port (`ls /dev/ttyACM*`) and open it:

```sh
python -m serial.tools.miniterm /dev/ttyACM0 115200
```

If nothing appears, try the other port (`/dev/ttyACM1`). Press the DK button
(initiator: **BUTTON0 = P0.23**) to trigger one 72-channel sweep. Quit
miniterm with `Ctrl+]`.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `nrfutil device list` shows nothing | Use a data USB cable, check the power switch, try another port. |
| Flash fails with "protected" / "access port" | Readback protection is on. Unlock (erases the board): `nrfutil device recover --serial-number <SN>`. |
| `device must be defined` compile error | Define `NRF54L15_XXAA`, not `NRF54L15`. |
| `undefined reference to uart_*` | Compile `../common/io.c` together with `initiator/main.c`. |

## Known gaps (before this flashes a working ranging demo)

1. **Startup + linker script.** Link `system_nrf54l.c` +
   `gcc_startup_nrf54l15_application.S` and provide an nRF54L15 application
   linker script (secure vs. non-secure partition; FLPR vs. application offset
   on the shared flash). Without it the ELF has no vector table, no stack and
   no correct memory mapping.
2. **Untested register sequences** (marked `CHECK` in `cs.h`): PLL phase
   coherence across the TX↔RX turnaround, CSTONES RX vs. RXIDLE behaviour,
   `PHASESHIFT` meaning, and FFOEST/FREQFINETUNE sign conventions.
