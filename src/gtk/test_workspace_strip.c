/*
 * test_workspace_strip.c — tests for the strip layout backend (Phase 3).
 *
 * Exercises camera clamping, pan-to-focused-column geometry,
 * width interpolation via tick, maximize/unmaximize, and strip
 * state lifecycle.
 *
 * Uses a real GTK init for widget allocation, but no display required
 * (GTK4 headless / GDK_BACKEND=broadway is sufficient).
 */
#include "workspace_strip.h"
#include "workspace.h"
#include <math.h>
#include <string.h>

/* ── Stubs for workspace.c functions referenced by workspace_strip.c ── */

static int focus_pane_called;

void
workspace_focus_first_terminal(Workspace *ws)
{
    (void)ws;
}

void
workspace_toggle_zoom(Workspace *ws)
{
    (void)ws;
}

gboolean
workspace_focus_pane(Workspace *ws, GtkNotebook *pane)
{
    (void)ws;
    (void)pane;
    focus_pane_called++;
    return TRUE;
}

int
workspace_get_pane_index(Workspace *ws, GtkNotebook *pane)
{
    (void)ws;
    (void)pane;
    return -1;
}

static void
on_notebook_destroyed(gpointer data, GObject *where_the_object_was)
{
    gboolean *destroyed = data;
    (void)where_the_object_was;
    *destroyed = TRUE;
}

/* ── Helpers ──────────────────────────────────────────────────────── */

static Workspace *
make_strip_workspace(void)
{
    Workspace *ws = g_new0(Workspace, 1);
    ws->layout_mode = WORKSPACE_LAYOUT_STRIP;
    ws->pane_notebooks = g_ptr_array_new();
    ws->terminals = g_ptr_array_new();
    return ws;
}

static void
free_strip_workspace(Workspace *ws)
{
    if (!ws)
        return;
    workspace_strip_state_free(ws->strip_state);
    if (ws->pane_notebooks)
        g_ptr_array_unref(ws->pane_notebooks);
    if (ws->terminals)
        g_ptr_array_unref(ws->terminals);
    g_free(ws);
}

/* ── Tests: state lifecycle ───────────────────────────────────────── */

static void
test_state_new_free(void)
{
    WorkspaceStripState *state = workspace_strip_state_new();
    g_assert_nonnull(state);
    g_assert_nonnull(state->columns);
    g_assert_cmpuint(state->columns->len, ==, 0);
    g_assert_cmpint(state->focused_col, ==, 0);
    g_assert_cmpfloat(state->camera_x, ==, 0.0);
    g_assert_cmpfloat(state->camera_target_x, ==, 0.0);
    workspace_strip_state_free(state);
}

static void
test_init_sets_mode(void)
{
    Workspace *ws = make_strip_workspace();
    ws->layout_mode = WORKSPACE_LAYOUT_CLASSIC;
    workspace_strip_init(ws);
    g_assert_cmpint(ws->layout_mode, ==, WORKSPACE_LAYOUT_STRIP);
    g_assert_nonnull(ws->strip_state);
    free_strip_workspace(ws);
}

static void
test_init_replaces_old_state(void)
{
    Workspace *ws = make_strip_workspace();
    WorkspaceColumn *col;

    workspace_strip_init(ws);
    g_assert_nonnull(ws->strip_state);

    ws->strip_state->focused_col = 7;
    ws->strip_state->camera_target_x = 123.0;
    col = g_new0(WorkspaceColumn, 1);
    col->target_width = 640;
    col->current_width = 640.0;
    g_ptr_array_add(ws->strip_state->columns, col);

    workspace_strip_init(ws);
    g_assert_nonnull(ws->strip_state);
    g_assert_cmpint(ws->strip_state->focused_col, ==, 0);
    g_assert_cmpfloat(ws->strip_state->camera_target_x, ==, 0.0);
    g_assert_cmpuint(ws->strip_state->columns->len, ==, 0);
    free_strip_workspace(ws);
}

/* ── Tests: camera clamping ───────────────────────────────────────── */

static void
test_clamp_camera_null(void)
{
    workspace_strip_clamp_camera(NULL, 800);
}

static void
test_clamp_camera_empty_columns(void)
{
    WorkspaceStripState *state = workspace_strip_state_new();
    state->camera_target_x = 100.0;
    workspace_strip_clamp_camera(state, 800);
    g_assert_cmpfloat(state->camera_target_x, ==, 100.0);
    workspace_strip_state_free(state);
}

static void
test_clamp_camera_no_overflow(void)
{
    WorkspaceStripState *state = workspace_strip_state_new();

    WorkspaceColumn *col = g_new0(WorkspaceColumn, 1);
    col->target_width = 400;
    col->current_width = 400.0;
    g_ptr_array_add(state->columns, col);

    state->camera_target_x = 100.0;
    workspace_strip_clamp_camera(state, 800);
    g_assert_cmpfloat(state->camera_target_x, ==, 0.0);

    workspace_strip_state_free(state);
}

static void
test_clamp_camera_within_bounds(void)
{
    WorkspaceStripState *state = workspace_strip_state_new();

    WorkspaceColumn *c1 = g_new0(WorkspaceColumn, 1);
    c1->target_width = 600;
    c1->current_width = 600.0;
    g_ptr_array_add(state->columns, c1);

    WorkspaceColumn *c2 = g_new0(WorkspaceColumn, 1);
    c2->target_width = 600;
    c2->current_width = 600.0;
    g_ptr_array_add(state->columns, c2);

    state->camera_target_x = 200.0;
    workspace_strip_clamp_camera(state, 800);
    g_assert_cmpfloat(state->camera_target_x, ==, 200.0);

    workspace_strip_state_free(state);
}

