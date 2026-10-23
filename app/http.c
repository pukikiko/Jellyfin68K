#include "http.h"
#include "mactcp/TCPHi.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <Events.h>

static int gNetReady;
static Boolean gCancel = false;

static void GiveTime(void)
{
    EventRecord ev;
    WaitNextEvent(0, &ev, 0, NULL);
}

static ip_addr ParseAddress(const char *s)
{
    unsigned long addr = 0;
    int part = 0;
    int i;
    for (i = 0; s[i]; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            part = part * 10 + (s[i] - '0');
        } else if (s[i] == '.') {
            addr = (addr << 8) | (unsigned long)(part & 0xFF);
            part = 0;
        }
    }
    addr = (addr << 8) | (unsigned long)(part & 0xFF);
    return (ip_addr)addr;
}

int HttpOpen(HttpConn *conn, const char *host, short port)
{
    OSErr err;
    ip_addr addr;

    memset(conn, 0, sizeof(*conn));
    conn->status = -1;
    conn->contentLength = -1;

    if (!gNetReady) {
        err = MT_InitNetwork();
        if (err != noErr)
            return (int)err;
        gNetReady = 1;
    }

    err = MT_CreateStream(&conn->stream, 8192, GiveTime, &gCancel);
    if (err != noErr)
        return (int)err;

    addr = ParseAddress(host);
    err = MT_OpenConnection(conn->stream, (long)addr, port, 30, GiveTime,
                            &gCancel);
    if (err != noErr) {
        MT_ReleaseStream(conn->stream, GiveTime, &gCancel);
        conn->stream = 0;
        return (int)err;
    }
    return 0;
}

int HttpRequest(HttpConn *conn, const char *method, const char *path,
                const char *host, short port, const char *token,
                const char *accept, const char *body, long bodyLength)
{
    char request[2048];
    long n;
    OSErr err;

    n = (long)snprintf(request, sizeof(request),
                       "%s %s HTTP/1.1\r\n"
                       "Host: %s:%d\r\n"
                       "User-Agent: Jellyfin68K/1.0\r\n"
                       "X-Emby-Authorization: MediaBrowser Client=\"Jellyfin 68K\", Device=\"Macintosh\", DeviceId=\"jellyfin68k-01\", Version=\"1.0\"\r\n"
                       "Accept: %s\r\n"
                       "Accept-Encoding: identity\r\n"
                       "Connection: close\r\n",
                       method, path, host, (int)port,
                       accept ? accept : "*/*");
    if (n < 0 || n >= (long)sizeof(request))
        return -1;
    if (token && token[0]) {
        long add = (long)snprintf(request + n, sizeof(request) - n,
                                  "X-Emby-Token: %s\r\n", token);
        if (add < 0 || n + add >= (long)sizeof(request))
            return -1;
        n += add;
    }
    if (body && bodyLength > 0) {
        long add = (long)snprintf(request + n, sizeof(request) - n,
                                  "Content-Type: application/json\r\n"
                                  "Content-Length: %ld\r\n", bodyLength);
        if (add < 0 || n + add >= (long)sizeof(request))
            return -1;
        n += add;
    }
    if (n + 2 >= (long)sizeof(request))
        return -1;
    request[n++] = '\r';
    request[n++] = '\n';
    request[n] = 0;

    err = MT_SendData(conn->stream, request, (unsigned short)n, true,
                      GiveTime, &gCancel);
    if (err != noErr)
        return (int)err;
    if (body && bodyLength > 0) {
        err = MT_SendData(conn->stream, (Ptr)body, (unsigned short)bodyLength,
                          true, GiveTime, &gCancel);
        if (err != noErr)
            return (int)err;
    }
    return 0;
}

