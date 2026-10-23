/*
 * 68K minimp3 decode test.
 *
 * Reads Unix:test.mp3 (8 kHz mono 8 kbps, as produced by Jellyfin's
 * lowest-quality MP3 transcode), decodes it, writes Unix:test.aiff,
 * and logs timing to Unix:mp3log.txt.
 */

#include <string.h>
#include <stdio.h>
#include <Files.h>
#include <OSUtils.h>
#include <Sound.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

#define MAX_PCM (2 * 1024 * 1024)

static short gPcm[MAX_PCM / 2];
static long gPcmSamples;

static short gLogRef;

static void LogLine(const char *s)
{
    long len = (long)strlen(s);
    if (gLogRef)
        FSWrite(gLogRef, &len, s);
}

static void LogNum(const char *label, long n)
{
    char buf[96];
    sprintf(buf, "%s%ld\r", label, n);
    LogLine(buf);
}

static OSErr OpenLog(void)
{
    FSSpec spec;
    OSErr err = FSMakeFSSpec(0, 0, "\pUnix:mp3log.txt", &spec);
    if (err == noErr)
        FSpDelete(&spec);
    err = FSpCreate(&spec, 'ttxt', 'ttxt', smSystemScript);
    if (err != noErr)
        return err;
    return FSpOpenDF(&spec, fsWrPerm, &gLogRef);
}

static OSErr ReadFile(const unsigned char *path, Ptr *data, long *size)
{
    FSSpec spec;
    short ref;
    OSErr err;
    long len;

    err = FSMakeFSSpec(0, 0, path, &spec);
    if (err != noErr)
        return err;
    err = FSpOpenDF(&spec, fsRdPerm, &ref);
    if (err != noErr)
        return err;
    err = GetEOF(ref, &len);
    if (err != noErr) {
        FSClose(ref);
        return err;
    }
    *data = NewPtr(len);
    if (*data == NULL) {
        FSClose(ref);
        return memFullErr;
    }
    err = FSRead(ref, &len, *data);
    FSClose(ref);
    *size = len;
    return err;
}

static void PutBE32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static OSErr WriteAiff(const unsigned char *path, long samples, long rate)
{
    FSSpec spec;
    short ref;
    OSErr err;
    unsigned char hdr[54];
    long dataLen = samples * 2;
    long fileLen = 54 + dataLen;
    long len;

    err = FSMakeFSSpec(0, 0, path, &spec);
    if (err == noErr)
        FSpDelete(&spec);
    err = FSpCreate(&spec, 'AIFF', 'TVOD', smSystemScript);
    if (err != noErr)
        return err;
    err = FSpOpenDF(&spec, fsWrPerm, &ref);
    if (err != noErr)
        return err;

    memcpy(hdr, "FORM", 4);
    PutBE32(hdr + 4, (unsigned long)(fileLen - 8));
    memcpy(hdr + 8, "AIFF", 4);
    memcpy(hdr + 12, "COMM", 4);
    PutBE32(hdr + 16, 18);
    hdr[20] = 0;
    hdr[21] = 1;                 /* mono */
    PutBE32(hdr + 22, (unsigned long)samples);
    hdr[26] = 0;
    hdr[27] = 16;                /* 16-bit */
    /* 80-bit IEEE extended sample rate */
    {
        unsigned long v = (unsigned long)rate;
        int exp = 16383 + 15;
        unsigned long mant = 0;
        while (v && !(v & 0x80000000UL)) {
            v <<= 1;
            exp--;
        }
        mant = v;
        hdr[28] = (unsigned char)(exp >> 8);
        hdr[29] = (unsigned char)exp;
        PutBE32(hdr + 30, mant);
        PutBE32(hdr + 34, 0);
    }
    memcpy(hdr + 38, "SSND", 4);
    PutBE32(hdr + 42, (unsigned long)(dataLen + 8));
    PutBE32(hdr + 46, 0);
    PutBE32(hdr + 50, 0);

    len = 54;
    err = FSWrite(ref, &len, hdr);
    if (err == noErr) {
        long i;
        unsigned char *p = (unsigned char *)gPcm;
        len = dataLen;
        /* AIFF is big-endian. */
        for (i = 0; i < samples; i++) {
            unsigned short s = (unsigned short)gPcm[i];
            p[i * 2] = (unsigned char)(s >> 8);
            p[i * 2 + 1] = (unsigned char)s;
        }
        err = FSWrite(ref, &len, p);
    }
    FSClose(ref);
    return err;
}

