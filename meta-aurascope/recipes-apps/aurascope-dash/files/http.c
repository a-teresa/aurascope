#include "http.h"
#include "audio.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    int  fd;
    char webroot[256];
} conn_ctx_t;

static void send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n <= 0)
            return;
        sent += (size_t)n;
    }
}

static void serve_index(int fd, const char *webroot) {
    char path[300];
    snprintf(path, sizeof(path), "%s/index.html", webroot);

    FILE *f = fopen(path, "rb");
    if (!f) {
        const char *msg = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send_all(fd, msg, strlen(msg));
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char header[128];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n",
        size);
    send_all(fd, header, (size_t)hlen);

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        send_all(fd, buf, n);
    fclose(f);
}

static void serve_stream(int fd) {
    const char *header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    send_all(fd, header, strlen(header));

    static char json[8192];
    for (;;) {
        audio_snapshot_t snap;
        pthread_mutex_lock(&g_snapshot_lock);
        snap = g_snapshot;
        pthread_mutex_unlock(&g_snapshot_lock);

        int len = snprintf(json, sizeof(json),
            "{\"t\":%ld,\"rms_db\":%.1f,\"peak_db\":%.1f,\"peak_hold_db\":%.1f,\"wave\":[",
            snap.ts_ms, snap.rms_db, snap.peak_db, snap.peak_hold_db);

        for (int i = 0; i < WAVE_POINTS && len < (int)sizeof(json) - 64; i++)
            len += snprintf(json + len, sizeof(json) - (size_t)len, "%s%.3f",
                             i ? "," : "", snap.wave[i]);

        len += snprintf(json + len, sizeof(json) - (size_t)len, "],\"spec\":[");

        for (int i = 0; i < SPEC_BARS && len < (int)sizeof(json) - 64; i++)
            len += snprintf(json + len, sizeof(json) - (size_t)len, "%s%.1f",
                             i ? "," : "", snap.spec[i]);

        len += snprintf(json + len, sizeof(json) - (size_t)len, "]}");

        static char frame[8300];
        int flen = snprintf(frame, sizeof(frame), "data: %s\n\n", json);

        size_t sent = 0;
        int fail = 0;
        while (sent < (size_t)flen) {
            ssize_t n = write(fd, frame + sent, (size_t)flen - sent);
            if (n <= 0) {
                fail = 1;
                break;
            }
            sent += (size_t)n;
        }
        if (fail)
            break;

        usleep(100 * 1000); /* ~10 Hz */
    }
}

static void *conn_thread(void *arg) {
    conn_ctx_t *ctx = (conn_ctx_t *)arg;

    char req[1024];
    ssize_t n = read(ctx->fd, req, sizeof(req) - 1);
    if (n <= 0) {
        close(ctx->fd);
        free(ctx);
        return NULL;
    }
    req[n] = '\0';

    char method[8] = {0}, path[256] = {0};
    sscanf(req, "%7s %255s", method, path);

    if (strcmp(path, "/stream") == 0)
        serve_stream(ctx->fd);
    else if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)
        serve_index(ctx->fd, ctx->webroot);
    else {
        const char *msg = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send_all(ctx->fd, msg, strlen(msg));
    }

    close(ctx->fd);
    free(ctx);
    return NULL;
}

int http_server_run(int port, const char *webroot) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        perror("socket");
        return -1;
    }

    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(srv);
        return -1;
    }

    if (listen(srv, 8) < 0) {
        perror("listen");
        close(srv);
        return -1;
    }

    fprintf(stderr, "aurascope-dash: listening on :%d\n", port);

    for (;;) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int fd = accept(srv, (struct sockaddr *)&cli, &clilen);
        if (fd < 0)
            continue;

        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        conn_ctx_t *ctx = malloc(sizeof(conn_ctx_t));
        if (!ctx) {
            close(fd);
            continue;
        }
        ctx->fd = fd;
        snprintf(ctx->webroot, sizeof(ctx->webroot), "%s", webroot);

        pthread_t tid;
        if (pthread_create(&tid, NULL, conn_thread, ctx) != 0) {
            close(fd);
            free(ctx);
            continue;
        }
        pthread_detach(tid);
    }

    return 0;
}
