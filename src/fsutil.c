/* Filesystem helpers for Winder */
#include "winder.h"

#include <fcntl.h>

char *fs_home_dir(void)
{
    const char *h = getenv("HOME");
    struct passwd *pw;

    if (h && h[0])
        return wstrdup(h);
    pw = getpwuid(getuid());
    if (pw && pw->pw_dir)
        return wstrdup(pw->pw_dir);
    return wstrdup("/");
}

char *fs_join_path(const char *dir, const char *name)
{
    size_t dl, nl;
    char *out;

    if (!dir || !name)
        return NULL;
    if (strcmp(dir, "/") == 0) {
        out = wmalloc(strlen(name) + 2);
        sprintf(out, "/%s", name);
        return out;
    }
    dl = strlen(dir);
    nl = strlen(name);
    out = wmalloc(dl + 1 + nl + 1);
    memcpy(out, dir, dl);
    out[dl] = '/';
    memcpy(out + dl + 1, name, nl + 1);
    return out;
}

char *fs_dirname(const char *path)
{
    char *copy, *slash;
    size_t n;

    if (!path || !*path)
        return wstrdup("/");
    copy = wstrdup(path);
    n = strlen(copy);
    while (n > 1 && copy[n - 1] == '/') {
        copy[--n] = '\0';
    }
    slash = strrchr(copy, '/');
    if (!slash) {
        wfree(copy);
        return wstrdup(".");
    }
    if (slash == copy) {
        wfree(copy);
        return wstrdup("/");
    }
    *slash = '\0';
    return copy;
}

char *fs_basename(const char *path)
{
    const char *slash;
    size_t n;
    char *tmp, *base;

    if (!path || !*path)
        return wstrdup("/");
    tmp = wstrdup(path);
    n = strlen(tmp);
    while (n > 1 && tmp[n - 1] == '/') {
        tmp[--n] = '\0';
    }
    slash = strrchr(tmp, '/');
    if (!slash)
        base = wstrdup(tmp);
    else if (slash[1] == '\0')
        base = wstrdup("/");
    else
        base = wstrdup(slash + 1);
    wfree(tmp);
    return base;
}

char *fs_normalize(const char *path)
{
    char resolved[PATH_MAX];
    char *home, *joined, *result;

    if (!path || !*path)
        return wstrdup("/");

    if (path[0] == '~') {
        home = fs_home_dir();
        if (path[1] == '\0' || path[1] == '/') {
            if (path[1] == '\0')
                joined = wstrdup(home);
            else
                joined = fs_join_path(home, path + 2);
            wfree(home);
            result = fs_normalize(joined);
            wfree(joined);
            return result;
        }
        wfree(home);
    }

    if (realpath(path, resolved))
        return wstrdup(resolved);

    /* realpath fails for non-existing paths; return cleaned copy */
    {
        char *out = wstrdup(path);
        size_t n = strlen(out);
        while (n > 1 && out[n - 1] == '/') {
            out[--n] = '\0';
        }
        return out;
    }
}

int fs_is_dir(const char *path)
{
    struct stat st;
    if (!path || stat(path, &st) != 0)
        return 0;
    return S_ISDIR(st.st_mode);
}

int fs_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0;
}

int fs_mkdir(const char *path)
{
    return mkdir(path, 0755);
}

int fs_remove_path(const char *path)
{
    struct stat st;

    if (!path || stat(path, &st) != 0)
        return -1;
    if (S_ISDIR(st.st_mode))
        return rmdir(path); /* non-recursive by design */
    return unlink(path);
}

int fs_rename_path(const char *from, const char *to)
{
    return rename(from, to);
}

int fs_run_command(char *const argv[])
{
    pid_t pid;
    int status;

    if (!argv || !argv[0])
        return -1;
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        /* child: quiet stdio for GUI helper tools */
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2)
                close(devnull);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

