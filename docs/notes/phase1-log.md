# AuraScope — Phase 1 Log: Yocto Environment Setup

This is the real, as-run log of setting up the Yocto build environment for
Phase 1 — cloning layers, hitting real dependency chains, pivoting away from
a too-heavy tool path, and successfully generating a machine from the
project's actual exported hardware (`design_2_wrapper.xsa`).

Nothing here is idealized. The dead ends are kept in, because they're
honest engineering and they're what the article's "tricky part" section
draws from.

- **Board:** Arty Z7-20 (Zynq-7000, XC7Z020)
- **Vivado:** 2025.2
- **Yocto:** Scarthgap (5.0)
- **meta-xilinx:** `rel-v2025.2`

---

## 1. Workspace + upstream layers

```bash
mkdir -p ~/aurascope-yocto
cd ~/aurascope-yocto

git clone -b scarthgap https://git.yoctoproject.org/poky
git clone -b scarthgap https://github.com/openembedded/meta-openembedded
git clone -b scarthgap https://git.yoctoproject.org/meta-arm
```

### Confirming the meta-xilinx branch before cloning

Rather than guessing a branch, list what actually exists upstream:

```bash
git ls-remote --heads https://github.com/Xilinx/meta-xilinx | grep rel-v
```

`rel-v2025.2` is present — matches the Vivado version being used, so it's
the natural choice. Confirmed it's Scarthgap-compatible by checking the
layer's own compat declarations (the authoritative source — not the README):

```bash
git clone -b rel-v2025.2 https://github.com/Xilinx/meta-xilinx
cd meta-xilinx
find . -name "layer.conf" -exec grep -H "LAYERSERIES_COMPAT" {} \;
```

Every sub-layer declared `scarthgap`. Branch choice locked in.

### Pin exact commits (reproducibility)

```bash
cd ~/aurascope-yocto
for d in poky meta-openembedded meta-arm meta-xilinx; do
  echo "$d: $(git -C $d rev-parse --short HEAD) ($(git -C $d branch --show-current))"
done | tee PINNED-COMMITS.txt
```

---

## 2. Project repo

Kept separate from the upstream clones — this is the one that's actually
mine and gets pushed to GitHub.

```bash
cd ~/aurascope-yocto
mkdir aurascope && cd aurascope
git init
git branch -m main          # default was 'master'; standardizing on 'main'
mkdir -p meta-aurascope hw/vivado nrf docs/notes conf-templates
```

---

## 3. Build environment + the custom layer

```bash
cd ~/aurascope-yocto
source poky/oe-init-build-env build
cd ~/aurascope-yocto
bitbake-layers create-layer aurascope/meta-aurascope
rm -rf aurascope/meta-aurascope/recipes-example
```

Verified the generated `layer.conf`:

```bash
cat aurascope/meta-aurascope/conf/layer.conf
```

```
BBFILE_COLLECTIONS += "meta-aurascope"
BBFILE_PATTERN_meta-aurascope = "^${LAYERDIR}/"
BBFILE_PRIORITY_meta-aurascope = "6"
LAYERDEPENDS_meta-aurascope = "core"
LAYERSERIES_COMPAT_meta-aurascope = "scarthgap"
```

Clean — `scarthgap`, priority 6, correct dependency on `core`. No edits needed.

Copied the earlier Vivado export into the repo (was sitting in a loose,
untracked `hw/` directory):

```bash
cp ~/aurascope-yocto/hw/design_2_wrapper.xsa ~/aurascope-yocto/aurascope/hw/
cd ~/aurascope-yocto/aurascope
git add -A
git commit -m "Initial commit: meta-aurascope layer skeleton + Vivado export"
```

---

## 4. Registering layers — the real dependency chain

This did **not** go in a straight line. Each command below is exactly what
was run, in order, including the ones that failed — the failures are the
useful part. This turned into the single biggest time sink of Phase 1:
what looked like "add five layers" became a circular, multi-round
dependency chase before it was deliberately simplified.

```bash
cd ~/aurascope-yocto/build
bitbake-layers add-layer ../meta-openembedded/meta-oe
```
Succeeded, no output of note.

```bash
bitbake-layers add-layer ../meta-arm/meta-arm
```
```
ERROR: Layer 'meta-arm' depends on layer 'arm-toolchain', but this layer is not enabled
```
`meta-arm` is itself a multi-layer repo; its toolchain sub-layer has to be
added first.

```bash
bitbake-layers add-layer ../meta-arm/meta-arm-toolchain
bitbake-layers add-layer ../meta-arm/meta-arm
```
Both succeeded.

