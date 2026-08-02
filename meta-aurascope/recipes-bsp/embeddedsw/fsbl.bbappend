# Zynq-7000 FSBL has no bitbake-native build path (XILINX_WITH_ESW="xsct"
# means bitbake expects a precompiled elf; XILINX_WITH_ESW="generic" is
# explicitly unsupported for zynq in meta-xilinx-standalone). Built once via
# the Vitis unified platform flow against hw/design_2_wrapper.xsa
# (ps7_cortexa9_0, standalone OS) and committed here.
FSBL_FILE := "${THISDIR}/files/fsbl"
