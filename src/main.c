/* Winder — native WINGs file manager
 *
 * A Finder-inspired browser for Window Maker / WINGs:
 * favorites sidebar, column & list views, Get Info preview,
 * back/forward history, and basic file operations.
 */
#include "winder.h"

int main(int argc, char **argv)
{
    WinderApp app;
    char *start = NULL;
    int i;

    memset(&app, 0, sizeof(app));

    WMInitializeApplication(WINDER_APP_NAME, &argc, argv);

    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            start = argv[i];
            break;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("%s %s\n", WINDER_APP_NAME, WINDER_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: winder [directory]\n"
                   "  Native WINGs file manager for Window Maker.\n"
                   "  --help, -h       show this help\n"
                   "  --version, -V    show version\n");
            return 0;
        }
    }

    app.dpy = XOpenDisplay("");
    if (!app.dpy) {
        fprintf(stderr, "winder: cannot open X display\n");
        return 1;
    }

    app.scr = WMCreateScreen(app.dpy, DefaultScreen(app.dpy));
    if (!app.scr) {
        fprintf(stderr, "winder: cannot create WINGs screen\n");
        return 1;
    }

    hist_init(&app.history);
    winder_build_ui(&app);

    if (start && fs_is_dir(start)) {
        char *norm = fs_normalize(start);
        winder_set_path(&app, norm, 1);
        wfree(norm);
    } else {
        char *home = fs_home_dir();
        winder_set_path(&app, home, 1);
        wfree(home);
    }

    WMMapWidget(app.win);
    WMScreenMainLoop(app.scr);

    return 0;
}