```bash
bitbake-layers add-layer ../meta-xilinx/meta-xilinx-core
```
```
ERROR: Layer 'xilinx' depends on layer 'meta-arm', but this layer is not enabled
```
This is why `meta-arm` turned out to be required rather than optional —
`meta-xilinx-core` needs it (almost certainly for ARM Trusted Firmware /
secure-boot plumbing in the Zynq boot chain). Since it was already added
above, this resolved automatically on retry.

```bash
bitbake-layers add-layer ../meta-xilinx/meta-xilinx-core   # now succeeds
bitbake-layers add-layer ../aurascope/meta-aurascope
bitbake-layers show-layers
```

**Result — nine layers registered cleanly:**

```
layer                 path                                              priority
core                  poky/meta                                        5
yocto                 poky/meta-poky                                   5
yoctobsp              poky/meta-yocto-bsp                               5
openembedded-layer    meta-openembedded/meta-oe                         5
meta-aurascope        aurascope/meta-aurascope                          6
arm-toolchain         meta-arm/meta-arm-toolchain                       5
meta-arm              meta-arm/meta-arm                                 5
xilinx                meta-xilinx/meta-xilinx-core                      5
xilinx-bsp            meta-xilinx/meta-xilinx-bsp                       5
```

(`meta-xilinx-bsp` was added at this point too, before it was known to be
deprecated — see §5.)

Saved the working config as templates and committed:

```bash
cp conf/local.conf ~/aurascope-yocto/aurascope/conf-templates/local.conf.sample
cp conf/bblayers.conf ~/aurascope-yocto/aurascope/conf-templates/bblayers.conf.sample
cd ~/aurascope-yocto/aurascope
git add -A
git commit -m "Layer stack registered: meta-oe, meta-arm(+toolchain), meta-xilinx-core/bsp, meta-aurascope"
```

---

## 5. Dead end: the circular dependency chase into AMD SDT / adaptive-socs

This section is a deliberate record of a path that was tried and abandoned.
It's kept because "the tool's recommended path was disproportionate, so I
stepped back" is a legitimate and honest engineering decision — not a
failure to hide. It's also the part of Phase 1 that cost the most real time.

**Discovery:** `meta-xilinx-bsp`'s own README states it is deprecated:

```bash
cat ~/aurascope-yocto/meta-xilinx/meta-xilinx-bsp/README.md
```
```
This layer is deprecated. All BSP components have been moved to
other layers. XSCT based machine are in generally in meta-xilinx-tools,
while SDT based machines are in meta-amd-adaptive-socs-bsps.
```

**Attempt to follow the SDT path it points to:**

```bash
git clone -b rel-v2025.2 https://github.com/Xilinx/meta-amd-adaptive-socs
cd ~/aurascope-yocto/build
bitbake-layers add-layer ../meta-amd-adaptive-socs/meta-amd-adaptive-socs-core
```
```
ERROR: depends on layer 'xilinx-microblaze', 'xilinx-standalone', 'xilinx-standalone-sdt'
```

Chased the chain — each fix surfaced a new dependency, and the chain kept
growing rather than terminating, which is what made it feel circular:

```bash
bitbake-layers add-layer ../meta-xilinx/meta-microblaze
bitbake-layers add-layer ../meta-xilinx/meta-xilinx-standalone
bitbake-layers add-layer ../meta-xilinx/meta-xilinx-standalone-sdt
```
```
ERROR: 'xilinx-standalone-sdt' depends on 'virtualization-layer', 'xilinx-microblaze'
```
```bash
bitbake-layers add-layer ../meta-xilinx/meta-xilinx-virtualization
```
```
ERROR: 'xilinx-virtualization' depends on 'security', 'tpm-layer', 'virtualization-layer'
```

At this point the chain was: **SDT machine → standalone-sdt → virtualization
→ security + TPM**, four levels deep, with no end in sight and each new
layer belonging to an entirely separate upstream project
(`meta-virtualization`, `meta-security`) that would have to be cloned
fresh. This is the shape of a genuinely circular-feeling dependency chase:
not an actual cycle in the dependency graph, but a chain so deep and
tangential to the actual goal that it stopped feeling like it was
converging.

**Decision point:** this infrastructure — virtualization, TPM, security —
belongs to AMD's full adaptive-SoC/Versal product line. It has nothing to
do with booting Linux on a Zynq-7020 with two AXI GPIO cores. Continuing
down this path would mean adopting a much heavier BSP framework than the
project needs, purely because the deprecated layer's README pointed there
by default.

