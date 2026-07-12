# AuraScope

A wireless audio monitor built end-to-end: Auracast (BLE Audio) → USB sound
card → Zynq-7000 embedded Linux → kernel character driver + eBPF
observability, side by side.

This is a portfolio project, documented as it's actually built — real
commands, real dead ends, real fixes. Full write-ups: [anateresaneto.pt/projects/aurascope](https://anateresaneto.pt/projects/aurascope)

## Architecture

```
nRF5340 (TX) --Auracast/LC3--> nRF5340 (RX) --USB Audio Class--> Arty Z7-20
                                                                  (Zynq-7000)
                                                                       |
                                                         Linux: snd-usb-audio
                                                                       |
                                              +------------------------+------------------------+
                                              |                                                  |
                                   char driver (/dev/audiomon)                          eBPF (kprobes on ALSA)
                                        in-kernel, owned data                           external, ad-hoc tracing
                                              |                                                  |
                                              +------------------------+------------------------+
                                                                       |
                                                            userspace dashboard
```

## Status

| Phase | What it covers | Status |
|---|---|---|
| **1 — Foundation** | Hardware definition, Yocto, first boot, custom recipe, audio-status LED | 🟡 in progress (2/5 steps live) |
| 2 — Wireless audio | Auracast TX/RX, USB Audio Class into Linux | ⬜ planned |
| 3 — Observability | Char driver, eBPF tracing, dashboard | ⬜ planned |
| 4 — Integration | Full system, retrospective | ⬜ planned |
| 5 — Bonus | I2S over the FPGA fabric, PL-driven LED, USB-vs-FPGA comparison | ⬜ planned |

### Phase 1 detail

| Step | Description | Tag |
|---|---|---|
| 01 | Defining the hardware — Vivado block design, catching a bitstream that never synthesized, exporting and verifying a real `.xsa` | `v0.1` |
| 02 | Yocto setup — matched branches, a dependency chain into AMD's SDT stack, stepping back, generating a machine from the real `.xsa` | `v0.1` |
| 03 | First boot | planned |
| 04 | Custom image & recipe | planned |
| 05 | Audio-status LED (AXI GPIO + device tree) | planned |

## Hardware

- **Board:** Digilent Arty Z7-20 (Zynq-7000, XC7Z020)
- **Vivado:** 2025.2
- **GPIO route:** AXI GPIO (not EMIO) — `axi_gpio_0` (switches) @ `0x41200000`, `axi_gpio_1` (LEDs ch.2 / buttons ch.1) @ `0x41210000`, confirmed from the exported `.hwh`

## Software

- **Yocto:** 5.0 "Scarthgap"
- **meta-xilinx:** `rel-v2025.2`
- **Machine:** `aurascope-arty-z7`, generated from `hw/design_2_wrapper.xsa` via `gen-machineconf`

## Repo layout

```
aurascope/
├── meta-aurascope/     # the Yocto layer — recipes, images, device-tree overrides
├── hw/
│   ├── vivado/         # exported block-design Tcl + XDC constraints
│   └── design_2_wrapper.xsa
├── nrf/                # Zephyr apps for the nRF5340 TX/RX (Phase 2+)
├── docs/notes/          # build logs, pinned commits, as-run detail
└── conf-templates/      # sample local.conf / bblayers.conf
```

## Building

```bash
# clone upstream layers as siblings (not part of this repo — see docs/notes/PINNED-COMMITS.txt)
source poky/oe-init-build-env build
bitbake-layers add-layer ../meta-openembedded/meta-oe
bitbake-layers add-layer ../meta-arm/meta-arm-toolchain
bitbake-layers add-layer ../meta-arm/meta-arm
bitbake-layers add-layer ../meta-xilinx/meta-xilinx-core
bitbake-layers add-layer ../aurascope/meta-aurascope

# MACHINE = "aurascope-arty-z7" in local.conf — see conf-templates/
bitbake aurascope-image
```

Full step-by-step, including the dependency-maze detour and how it was
resolved: `docs/notes/phase1-log.md`.

## License

MIT — see `meta-aurascope/COPYING.MIT`.

## Author

Ana Teresa Neto — [anateresaneto.pt](https://anateresaneto.pt) · [LinkedIn](https://linkedin.com/in/ana-teresa-n-5a11961a3) · [GitHub](https://github.com/a-teresa)
