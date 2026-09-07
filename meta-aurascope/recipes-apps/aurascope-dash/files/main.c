#include "audio.h"
#include "health.h"
#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *device  = "default";
    const char *webroot = "/usr/share/aurascope-dash";
    int port = 8080;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc)
            device = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            port = atoi(argv[++i]);
        else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc)
            webroot = argv[++i];
        else {
            fprintf(stderr, "usage: %s [-d alsa-device] [-p port] [-w webroot]\n", argv[0]);
            return 1;
        }
    }

    if (audio_capture_start(device) != 0) {
        fprintf(stderr, "aurascope-dash: failed to start capture thread\n");
        return 1;
    }

    if (health_monitor_start() != 0)
        fprintf(stderr, "aurascope-dash: health monitor failed to start, continuing without it\n");

    return http_server_run(port, webroot) == 0 ? 0 : 1;
}