static void
test_clamp_camera_negative(void)
{
    WorkspaceStripState *state = workspace_strip_state_new();

    WorkspaceColumn *col = g_new0(WorkspaceColumn, 1);
    col->target_width = 1000;
    col->current_width = 1000.0;
    g_ptr_array_add(state->columns, col);

    state->camera_target_x = -50.0;
    workspace_strip_clamp_camera(state, 800);
    g_assert_cmpfloat(state->camera_target_x, ==, 0.0);

    workspace_strip_state_free(state);
}

static void
test_clamp_camera_past_max(void)
{
    WorkspaceStripState *state = workspace_strip_state_new();

    WorkspaceColumn *c1 = g_new0(WorkspaceColumn, 1);
    c1->target_width = 600;
    c1->current_width = 600.0;
    g_ptr_array_add(state->columns, c1);

    WorkspaceColumn *c2 = g_new0(WorkspaceColumn, 1);
    c2->target_width = 600;
    c2->current_width = 600.0;
    g_ptr_array_add(state->columns, c2);

    /* total = 1200, viewport = 800, max_scroll = 400 */
    state->camera_target_x = 500.0;
    workspace_strip_clamp_camera(state, 800);
    g_assert_cmpfloat(state->camera_target_x, ==, 400.0);

    workspace_strip_state_free(state);
}

/* ── Tests: focus_column ──────────────────────────────────────────── */

static void
test_focus_column_null(void)
{
    workspace_strip_focus_column(NULL, 0);
}

static void
test_focus_column_no_state(void)
{
    Workspace *ws = g_new0(Workspace, 1);
    workspace_strip_focus_column(ws, 0);
    g_free(ws);
}

static void
test_focus_column_out_of_range(void)
{
    Workspace *ws = make_strip_workspace();
    workspace_strip_init(ws);

    WorkspaceColumn *col = g_new0(WorkspaceColumn, 1);
    col->target_width = 600;
    col->current_width = 600.0;
    g_ptr_array_add(ws->strip_state->columns, col);

    workspace_strip_focus_column(ws, 5);
    g_assert_cmpint(ws->strip_state->focused_col, ==, 0);

    workspace_strip_focus_column(ws, -1);
    g_assert_cmpint(ws->strip_state->focused_col, ==, 0);

    free_strip_workspace(ws);
}

/* ── Tests: pan_to_focused_column geometry ────────────────────────── */

static void
test_pan_single_column(void)
{
    Workspace *ws = make_strip_workspace();
    workspace_strip_init(ws);
    WorkspaceStripState *state = ws->strip_state;

    WorkspaceColumn *col = g_new0(WorkspaceColumn, 1);
    col->current_width = 600.0;
    col->target_width = 600;
    g_ptr_array_add(state->columns, col);

    state->focused_col = 0;
    workspace_strip_pan_to_focused_column(ws);

    /* col_center = 300, viewport = 600 (default), camera_target = 300 - 300 = 0 */
    g_assert_cmpfloat(state->camera_target_x, ==, 0.0);

    free_strip_workspace(ws);
}

static void
test_pan_second_of_three_columns(void)
{
    Workspace *ws = make_strip_workspace();
    workspace_strip_init(ws);
    WorkspaceStripState *state = ws->strip_state;

    for (int i = 0; i < 3; i++) {
        WorkspaceColumn *col = g_new0(WorkspaceColumn, 1);
        col->current_width = 600.0;
        col->target_width = 600;
        g_ptr_array_add(state->columns, col);
    }

    state->focused_col = 1;
    workspace_strip_pan_to_focused_column(ws);

    /* col_left = 600, col_center = 600 + 300 = 900
     * viewport = 600 (default), unclamped = 900 - 300 = 600
     * total = 1800, max_scroll = 1800 - 600 = 1200
     * clamped: 600 is within [0, 1200] */
    g_assert_cmpfloat(state->camera_target_x, ==, 600.0);

    free_strip_workspace(ws);
}

/* ── Tests: tick callback ─────────────────────────────────────────── */

static void
test_tick_converges_camera(void)
{
    Workspace *ws = make_strip_workspace();
    workspace_strip_init(ws);
    WorkspaceStripState *state = ws->strip_state;

    WorkspaceColumn *col = g_new0(WorkspaceColumn, 1);
    col->current_width = 600.0;
    col->target_width = 600;
    g_ptr_array_add(state->columns, col);

    state->camera_x = 0.0;
    state->camera_target_x = 100.0;

    gboolean running = TRUE;
    for (int i = 0; i < 200 && running; i++)
        running = workspace_strip_tick_cb(NULL, NULL, ws) == G_SOURCE_CONTINUE;

    g_assert_cmpfloat(fabs(state->camera_x - 100.0), <, 1.0);

    free_strip_workspace(ws);
}