**The simplifying choice:** stop following the "recommended" SDT
migration path, and instead check whether the *old*, deprecated, but
still-documented `.xsa`-based tool was still present and functional. It
was (see §6). This one decision collapsed four layers of cascading
dependencies down to zero additional layers.

**Reverted the entire SDT detour:**

```bash
bitbake-layers remove-layer '*/meta-amd-adaptive-socs-bsp'
bitbake-layers remove-layer '*/meta-amd-adaptive-socs-core'
bitbake-layers remove-layer '*/meta-xilinx-standalone-sdt'
bitbake-layers remove-layer '*/meta-xilinx-virtualization'
bitbake-layers remove-layer '*/meta-microblaze'
bitbake-layers remove-layer '*/meta-xilinx-multimedia'
```

Back to the clean nine-layer stack from §4 — **no virtualization, no
security, no TPM, no microblaze/standalone layers.** Everything needed
turned out to already be inside `meta-xilinx-core`, just not obviously so.

---

## 6. What actually worked: gen-machineconf, the classic .xsa path

`meta-xilinx-core` still ships a standalone tool, `gen-machineconf`, that
consumes a `.xsa` directly — no SDT infrastructure required. Confirmed it's
present (as a submodule that needed initializing):

```bash
cd ~/aurascope-yocto/meta-xilinx
cat .gitmodules
```
```
[submodule "gen-machine-conf"]
	path = meta-xilinx-core/gen-machine-conf
	url = https://github.com/Xilinx/gen-machine-conf.git
	branch = xlnx_rel_v2025.2
```

```bash
git submodule status
```
```
-e1bc2ac323a92fe3c2b87f11044010351cd79b25 meta-xilinx-core/gen-machine-conf
```
Leading `-` confirms it was uninitialized.

```bash
git submodule update --init --recursive
```

Read the tool's own docs before running anything:

```bash
cd meta-xilinx-core/gen-machine-conf
cat README.md
cat docs/examples.rst
```

The docs confirmed the classic `.xsa` flow is still present and documented
(marked deprecated in favor of SDT, but functional):

```
Examples Using .xsa file (deprecated and will be removed in future releases)
  $ gen-machine-conf --soc-family <microblaze|zynq|zynqmp|versal> \
      --hw-description <path>/<project_name>.xsa --machine-name <your-custom-name>
```

**Decision:** use the deprecated-but-documented-and-functional `.xsa` path.
It's the one that actually fits a simple Zynq-7000 board without dragging
in unrelated infrastructure, and it can be migrated to SDT later if a
future step genuinely needs it.

### First attempt — missing XSCT

```bash
cd ~/aurascope-yocto/meta-xilinx/meta-xilinx-core/gen-machine-conf
./gen-machineconf --soc-family zynq \
  --hw-description ~/aurascope-yocto/aurascope/hw/design_2_wrapper.xsa \
  --machine-name aurascope-arty-z7
```

Got past the initial unpack and layer warnings, then:

```
[ERROR] xsct command not found, use --xsct-tool option to specify path
Unable to get required xsct-native sysroot path
```

`gen-machineconf` needs XSCT (Xilinx Software Command-line Tool, part of
the Vitis install) to actually parse the hardware description — it isn't
purely self-contained. Located it:

```bash
find / -iname "xsct" -type f 2>/dev/null
```
```
/opt/2025.2/Vitis/bin/scripts/xsct
/opt/2025.2/Vitis/bin/xsct
```

Used the top-level wrapper (`bin/xsct`), not the internal `scripts/xsct`.

### Second attempt — pointed at XSCT, still failed

```bash
./gen-machineconf --soc-family zynq \
  --hw-description ~/aurascope-yocto/aurascope/hw/design_2_wrapper.xsa \
  --machine-name aurascope-arty-z7 \
  --xsct-tool /opt/2025.2/Vitis/bin/xsct
```
```
[ERROR] XSCT_TOOL path not found: /opt/2025.2/Vitis/bin/xsct
```

Confusing at first — `find` had just confirmed the file exists. Verified
it wasn't a permissions or broken-symlink issue:

```bash
ls -la /opt/2025.2/Vitis/bin/xsct
file /opt/2025.2/Vitis/bin/xsct
```
```
-rwxr-xr-x 1 root root 8588 Nov 14  2025 /opt/2025.2/Vitis/bin/xsct
/opt/2025.2/Vitis/bin/xsct: Bourne-Again shell script, ASCII text executable
```

File genuinely exists, is executable, is a real script. The mismatch was
in what `--xsct-tool` expects as its argument.

### Third attempt — success

Passing the **installation root directory**, not the binary path, is what
`gen-machineconf` actually wants for `--xsct-tool`:

