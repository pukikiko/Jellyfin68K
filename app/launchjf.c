#include <string.h>
#include <stdio.h>
#include <Files.h>
#include <OSUtils.h>
#include <Processes.h>

static short gLog;

static void Log(const char *s)
{
    long n = (long)strlen(s);
    if (gLog)
        FSWrite(gLog, &n, (Ptr)s);
}

static void LogNum(const char *label, long n)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s%ld\r", label, n);
    Log(buf);
}

static OSErr LaunchPath(const char *path)
{
    FSSpec spec;
    LaunchParamBlockRec lpb;
    OSErr err;

    err = FSMakeFSSpec(0, 0, (ConstStr255Param)path, &spec);
    Log("LaunchPath ");
    Log(path + 1);
    Log(": ");
    LogNum("", err);
    if (err != noErr)
        return err;

    memset(&lpb, 0, sizeof(lpb));
    lpb.launchBlockID = extendedBlock;
    lpb.launchEPBLength = extendedBlockLen;
    lpb.launchFileFlags = 0;
    lpb.launchControlFlags = launchContinue;
    lpb.launchAppSpec = &spec;
    err = LaunchApplication(&lpb);
    LogNum("LaunchApplication: ", err);
    return err;
}

int main(void)
{
    FSSpec spec;
    OSErr err;

    err = FSMakeFSSpec(0, 0, "\pUnix:launchjf.txt", &spec);
    if (err == noErr)
        FSpDelete(&spec);
    if (FSpCreate(&spec, 'ttxt', 'ttxt', smSystemScript) != noErr)
        return 0;
    if (FSpOpenDF(&spec, fsWrPerm, &gLog) != noErr)
        return 0;

    Log("launchjf start\r");
    /* Fifteen seconds is enough time to close the MacTCP control panel.
     * Closing it installs the .IPP driver that the client uses. */
    Delay(1200, NULL);

    if (LaunchPath("\pJellyfin68K:Jellyfin68K") != noErr &&
        LaunchPath("\pJellyfinApp:Jellyfin68K") != noErr &&
        LaunchPath("\pNetProbe:Jellyfin68K") != noErr) {
        Log("No Jellyfin68K app found on a mounted volume\r");
    }

    Log("launchjf done\r");
    FSClose(gLog);
    return 0;
}