static void
test_tick_converges_width(void)
{
    Workspace *ws = make_strip_workspace();
    workspace_strip_init(ws);
    WorkspaceStripState *state = ws->strip_state;

    WorkspaceColumn *col = g_new0(WorkspaceColumn, 1);
    col->current_width = 400.0;
    col->target_width = 800;
    g_ptr_array_add(state->columns, col);

    state->camera_x = 0.0;
    state->camera_target_x = 0.0;

    gboolean running = TRUE;
    for (int i = 0; i < 200 && running; i++)
        running = workspace_strip_tick_cb(NULL, NULL, ws) == G_SOURCE_CONTINUE;

    g_assert_cmpfloat(fabs(col->current_width - 800.0), <, 1.0);

    free_strip_workspace(ws);
}

static void
test_tick_stops_when_converged(void)
{
    Workspace *ws = make_strip_workspace();
    workspace_strip_init(ws);
    WorkspaceStripState *state = ws->strip_state;

    WorkspaceColumn *col = g_new0(WorkspaceColumn, 1);
    col->current_width = 600.0;
    col->target_width = 600;
    g_ptr_array_add(state->columns, col);

    state->camera_x = 50.0;
    state->camera_target_x = 50.0;

    gboolean result = workspace_strip_tick_cb(NULL, NULL, ws);
    g_assert_cmpint(result, ==, G_SOURCE_REMOVE);

    free_strip_workspace(ws);
}

static void
test_tick_null_safety(void)
{
    gboolean result = workspace_strip_tick_cb(NULL, NULL, NULL);
    g_assert_cmpint(result, ==, G_SOURCE_REMOVE);
}

/* ── Tests: apply_layout ──────────────────────────────────────────── */

static void
test_apply_layout_sets_defaults(void)
{
    Workspace *ws = make_strip_workspace();
    workspace_strip_init(ws);
    WorkspaceStripState *state = ws->strip_state;

    WorkspaceColumn *col = g_new0(WorkspaceColumn, 1);
    col->target_width = 0;
    col->current_width = 0;
    g_ptr_array_add(state->columns, col);

    workspace_strip_apply_layout(ws);

    g_assert_cmpint(col->target_width, >, 0);
    g_assert_cmpfloat(col->current_width, >, 0.0);

    free_strip_workspace(ws);
}

static void
test_apply_layout_null_safety(void)
{
    workspace_strip_apply_layout(NULL);

    Workspace *ws = g_new0(Workspace, 1);
    workspace_strip_apply_layout(ws);
    g_free(ws);
}

/* ── Tests: focus_primary null safety ─────────────────────────────── */

static void
test_focus_primary_null(void)
{
    workspace_strip_focus_primary(NULL);
}

static void
test_focus_primary_no_state(void)
{
    Workspace *ws = g_new0(Workspace, 1);
    workspace_strip_focus_primary(ws);
    g_free(ws);
}

/* ── Tests: focus_primary calls workspace_focus_pane ──────────────── */

static void
test_focus_primary_calls_focus_pane(void)
{
    Workspace *ws = make_strip_workspace();
    GtkWidget *nb = gtk_notebook_new();
    workspace_strip_create_root(ws, nb);

    focus_pane_called = 0;
    workspace_strip_focus_primary(ws);
    g_assert_cmpint(focus_pane_called, ==, 1);

    free_strip_workspace(ws);
}

/* ── Tests: create_root widget hierarchy ─────────────────────────── */

static void
test_create_root_returns_scrolled_window(void)
{
    Workspace *ws = make_strip_workspace();
    GtkWidget *nb = gtk_notebook_new();

    GtkWidget *root = workspace_strip_create_root(ws, nb);

    g_assert_nonnull(root);
    g_assert_true(GTK_IS_SCROLLED_WINDOW(root));
    g_assert_true(ws->strip_state->scroll_container == root);
    g_assert_nonnull(ws->strip_state->column_box);

    free_strip_workspace(ws);
}

static void
test_create_root_registers_column(void)
{
    Workspace *ws = make_strip_workspace();
    GtkWidget *nb = gtk_notebook_new();

    workspace_strip_create_root(ws, nb);

    g_assert_cmpuint(ws->strip_state->columns->len, ==, 1);
    WorkspaceColumn *col = g_ptr_array_index(ws->strip_state->columns, 0);
    g_assert_true(col->notebook == nb);
    g_assert_cmpint(col->target_width, ==, 600);
    g_assert_cmpfloat(col->current_width, ==, 600.0);

    free_strip_workspace(ws);
}

static void
test_create_root_sets_strip_mode(void)
{
    Workspace *ws = make_strip_workspace();
    ws->layout_mode = WORKSPACE_LAYOUT_CLASSIC;
    GtkWidget *nb = gtk_notebook_new();

    workspace_strip_create_root(ws, nb);

    g_assert_cmpint(ws->layout_mode, ==, WORKSPACE_LAYOUT_STRIP);

    free_strip_workspace(ws);
}

/* ── Tests: focus_primary lands on correct notebook after create_root ── */

static void
test_focus_primary_lands_on_first_notebook(void)
{
    Workspace *ws = make_strip_workspace();
    GtkWidget *nb = gtk_notebook_new();
    workspace_strip_create_root(ws, nb);

    focus_pane_called = 0;
    workspace_strip_focus_primary(ws);

    g_assert_cmpint(focus_pane_called, ==, 1);
    g_assert_true(ws->active_pane == GTK_NOTEBOOK(nb));

    free_strip_workspace(ws);
}

