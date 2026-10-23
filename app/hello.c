/*
 * Minimal classic Macintosh application built with Retro68.
 *
 * It opens a single window using the original Toolbox APIs, draws a little
 * text, and exits when you click in the window or press a key.
 */

#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Events.h>
#include <Menus.h>
#include <Dialogs.h>
#include <TextEdit.h>
#include <ToolUtils.h>

static void DrawHello(WindowPtr window)
{
    Rect content;
    const unsigned char *title = "\pHello, 68K Mac!";
    const unsigned char *subtitle = "\pRetro68 + System 7 + GCC";
    const unsigned char *hint = "\pClick in this window or press a key to quit.";

    SetPort(window);
    content = window->portRect;
    EraseRect(&content);
    FrameRect(&content);

    TextFont(kFontIDGeneva);
    TextFace(bold);
    TextSize(24);
    MoveTo((content.right - StringWidth(title)) / 2, 90);
    DrawString(title);

    TextFace(0);
    TextSize(12);
    MoveTo((content.right - StringWidth(subtitle)) / 2, 122);
    DrawString(subtitle);

    TextSize(10);
    MoveTo((content.right - StringWidth(hint)) / 2, content.bottom - 32);
    DrawString(hint);
}

int main(void)
{
    WindowPtr window;
    EventRecord event;
    Rect bounds = { 60, 80, 260, 560 };

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);

    window = NewWindow(NULL, &bounds, "\pHello 68K",
                       true, documentProc, (WindowPtr)-1, false, 0);
    if (window == NULL)
        return 1;

    SelectWindow(window);
    SetPort(window);
    ShowWindow(window);
    DrawHello(window);

    for (;;) {
        WaitNextEvent(everyEvent, &event, 30, NULL);

        switch (event.what) {
            case updateEvt:
                if ((WindowPtr)event.message == window) {
                    BeginUpdate(window);
                    DrawHello(window);
                    EndUpdate(window);
                }
                break;

            case mouseDown: {
                WindowPtr clicked;
                short part = FindWindow(event.where, &clicked);

                if (part == inContent && clicked == window) {
                    FlushEvents(everyEvent, -1);
                    return 0;
                }
                break;
            }

            case keyDown:
            case autoKey:
                FlushEvents(everyEvent, -1);
                return 0;

            default:
                break;
        }
    }
}
