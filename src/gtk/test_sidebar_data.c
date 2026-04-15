/*
 * test_sidebar_data.c — tests for the production sidebar_data helpers.
 *
 * Calls the real sidebar_data_format_status, sidebar_data_resolve_branch,
 * and sidebar_data_resolve_cwd from sidebar_data.c.  No GTK needed.
 */
#include "sidebar_data.h"
#include <string.h>

/* ── sidebar_data_format_status ──────────────────────────────────── */

static void
test_format_status_null_like(void)
{
    g_autofree char *s = sidebar_data_format_status(0, 0);
    g_assert_cmpstr(s, ==, "");
}

static void
test_format_status_single_pane_single_tab(void)
{
    g_autofree char *s = sidebar_data_format_status(1, 1);
    g_assert_cmpstr(s, ==, "");
}

static void
test_format_status_single_pane_multi_tab(void)
{
    g_autofree char *s = sidebar_data_format_status(1, 3);
    g_assert_cmpstr(s, ==, "3 tabs");
}

static void
test_format_status_multi_pane(void)
{
    g_autofree char *s = sidebar_data_format_status(2, 5);
    g_assert_nonnull(strstr(s, "2 panes"));
    g_assert_nonnull(strstr(s, "5 tabs"));
}

static void
test_format_status_zero_panes_multi_tab(void)
{
    g_autofree char *s = sidebar_data_format_status(0, 4);
    g_assert_cmpstr(s, ==, "4 tabs");
}

/* ── sidebar_data_resolve_branch ─────────────────────────────────── */

static void
test_resolve_branch_primary_wins(void)
{
    const char *b = sidebar_data_resolve_branch("main", "develop", FALSE);
    g_assert_cmpstr(b, ==, "main");

    b = sidebar_data_resolve_branch("main", "develop", TRUE);
    g_assert_cmpstr(b, ==, "main");
}

static void
test_resolve_branch_fallback_no_terminal(void)
{
    const char *b = sidebar_data_resolve_branch("", "develop", FALSE);
    g_assert_cmpstr(b, ==, "develop");

    b = sidebar_data_resolve_branch(NULL, "develop", FALSE);
    g_assert_cmpstr(b, ==, "develop");
}

static void
test_resolve_branch_suppressed_with_terminal(void)
{
    const char *b = sidebar_data_resolve_branch("", "develop", TRUE);
    g_assert_null(b);

    b = sidebar_data_resolve_branch(NULL, "develop", TRUE);
    g_assert_null(b);
}

static void
test_resolve_branch_all_empty(void)
{
    g_assert_null(sidebar_data_resolve_branch("", "", FALSE));
    g_assert_null(sidebar_data_resolve_branch(NULL, NULL, FALSE));
    g_assert_null(sidebar_data_resolve_branch("", "", TRUE));
}

/* ── sidebar_data_resolve_cwd ────────────────────────────────────── */

static void
test_resolve_cwd_terminal_wins(void)
{
    const char *c = sidebar_data_resolve_cwd("/new/path", "/old/path");
    g_assert_cmpstr(c, ==, "/new/path");
}

static void
test_resolve_cwd_fallback(void)
{
    const char *c = sidebar_data_resolve_cwd(NULL, "/workspace/path");
    g_assert_cmpstr(c, ==, "/workspace/path");

    c = sidebar_data_resolve_cwd("", "/workspace/path");
    g_assert_cmpstr(c, ==, "/workspace/path");
}

static void
test_resolve_cwd_both_empty(void)
{
    g_assert_null(sidebar_data_resolve_cwd(NULL, NULL));
    g_assert_null(sidebar_data_resolve_cwd("", ""));
    g_assert_null(sidebar_data_resolve_cwd(NULL, ""));
    g_assert_null(sidebar_data_resolve_cwd("", NULL));
}

/* ── Stability contract tests ───────────────────────────────────── */
/*
 * These tests verify the data-layer contracts that workspace.c relies on
 * to implement the first-tab/first-pane primary-path stability rule:
 *
 * - workspace_get_sidebar_primary_cwd: when first terminal exists but has
 *   empty CWD, workspace.c returns NULL directly (does NOT call resolve_cwd
 *   with ws->cwd as fallback, preventing drift from non-primary terminals).
 *
 * - workspace_get_sidebar_primary_branch: when first terminal exists but
 *   primary branch is empty, resolve_branch suppresses the fallback (returns
 *   NULL) to prevent showing a stale branch from a different terminal.
 */