/* ── Tests: apply_layout with real widgets ───────────────────────── */

static void
test_apply_layout_with_scroll_container(void)
{
    Workspace *ws = make_strip_workspace();
    GtkWidget *nb = gtk_notebook_new();
    workspace_strip_create_root(ws, nb);

    WorkspaceColumn *col = g_ptr_array_index(ws->strip_state->columns, 0);
    col->target_width = 0;
    col->current_width = 0;

    workspace_strip_apply_layout(ws);

    g_assert_cmpint(col->target_width, >, 0);
    g_assert_cmpfloat(col->current_width, >, 0.0);

    free_strip_workspace(ws);
}

/* ── Tests: toggle_zoom (maximize / unmaximize) ──────────────────── */

static void
test_toggle_zoom_null(void)
{
    workspace_strip_toggle_zoom(NULL);
}

static void
test_toggle_zoom_maximizes_column(void)
{
    Workspace *ws = make_strip_workspace();
    workspace_strip_init(ws);

    WorkspaceColumn *col = g_new0(WorkspaceColumn, 1);
    col->target_width = 400;
    col->current_width = 400.0;
    g_ptr_array_add(ws->strip_state->columns, col);

    ws->strip_state->focused_col = 0;
    g_assert_false(col->maximized);

    workspace_strip_toggle_zoom(ws);

    g_assert_true(col->maximized);
    /* No scroll_container, so viewport defaults to 600 */
    g_assert_cmpint(col->target_width, ==, 600);

    free_strip_workspace(ws);
}

static void
test_toggle_zoom_unmaximizes_column(void)
{
    Workspace *ws = make_strip_workspace();
    workspace_strip_init(ws);

    WorkspaceColumn *col = g_new0(WorkspaceColumn, 1);
    col->target_width = 800;
    col->current_width = 800.0;
    col->maximized = TRUE;
    g_ptr_array_add(ws->strip_state->columns, col);

    ws->strip_state->focused_col = 0;

    workspace_strip_toggle_zoom(ws);

    g_assert_false(col->maximized);
    g_assert_cmpint(col->target_width, ==, 600);

    free_strip_workspace(ws);
}

static void
test_toggle_zoom_no_columns(void)
{
    Workspace *ws = make_strip_workspace();
    workspace_strip_init(ws);
    ws->strip_state->focused_col = 0;

    workspace_strip_toggle_zoom(ws);

    free_strip_workspace(ws);
}

/* ── Tests: insert/remove column actions ─────────────────────────── */

static void
test_insert_column_after_active_inserts_and_focuses(void)
{
    Workspace *ws = make_strip_workspace();
    GtkWidget *nb0 = gtk_notebook_new();
    GtkWidget *nb1 = gtk_notebook_new();

    workspace_strip_create_root(ws, nb0);
    g_assert_cmpuint(ws->strip_state->columns->len, ==, 1);

    g_assert_true(workspace_strip_insert_column_after_active(ws, nb1));
    g_assert_cmpuint(ws->strip_state->columns->len, ==, 2);
    g_assert_cmpint(ws->strip_state->focused_col, ==, 1);

    WorkspaceColumn *col0 = g_ptr_array_index(ws->strip_state->columns, 0);
    WorkspaceColumn *col1 = g_ptr_array_index(ws->strip_state->columns, 1);
    g_assert_true(col0->notebook == nb0);
    g_assert_true(col1->notebook == nb1);
    g_assert_true(ws->active_pane == GTK_NOTEBOOK(nb1));

    free_strip_workspace(ws);
}

static void
test_split_vertical_preserves_existing_notebook_lifetime(void)
{
    Workspace *ws = make_strip_workspace();
    GtkWidget *nb0 = gtk_notebook_new();
    GtkWidget *nb1 = gtk_notebook_new();
    gboolean nb0_destroyed = FALSE;

    workspace_strip_create_root(ws, nb0);
    g_object_weak_ref(G_OBJECT(nb0), on_notebook_destroyed, &nb0_destroyed);

    g_assert_true(workspace_strip_split_vertical_in_column(ws, nb1));
    g_assert_false(nb0_destroyed);

    WorkspaceColumn *col = g_ptr_array_index(ws->strip_state->columns, 0);
    g_assert_nonnull(col);
    g_assert_cmpuint(col->panes->len, ==, 2);
    g_assert_true(g_ptr_array_index(col->panes, 0) == nb0);
    g_assert_true(g_ptr_array_index(col->panes, 1) == nb1);

    free_strip_workspace(ws);
}

static void
test_remove_pane_from_column_preserves_survivor_lifetime(void)
{
    Workspace *ws = make_strip_workspace();
    GtkWidget *nb0 = gtk_notebook_new();
    GtkWidget *nb1 = gtk_notebook_new();
    gboolean nb0_destroyed = FALSE;

    workspace_strip_create_root(ws, nb0);
    g_assert_true(workspace_strip_split_vertical_in_column(ws, nb1));
    g_object_weak_ref(G_OBJECT(nb0), on_notebook_destroyed, &nb0_destroyed);

    g_assert_true(workspace_strip_remove_pane_from_column(ws, GTK_NOTEBOOK(nb1)));
    g_assert_false(nb0_destroyed);

    WorkspaceColumn *col = g_ptr_array_index(ws->strip_state->columns, 0);
    g_assert_nonnull(col);
    g_assert_cmpuint(col->panes->len, ==, 1);
    g_assert_true(g_ptr_array_index(col->panes, 0) == nb0);
    g_assert_true(col->notebook == nb0);

    free_strip_workspace(ws);
}