/* Fill the connection buffer. Returns 1 for data, 0 for EOF, -1 for error. */
static int Fill(HttpConn *conn)
{
    unsigned short n;
    OSErr err;

    if (conn->pos < conn->len)
        return 1;
    /* MT_RecvData appends a NUL byte after the received data. */
    n = sizeof(conn->buf) - 1;
    err = MT_RecvData(conn->stream, (Ptr)conn->buf, &n, true, GiveTime,
                      &gCancel);
    conn->pos = 0;
    if (err == connectionClosing) {
        conn->len = 0;
        return 0;
    }
    conn->len = (long)n;
    if (n == 0)
        return 0;
    if (err != noErr)
        return -1;
    return 1;
}

static int ReadByte(HttpConn *conn, int *out)
{
    int r = Fill(conn);
    if (r <= 0)
        return r;
    *out = conn->buf[conn->pos++];
    return 1;
}

static int ReadLine(HttpConn *conn, char *line, long max)
{
    long n = 0;
    for (;;) {
        int c;
        int r = ReadByte(conn, &c);
        if (r <= 0)
            return r;
        if (c == '\n') {
            if (n > 0 && line[n - 1] == '\r')
                n--;
            line[n] = 0;
            return 1;
        }
        if (n < max - 1)
            line[n++] = (char)c;
    }
}

static long HexValue(const char *s)
{
    long v = 0;
    while (*s) {
        int c = *s++;
        int d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            break;
        v = (v << 4) | d;
    }
    return v;
}

static int ReadExact(HttpConn *conn, unsigned char *dst, long count,
                     HttpBodyCallback callback, void *ctx)
{
    while (count > 0) {
        long avail;
        long take;
        int r = Fill(conn);
        if (r <= 0)
            return r;
        avail = conn->len - conn->pos;
        take = count < avail ? count : avail;
        if (dst) {
            memcpy(dst, conn->buf + conn->pos, take);
            dst += take;
        } else if (callback) {
            if (callback(conn->buf + conn->pos, take, ctx) != 0)
                return -1;
        }
        conn->pos += take;
        count -= take;
    }
    return 1;
}

static int ReadHeaders(HttpConn *conn)
{
    char line[256];
    int r;

    r = ReadLine(conn, line, sizeof(line));
    if (r <= 0)
        return -1;
    conn->status = 0;
    if (strncmp(line, "HTTP/", 5) == 0) {
        const char *p = line;
        while (*p && *p != ' ')
            p++;
        while (*p == ' ')
            p++;
        while (*p >= '0' && *p <= '9') {
            conn->status = conn->status * 10 + (*p - '0');
            p++;
        }
    }

    for (;;) {
        r = ReadLine(conn, line, sizeof(line));
        if (r <= 0)
            return -1;
        if (line[0] == 0)
            break;
        if (strncasecmp(line, "Transfer-Encoding:", 18) == 0) {
            if (strstr(line + 18, "chunked") || strstr(line + 18, "Chunked"))
                conn->chunked = 1;
        } else if (strncasecmp(line, "Content-Length:", 15) == 0) {
            conn->contentLength = atol(line + 15);
        } else if (strncasecmp(line, "Content-Type:", 13) == 0) {
            const char *p = line + 13;
            while (*p == ' ')
                p++;
            strncpy(conn->contentType, p, sizeof(conn->contentType) - 1);
            conn->contentType[sizeof(conn->contentType) - 1] = 0;
        }
    }
    return 0;
}

static int ReadChunked(HttpConn *conn, HttpBodyCallback callback, void *ctx)
{
    char line[128];
    for (;;) {
        long size;
        int r = ReadLine(conn, line, sizeof(line));
        if (r <= 0)
            return -1;
        size = HexValue(line);
        if (size == 0) {
            /* Consume trailer headers. */
            for (;;) {
                r = ReadLine(conn, line, sizeof(line));
                if (r <= 0)
                    return 0;
                if (line[0] == 0)
                    return 0;
            }
        }
        r = ReadExact(conn, NULL, size, callback, ctx);
        if (r <= 0)
            return -1;
        {
            int c1, c2;
            if (ReadByte(conn, &c1) <= 0 || ReadByte(conn, &c2) <= 0)
                return -1;
            if (c1 != '\r' || c2 != '\n')
                return -1;
        }
    }
}

