/* User actions: open, delete, rename, navigate, etc. */
#include "winder.h"

static void sync_path_only(WinderApp *app)
{
    char title[PATH_MAX + 32];
    char *base;

    WMSetTextFieldText(app->pathField, app->currentPath);
    base = fs_basename(app->currentPath);
    snprintf(title, sizeof(title), "%s — %s", base, WINDER_APP_NAME);
    WMSetWindowTitle(app->win, title);
    WMSetWindowMiniwindowTitle(app->win, base);
    wfree(base);
}

void close_app(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    (void)self;
    hist_free(&app->history);
    exit(0);
}

void action_back(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    const char *path;
    (void)self;

    path = hist_back(&app->history);
    if (!path)
        return;
    app->suppressHistory = True;
    winder_set_path(app, path, 0);
    app->suppressHistory = False;
    winder_update_nav_buttons(app);
}

void action_forward(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    const char *path;
    (void)self;

    path = hist_forward(&app->history);
    if (!path)
        return;
    app->suppressHistory = True;
    winder_set_path(app, path, 0);
    app->suppressHistory = False;
    winder_update_nav_buttons(app);
}

void action_up(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *parent;
    (void)self;

    if (strcmp(app->currentPath, "/") == 0)
        return;
    parent = fs_dirname(app->currentPath);
    winder_set_path(app, parent, 1);
    wfree(parent);
}

void action_home(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *home;
    (void)self;

    home = fs_home_dir();
    winder_set_path(app, home, 1);
    wfree(home);
}

void action_go(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *text;
    (void)self;

    text = WMGetTextFieldText(app->pathField);
    if (text && text[0])
        winder_set_path(app, text, 1);
    if (text)
        wfree(text);
}

void action_refresh(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    (void)self;
    winder_refresh(app);
}

void action_view_columns(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    (void)self;
    winder_set_view_mode(app, VIEW_COLUMNS);
}

void action_view_list(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    (void)self;
    winder_set_view_mode(app, VIEW_LIST);
}

void action_toggle_hidden(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    (void)self;
    app->showHidden = !app->showHidden;
    WMSetButtonSelected(app->btnHidden, app->showHidden ? True : False);
    winder_refresh(app);
}

static void set_sort_column(WinderApp *app, SortColumn col)
{
    if (app->sortColumn == col)
        app->sortAscending = !app->sortAscending;
    else {
        app->sortColumn = col;
        /* Date/size feel more natural newest/largest first on first click */
        if (col == SORT_DATE || col == SORT_SIZE)
            app->sortAscending = False;
        else
            app->sortAscending = True;
    }
    if (app->viewMode == VIEW_LIST)
        populate_list_view(app);
    else
        list_update_sort_headers(app);
}

void action_sort_name(WMWidget *self, void *data)
{
    (void)self;
    set_sort_column((WinderApp *)data, SORT_NAME);
}

void action_sort_size(WMWidget *self, void *data)
{
    (void)self;
    set_sort_column((WinderApp *)data, SORT_SIZE);
}

void action_sort_kind(WMWidget *self, void *data)
{
    (void)self;
    set_sort_column((WinderApp *)data, SORT_KIND);
}

void action_sort_date(WMWidget *self, void *data)
{
    (void)self;
    set_sort_column((WinderApp *)data, SORT_DATE);
}

void action_new_folder(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *name, *dir, *full;
    (void)self;

    dir = winder_selected_directory(app);
    name = WMRunInputPanel(app->scr, app->win,
                           "New Folder",
                           "Name for the new folder:",
                           "untitled folder",
                           "Create", "Cancel");
    if (!name) {
        wfree(dir);
        return;
    }
    if (!name[0]) {
        wfree(name);
        wfree(dir);
        return;
    }
    full = fs_join_path(dir, name);
    if (fs_mkdir(full) != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Could not create folder:\n%s\n(%s)",
                 full, strerror(errno));
        WMRunAlertPanel(app->scr, app->win, "Error", msg, "OK", NULL, NULL);
    }
    wfree(full);
    wfree(name);
    wfree(dir);
    winder_refresh(app);
}