static void
test_remove_active_column_updates_focus(void)
{
    Workspace *ws = make_strip_workspace();
    GtkWidget *nb0 = gtk_notebook_new();
    GtkWidget *nb1 = gtk_notebook_new();

    workspace_strip_create_root(ws, nb0);
    workspace_strip_add_notebook_column(ws, nb1);
    g_assert_cmpuint(ws->strip_state->columns->len, ==, 2);

    workspace_strip_focus_column(ws, 1);
    g_assert_cmpint(ws->strip_state->focused_col, ==, 1);
    g_assert_true(ws->active_pane == GTK_NOTEBOOK(nb1));

    g_assert_true(workspace_strip_remove_active_column(ws));
    g_assert_cmpuint(ws->strip_state->columns->len, ==, 1);
    g_assert_cmpint(ws->strip_state->focused_col, ==, 0);
    g_assert_true(ws->active_pane == GTK_NOTEBOOK(nb0));

    free_strip_workspace(ws);
}

static void
test_remove_active_column_single_fails(void)
{
    Workspace *ws = make_strip_workspace();
    GtkWidget *nb0 = gtk_notebook_new();

    workspace_strip_create_root(ws, nb0);
    g_assert_cmpuint(ws->strip_state->columns->len, ==, 1);
    g_assert_false(workspace_strip_remove_active_column(ws));

    free_strip_workspace(ws);
}

/* ── Tests: teardown with active tick (destroy widget before free state) ── */

static void
test_teardown_widget_destroyed_before_state_free(void)
{
    Workspace *ws = make_strip_workspace();
    GtkWidget *nb = gtk_notebook_new();
    GtkWidget *root = workspace_strip_create_root(ws, nb);

    WorkspaceStripState *state = ws->strip_state;
    state->camera_x = 0.0;
    state->camera_target_x = 200.0;

    /* Simulate active animation: tick_id would be set by ensure_tick_running,
     * but that needs a realized widget. Manually poke a nonzero value so
     * the free path exercises the tick-removal branch. */
    state->tick_id = 42;

    /* Simulate workspace_remove() ordering: widget tree destroyed first.
     * Use a GtkBox parent so unparent triggers finalization. */
    GtkWidget *parent = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(parent), root);

    /* Remove root from parent — drops the last reference, triggering
     * the weak notify which should NULL scroll_container and tick_id. */
    gtk_box_remove(GTK_BOX(parent), root);

    g_assert_null(state->scroll_container);
    g_assert_cmpuint(state->tick_id, ==, 0);

    /* Now free the strip state — must not crash. */
    workspace_strip_state_free(ws->strip_state);
    ws->strip_state = NULL;

    if (ws->pane_notebooks)
        g_ptr_array_unref(ws->pane_notebooks);
    if (ws->terminals)
        g_ptr_array_unref(ws->terminals);
    g_free(ws);
}

/* ── Tests: rebuild for layout mode ──────────────────────────────── */

static void
test_rebuild_null(void)
{
    g_assert_false(workspace_rebuild_for_layout_mode(NULL, WORKSPACE_LAYOUT_STRIP));
}

static void
test_rebuild_same_mode(void)
{
    Workspace *ws = make_strip_workspace();
    workspace_strip_init(ws);
    g_assert_true(workspace_rebuild_for_layout_mode(ws, WORKSPACE_LAYOUT_STRIP));
    free_strip_workspace(ws);
}

static void
test_rebuild_classic_to_strip(void)
{
    Workspace *ws = make_strip_workspace();
    ws->layout_mode = WORKSPACE_LAYOUT_CLASSIC;

    GtkWidget *nb = gtk_notebook_new();
    ws->notebook = nb;
    ws->overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(ws->overlay), nb);

    g_assert_true(workspace_rebuild_for_layout_mode(ws, WORKSPACE_LAYOUT_STRIP));
    g_assert_cmpint(ws->layout_mode, ==, WORKSPACE_LAYOUT_STRIP);
    g_assert_nonnull(ws->strip_state);

    GtkWidget *root = gtk_overlay_get_child(GTK_OVERLAY(ws->overlay));
    g_assert_true(GTK_IS_SCROLLED_WINDOW(root));

    free_strip_workspace(ws);
}

static void
test_rebuild_strip_to_classic(void)
{
    Workspace *ws = make_strip_workspace();
    GtkWidget *nb = gtk_notebook_new();
    ws->notebook = nb;
    ws->overlay = gtk_overlay_new();

    GtkWidget *strip_root = workspace_strip_create_root(ws, nb);
    gtk_overlay_set_child(GTK_OVERLAY(ws->overlay), strip_root);

    g_assert_cmpint(ws->layout_mode, ==, WORKSPACE_LAYOUT_STRIP);

    g_assert_true(workspace_rebuild_for_layout_mode(ws, WORKSPACE_LAYOUT_CLASSIC));
    g_assert_cmpint(ws->layout_mode, ==, WORKSPACE_LAYOUT_CLASSIC);
    g_assert_null(ws->strip_state);

    GtkWidget *root = gtk_overlay_get_child(GTK_OVERLAY(ws->overlay));
    g_assert_true(root == nb);

    if (ws->pane_notebooks)
        g_ptr_array_unref(ws->pane_notebooks);
    if (ws->terminals)
        g_ptr_array_unref(ws->terminals);
    g_free(ws);
}