#define BODY_LENGTH 1
#define BODY_CHUNKED 2
#define BODY_CLOSE 3

#define CHUNK_SIZE 0
#define CHUNK_DATA 1
#define CHUNK_CRLF 2
#define CHUNK_TRAILER 3
#define CHUNK_LF 4

int HttpReadResponseBegin(HttpConn *conn)
{
    int r = ReadHeaders(conn);

    if (r < 0)
        return -1;

    conn->bodyDone = 0;
    conn->eofPending = 0;
    conn->asyncPending = 0;
    conn->asyncPB = NULL;
    conn->lineLen = 0;
    conn->remaining = 0;

    if (conn->chunked) {
        conn->bodyMode = BODY_CHUNKED;
        conn->chunkPhase = CHUNK_SIZE;
    } else if (conn->contentLength >= 0) {
        conn->bodyMode = BODY_LENGTH;
        conn->remaining = conn->contentLength;
        if (conn->remaining == 0)
            conn->bodyDone = 1;
    } else {
        conn->bodyMode = BODY_CLOSE;
    }
    return 0;
}

/* Consume buffered body bytes.  Returns 1 if the caller should try again
 * later (the sink is full or more network data is needed), 0 when the body
 * is complete, and -1 on a protocol error. */
static int ProcessBody(HttpConn *conn, HttpBodyCallback callback, void *ctx)
{
    for (;;) {
        while (conn->pos < conn->len) {
            long avail = conn->len - conn->pos;

            if (conn->bodyMode == BODY_LENGTH) {
                long take = avail < conn->remaining ? avail : conn->remaining;
                if (take > 0) {
                    int rc = callback(conn->buf + conn->pos, take, ctx);
                    if (rc < 0)
                        return -1;
                    if (rc > 0)
                        return 1;
                }
                conn->pos += take;
                conn->remaining -= take;
                if (conn->remaining == 0) {
                    conn->bodyDone = 1;
                    return 0;
                }
            } else if (conn->bodyMode == BODY_CLOSE) {
                int rc = callback(conn->buf + conn->pos, avail, ctx);
                if (rc < 0)
                    return -1;
                if (rc > 0)
                    return 1;
                conn->pos = conn->len;
            } else if (conn->chunkPhase == CHUNK_SIZE ||
                       conn->chunkPhase == CHUNK_TRAILER) {
                int gotLine = 0;
                while (conn->pos < conn->len) {
                    int c = conn->buf[conn->pos++];
                    if (c == '\n') {
                        gotLine = 1;
                        break;
                    }
                    if (c != '\r' && conn->lineLen < (int)sizeof(conn->line) - 1)
                        conn->line[conn->lineLen++] = (char)c;
                }
                if (!gotLine)
                    break;

                conn->line[conn->lineLen] = 0;
                if (conn->chunkPhase == CHUNK_TRAILER) {
                    if (conn->lineLen == 0) {
                        conn->bodyDone = 1;
                        return 0;
                    }
                } else {
                    long size = HexValue(conn->line);
                    if (size == 0) {
                        conn->chunkPhase = CHUNK_TRAILER;
                    } else {
                        conn->remaining = size;
                        conn->chunkPhase = CHUNK_DATA;
                    }
                }
                conn->lineLen = 0;
            } else if (conn->chunkPhase == CHUNK_DATA) {
                long take = avail < conn->remaining ? avail : conn->remaining;
                if (take > 0) {
                    int rc = callback(conn->buf + conn->pos, take, ctx);
                    if (rc < 0)
                        return -1;
                    if (rc > 0)
                        return 1;
                }
                conn->pos += take;
                conn->remaining -= take;
                if (conn->remaining == 0)
                    conn->chunkPhase = CHUNK_CRLF;
            } else if (conn->chunkPhase == CHUNK_LF) {
                /* The previous buffer ended between CR and LF. */
                if (conn->buf[conn->pos++] != '\n')
                    return -1;
                conn->chunkPhase = CHUNK_SIZE;
            } else { /* CHUNK_CRLF */
                int c = conn->buf[conn->pos++];
                if (c == '\r') {
                    conn->chunkPhase = CHUNK_LF;
                } else if (c == '\n') {
                    conn->chunkPhase = CHUNK_SIZE;
                } else {
                    return -1;
                }
            }
        }

        if (conn->bodyDone)
            return 0;

        if (conn->eofPending) {
            conn->eofPending = 0;
            if (conn->bodyMode == BODY_CLOSE) {
                conn->bodyDone = 1;
                return 0;
            }
            if (conn->bodyMode == BODY_LENGTH && conn->remaining == 0) {
                conn->bodyDone = 1;
                return 0;
            }
            return -1;
        }

        if (conn->asyncPending) {
            unsigned short n = 0;
            OSErr err;

            if (conn->asyncPB->ioResult > 0)
                return 1;

            err = MT_GetDataLength(conn->asyncPB, &n);
            conn->asyncPending = 0;
            conn->asyncPB = NULL;
            conn->pos = 0;
            conn->len = 0;

            /* A timeout leaves the requested length in the parameter
             * block, so only trust n for real completions. */
            if (err == commandTimeout)
                return 1;
            if (err != noErr && err != connectionClosing)
                return -1;

            if (n > 0) {
                conn->len = (long)n;
                if (err == connectionClosing)
                    conn->eofPending = 1;
                continue;
            }

            if (conn->bodyMode == BODY_CLOSE) {
                conn->bodyDone = 1;
                return 0;
            }
            if (conn->bodyMode == BODY_LENGTH && conn->remaining == 0) {
                conn->bodyDone = 1;
                return 0;
            }
            return -1;
        }

        /* Need more data.  Do not read ahead when the sink has already
         * stopped accepting bytes, so TCP applies backpressure. */
        conn->pos = 0;
        conn->len = 0;
        conn->asyncPB = NULL;
        MT_RecvDataAsync(conn->stream, (Ptr)conn->buf, sizeof(conn->buf),
                         &conn->asyncPB, GiveTime, &gCancel);
        if (!conn->asyncPB)
            return -1;
        conn->asyncPending = 1;
    }
}

