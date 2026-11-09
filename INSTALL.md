# Installing the toolchain (Windows)

This sets up a PC to build, flash and debug the bare-metal ranging firmware for two
nRF54L15 DKs. You do **not** need the nRF Connect SDK, Zephyr or VS Code.

Tested with: Windows 11, PowerShell. Last updated 09-Nov-2026.

## 1. Install the tools

Open PowerShell and run:

```powershell
winget install Arm.GnuArmEmbeddedToolchain ezwinports.make NordicSemiconductor.JLink NordicSemiconductor.nrfutil Git.Git Python.Python.3.13
```

| Package | What it is for |
|---|---|
| `Arm.GnuArmEmbeddedToolchain` | `arm-none-eabi-gcc` compiler for the Cortex-M33 |
| `ezwinports.make` | `make`, runs the build |
| `NordicSemiconductor.JLink` | SEGGER J-Link software (the DK's onboard debugger is a J-Link) |
| `NordicSemiconductor.nrfutil` | Nordic's command-line tool to list, flash and recover boards |
| `Git.Git`, `Python.Python.3.13` | skip these if you already have them |

If the installer for the Arm toolchain asks, tick **"Add path to environment variable"**.

Then **close PowerShell and open a new window** so the new `PATH` is loaded, and run:

```powershell
nrfutil install device
pip install numpy pyserial
```

### Optional: SEGGER debuggers

The firmware is a plain ELF file, so any J-Link debugger can load it.

- **Ozone** (recommended for stepping through register code and watching `RADIO` live):
  download from https://www.segger.com/products/development-tools/ozone-j-link-debugger/
- **SEGGER Embedded Studio**: `winget install Segger.EmbeddedStudioARM`.
  Nordic stopped supporting it *for the nRF Connect SDK*. That does not matter here,
  since we do not use the SDK.

## 2. Check the installation

In a **new** PowerShell window:

```powershell
arm-none-eabi-gcc --version
make --version
nrfutil device --version
python -c "import numpy, serial; print('python ok')"
```

All four must print a version (or `python ok`). If one says *"not recognized"*,
that tool is not on `PATH`: close and reopen PowerShell, and if that does not help,
reinstall it.

## 3. Get the code

The nRF54L15 register headers, startup file and linker script come from Nordic's
`nrfx` repository, included as a git submodule in `ext/nrfx`. Clone **with** submodules:

```powershell
git clone --recurse-submodules https://github.com/twatteyne/nrf54j14_channel_sounding.git
```

If you already cloned without them:

```powershell
git submodule update --init
```

## 4. Connect the boards

Plug both DKs in with USB (use the debugger's USB connector; see the DK user guide if
your board has more than one) and switch them on. Then:

```powershell
nrfutil device list
```

You should see two boards, each with a **serial number** and two COM ports. Write down
which serial number is the **initiator** and which is the **reflector** (a sticky note on
each board helps). You will need the serial numbers to flash and the COM ports to read
the output.

To read a board's UART output (115200 baud):

```powershell
python -m serial.tools.miniterm COM5 115200
```

Replace `COM5` with the board's port. If you see nothing, try the board's other COM port.
Press `Ctrl+]` to quit.

## Troubleshooting

- **`nrfutil device list` shows nothing:** use a USB data cable (not a charge-only one),
  check the power switch, and try another USB port.
- **Flashing fails with "protected" / "access port":** the board has readback
  protection on. Unlock it (this erases the board):
  `nrfutil device recover --serial-number <SN>`.
- **`make` finds the wrong compiler:** run `where.exe arm-none-eabi-gcc`. Only the Arm
  toolchain should be listed.

How to build, flash and run each bring-up test is in `BRINGUP.md`.