static void
test_rebuild_roundtrip(void)
{
    Workspace *ws = make_strip_workspace();
    ws->layout_mode = WORKSPACE_LAYOUT_CLASSIC;

    GtkWidget *nb = gtk_notebook_new();
    ws->notebook = nb;
    ws->overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(ws->overlay), nb);

    g_assert_true(workspace_rebuild_for_layout_mode(ws, WORKSPACE_LAYOUT_STRIP));
    g_assert_cmpint(ws->layout_mode, ==, WORKSPACE_LAYOUT_STRIP);

    g_assert_true(workspace_rebuild_for_layout_mode(ws, WORKSPACE_LAYOUT_CLASSIC));
    g_assert_cmpint(ws->layout_mode, ==, WORKSPACE_LAYOUT_CLASSIC);

    GtkWidget *root = gtk_overlay_get_child(GTK_OVERLAY(ws->overlay));
    g_assert_true(root == nb);

    if (ws->pane_notebooks)
        g_ptr_array_unref(ws->pane_notebooks);
    if (ws->terminals)
        g_ptr_array_unref(ws->terminals);
    g_free(ws);
}

/* ── Tests: multi-pane rebuild ─────────────────────────────────── */

static void
test_rebuild_classic_to_strip_multi_pane(void)
{
    Workspace *ws = make_strip_workspace();
    ws->layout_mode = WORKSPACE_LAYOUT_CLASSIC;

    GtkWidget *nb1 = gtk_notebook_new();
    GtkWidget *nb2 = gtk_notebook_new();
    ws->notebook = nb1;
    g_ptr_array_add(ws->pane_notebooks, nb1);
    g_ptr_array_add(ws->pane_notebooks, nb2);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(paned), nb1);
    gtk_paned_set_end_child(GTK_PANED(paned), nb2);

    ws->overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(ws->overlay), paned);

    g_assert_true(workspace_rebuild_for_layout_mode(ws, WORKSPACE_LAYOUT_STRIP));
    g_assert_cmpint(ws->layout_mode, ==, WORKSPACE_LAYOUT_STRIP);
    g_assert_nonnull(ws->strip_state);
    g_assert_cmpuint(ws->strip_state->columns->len, ==, 2);

    WorkspaceColumn *col0 = g_ptr_array_index(ws->strip_state->columns, 0);
    WorkspaceColumn *col1 = g_ptr_array_index(ws->strip_state->columns, 1);
    g_assert_true(col0->notebook == nb1);
    g_assert_true(col1->notebook == nb2);

    g_assert_cmpuint(ws->pane_notebooks->len, ==, 2);
    g_assert_true(g_ptr_array_index(ws->pane_notebooks, 0) == nb1);
    g_assert_true(g_ptr_array_index(ws->pane_notebooks, 1) == nb2);
    g_assert_true(ws->notebook == nb1);

    free_strip_workspace(ws);
}

static void
test_rebuild_classic_to_strip_multi_pane_keeps_secondary_alive(void)
{
    Workspace *ws = make_strip_workspace();
    ws->layout_mode = WORKSPACE_LAYOUT_CLASSIC;

    GtkWidget *nb1 = gtk_notebook_new();
    GtkWidget *nb2 = gtk_notebook_new();
    gboolean nb2_destroyed = FALSE;

    ws->notebook = nb1;
    g_ptr_array_add(ws->pane_notebooks, nb1);
    g_ptr_array_add(ws->pane_notebooks, nb2);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(paned), nb1);
    gtk_paned_set_end_child(GTK_PANED(paned), nb2);

    g_object_weak_ref(G_OBJECT(nb2), on_notebook_destroyed, &nb2_destroyed);

    ws->overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(ws->overlay), paned);

    g_assert_true(workspace_rebuild_for_layout_mode(ws, WORKSPACE_LAYOUT_STRIP));
    g_assert_cmpuint(ws->pane_notebooks->len, ==, 2);
    g_assert_true(g_ptr_array_index(ws->pane_notebooks, 1) == nb2);
    g_assert_false(nb2_destroyed);

    free_strip_workspace(ws);
}

static void
test_rebuild_classic_to_strip_collects_live_tree_notebooks(void)
{
    Workspace *ws = make_strip_workspace();
    ws->layout_mode = WORKSPACE_LAYOUT_CLASSIC;

    GtkWidget *nb1 = gtk_notebook_new();
    GtkWidget *nb2 = gtk_notebook_new();
    ws->notebook = nb1;

    /* Simulate out-of-sync pane_notebooks: only primary pane tracked. */
    g_ptr_array_add(ws->pane_notebooks, nb1);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(paned), nb1);
    gtk_paned_set_end_child(GTK_PANED(paned), nb2);

    ws->overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(ws->overlay), paned);

    g_assert_true(workspace_rebuild_for_layout_mode(ws, WORKSPACE_LAYOUT_STRIP));
    g_assert_cmpuint(ws->strip_state->columns->len, ==, 2);
    g_assert_cmpuint(ws->pane_notebooks->len, ==, 2);
    g_assert_true(g_ptr_array_index(ws->pane_notebooks, 0) == nb1);
    g_assert_true(g_ptr_array_index(ws->pane_notebooks, 1) == nb2);

    free_strip_workspace(ws);
}

