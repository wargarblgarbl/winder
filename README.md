# Winder

**Winder** is a native file manager for [Window Maker](https://windowmaker.org/) built with **C** and **WINGs**.

It follows the layout of a modern Apple Finder, on top of the classic NeXT / Window Maker look:

| Area | Role |
|------|------|
| **Toolbar** | Back, Forward, Up, Home · Columns / List · New, Delete, Rename, Open · Reload · Hidden |
| **Path bar** | Editable path + **Go** · **Find** filter (list view) |
| **Sidebar** | Favorites: Home, Desktop, Documents, Downloads, media, … |
| **Columns** | NeXT-style multi-column browser (default) |
| **List** | Name · size · kind · modified |
| **Get Info** | Preview panel for the selection |
| **Status** | Folder / file counts and size on disk |

## Build

Needs: `gcc`, X11, WINGs (`libWINGs`, `libwraster`, `libWUtil`).

```sh
make
./winder
# or
./winder ~/Documents
```

Install (optional):

```sh
sudo make install          # → /usr/local/bin/winder
sudo make PREFIX=/usr install
```

## Use

| Action | How |
|--------|-----|
| Browse | Click folders in **Columns**, or open from **List** |
| Open file | Double-click or **Open** (uses `xdg-open`) |
| Go to path | Edit the path field and press Return / **Go** |
| New folder | **New…** |
| Rename / Delete | Select an item, then **Rename** or **Delete** |
| Hidden files | Toggle **Hidden** |
| Filter | Switch to **List**, type in **Find** |

## Window Maker tip

From a terminal:

```sh
winder &
```

Or put it on the dock / root menu as a plain application.

## Layout of the tree

```
winder/
├── Makefile
├── README.md
├── winder          # binary after make
└── src/
    ├── winder.h    # shared types & API
    ├── main.c      # entry
    ├── ui.c        # window, layout, views
    ├── actions.c   # toolbar & callbacks
    ├── fsutil.c    # paths, open, format
    └── history.c   # back / forward stack
```

## License

GPL-2.0-or-later (same family as Window Maker / WINGs).
