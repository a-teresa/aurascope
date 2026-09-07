SUMMARY = "AuraScope base image for the Arty Z7-20 (Zynq-7000)"

LICENSE = "MIT"

inherit core-image

IMAGE_INSTALL = "packagegroup-core-boot ${CORE_IMAGE_EXTRA_INSTALL}"
IMAGE_INSTALL:append = " alsa-utils alsa-lib aurascope-dash"

IMAGE_ROOTFS_SIZE ?= "8192"
IMAGE_ROOTFS_EXTRA_SPACE:append = "${@bb.utils.contains("DISTRO_FEATURES", "systemd", " + 4096", "", d)}"