```bash
./gen-machineconf --soc-family zynq \
  --hw-description ~/aurascope-yocto/aurascope/hw/design_2_wrapper.xsa \
  --machine-name aurascope-arty-z7 \
  --xsct-tool /opt/2025.2/Vitis
```

**Full pipeline ran to completion:**

```
***** Gen Machine Conf v2025.2
NOTE: Starting bitbake server...
NOTE: Unpacking .../design_2_wrapper.xsa to .../build/.hw-description/
[INFO] Using HW file: .../build/hw-description/aurascope-arty-z7/.../design_2_wrapper.xsa
WARNING: You have included the meta-xilinx-standalone layer, but it has not
been enabled using XILINX_WITH_ESW ...
WARNING: No bb files in default matched BBFILE_PATTERN_meta-aurascope ...
[INFO] Getting Platform info from HW file
[INFO] Generating Kconfig for project
[INFO] Silentconfig project
[INFO] Generating configuration files
[INFO] Generating machine conf file
[NOTE] To enable this, add the following to your local.conf:
# Use the newly generated MACHINE
MACHINE = "aurascope-arty-z7"
```

Both warnings are expected and harmless (standalone/ESW relates to
bare-metal BSP features not used here; the empty-layer warning is because
`meta-aurascope` has no recipes yet). The five `[INFO]` lines are the real
pipeline: read the `.xsa` → derive a Kconfig → silent-configure it →
generate config files → emit the machine `.conf`.

> **Note on the tool itself:** `gen-machineconf` is a standalone Python
> script, not a BitBake invocation — it prints "Starting bitbake server"
> because it reuses BitBake's config-parsing library internally, but it
> runs offline, reads the `.xsa`, and writes a machine `.conf` as plain
> text. No build happens at this step. `bitbake` proper only enters once
> the generated machine conf is wired into `local.conf` and an image
> target is actually built.

### ✅ Checkpoint — CONFIRMED

Machine `.conf` generated from the project's real hardware and the tool
explicitly reported the exact line to use:

```
MACHINE = "aurascope-arty-z7"
```

This is the same `.xsa` verified earlier to contain `axi_gpio_0` (switches,
`0x41200000`) and `axi_gpio_1` (LEDs ch.2 / buttons ch.1, `0x41210000`).

---

## Next (not yet done)

- [ ] Locate the generated machine `.conf` under `build/.hw-description/`
      (or wherever `gen-machineconf` placed it) and confirm it references
      `axi_gpio_0`/`axi_gpio_1` at the expected addresses.
- [ ] Move/copy the generated machine conf into `meta-aurascope/conf/machine/`
      so it ships from the project's own layer.
- [ ] Set `MACHINE = "aurascope-arty-z7"` in `conf/local.conf`, as the
      tool itself instructed.
- [ ] `bitbake aurascope-image` (the custom image recipe, not
      `core-image-sato`) — first real build.
- [ ] Flash, boot, confirm login prompt. → tag `v0.1-yocto-boot`.

---

## Repo state after this log

```
aurascope/
├── meta-aurascope/          # layer, scarthgap-compatible, priority 6
├── hw/
│   └── design_2_wrapper.xsa # committed
├── conf-templates/
│   ├── local.conf.sample
│   └── bblayers.conf.sample
├── docs/notes/
│   └── PINNED-COMMITS.txt
├── nrf/                     # empty, Phase 2
└── (this file)
```

## Honest summary for the article

The straight-line version of Phase 1 would read as five clean commands.
It wasn't that. Two separate struggles dominated the real time spent:

**The layer dependency chain (§4–5).** What looked like "register five
layers" turned into a cascade: `meta-arm` needed its own toolchain
sub-layer first; `meta-xilinx-core` silently required `meta-arm`; and
following the deprecated BSP layer's own README toward the "correct" SDT
migration path led four layers deep into virtualization, TPM, and
security infrastructure that has nothing to do with a Zynq-7020 LED
project. The fix wasn't finding one missing layer — it was recognizing
the recommended path was disproportionate and deliberately stepping back
to a simpler, older tool that still worked.

**The XSCT path format (§6).** Even after choosing the right tool, the
`--xsct-tool` argument silently expected an installation root directory,
not the executable path that `find` had just confirmed existed — a small
thing, but it took three attempts and a permissions/symlink sanity check
to rule out before landing on the actual fix.

Neither of these is a bug in the traditional sense. Both are the kind of
"the tooling has more shape than the docs suggest" problem that's common
in real Yocto/BSP work and rarely shows up in tutorials — which is exactly
why it belongs in the article.
