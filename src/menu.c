/* Right-click context menu for Winder
 *
 * WINGs has no public popup-menu API, so this is a borderless WMWindow at
 * WMPopUpMenuWindowLevel with a fixed pool of command buttons (created once
 * and realized with the menu — never destroyed on each popup).
 */
#include "winder.h"

typedef enum {
    CTX_NONE = 0,
    CTX_OPEN,
    CTX_RENAME,
    CTX_DUPLICATE,
    CTX_DELETE,
    CTX_COMPRESS,
    CTX_COPY,
    CTX_COPY_PATH,
    CTX_PASTE,
    CTX_NEW_FOLDER,
    CTX_TERMINAL,
    CTX_RELOAD,
    CTX_SEP
} CtxActionId;

typedef struct {
    CtxActionId id;
    const char *label;
} CtxItemDef;

static void ctx_button_action(WMWidget *self, void *data);
static void ctx_event_handler(XEvent *event, void *data);
static void list_context_button(XEvent *event, void *data);

static Window ctx_menu_xid(WinderApp *app)
{
    if (!app || !app->ctxMenu)
        return None;
    return WMViewXID(WMWidgetView(app->ctxMenu));
}

void context_menu_hide(WinderApp *app)
{
    Display *dpy;
    Window xid;
    int i;

    if (!app || !app->ctxMenu)
        return;

    dpy = WMScreenDisplay(app->scr);
    xid = ctx_menu_xid(app);

    if (app->ctxVisible) {
        XUngrabPointer(dpy, CurrentTime);
        XUngrabKeyboard(dpy, CurrentTime);
    }

    /* Hide buttons first (they are realized; safe to unmap). */
    for (i = 0; i < CTX_MENU_MAX_ITEMS; i++) {
        if (app->ctxButtons[i] && WMWidgetIsMapped(app->ctxButtons[i]))
            WMUnmapWidget(app->ctxButtons[i]);
    }

    if (xid != None && WMWidgetIsMapped(app->ctxMenu))
        WMUnmapWidget(app->ctxMenu);

    app->ctxVisible = 0;
    app->ctxButtonCount = 0;
}

static void ctx_button_action(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    CtxActionId id = (CtxActionId)(intptr_t)WMGetHangedData(self);

    context_menu_hide(app);

    switch (id) {
    case CTX_OPEN:
        action_open(NULL, app);
        break;
    case CTX_RENAME:
        action_rename(NULL, app);
        break;
    case CTX_DUPLICATE:
        action_duplicate(NULL, app);
        break;
    case CTX_DELETE:
        action_delete(NULL, app);
        break;
    case CTX_COMPRESS:
        action_compress(NULL, app);
        break;
    case CTX_COPY:
        action_copy_item(NULL, app);
        break;
    case CTX_COPY_PATH:
        action_copy_path(NULL, app);
        break;
    case CTX_PASTE:
        action_paste_item(NULL, app);
        break;
    case CTX_NEW_FOLDER:
        action_new_folder(NULL, app);
        break;
    case CTX_TERMINAL:
        action_terminal(NULL, app);
        break;
    case CTX_RELOAD:
        action_refresh(NULL, app);
        break;
    default:
        break;
    }
}

static void ctx_event_handler(XEvent *event, void *data)
{
    WinderApp *app = (WinderApp *)data;
    Window menu_xid;
    Display *dpy;
    int mx, my;
    unsigned mw, mh, b, d;
    Window root;

    if (!app->ctxVisible || !app->ctxMenu)
        return;

    dpy = event->xany.display;
    menu_xid = ctx_menu_xid(app);
    if (menu_xid == None)
        return;

    switch (event->type) {
    case ButtonPress: {
        /*
         * With an active pointer grab (owner_events=True), outside clicks are
         * often reported to the grab window. Use root coordinates vs menu
         * geometry so "click outside" always dismisses.
         */
        if (!XGetGeometry(dpy, menu_xid, &root, &mx, &my, &mw, &mh, &b, &d)) {
            context_menu_hide(app);
            break;
        }
        {
            int rx = event->xbutton.x_root;
            int ry = event->xbutton.y_root;
            if (rx < mx || ry < my || rx >= mx + (int)mw || ry >= my + (int)mh)
                context_menu_hide(app);
        }
        break;
    }
    case KeyPress: {
        KeySym ks;
        XLookupString(&event->xkey, NULL, 0, &ks, NULL);
        if (ks == XK_Escape)
            context_menu_hide(app);
        break;
    }
    default:
        break;
    }
}