static int unique_path(const char *dir, const char *base, const char *suffix,
                       char *out, size_t out_len)
{
    char candidate[PATH_MAX];
    int n;

    if (strcmp(dir, "/") == 0)
        snprintf(candidate, sizeof(candidate), "/%s%s", base, suffix ? suffix : "");
    else
        snprintf(candidate, sizeof(candidate), "%s/%s%s", dir, base,
                 suffix ? suffix : "");
    if (!fs_exists(candidate)) {
        wstrlcpy(out, candidate, out_len);
        return 0;
    }
    for (n = 2; n < 1000; n++) {
        if (strcmp(dir, "/") == 0)
            snprintf(candidate, sizeof(candidate), "/%s %d%s", base, n,
                     suffix ? suffix : "");
        else
            snprintf(candidate, sizeof(candidate), "%s/%s %d%s", dir, base, n,
                     suffix ? suffix : "");
        if (!fs_exists(candidate)) {
            wstrlcpy(out, candidate, out_len);
            return 0;
        }
    }
    return -1;
}

int fs_duplicate(const char *path, char *out_path, size_t out_len)
{
    char *parent, *base, *dot;
    char stem[NAME_MAX + 1];
    char dest[PATH_MAX];
    char *argv[8];
    int rc;

    if (!path || !fs_exists(path))
        return -1;
    parent = fs_dirname(path);
    base = fs_basename(path);

    /* "file.txt" -> "file copy.txt"; dirs -> "name copy" */
    wstrlcpy(stem, base, sizeof(stem));
    if (!fs_is_dir(path) && (dot = strrchr(stem, '.')) && dot != stem) {
        char ext[64];
        wstrlcpy(ext, dot, sizeof(ext));
        *dot = '\0';
        {
            char with_copy[NAME_MAX + 16];
            snprintf(with_copy, sizeof(with_copy), "%s copy", stem);
            if (unique_path(parent, with_copy, ext, dest, sizeof(dest)) != 0) {
                wfree(parent);
                wfree(base);
                return -1;
            }
        }
    } else {
        char with_copy[NAME_MAX + 16];
        snprintf(with_copy, sizeof(with_copy), "%s copy", stem);
        if (unique_path(parent, with_copy, "", dest, sizeof(dest)) != 0) {
            wfree(parent);
            wfree(base);
            return -1;
        }
    }

    argv[0] = "cp";
    argv[1] = "-a";
    argv[2] = (char *)path;
    argv[3] = dest;
    argv[4] = NULL;
    rc = fs_run_command(argv);
    if (rc == 0 && out_path && out_len)
        wstrlcpy(out_path, dest, out_len);
    wfree(parent);
    wfree(base);
    return rc == 0 ? 0 : -1;
}

int fs_compress_tar_gz(const char *path, char *out_path, size_t out_len)
{
    char *parent, *base;
    char dest[PATH_MAX];
    char *argv[12];
    int rc;

    if (!path || !fs_exists(path))
        return -1;
    parent = fs_dirname(path);
    base = fs_basename(path);

    if (unique_path(parent, base, ".tar.gz", dest, sizeof(dest)) != 0) {
        wfree(parent);
        wfree(base);
        return -1;
    }

    /* tar -czf dest -C parent base */
    argv[0] = "tar";
    argv[1] = "-czf";
    argv[2] = dest;
    argv[3] = "-C";
    argv[4] = parent;
    argv[5] = base;
    argv[6] = NULL;
    rc = fs_run_command(argv);
    if (rc == 0 && out_path && out_len)
        wstrlcpy(out_path, dest, out_len);
    wfree(parent);
    wfree(base);
    return rc == 0 ? 0 : -1;
}

int fs_copy_into_dir(const char *src, const char *dest_dir, char *out_path,
                     size_t out_len)
{
    char *base;
    char dest[PATH_MAX];
    char *argv[8];
    int rc;

    if (!src || !dest_dir || !fs_exists(src) || !fs_is_dir(dest_dir))
        return -1;
    base = fs_basename(src);
    if (unique_path(dest_dir, base, "", dest, sizeof(dest)) != 0) {
        wfree(base);
        return -1;
    }
    argv[0] = "cp";
    argv[1] = "-a";
    argv[2] = (char *)src;
    argv[3] = dest;
    argv[4] = NULL;
    rc = fs_run_command(argv);
    if (rc == 0 && out_path && out_len)
        wstrlcpy(out_path, dest, out_len);
    wfree(base);
    return rc == 0 ? 0 : -1;
}

