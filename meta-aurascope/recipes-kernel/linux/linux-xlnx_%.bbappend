FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI += "file://usb-host.cfg"
SRC_URI += "file://sound-core.cfg"
SRC_URI += "file://observ.cfg"

# CONFIG_DEBUG_INFO_BTF in observ.cfg needs pahole >=1.16 visible at kernel
# configure time (lib/Kconfig.debug: "depends on PAHOLE_VERSION >= 116").
# Without this, olddefconfig silently falls back to DEBUG_INFO_NONE instead
# of failing loudly.
DEPENDS += "pahole-native"
