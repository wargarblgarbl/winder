/* Winder — native WINGs file manager (Finder-inspired)
 *
 * Copyright (c) 2026
 * Licensed under the GNU GPL version 2 or later.
 */
#ifndef WINDER_H
#define WINDER_H

#include <WINGs/WINGs.h>
#include <WINGs/WUtil.h>
#include <wraster.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define WINDER_APP_NAME     "Winder"
#define WINDER_WIN_NAME     "winder"
#define WINDER_VERSION      "0.1.0"

#define WIN_W               960
#define WIN_H               600
#define WIN_MIN_W           640
#define WIN_MIN_H           420

#define TOOLBAR_H           36
#define PATHBAR_H           30
#define STATUS_H            24
#define LIST_HEADER_H       24
#define SIDEBAR_W           160
#define PREVIEW_W           240
#define PREVIEW_MEDIA_H     150
#define PREVIEW_TEXT_MAX    8192
#define PREVIEW_FOLDER_MAX  40
#define MARGIN              6
#define BTN_H               26
#define BTN_W               72
#define BTN_SM              28

#define COL_SIZE_W          90
#define COL_KIND_W          110
#define COL_DATE_W          140
/* WINGs list places its scroller on the left; content starts after this */
#define LIST_SCROLLER_W     19
#define LIST_COL_PAD        6

#define HISTORY_MAX         64
#define SIDEBAR_MAX         24
#define CTX_MENU_MAX_ITEMS  16
#define CTX_MENU_ITEM_H     24
#define CTX_MENU_W          180

typedef enum {
    VIEW_COLUMNS = 0,
    VIEW_LIST    = 1
} WinderViewMode;

typedef enum {
    SORT_NAME = 0,
    SORT_SIZE = 1,
    SORT_KIND = 2,
    SORT_DATE = 3
} SortColumn;

/* What the Get Info pane should render for a path */
typedef enum {
    PREV_NONE = 0,
    PREV_FOLDER,
    PREV_TEXT,
    PREV_IMAGE,
    PREV_PLACEHOLDER
} PreviewKind;

/* metadata hung on list-view items (clientData) */
typedef struct {
    char      *name;
    off_t      size;
    time_t     mtime;
    int        is_dir;
    const char *kind;   /* static string from fs_kind_for */
} FileEntry;

typedef struct {
    char *label;        /* display name in sidebar */
    char *path;         /* absolute path */
    int    is_separator;
} SidebarEntry;

typedef struct {
    char *stack[HISTORY_MAX];
    int   count;
    int   index;        /* current position in stack */
} NavHistory;

typedef struct WinderApp {
    Display   *dpy;
    WMScreen  *scr;
    WMWindow  *win;

    /* chrome */
    WMFrame   *toolbar;
    WMButton  *btnBack;
    WMButton  *btnForward;
    WMButton  *btnUp;
    WMButton  *btnHome;
    WMButton  *btnColumns;
    WMButton  *btnList;
    WMButton  *btnNewFolder;
    WMButton  *btnDelete;
    WMButton  *btnRename;
    WMButton  *btnOpen;
    WMButton  *btnRefresh;
    WMButton  *btnGo;
    WMButton  *btnHidden;

    WMFrame   *pathBar;
    WMLabel   *pathLabel;
    WMTextField *pathField;
    WMTextField *filterField;
    WMLabel   *filterLabel;

    /* body */
    WMFrame   *sidebarFrame;
    WMLabel   *sidebarTitle;
    WMList    *sidebar;

    WMBrowser *browser;         /* column view */
    Bool       browserLoaded;

    /* list view: pane holds header + list */
    WMFrame   *listPane;
    WMFrame   *listHeader;
    WMButton  *btnSortName;
    WMButton  *btnSortSize;
    WMButton  *btnSortKind;
    WMButton  *btnSortDate;
    WMList    *listView;
    WMFont    *listFont;
    /* pixel widths of the four columns (content area, after scroller) */
    int        colNameW;
    int        colSizeW;
    int        colKindW;
    int        colDateW;

    WMFrame   *previewFrame;
    WMLabel   *previewTitle;
    WMLabel   *previewName;
    WMFrame   *previewBox;      /* media / text preview area */
    WMLabel   *previewImage;    /* image thumb or icon placeholder */
    WMText    *previewBody;     /* text file or folder listing */
    WMLabel   *previewKind;
    WMLabel   *previewSize;
    WMLabel   *previewDate;
    WMLabel   *previewPerms;
    WMLabel   *previewPath;
    WMPixmap  *previewPixmap;   /* retained image currently shown */

    WMFrame   *statusBar;
    WMLabel   *statusLabel;

    /* right-click context menu (borderless popup) */
    WMWindow  *ctxMenu;
    WMFrame   *ctxFrame;
    WMButton  *ctxButtons[CTX_MENU_MAX_ITEMS];
    int        ctxButtonCount;
    char       ctxPath[PATH_MAX];   /* target of the menu, or "" for background */
    int        ctxIsBackground;     /* 1 = empty area / current folder */
    int        ctxVisible;

    /* simple in-app clipboard for copy/paste of paths */
    char       clipboardPath[PATH_MAX];

    /* state */
    char           currentPath[PATH_MAX];
    char           filter[256];
    char           lastPreviewPath[PATH_MAX];
    WinderViewMode viewMode;
    SortColumn     sortColumn;
    Bool           sortAscending;
    Bool           showHidden;
    Bool           showPreview;
    Bool           suppressHistory; /* don't push while restoring history */

    NavHistory     history;

    SidebarEntry   favorites[SIDEBAR_MAX];
    int            favoriteCount;

    WMBrowserDelegate browserDelegate;
} WinderApp;