void fs_copy_path_to_clipboard(const char *path)
{
    char *argv[8];
    int pipes[2];
    pid_t pid;

    if (!path || !path[0])
        return;

    /* Prefer xclip, then xsel; ignore failure (path still in app clipboard). */
    if (pipe(pipes) == 0) {
        pid = fork();
        if (pid == 0) {
            const char *tools[][6] = {
                { "xclip", "-selection", "clipboard", NULL },
                { "xsel", "--clipboard", "--input", NULL },
                { NULL }
            };
            int t;
            close(pipes[1]);
            dup2(pipes[0], STDIN_FILENO);
            close(pipes[0]);
            for (t = 0; tools[t][0]; t++) {
                char *av[8];
                int i;
                for (i = 0; tools[t][i]; i++)
                    av[i] = (char *)tools[t][i];
                av[i] = NULL;
                execvp(av[0], av);
            }
            _exit(127);
        } else if (pid > 0) {
            ssize_t len = (ssize_t)strlen(path);
            close(pipes[0]);
            if (write(pipes[1], path, (size_t)len) != len) {
                /* ignore */
            }
            close(pipes[1]);
            waitpid(pid, NULL, 0);
            return;
        }
        close(pipes[0]);
        close(pipes[1]);
    }
    (void)argv;
}

int fs_open_terminal(const char *dir)
{
    char *argv[12];
    const char *term;
    pid_t pid;

    if (!dir || !fs_is_dir(dir))
        return -1;

    term = getenv("TERMINAL");
    if (!term || !term[0])
        term = getenv("TERMCMD");

    pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        if (chdir(dir) != 0)
            _exit(126);
        if (term && term[0]) {
            execlp(term, term, (char *)NULL);
        }
        /* common fallbacks */
        execlp("x-terminal-emulator", "x-terminal-emulator", (char *)NULL);
        execlp("xterm", "xterm", (char *)NULL);
        execlp("uxterm", "uxterm", (char *)NULL);
        execlp("kitty", "kitty", (char *)NULL);
        execlp("alacritty", "alacritty", (char *)NULL);
        _exit(127);
    }
    (void)argv;
    return 0;
}

void fs_format_size(off_t size, char *buf, size_t buflen)
{
    const char *units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = (double)size;
    int u = 0;

    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        u++;
    }
    if (u == 0)
        snprintf(buf, buflen, "%lld %s", (long long)size, units[u]);
    else if (v >= 100.0)
        snprintf(buf, buflen, "%.0f %s", v, units[u]);
    else if (v >= 10.0)
        snprintf(buf, buflen, "%.1f %s", v, units[u]);
    else
        snprintf(buf, buflen, "%.2f %s", v, units[u]);
}

void fs_format_time(time_t t, char *buf, size_t buflen)
{
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, buflen, "%Y-%m-%d %H:%M", &tm);
}

void fs_format_mode(mode_t mode, char *buf, size_t buflen)
{
    char t = '-';
    if (S_ISDIR(mode)) t = 'd';
    else if (S_ISLNK(mode)) t = 'l';
    else if (S_ISCHR(mode)) t = 'c';
    else if (S_ISBLK(mode)) t = 'b';
    else if (S_ISFIFO(mode)) t = 'p';
    else if (S_ISSOCK(mode)) t = 's';

    snprintf(buf, buflen, "%c%c%c%c%c%c%c%c%c%c",
             t,
             mode & S_IRUSR ? 'r' : '-',
             mode & S_IWUSR ? 'w' : '-',
             mode & S_IXUSR ? 'x' : '-',
             mode & S_IRGRP ? 'r' : '-',
             mode & S_IWGRP ? 'w' : '-',
             mode & S_IXGRP ? 'x' : '-',
             mode & S_IROTH ? 'r' : '-',
             mode & S_IWOTH ? 'w' : '-',
             mode & S_IXOTH ? 'x' : '-');
}

