FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://system-top.dts"

SYSTEM_DTFILE = "${THISDIR}/files/system-top.dts"

# Pull in the real zynq-7000.dtsi shipped by linux-xlnx instead of
# hand-duplicating the SoC-level device tree.
KERNEL_INCLUDE = " \
    ${STAGING_KERNEL_DIR}/arch/${ARCH}/boot/dts \
    ${STAGING_KERNEL_DIR}/arch/${ARCH}/boot/dts/* \
    ${STAGING_KERNEL_DIR}/scripts/dtc/include-prefixes \
    "
