# Winder

**Winder** is a native file manager for [Window Maker](https://windowmaker.org/) built with **C** and **WINGs**.

It follows the layout of a modern Apple Finder, on top of the classic NeXT / Window Maker look:

| Area | Role |
|------|------|
| **Toolbar** | Back, Forward, Up, Home · Columns / List · New, Delete, Rename, Open · Reload · Hidden |
| **Path bar** | Editable path + **Go** · **Find** filter (list view) |
| **Sidebar** | Favorites: Home, Desktop, Documents, Downloads, media, … |
| **Columns** | NeXT-style multi-column browser (default) |
| **List** | Name · size · kind · modified (click headers to sort) |
| **Get Info** | Preview panel: text, images, folder listing, metadata |
| **Context menu** | Right-click: Open, Rename, Duplicate, Delete, Compress, Copy / Paste, Copy Path, Terminal… |
| **Status** | Folder / file counts and size on disk |

Version **0.1.0**.

## Build

Needs: `gcc`, X11, WINGs (`libWINGs`, `libwraster`, `libWUtil`).

```sh
make
./winder
# or
./winder ~/Documents
```

Options:

```sh
winder --help      # usage
winder --version   # print version
```

## Install

GNU-style paths. Default prefix is `/usr/local`.

```sh
make
sudo make install              # bin, man, .desktop, docs
sudo make install-strip        # same, stripped binary
sudo make PREFIX=/usr install  # FHS under /usr
make DESTDIR=/tmp/stage PREFIX=/usr install   # staged package root
sudo make uninstall
make print-paths               # show where files will go
```

| Path | File |
|------|------|
| `$(PREFIX)/bin/winder` | binary |
| `$(PREFIX)/share/man/man1/winder.1` | man page |
| `$(PREFIX)/share/applications/winder.desktop` | desktop entry |
| `$(PREFIX)/share/doc/winder/README.md` | docs |
| `$(PREFIX)/share/doc/winder/LICENSE` | license |

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
| Sort | In **List**, click a column header (Name, Size, Kind, Date) |
| Context menu | Right-click a file, folder, or empty area |
| Copy / Paste | Context menu **Copy** then **Paste** into a folder |
| Compress | Context menu **Compress…** (creates a `.tar.gz`) |
| Terminal | Context menu **Open Terminal Here** |

## Keyboard

When focus is on a browse surface (file list, column browser, or sidebar), not the path or Find fields:

| Key | Action |
|-----|--------|
| Up / Down | Move the selection |
| Left / Right | Columns: parent column or open folder. List: go back or open |
| Enter | Open the selection |
| `/` | Focus the Find field |
| Escape | Return focus to the file view |

## Window Maker tip

From a terminal:

```sh
winder &
```

Or put it on the dock / root menu as a plain application. A desktop entry is installed as `winder.desktop`.

## Layout of the tree

```
winder/
├── Makefile
├── README.md
├── LICENSE
├── data/
│   └── winder.desktop
├── man/
│   └── winder.1
├── winder          # binary after make
└── src/
    ├── winder.h    # shared types & API
    ├── main.c      # entry
    ├── ui.c        # window, layout, views
    ├── actions.c   # toolbar & callbacks
    ├── fsutil.c    # paths, open, format
    ├── history.c   # back / forward stack
    └── menu.c      # right-click context menu
```

## Author

Dobroslav Slavenskoj

## License

MIT License. See [LICENSE](LICENSE).
