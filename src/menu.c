/* Right-click context menu for Winder
 *
 * WINGs has no public popup-menu API, so this is a borderless WMWindow at
 * WMPopUpMenuWindowLevel with a column of command buttons.
 */
#include "winder.h"

typedef enum {
    CTX_OPEN = 1,
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

static int widget_is_descendant(Display *dpy, Window ancestor, Window w)
{
    Window root, parent, *kids = NULL;
    unsigned nk = 0;

    while (w && w != None && w != root) {
        if (w == ancestor)
            return 1;
        if (!XQueryTree(dpy, w, &root, &parent, &kids, &nk))
            return 0;
        if (kids)
            XFree(kids);
        if (parent == ancestor)
            return 1;
        if (parent == root || parent == None)
            return 0;
        w = parent;
    }
    return 0;
}

static void ctx_clear_buttons(WinderApp *app)
{
    int i;

    for (i = 0; i < app->ctxButtonCount; i++) {
        if (app->ctxButtons[i]) {
            WMUnmapWidget(app->ctxButtons[i]);
            WMDestroyWidget(app->ctxButtons[i]);
            app->ctxButtons[i] = NULL;
        }
    }
    app->ctxButtonCount = 0;
}

void context_menu_hide(WinderApp *app)
{
    Display *dpy;

    if (!app || !app->ctxVisible)
        return;
    dpy = WMScreenDisplay(app->scr);
    XUngrabPointer(dpy, CurrentTime);
    XUngrabKeyboard(dpy, CurrentTime);
    WMUnmapWidget(app->ctxMenu);
    app->ctxVisible = 0;
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

    if (!app->ctxVisible)
        return;

    dpy = event->xany.display;
    menu_xid = WMViewXID(WMWidgetView(app->ctxMenu));

    switch (event->type) {
    case ButtonPress:
        if (!widget_is_descendant(dpy, menu_xid, event->xbutton.window))
            context_menu_hide(app);
        break;
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

static void ctx_add_button(WinderApp *app, const char *label, CtxActionId id, int y)
{
    WMButton *b;

    if (app->ctxButtonCount >= CTX_MENU_MAX_ITEMS)
        return;

    b = WMCreateCommandButton(app->ctxFrame);
    WMResizeWidget(b, CTX_MENU_W - 4, CTX_MENU_ITEM_H);
    WMMoveWidget(b, 2, y);
    WMSetButtonText(b, label);
    WMSetButtonTextAlignment(b, WALeft);
    WMSetButtonAction(b, ctx_button_action, app);
    WMHangData(b, (void *)(intptr_t)id);
    WMMapWidget(b);
    app->ctxButtons[app->ctxButtonCount++] = b;
}

static void ctx_add_separator(WinderApp *app, int y)
{
    WMFrame *line;

    if (app->ctxButtonCount >= CTX_MENU_MAX_ITEMS)
        return;
    line = WMCreateFrame(app->ctxFrame);
    WMSetFrameRelief(line, WRGroove);
    WMMoveWidget(line, 4, y + 2);
    WMResizeWidget(line, CTX_MENU_W - 8, 2);
    WMMapWidget(line);
    /* store as widget pointer for later destroy */
    app->ctxButtons[app->ctxButtonCount++] = (WMButton *)line;
}

void context_menu_init(WinderApp *app)
{
    app->ctxMenu = WMCreateWindowWithStyle(app->scr, "winder-ctx",
                                           WMBorderlessWindowMask);
    WMSetWindowLevel(app->ctxMenu, WMPopUpMenuWindowLevel);
    WMResizeWidget(app->ctxMenu, CTX_MENU_W, CTX_MENU_ITEM_H * 8);

    app->ctxFrame = WMCreateFrame(app->ctxMenu);
    WMSetFrameRelief(app->ctxFrame, WRRaised);
    WMMoveWidget(app->ctxFrame, 0, 0);
    WMResizeWidget(app->ctxFrame, CTX_MENU_W, CTX_MENU_ITEM_H * 8);

    app->ctxButtonCount = 0;
    app->ctxVisible = 0;
    app->ctxPath[0] = '\0';
    app->ctxIsBackground = 0;
    app->clipboardPath[0] = '\0';

    WMRealizeWidget(app->ctxMenu);
    WMMapSubwidgets(app->ctxMenu);
    WMUnmapWidget(app->ctxMenu);

    WMCreateEventHandler(WMWidgetView(app->ctxMenu),
                         ButtonPressMask | KeyPressMask,
                         ctx_event_handler, app);
}

void context_menu_show(WinderApp *app, int root_x, int root_y,
                       const char *path, int is_background)
{
    CtxItemDef items[CTX_MENU_MAX_ITEMS];
    int nitems = 0, i, y, h;
    int has_target;
    int is_dir = 0;
    Display *dpy;
    int scr_w, scr_h;

    if (!app || !app->ctxMenu)
        return;

    context_menu_hide(app);
    ctx_clear_buttons(app);

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
        items[nitems++] = (CtxItemDef){ CTX_SEP, NULL };
        items[nitems++] = (CtxItemDef){ CTX_RENAME, "Rename…" };
        items[nitems++] = (CtxItemDef){ CTX_DUPLICATE, "Duplicate" };
        items[nitems++] = (CtxItemDef){ CTX_DELETE, "Delete…" };
        items[nitems++] = (CtxItemDef){ CTX_SEP, NULL };
        items[nitems++] = (CtxItemDef){ CTX_COMPRESS, "Compress…" };
        items[nitems++] = (CtxItemDef){ CTX_COPY, "Copy" };
        items[nitems++] = (CtxItemDef){ CTX_COPY_PATH, "Copy Path" };
        if (app->clipboardPath[0])
            items[nitems++] = (CtxItemDef){ CTX_PASTE, "Paste" };
        if (is_dir) {
            items[nitems++] = (CtxItemDef){ CTX_SEP, NULL };
            items[nitems++] = (CtxItemDef){ CTX_TERMINAL, "Open Terminal Here" };
        }
    } else {
        items[nitems++] = (CtxItemDef){ CTX_NEW_FOLDER, "New Folder…" };
        items[nitems++] = (CtxItemDef){ CTX_RELOAD, "Reload" };
        if (app->clipboardPath[0])
            items[nitems++] = (CtxItemDef){ CTX_PASTE, "Paste" };
        items[nitems++] = (CtxItemDef){ CTX_SEP, NULL };
        items[nitems++] = (CtxItemDef){ CTX_TERMINAL, "Open Terminal Here" };
        items[nitems++] = (CtxItemDef){ CTX_COPY_PATH, "Copy Path" };
    }

    y = 2;
    for (i = 0; i < nitems; i++) {
        if (items[i].id == CTX_SEP) {
            ctx_add_separator(app, y);
            y += 6;
        } else {
            ctx_add_button(app, items[i].label, items[i].id, y);
            y += CTX_MENU_ITEM_H;
        }
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
    WMMapWidget(app->ctxMenu);
    WMMapSubwidgets(app->ctxMenu);
    WMMapSubwidgets(app->ctxFrame);
    WMSetFocusToWidget(app->ctxMenu);

    XGrabPointer(dpy, WMViewXID(WMWidgetView(app->ctxMenu)), True,
                 ButtonPressMask | ButtonReleaseMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(dpy, WMViewXID(WMWidgetView(app->ctxMenu)), True,
                  GrabModeAsync, GrabModeAsync, CurrentTime);

    app->ctxVisible = 1;
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