void action_delete(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *sel, *base;
    char msg[512];
    int r;
    (void)self;

    sel = winder_selected_path(app);
    if (!sel || strcmp(sel, app->currentPath) == 0 ||
        strcmp(sel, "/") == 0) {
        WMRunAlertPanel(app->scr, app->win, "Delete",
                        "Select a file or folder to delete.",
                        "OK", NULL, NULL);
        if (sel) wfree(sel);
        return;
    }

    base = fs_basename(sel);
    if (fs_is_dir(sel))
        snprintf(msg, sizeof(msg),
                 "Remove this empty folder?\n\n%s\n\n"
                 "(Only empty folders can be removed.)", base);
    else
        snprintf(msg, sizeof(msg), "Permanently delete this file?\n\n%s", base);

    r = WMRunAlertPanel(app->scr, app->win, "Delete", msg,
                        "Delete", "Cancel", NULL);
    wfree(base);

    if (r == WAPRDefault) {
        if (fs_remove_path(sel) != 0) {
            snprintf(msg, sizeof(msg), "Could not delete:\n%s\n(%s)",
                     sel, strerror(errno));
            WMRunAlertPanel(app->scr, app->win, "Error", msg, "OK", NULL, NULL);
        } else {
            winder_refresh(app);
        }
    }
    wfree(sel);
}

void action_rename(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *sel, *base, *parent, *newname, *dest;
    (void)self;

    sel = winder_selected_path(app);
    if (!sel || strcmp(sel, app->currentPath) == 0 ||
        strcmp(sel, "/") == 0) {
        WMRunAlertPanel(app->scr, app->win, "Rename",
                        "Select a file or folder to rename.",
                        "OK", NULL, NULL);
        if (sel) wfree(sel);
        return;
    }

    base = fs_basename(sel);
    newname = WMRunInputPanel(app->scr, app->win,
                              "Rename",
                              "New name:",
                              base,
                              "Rename", "Cancel");
    wfree(base);
    if (!newname || !newname[0]) {
        if (newname) wfree(newname);
        wfree(sel);
        return;
    }

    parent = fs_dirname(sel);
    dest = fs_join_path(parent, newname);
    if (fs_rename_path(sel, dest) != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Could not rename:\n%s\n(%s)",
                 sel, strerror(errno));
        WMRunAlertPanel(app->scr, app->win, "Error", msg, "OK", NULL, NULL);
    } else {
        winder_refresh(app);
    }
    wfree(parent);
    wfree(dest);
    wfree(newname);
    wfree(sel);
}

void action_open(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *sel;
    (void)self;

    sel = winder_selected_path(app);
    if (!sel) {
        WMRunAlertPanel(app->scr, app->win, "Open",
                        "Nothing is selected.", "OK", NULL, NULL);
        return;
    }

    if (fs_is_dir(sel)) {
        winder_set_path(app, sel, 1);
    } else {
        if (fs_open_with_default(sel) != 0) {
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "Could not open:\n%s\n\n"
                     "Install xdg-open or set a default handler.",
                     sel);
            WMRunAlertPanel(app->scr, app->win, "Open", msg, "OK", NULL, NULL);
        }
    }
    wfree(sel);
}

void action_open_new_window(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *sel;
    (void)self;

    sel = winder_selected_path(app);
    if (!sel || !fs_is_dir(sel)) {
        WMRunAlertPanel(app->scr, app->win, "Open in New Window",
                        "Select a folder first.", "OK", NULL, NULL);
        if (sel)
            wfree(sel);
        return;
    }
    if (fs_open_in_new_window(sel) != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "Could not open a new window for:\n%s", sel);
        WMRunAlertPanel(app->scr, app->win, "Open in New Window",
                        msg, "OK", NULL, NULL);
    }
    wfree(sel);
}

void action_duplicate(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *sel;
    char out[PATH_MAX];
    char msg[512];
    (void)self;

    sel = winder_selected_path(app);
    if (!sel || strcmp(sel, "/") == 0 || strcmp(sel, app->currentPath) == 0) {
        WMRunAlertPanel(app->scr, app->win, "Duplicate",
                        "Select a file or folder to duplicate.",
                        "OK", NULL, NULL);
        if (sel) wfree(sel);
        return;
    }
    if (fs_duplicate(sel, out, sizeof(out)) != 0) {
        snprintf(msg, sizeof(msg), "Could not duplicate:\n%s\n(%s)",
                 sel, strerror(errno));
        WMRunAlertPanel(app->scr, app->win, "Error", msg, "OK", NULL, NULL);
    } else {
        winder_refresh(app);
    }
    wfree(sel);
}

void action_compress(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *sel;
    char out[PATH_MAX];
    char msg[512];
    (void)self;

    sel = winder_selected_path(app);
    if (!sel || strcmp(sel, "/") == 0 || strcmp(sel, app->currentPath) == 0) {
        WMRunAlertPanel(app->scr, app->win, "Compress",
                        "Select a file or folder to compress.",
                        "OK", NULL, NULL);
        if (sel) wfree(sel);
        return;
    }
    if (fs_compress_tar_gz(sel, out, sizeof(out)) != 0) {
        snprintf(msg, sizeof(msg),
                 "Could not create archive for:\n%s\n\n"
                 "(Needs tar. Check write permission.)",
                 sel);
        WMRunAlertPanel(app->scr, app->win, "Error", msg, "OK", NULL, NULL);
    } else {
        char *base = fs_basename(out);
        snprintf(msg, sizeof(msg), "Created archive:\n%s", base);
        wfree(base);
        WMRunAlertPanel(app->scr, app->win, "Compress", msg, "OK", NULL, NULL);
        winder_refresh(app);
    }
    wfree(sel);
}