/* ---------- fsutil.c ---------- */
char *fs_home_dir(void);
char *fs_join_path(const char *dir, const char *name);
char *fs_dirname(const char *path);
char *fs_basename(const char *path);
char *fs_normalize(const char *path);
int   fs_is_dir(const char *path);
int   fs_exists(const char *path);
int   fs_mkdir(const char *path);
int   fs_remove_path(const char *path);
int   fs_rename_path(const char *from, const char *to);
int   fs_duplicate(const char *path, char *out_path, size_t out_len);
int   fs_compress_tar_gz(const char *path, char *out_path, size_t out_len);
int   fs_copy_into_dir(const char *src, const char *dest_dir, char *out_path,
                       size_t out_len);
int   fs_run_command(char *const argv[]);
void  fs_copy_path_to_clipboard(const char *path);
int   fs_open_terminal(const char *dir);
void  fs_format_size(off_t size, char *buf, size_t buflen);
void  fs_format_time(time_t t, char *buf, size_t buflen);
void  fs_format_mode(mode_t mode, char *buf, size_t buflen);
const char *fs_kind_for(const char *path, mode_t mode);
int   fs_open_with_default(const char *path);
off_t fs_dir_entry_count(const char *path, int show_hidden, int *folders, int *files);

PreviewKind fs_preview_kind(const char *path, mode_t mode);
/* caller frees with wfree */
char *fs_read_text_preview(const char *path, size_t max_bytes);
/* caller frees with wfree; lists up to max_entries names */
char *fs_folder_listing_preview(const char *path, int show_hidden, int max_entries);
const char *fs_placeholder_label(const char *path, mode_t mode);

/* ---------- history.c ---------- */
void hist_init(NavHistory *h);
void hist_free(NavHistory *h);
void hist_push(NavHistory *h, const char *path);
const char *hist_back(NavHistory *h);
const char *hist_forward(NavHistory *h);
int  hist_can_back(const NavHistory *h);
int  hist_can_forward(const NavHistory *h);

/* ---------- ui.c / actions (in main modules) ---------- */
void winder_build_ui(WinderApp *app);
void winder_layout(WinderApp *app);
void winder_set_path(WinderApp *app, const char *path, int push_history);
void winder_refresh(WinderApp *app);
void winder_set_view_mode(WinderApp *app, WinderViewMode mode);
void winder_update_status(WinderApp *app);
void winder_update_preview(WinderApp *app, const char *path);
void winder_update_nav_buttons(WinderApp *app);
char *winder_selected_path(WinderApp *app);
char *winder_selected_directory(WinderApp *app);

void action_back(WMWidget *self, void *data);
void action_forward(WMWidget *self, void *data);
void action_up(WMWidget *self, void *data);
void action_home(WMWidget *self, void *data);
void action_go(WMWidget *self, void *data);
void action_refresh(WMWidget *self, void *data);
void action_new_folder(WMWidget *self, void *data);
void action_delete(WMWidget *self, void *data);
void action_rename(WMWidget *self, void *data);
void action_open(WMWidget *self, void *data);
void action_view_columns(WMWidget *self, void *data);
void action_view_list(WMWidget *self, void *data);
void action_toggle_hidden(WMWidget *self, void *data);
void action_duplicate(WMWidget *self, void *data);
void action_compress(WMWidget *self, void *data);
void action_copy_path(WMWidget *self, void *data);
void action_copy_item(WMWidget *self, void *data);
void action_paste_item(WMWidget *self, void *data);
void action_terminal(WMWidget *self, void *data);

/* context menu */
void context_menu_init(WinderApp *app);
void context_menu_show(WinderApp *app, int root_x, int root_y,
                       const char *path, int is_background);
void context_menu_hide(WinderApp *app);
void context_menu_attach_list(WinderApp *app, WMList *list);

void sidebar_select(WMWidget *self, void *data);
void browser_click(WMWidget *self, void *data);
void browser_dclick(WMWidget *self, void *data);
void list_click(WMWidget *self, void *data);
void list_dclick(WMWidget *self, void *data);
void action_sort_name(WMWidget *self, void *data);
void action_sort_size(WMWidget *self, void *data);
void action_sort_kind(WMWidget *self, void *data);
void action_sort_date(WMWidget *self, void *data);
void path_field_action(void *observer, WMNotification *notif);
void filter_field_action(void *observer, WMNotification *notif);
void fill_browser_column(WMBrowserDelegate *self, WMBrowser *bPtr,
                         int column, WMList *list);
void populate_list_view(WinderApp *app);
void populate_sidebar(WinderApp *app);
void list_update_sort_headers(WinderApp *app);
void browser_ensure_loaded(WinderApp *app);

void close_app(WMWidget *self, void *data);

void file_entry_free(FileEntry *fe);

#endif /* WINDER_H */
