/*
 * sidebar_data.h — Pure-logic sidebar data computation.
 *
 * No GTK dependencies. Used by workspace.c (production) and
 * test_sidebar_data.c (tests) so the same code paths are
 * exercised in both builds.
 */
#pragma once

#include <glib.h>

char       *sidebar_data_format_status(int pane_count, int tab_count);
const char *sidebar_data_resolve_branch(const char *primary_branch,
                                        const char *fallback_branch,
                                        gboolean    has_first_terminal);
const char *sidebar_data_resolve_cwd(const char *first_terminal_cwd,
                                     const char *fallback_cwd);