const char *fs_kind_for(const char *path, mode_t mode)
{
    const char *ext;

    if (S_ISDIR(mode)) return "Folder";
    if (S_ISLNK(mode)) return "Alias / Link";
    if (S_ISFIFO(mode)) return "FIFO";
    if (S_ISSOCK(mode)) return "Socket";
    if (S_ISCHR(mode) || S_ISBLK(mode)) return "Device";

    if (!path) return "Document";
    ext = strrchr(path, '.');
    if (!ext || ext == path) return "Document";
    ext++;
    if (!strcasecmp(ext, "png") || !strcasecmp(ext, "jpg") ||
        !strcasecmp(ext, "jpeg") || !strcasecmp(ext, "gif") ||
        !strcasecmp(ext, "webp") || !strcasecmp(ext, "bmp") ||
        !strcasecmp(ext, "tiff") || !strcasecmp(ext, "tif") ||
        !strcasecmp(ext, "svg") || !strcasecmp(ext, "xpm") ||
        !strcasecmp(ext, "ico"))
        return "Image";
    if (!strcasecmp(ext, "mp3") || !strcasecmp(ext, "ogg") ||
        !strcasecmp(ext, "flac") || !strcasecmp(ext, "wav") ||
        !strcasecmp(ext, "m4a") || !strcasecmp(ext, "aac"))
        return "Audio";
    if (!strcasecmp(ext, "mp4") || !strcasecmp(ext, "mkv") ||
        !strcasecmp(ext, "avi") || !strcasecmp(ext, "webm") ||
        !strcasecmp(ext, "mov"))
        return "Video";
    if (!strcasecmp(ext, "pdf")) return "PDF Document";
    if (!strcasecmp(ext, "txt") || !strcasecmp(ext, "md") ||
        !strcasecmp(ext, "rst") || !strcasecmp(ext, "log"))
        return "Plain Text";
    if (!strcasecmp(ext, "c") || !strcasecmp(ext, "h") ||
        !strcasecmp(ext, "cc") || !strcasecmp(ext, "cpp") ||
        !strcasecmp(ext, "py") || !strcasecmp(ext, "rs") ||
        !strcasecmp(ext, "go") || !strcasecmp(ext, "js") ||
        !strcasecmp(ext, "ts") || !strcasecmp(ext, "sh"))
        return "Source Code";
    if (!strcasecmp(ext, "zip") || !strcasecmp(ext, "tar") ||
        !strcasecmp(ext, "gz") || !strcasecmp(ext, "bz2") ||
        !strcasecmp(ext, "xz") || !strcasecmp(ext, "7z") ||
        !strcasecmp(ext, "tgz") || !strcasecmp(ext, "rar"))
        return "Archive";
    if (!strcasecmp(ext, "html") || !strcasecmp(ext, "htm"))
        return "Web Page";
    return "Document";
}

static const char *extension_of(const char *path)
{
    const char *base, *ext;

    if (!path)
        return NULL;
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    ext = strrchr(base, '.');
    if (!ext || ext == base || ext[1] == '\0')
        return NULL;
    return ext + 1;
}

static int ext_in_list(const char *ext, const char *const *list)
{
    int i;
    if (!ext)
        return 0;
    for (i = 0; list[i]; i++) {
        if (!strcasecmp(ext, list[i]))
            return 1;
    }
    return 0;
}

static int looks_like_text_bytes(const unsigned char *buf, size_t n)
{
    size_t i, weird = 0;

    if (n == 0)
        return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = buf[i];
        if (c == 0)
            return 0;
        if (c == '\n' || c == '\r' || c == '\t' || c == '\f')
            continue;
        if (c < 32 || c == 127)
            weird++;
    }
    /* allow a few control bytes; reject if mostly binary */
    return weird * 10 <= n;
}