int main(void)
{
    Ptr mp3 = NULL;
    long mp3Size = 0;
    OSErr err;
    mp3dec_t dec;
    mp3dec_frame_info_t info;
    short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    long offset = 0;
    long frames = 0;
    long bad = 0;
    long start, elapsed;
    long maxFrameTicks = 0;

    OpenLog();
    LogLine("mp3test start\r");

    err = ReadFile("\pUnix:test.mp3", &mp3, &mp3Size);
    LogNum("read mp3 err: ", err);
    LogNum("mp3 bytes: ", mp3Size);
    if (err != noErr || mp3Size <= 0) {
        LogLine("no input\r");
        goto done;
    }

    mp3dec_init(&dec);
    start = TickCount();
    while (offset < mp3Size) {
        long t0 = TickCount();
        int samples = mp3dec_decode_frame(&dec, (const uint8_t *)mp3 + offset,
                                          (int)(mp3Size - offset), pcm, &info);
        long dt = TickCount() - t0;
        if (dt > maxFrameTicks)
            maxFrameTicks = dt;
        if (info.frame_bytes <= 0) {
            LogNum("stuck at offset: ", offset);
            break;
        }
        offset += info.frame_bytes;
        if (samples > 0) {
            if (gPcmSamples + samples > MAX_PCM / 2) {
                LogLine("pcm buffer full\r");
                break;
            }
            memcpy(gPcm + gPcmSamples, pcm, samples * sizeof(short));
            gPcmSamples += samples;
            frames++;
        } else if (info.frame_bytes <= 4) {
            bad++;
        }
    }
    elapsed = TickCount() - start;
    LogNum("frames: ", frames);
    LogNum("bad frames: ", bad);
    LogNum("samples: ", gPcmSamples);
    LogNum("decode ticks: ", elapsed);
    LogNum("max frame ticks: ", maxFrameTicks);
    LogNum("sample rate: ", info.hz);
    LogNum("channels: ", info.channels);
    if (elapsed > 0)
        LogNum("samples per tick: ", gPcmSamples / elapsed);

    if (gPcmSamples > 0) {
        err = WriteAiff("\pUnix:test.aiff", gPcmSamples, info.hz);
        LogNum("write aiff err: ", err);
        if (err == noErr) {
            FSSpec spec;
            short ref = 0;
            SndChannelPtr chan = NULL;
            OSErr serr;
            if (FSMakeFSSpec(0, 0, "\pUnix:test.aiff", &spec) == noErr &&
                FSpOpenDF(&spec, fsRdPerm, &ref) == noErr) {
                serr = SndNewChannel(&chan, sampledSynth, 1, NULL);
                LogNum("SndNewChannel: ", serr);
                if (serr == noErr) {
                    serr = SndStartFilePlay(chan, ref, 0, 32768, NULL, NULL,
                                            NULL, true);
                    LogNum("SndStartFilePlay: ", serr);
                    Delay(90, NULL);   /* let it play ~1.5 seconds */
                    serr = SndStopFilePlay(chan, true);
                    LogNum("SndStopFilePlay: ", serr);
                    SndDisposeChannel(chan, true);
                }
                FSClose(ref);
            }
        }
    }

done:
    LogLine("mp3test done\r");
    if (gLogRef) {
        FSClose(gLogRef);
        gLogRef = 0;
    }
    if (mp3)
        DisposePtr(mp3);
    return 0;
}