static void
test_resolve_branch_suppression_prevents_drift(void)
{
    /* Scenario: first terminal exists in the workspace but its git-branch
     * async hasn't completed yet.  ws->git_branch has a stale value "old"
     * from a previously-focused non-primary terminal.  The sidebar must NOT
     * show "old" — it should show nothing until the primary branch resolves. */
    const char *b = sidebar_data_resolve_branch("", "old-stale-branch", TRUE);
    g_assert_null(b);

    b = sidebar_data_resolve_branch(NULL, "old-stale-branch", TRUE);
    g_assert_null(b);
}

static void
test_resolve_cwd_fallback_only_without_terminal(void)
{
    /* The workspace layer only passes a fallback CWD when the first terminal
     * is unavailable.  This test documents that resolve_cwd uses the fallback
     * when first_terminal_cwd is absent, which is the correct path for a
     * workspace with no terminals (e.g., during init). */
    const char *c = sidebar_data_resolve_cwd(NULL, "/init/path");
    g_assert_cmpstr(c, ==, "/init/path");

    /* When first terminal exists with real CWD, it wins over any fallback. */
    c = sidebar_data_resolve_cwd("/real/path", "/init/path");
    g_assert_cmpstr(c, ==, "/real/path");
}

static void
test_format_status_large_counts(void)
{
    g_autofree char *s = sidebar_data_format_status(10, 50);
    g_assert_nonnull(strstr(s, "10 panes"));
    g_assert_nonnull(strstr(s, "50 tabs"));
}

static void
test_resolve_branch_primary_empty_string_vs_null(void)
{
    /* Both empty-string and NULL primary should behave identically */
    const char *b1 = sidebar_data_resolve_branch("", "fallback", FALSE);
    const char *b2 = sidebar_data_resolve_branch(NULL, "fallback", FALSE);
    g_assert_cmpstr(b1, ==, b2);

    b1 = sidebar_data_resolve_branch("", "fallback", TRUE);
    b2 = sidebar_data_resolve_branch(NULL, "fallback", TRUE);
    g_assert_true(b1 == NULL && b2 == NULL);
}

/* ── main ────────────────────────────────────────────────────────── */

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/sidebar-data/format-status/null-like",
                    test_format_status_null_like);
    g_test_add_func("/sidebar-data/format-status/single-pane-single-tab",
                    test_format_status_single_pane_single_tab);
    g_test_add_func("/sidebar-data/format-status/single-pane-multi-tab",
                    test_format_status_single_pane_multi_tab);
    g_test_add_func("/sidebar-data/format-status/multi-pane",
                    test_format_status_multi_pane);
    g_test_add_func("/sidebar-data/format-status/zero-panes-multi-tab",
                    test_format_status_zero_panes_multi_tab);

    g_test_add_func("/sidebar-data/resolve-branch/primary-wins",
                    test_resolve_branch_primary_wins);
    g_test_add_func("/sidebar-data/resolve-branch/fallback-no-terminal",
                    test_resolve_branch_fallback_no_terminal);
    g_test_add_func("/sidebar-data/resolve-branch/suppressed-with-terminal",
                    test_resolve_branch_suppressed_with_terminal);
    g_test_add_func("/sidebar-data/resolve-branch/all-empty",
                    test_resolve_branch_all_empty);

    g_test_add_func("/sidebar-data/resolve-cwd/terminal-wins",
                    test_resolve_cwd_terminal_wins);
    g_test_add_func("/sidebar-data/resolve-cwd/fallback",
                    test_resolve_cwd_fallback);
    g_test_add_func("/sidebar-data/resolve-cwd/both-empty",
                    test_resolve_cwd_both_empty);

    g_test_add_func("/sidebar-data/stability/branch-suppression-prevents-drift",
                    test_resolve_branch_suppression_prevents_drift);
    g_test_add_func("/sidebar-data/stability/cwd-fallback-only-without-terminal",
                    test_resolve_cwd_fallback_only_without_terminal);
    g_test_add_func("/sidebar-data/format-status/large-counts",
                    test_format_status_large_counts);
    g_test_add_func("/sidebar-data/stability/branch-empty-string-vs-null",
                    test_resolve_branch_primary_empty_string_vs_null);

    return g_test_run();
}
