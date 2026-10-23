#include "audio.h"

#include <string.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Sound.h>

/* Sound Manager channel init flags (Universal Interfaces no longer exports
 * the friendly names). */
#define JF_INIT_MONO 0x0080
#define JF_INIT_STEREO 0x00C0

/* The server sends raw 16-bit big-endian PCM at 8 kHz mono, which is the
 * native byte order and sample size of the Sound Manager.  A 500 ms double
 * buffer and a four-second ring ride out network jitter; the downloader
 * applies backpressure rather than growing the buffer. */
#define BUF_MS 500L
#define RING_SECONDS 4
#define PRIME_MS 1500L
#define END_GRACE_SECONDS 1L

typedef struct {
    unsigned char *ring;
    long ringBytes;
    long frameBytes;        /* bytes per sample frame (channels * sample size) */
    long frameShift;        /* log2(frameBytes) */
    long bufBytes;          /* PCM bytes in each double buffer */
    volatile long writeAbs; /* written by main loop */
    volatile long readAbs;  /* written by doubleback (interrupt) */
    long writeIdx;
    long readIdx;
    volatile int ended;
} PcmStream;

static PcmStream gStream;
static SndChannelPtr gChan;
static SndDoubleBufferHeader gHdr;
static SndDoubleBufferPtr gBuf[2];

static long gRate;
static int gChannels;
static long gPlayStart;
static int gPlaying;

/* Jellyfin normally streams bare PCM.  If a future server prepends a
 * canonical 44-byte WAV header, discard it instead of playing clicks. */
static unsigned char gHeader[64];
static int gHeaderLen;
static int gHeaderDone;

static void AudioCleanup(void)
{
    if (gChan) {
        SndDisposeChannel(gChan, true);
        gChan = NULL;
    }
    if (gBuf[0]) {
        DisposePtr((Ptr)gBuf[0]);
        gBuf[0] = NULL;
    }
    if (gBuf[1]) {
        DisposePtr((Ptr)gBuf[1]);
        gBuf[1] = NULL;
    }
    if (gStream.ring) {
        DisposePtr((Ptr)gStream.ring);
        gStream.ring = NULL;
    }
    memset(&gStream, 0, sizeof(gStream));
    gPlaying = 0;
}

/* Called by the Sound Manager at interrupt time.  It must only copy bytes;
 * no Memory Manager or Toolbox calls are allowed here. */
static pascal void DoubleBack(SndChannelPtr chan, SndDoubleBufferPtr buf)
{
    PcmStream *st = (PcmStream *)buf->dbUserInfo[0];
    unsigned char *dst = buf->dbSoundData;
    long need;
    long avail;
    long bytes;
    long i;
    long idx;
    int last;

    (void)chan;
    if (!st || !st->ring) {
        buf->dbNumFrames = 0;
        buf->dbFlags = dbLastBuffer;
        return;
    }

    need = st->bufBytes;
    avail = st->writeAbs - st->readAbs;
    if (avail < 0)
        avail = 0;
    if (avail > need)
        avail = need;
    bytes = avail;

    idx = st->readIdx;
    for (i = 0; i < bytes; i++) {
        dst[i] = st->ring[idx++];
        if (idx >= st->ringBytes)
            idx = 0;
    }
    st->readIdx = idx;
    for (i = bytes; i < need; i++)
        ((volatile unsigned char *)dst)[i] = 0;
    st->readAbs += bytes;

    last = st->ended && st->readAbs >= st->writeAbs;
    buf->dbNumFrames = (last ? bytes : need) >> st->frameShift;
    buf->dbFlags = dbBufferReady | (last ? dbLastBuffer : 0);
}

static void RingWrite(const unsigned char *src, long bytes)
{
    long idx = gStream.writeIdx;
    long first = gStream.ringBytes - idx;

    if (first > bytes)
        first = bytes;
    memcpy(gStream.ring + idx, src, first);
    if (bytes > first)
        memcpy(gStream.ring, src + first, bytes - first);
    idx += bytes;
    if (idx >= gStream.ringBytes)
        idx -= gStream.ringBytes;
    gStream.writeIdx = idx;
    gStream.writeAbs += bytes;
}

static long MsToBytes(long ms)
{
    return (gRate * ms / 1000L) * gStream.frameBytes;
}

static void AudioStart(void)
{
    OSErr err;

    if (gPlaying || !gChan || !gStream.ring)
        return;

    /* Fill both double buffers before starting.  Calling the doubleback
     * routine directly is legal when it is not running at interrupt time. */
    DoubleBack(NULL, gBuf[0]);
    if (!(gBuf[0]->dbFlags & dbLastBuffer))
        DoubleBack(NULL, gBuf[1]);

    err = SndPlayDoubleBuffer(gChan, &gHdr);
    if (err == noErr) {
        gPlaying = 1;
        gPlayStart = TickCount();
    }
}

