/*
 * Headless MacTCP probe used to bring up the emulator network.
 * Writes progress to Unix:netprobe.txt one line at a time.
 */

#include <string.h>
#include <stdio.h>
#include <Files.h>
#include <OSUtils.h>
#include <Devices.h>

#include "MacTCP.h"
#include "mactcp/TCPHi.h"

static short gLogRef = 0;
static Boolean gCancel = false;

static void LogOpen(void)
{
    FSSpec spec;
    OSErr err = FSMakeFSSpec(0, 0, "\pUnix:netprobe.txt", &spec);
    if (err == noErr)
        FSpDelete(&spec);
    if (FSpCreate(&spec, 'ttxt', 'ttxt', smSystemScript) != noErr)
        return;
    FSpOpenDF(&spec, fsWrPerm, &gLogRef);
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
    sprintf(buf, "%s%ld\r", label, n);
    LogLine(buf);
}

static void GiveTime(void)
{
    Delay(1, NULL);
}

int main(void)
{
    StreamPtr stream;
    OSErr err;
    char request[] =
        "GET /System/Info/Public HTTP/1.0\r\n"
        "Host: 10.0.2.2:8096\r\n"
        "User-Agent: Jellyfin68K-Probe\r\n"
        "Connection: close\r\n\r\n";
    static char buf[4096];
    unsigned short len;
    long total = 0;
    long start;

    LogOpen();
    LogLine("netprobe start\r");

    /* Give the Finder/MacTCP control panel time to settle. */
    start = TickCount();
    while (TickCount() - start < 300)
        ;

    err = MT_InitNetwork();
    LogNum("InitNetwork: ", err);
    if (err != noErr)
        goto done;

    err = MT_CreateStream(&stream, 8192, GiveTime, &gCancel);
    LogNum("CreateStream: ", err);
    if (err != noErr)
        goto done;

    err = MT_OpenConnection(stream, 0x0A000202L, 8096, 30, GiveTime, &gCancel);
    LogNum("OpenConnection: ", err);
    if (err != noErr) {
        MT_ReleaseStream(stream, GiveTime, &gCancel);
        goto done;
    }

    err = MT_SendData(stream, request, (unsigned short)strlen(request), true,
                      GiveTime, &gCancel);
    LogNum("SendData: ", err);

    for (;;) {
        len = sizeof(buf) - 1;
        err = MT_RecvData(stream, buf, &len, true, GiveTime, &gCancel);
        LogNum("Recv: ", err);
        if (err == connectionClosing)
            break;
        LogNum("Recv len: ", len);
        if (len == 0)
            break;
        buf[len] = 0;
        {
            unsigned short i;
            for (i = 0; i < len; i++) {
                unsigned char c = (unsigned char)buf[i];
                if (c < 32 && c != '\r' && c != '\n' && c != '\t')
                    buf[i] = '.';
            }
        }
        LogLine(buf);
        total += len;
        if (err != noErr)
            break;
    }
    LogNum("total bytes: ", total);

    MT_CloseConnection(stream, GiveTime, &gCancel);
    MT_ReleaseStream(stream, GiveTime, &gCancel);

done:
    LogLine("netprobe done\r");
    if (gLogRef) {
        FSClose(gLogRef);
        gLogRef = 0;
    }
    return 0;
}
