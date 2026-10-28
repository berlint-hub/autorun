/* Shared run-report naming: the launcher finds these reports by the same
 * safe executable basename the runtime uses for its per-run log files. */
#ifndef WINE_NX_LAUNCHER_RUN_REPORT_H
#define WINE_NX_LAUNCHER_RUN_REPORT_H

#include <stddef.h>
#include <string.h>
#include <strings.h>

static inline void launcher_run_report_name( const char *path, char *name, size_t size )
{
    const char *base = path, *p;
    size_t i = 0;

    if (!size) return;
    for (p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    while (base[i] && i + 1 < size)
    {
        unsigned char c = (unsigned char)base[i];
        name[i] = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' ? c : '-';
        i++;
    }
    name[i] = 0;
    if (i > 4 && !strcasecmp( name + i - 4, ".exe" )) name[i - 4] = 0;
}

#endif
