/*
 * Jellyfin 68K - a music client for classic Macintosh.
 *
 * Talks to a Jellyfin server over plain HTTP, asks the server to transcode
 * to raw 16-bit big-endian PCM (8 kHz mono), and streams it straight into
 * the Sound Manager's double buffer.  There is no MP3 decode on the Mac.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Events.h>
#include <Menus.h>
#include <Dialogs.h>
#include <TextEdit.h>
#include <ToolUtils.h>
#include <Files.h>
#include <Devices.h>
#include <OSUtils.h>
#include <Memory.h>

#include "MacTCP.h"
#include "http.h"
#include "json.h"
#include "audio.h"

#define MAX_TRACKS 400
/* 400 tracks are about 330 KB of Jellyfin JSON.  Keep a little headroom and
 * do not reserve 1.2 MB of a small Mac's memory for it. */
#define MAX_JSON (768L * 1024L)

/* Compact layout for the 512x342 display of a Macintosh Classic.  At that
 * size the content area is 504x296: twelve 16-pixel rows plus the header and
 * footer.  Larger screens keep the same compact width and add rows. */
#define WINDOW_WIDTH 504
#define WINDOW_HEIGHT 312
#define ROW_HEIGHT 16
#define LIST_FRAME_TOP 44
#define LIST_TOP 58
#define SEARCH_TOP 22
#define SEARCH_BOTTOM 42
#define SEARCH_RIGHT_GAP 132
#define SORT_WIDTH 118
#define COL_NUM_X 12
#define COL_TITLE_X 34

#define DEFAULT_HOST "10.0.1.1"
#define DEFAULT_PORT 8096
#define DEFAULT_USER "guest"
#define DEFAULT_PASS ""
#define DEVICE_ID "jellyfin68k-01"

typedef struct {
    char id[40];
    char name[80];
    char artist[80];
    char albumArtist[80];
    char album[80];
    long seconds;
    long trackNo;
    long discNo;
} Track;

enum {
    SORT_ALBUM_ARTIST = 0,
    SORT_ARTIST,
    SORT_ALBUM,
    SORT_TITLE,
    SORT_COUNT
};

static const char *gSortNames[SORT_COUNT] = {
    "Album Artist", "Artist", "Album", "Title"
};

static WindowPtr gWin;
static Track gTracks[MAX_TRACKS];
static int gTrackCount;
static int gOrder[MAX_TRACKS];   /* all tracks in the active sort order */
static int gView[MAX_TRACKS];    /* gOrder indices matching the search */
static int gViewCount;
static int gSelected;
static int gTop;
static int gSortMode = SORT_ALBUM_ARTIST;
static char gQuery[64];
static int gSearchActive;
static long gLastClickTick;
static int gLastClickRow = -1;
static char gStatus[160];
static char gToken[96];
static char gUserId[48];
static char gHost[64];
static int gPort;
static char gUser[48];
static char gPass[64];
static int gBusy;
static int gPlayingIndex = -1;
static char gNowPlaying[96];

static short gColArtistX;
static short gColAlbumX;
static int gListRows;
static short gListBottom;

static char gJson[MAX_JSON];
static long gJsonLen;

static HttpConn gAudioConn;
static int gDownloadActive;
static int gLastFooterSecond = -1;
static int gPlaybackSerial;

static short gLogRef;

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static void DrawCString(const char *s)
{
    Str255 p;
    long n = (long)strlen(s);
    if (n > 255)
        n = 255;
    p[0] = (unsigned char)n;
    memcpy(p + 1, s, n);
    DrawString(p);
}

static RgnHandle gSavedClip;

/* Draw a C string, clipped to a rectangle.  This is much cheaper than
 * measuring and truncating every field on a 68K. */
static void DrawTextAt(const char *s, short x, short y, const Rect *clip)
{
    if (gSavedClip) {
        GetClip(gSavedClip);
        ClipRect((Rect *)clip);
    }
    MoveTo(x, y);
    DrawText((Ptr)s, 0, (short)strlen(s));
    if (gSavedClip)
        SetClip(gSavedClip);
}

static void DrawRightCString(const char *s, short right, short y)
{
    Str255 p;
    long n = (long)strlen(s);
    if (n > 255)
        n = 255;
    p[0] = (unsigned char)n;
    memcpy(p + 1, s, n);
    MoveTo((short)(right - StringWidth(p)), y);
    DrawString(p);
}

static void SetStatus(const char *s)
{
    strncpy(gStatus, s, sizeof(gStatus) - 1);
    gStatus[sizeof(gStatus) - 1] = 0;
}

static void CopyField(char *dst, long dstSize, const char *src)
{
    if (!src)
        src = "";
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = 0;
}

/* Convert UTF-8-ish Jellyfin names to something that looks sane in the
 * MacRoman Geneva font: common punctuation becomes ASCII, other non-ASCII
 * code points become one question mark each. */
static void CopyDisplay(char *dst, long dstSize, const char *src)
{
    long out = 0;
    if (!src)
        src = "";
    while (*src && out < dstSize - 1) {
        unsigned char c = (unsigned char)*src++;
        if (c < 0x80) {
            dst[out++] = (char)c;
        } else if (c == 0xE2 && (unsigned char)src[0] == 0x80) {
            unsigned char c2 = (unsigned char)src[1];
            if (c2 == 0x98 || c2 == 0x99) {
                dst[out++] = '\'';
                src += 2;
            } else if (c2 == 0x9C || c2 == 0x9D) {
                dst[out++] = '"';
                src += 2;
            } else if (c2 == 0x93 || c2 == 0x94) {
                dst[out++] = '-';
                src += 2;
            } else if (c2 == 0xA6) {
                if (out + 3 < dstSize - 1) {
                    memcpy(dst + out, "...", 3);
                    out += 3;
                }
                src += 2;
            } else {
                dst[out++] = '?';
                src += 2;
            }
        } else {
            while (*src && ((unsigned char)*src & 0xC0) == 0x80)
                src++;
            dst[out++] = '?';
        }
    }
    dst[out] = 0;
}

