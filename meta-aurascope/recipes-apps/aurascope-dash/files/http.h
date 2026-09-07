#ifndef HTTP_H
#define HTTP_H

/* Blocks forever serving GET / (webroot/index.html) and GET /stream (SSE). */
int http_server_run(int port, const char *webroot);

#endif