void action_copy_path(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *sel;
    (void)self;

    sel = winder_selected_path(app);
    if (!sel) {
        WMRunAlertPanel(app->scr, app->win, "Copy Path",
                        "Nothing is selected.", "OK", NULL, NULL);
        return;
    }
    wstrlcpy(app->clipboardPath, sel, sizeof(app->clipboardPath));
    fs_copy_path_to_clipboard(sel);
    wfree(sel);
}

void action_copy_item(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *sel;
    (void)self;

    sel = winder_selected_path(app);
    if (!sel || strcmp(sel, "/") == 0) {
        WMRunAlertPanel(app->scr, app->win, "Copy",
                        "Select a file or folder to copy.",
                        "OK", NULL, NULL);
        if (sel) wfree(sel);
        return;
    }
    /* For Paste we need the item path, not only as text */
    wstrlcpy(app->clipboardPath, sel, sizeof(app->clipboardPath));
    fs_copy_path_to_clipboard(sel);
    wfree(sel);
}

void action_paste_item(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *dir;
    char out[PATH_MAX];
    char msg[512];
    (void)self;

    if (!app->clipboardPath[0] || !fs_exists(app->clipboardPath)) {
        WMRunAlertPanel(app->scr, app->win, "Paste",
                        "Clipboard is empty or the source is gone.",
                        "OK", NULL, NULL);
        return;
    }
    dir = winder_selected_directory(app);
    if (fs_copy_into_dir(app->clipboardPath, dir, out, sizeof(out)) != 0) {
        snprintf(msg, sizeof(msg), "Could not paste into:\n%s", dir);
        WMRunAlertPanel(app->scr, app->win, "Error", msg, "OK", NULL, NULL);
    } else {
        winder_refresh(app);
    }
    wfree(dir);
}

void action_terminal(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *dir;
    (void)self;

    dir = winder_selected_directory(app);
    if (fs_open_terminal(dir) != 0) {
        WMRunAlertPanel(app->scr, app->win, "Terminal",
                        "Could not open a terminal.\n"
                        "Set $TERMINAL or install xterm.",
                        "OK", NULL, NULL);
    }
    wfree(dir);
}

/* ---- widget callbacks ---- */
void sidebar_select(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    WMListItem *item;
    int idx;
    (void)self;

    item = WMGetListSelectedItem(app->sidebar);
    if (!item || item->disabled)
        return;
    idx = (int)(intptr_t)item->clientData;
    if (idx < 0 || idx >= app->favoriteCount)
        return;
    if (app->favorites[idx].is_separator || !app->favorites[idx].path)
        return;
    winder_set_path(app, app->favorites[idx].path, 1);
}

void browser_click(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *path;
    (void)self;

    path = winder_selected_path(app);
    if (path) {
        if (fs_is_dir(path)) {
            if (strcmp(app->currentPath, path) != 0) {
                wstrlcpy(app->currentPath, path, sizeof(app->currentPath));
                sync_path_only(app);
                if (!app->suppressHistory)
                    hist_push(&app->history, app->currentPath);
                winder_update_nav_buttons(app);
            }
        }
        winder_update_preview(app, path);
        winder_update_status(app);
        wfree(path);
    }

    /* Rebind keys/right-click on any columns created by this selection. */
    winder_bind_browser_lists(app);
}

void browser_dclick(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    (void)self;
    action_open(NULL, app);
}

void list_click(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    char *path;
    (void)self;

    path = winder_selected_path(app);
    if (path) {
        winder_update_preview(app, path);
        winder_update_status(app);
        wfree(path);
    }
}

void list_dclick(WMWidget *self, void *data)
{
    WinderApp *app = (WinderApp *)data;
    (void)self;
    action_open(NULL, app);
}

void path_field_action(void *observer, WMNotification *notif)
{
    WinderApp *app = (WinderApp *)observer;
    uintptr_t reason;

    reason = (uintptr_t)WMGetNotificationClientData(notif);
    if (reason == WMReturnTextMovement) {
        action_go(NULL, app);
    }
}

void filter_field_action(void *observer, WMNotification *notif)
{
    WinderApp *app = (WinderApp *)observer;
    char *text;
    (void)notif;

    text = WMGetTextFieldText(app->filterField);
    if (text) {
        wstrlcpy(app->filter, text, sizeof(app->filter));
        wfree(text);
    } else {
        app->filter[0] = '\0';
    }
    winder_refresh(app);
}
