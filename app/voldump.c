#include <string.h>
#include <stdio.h>
#include <Files.h>

static short gLog;

static void Log(const char *s)
{
    long n = (long)strlen(s);
    if (gLog)
        FSWrite(gLog, &n, (Ptr)s);
}

int main(void)
{
    FSSpec spec;
    short ref;
    short v;
    char line[128];

    if (FSMakeFSSpec(0, 0, "\pUnix:voldump.txt", &spec) == noErr)
        FSpDelete(&spec);
    if (FSpCreate(&spec, 'ttxt', 'ttxt', smSystemScript) != noErr)
        return 0;
    if (FSpOpenDF(&spec, fsWrPerm, &gLog) != noErr)
        return 0;

    Log("volumes\r");
    for (v = 1; v <= 16; v++) {
        Str255 name;
        ParamBlockRec pb;
        OSErr err;
        memset(&pb, 0, sizeof(pb));
        pb.volumeParam.ioVolIndex = v;
        pb.volumeParam.ioNamePtr = name;
        err = PBGetVInfoSync(&pb);
        if (err == noErr) {
            snprintf(line, sizeof(line), "idx %d ref %d name %.*s free %ld\r",
                     v, (int)pb.volumeParam.ioVRefNum, (int)name[0],
                     (char *)name + 1, (long)pb.volumeParam.ioVFrBlk * 512L);
            Log(line);
        }
    }
    Log("done\r");
    FSClose(gLog);
    return 0;
}