static int is_image_ext(const char *ext)
{
    static const char *const imgs[] = {
        "png", "jpg", "jpeg", "gif", "bmp", "tif", "tiff",
        "xpm", "xbm", "ppm", "pgm", "pbm", "tga", "ico",
        NULL
    };
    return ext_in_list(ext, imgs);
}

static int is_text_ext(const char *ext)
{
    static const char *const texts[] = {
        "txt", "text", "md", "markdown", "rst", "log", "csv", "tsv",
        "c", "h", "cc", "hh", "cpp", "cxx", "hpp", "m", "mm",
        "py", "rb", "pl", "pm", "php", "js", "ts", "jsx", "tsx",
        "java", "go", "rs", "swift", "kt", "kts", "scala",
        "sh", "bash", "zsh", "fish", "csh", "bat", "ps1",
        "html", "htm", "xml", "xhtml", "css", "scss", "less",
        "json", "yml", "yaml", "toml", "ini", "cfg", "conf",
        "desktop", "service", "mount", "timer",
        "makefile", "cmake", "am", "ac", "m4",
        "diff", "patch", "sql", "r", "lua", "vim", "el",
        "tex", "bib", "org", "adoc", "asciidoc",
        NULL
    };
    return ext_in_list(ext, texts);
}

PreviewKind fs_preview_kind(const char *path, mode_t mode)
{
    const char *ext;
    FILE *fp;
    unsigned char buf[512];
    size_t n;

    if (!path)
        return PREV_NONE;
    if (S_ISDIR(mode))
        return PREV_FOLDER;
    if (!S_ISREG(mode))
        return PREV_PLACEHOLDER;

    ext = extension_of(path);
    if (is_image_ext(ext))
        return PREV_IMAGE;
    if (is_text_ext(ext))
        return PREV_TEXT;

    /* special basenames without extensions */
    {
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (!strcasecmp(base, "Makefile") || !strcasecmp(base, "Dockerfile") ||
            !strcasecmp(base, "README") || !strcasecmp(base, "LICENSE") ||
            !strcasecmp(base, "COPYING") || !strcasecmp(base, "CHANGELOG") ||
            !strcasecmp(base, "CMakeLists.txt") || !strcasecmp(base, "meson.build"))
            return PREV_TEXT;
    }

    fp = fopen(path, "rb");
    if (!fp)
        return PREV_PLACEHOLDER;
    n = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);

    if (looks_like_text_bytes(buf, n))
        return PREV_TEXT;

    return PREV_PLACEHOLDER;
}

char *fs_read_text_preview(const char *path, size_t max_bytes)
{
    FILE *fp;
    char *buf;
    size_t n, i;
    long fsz;

    if (!path || max_bytes < 64)
        return NULL;
    fp = fopen(path, "rb");
    if (!fp)
        return NULL;

    if (fseek(fp, 0, SEEK_END) == 0) {
        fsz = ftell(fp);
        if (fsz < 0)
            fsz = 0;
        rewind(fp);
    } else {
        fsz = 0;
    }

    buf = wmalloc(max_bytes + 64);
    n = fread(buf, 1, max_bytes, fp);
    fclose(fp);
    if (n == 0) {
        wfree(buf);
        return wstrdup("(empty file)");
    }

    /* scrub NULs / bad control chars for WMText */
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == 0)
            buf[i] = ' ';
        else if (c < 32 && c != '\n' && c != '\r' && c != '\t')
            buf[i] = '?';
    }
    buf[n] = '\0';

    if ((size_t)fsz > n || n >= max_bytes) {
        snprintf(buf + n, 64, "\n\n… (preview truncated)");
    }
    return buf;
}

