/* Navigation history (Finder-style back/forward) */
#include "winder.h"

void hist_init(NavHistory *h)
{
    int i;
    for (i = 0; i < HISTORY_MAX; i++)
        h->stack[i] = NULL;
    h->count = 0;
    h->index = -1;
}

void hist_free(NavHistory *h)
{
    int i;
    for (i = 0; i < HISTORY_MAX; i++) {
        if (h->stack[i]) {
            wfree(h->stack[i]);
            h->stack[i] = NULL;
        }
    }
    h->count = 0;
    h->index = -1;
}

void hist_push(NavHistory *h, const char *path)
{
    int i;

    if (!path || !*path)
        return;

    /* drop any forward entries */
    for (i = h->index + 1; i < h->count; i++) {
        if (h->stack[i]) {
            wfree(h->stack[i]);
            h->stack[i] = NULL;
        }
    }
    h->count = h->index + 1;

    /* skip duplicate of current */
    if (h->index >= 0 && h->stack[h->index] &&
        strcmp(h->stack[h->index], path) == 0)
        return;

    if (h->count >= HISTORY_MAX) {
        wfree(h->stack[0]);
        memmove(&h->stack[0], &h->stack[1],
                sizeof(char *) * (HISTORY_MAX - 1));
        h->stack[HISTORY_MAX - 1] = NULL;
        h->count = HISTORY_MAX - 1;
        h->index = h->count - 1;
    }

    h->stack[h->count] = wstrdup(path);
    h->index = h->count;
    h->count++;
}

const char *hist_back(NavHistory *h)
{
    if (!hist_can_back(h))
        return NULL;
    h->index--;
    return h->stack[h->index];
}

const char *hist_forward(NavHistory *h)
{
    if (!hist_can_forward(h))
        return NULL;
    h->index++;
    return h->stack[h->index];
}

int hist_can_back(const NavHistory *h)
{
    return h->index > 0;
}

int hist_can_forward(const NavHistory *h)
{
    return h->index >= 0 && h->index < h->count - 1;
}
