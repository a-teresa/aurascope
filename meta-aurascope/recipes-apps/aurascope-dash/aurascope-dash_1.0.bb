SUMMARY = "AuraScope live audio dashboard (ALSA capture + SSE + Canvas UI)"
DESCRIPTION = "Captures ALSA audio, computes RMS/peak/FFT, traces the ALSA \
period/xrun path via ply, and serves a Canvas + Server-Sent-Events dashboard \
over a hand-rolled HTTP server. Phase 3 (M1 + M3) of the AuraScope \
observability plan."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "alsa-lib"
RDEPENDS:${PN} = "ply"

inherit update-rc.d

INITSCRIPT_NAME = "aurascope-dash"
INITSCRIPT_PARAMS = "start 90 5 . stop 10 0 1 6 ."

SRC_URI = " \
    file://main.c \
    file://audio.c \
    file://audio.h \
    file://http.c \
    file://http.h \
    file://fft.c \
    file://fft.h \
    file://health.c \
    file://health.h \
    file://Makefile \
    file://index.html \
    file://aurascope-dash.init \
    "

S = "${WORKDIR}"

do_compile() {
    oe_runmake
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/aurascope-dash ${D}${bindir}/aurascope-dash

    install -d ${D}${datadir}/aurascope-dash
    install -m 0644 ${S}/index.html ${D}${datadir}/aurascope-dash/index.html

    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${S}/aurascope-dash.init ${D}${sysconfdir}/init.d/aurascope-dash
}

FILES:${PN} += "${datadir}/aurascope-dash"
