/*
 * sidebar_data.c — Pure-logic sidebar data computation.
 *
 * These functions contain the actual decision logic for sidebar
 * summaries.  workspace.c wraps them with GTK widget extraction;
 * test_sidebar_data.c calls them directly.
 */
#include "sidebar_data.h"

char *
sidebar_data_format_status(int pane_count, int tab_count)
{
    if (pane_count <= 1 && tab_count <= 1)
        return g_strdup("");
    if (pane_count <= 1)
        return g_strdup_printf("%d tabs", tab_count);
    return g_strdup_printf("%d panes \302\267 %d tabs", pane_count, tab_count);
}

const char *
sidebar_data_resolve_branch(const char *primary_branch,
                            const char *fallback_branch,
                            gboolean    has_first_terminal)
{
    if (primary_branch && primary_branch[0])
        return primary_branch;
    if (has_first_terminal)
        return NULL;
    return (fallback_branch && fallback_branch[0]) ? fallback_branch : NULL;
}

const char *
sidebar_data_resolve_cwd(const char *first_terminal_cwd,
                         const char *fallback_cwd)
{
    if (first_terminal_cwd && first_terminal_cwd[0])
        return first_terminal_cwd;
    return (fallback_cwd && fallback_cwd[0]) ? fallback_cwd : NULL;
}
