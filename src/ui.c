/* Main window layout, views, and navigation for Winder */
#include "winder.h"

/* keyboard helpers (defined later) */
static void ensure_browser_key_handlers(WinderApp *app);
static void focus_file_surface(WinderApp *app);
static void install_key_handlers(WinderApp *app);
static void winder_key_handler(XEvent *event, void *data);

/* ---- sort helpers ---- */
#define CAST_ITEM(p) (*((WMListItem **)(p)))

/* browser: folders first, then name */
static int browser_item_comparer(const void *a, const void *b)
{
    WMListItem *ia = CAST_ITEM(a);
    WMListItem *ib = CAST_ITEM(b);

    if (ia->isBranch != ib->isBranch)
        return ia->isBranch ? -1 : 1;
    return strcasecmp(ia->text, ib->text);
}

/* list view: sort by active column; folders still float first for name sort */
static WinderApp *g_sort_app; /* comparer has no user-data arg */

static int list_item_comparer(const void *a, const void *b)
{
    WMListItem *ia = CAST_ITEM(a);
    WMListItem *ib = CAST_ITEM(b);
    FileEntry *fa = (FileEntry *)ia->clientData;
    FileEntry *fb = (FileEntry *)ib->clientData;
    int dir = (g_sort_app && !g_sort_app->sortAscending) ? -1 : 1;
    int cmp = 0;

    if (!fa || !fb)
        return 0;

    /* folders first, always (Finder-like), independent of direction for type */
    if (fa->is_dir != fb->is_dir)
        return fa->is_dir ? -1 : 1;

    switch (g_sort_app ? g_sort_app->sortColumn : SORT_NAME) {
    case SORT_SIZE:
        if (fa->size < fb->size) cmp = -1;
        else if (fa->size > fb->size) cmp = 1;
        else cmp = strcasecmp(fa->name, fb->name);
        break;
    case SORT_KIND:
        cmp = strcasecmp(fa->kind ? fa->kind : "", fb->kind ? fb->kind : "");
        if (cmp == 0)
            cmp = strcasecmp(fa->name, fb->name);
        break;
    case SORT_DATE:
        if (fa->mtime < fb->mtime) cmp = -1;
        else if (fa->mtime > fb->mtime) cmp = 1;
        else cmp = strcasecmp(fa->name, fb->name);
        break;
    case SORT_NAME:
    default:
        cmp = strcasecmp(fa->name, fb->name);
        break;
    }
    return cmp * dir;
}

#undef CAST_ITEM

void file_entry_free(FileEntry *fe)
{
    if (!fe)
        return;
    if (fe->name)
        wfree(fe->name);
    wfree(fe);
}

static void free_list_entries(WMList *list)
{
    WMArray *items;
    int i, n;

    items = WMGetListItems(list);
    if (!items)
        return;
    n = WMGetArrayItemCount(items);
    for (i = 0; i < n; i++) {
        WMListItem *it = WMGetFromArray(items, i);
        if (it && it->clientData) {
            file_entry_free((FileEntry *)it->clientData);
            it->clientData = NULL;
        }
    }
}

static int name_matches_filter(WinderApp *app, const char *name)
{
    if (!app->filter[0])
        return 1;
    {
        char nbuf[256], fbuf[256];
        size_t i, lim = sizeof(nbuf) - 1;
        for (i = 0; name[i] && i < lim; i++)
            nbuf[i] = (char)tolower((unsigned char)name[i]);
        nbuf[i] = '\0';
        for (i = 0; app->filter[i] && i < sizeof(fbuf) - 1; i++)
            fbuf[i] = (char)tolower((unsigned char)app->filter[i]);
        fbuf[i] = '\0';
        return strstr(nbuf, fbuf) != NULL;
    }
}

/* ---- column browser fill ---- */
static void list_directory_on_column(WinderApp *app, int column, const char *path)
{
    DIR *dir;
    struct dirent *de;
    struct stat st;
    char full[PATH_MAX];
    char *title;

    title = fs_basename(path);
    WMSetBrowserColumnTitle(app->browser, column, title);
    wfree(title);

    dir = opendir(path);
    if (!dir)
        return;

    while ((de = readdir(dir))) {
        int is_dir;

        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (!app->showHidden && de->d_name[0] == '.')
            continue;
        /* column view keeps full tree visible; Find filter is list-view only */

        if (strcmp(path, "/") == 0) {
            if (snprintf(full, sizeof(full), "/%s", de->d_name) >= (int)sizeof(full))
                continue;
        } else {
            if (snprintf(full, sizeof(full), "%s/%s", path, de->d_name) >= (int)sizeof(full))
                continue;
        }

        if (lstat(full, &st) != 0)
            continue;

        is_dir = S_ISDIR(st.st_mode);
        /* follow symlink-to-dir for branch glyph */
        if (!is_dir && S_ISLNK(st.st_mode)) {
            struct stat st2;
            if (stat(full, &st2) == 0 && S_ISDIR(st2.st_mode))
                is_dir = 1;
        }
        WMInsertBrowserItem(app->browser, column, -1, de->d_name, is_dir);
    }
    closedir(dir);
    WMSortBrowserColumnWithComparer(app->browser, column, browser_item_comparer);
}

void fill_browser_column(WMBrowserDelegate *self, WMBrowser *bPtr,
                         int column, WMList *list)
{
    WinderApp *app = (WinderApp *)self->data;
    char *path;

    (void)bPtr;

    if (column > 0)
        path = WMGetBrowserPathToColumn(app->browser, column - 1);
    else
        path = wstrdup("/");

    list_directory_on_column(app, column, path);
    wfree(path);

    /*
     * New columns are created as the user drills in. Attach keyboard and
     * right-click handlers to each column list as soon as it is filled —
     * otherwise only the first few columns (from initial load) get them.
     */
    if (list) {
        WMCreateEventHandler(WMWidgetView(list), KeyPressMask,
                             winder_key_handler, app);
        context_menu_attach_list(app, list);
    }
}

/* ---- list view (pixel-aligned columns under header) ---- */