static char LowerChar(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static int CompareCI(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = LowerChar(*a++);
        char cb = LowerChar(*b++);
        if (ca != cb)
            return ca < cb ? -1 : 1;
    }
    if (*a == *b)
        return 0;
    return *a ? 1 : -1;
}

/* Empty metadata sorts after real values instead of all at the top. */
static int CompareKeyCI(const char *a, const char *b)
{
    if (!a[0] && !b[0])
        return 0;
    if (!a[0])
        return 1;
    if (!b[0])
        return -1;
    return CompareCI(a, b);
}

static int ContainsCI(const char *haystack, const char *needle, long length)
{
    const char *p;
    for (p = haystack; *p; p++) {
        long i;
        for (i = 0; i < length; i++) {
            if (!p[i] || LowerChar(p[i]) != LowerChar(needle[i]))
                break;
        }
        if (i == length)
            return 1;
    }
    return 0;
}

static int TrackMatches(const Track *t, const char *query)
{
    const char *p = query;
    while (*p) {
        const char *start;
        long length;
        while (*p == ' ')
            p++;
        start = p;
        while (*p && *p != ' ')
            p++;
        length = p - start;
        if (length > 0) {
            if (!ContainsCI(t->name, start, length) &&
                !ContainsCI(t->artist, start, length) &&
                !ContainsCI(t->albumArtist, start, length) &&
                !ContainsCI(t->album, start, length))
                return 0;
        }
    }
    return 1;
}

static void LogLine(const char *s)
{
    long len = (long)strlen(s);
    if (gLogRef)
        FSWrite(gLogRef, &len, s);
}

static void LogNum(const char *label, long n)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s%ld\r", label, n);
    LogLine(buf);
}

static void OpenLog(void)
{
    FSSpec spec;
    OSErr err = FSMakeFSSpec(0, 0, "\pUnix:jellyfin.log", &spec);
    if (err == noErr)
        FSpDelete(&spec);
    if (FSpCreate(&spec, 'ttxt', 'ttxt', smSystemScript) == noErr)
        FSpOpenDF(&spec, fsWrPerm, &gLogRef);
}

/* The status shares the title row with the program name and the match
 * count, so updating it never repaints the list. */
static Rect StatusRect(void)
{
    Rect r = gWin->portRect;
    Rect area;

    area.left = 8;
    area.top = 1;
    area.right = r.right - 62;
    area.bottom = 19;
    return area;
}

static Rect CountRect(void)
{
    Rect r = gWin->portRect;
    Rect area;

    area.left = r.right - 60;
    area.top = 1;
    area.right = r.right - 8;
    area.bottom = 19;
    return area;
}

static void DrawStatus(void)
{
    Rect area = StatusRect();

    SetPort(gWin);
    TextFont(kFontIDGeneva);
    TextSize(10);
    TextFace(0);
    EraseRect(&area);
    DrawTextAt(gStatus[0] ? gStatus : "Starting...", 12, 15, &area);
}

static void DrawCount(void)
{
    Rect area = CountRect();
    char tmp[24];

    SetPort(gWin);
    TextFont(kFontIDGeneva);
    TextSize(10);
    TextFace(0);
    EraseRect(&area);
    if (gQuery[0])
        snprintf(tmp, sizeof(tmp), "%d/%d", gViewCount, gTrackCount);
    else
        snprintf(tmp, sizeof(tmp), "%d", gTrackCount);
    DrawRightCString(tmp, (short)(area.right - 2), 15);
}

/* Kept as a name used by the long login/library operations. */
static void ShowStatusNow(void)
{
    if (gWin)
        DrawStatus();
}

/* ------------------------------------------------------------------ */
/* Preferences                                                        */
/* ------------------------------------------------------------------ */

static void ParseConfigBuffer(char *buf, long length)
{
    char *p = buf;

    buf[length] = 0;
    while (*p) {
        char *line = p;
        char *eq;
        while (*p && *p != '\r' && *p != '\n')
            p++;
        if (*p)
            *p++ = 0;
        while (*p == '\r' || *p == '\n')
            p++;
        if (line[0] == '#' || line[0] == 0)
            continue;
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq++ = 0;
        while (*eq == ' ' || *eq == '\t')
            eq++;
        {
            char *end = line + strlen(line);
            while (end > line && (end[-1] == ' ' || end[-1] == '\t'))
                *--end = 0;
        }
        if (strcmp(line, "host") == 0)
            CopyField(gHost, sizeof(gHost), eq);
        else if (strcmp(line, "port") == 0)
            gPort = atoi(eq);
        else if (strcmp(line, "user") == 0)
            CopyField(gUser, sizeof(gUser), eq);
        else if (strcmp(line, "pass") == 0)
            CopyField(gPass, sizeof(gPass), eq);
    }
}