static void
test_rebuild_strip_to_classic_multi_pane(void)
{
    Workspace *ws = make_strip_workspace();

    GtkWidget *nb1 = gtk_notebook_new();
    GtkWidget *nb2 = gtk_notebook_new();
    ws->notebook = nb1;
    g_ptr_array_add(ws->pane_notebooks, nb1);
    g_ptr_array_add(ws->pane_notebooks, nb2);

    ws->overlay = gtk_overlay_new();
    GtkWidget *strip_root = workspace_strip_create_root(ws, nb1);
    workspace_strip_add_notebook_column(ws, nb2);
    gtk_overlay_set_child(GTK_OVERLAY(ws->overlay), strip_root);

    g_assert_cmpint(ws->layout_mode, ==, WORKSPACE_LAYOUT_STRIP);
    g_assert_cmpuint(ws->strip_state->columns->len, ==, 2);

    g_assert_true(workspace_rebuild_for_layout_mode(ws, WORKSPACE_LAYOUT_CLASSIC));
    g_assert_cmpint(ws->layout_mode, ==, WORKSPACE_LAYOUT_CLASSIC);
    g_assert_null(ws->strip_state);

    GtkWidget *root = gtk_overlay_get_child(GTK_OVERLAY(ws->overlay));
    g_assert_true(GTK_IS_PANED(root));

    GtkWidget *start = gtk_paned_get_start_child(GTK_PANED(root));
    GtkWidget *end = gtk_paned_get_end_child(GTK_PANED(root));
    g_assert_true(start == nb1);
    g_assert_true(end == nb2);

    g_assert_cmpuint(ws->pane_notebooks->len, ==, 2);
    g_assert_true(g_ptr_array_index(ws->pane_notebooks, 0) == nb1);
    g_assert_true(g_ptr_array_index(ws->pane_notebooks, 1) == nb2);
    g_assert_true(ws->notebook == nb1);

    if (ws->pane_notebooks)
        g_ptr_array_unref(ws->pane_notebooks);
    if (ws->terminals)
        g_ptr_array_unref(ws->terminals);
    g_free(ws);
}

static void
test_rebuild_roundtrip_multi_pane(void)
{
    Workspace *ws = make_strip_workspace();
    ws->layout_mode = WORKSPACE_LAYOUT_CLASSIC;

    GtkWidget *nb1 = gtk_notebook_new();
    GtkWidget *nb2 = gtk_notebook_new();
    ws->notebook = nb1;
    g_ptr_array_add(ws->pane_notebooks, nb1);
    g_ptr_array_add(ws->pane_notebooks, nb2);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(paned), nb1);
    gtk_paned_set_end_child(GTK_PANED(paned), nb2);

    ws->overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(ws->overlay), paned);

    g_assert_true(workspace_rebuild_for_layout_mode(ws, WORKSPACE_LAYOUT_STRIP));
    g_assert_cmpint(ws->layout_mode, ==, WORKSPACE_LAYOUT_STRIP);
    g_assert_cmpuint(ws->strip_state->columns->len, ==, 2);

    g_assert_true(workspace_rebuild_for_layout_mode(ws, WORKSPACE_LAYOUT_CLASSIC));
    g_assert_cmpint(ws->layout_mode, ==, WORKSPACE_LAYOUT_CLASSIC);

    GtkWidget *root = gtk_overlay_get_child(GTK_OVERLAY(ws->overlay));
    g_assert_true(GTK_IS_PANED(root));

    GtkWidget *start = gtk_paned_get_start_child(GTK_PANED(root));
    GtkWidget *end = gtk_paned_get_end_child(GTK_PANED(root));
    g_assert_true(start == nb1);
    g_assert_true(end == nb2);

    g_assert_cmpuint(ws->pane_notebooks->len, ==, 2);
    g_assert_true(g_ptr_array_index(ws->pane_notebooks, 0) == nb1);
    g_assert_true(g_ptr_array_index(ws->pane_notebooks, 1) == nb2);

    if (ws->pane_notebooks)
        g_ptr_array_unref(ws->pane_notebooks);
    if (ws->terminals)
        g_ptr_array_unref(ws->terminals);
    g_free(ws);
}

/* ── main ──────────────────────────────────────────────────────────── */

