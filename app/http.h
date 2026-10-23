/* Small HTTP/1.1 client over MacTCP for the Jellyfin 68K client. */
#ifndef JF_HTTP_H
#define JF_HTTP_H

#include "MacTCP.h"

typedef struct {
    StreamPtr stream;
    unsigned char buf[4096];
    long pos;
    long len;
    int status;
    int chunked;
    long contentLength;
    char contentType[64];

    /* Incremental body reader used for audio streaming.  Headers are read
     * synchronously; the body is then pulled a buffer at a time from the
     * event loop so the UI stays responsive. */
    int bodyMode;             /* 0 none, 1 length, 2 chunked, 3 until close */
    int bodyDone;
    int eofPending;
    long remaining;           /* bytes left in the current chunk/body */
    int chunkPhase;           /* 0 size line, 1 data, 2 data CRLF, 3 trailer */
    char line[128];
    int lineLen;
    int asyncPending;
    TCPiopb *asyncPB;
} HttpConn;

typedef int (*HttpBodyCallback)(const unsigned char *data, long length,
                                void *ctx);

/* Returns 0 on success. */
int HttpOpen(HttpConn *conn, const char *host, short port);
int HttpRequest(HttpConn *conn, const char *method, const char *path,
                const char *host, short port, const char *token,
                const char *accept, const char *body, long bodyLength);

/* Blocking response reader, used for login and the library. */
int HttpReadResponse(HttpConn *conn, HttpBodyCallback callback, void *ctx);

/* Streaming response: read and parse the headers, then call
 * HttpReadBodyStep() from the main loop until it returns 0 (complete) or
 * -1 (error).  It returns 1 while more data is expected. */
int HttpReadResponseBegin(HttpConn *conn);
int HttpReadBodyStep(HttpConn *conn, HttpBodyCallback callback, void *ctx);

void HttpClose(HttpConn *conn);

#endif