void context_menu_init(WinderApp *app)
{
    int i;

    app->ctxMenu = WMCreateWindowWithStyle(app->scr, "winder-ctx",
                                           WMBorderlessWindowMask);
    WMSetWindowLevel(app->ctxMenu, WMPopUpMenuWindowLevel);
    WMResizeWidget(app->ctxMenu, CTX_MENU_W, CTX_MENU_ITEM_H * CTX_MENU_MAX_ITEMS);

    app->ctxFrame = WMCreateFrame(app->ctxMenu);
    WMSetFrameRelief(app->ctxFrame, WRRaised);
    WMMoveWidget(app->ctxFrame, 0, 0);
    WMResizeWidget(app->ctxFrame, CTX_MENU_W,
                   CTX_MENU_ITEM_H * CTX_MENU_MAX_ITEMS);

    /* Fixed button pool — realized once with the menu window. */
    for (i = 0; i < CTX_MENU_MAX_ITEMS; i++) {
        WMButton *b = WMCreateCommandButton(app->ctxFrame);
        WMResizeWidget(b, CTX_MENU_W - 4, CTX_MENU_ITEM_H);
        WMMoveWidget(b, 2, 2 + i * CTX_MENU_ITEM_H);
        WMSetButtonText(b, "");
        WMSetButtonTextAlignment(b, WALeft);
        WMSetButtonAction(b, ctx_button_action, app);
        WMHangData(b, (void *)(intptr_t)CTX_NONE);
        app->ctxButtons[i] = b;
    }

    app->ctxButtonCount = 0;
    app->ctxVisible = 0;
    app->ctxPath[0] = '\0';
    app->ctxIsBackground = 0;
    app->clipboardPath[0] = '\0';

    WMRealizeWidget(app->ctxMenu);
    /*
     * After realize, children have real X windows. Leave everything unmapped
     * until show. Never Unmap before realize (that caused BadWindow 0x0).
     */
    if (WMWidgetIsMapped(app->ctxMenu))
        WMUnmapWidget(app->ctxMenu);

    WMCreateEventHandler(WMWidgetView(app->ctxMenu),
                         ButtonPressMask | KeyPressMask,
                         ctx_event_handler, app);
}

