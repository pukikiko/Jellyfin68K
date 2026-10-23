/*
 * MacTCP diagnostic application for the Jellyfin 68K client project.
 * Connects to the HTTP relay (10.0.2.2:8096) using MacTCP and fetches
 * /System/Info/Public.  The response is shown in a window and written
 * to Unix:netlog.txt for inspection from the host.
 */

#include <string.h>
#include <stdio.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Events.h>
#include <Menus.h>
#include <Dialogs.h>
#include <TextEdit.h>
#include <ToolUtils.h>
#include <Devices.h>
#include <Files.h>

#include "MacTCP.h"
#include "mactcp/TCPHi.h"

static WindowPtr gWindow;
static char gText[32768];
static char gLog[32768];
static char gBuf[4096];
static Boolean gCancel = false;

static void Log(const char *s)
{
    strncat(gLog, s, sizeof(gLog) - strlen(gLog) - 1);
}

static void LogNum(const char *label, long n)
{
    char buf[64];
    sprintf(buf, "%s%ld\r", label, n);
    Log(buf);
}

static void SaveLog(void)
{
    FSSpec spec;
    short refNum;
    long len;
    OSErr err;

    err = FSMakeFSSpec(0, 0, "\pUnix:netlog.txt", &spec);
    if (err == noErr)
        FSpDelete(&spec);
    err = FSpCreate(&spec, 'ttxt', 'ttxt', smSystemScript);
    if (err != noErr && err != dupFNErr)
        return;
    err = FSpOpenDF(&spec, fsWrPerm, &refNum);
    if (err != noErr)
        return;
    len = (long)strlen(gLog);
    FSWrite(refNum, &len, gLog);
    FSClose(refNum);
    FlushVol(NULL, 0);
}

static void GiveTime(void)
{
    EventRecord ev;
    WaitNextEvent(0, &ev, 0, NULL);
}

static void Append(char **p, const char *s)
{
    while (*s && *p < gText + sizeof(gText) - 1)
        *(*p)++ = *s++;
    **p = 0;
}

static void Draw(void)
{
    Rect r;
    char *p;
    char line[128];
    short y = 26;
    const char *s;
    RGBColor black = { 0, 0, 0 };
    RGBColor white = { 65535, 65535, 65535 };

    SetPort(gWindow);
    r = gWindow->portRect;
    RGBForeColor(&white);
    PaintRect(&r);
    RGBForeColor(&black);

    TextFont(kFontIDGeneva);
    TextSize(12);
    TextFace(0);

    p = gText;
    while (*p && y < r.bottom - 24) {
        short n = 0;
        while (*p && *p != '\r' && *p != '\n' && n < 100)
            line[n++] = *p++;
        line[n] = 0;
        while (*p == '\r' || *p == '\n')
            p++;
        MoveTo(10, y);
        DrawString((const unsigned char *)line);
        y += 14;
    }

    s = "NetTest: click to quit";
    MoveTo(10, r.bottom - 10);
    DrawString((const unsigned char *)s);
}

static OSErr Fetch(void)
{
    StreamPtr stream;
    OSErr err;
    char request[] =
        "GET /System/Info/Public HTTP/1.0\r\n"
        "Host: 10.0.2.2:8096\r\n"
        "User-Agent: Jellyfin68K-NetTest\r\n"
        "Connection: close\r\n\r\n";
    char *buf = gBuf;
    unsigned short len;
    long total = 0;
    char *p = gText;

    Log("MacTCP .MPP test\r");
    {
        static const unsigned char *pnames[] = {
            "\p.MPP", "\p.IPP", "\p.ENET", "\p.ENET0"
        };
        static const char *cnames[] = { ".MPP", ".IPP", ".ENET", ".ENET0" };
        short i;
        for (i = 0; i < 4; i++) {
            short ref = 0;
            OSErr oerr = OpenDriver((ConstStr255Param)pnames[i], &ref);
            char tmp[80];
            sprintf(tmp, "OpenDriver %s = %d (ref %d)\r", cnames[i], oerr, ref);
            Log(tmp);
            if (oerr == noErr)
                CloseDriver(ref);
        }
    }
    Append(&p, "Opening .MPP driver...\r");
    err = MT_InitNetwork();
    LogNum("Open .MPP result: ", err);
    if (err != noErr) {
        char tmp[64];
        sprintf(tmp, "Open .MPP failed: %d\r", err);
        Append(&p, tmp);
        return err;
    }
    Append(&p, "Driver open OK\r");
    Log("Driver open OK\r");

    err = MT_CreateStream(&stream, 8192, GiveTime, &gCancel);
    LogNum("CreateStream result: ", err);
    if (err != noErr) {
        Append(&p, "CreateStream failed\r");
        return err;
    }
    Append(&p, "Stream created\r");

    Append(&p, "Connecting to 10.0.2.2:8096...\r");
    Log("Connecting to 10.0.2.2:8096...\r");
    err = MT_OpenConnection(stream, 0x0A000202L, 8096, 30, GiveTime, &gCancel);
    LogNum("Connect result: ", err);
    if (err != noErr) {
        char tmp[64];
        sprintf(tmp, "Connect failed: %d\r", err);
        Append(&p, tmp);
        return err;
    }
    Append(&p, "Connected\rSending request...\r");
    Log("Connected, sending request\r");

    err = MT_SendData(stream, request, (unsigned short)strlen(request), true,
                      GiveTime, &gCancel);
    LogNum("Send result: ", err);
    if (err != noErr) {
        Append(&p, "Send failed\r");
        return err;
    }
    Append(&p, "Response:\r");

    for (;;) {
        len = sizeof(gBuf) - 1;
        err = MT_RecvData(stream, buf, &len, true, GiveTime, &gCancel);
        LogNum("Recv result: ", err);
        LogNum("Recv length: ", len);
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
        Append(&p, buf);
        total += len;
        if (p >= gText + sizeof(gText) - 2)
            break;
        if (err != noErr)
            break;
    }

    {
        char tmp[64];
        sprintf(tmp, "\rbytes received: %ld\r", total);
        Append(&p, tmp);
        Log(tmp);
    }

    MT_CloseConnection(stream, GiveTime, &gCancel);
    MT_ReleaseStream(stream, GiveTime, &gCancel);
    return err;
}

int main(void)
{
    EventRecord event;
    Rect bounds = { 30, 30, 600, 1000 };

    Delay(120, NULL);

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);

    gLog[0] = 0;
    gText[0] = 0;

    gWindow = NewWindow(NULL, &bounds, "\pJellyfin 68K - network test",
                        true, documentProc, (WindowPtr)-1, false, 0);
    if (gWindow == NULL)
        return 1;

    SelectWindow(gWindow);
    SetPort(gWindow);
    ShowWindow(gWindow);
    Draw();

    Fetch();
    Draw();
    Log("Done\r");
    SaveLog();

    for (;;) {
        WaitNextEvent(everyEvent, &event, 30, NULL);
        switch (event.what) {
        case updateEvt:
            if ((WindowPtr)event.message == gWindow) {
                BeginUpdate(gWindow);
                Draw();
                EndUpdate(gWindow);
            }
            break;
        case mouseDown:
        case keyDown:
        case autoKey:
            return 0;
        default:
            break;
        }
    }
}