/* Fit text into max_w pixels; right-align if requested. */
static void draw_cell_text(WMScreen *scr, Drawable d, WMColor *color, WMFont *font,
                           int x, int y, int max_w, const char *text, int right_align)
{
    char buf[512];
    int len, tw, draw_x;
    const char *draw;

    if (!text)
        text = "";
    len = (int)strlen(text);
    if (len == 0 || max_w < 4)
        return;

    draw = text;
    tw = WMWidthOfString(font, text, len);
    if (tw > max_w) {
        int lo = 0, hi = len, best = 0;

        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            int n;

            if (mid > (int)sizeof(buf) - 4)
                mid = (int)sizeof(buf) - 4;
            memcpy(buf, text, (size_t)mid);
            buf[mid] = '.';
            buf[mid + 1] = '.';
            buf[mid + 2] = '.';
            buf[mid + 3] = '\0';
            n = WMWidthOfString(font, buf, mid + 3);
            if (n <= max_w) {
                best = mid;
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        if (best <= 0)
            return;
        memcpy(buf, text, (size_t)best);
        buf[best] = '.';
        buf[best + 1] = '.';
        buf[best + 2] = '.';
        buf[best + 3] = '\0';
        draw = buf;
        len = best + 3;
        tw = WMWidthOfString(font, draw, len);
    }

    if (right_align) {
        draw_x = x + max_w - tw;
        if (draw_x < x)
            draw_x = x;
    } else {
        draw_x = x;
    }

    WMDrawString(scr, d, color, font, draw_x, y, draw, len);
}

static void list_compute_columns(WinderApp *app, int content_w)
{
    int name_w;

    app->colSizeW = COL_SIZE_W;
    app->colKindW = COL_KIND_W;
    app->colDateW = COL_DATE_W;
    name_w = content_w - app->colSizeW - app->colKindW - app->colDateW;
    if (name_w < 64)
        name_w = 64;
    app->colNameW = name_w;
}

static void list_draw_item(WMList *lPtr, int index, Drawable d, char *text,
                           int state, WMRect *rect)
{
    WinderApp *app = (WinderApp *)WMGetHangedData(lPtr);
    WMScreen *scr = WMWidgetScreen(lPtr);
    Display *dpy = WMScreenDisplay(scr);
    WMListItem *item;
    FileEntry *fe;
    WMColor *bg, *fg;
    WMFont *font;
    int x, y, h, ty;
    int name_w, size_w, kind_w, date_w;
    int cx;
    char sizebuf[32], timebuf[32];
    const char *kind;
    const char *name;

    (void)text;

    if (!app)
        return;

    item = WMGetListItem(lPtr, index);
    fe = item ? (FileEntry *)item->clientData : NULL;

    x = rect->pos.x;
    y = rect->pos.y;
    h = rect->size.height;

    /* recompute from drawable width so resize stays aligned */
    list_compute_columns(app, rect->size.width);
    name_w = app->colNameW;
    size_w = app->colSizeW;
    kind_w = app->colKindW;
    date_w = app->colDateW;

    if (state & WLDSSelected) {
        bg = WMWhiteColor(scr);
        fg = WMBlackColor(scr);
    } else {
        bg = WMGrayColor(scr);
        fg = WMBlackColor(scr);
    }

    XFillRectangle(dpy, d, WMColorGC(bg), x, y, rect->size.width, h);

    font = app->listFont ? app->listFont : WMSystemFontOfSize(scr, 12);
    ty = y + (h - (int)WMFontHeight(font)) / 2;
    if (ty < y)
        ty = y + 1;

    if (!fe) {
        if (text && text[0])
            draw_cell_text(scr, d, fg, font, x + LIST_COL_PAD, ty,
                           rect->size.width - LIST_COL_PAD * 2, text, 0);
        WMReleaseColor(bg);
        WMReleaseColor(fg);
        return;
    }

    name = fe->name ? fe->name : "";
    kind = fe->kind ? fe->kind : "";
    if (fe->is_dir)
        snprintf(sizebuf, sizeof(sizebuf), "--");
    else
        fs_format_size(fe->size, sizebuf, sizeof(sizebuf));
    fs_format_time(fe->mtime, timebuf, sizeof(timebuf));

    cx = x;
    /* Name */
    draw_cell_text(scr, d, fg, font, cx + LIST_COL_PAD, ty,
                   name_w - LIST_COL_PAD * 2, name, 0);
    cx += name_w;
    /* Size (right-aligned in its cell) */
    draw_cell_text(scr, d, fg, font, cx + LIST_COL_PAD, ty,
                   size_w - LIST_COL_PAD * 2, sizebuf, 1);
    cx += size_w;
    /* Kind */
    draw_cell_text(scr, d, fg, font, cx + LIST_COL_PAD, ty,
                   kind_w - LIST_COL_PAD * 2, kind, 0);
    cx += kind_w;
    /* Date */
    draw_cell_text(scr, d, fg, font, cx + LIST_COL_PAD, ty,
                   date_w - LIST_COL_PAD * 2, timebuf, 0);

    WMReleaseColor(bg);
    WMReleaseColor(fg);
}

void list_update_sort_headers(WinderApp *app)
{
    const char *arrow = app->sortAscending ? " ▲" : " ▼";
    char label[48];

    snprintf(label, sizeof(label), "Name%s",
             app->sortColumn == SORT_NAME ? arrow : "");
    WMSetButtonText(app->btnSortName, label);

    snprintf(label, sizeof(label), "Size%s",
             app->sortColumn == SORT_SIZE ? arrow : "");
    WMSetButtonText(app->btnSortSize, label);

    snprintf(label, sizeof(label), "Kind%s",
             app->sortColumn == SORT_KIND ? arrow : "");
    WMSetButtonText(app->btnSortKind, label);

    snprintf(label, sizeof(label), "Date Modified%s",
             app->sortColumn == SORT_DATE ? arrow : "");
    WMSetButtonText(app->btnSortDate, label);
}

static void apply_list_sort(WinderApp *app)
{
    g_sort_app = app;
    WMSortListItemsWithComparer(app->listView, list_item_comparer);
    g_sort_app = NULL;
    list_update_sort_headers(app);
}

void populate_list_view(WinderApp *app)
{
    DIR *dir;
    struct dirent *de;
    struct stat st;
    char full[PATH_MAX];
    WMListItem *item;
    FileEntry *fe;

    free_list_entries(app->listView);
    WMClearList(app->listView);

    dir = opendir(app->currentPath);
    if (!dir) {
        item = WMAddListItem(app->listView, "(cannot open directory)");
        item->disabled = 1;
        list_update_sort_headers(app);
        return;
    }

    while ((de = readdir(dir))) {
        int is_dir;

        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (!app->showHidden && de->d_name[0] == '.')
            continue;
        if (!name_matches_filter(app, de->d_name))
            continue;

        if (strcmp(app->currentPath, "/") == 0) {
            if (snprintf(full, sizeof(full), "/%s", de->d_name) >= (int)sizeof(full))
                continue;
        } else {
            if (snprintf(full, sizeof(full), "%s/%s",
                         app->currentPath, de->d_name) >= (int)sizeof(full))
                continue;
        }

        if (lstat(full, &st) != 0)
            continue;

        is_dir = S_ISDIR(st.st_mode);
        if (!is_dir && S_ISLNK(st.st_mode)) {
            struct stat st2;
            if (stat(full, &st2) == 0 && S_ISDIR(st2.st_mode))
                is_dir = 1;
        }

        fe = wmalloc(sizeof(FileEntry));
        memset(fe, 0, sizeof(FileEntry));
        fe->name = wstrdup(de->d_name);
        fe->size = S_ISDIR(st.st_mode) ? 0 : st.st_size;
        fe->mtime = st.st_mtime;
        fe->is_dir = is_dir;
        fe->kind = fs_kind_for(full, st.st_mode);

        /* text = name only (typeahead); columns are painted by list_draw_item */
        item = WMAddListItem(app->listView, fe->name);
        item->isBranch = is_dir ? 1 : 0;
        item->clientData = fe;
    }
    closedir(dir);
    apply_list_sort(app);
}

/* ---- sidebar favorites ---- */
static void add_favorite(WinderApp *app, const char *label, const char *path)
{
    if (app->favoriteCount >= SIDEBAR_MAX)
        return;
    app->favorites[app->favoriteCount].label = wstrdup(label);
    app->favorites[app->favoriteCount].path = path ? wstrdup(path) : NULL;
    app->favorites[app->favoriteCount].is_separator = (path == NULL);
    app->favoriteCount++;
}

static void build_favorites(WinderApp *app)
{
    char *home = fs_home_dir();
    char path[PATH_MAX];
    int i;

    for (i = 0; i < app->favoriteCount; i++) {
        if (app->favorites[i].label) wfree(app->favorites[i].label);
        if (app->favorites[i].path) wfree(app->favorites[i].path);
        app->favorites[i].label = NULL;
        app->favorites[i].path = NULL;
    }
    app->favoriteCount = 0;

    add_favorite(app, "Home", home);
    snprintf(path, sizeof(path), "%s/Desktop", home);
    if (fs_is_dir(path)) add_favorite(app, "Desktop", path);
    snprintf(path, sizeof(path), "%s/Documents", home);
    if (fs_is_dir(path)) add_favorite(app, "Documents", path);
    snprintf(path, sizeof(path), "%s/Downloads", home);
    if (fs_is_dir(path)) add_favorite(app, "Downloads", path);
    snprintf(path, sizeof(path), "%s/Pictures", home);
    if (fs_is_dir(path)) add_favorite(app, "Pictures", path);
    snprintf(path, sizeof(path), "%s/Music", home);
    if (fs_is_dir(path)) add_favorite(app, "Music", path);
    snprintf(path, sizeof(path), "%s/Videos", home);
    if (fs_is_dir(path)) add_favorite(app, "Videos", path);

    add_favorite(app, "— Locations —", NULL);
    add_favorite(app, "Computer", "/");
    if (fs_is_dir("/media")) add_favorite(app, "Media", "/media");
    if (fs_is_dir("/mnt")) add_favorite(app, "Mounts", "/mnt");
    if (fs_is_dir("/tmp")) add_favorite(app, "Temporary", "/tmp");

    wfree(home);
}

void populate_sidebar(WinderApp *app)
{
    int i;
    WMListItem *item;

    build_favorites(app);
    WMClearList(app->sidebar);

    for (i = 0; i < app->favoriteCount; i++) {
        item = WMAddListItem(app->sidebar, app->favorites[i].label);
        item->clientData = (void *)(intptr_t)i;
        if (app->favorites[i].is_separator)
            item->disabled = 1;
    }
}

/* ---- selection helpers ---- */
char *winder_selected_path(WinderApp *app)
{
    if (app->viewMode == VIEW_COLUMNS) {
        char *path = WMGetBrowserPath(app->browser);
        size_t n;

        if (!path)
            return wstrdup(app->currentPath);
        n = strlen(path);
        /* browser paths often end with '/' for directories */
        while (n > 1 && path[n - 1] == '/') {
            path[--n] = '\0';
        }
        if (n == 0) {
            wfree(path);
            return wstrdup("/");
        }
        return path;
    } else {
        WMListItem *item = WMGetListSelectedItem(app->listView);
        FileEntry *fe;
        char *full;

        if (!item || !item->clientData)
            return wstrdup(app->currentPath);
        fe = (FileEntry *)item->clientData;
        if (!fe->name)
            return wstrdup(app->currentPath);
        full = fs_join_path(app->currentPath, fe->name);
        return full;
    }
}

char *winder_selected_directory(WinderApp *app)
{
    char *sel = winder_selected_path(app);
    char *dir;

    if (fs_is_dir(sel)) {
        return sel;
    }
    dir = fs_dirname(sel);
    wfree(sel);
    return dir;
}

/* ---- status / preview ---- */
void winder_update_status(WinderApp *app)
{
    int folders = 0, files = 0;
    off_t total;
    char sizebuf[32];
    char msg[256];
    char *sel;

    total = fs_dir_entry_count(app->currentPath, app->showHidden,
                               &folders, &files);
    fs_format_size(total, sizebuf, sizeof(sizebuf));

    sel = winder_selected_path(app);
    if (sel && strcmp(sel, app->currentPath) != 0) {
        char *base = fs_basename(sel);
        snprintf(msg, sizeof(msg),
                 "%d folders, %d files  ·  %s selected  ·  %s on disk in view",
                 folders, files, base, sizebuf);
        wfree(base);
    } else {
        snprintf(msg, sizeof(msg),
                 "%d folders, %d files  ·  %s on disk in view",
                 folders, files, sizebuf);
    }
    if (sel) wfree(sel);

    WMSetLabelText(app->statusLabel, msg);
}

/* ---- Get Info media helpers ---- */

/* Compact 32x32 XPM placeholders (exact geometry) */
static char *xpm_doc[] = {
    "32 32 4 1",
    "  c None",
    ". c #333333",
    "X c #F0F0F0",
    "o c #A8A8A8",
    "                                ",
    "      ................          ",
    "      .XXXXXXXXXXXXXXo.         ",
    "      .XXXXXXXXXXXXXXo.         ",
    "      .XXXXXXXXXXXXXXo.         ",
    "      .XXXXXXXX.......          ",
    "      .XXXXXXXX.ooooo.          ",
    "      .XXXXXXXX.ooooo.          ",
    "      .XXXXXXXXXXXXXXo.         ",
    "      .XXXXXXXXXXXXXXo.         ",
    "      .XX..........XXo.         ",
    "      .XXXXXXXXXXXXXXo.         ",
    "      .XX..........XXo.         ",
    "      .XXXXXXXXXXXXXXo.         ",
    "      .XX..........XXo.         ",
    "      .XXXXXXXXXXXXXXo.         ",
    "      .XX..........XXo.         ",
    "      .XXXXXXXXXXXXXXo.         ",
    "      .XXXXXXXXXXXXXXo.         ",
    "      .XXXXXXXXXXXXXXo.         ",
    "      ..................        ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                "
};

static char *xpm_audio[] = {
    "32 32 3 1",
    "  c None",
    ". c #222222",
    "X c #3B7DD8",
    "                                ",
    "             .....              ",
    "            .XXXXX.             ",
    "           .XXXXXXX.            ",
    "          .XXXXX...             ",
    "         .XXXXX.                ",
    "        .XXXXX.                 ",
    "       .XXXXX.                  ",
    "      .XXXXX.                   ",
    "     .XXXXX.                    ",
    "    .XXXXX.                     ",
    "    .XXXX.   ...                ",
    "    .XXX.   .XXX.               ",
    "    .XX.   .XXXXX.              ",
    "    .X.   .XXXXXXX.             ",
    "    ..    .XXXXXXX.             ",
    "           .XXXXX.              ",
    "            .XXX.               ",
    "             ...                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                "
};

static char *xpm_video[] = {
    "32 32 3 1",
    "  c None",
    ". c #222222",
    "X c #C23B45",
    "                                ",
    "  ........................      ",
    "  .XXXXXXXXXXXXXXXXXXXXXX.      ",
    "  .XX..................XX.      ",
    "  .XX.XXXXXXXXXXXXXXXX.XX.      ",
    "  .XX.XX..XXXXXXXX..XX.XX.      ",
    "  .XX.XXXXXXXXXXXXXXXX.XX.      ",
    "  .XX.XX..XXXXXXXX..XX.XX.      ",
    "  .XX.XXXXXXXXXXXXXXXX.XX.      ",
    "  .XX.XX..XXXXXXXX..XX.XX.      ",
    "  .XX.XXXXXXXXXXXXXXXX.XX.      ",
    "  .XX..................XX.      ",
    "  .XXXXXXXXXXXXXXXXXXXXXX.      ",
    "  ........................      ",
    "                                ",
    "         ..........             ",
    "        .XXXXXXXXXX.            ",
    "       .XX........XX.           ",
    "       .XX.  ..  .XX.           ",
    "       .XX. .XX. .XX.           ",
    "       .XX.  ..  .XX.           ",
    "       .XX........XX.           ",
    "        .XXXXXXXXXX.            ",
    "         ..........             ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                "
};

static char *xpm_archive[] = {
    "32 32 3 1",
    "  c None",
    ". c #222222",
    "X c #C9A227",
    "                                ",
    "     ....................       ",
    "     .XXXXXXXXXXXXXXXXXX.       ",
    "     .XX..............XX.       ",
    "     .XX.XXXXXXXXXXXX.XX.       ",
    "     .XX.X..........X.XX.       ",
    "     .XX.XXXXXXXXXXXX.XX.       ",
    "     .XX..............XX.       ",
    "     .XXXXXXXXXXXXXXXXXX.       ",
    "     .XX..............XX.       ",
    "     .XX.XXXXXXXXXXXX.XX.       ",
    "     .XX.X..........X.XX.       ",
    "     .XX.XXXXXXXXXXXX.XX.       ",
    "     .XX..............XX.       ",
    "     .XXXXXXXXXXXXXXXXXX.       ",
    "     .XX..............XX.       ",
    "     .XX.XXXXXXXXXXXX.XX.       ",
    "     .XX.X..........X.XX.       ",
    "     .XX.XXXXXXXXXXXX.XX.       ",
    "     .XX..............XX.       ",
    "     .XXXXXXXXXXXXXXXXXX.       ",
    "     ....................       ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                "
};

static char *xpm_folder[] = {
    "32 32 3 1",
    "  c None",
    ". c #222222",
    "X c #E0C060",
    "                                ",
    "   ..........                   ",
    "  .XXXXXXXXXX.                  ",
    " .XXXXXXXXXXXX............      ",
    " .XXXXXXXXXXXXXXXXXXXXXXXX.     ",
    " .XXXXXXXXXXXXXXXXXXXXXXXX.     ",
    " .XXXXXXXXXXXXXXXXXXXXXXXX.     ",
    " .XXXXXXXXXXXXXXXXXXXXXXXX.     ",
    " .XXXXXXXXXXXXXXXXXXXXXXXX.     ",
    " .XXXXXXXXXXXXXXXXXXXXXXXX.     ",
    " .XXXXXXXXXXXXXXXXXXXXXXXX.     ",
    " .XXXXXXXXXXXXXXXXXXXXXXXX.     ",
    " .XXXXXXXXXXXXXXXXXXXXXXXX.     ",
    " .XXXXXXXXXXXXXXXXXXXXXXXX.     ",
    " .XXXXXXXXXXXXXXXXXXXXXXXX.     ",
    " .XXXXXXXXXXXXXXXXXXXXXXXX.     ",
    "  ........................      ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                "
};

static void preview_clear_media(WinderApp *app)
{
    WMSetLabelImage(app->previewImage, NULL);
    if (app->previewPixmap) {
        WMReleasePixmap(app->previewPixmap);
        app->previewPixmap = NULL;
    }
    WMClearText(app->previewBody);
    WMSetLabelText(app->previewImage, "");
}

static void preview_show_image_widget(WinderApp *app, int show_image)
{
    if (show_image) {
        WMMapWidget(app->previewImage);
        WMUnmapWidget(app->previewBody);
    } else {
        WMUnmapWidget(app->previewImage);
        WMMapWidget(app->previewBody);
    }
}

static WMPixmap *make_placeholder_pixmap(WinderApp *app, const char *path, mode_t mode)
{
    const char *label = fs_placeholder_label(path, mode);
    char **xpm = xpm_doc;

    if (!strcmp(label, "Audio"))
        xpm = xpm_audio;
    else if (!strcmp(label, "Video"))
        xpm = xpm_video;
    else if (!strcmp(label, "Archive"))
        xpm = xpm_archive;
    else if (S_ISDIR(mode))
        xpm = xpm_folder;

    return WMCreatePixmapFromXPMData(app->scr, xpm);
}

static int preview_load_image_thumb(WinderApp *app, const char *path)
{
    RContext *ctx;
    RImage *img = NULL, *scaled = NULL;
    WMPixmap *pix = NULL;
    unsigned max_w, max_h, tw, th;

    ctx = WMScreenRContext(app->scr);
    if (!ctx)
        return 0;

    img = RLoadImage(ctx, path, 0);
    if (!img)
        return 0;

    max_w = (unsigned)(PREVIEW_W - MARGIN * 4);
    max_h = (unsigned)(PREVIEW_MEDIA_H - MARGIN * 2);
    if (max_w < 32)
        max_w = 32;
    if (max_h < 32)
        max_h = 32;

    if ((unsigned)img->width <= max_w && (unsigned)img->height <= max_h) {
        scaled = RRetainImage(img);
    } else {
        if (img->width * (int)max_h > img->height * (int)max_w) {
            tw = max_w;
            th = (unsigned)((img->height * (int)max_w) / img->width);
        } else {
            th = max_h;
            tw = (unsigned)((img->width * (int)max_h) / img->height);
        }
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;
        scaled = RSmoothScaleImage(img, tw, th);
        if (!scaled)
            scaled = RScaleImage(img, tw, th);
    }

    RReleaseImage(img);
    if (!scaled)
        return 0;

    pix = WMCreatePixmapFromRImage(app->scr, scaled, 127);
    RReleaseImage(scaled);
    if (!pix)
        return 0;

    app->previewPixmap = pix;
    WMSetLabelImage(app->previewImage, pix);
    WMSetLabelImagePosition(app->previewImage, WIPImageOnly);
    WMSetLabelText(app->previewImage, NULL);
    preview_show_image_widget(app, 1);
    return 1;
}

static void preview_show_text_body(WinderApp *app, const char *text)
{
    WMFreezeText(app->previewBody);
    WMClearText(app->previewBody);
    if (text && text[0])
        WMAppendTextStream(app->previewBody, text);
    else
        WMAppendTextStream(app->previewBody, "(no preview)");
    WMThawText(app->previewBody);
    preview_show_image_widget(app, 0);
}

static void preview_show_placeholder(WinderApp *app, const char *path, mode_t mode)
{
    WMPixmap *pix;
    char msg[160];

    pix = make_placeholder_pixmap(app, path, mode);
    if (pix) {
        app->previewPixmap = pix;
        WMSetLabelImage(app->previewImage, pix);
        WMSetLabelImagePosition(app->previewImage, WIPImageOnly);
        WMSetLabelText(app->previewImage, "");
        preview_show_image_widget(app, 1);
    } else {
        snprintf(msg, sizeof(msg),
                 "No quick preview\n\n%s",
                 fs_placeholder_label(path, mode));
        preview_show_text_body(app, msg);
    }
}

void winder_update_preview(WinderApp *app, const char *path)
{
    struct stat st;
    char buf[PATH_MAX];
    char sizebuf[32], timebuf[32], modebuf[16];
    char *base;
    const char *kind;
    PreviewKind pk;

    preview_clear_media(app);

    if (!path || lstat(path, &st) != 0) {
        WMSetLabelText(app->previewName, "No selection");
        WMSetLabelText(app->previewKind, "");
        WMSetLabelText(app->previewSize, "");
        WMSetLabelText(app->previewDate, "");
        WMSetLabelText(app->previewPerms, "");
        WMSetLabelText(app->previewPath, "");
        preview_show_text_body(app, "Select a file or folder\nto see a preview.");
        app->lastPreviewPath[0] = '\0';
        return;
    }

    wstrlcpy(app->lastPreviewPath, path, sizeof(app->lastPreviewPath));

    base = fs_basename(path);
    kind = fs_kind_for(path, st.st_mode);
    fs_format_size(S_ISDIR(st.st_mode) ? 0 : st.st_size, sizebuf, sizeof(sizebuf));
    fs_format_time(st.st_mtime, timebuf, sizeof(timebuf));
    fs_format_mode(st.st_mode, modebuf, sizeof(modebuf));

    WMSetLabelText(app->previewName, base);
    wfree(base);

    snprintf(buf, sizeof(buf), "Kind: %s", kind);
    WMSetLabelText(app->previewKind, buf);

    if (S_ISDIR(st.st_mode)) {
        int f = 0, d = 0;
        fs_dir_entry_count(path, app->showHidden, &d, &f);
        snprintf(buf, sizeof(buf), "Contents: %d folders, %d files", d, f);
    } else {
        snprintf(buf, sizeof(buf), "Size: %s", sizebuf);
    }
    WMSetLabelText(app->previewSize, buf);

    snprintf(buf, sizeof(buf), "Modified: %s", timebuf);
    WMSetLabelText(app->previewDate, buf);

    snprintf(buf, sizeof(buf), "Permissions: %s", modebuf);
    WMSetLabelText(app->previewPerms, buf);

    snprintf(buf, sizeof(buf), "Where: %s", path);
    WMSetLabelText(app->previewPath, buf);

    /* --- content preview --- */
    pk = fs_preview_kind(path, st.st_mode);
    switch (pk) {
    case PREV_FOLDER: {
        char *listing = fs_folder_listing_preview(path, app->showHidden,
                                                  PREVIEW_FOLDER_MAX);
        preview_show_text_body(app, listing);
        if (listing)
            wfree(listing);
        break;
    }
    case PREV_TEXT: {
        char *text = fs_read_text_preview(path, PREVIEW_TEXT_MAX);
        preview_show_text_body(app, text);
        if (text)
            wfree(text);
        break;
    }
    case PREV_IMAGE:
        if (!preview_load_image_thumb(app, path))
            preview_show_placeholder(app, path, st.st_mode);
        break;
    case PREV_PLACEHOLDER:
    default:
        preview_show_placeholder(app, path, st.st_mode);
        break;
    }
}

void winder_update_nav_buttons(WinderApp *app)
{
    WMSetButtonEnabled(app->btnBack, hist_can_back(&app->history) ? True : False);
    WMSetButtonEnabled(app->btnForward, hist_can_forward(&app->history) ? True : False);
    WMSetButtonEnabled(app->btnUp,
                       (strcmp(app->currentPath, "/") != 0) ? True : False);
}

/* ---- path navigation ---- */
static void sync_path_field(WinderApp *app)
{
    WMSetTextFieldText(app->pathField, app->currentPath);
    {
        char title[PATH_MAX + 32];
        char *base = fs_basename(app->currentPath);
        snprintf(title, sizeof(title), "%s — %s", base, WINDER_APP_NAME);
        WMSetWindowTitle(app->win, title);
        WMSetWindowMiniwindowTitle(app->win, base);
        wfree(base);
    }
}

void browser_ensure_loaded(WinderApp *app)
{
    WMList *col0;

    if (!app->browserLoaded) {
        /* Required before any WMSetBrowserPath — creates & fills column 0 */
        WMLoadBrowserColumnZero(app->browser);
        app->browserLoaded = True;
        return;
    }

    /* Refill column 0 so Hidden / refresh actually updates entries */
    col0 = WMGetBrowserListInColumn(app->browser, 0);
    if (col0) {
        WMClearList(col0);
        list_directory_on_column(app, 0, "/");
    }
}

void winder_set_path(WinderApp *app, const char *path, int push_history)
{
    char *norm;
    char browser_path[PATH_MAX + 2];

    if (!path || !*path)
        return;

    context_menu_hide(app);

    norm = fs_normalize(path);
    if (!fs_is_dir(norm)) {
        /* if it's a file, open parent and select later — for now stay put */
        char *parent = fs_dirname(norm);
        wfree(norm);
        if (!fs_is_dir(parent)) {
            wfree(parent);
            WMRunAlertPanel(app->scr, app->win, "Winder",
                            "That location is not available.",
                            "OK", NULL, NULL);
            return;
        }
        norm = parent;
    }

    wstrlcpy(app->currentPath, norm, sizeof(app->currentPath));

    if (push_history && !app->suppressHistory)
        hist_push(&app->history, app->currentPath);

    sync_path_field(app);

    if (app->viewMode == VIEW_COLUMNS) {
        browser_ensure_loaded(app);
        /* path components separated by '/'; trailing slash is fine */
        if (strcmp(app->currentPath, "/") == 0)
            wstrlcpy(browser_path, "/", sizeof(browser_path));
        else
            snprintf(browser_path, sizeof(browser_path), "%s/", app->currentPath);
        WMSetBrowserPath(app->browser, browser_path);
        ensure_browser_key_handlers(app);
    } else {
        populate_list_view(app);
    }

    winder_update_nav_buttons(app);
    winder_update_status(app);
    winder_update_preview(app, app->currentPath);
    wfree(norm);
}

void winder_refresh(WinderApp *app)
{
    winder_set_path(app, app->currentPath, 0);
}

void winder_set_view_mode(WinderApp *app, WinderViewMode mode)
{
    app->viewMode = mode;

    if (mode == VIEW_COLUMNS) {
        WMMapWidget(app->browser);
        WMUnmapWidget(app->listPane);
        WMSetButtonSelected(app->btnColumns, True);
        WMSetButtonSelected(app->btnList, False);
    } else {
        WMUnmapWidget(app->browser);
        WMMapWidget(app->listPane);
        WMMapSubwidgets(app->listPane);
        WMMapSubwidgets(app->listHeader);
        WMMapWidget(app->listView);
        WMSetButtonSelected(app->btnColumns, False);
        WMSetButtonSelected(app->btnList, True);
    }
    winder_layout(app);
    winder_refresh(app);
    focus_file_surface(app);
}

/* ---- layout ---- */
void winder_layout(WinderApp *app)
{
    WMSize size = WMGetViewSize(WMWidgetView(app->win));
    int w = size.width;
    int h = size.height;
    int body_y, body_h;
    int content_x, content_w;
    int preview_w = app->showPreview ? PREVIEW_W : 0;
    int i, x;

    if (w < WIN_MIN_W) w = WIN_MIN_W;
    if (h < WIN_MIN_H) h = WIN_MIN_H;

    /* toolbar */
    WMMoveWidget(app->toolbar, 0, 0);
    WMResizeWidget(app->toolbar, w, TOOLBAR_H);

    x = MARGIN;
    WMMoveWidget(app->btnBack, x, (TOOLBAR_H - BTN_H) / 2); x += BTN_SM + 2;
    WMMoveWidget(app->btnForward, x, (TOOLBAR_H - BTN_H) / 2); x += BTN_SM + 2;
    WMMoveWidget(app->btnUp, x, (TOOLBAR_H - BTN_H) / 2); x += BTN_SM + 6;
    WMMoveWidget(app->btnHome, x, (TOOLBAR_H - BTN_H) / 2); x += BTN_W + 8;

    WMMoveWidget(app->btnColumns, x, (TOOLBAR_H - BTN_H) / 2); x += BTN_W + 2;
    WMMoveWidget(app->btnList, x, (TOOLBAR_H - BTN_H) / 2); x += BTN_W + 8;

    WMMoveWidget(app->btnNewFolder, x, (TOOLBAR_H - BTN_H) / 2); x += BTN_W + 2;
    WMMoveWidget(app->btnDelete, x, (TOOLBAR_H - BTN_H) / 2); x += BTN_W + 2;
    WMMoveWidget(app->btnRename, x, (TOOLBAR_H - BTN_H) / 2); x += BTN_W + 2;
    WMMoveWidget(app->btnOpen, x, (TOOLBAR_H - BTN_H) / 2); x += BTN_W + 8;

    WMMoveWidget(app->btnRefresh, x, (TOOLBAR_H - BTN_H) / 2); x += BTN_W + 2;
    WMMoveWidget(app->btnHidden, x, (TOOLBAR_H - BTN_H) / 2);

    /* path bar */
    WMMoveWidget(app->pathBar, 0, TOOLBAR_H);
    WMResizeWidget(app->pathBar, w, PATHBAR_H);

    WMMoveWidget(app->pathLabel, MARGIN, (PATHBAR_H - 16) / 2);
    WMResizeWidget(app->pathLabel, 40, 16);

    WMMoveWidget(app->pathField, MARGIN + 42, (PATHBAR_H - 22) / 2);
    WMResizeWidget(app->pathField, w - 42 - BTN_SM - 140 - MARGIN * 3, 22);

    WMMoveWidget(app->btnGo, w - BTN_SM - 130 - MARGIN * 2,
                 (PATHBAR_H - BTN_H) / 2);
    WMResizeWidget(app->btnGo, BTN_SM, BTN_H);

    WMMoveWidget(app->filterLabel, w - 120 - MARGIN, (PATHBAR_H - 16) / 2);
    WMResizeWidget(app->filterLabel, 40, 16);
    WMMoveWidget(app->filterField, w - 80 - MARGIN, (PATHBAR_H - 22) / 2);
    WMResizeWidget(app->filterField, 80, 22);

    body_y = TOOLBAR_H + PATHBAR_H;
    body_h = h - body_y - STATUS_H;

    /* sidebar */
    WMMoveWidget(app->sidebarFrame, 0, body_y);
    WMResizeWidget(app->sidebarFrame, SIDEBAR_W, body_h);
    WMMoveWidget(app->sidebarTitle, MARGIN, 4);
    WMResizeWidget(app->sidebarTitle, SIDEBAR_W - MARGIN * 2, 16);
    WMMoveWidget(app->sidebar, 2, 22);
    WMResizeWidget(app->sidebar, SIDEBAR_W - 4, body_h - 24);

    content_x = SIDEBAR_W;
    content_w = w - SIDEBAR_W - preview_w;
    if (content_w < 200)
        content_w = 200;

    if (app->viewMode == VIEW_COLUMNS) {
        WMMoveWidget(app->browser, content_x + 2, body_y + 2);
        WMResizeWidget(app->browser, content_w - 4, body_h - 4);
    } else {
        int pane_w = content_w - 4;
        int pane_h = body_h - 4;
        int row_content_w;
        int hx;

        WMMoveWidget(app->listPane, content_x + 2, body_y + 2);
        WMResizeWidget(app->listPane, pane_w, pane_h);

        WMMoveWidget(app->listHeader, 0, 0);
        WMResizeWidget(app->listHeader, pane_w, LIST_HEADER_H);

        /*
         * WINGs paints list rows into a strip of width (listW - 2 - 19)
         * starting at x=19 (scroller is on the left). Headers use the same
         * origin and widths so each cell sits under its button.
         */
        row_content_w = pane_w - LIST_SCROLLER_W - 2;
        if (row_content_w < 200)
            row_content_w = 200;
        list_compute_columns(app, row_content_w);

        hx = LIST_SCROLLER_W;
        WMMoveWidget(app->btnSortName, hx, 0);
        WMResizeWidget(app->btnSortName, app->colNameW, LIST_HEADER_H);
        hx += app->colNameW;
        WMMoveWidget(app->btnSortSize, hx, 0);
        WMResizeWidget(app->btnSortSize, app->colSizeW, LIST_HEADER_H);
        hx += app->colSizeW;
        WMMoveWidget(app->btnSortKind, hx, 0);
        WMResizeWidget(app->btnSortKind, app->colKindW, LIST_HEADER_H);
        hx += app->colKindW;
        WMMoveWidget(app->btnSortDate, hx, 0);
        WMResizeWidget(app->btnSortDate, app->colDateW, LIST_HEADER_H);

        WMMoveWidget(app->listView, 0, LIST_HEADER_H);
        WMResizeWidget(app->listView, pane_w, pane_h - LIST_HEADER_H);
    }

    if (app->showPreview) {
        int py = body_y;
        int ph = body_h;
        int ly;

        WMMapWidget(app->previewFrame);
        WMMoveWidget(app->previewFrame, content_x + content_w, py);
        WMResizeWidget(app->previewFrame, preview_w, ph);

        WMMoveWidget(app->previewTitle, MARGIN, 6);
        WMResizeWidget(app->previewTitle, preview_w - MARGIN * 2, 18);

        ly = 28;
        WMMoveWidget(app->previewName, MARGIN, ly);
        WMResizeWidget(app->previewName, preview_w - MARGIN * 2, 32);
        ly += 34;

        /* media / text preview box */
        WMMoveWidget(app->previewBox, MARGIN, ly);
        WMResizeWidget(app->previewBox, preview_w - MARGIN * 2, PREVIEW_MEDIA_H);
        WMMoveWidget(app->previewImage, 1, 1);
        WMResizeWidget(app->previewImage,
                       preview_w - MARGIN * 2 - 2, PREVIEW_MEDIA_H - 2);
        WMMoveWidget(app->previewBody, 1, 1);
        WMResizeWidget(app->previewBody,
                       preview_w - MARGIN * 2 - 2, PREVIEW_MEDIA_H - 2);
        ly += PREVIEW_MEDIA_H + 8;

        WMMoveWidget(app->previewKind, MARGIN, ly);
        WMResizeWidget(app->previewKind, preview_w - MARGIN * 2, 16);
        ly += 18;
        WMMoveWidget(app->previewSize, MARGIN, ly);
        WMResizeWidget(app->previewSize, preview_w - MARGIN * 2, 16);
        ly += 18;
        WMMoveWidget(app->previewDate, MARGIN, ly);
        WMResizeWidget(app->previewDate, preview_w - MARGIN * 2, 16);
        ly += 18;
        WMMoveWidget(app->previewPerms, MARGIN, ly);
        WMResizeWidget(app->previewPerms, preview_w - MARGIN * 2, 16);
        ly += 20;
        {
            int path_h = ph - ly - MARGIN;
            if (path_h < 32)
                path_h = 32;
            WMMoveWidget(app->previewPath, MARGIN, ly);
            WMResizeWidget(app->previewPath, preview_w - MARGIN * 2, path_h);
        }
    } else {
        WMUnmapWidget(app->previewFrame);
    }

    WMMoveWidget(app->statusBar, 0, h - STATUS_H);
    WMResizeWidget(app->statusBar, w, STATUS_H);
    WMMoveWidget(app->statusLabel, MARGIN, 4);
    WMResizeWidget(app->statusLabel, w - MARGIN * 2, 16);

    (void)i;
}

static void resize_handler(void *self, WMNotification *notif)
{
    WinderApp *app = (WinderApp *)self;
    (void)notif;
    winder_layout(app);
}

/* ---- keyboard navigation (browse surfaces only; not path/Find fields) ---- */

static void ensure_browser_key_handlers(WinderApp *app);
static void browser_sync_from_selection(WinderApp *app);

static void list_ensure_visible(WMList *list, int row)
{
    int top, vis, item_h, h;

    if (!list || row < 0)
        return;
    item_h = WMGetListItemHeight(list);
    if (item_h < 1)
        item_h = 1;
    h = (int)WMWidgetHeight(list);
    vis = h / item_h;
    if (vis < 1)
        vis = 1;
    top = WMGetListPosition(list);
    if (row < top)
        WMSetListPosition(list, row);
    else if (row >= top + vis)
        WMSetListPosition(list, row - vis + 1);
}

static int list_visible_rows(WMList *list)
{
    int item_h = WMGetListItemHeight(list);
    int vis;

    if (item_h < 1)
        item_h = 1;
    vis = (int)WMWidgetHeight(list) / item_h;
    return vis > 0 ? vis : 1;
}

/* Move selection in a list; returns new row or -1 */
static int list_move_selection(WMList *list, int delta)
{
    int n, cur, next;

    if (!list)
        return -1;
    n = WMGetListNumberOfRows(list);
    if (n <= 0)
        return -1;
    cur = WMGetListSelectedItemRow(list);
    if (cur == WLNotFound || cur < 0)
        next = (delta >= 0) ? 0 : n - 1;
    else
        next = cur + delta;
    if (next < 0)
        next = 0;
    if (next >= n)
        next = n - 1;
    WMSelectListItem(list, next);
    list_ensure_visible(list, next);
    return next;
}

static int list_jump_selection(WMList *list, int row)
{
    int n;

    if (!list)
        return -1;
    n = WMGetListNumberOfRows(list);
    if (n <= 0)
        return -1;
    if (row < 0)
        row = 0;
    if (row >= n)
        row = n - 1;
    WMSelectListItem(list, row);
    list_ensure_visible(list, row);
    return row;
}

static WMList *browser_focus_list(WinderApp *app)
{
    int col;

    if (!app->browser)
        return NULL;
    col = WMGetBrowserSelectedColumn(app->browser);
    if (col < 0)
        col = WMGetBrowserNumberOfColumns(app->browser) - 1;
    if (col < 0)
        return NULL;
    return WMGetBrowserListInColumn(app->browser, col);
}

static void focus_file_surface(WinderApp *app)
{
    if (app->viewMode == VIEW_COLUMNS) {
        WMList *list = browser_focus_list(app);
        if (list)
            WMSetFocusToWidget(list);
        else
            WMSetFocusToWidget(app->browser);
    } else {
        WMSetFocusToWidget(app->listView);
    }
}

/*
 * WMSelectListItem does not run the browser list action, so the next column
 * would stay stale. Re-apply the path so columns match the selection.
 */
static void browser_sync_from_selection(WinderApp *app)
{
    char *path;
    char bp[PATH_MAX + 4];
    size_t n;

    path = WMGetBrowserPath(app->browser);
    if (!path || !path[0]) {
        if (path)
            wfree(path);
        browser_click((WMWidget *)app->browser, app);
        return;
    }

    n = strlen(path);
    while (n > 1 && path[n - 1] == '/')
        path[--n] = '\0';

    if (fs_is_dir(path)) {
        if (strcmp(path, "/") == 0)
            wstrlcpy(bp, "/", sizeof(bp));
        else
            snprintf(bp, sizeof(bp), "%s/", path);
    } else {
        wstrlcpy(bp, path, sizeof(bp));
    }

    WMSetBrowserPath(app->browser, bp);
    wfree(path);
    ensure_browser_key_handlers(app);
    browser_click((WMWidget *)app->browser, app);
}

static void handle_browser_nav(WinderApp *app, KeySym ksym)
{
    WMList *list = browser_focus_list(app);
    int col;

    if (!list)
        return;

    col = WMGetBrowserSelectedColumn(app->browser);
    if (col < 0)
        col = 0;

    switch (ksym) {
    case XK_Up:
        if (list_move_selection(list, -1) >= 0)
            browser_sync_from_selection(app);
        break;
    case XK_Down:
        if (list_move_selection(list, 1) >= 0)
            browser_sync_from_selection(app);
        break;
    case XK_Home:
        if (list_jump_selection(list, 0) >= 0)
            browser_sync_from_selection(app);
        break;
    case XK_End:
        if (list_jump_selection(list, WMGetListNumberOfRows(list) - 1) >= 0)
            browser_sync_from_selection(app);
        break;
    case XK_Page_Up:
        if (list_move_selection(list, -list_visible_rows(list)) >= 0)
            browser_sync_from_selection(app);
        break;
    case XK_Page_Down:
        if (list_move_selection(list, list_visible_rows(list)) >= 0)
            browser_sync_from_selection(app);
        break;
    case XK_Left:
        if (col > 0) {
            WMList *prev = WMGetBrowserListInColumn(app->browser, col - 1);
            if (prev) {
                if (WMGetListSelectedItemRow(prev) < 0 &&
                    WMGetListNumberOfRows(prev) > 0)
                    WMSelectListItem(prev, 0);
                WMSetFocusToWidget(prev);
                browser_sync_from_selection(app);
            }
        } else {
            action_up(NULL, app);
            focus_file_surface(app);
        }
        break;
    case XK_Right: {
        WMListItem *item = WMGetListSelectedItem(list);
        if (item && item->isBranch) {
            browser_sync_from_selection(app);
            {
                WMList *next = browser_focus_list(app);
                /* Prefer the column after the one we started on */
                if (WMGetBrowserSelectedColumn(app->browser) <= col) {
                    next = WMGetBrowserListInColumn(app->browser, col + 1);
                }
                if (next) {
                    if (WMGetListNumberOfRows(next) > 0 &&
                        WMGetListSelectedItemRow(next) < 0)
                        WMSelectListItem(next, 0);
                    WMSetFocusToWidget(next);
                    browser_sync_from_selection(app);
                }
            }
        } else if (item) {
            action_open(NULL, app);
        }
        break;
    }
    default:
        break;
    }
}

static void handle_list_nav(WinderApp *app, KeySym ksym)
{
    WMList *list = app->listView;

    switch (ksym) {
    case XK_Up:
        if (list_move_selection(list, -1) >= 0)
            list_click((WMWidget *)list, app);
        break;
    case XK_Down:
        if (list_move_selection(list, 1) >= 0)
            list_click((WMWidget *)list, app);
        break;
    case XK_Home:
        if (list_jump_selection(list, 0) >= 0)
            list_click((WMWidget *)list, app);
        break;
    case XK_End:
        if (list_jump_selection(list, WMGetListNumberOfRows(list) - 1) >= 0)
            list_click((WMWidget *)list, app);
        break;
    case XK_Page_Up:
        if (list_move_selection(list, -list_visible_rows(list)) >= 0)
            list_click((WMWidget *)list, app);
        break;
    case XK_Page_Down:
        if (list_move_selection(list, list_visible_rows(list)) >= 0)
            list_click((WMWidget *)list, app);
        break;
    case XK_Left:
        action_back(NULL, app);
        break;
    case XK_Right:
        action_open(NULL, app);
        break;
    default:
        break;
    }
}

static void handle_sidebar_nav(WinderApp *app, KeySym ksym)
{
    WMList *list = app->sidebar;
    int n, cur, next;

    n = WMGetListNumberOfRows(list);
    if (n <= 0)
        return;
    cur = WMGetListSelectedItemRow(list);
    if (cur < 0)
        cur = 0;

    switch (ksym) {
    case XK_Up:
    case XK_Down: {
        int delta = (ksym == XK_Up) ? -1 : 1;
        next = cur;
        do {
            next += delta;
            if (next < 0 || next >= n)
                return;
            {
                WMListItem *it = WMGetListItem(list, next);
                if (it && !it->disabled)
                    break;
            }
        } while (1);
        WMSelectListItem(list, next);
        list_ensure_visible(list, next);
        sidebar_select((WMWidget *)list, app);
        break;
    }
    case XK_Return:
    case XK_KP_Enter:
    case XK_Right:
        sidebar_select((WMWidget *)list, app);
        focus_file_surface(app);
        break;
    default:
        break;
    }
}

/*
 * Key handler for browse surfaces only (lists, browser columns, chrome).
 * Path and Find text fields do NOT get this handler, so while the user
 * types there, arrows move the caret and '/' inserts a slash.
 */
static void winder_key_handler(XEvent *event, void *data)
{
    WinderApp *app = (WinderApp *)data;
    KeySym ksym;
    char buf[16];
    int len;

    if (event->type != KeyPress)
        return;

    /* Ctrl/Alt/Super combos left alone */
    if (event->xkey.state & (ControlMask | Mod1Mask | Mod4Mask))
        return;

    len = XLookupString(&event->xkey, buf, (int)sizeof(buf) - 1, &ksym, NULL);
    if (len < 0)
        len = 0;
    buf[len] = '\0';

    /* /  → jump to Find (only reaches here when not typing in path/Find) */
    if (ksym == XK_slash || (len == 1 && buf[0] == '/')) {
        WMSetFocusToWidget(app->filterField);
        return;
    }

    if (ksym == XK_Escape) {
        focus_file_surface(app);
        return;
    }

    if (ksym == XK_Return || ksym == XK_KP_Enter) {
        action_open(NULL, app);
        return;
    }

    if (ksym != XK_Up && ksym != XK_Down && ksym != XK_Left && ksym != XK_Right &&
        ksym != XK_Home && ksym != XK_End && ksym != XK_Page_Up && ksym != XK_Page_Down)
        return;

    /*
     * Which surface? Use view mode for files; sidebar if that list has a
     * selection focus — we attach this handler to the sidebar view too, so
     * when it is focused, keys arrive here with the sidebar as event window.
     */
    if (event->xany.window == WMViewXID(WMWidgetView(app->sidebar)) ||
        event->xany.window == WMViewXID(WMWidgetView(app->sidebarFrame))) {
        handle_sidebar_nav(app, ksym);
        return;
    }

    if (app->viewMode == VIEW_LIST)
        handle_list_nav(app, ksym);
    else
        handle_browser_nav(app, ksym);
}

static void ensure_browser_key_handlers(WinderApp *app)
{
    int i, n;

    if (!app->browser)
        return;
    n = WMGetBrowserNumberOfColumns(app->browser);
    for (i = 0; i < n; i++) {
        WMList *list = WMGetBrowserListInColumn(app->browser, i);
        if (list) {
            WMCreateEventHandler(WMWidgetView(list), KeyPressMask,
                                 winder_key_handler, app);
            context_menu_attach_list(app, list);
        }
    }
    WMCreateEventHandler(WMWidgetView(app->browser), KeyPressMask,
                         winder_key_handler, app);
}

void winder_bind_browser_lists(WinderApp *app)
{
    ensure_browser_key_handlers(app);
}

static void install_key_handlers(WinderApp *app)
{
    /* Browse surfaces only — never pathField or filterField */
    WMCreateEventHandler(WMWidgetView(app->win), KeyPressMask,
                         winder_key_handler, app);
    WMCreateEventHandler(WMWidgetView(app->listView), KeyPressMask,
                         winder_key_handler, app);
    WMCreateEventHandler(WMWidgetView(app->listPane), KeyPressMask,
                         winder_key_handler, app);
    WMCreateEventHandler(WMWidgetView(app->sidebar), KeyPressMask,
                         winder_key_handler, app);
    WMCreateEventHandler(WMWidgetView(app->sidebarFrame), KeyPressMask,
                         winder_key_handler, app);
    WMCreateEventHandler(WMWidgetView(app->toolbar), KeyPressMask,
                         winder_key_handler, app);
    ensure_browser_key_handlers(app);

    context_menu_attach_list(app, app->listView);
    context_menu_attach_list(app, app->sidebar);
}

/* ---- UI construction ---- */
static WMButton *make_btn(WMWidget *parent, const char *title, int w,
                          WMAction *action, void *data)
{
    WMButton *b = WMCreateCommandButton(parent);
    WMResizeWidget(b, w, BTN_H);
    WMSetButtonText(b, title);
    WMSetButtonAction(b, action, data);
    return b;
}

void winder_build_ui(WinderApp *app)
{
    WMFont *bold;
    WMColor *sidebarBg;

    app->win = WMCreateWindow(app->scr, WINDER_WIN_NAME);
    WMResizeWidget(app->win, WIN_W, WIN_H);
    WMSetWindowTitle(app->win, WINDER_APP_NAME);
    WMSetWindowMiniwindowTitle(app->win, WINDER_APP_NAME);
    WMSetWindowCloseAction(app->win, close_app, app);
    WMSetWindowMinSize(app->win, WIN_MIN_W, WIN_MIN_H);

    WMSetViewNotifySizeChanges(WMWidgetView(app->win), True);
    WMAddNotificationObserver(resize_handler, app,
                              WMViewSizeDidChangeNotification,
                              WMWidgetView(app->win));

    /* --- toolbar --- */
    app->toolbar = WMCreateFrame(app->win);
    WMSetFrameRelief(app->toolbar, WRRaised);

    app->btnBack = make_btn(app->toolbar, "◀", BTN_SM, action_back, app);
    app->btnForward = make_btn(app->toolbar, "▶", BTN_SM, action_forward, app);
    app->btnUp = make_btn(app->toolbar, "▲", BTN_SM, action_up, app);
    app->btnHome = make_btn(app->toolbar, "Home", BTN_W, action_home, app);

    app->btnColumns = WMCreateButton(app->toolbar, WBTOnOff);
    WMResizeWidget(app->btnColumns, BTN_W, BTN_H);
    WMSetButtonText(app->btnColumns, "Columns");
    WMSetButtonAction(app->btnColumns, action_view_columns, app);
    WMSetButtonSelected(app->btnColumns, True);

    app->btnList = WMCreateButton(app->toolbar, WBTOnOff);
    WMResizeWidget(app->btnList, BTN_W, BTN_H);
    WMSetButtonText(app->btnList, "List");
    WMSetButtonAction(app->btnList, action_view_list, app);

    app->btnNewFolder = make_btn(app->toolbar, "New…", BTN_W, action_new_folder, app);
    app->btnDelete = make_btn(app->toolbar, "Delete", BTN_W, action_delete, app);
    app->btnRename = make_btn(app->toolbar, "Rename", BTN_W, action_rename, app);
    app->btnOpen = make_btn(app->toolbar, "Open", BTN_W, action_open, app);
    app->btnRefresh = make_btn(app->toolbar, "Reload", BTN_W, action_refresh, app);

    app->btnHidden = WMCreateButton(app->toolbar, WBTToggle);
    WMResizeWidget(app->btnHidden, BTN_W + 12, BTN_H);
    WMSetButtonText(app->btnHidden, "Hidden");
    WMSetButtonAction(app->btnHidden, action_toggle_hidden, app);

    /* --- path bar --- */
    app->pathBar = WMCreateFrame(app->win);
    WMSetFrameRelief(app->pathBar, WRSunken);

    app->pathLabel = WMCreateLabel(app->pathBar);
    WMSetLabelText(app->pathLabel, "Path");
    WMSetLabelTextAlignment(app->pathLabel, WARight);

    app->pathField = WMCreateTextField(app->pathBar);
    WMAddNotificationObserver(path_field_action, app,
                              WMTextDidEndEditingNotification,
                              app->pathField);

    app->btnGo = make_btn(app->pathBar, "Go", BTN_SM, action_go, app);

    app->filterLabel = WMCreateLabel(app->pathBar);
    WMSetLabelText(app->filterLabel, "Find");
    WMSetLabelTextAlignment(app->filterLabel, WARight);

    app->filterField = WMCreateTextField(app->pathBar);
    WMAddNotificationObserver(filter_field_action, app,
                              WMTextDidChangeNotification,
                              app->filterField);

    /* --- sidebar --- */
    app->sidebarFrame = WMCreateFrame(app->win);
    WMSetFrameRelief(app->sidebarFrame, WRSunken);
    sidebarBg = WMGrayColor(app->scr);
    WMSetWidgetBackgroundColor(app->sidebarFrame, sidebarBg);
    WMReleaseColor(sidebarBg);

    bold = WMBoldSystemFontOfSize(app->scr, 12);
    app->sidebarTitle = WMCreateLabel(app->sidebarFrame);
    WMSetLabelText(app->sidebarTitle, "Favorites");
    WMSetLabelFont(app->sidebarTitle, bold);

    app->sidebar = WMCreateList(app->sidebarFrame);
    WMSetListAction(app->sidebar, sidebar_select, app);
    WMSetListDoubleAction(app->sidebar, sidebar_select, app);
    WMSetListAllowEmptySelection(app->sidebar, True);

    /* --- column browser --- */
    app->browser = WMCreateBrowser(app->win);
    WMSetBrowserAllowEmptySelection(app->browser, True);
    WMSetBrowserHasScroller(app->browser, True);
    WMSetBrowserMaxVisibleColumns(app->browser, 4);
    WMSetBrowserTitled(app->browser, True);
    WMSetBrowserPathSeparator(app->browser, "/");

    memset(&app->browserDelegate, 0, sizeof(app->browserDelegate));
    app->browserDelegate.data = app;
    app->browserDelegate.createRowsForColumn = fill_browser_column;
    WMSetBrowserDelegate(app->browser, &app->browserDelegate);
    WMSetBrowserAction(app->browser, browser_click, app);
    WMSetBrowserDoubleAction(app->browser, browser_dclick, app);
    WMHangData(app->browser, app);

    /* --- list view (pane + sort header + rows) --- */
    app->listPane = WMCreateFrame(app->win);
    WMSetFrameRelief(app->listPane, WRSunken);

    app->listHeader = WMCreateFrame(app->listPane);
    WMSetFrameRelief(app->listHeader, WRRaised);

    app->btnSortName = WMCreateCommandButton(app->listHeader);
    WMSetButtonText(app->btnSortName, "Name");
    WMSetButtonAction(app->btnSortName, action_sort_name, app);
    WMSetButtonTextAlignment(app->btnSortName, WALeft);

    app->btnSortSize = WMCreateCommandButton(app->listHeader);
    WMSetButtonText(app->btnSortSize, "Size");
    WMSetButtonAction(app->btnSortSize, action_sort_size, app);
    WMSetButtonTextAlignment(app->btnSortSize, WARight);

    app->btnSortKind = WMCreateCommandButton(app->listHeader);
    WMSetButtonText(app->btnSortKind, "Kind");
    WMSetButtonAction(app->btnSortKind, action_sort_kind, app);
    WMSetButtonTextAlignment(app->btnSortKind, WALeft);

    app->btnSortDate = WMCreateCommandButton(app->listHeader);
    WMSetButtonText(app->btnSortDate, "Date Modified");
    WMSetButtonAction(app->btnSortDate, action_sort_date, app);
    WMSetButtonTextAlignment(app->btnSortDate, WALeft);

    app->listView = WMCreateList(app->listPane);
    WMSetListAction(app->listView, list_click, app);
    WMSetListDoubleAction(app->listView, list_dclick, app);
    WMSetListAllowEmptySelection(app->listView, True);
    WMSetListAllowMultipleSelection(app->listView, False);
    WMHangData(app->listView, app);
    app->listFont = WMSystemFontOfSize(app->scr, 12);
    WMSetListUserDrawProc(app->listView, list_draw_item);
    WMSetListUserDrawItemHeight(app->listView,
                                (unsigned short)(WMFontHeight(app->listFont) + 6));

    app->sortColumn = SORT_NAME;
    app->sortAscending = True;
    app->colNameW = 200;
    app->colSizeW = COL_SIZE_W;
    app->colKindW = COL_KIND_W;
    app->colDateW = COL_DATE_W;

    /* --- preview (Get Info style) --- */
    app->showPreview = True;
    app->previewPixmap = NULL;
    app->lastPreviewPath[0] = '\0';
    app->previewFrame = WMCreateFrame(app->win);
    WMSetFrameRelief(app->previewFrame, WRGroove);
    WMSetFrameTitle(app->previewFrame, "");

    app->previewTitle = WMCreateLabel(app->previewFrame);
    WMSetLabelText(app->previewTitle, "Get Info");
    WMSetLabelFont(app->previewTitle, bold);
    WMSetLabelTextAlignment(app->previewTitle, WACenter);

    app->previewName = WMCreateLabel(app->previewFrame);
    WMSetLabelTextAlignment(app->previewName, WACenter);
    WMSetLabelWraps(app->previewName, True);

    app->previewBox = WMCreateFrame(app->previewFrame);
    WMSetFrameRelief(app->previewBox, WRSunken);

    app->previewImage = WMCreateLabel(app->previewBox);
    WMSetLabelTextAlignment(app->previewImage, WACenter);
    WMSetLabelImagePosition(app->previewImage, WIPImageOnly);
    WMSetLabelRelief(app->previewImage, WRFlat);
    WMSetLabelWraps(app->previewImage, True);

    app->previewBody = WMCreateText(app->previewBox);
    WMSetTextHasVerticalScroller(app->previewBody, True);
    WMSetTextHasHorizontalScroller(app->previewBody, False);
    WMSetTextEditable(app->previewBody, False);
    WMSetTextIgnoresNewline(app->previewBody, False);
    WMSetTextUsesMonoFont(app->previewBody, True);
    WMSetTextRelief(app->previewBody, WRFlat);
    {
        WMFont *mono = WMSystemFontOfSize(app->scr, 10);
        if (mono) {
            WMSetTextDefaultFont(app->previewBody, mono);
            WMReleaseFont(mono);
        }
    }

    app->previewKind = WMCreateLabel(app->previewFrame);
    app->previewSize = WMCreateLabel(app->previewFrame);
    app->previewDate = WMCreateLabel(app->previewFrame);
    app->previewPerms = WMCreateLabel(app->previewFrame);
    app->previewPath = WMCreateLabel(app->previewFrame);
    WMSetLabelWraps(app->previewPath, True);

    WMReleaseFont(bold);

    /* --- status bar --- */
    app->statusBar = WMCreateFrame(app->win);
    WMSetFrameRelief(app->statusBar, WRRaised);
    app->statusLabel = WMCreateLabel(app->statusBar);
    WMSetLabelText(app->statusLabel, "Ready");

    app->viewMode = VIEW_COLUMNS;
    app->showHidden = False;
    app->browserLoaded = False;
    app->filter[0] = '\0';
    app->suppressHistory = False;

    populate_sidebar(app);

    /* realize / map */
    WMRealizeWidget(app->win);
    WMMapSubwidgets(app->toolbar);
    WMMapSubwidgets(app->pathBar);
    WMMapSubwidgets(app->sidebarFrame);
    WMMapSubwidgets(app->listHeader);
    WMMapSubwidgets(app->listPane);
    WMMapSubwidgets(app->previewBox);
    WMMapSubwidgets(app->previewFrame);
    WMMapSubwidgets(app->statusBar);
    WMMapSubwidgets(app->win);

    /* start with text placeholder area hidden under image defaults */
    WMUnmapWidget(app->previewBody);
    WMMapWidget(app->previewImage);

    WMMapWidget(app->browser);
    WMUnmapWidget(app->listPane);

    /* Column 0 must be loaded before any path is applied */
    browser_ensure_loaded(app);
    list_update_sort_headers(app);

    context_menu_init(app);
    install_key_handlers(app);

    winder_layout(app);
}