void context_menu_show(WinderApp *app, int root_x, int root_y,
                       const char *path, int is_background)
{
    CtxItemDef items[CTX_MENU_MAX_ITEMS];
    int nitems = 0, i, y, h, slot;
    int has_target;
    int is_dir = 0;
    Display *dpy;
    int scr_w, scr_h;
    Window xid;

    if (!app || !app->ctxMenu)
        return;

    /* Dismiss any previous popup first. */
    context_menu_hide(app);

    if (path && path[0])
        wstrlcpy(app->ctxPath, path, sizeof(app->ctxPath));
    else
        wstrlcpy(app->ctxPath, app->currentPath, sizeof(app->ctxPath));

    app->ctxIsBackground = is_background ? 1 : 0;
    has_target = !is_background && path && path[0] && strcmp(path, "/") != 0;
    if (has_target)
        is_dir = fs_is_dir(path);

    nitems = 0;
    if (has_target) {
        items[nitems++] = (CtxItemDef){ CTX_OPEN, "Open" };
        items[nitems++] = (CtxItemDef){ CTX_SEP, "-" };
        items[nitems++] = (CtxItemDef){ CTX_RENAME, "Rename..." };
        items[nitems++] = (CtxItemDef){ CTX_DUPLICATE, "Duplicate" };
        items[nitems++] = (CtxItemDef){ CTX_DELETE, "Delete..." };
        items[nitems++] = (CtxItemDef){ CTX_SEP, "-" };
        items[nitems++] = (CtxItemDef){ CTX_COMPRESS, "Compress..." };
        items[nitems++] = (CtxItemDef){ CTX_COPY, "Copy" };
        items[nitems++] = (CtxItemDef){ CTX_COPY_PATH, "Copy Path" };
        if (app->clipboardPath[0])
            items[nitems++] = (CtxItemDef){ CTX_PASTE, "Paste" };
        if (is_dir) {
            items[nitems++] = (CtxItemDef){ CTX_SEP, "-" };
            items[nitems++] = (CtxItemDef){ CTX_TERMINAL, "Open Terminal Here" };
        }
    } else {
        items[nitems++] = (CtxItemDef){ CTX_NEW_FOLDER, "New Folder..." };
        items[nitems++] = (CtxItemDef){ CTX_RELOAD, "Reload" };
        if (app->clipboardPath[0])
            items[nitems++] = (CtxItemDef){ CTX_PASTE, "Paste" };
        items[nitems++] = (CtxItemDef){ CTX_SEP, "-" };
        items[nitems++] = (CtxItemDef){ CTX_TERMINAL, "Open Terminal Here" };
        items[nitems++] = (CtxItemDef){ CTX_COPY_PATH, "Copy Path" };
    }

    if (nitems > CTX_MENU_MAX_ITEMS)
        nitems = CTX_MENU_MAX_ITEMS;

    y = 2;
    slot = 0;
    for (i = 0; i < nitems; i++) {
        WMButton *b = app->ctxButtons[slot];
        if (!b)
            break;

        if (items[i].id == CTX_SEP) {
            WMSetButtonText(b, "----------");
            WMSetButtonEnabled(b, False);
            WMHangData(b, (void *)(intptr_t)CTX_NONE);
        } else {
            WMSetButtonText(b, items[i].label);
            WMSetButtonEnabled(b, True);
            WMHangData(b, (void *)(intptr_t)items[i].id);
        }
        WMMoveWidget(b, 2, y);
        WMResizeWidget(b, CTX_MENU_W - 4, CTX_MENU_ITEM_H);
        WMMapWidget(b);
        y += CTX_MENU_ITEM_H;
        slot++;
    }
    app->ctxButtonCount = slot;

    /* Ensure unused pool buttons stay hidden */
    for (i = slot; i < CTX_MENU_MAX_ITEMS; i++) {
        if (app->ctxButtons[i] && WMWidgetIsMapped(app->ctxButtons[i]))
            WMUnmapWidget(app->ctxButtons[i]);
    }

    y += 2;
    h = y;
    if (h < CTX_MENU_ITEM_H * 2)
        h = CTX_MENU_ITEM_H * 2;

    WMResizeWidget(app->ctxMenu, CTX_MENU_W, h);
    WMResizeWidget(app->ctxFrame, CTX_MENU_W, h);

    dpy = WMScreenDisplay(app->scr);
    scr_w = DisplayWidth(dpy, DefaultScreen(dpy));
    scr_h = DisplayHeight(dpy, DefaultScreen(dpy));
    if (root_x + CTX_MENU_W > scr_w)
        root_x = scr_w - CTX_MENU_W - 4;
    if (root_y + h > scr_h)
        root_y = scr_h - h - 4;
    if (root_x < 0)
        root_x = 0;
    if (root_y < 0)
        root_y = 0;

    WMSetWindowInitialPosition(app->ctxMenu, root_x, root_y);
    WMSetWindowUserPosition(app->ctxMenu, root_x, root_y);

    /* Top-level first, then frame, then buttons (WINGs/X parent order). */
    WMMapWidget(app->ctxMenu);
    WMMapWidget(app->ctxFrame);
    for (i = 0; i < app->ctxButtonCount; i++)
        WMMapWidget(app->ctxButtons[i]);

    xid = ctx_menu_xid(app);
    if (xid == None) {
        context_menu_hide(app);
        return;
    }

    XRaiseWindow(dpy, xid);
    WMSetFocusToWidget(app->ctxMenu);

    /*
     * owner_events=False so outside clicks are delivered to the grab window
     * and our handler can dismiss via root coordinates.
     */
    XGrabPointer(dpy, xid, False,
                 ButtonPressMask | ButtonReleaseMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(dpy, xid, False,
                  GrabModeAsync, GrabModeAsync, CurrentTime);

    app->ctxVisible = 1;
    XFlush(dpy);
}

static int list_row_at_y(WMList *list, int y)
{
    int top, item_h, row, n;

    if (!list)
        return -1;
    item_h = WMGetListItemHeight(list);
    if (item_h < 1)
        item_h = 1;
    top = WMGetListPosition(list);
    row = (y - 2) / item_h + top;
    n = WMGetListNumberOfRows(list);
    if (row < 0 || row >= n)
        return -1;
    return row;
}

static void list_context_button(XEvent *event, void *data)
{
    WinderApp *app = (WinderApp *)data;
    WMList *list = NULL;
    WMList *candidates[12];
    int nc = 0, i, c, cols, row, background = 0;
    char *path = NULL;
    int root_x, root_y;

    if (event->type != ButtonPress || event->xbutton.button != Button3)
        return;

    candidates[nc++] = app->listView;
    candidates[nc++] = app->sidebar;
    if (app->browser) {
        cols = WMGetBrowserNumberOfColumns(app->browser);
        for (c = 0; c < cols && nc < 12; c++) {
            WMList *cl = WMGetBrowserListInColumn(app->browser, c);
            if (cl)
                candidates[nc++] = cl;
        }
    }
    for (i = 0; i < nc; i++) {
        if (candidates[i] &&
            WMViewXID(WMWidgetView(candidates[i])) == event->xbutton.window) {
            list = candidates[i];
            break;
        }
    }
    if (!list)
        return;
    if (event->xbutton.x <= LIST_SCROLLER_W)
        return;

    row = list_row_at_y(list, event->xbutton.y);
    root_x = event->xbutton.x_root;
    root_y = event->xbutton.y_root;

    if (list == app->sidebar) {
        if (row >= 0) {
            WMListItem *item = WMGetListItem(list, row);
            int idx;
            if (!item || item->disabled)
                return;
            WMSelectListItem(list, row);
            idx = (int)(intptr_t)item->clientData;
            if (idx >= 0 && idx < app->favoriteCount && app->favorites[idx].path) {
                winder_set_path(app, app->favorites[idx].path, 1);
                path = wstrdup(app->favorites[idx].path);
                background = 1;
            }
        }
        if (!path) {
            path = wstrdup(app->currentPath);
            background = 1;
        }
    } else if (list == app->listView) {
        if (row >= 0) {
            WMListItem *item = WMGetListItem(list, row);
            FileEntry *fe = item ? (FileEntry *)item->clientData : NULL;
            WMSelectListItem(list, row);
            list_click((WMWidget *)list, app);
            if (fe && fe->name)
                path = fs_join_path(app->currentPath, fe->name);
        }
        if (!path) {
            path = wstrdup(app->currentPath);
            background = 1;
        }
    } else {
        /* browser column */
        if (row >= 0) {
            char *bp;
            char browser_path[PATH_MAX + 4];

            WMSelectListItem(list, row);
            bp = WMGetBrowserPath(app->browser);
            if (bp) {
                size_t n = strlen(bp);
                while (n > 1 && bp[n - 1] == '/')
                    bp[--n] = '\0';
                if (fs_is_dir(bp)) {
                    if (strcmp(bp, "/") == 0)
                        wstrlcpy(browser_path, "/", sizeof(browser_path));
                    else
                        snprintf(browser_path, sizeof(browser_path), "%s/", bp);
                    WMSetBrowserPath(app->browser, browser_path);
                }
                path = wstrdup(bp);
                wfree(bp);
            }
            browser_click((WMWidget *)app->browser, app);
        }
        if (!path) {
            path = wstrdup(app->currentPath);
            background = 1;
        }
    }

    context_menu_show(app, root_x, root_y, path, background);
    if (path)
        wfree(path);
}

void context_menu_attach_list(WinderApp *app, WMList *list)
{
    if (!list)
        return;
    WMCreateEventHandler(WMWidgetView(list), ButtonPressMask,
                         list_context_button, app);
}