int HttpReadBodyStep(HttpConn *conn, HttpBodyCallback callback, void *ctx)
{
    if (conn->bodyDone)
        return 0;
    return ProcessBody(conn, callback, ctx);
}

int HttpReadResponse(HttpConn *conn, HttpBodyCallback callback, void *ctx)
{
    int r;

    r = ReadHeaders(conn);
    if (r < 0)
        return -1;

    if (conn->chunked)
        return ReadChunked(conn, callback, ctx);

    if (conn->contentLength >= 0)
        return ReadExact(conn, NULL, conn->contentLength, callback, ctx) == 1
                   ? 0
                   : -1;

    /* No length: read until the server closes. */
    for (;;) {
        long avail;
        r = Fill(conn);
        if (r == 0)
            return 0;
        if (r < 0)
            return -1;
        avail = conn->len - conn->pos;
        if (avail > 0 && callback) {
            if (callback(conn->buf + conn->pos, avail, ctx) != 0)
                return -1;
        }
        conn->pos = conn->len;
    }
}

void HttpClose(HttpConn *conn)
{
    if (conn->asyncPending && conn->asyncPB) {
        if (conn->asyncPB->ioResult > 0)
            LowKillTCP(conn->asyncPB);
        DisposePtr((Ptr)conn->asyncPB);
        conn->asyncPB = NULL;
        conn->asyncPending = 0;
    }
    if (conn->stream) {
        MT_CloseConnection(conn->stream, GiveTime, &gCancel);
        MT_ReleaseStream(conn->stream, GiveTime, &gCancel);
        conn->stream = 0;
    }
}