static int ReadConfigFile(void)
{
    static const unsigned char *paths[] = {
        "\pUnix:Jellyfin68K.cfg",
        "\pJellyfin68K.cfg",
        "\pSystem Folder:Preferences:Jellyfin68K.cfg"
    };
    int i;

    for (i = 0; i < 3; i++) {
        FSSpec spec;
        short ref;
        OSErr err;
        char buf[512];
        long len = sizeof(buf) - 1;

        err = FSMakeFSSpec(0, 0, (ConstStr255Param)paths[i], &spec);
        if (err != noErr)
            continue;
        err = FSpOpenDF(&spec, fsRdPerm, &ref);
        if (err != noErr)
            continue;
        err = FSRead(ref, &len, buf);
        FSClose(ref);
        if (err != noErr && err != eofErr)
            continue;
        ParseConfigBuffer(buf, len);
        LogLine("Config file: ");
        LogLine((const char *)paths[i] + 1);
        LogLine("\r");
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* HTTP body sinks                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    char *buf;
    long length;
    long max;
} BufferSink;

static int BufferSinkWrite(const unsigned char *data, long length, void *ctx)
{
    BufferSink *sink = (BufferSink *)ctx;
    if (sink->length + length >= sink->max)
        return -1;
    memcpy(sink->buf + sink->length, data, length);
    sink->length += length;
    sink->buf[sink->length] = 0;
    return 0;
}

/* The audio body is PCM now, so there is nothing to decode: hand each
 * network block straight to the Sound Manager ring. */
static int AudioSinkWrite(const unsigned char *data, long length, void *ctx)
{
    (void)ctx;
    return AudioPcmWrite(data, length);
}

/* ------------------------------------------------------------------ */
/* Library                                                            */
/* ------------------------------------------------------------------ */

static int JsonGetToken(void)
{
    JsonValue *root;
    JsonValue *user;
    JsonValue *token;
    JsonValue *id;

    root = JsonParse(gJson, gJsonLen);
    if (!root) {
        SetStatus("Login: JSON parse failed");
        return -1;
    }
    user = JsonGet(root, "User");
    token = JsonGet(root, "AccessToken");
    id = user ? JsonGet(user, "Id") : 0;
    if (!token || !id) {
        SetStatus("Login failed (check username/password)");
        return -1;
    }
    CopyField(gToken, sizeof(gToken), JsonString(token));
    CopyField(gUserId, sizeof(gUserId), JsonString(id));
    return 0;
}

static int Login(void)
{
    HttpConn conn;
    BufferSink sink;
    char body[256];
    int err;
    long bodyLen;

    SetStatus("Signing in to Jellyfin...");
    ShowStatusNow();
    LogLine("Login start\r");
    LogLine(gHost);
    LogLine(":");
    LogNum("", (long)gPort);
    LogLine("\r");
    bodyLen = (long)snprintf(body, sizeof(body),
                             "{\"Username\":\"%s\",\"Pw\":\"%s\"}",
                             gUser, gPass);
    memset(&sink, 0, sizeof(sink));
    sink.buf = gJson;
    sink.max = sizeof(gJson);

    err = HttpOpen(&conn, gHost, (short)gPort);
    LogNum("Login HttpOpen: ", err);
    if (err != 0) {
        if (err == -23)
            SetStatus("MacTCP .IPP not open - open MacTCP, close it, relaunch");
        else {
            char tmp[80];
            snprintf(tmp, sizeof(tmp), "MacTCP connect error %d", err);
            SetStatus(tmp);
        }
        return err;
    }
    err = HttpRequest(&conn, "POST", "/Users/AuthenticateByName",
                      gHost, (short)gPort, "", "application/json",
                      body, bodyLen);
    if (err == 0) {
        gJsonLen = 0;
        err = HttpReadResponse(&conn, BufferSinkWrite, &sink);
        LogNum("Login read: ", err);
        LogNum("Login status: ", (long)conn.status);
        LogNum("Login body bytes: ", sink.length);
        if (err == 0 && conn.status != 200) {
            char tmp[80];
            snprintf(tmp, sizeof(tmp), "Login HTTP %d", conn.status);
            SetStatus(tmp);
            err = -1;
        }
    }
    HttpClose(&conn);
    if (err != 0) {
        if (gStatus[0] == 0 || strncmp(gStatus, "Signing", 7) == 0)
            SetStatus("Login request failed");
        return err;
    }
    gJsonLen = sink.length;
    err = JsonGetToken();
    LogNum("Login token parse: ", err);
    LogLine("UserId: ");
    LogLine(gUserId);
    LogLine("\r");
    return err;
}

static void AddTrack(const char *name, const char *id, const char *artist,
                     const char *albumArtist, const char *album,
                     long seconds, long trackNo, long discNo, void *ctx)
{
    int *count = (int *)ctx;
    Track *t;

    if (*count >= MAX_TRACKS)
        return;
    t = &gTracks[*count];
    CopyDisplay(t->name, sizeof(t->name), name);
    CopyField(t->id, sizeof(t->id), id);
    CopyDisplay(t->artist, sizeof(t->artist), artist);
    CopyDisplay(t->albumArtist, sizeof(t->albumArtist), albumArtist);
    CopyDisplay(t->album, sizeof(t->album), album);
    if (!t->albumArtist[0])
        CopyField(t->albumArtist, sizeof(t->albumArtist), t->artist);
    if (!t->artist[0])
        CopyField(t->artist, sizeof(t->artist), t->albumArtist);
    t->seconds = seconds;
    t->trackNo = trackNo;
    t->discNo = discNo;
    (*count)++;
}

static int CompareTracks(const Track *a, const Track *b)
{
    int c = 0;

    switch (gSortMode) {
    case SORT_ALBUM_ARTIST:
        c = CompareKeyCI(a->albumArtist, b->albumArtist);
        if (!c)
            c = CompareKeyCI(a->album, b->album);
        break;
    case SORT_ARTIST:
        c = CompareKeyCI(a->artist, b->artist);
        if (!c)
            c = CompareKeyCI(a->album, b->album);
        break;
    case SORT_ALBUM:
        c = CompareKeyCI(a->album, b->album);
        break;
    case SORT_TITLE:
        c = CompareCI(a->name, b->name);
        break;
    default:
        break;
    }
    if (!c)
        c = (a->discNo > b->discNo) - (a->discNo < b->discNo);
    if (!c)
        c = (a->trackNo > b->trackNo) - (a->trackNo < b->trackNo);
    if (!c)
        c = CompareCI(a->name, b->name);
    if (!c)
        c = CompareKeyCI(a->albumArtist, b->albumArtist);
    return c;
}

/* Sort all tracks once, not once per search keystroke.  Shell sort is small
 * and fast enough for 400 entries on a 25 MHz 68040. */
static void RebuildOrder(void)
{
    int gap;
    int i;

    for (i = 0; i < gTrackCount; i++)
        gOrder[i] = i;

    for (gap = gTrackCount / 2; gap > 0; gap /= 2) {
        for (i = gap; i < gTrackCount; i++) {
            int tmp = gOrder[i];
            int j = i;
            while (j >= gap &&
                   CompareTracks(&gTracks[gOrder[j - gap]], &gTracks[tmp]) > 0) {
                gOrder[j] = gOrder[j - gap];
                j -= gap;
            }
            gOrder[j] = tmp;
        }
    }
}

/* Filtering now walks the already-sorted order, so typing in the search box
 * is O(n) instead of a full sort. */
static void RebuildView(void)
{
    char keepId[40];
    int i;

    keepId[0] = 0;
    if (gSelected >= 0 && gSelected < gViewCount)
        CopyField(keepId, sizeof(keepId), gTracks[gView[gSelected]].id);

    gViewCount = 0;
    for (i = 0; i < gTrackCount; i++) {
        int idx = gOrder[i];
        if (!gQuery[0] || TrackMatches(&gTracks[idx], gQuery))
            gView[gViewCount++] = idx;
    }

    gSelected = 0;
    if (keepId[0]) {
        for (i = 0; i < gViewCount; i++) {
            if (strcmp(gTracks[gView[i]].id, keepId) == 0) {
                gSelected = i;
                break;
            }
        }
    }
    if (gSelected < gTop)
        gTop = gSelected;
    if (gSelected >= gTop + gListRows)
        gTop = gSelected - gListRows + 1;
    if (gTop > gViewCount - gListRows)
        gTop = gViewCount - gListRows;
    if (gTop < 0)
        gTop = 0;
}

static int FetchTracks(void)
{
    HttpConn conn;
    BufferSink sink;
    char path[512];
    int err;
    int count = 0;

    SetStatus("Loading music library...");
    ShowStatusNow();
    LogLine("Fetch tracks start\r");
    snprintf(path, sizeof(path),
             "/Users/%s/Items?Recursive=true&IncludeItemTypes=Audio"
             "&SortBy=AlbumArtist,Album,ParentIndexNumber,IndexNumber"
             "&Limit=%d&Fields=RunTimeTicks,Album,Artists"
             "&EnableImages=false&EnableUserData=false",
             gUserId, MAX_TRACKS);

    memset(&sink, 0, sizeof(sink));
    sink.buf = gJson;
    sink.max = sizeof(gJson);
    err = HttpOpen(&conn, gHost, (short)gPort);
    LogNum("Library HttpOpen: ", err);
    if (err != 0) {
        SetStatus("MacTCP connect failed");
        return err;
    }
    err = HttpRequest(&conn, "GET", path, gHost, (short)gPort, gToken,
                      "application/json", NULL, 0);
    if (err == 0) {
        gJsonLen = 0;
        err = HttpReadResponse(&conn, BufferSinkWrite, &sink);
        LogNum("Library read: ", err);
        LogNum("Library status: ", (long)conn.status);
        LogNum("Library body bytes: ", sink.length);
        if (err == 0 && conn.status != 200) {
            char tmp[80];
            snprintf(tmp, sizeof(tmp), "Library HTTP %d", conn.status);
            SetStatus(tmp);
            err = -1;
        }
    }
    HttpClose(&conn);
    if (err != 0)
        return err;

    gJsonLen = sink.length;
    gTrackCount = 0;
    count = (int)JsonParseItems(gJson, gJsonLen, AddTrack, &count, MAX_TRACKS);
    gTrackCount = count;
    LogNum("Tracks parsed: ", (long)gTrackCount);
    gSelected = 0;
    gTop = 0;
    gLastClickRow = -1;
    RebuildOrder();
    RebuildView();
    {
        char tmp[80];
        snprintf(tmp, sizeof(tmp), "Loaded %d tracks from %s", gTrackCount,
                 gHost);
        SetStatus(tmp);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Playback                                                           */
/* ------------------------------------------------------------------ */

static void DrawFooter(void);

static void StopPlayback(void)
{
    if (gDownloadActive) {
        HttpClose(&gAudioConn);
        gDownloadActive = 0;
    }
    AudioStop();
    gPlayingIndex = -1;
    gLastFooterSecond = -1;
    gPlaybackSerial++;
}

static int PlayTrack(int index)
{
    char path[768];
    char msg[160];
    int err;

    if (index < 0 || index >= gTrackCount)
        return -1;
    if (gBusy)
        return -1;

    StopPlayback();

    /* Ask for raw PCM.  There is no MP3 decode on the Mac at all; even a
     * 68LC040 can copy 16 KB/s while the Sound Manager plays it. */
    snprintf(path, sizeof(path),
             "/Audio/%s/universal?UserId=%s&DeviceId=%s"
             "&MaxStreamingBitrate=128000&Container=wav"
             "&AudioCodec=pcm_s16be"
             "&MaxAudioChannels=1&MaxAudioSampleRate=8000"
             "&TranscodingContainer=wav&TranscodingProtocol=http"
             "&EnableRedirection=false",
             gTracks[index].id, gUserId, DEVICE_ID);

    SetStatus("Requesting 8 kHz PCM stream...");
    DrawStatus();
    LogLine("Play path: ");
    LogLine(path);
    LogLine("\r");
    snprintf(msg, sizeof(msg), "%s - %s", gTracks[index].artist,
             gTracks[index].name);
    CopyField(gNowPlaying, sizeof(gNowPlaying), msg);

    err = HttpOpen(&gAudioConn, gHost, (short)gPort);
    LogNum("Audio HttpOpen: ", err);
    if (err == 0) {
        err = HttpRequest(&gAudioConn, "GET", path, gHost, (short)gPort,
                          gToken, "audio/wav", NULL, 0);
        if (err == 0)
            err = HttpReadResponseBegin(&gAudioConn);
        LogNum("Audio header read: ", err);
        LogNum("Audio status: ", (long)gAudioConn.status);
        if (err == 0 && gAudioConn.status != 200) {
            snprintf(msg, sizeof(msg), "Audio HTTP %d", gAudioConn.status);
            SetStatus(msg);
            err = -1;
        }
    }
    if (err != 0) {
        HttpClose(&gAudioConn);
        if (strncmp(gStatus, "Audio HTTP", 10) != 0) {
            snprintf(msg, sizeof(msg), "Audio connect failed: %d", err);
            SetStatus(msg);
        }
        DrawStatus();
        return err;
    }

    err = AudioPcmBegin(8000, 1, 16);
    if (err != 0) {
        HttpClose(&gAudioConn);
        SetStatus("Sound Manager could not start audio");
        DrawStatus();
        return err;
    }

    gPlayingIndex = index;
    gDownloadActive = 1;
    gLastFooterSecond = -1;
    SetStatus("Buffering...");
    DrawStatus();
    DrawFooter();
    return 0;
}

/* Pull a small piece of the HTTP body on every event-loop pass.  TCP
 * backpressure keeps memory use flat, and the UI stays responsive. */
static void PumpAudio(void)
{
    int r;

    if (!gDownloadActive)
        return;

    r = HttpReadBodyStep(&gAudioConn, AudioSinkWrite, NULL);
    if (r == 0) {
        gDownloadActive = 0;
        HttpClose(&gAudioConn);
        AudioPcmEnd();
        if (gPlayingIndex >= 0) {
            if (AudioIsPlaying()) {
                char msg[160];
                snprintf(msg, sizeof(msg), "Playing: %s", gNowPlaying);
                SetStatus(msg);
            } else {
                SetStatus("Audio stream was empty");
                gPlayingIndex = -1;
                AudioStop();
            }
            DrawStatus();
            DrawFooter();
        }
    } else if (r < 0) {
        char msg[80];
        gDownloadActive = 0;
        HttpClose(&gAudioConn);
        AudioStop();
        gPlayingIndex = -1;
        snprintf(msg, sizeof(msg), "Audio stream failed: %d", r);
        SetStatus(msg);
        DrawStatus();
        DrawFooter();
    }
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

static Rect SearchBox(void)
{
    Rect r = gWin->portRect;
    Rect box;

    box.top = SEARCH_TOP;
    box.bottom = SEARCH_BOTTOM;
    box.left = 8;
    box.right = r.right - SEARCH_RIGHT_GAP;
    return box;
}

static Rect SortBox(void)
{
    Rect r = gWin->portRect;
    Rect box;

    box.top = SEARCH_TOP;
    box.bottom = SEARCH_BOTTOM;
    box.right = r.right - 8;
    box.left = box.right - SORT_WIDTH;
    return box;
}

static void DrawSearch(void)
{
    Rect box = SearchBox();
    Rect textClip;
    RGBColor black = { 0, 0, 0 };
    short textX = (short)(box.left + 6);
    short baseline = (short)(box.bottom - 6);

    SetPort(gWin);
    TextFont(kFontIDGeneva);
    TextSize(10);
    TextFace(0);
    RGBForeColor(&black);

    EraseRect(&box);
    FrameRect(&box);
    if (gSearchActive) {
        Rect focus = box;
        InsetRect(&focus, 1, 1);
        FrameRect(&focus);
    }

    textClip = box;
    InsetRect(&textClip, 2, 2);
    if (gQuery[0])
        DrawTextAt(gQuery, textX, baseline, &textClip);
    else if (!gSearchActive)
        DrawTextAt("Search (/)", textX, baseline, &textClip);

    if (gSearchActive && ((TickCount() / 20) & 1)) {
        short w = 0;
        short caret;
        if (gQuery[0]) {
            Str255 p;
            long n = (long)strlen(gQuery);
            if (n > 255)
                n = 255;
            p[0] = (unsigned char)n;
            memcpy(p + 1, gQuery, n);
            w = StringWidth(p);
        }
        caret = (short)(textX + w);
        if (caret > box.right - 4)
            caret = (short)(box.right - 4);
        MoveTo(caret, (short)(box.top + 4));
        LineTo(caret, (short)(box.bottom - 4));
    }
}

static void DrawSort(void)
{
    Rect box = SortBox();
    Rect textClip = box;
    char tmp[48];

    SetPort(gWin);
    TextFont(kFontIDGeneva);
    TextSize(10);
    TextFace(0);

    EraseRect(&box);
    FrameRect(&box);
    snprintf(tmp, sizeof(tmp), "Sort: %s", gSortNames[gSortMode]);
    InsetRect(&textClip, 2, 2);
    DrawTextAt(tmp, (short)(box.left + 6), (short)(box.bottom - 6), &textClip);
}

static void DrawColumns(void)
{
    SetPort(gWin);
    TextFont(kFontIDGeneva);
    TextSize(10);
    TextFace(0);
    MoveTo(COL_NUM_X, LIST_TOP - 4);
    DrawCString("#");
    MoveTo(COL_TITLE_X, LIST_TOP - 4);
    DrawCString("Title");
    MoveTo(gColArtistX, LIST_TOP - 4);
    DrawCString("Album Artist");
    MoveTo(gColAlbumX, LIST_TOP - 4);
    DrawCString("Album");
}

static void DrawScrollbar(void)
{
    Rect r = gWin->portRect;
    Rect area;
    Rect frame;
    Rect thumbRect;
    long total = gViewCount;
    long visible = gListRows;
    long tray;
    long thumb;
    long pos;

    area.top = LIST_TOP;
    area.bottom = gListBottom;
    area.left = r.right - 19;
    area.right = r.right - 9;

    SetPort(gWin);
    EraseRect(&area);
    if (total <= visible)
        return;

    frame = area;
    FrameRect(&frame);

    tray = (gListBottom - LIST_TOP) - 8;
    thumb = (visible * tray) / total;
    if (thumb < 12)
        thumb = 12;
    if (thumb > tray)
        thumb = tray;

    pos = 0;
    if (total > visible)
        pos = ((long)gTop * (tray - thumb)) / (total - visible);

    thumbRect.left = (short)(area.left + 1);
    thumbRect.right = (short)(area.right - 1);
    thumbRect.top = (short)(area.top + 4 + pos);
    thumbRect.bottom = (short)(thumbRect.top + thumb);
    PaintRect(&thumbRect);
}

/* Draw the visible rows column by column.  Each column is clipped once, so
 * long metadata never bleeds into the next field and no per-string width
 * measurement is needed. */
static void DrawList(void)
{
    Rect r = gWin->portRect;
    Rect content;
    Rect clip;
    int i;

    SetPort(gWin);
    TextFont(kFontIDGeneva);
    TextSize(10);
    TextFace(0);

    content.top = LIST_TOP;
    content.left = 8;
    content.right = r.right - 22;
    content.bottom = gListBottom;
    EraseRect(&content);

    clip = content;
    clip.right = COL_TITLE_X - 2;
    if (gSavedClip) {
        GetClip(gSavedClip);
        ClipRect(&clip);
    }
    for (i = 0; i < gListRows; i++) {
        int viewIdx = gTop + i;
        Str255 num;
        if (viewIdx >= gViewCount)
            break;
        NumToString((long)(viewIdx + 1), num);
        MoveTo((short)(COL_TITLE_X - 6 - StringWidth(num)),
               (short)(LIST_TOP + i * ROW_HEIGHT + 12));
        DrawString(num);
    }
    if (gSavedClip)
        SetClip(gSavedClip);

    clip.left = COL_TITLE_X;
    clip.right = gColArtistX - 2;
    if (gSavedClip) {
        GetClip(gSavedClip);
        ClipRect(&clip);
    }
    for (i = 0; i < gListRows; i++) {
        int viewIdx = gTop + i;
        const char *s;
        if (viewIdx >= gViewCount)
            break;
        s = gTracks[gView[viewIdx]].name;
        MoveTo(COL_TITLE_X, (short)(LIST_TOP + i * ROW_HEIGHT + 12));
        DrawText((Ptr)s, 0, (short)strlen(s));
    }
    if (gSavedClip)
        SetClip(gSavedClip);

    clip.left = gColArtistX;
    clip.right = gColAlbumX - 2;
    if (gSavedClip) {
        GetClip(gSavedClip);
        ClipRect(&clip);
    }
    for (i = 0; i < gListRows; i++) {
        int viewIdx = gTop + i;
        Track *t;
        const char *s;
        if (viewIdx >= gViewCount)
            break;
        t = &gTracks[gView[viewIdx]];
        s = t->albumArtist[0] ? t->albumArtist : t->artist;
        MoveTo(gColArtistX, (short)(LIST_TOP + i * ROW_HEIGHT + 12));
        DrawText((Ptr)s, 0, (short)strlen(s));
    }
    if (gSavedClip)
        SetClip(gSavedClip);

    clip.left = gColAlbumX;
    clip.right = content.right;
    if (gSavedClip) {
        GetClip(gSavedClip);
        ClipRect(&clip);
    }
    for (i = 0; i < gListRows; i++) {
        int viewIdx = gTop + i;
        const char *s;
        if (viewIdx >= gViewCount)
            break;
        s = gTracks[gView[viewIdx]].album;
        MoveTo(gColAlbumX, (short)(LIST_TOP + i * ROW_HEIGHT + 12));
        DrawText((Ptr)s, 0, (short)strlen(s));
    }
    if (gSavedClip)
        SetClip(gSavedClip);

    /* Inverting the finished row gives the classic black selection bar
     * without redrawing the text in a second color. */
    if (gSelected >= gTop && gSelected < gTop + gListRows) {
        Rect sel;
        sel.top = (short)(LIST_TOP + (gSelected - gTop) * ROW_HEIGHT);
        sel.bottom = (short)(sel.top + ROW_HEIGHT);
        sel.left = 8;
        sel.right = content.right;
        InvertRect(&sel);
    }

    DrawScrollbar();
}

static void DrawFooter(void)
{
    Rect r;
    Rect area;
    Rect clip;
    char tmp[180];
    Track *t;
    long elapsed;
    long duration;

    if (!gWin)
        return;

    SetPort(gWin);
    TextFont(kFontIDGeneva);
    TextSize(10);
    TextFace(0);

    r = gWin->portRect;
    area.left = 8;
    area.right = r.right - 8;
    area.top = r.bottom - 34;
    area.bottom = r.bottom - 15;
    EraseRect(&area);

    if (gPlayingIndex < 0 || !AudioIsPlaying())
        return;

    t = &gTracks[gPlayingIndex];
    elapsed = AudioPlaySeconds();
    if (elapsed < 0)
        elapsed = 0;
    duration = t->seconds;
    if (duration > 0 && elapsed > duration)
        elapsed = duration;

    if (duration > 0) {
        snprintf(tmp, sizeof(tmp), "Playing %ld:%02ld / %ld:%02ld  %s",
                 elapsed / 60, elapsed % 60, duration / 60, duration % 60,
                 gNowPlaying);
    } else {
        snprintf(tmp, sizeof(tmp), "Playing %ld:%02ld  %s", elapsed / 60,
                 elapsed % 60, gNowPlaying);
    }

    clip = area;
    clip.bottom = (short)(area.bottom - 6);
    DrawTextAt(tmp, 12, (short)(r.bottom - 26), &clip);

    {
        Rect pb;
        pb.left = 12;
        pb.right = r.right - 12;
        pb.top = r.bottom - 22;
        pb.bottom = r.bottom - 18;
        FrameRect(&pb);
        if (duration > 0) {
            Rect fill = pb;
            InsetRect(&fill, 1, 1);
            fill.right = fill.left +
                (short)((fill.right - fill.left) * elapsed / duration);
            if (fill.right > fill.left)
                PaintRect(&fill);
        }
    }
}

static void Draw(void)
{
    Rect r;
    Rect area;
    Rect hintClip;
    RGBColor black = { 0, 0, 0 };
    const char *hint =
        "Up/Down select  Return play  / search  Tab sort  S stop  R reload  Q quit";

    SetPort(gWin);
    r = gWin->portRect;
    EraseRect(&r);
    RGBForeColor(&black);

    TextFont(kFontIDGeneva);
    TextSize(10);
    TextFace(0);
    DrawStatus();
    DrawCount();
    DrawSearch();
    DrawSort();

    area.top = LIST_FRAME_TOP;
    area.bottom = r.bottom - 38;
    area.left = 6;
    area.right = r.right - 6;
    FrameRect(&area);
    DrawColumns();
    DrawList();

    hintClip = r;
    hintClip.top = r.bottom - 15;
    hintClip.left = 8;
    hintClip.right = r.right - 8;
    hintClip.bottom = r.bottom - 1;
    DrawTextAt(hint, 12, (short)(r.bottom - 5), &hintClip);

    DrawFooter();
}

/* ------------------------------------------------------------------ */
/* Events                                                             */
/* ------------------------------------------------------------------ */

static void SelectRow(int row)
{
    if (gViewCount <= 0)
        return;
    if (row < 0)
        row = 0;
    if (row >= gViewCount)
        row = gViewCount - 1;
    gSelected = row;
    if (gSelected < gTop)
        gTop = gSelected;
    if (gSelected >= gTop + gListRows)
        gTop = gSelected - gListRows + 1;
    if (gTop > gViewCount - gListRows)
        gTop = gViewCount - gListRows;
    if (gTop < 0)
        gTop = 0;
}

static void CycleSort(void)
{
    gSortMode++;
    if (gSortMode >= SORT_COUNT)
        gSortMode = 0;
    RebuildOrder();
    RebuildView();
}

static void RedrawFilter(void)
{
    DrawSearch();
    DrawList();
    DrawCount();
}

static void AppendQuery(int c)
{
    long n = (long)strlen(gQuery);
    if (n < (long)sizeof(gQuery) - 1) {
        gQuery[n] = (char)c;
        gQuery[n + 1] = 0;
        RebuildView();
    }
}

static void BackspaceQuery(void)
{
    long n = (long)strlen(gQuery);
    if (n > 0) {
        gQuery[n - 1] = 0;
        RebuildView();
    }
}

static void HandleKey(long message)
{
    int code = (int)(message & charCodeMask);

    if (gSearchActive) {
        switch (code) {
        case 0x1B: /* escape */
            gQuery[0] = 0;
            gSearchActive = 0;
            RebuildView();
            RedrawFilter();
            return;
        case 0x08:
        case 0x7F:
            BackspaceQuery();
            RedrawFilter();
            return;
        case 0x1E:
            SelectRow(gSelected - 1);
            DrawList();
            return;
        case 0x1F:
            SelectRow(gSelected + 1);
            DrawList();
            return;
        case 0x01:
            SelectRow(0);
            DrawList();
            return;
        case 0x04:
            SelectRow(gViewCount - 1);
            DrawList();
            return;
        case 0x0B:
            SelectRow(gSelected - gListRows);
            DrawList();
            return;
        case 0x0C:
            SelectRow(gSelected + gListRows);
            DrawList();
            return;
        case 0x0D:
        case 0x03:
            if (gViewCount > 0)
                PlayTrack(gView[gSelected]);
            return;
        case 0x09:
            CycleSort();
            DrawSort();
            DrawList();
            return;
        default:
            if (code >= 0x20 && code < 0x7F) {
                AppendQuery(code);
                RedrawFilter();
            }
            return;
        }
    }

    switch (code) {
    case '/':
        gSearchActive = 1;
        DrawSearch();
        break;
    case 0x1B:
        if (gQuery[0]) {
            gQuery[0] = 0;
            RebuildView();
        }
        gSearchActive = 0;
        RedrawFilter();
        break;
    case 0x1E: /* up */
        SelectRow(gSelected - 1);
        DrawList();
        break;
    case 0x1F: /* down */
        SelectRow(gSelected + 1);
        DrawList();
        break;
    case 0x01: /* home */
        SelectRow(0);
        DrawList();
        break;
    case 0x04: /* end */
        SelectRow(gViewCount - 1);
        DrawList();
        break;
    case 0x0B: /* page up */
        SelectRow(gSelected - gListRows);
        DrawList();
        break;
    case 0x0C: /* page down */
        SelectRow(gSelected + gListRows);
        DrawList();
        break;
    case 0x0D:
    case 0x03:
    case 0x20:
        if (gViewCount > 0)
            PlayTrack(gView[gSelected]);
        break;
    case 0x09: /* tab */
        CycleSort();
        DrawSort();
        DrawList();
        break;
    case 's':
    case 'S':
        StopPlayback();
        SetStatus("Stopped");
        DrawStatus();
        DrawFooter();
        break;
    case 'r':
    case 'R':
        if (!gBusy) {
            gBusy = 1;
            StopPlayback();
            Login();
            if (gToken[0])
                FetchTracks();
            gBusy = 0;
            Draw();
        }
        break;
    case 'q':
    case 'Q':
        StopPlayback();
        ExitToShell();
        break;
    default:
        break;
    }
}

static void HandleContentClick(Point local)
{
    Rect search = SearchBox();
    Rect sort = SortBox();

    if (PtInRect(local, &search)) {
        if (!gSearchActive) {
            gSearchActive = 1;
            DrawSearch();
        }
        return;
    }
    if (PtInRect(local, &sort)) {
        if (gSearchActive) {
            gSearchActive = 0;
            DrawSearch();
        }
        CycleSort();
        DrawSort();
        DrawList();
        return;
    }
    if (local.v >= LIST_TOP && local.v < gListBottom) {
        int row = gTop + (local.v - LIST_TOP) / ROW_HEIGHT;
        if (row >= 0 && row < gViewCount) {
            int same = (row == gSelected);
            long now = TickCount();
            if (gSearchActive) {
                gSearchActive = 0;
                DrawSearch();
            }
            SelectRow(row);
            if (!same)
                DrawList();
            if (same && gLastClickRow == row &&
                now - gLastClickTick <= 60) {
                PlayTrack(gView[row]);
                gLastClickRow = -1;
            } else {
                gLastClickRow = row;
                gLastClickTick = now;
            }
            return;
        }
    }
    if (gSearchActive) {
        gSearchActive = 0;
        DrawSearch();
    }
}

int main(void)
{
    EventRecord ev;
    Rect bounds;
    Rect sb;
    short availTop;
    short availBottom;
    short width;
    short height;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);

    OpenLog();
    LogLine("Jellyfin68K start\r");
    gHost[0] = 0;
    CopyField(gHost, sizeof(gHost), DEFAULT_HOST);
    gPort = DEFAULT_PORT;
    CopyField(gUser, sizeof(gUser), DEFAULT_USER);
    CopyField(gPass, sizeof(gPass), DEFAULT_PASS);
    ReadConfigFile();
    LogLine("Config host: ");
    LogLine(gHost);
    LogLine("\r");
    LogNum("Config port: ", (long)gPort);

    /* NewWindow() treats the bounds as the content rectangle and adds the
     * title bar above it.  Reserve the menu bar, the title bar, and a small
     * margin so the whole window fits on a 512x342 Macintosh Classic. */
    sb = qd.screenBits.bounds;
    availTop = (short)(sb.top + 22);
    availBottom = (short)(sb.bottom - 4);
    width = WINDOW_WIDTH;
    height = WINDOW_HEIGHT;      /* desired content height */
    if (width > sb.right - sb.left - 8)
        width = (short)(sb.right - sb.left - 8);
    if (height + 20 > availBottom - availTop)
        height = (short)(availBottom - availTop - 20);
    if (height < 160)
        height = 160;
    bounds.left = (short)(sb.left + (sb.right - sb.left - width) / 2);
    bounds.top = (short)(availTop +
                         (availBottom - availTop - (height + 20)) / 2 + 20);
    bounds.right = (short)(bounds.left + width);
    bounds.bottom = (short)(bounds.top + height);

    gSavedClip = NewRgn();
    gWin = NewWindow(NULL, &bounds, "\pJellyfin 68K",
                     true, documentProc, (WindowPtr)-1, false, 0);
    if (!gWin)
        return 1;
    SelectWindow(gWin);
    SetPort(gWin);
    ShowWindow(gWin);

    {
        Rect r = gWin->portRect;
        short span = (short)((r.right - 22) - COL_TITLE_X);
        gColArtistX = (short)(COL_TITLE_X + span * 40 / 100);
        gColAlbumX = (short)(COL_TITLE_X + span * 66 / 100);
        gListRows = (r.bottom - 40 - LIST_TOP) / ROW_HEIGHT;
        if (gListRows < 1)
            gListRows = 1;
        gListBottom = (short)(LIST_TOP + gListRows * ROW_HEIGHT);
    }

    SetStatus("Starting...");
    Draw();

    gBusy = 1;
    if (Login() == 0)
        FetchTracks();
    gBusy = 0;
    Draw();

    {
        int wasPlaying = 0;
        int wasSerial = gPlaybackSerial;
        int caretPhase = -1;

        for (;;) {
            int playing;
            long sleep;

            /* Starting or stopping a track from a key or click resets the
             * transition tracking, so a new track is never mistaken for the
             * natural end of the previous one. */
            if (gPlaybackSerial != wasSerial) {
                wasSerial = gPlaybackSerial;
                wasPlaying = 0;
            }

            PumpAudio();
            playing = AudioIsPlaying();

            if (playing && !wasPlaying) {
                if (gPlayingIndex >= 0) {
                    char tmp[140];
                    snprintf(tmp, sizeof(tmp), "Playing: %s", gNowPlaying);
                    SetStatus(tmp);
                    DrawStatus();
                }
                gLastFooterSecond = -1;
                DrawFooter();
            } else if (!playing && wasPlaying) {
                if (gPlayingIndex >= 0) {
                    char tmp[140];
                    snprintf(tmp, sizeof(tmp), "Finished: %s", gNowPlaying);
                    SetStatus(tmp);
                    gPlayingIndex = -1;
                    DrawStatus();
                }
                DrawFooter();
            }
            wasPlaying = playing;

            if (playing) {
                long sec = AudioPlaySeconds();
                if (sec != gLastFooterSecond) {
                    gLastFooterSecond = sec;
                    DrawFooter();
                }
            }

            if (!playing && gSearchActive) {
                int phase = (int)((TickCount() / 20) & 1);
                if (phase != caretPhase) {
                    caretPhase = phase;
                    DrawSearch();
                }
            } else {
                caretPhase = -1;
            }

            if (gDownloadActive || playing)
                sleep = 10;
            else if (gSearchActive)
                sleep = 10;
            else
                sleep = 60;

            WaitNextEvent(everyEvent, &ev, sleep, NULL);
            switch (ev.what) {
            case updateEvt:
                if ((WindowPtr)ev.message == gWin) {
                    BeginUpdate(gWin);
                    Draw();
                    EndUpdate(gWin);
                }
                break;
            case keyDown:
            case autoKey:
                HandleKey(ev.message);
                break;
            case mouseDown: {
                WindowPtr clicked;
                short part = FindWindow(ev.where, &clicked);
                if (part == inGoAway) {
                    if (TrackGoAway(gWin, ev.where)) {
                        StopPlayback();
                        return 0;
                    }
                } else if (part == inDrag) {
                    DragWindow(gWin, ev.where, &sb);
                } else if (part == inContent && clicked == gWin) {
                    Point local = ev.where;
                    SetPort(gWin);
                    GlobalToLocal(&local);
                    HandleContentClick(local);
                }
                break;
            }
            default:
                break;
            }
        }
    }
}