static gboolean have_display;

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    have_display = gtk_init_check();

    /* State lifecycle */
    g_test_add_func("/workspace-strip/state/new-free",
                    test_state_new_free);
    g_test_add_func("/workspace-strip/init/sets-mode",
                    test_init_sets_mode);
    g_test_add_func("/workspace-strip/init/replaces-old-state",
                    test_init_replaces_old_state);

    /* Camera clamping */
    g_test_add_func("/workspace-strip/clamp-camera/null",
                    test_clamp_camera_null);
    g_test_add_func("/workspace-strip/clamp-camera/empty-columns",
                    test_clamp_camera_empty_columns);
    g_test_add_func("/workspace-strip/clamp-camera/no-overflow",
                    test_clamp_camera_no_overflow);
    g_test_add_func("/workspace-strip/clamp-camera/within-bounds",
                    test_clamp_camera_within_bounds);
    g_test_add_func("/workspace-strip/clamp-camera/negative",
                    test_clamp_camera_negative);
    g_test_add_func("/workspace-strip/clamp-camera/past-max",
                    test_clamp_camera_past_max);

    /* Column focus */
    g_test_add_func("/workspace-strip/focus-column/null",
                    test_focus_column_null);
    g_test_add_func("/workspace-strip/focus-column/no-state",
                    test_focus_column_no_state);
    g_test_add_func("/workspace-strip/focus-column/out-of-range",
                    test_focus_column_out_of_range);

    /* Pan geometry */
    g_test_add_func("/workspace-strip/pan/single-column",
                    test_pan_single_column);
    g_test_add_func("/workspace-strip/pan/second-of-three",
                    test_pan_second_of_three_columns);

    /* Tick callback */
    g_test_add_func("/workspace-strip/tick/converges-camera",
                    test_tick_converges_camera);
    g_test_add_func("/workspace-strip/tick/converges-width",
                    test_tick_converges_width);
    g_test_add_func("/workspace-strip/tick/stops-when-converged",
                    test_tick_stops_when_converged);
    g_test_add_func("/workspace-strip/tick/null-safety",
                    test_tick_null_safety);

    /* Apply layout */
    g_test_add_func("/workspace-strip/apply-layout/sets-defaults",
                    test_apply_layout_sets_defaults);
    g_test_add_func("/workspace-strip/apply-layout/null-safety",
                    test_apply_layout_null_safety);

    /* Widget tests require a display (GTK init) */
    if (have_display) {
        g_test_add_func("/workspace-strip/apply-layout/with-scroll-container",
                        test_apply_layout_with_scroll_container);
        g_test_add_func("/workspace-strip/focus-primary/calls-focus-pane",
                        test_focus_primary_calls_focus_pane);
        g_test_add_func("/workspace-strip/focus-primary/lands-on-first-notebook",
                        test_focus_primary_lands_on_first_notebook);
        g_test_add_func("/workspace-strip/create-root/returns-scrolled-window",
                        test_create_root_returns_scrolled_window);
        g_test_add_func("/workspace-strip/create-root/registers-column",
                        test_create_root_registers_column);
        g_test_add_func("/workspace-strip/create-root/sets-strip-mode",
                        test_create_root_sets_strip_mode);
        g_test_add_func("/workspace-strip/teardown/widget-destroyed-before-state-free",
                        test_teardown_widget_destroyed_before_state_free);
        g_test_add_func("/workspace-strip/columns/insert-after-active",
                        test_insert_column_after_active_inserts_and_focuses);
        g_test_add_func("/workspace-strip/columns/split-vertical-preserves-existing",
                        test_split_vertical_preserves_existing_notebook_lifetime);
        g_test_add_func("/workspace-strip/columns/remove-pane-preserves-survivor",
                        test_remove_pane_from_column_preserves_survivor_lifetime);
        g_test_add_func("/workspace-strip/columns/remove-active",
                        test_remove_active_column_updates_focus);
        g_test_add_func("/workspace-strip/columns/remove-active-single-fails",
                        test_remove_active_column_single_fails);

        /* Rebuild tests */
        g_test_add_func("/workspace-strip/rebuild/classic-to-strip",
                        test_rebuild_classic_to_strip);
        g_test_add_func("/workspace-strip/rebuild/strip-to-classic",
                        test_rebuild_strip_to_classic);
        g_test_add_func("/workspace-strip/rebuild/roundtrip",
                        test_rebuild_roundtrip);

        /* Multi-pane rebuild */
        g_test_add_func("/workspace-strip/rebuild/classic-to-strip-multi-pane",
                        test_rebuild_classic_to_strip_multi_pane);
        g_test_add_func("/workspace-strip/rebuild/classic-to-strip-multi-pane-keeps-secondary-alive",
                        test_rebuild_classic_to_strip_multi_pane_keeps_secondary_alive);
        g_test_add_func("/workspace-strip/rebuild/classic-to-strip-collects-live-tree-notebooks",
                        test_rebuild_classic_to_strip_collects_live_tree_notebooks);
        g_test_add_func("/workspace-strip/rebuild/strip-to-classic-multi-pane",
                        test_rebuild_strip_to_classic_multi_pane);
        g_test_add_func("/workspace-strip/rebuild/roundtrip-multi-pane",
                        test_rebuild_roundtrip_multi_pane);
    }

    /* Focus primary */
    g_test_add_func("/workspace-strip/focus-primary/null",
                    test_focus_primary_null);
    g_test_add_func("/workspace-strip/focus-primary/no-state",
                    test_focus_primary_no_state);

    /* Rebuild (no display needed) */
    g_test_add_func("/workspace-strip/rebuild/null",
                    test_rebuild_null);
    g_test_add_func("/workspace-strip/rebuild/same-mode",
                    test_rebuild_same_mode);

    /* Toggle zoom */
    g_test_add_func("/workspace-strip/toggle-zoom/null",
                    test_toggle_zoom_null);
    g_test_add_func("/workspace-strip/toggle-zoom/maximizes-column",
                    test_toggle_zoom_maximizes_column);
    g_test_add_func("/workspace-strip/toggle-zoom/unmaximizes-column",
                    test_toggle_zoom_unmaximizes_column);
    g_test_add_func("/workspace-strip/toggle-zoom/no-columns",
                    test_toggle_zoom_no_columns);

    return g_test_run();
}