int AudioPcmBegin(long rate, int channels, int bits)
{
    long bufFrames;
    long bufBytes;
    long ringBytes;
    OSErr err;

    AudioCleanup();
    if (rate <= 0 || channels < 1 || channels > 2 ||
        (bits != 8 && bits != 16))
        return -1;

    gRate = rate;
    gChannels = channels;
    gStream.frameBytes = channels * (bits / 8);
    gStream.frameShift = (gStream.frameBytes == 1) ? 0
                       : (gStream.frameBytes == 2) ? 1 : 2;

    bufFrames = rate * BUF_MS / 1000L;
    if (bufFrames < 256)
        bufFrames = 256;
    bufBytes = bufFrames * gStream.frameBytes;
    ringBytes = rate * RING_SECONDS * gStream.frameBytes;

    gStream.bufBytes = bufBytes;
    gStream.ringBytes = ringBytes;
    gStream.ring = (unsigned char *)NewPtr(ringBytes);
    if (!gStream.ring) {
        AudioCleanup();
        return (int)memFullErr;
    }
    gBuf[0] = (SndDoubleBufferPtr)NewPtr(sizeof(SndDoubleBuffer) +
                                         bufBytes - 1);
    gBuf[1] = (SndDoubleBufferPtr)NewPtr(sizeof(SndDoubleBuffer) +
                                         bufBytes - 1);
    if (!gBuf[0] || !gBuf[1]) {
        AudioCleanup();
        return (int)memFullErr;
    }

    gBuf[0]->dbNumFrames = 0;
    gBuf[0]->dbFlags = 0;
    gBuf[0]->dbUserInfo[0] = (long)&gStream;
    gBuf[1]->dbNumFrames = 0;
    gBuf[1]->dbFlags = 0;
    gBuf[1]->dbUserInfo[0] = (long)&gStream;

    gHdr.dbhNumChannels = gChannels;
    gHdr.dbhSampleSize = bits;
    gHdr.dbhCompressionID = 0;
    gHdr.dbhPacketSize = 0;
    gHdr.dbhSampleRate = (Fixed)((unsigned long)gRate << 16);
    gHdr.dbhBufferPtr[0] = gBuf[0];
    gHdr.dbhBufferPtr[1] = gBuf[1];
    gHdr.dbhDoubleBack = NewSndDoubleBackProc(DoubleBack);

    err = SndNewChannel(&gChan, sampledSynth,
                        gChannels == 2 ? JF_INIT_STEREO : JF_INIT_MONO, NULL);
    if (err != noErr) {
        AudioCleanup();
        return (int)err;
    }

    gHeaderLen = 0;
    gHeaderDone = 0;
    return 0;
}

static int WriteBytes(const unsigned char *data, long length)
{
    if (length <= 0)
        return 0;
    if (gStream.writeAbs - gStream.readAbs + length > gStream.ringBytes)
        return 1;

    RingWrite(data, length);
    if (!gPlaying && gStream.writeAbs >= MsToBytes(PRIME_MS))
        AudioStart();
    return 0;
}

int AudioPcmWrite(const unsigned char *data, long length)
{
    long take;
    int off;

    if (!gStream.ring)
        return -1;
    if (length <= 0)
        return 0;

    if (!gHeaderDone) {
        take = (long)sizeof(gHeader) - gHeaderLen;
        if (take > length)
            take = length;
        memcpy(gHeader + gHeaderLen, data, take);
        gHeaderLen += (int)take;
        data += take;
        length -= take;

        if (gHeaderLen < 12)
            return 0;

        if (memcmp(gHeader, "RIFF", 4) == 0 &&
            memcmp(gHeader + 8, "WAVE", 4) == 0) {
            if (gHeaderLen < 44)
                return 0;
            off = 44;
        } else {
            off = 0;
        }

        gHeaderDone = 1;
        if (off < gHeaderLen) {
            int rc = WriteBytes(gHeader + off, gHeaderLen - off);
            if (rc != 0)
                return rc;
        }
    }

    return WriteBytes(data, length);
}

void AudioPcmEnd(void)
{
    if (!gStream.ring)
        return;
    gStream.ended = 1;
    if (!gPlaying && gStream.writeAbs > 0)
        AudioStart();
}

long AudioPlaySeconds(void)
{
    if (!gPlaying)
        return -1;
    return (TickCount() - gPlayStart) / 60L;
}

int AudioIsPlaying(void)
{
    unsigned long elapsed;
    unsigned long seconds;

    if (!gPlaying)
        return 0;

    /* The Sound Manager copies ahead into its two buffers, so use the wall
     * clock to avoid cutting off the final buffer. */
    if (gStream.ended) {
        seconds = (unsigned long)gStream.writeAbs /
                  (unsigned long)(gRate * gStream.frameBytes);
        elapsed = (unsigned long)(TickCount() - gPlayStart) / 60UL;
        if (elapsed >= seconds + (unsigned long)END_GRACE_SECONDS) {
            AudioStop();
            return 0;
        }
    }
    return 1;
}

void AudioStop(void)
{
    AudioCleanup();
}