char *fs_folder_listing_preview(const char *path, int show_hidden, int max_entries)
{
    DIR *dir;
    struct dirent *de;
    struct stat st;
    char full[PATH_MAX];
    char *out;
    size_t cap = 4096, len = 0;
    int count = 0, total = 0;
    char line[NAME_MAX + 8];

    if (!path)
        return NULL;
    dir = opendir(path);
    if (!dir)
        return wstrdup("(cannot read folder)");

    out = wmalloc(cap);
    out[0] = '\0';

    while ((de = readdir(dir))) {
        int is_dir;

        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (!show_hidden && de->d_name[0] == '.')
            continue;
        total++;
        if (count >= max_entries)
            continue;

        if (strcmp(path, "/") == 0)
            snprintf(full, sizeof(full), "/%s", de->d_name);
        else
            snprintf(full, sizeof(full), "%s/%s", path, de->d_name);

        is_dir = 0;
        if (lstat(full, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            if (!is_dir && S_ISLNK(st.st_mode)) {
                struct stat st2;
                if (stat(full, &st2) == 0 && S_ISDIR(st2.st_mode))
                    is_dir = 1;
            }
        }

        snprintf(line, sizeof(line), "%s %s\n", is_dir ? "▸" : " ", de->d_name);
        {
            size_t ll = strlen(line);
            if (len + ll + 64 >= cap) {
                cap *= 2;
                out = wrealloc(out, cap);
            }
            memcpy(out + len, line, ll + 1);
            len += ll;
        }
        count++;
    }
    closedir(dir);

    if (total == 0) {
        wfree(out);
        return wstrdup("(empty folder)");
    }
    if (total > count) {
        char more[64];
        size_t ll;
        snprintf(more, sizeof(more), "\n… %d more items", total - count);
        ll = strlen(more);
        if (len + ll + 1 >= cap) {
            cap = len + ll + 1;
            out = wrealloc(out, cap);
        }
        memcpy(out + len, more, ll + 1);
    }
    return out;
}

const char *fs_placeholder_label(const char *path, mode_t mode)
{
    const char *kind = fs_kind_for(path, mode);
    if (!kind)
        return "File";
    if (!strcmp(kind, "Audio")) return "Audio";
    if (!strcmp(kind, "Video")) return "Video";
    if (!strcmp(kind, "Archive")) return "Archive";
    if (!strcmp(kind, "PDF Document")) return "PDF";
    if (!strcmp(kind, "Device")) return "Device";
    if (!strcmp(kind, "FIFO") || !strcmp(kind, "Socket")) return "Special";
    return "Document";
}

int fs_open_with_default(const char *path)
{
    pid_t pid;
    const char *helpers[] = { "xdg-open", "mimeopen", "exo-open", NULL };
    int i;

    if (!path)
        return -1;

    for (i = 0; helpers[i]; i++) {
        pid = fork();
        if (pid < 0)
            return -1;
        if (pid == 0) {
            /* child: detach from terminal noise */
            setsid();
            execlp(helpers[i], helpers[i], path, (char *)NULL);
            _exit(127);
        }
        /* parent: if helper missing, child exits 127 quickly — try next */
        {
            int status = 0;
            /* non-blocking: give it a brief moment only for missing binary */
            usleep(30000);
            if (waitpid(pid, &status, WNOHANG) > 0) {
                if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
                    continue; /* try next helper */
                return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            }
            /* still running — success enough */
            return 0;
        }
    }
    return -1;
}

off_t fs_dir_entry_count(const char *path, int show_hidden, int *folders, int *files)
{
    DIR *dir;
    struct dirent *de;
    struct stat st;
    char full[PATH_MAX];
    int nf = 0, nd = 0;
    off_t total = 0;

    if (folders) *folders = 0;
    if (files) *files = 0;

    dir = opendir(path);
    if (!dir)
        return 0;

    while ((de = readdir(dir))) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (!show_hidden && de->d_name[0] == '.')
            continue;
        if (snprintf(full, sizeof(full), "%s/%s",
                     strcmp(path, "/") == 0 ? "" : path,
                     de->d_name) >= (int)sizeof(full))
            continue;
        if (lstat(full, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            nd++;
        else {
            nf++;
            total += st.st_size;
        }
    }
    closedir(dir);
    if (folders) *folders = nd;
    if (files) *files = nf;
    return total;
}
