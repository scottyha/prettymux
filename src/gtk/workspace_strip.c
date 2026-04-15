/*
 * workspace_strip.c — Strip layout backend (Phase 3).
 *
 * Horizontal column strip with model-driven widths, camera panning,
 * and tick-based animation.  One notebook per column initially.
 */
#include "workspace_strip.h"
#include "workspace.h"

#define STRIP_DEFAULT_COL_WIDTH       600
#define STRIP_INSERT_START_WIDTH      180
#define STRIP_REMOVE_SETTLE_MAX_DELTA 120.0
#define STRIP_ANIM_EPSILON            0.5
#define STRIP_MIN_DT_SEC              (1.0 / 240.0)
#define STRIP_MAX_DT_SEC              (1.0 / 20.0)
#define STRIP_DEFAULT_DT_SEC          (1.0 / 60.0)

#define STRIP_CAMERA_LAMBDA_DEFAULT   10.5
#define STRIP_CAMERA_LAMBDA_INSERT    8.5
#define STRIP_CAMERA_LAMBDA_REMOVE    12.0
#define STRIP_CAMERA_LAMBDA_MAXIMIZE  7.0

#define STRIP_WIDTH_LAMBDA_DEFAULT    11.5
#define STRIP_WIDTH_LAMBDA_INSERT     9.5
#define STRIP_WIDTH_LAMBDA_REMOVE     12.5
#define STRIP_WIDTH_LAMBDA_MAXIMIZE   6.5

/* ── Lifecycle ──────────────────────────────────────────────────── */

static void
workspace_column_free(gpointer data)
{
    g_free(data);
}

static void
on_scroll_container_weak_notify(gpointer data, GObject *where_the_object_was)
{
    (void)where_the_object_was;
    WorkspaceStripState *state = data;
    state->scroll_container = NULL;
    state->tick_id = 0;
    state->last_tick_usec = 0;
}

WorkspaceStripState *
workspace_strip_state_new(void)
{
    WorkspaceStripState *state = g_new0(WorkspaceStripState, 1);
    state->columns = g_ptr_array_new_with_free_func(workspace_column_free);
    state->focused_col = 0;
    state->camera_x = 0.0;
    state->camera_target_x = 0.0;
    state->anim_profile = WORKSPACE_STRIP_ANIM_DEFAULT;
    return state;
}

void
workspace_strip_state_free(WorkspaceStripState *state)
{
    if (!state)
        return;
    if (state->scroll_container) {
        if (state->tick_id)
            gtk_widget_remove_tick_callback(state->scroll_container,
                                            state->tick_id);
        g_object_weak_unref(G_OBJECT(state->scroll_container),
                            on_scroll_container_weak_notify, state);
    }
    if (state->columns)
        g_ptr_array_unref(state->columns);
    g_free(state);
}

/* ── Animation helpers ──────────────────────────────────────────── */

static double
exp_smooth_toward(double current, double target, double lambda, double dt_sec)
{
    double delta = target - current;
    double delta_abs = delta < 0.0 ? -delta : delta;
    double factor = CLAMP(lambda * dt_sec, 0.0, 1.0);

    if (delta_abs < STRIP_ANIM_EPSILON)
        return target;

    return current + delta * factor;
}

static double
workspace_strip_tick_dt_sec(WorkspaceStripState *state,
                            GdkFrameClock *frame_clock)
{
    gint64 now_usec;
    double dt_sec;

    if (!state)
        return STRIP_DEFAULT_DT_SEC;

    if (!frame_clock) {
        state->last_tick_usec = 0;
        return STRIP_DEFAULT_DT_SEC;
    }

    now_usec = gdk_frame_clock_get_frame_time(frame_clock);
    if (state->last_tick_usec <= 0 || now_usec <= state->last_tick_usec) {
        state->last_tick_usec = now_usec;
        return STRIP_DEFAULT_DT_SEC;
    }

    dt_sec = (double)(now_usec - state->last_tick_usec) / 1000000.0;
    state->last_tick_usec = now_usec;
    return CLAMP(dt_sec, STRIP_MIN_DT_SEC, STRIP_MAX_DT_SEC);
}

static void
workspace_strip_animation_rates(const WorkspaceStripState *state,
                                double *camera_lambda,
                                double *width_lambda)
{
    double cam = STRIP_CAMERA_LAMBDA_DEFAULT;
    double width = STRIP_WIDTH_LAMBDA_DEFAULT;

    if (state) {
        switch (state->anim_profile) {
        case WORKSPACE_STRIP_ANIM_INSERT:
            cam = STRIP_CAMERA_LAMBDA_INSERT;
            width = STRIP_WIDTH_LAMBDA_INSERT;
            break;
        case WORKSPACE_STRIP_ANIM_REMOVE:
            cam = STRIP_CAMERA_LAMBDA_REMOVE;
            width = STRIP_WIDTH_LAMBDA_REMOVE;
            break;
        case WORKSPACE_STRIP_ANIM_MAXIMIZE:
            cam = STRIP_CAMERA_LAMBDA_MAXIMIZE;
            width = STRIP_WIDTH_LAMBDA_MAXIMIZE;
            break;
        case WORKSPACE_STRIP_ANIM_DEFAULT:
        default:
            break;
        }
    }

    if (camera_lambda)
        *camera_lambda = cam;
    if (width_lambda)
        *width_lambda = width;
}

static void
apply_camera(WorkspaceStripState *state)
{
    if (!state->scroll_container)
        return;
    GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment(
        GTK_SCROLLED_WINDOW(state->scroll_container));
    if (hadj)
        gtk_adjustment_set_value(hadj, state->camera_x);
}

static void
apply_column_widths(WorkspaceStripState *state)
{
    for (guint i = 0; i < state->columns->len; i++) {
        WorkspaceColumn *col = g_ptr_array_index(state->columns, i);
        int w = (int)(col->current_width + 0.5);
        if (col->notebook)
            gtk_widget_set_size_request(col->notebook, w, -1);
    }
}

static void
ensure_tick_running(Workspace *ws)
{
    WorkspaceStripState *state = ws->strip_state;
    if (!state || state->tick_id || !state->scroll_container)
        return;
    state->last_tick_usec = 0;
    state->tick_id = gtk_widget_add_tick_callback(
        state->scroll_container, workspace_strip_tick_cb, ws, NULL);
}

static int
workspace_strip_find_column_for_notebook(Workspace *ws, GtkWidget *notebook)
{
    WorkspaceStripState *state;

    if (!ws || !ws->strip_state || !notebook)
        return -1;

    state = ws->strip_state;
    for (guint i = 0; i < state->columns->len; i++) {
        WorkspaceColumn *col = g_ptr_array_index(state->columns, i);
        if (col->notebook == notebook)
            return (int)i;
    }

    return -1;
}

static gboolean
workspace_strip_resolve_focused_col(Workspace *ws, int *col_idx_out)
{
    WorkspaceStripState *state;
    int col_idx;

    if (!ws || !ws->strip_state || !col_idx_out)
        return FALSE;

    state = ws->strip_state;
    col_idx = state->focused_col;
    if (col_idx >= 0 && col_idx < (int)state->columns->len) {
        *col_idx_out = col_idx;
        return TRUE;
    }

    if (ws->active_pane) {
        col_idx = workspace_strip_find_column_for_notebook(
            ws, GTK_WIDGET(ws->active_pane));
        if (col_idx >= 0) {
            *col_idx_out = col_idx;
            return TRUE;
        }
    }

    if (state->columns->len == 0)
        return FALSE;

    *col_idx_out = 0;
    return TRUE;
}

static void
workspace_strip_set_anim_profile(WorkspaceStripState *state,
                                 WorkspaceStripAnimProfile profile)
{
    if (!state)
        return;
    state->anim_profile = profile;
}

static void
workspace_strip_settle_after_remove(WorkspaceStripState *state,
                                    int focus_col,
                                    double removed_width)
{
    WorkspaceColumn *focused;
    double settle_delta;
    double min_width;

    if (!state || focus_col < 0 || focus_col >= (int)state->columns->len)
        return;

    focused = g_ptr_array_index(state->columns, focus_col);
    if (!focused)
        return;

    if (removed_width <= 0.0)
        removed_width = (double)STRIP_DEFAULT_COL_WIDTH;

    settle_delta = MIN(removed_width * 0.2, STRIP_REMOVE_SETTLE_MAX_DELTA);
    min_width = (double)(STRIP_DEFAULT_COL_WIDTH / 2);
    focused->current_width = MAX(focused->current_width - settle_delta,
                                 min_width);
}

/* ── Tick callback ──────────────────────────────────────────────── */

gboolean
workspace_strip_tick_cb(GtkWidget *widget, GdkFrameClock *frame_clock,
                        gpointer user_data)
{
    (void)widget;
    Workspace *ws = user_data;
    if (!ws || !ws->strip_state)
        return G_SOURCE_REMOVE;

    WorkspaceStripState *state = ws->strip_state;
    gboolean still_moving = FALSE;
    double camera_lambda;
    double width_lambda;
    double dt_sec = workspace_strip_tick_dt_sec(state, frame_clock);

    workspace_strip_animation_rates(state, &camera_lambda, &width_lambda);

    double new_cam = exp_smooth_toward(state->camera_x,
                                       state->camera_target_x,
                                       camera_lambda,
                                       dt_sec);
    if (new_cam != state->camera_x) {
        state->camera_x = new_cam;
        still_moving = TRUE;
    }

    for (guint i = 0; i < state->columns->len; i++) {
        WorkspaceColumn *col = g_ptr_array_index(state->columns, i);
        double new_w = exp_smooth_toward(col->current_width,
                                         (double)col->target_width,
                                         width_lambda,
                                         dt_sec);
        if (new_w != col->current_width) {
            col->current_width = new_w;
            still_moving = TRUE;
        }
    }

    apply_column_widths(state);
    apply_camera(state);

    if (!still_moving) {
        state->tick_id = 0;
        state->last_tick_usec = 0;
        state->anim_profile = WORKSPACE_STRIP_ANIM_DEFAULT;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* ── Camera ─────────────────────────────────────────────────────── */

void
workspace_strip_clamp_camera(WorkspaceStripState *state, int viewport_width)
{
    if (!state || state->columns->len == 0)
        return;

    double total_width = 0;
    for (guint i = 0; i < state->columns->len; i++) {
        WorkspaceColumn *col = g_ptr_array_index(state->columns, i);
        total_width += (double)col->target_width;
    }

    double max_scroll = total_width - (double)viewport_width;
    if (max_scroll < 0)
        max_scroll = 0;

    if (state->camera_target_x < 0)
        state->camera_target_x = 0;
    if (state->camera_target_x > max_scroll)
        state->camera_target_x = max_scroll;
}

void
workspace_strip_pan_to_focused_column(Workspace *ws)
{
    if (!ws || !ws->strip_state)
        return;

    WorkspaceStripState *state = ws->strip_state;
    if (state->focused_col < 0 ||
        state->focused_col >= (int)state->columns->len)
        return;

    double col_left = 0;
    for (int i = 0; i < state->focused_col; i++) {
        WorkspaceColumn *c = g_ptr_array_index(state->columns, i);
        col_left += (double)c->target_width;
    }
    WorkspaceColumn *focused = g_ptr_array_index(state->columns,
                                                  state->focused_col);
    double col_center = col_left + (double)focused->target_width / 2.0;

    int viewport_width = 0;
    if (state->scroll_container)
        viewport_width = gtk_widget_get_width(state->scroll_container);
    if (viewport_width <= 0)
        viewport_width = STRIP_DEFAULT_COL_WIDTH;

    state->camera_target_x = col_center - (double)viewport_width / 2.0;
    workspace_strip_clamp_camera(state, viewport_width);
    ensure_tick_running(ws);
}

/* ── Column focus ───────────────────────────────────────────────── */

void
workspace_strip_focus_column(Workspace *ws, int col_idx)
{
    if (!ws || !ws->strip_state)
        return;

    WorkspaceStripState *state = ws->strip_state;
    if (col_idx < 0 || col_idx >= (int)state->columns->len)
        return;

    state->focused_col = col_idx;

    WorkspaceColumn *col = g_ptr_array_index(state->columns, col_idx);
    if (col->notebook && GTK_IS_NOTEBOOK(col->notebook))
        ws->active_pane = GTK_NOTEBOOK(col->notebook);

    workspace_strip_pan_to_focused_column(ws);
}

static void
on_column_notebook_focus_enter(GtkEventControllerFocus *ctrl,
                               gpointer user_data)
{
    (void)ctrl;
    GtkWidget *notebook = user_data;

    Workspace *ws = g_object_get_data(G_OBJECT(notebook), "workspace-ptr");
    if (!ws || !ws->strip_state)
        return;

    WorkspaceStripState *state = ws->strip_state;
    for (guint i = 0; i < state->columns->len; i++) {
        WorkspaceColumn *col = g_ptr_array_index(state->columns, i);
        if (col->notebook == notebook) {
            workspace_strip_focus_column(ws, (int)i);
            break;
        }
    }
}

/* ── Init ───────────────────────────────────────────────────────── */

void
workspace_strip_init(Workspace *ws)
{
    if (!ws)
        return;
    if (ws->strip_state)
        workspace_strip_state_free(ws->strip_state);
    ws->strip_state = workspace_strip_state_new();
    ws->layout_mode = WORKSPACE_LAYOUT_STRIP;
}

/* ── Apply layout ───────────────────────────────────────────────── */

void
workspace_strip_apply_layout(Workspace *ws)
{
    if (!ws || !ws->strip_state)
        return;

    WorkspaceStripState *state = ws->strip_state;
    int viewport_width = 0;
    if (state->scroll_container)
        viewport_width = gtk_widget_get_width(state->scroll_container);
    if (viewport_width <= 0)
        viewport_width = STRIP_DEFAULT_COL_WIDTH;

    for (guint i = 0; i < state->columns->len; i++) {
        WorkspaceColumn *col = g_ptr_array_index(state->columns, i);
        if (col->maximized)
            col->target_width = viewport_width;
        else if (col->target_width <= 0)
            col->target_width = STRIP_DEFAULT_COL_WIDTH;
        if (col->current_width <= 0)
            col->current_width = col->target_width;
    }

    apply_column_widths(state);
    workspace_strip_pan_to_focused_column(ws);
}

/* ── Create root widget ─────────────────────────────────────────── */

static void
wire_column_focus_tracking(GtkWidget *notebook)
{
    GtkEventController *focus_ctrl = gtk_event_controller_focus_new();
    g_signal_connect(focus_ctrl, "enter",
                     G_CALLBACK(on_column_notebook_focus_enter), notebook);
    gtk_widget_add_controller(notebook, focus_ctrl);
}

static void
on_hadjustment_page_size_changed(GObject *adj, GParamSpec *pspec,
                                 gpointer user_data)
{
    (void)adj;
    (void)pspec;
    Workspace *ws = user_data;
    if (ws && ws->strip_state)
        workspace_strip_apply_layout(ws);
}

GtkWidget *
workspace_strip_create_root(Workspace *ws, GtkWidget *first_notebook)
{
    if (!ws)
        return first_notebook;

    workspace_strip_init(ws);
    WorkspaceStripState *state = ws->strip_state;

    state->scroll_container = gtk_scrolled_window_new();
    g_object_weak_ref(G_OBJECT(state->scroll_container),
                      on_scroll_container_weak_notify, state);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(state->scroll_container),
        GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
    gtk_widget_set_hexpand(state->scroll_container, TRUE);
    gtk_widget_set_vexpand(state->scroll_container, TRUE);
    gtk_widget_set_overflow(state->scroll_container, GTK_OVERFLOW_HIDDEN);

    GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment(
        GTK_SCROLLED_WINDOW(state->scroll_container));
    if (hadj)
        g_signal_connect(hadj, "notify::page-size",
                         G_CALLBACK(on_hadjustment_page_size_changed), ws);

    state->column_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(state->column_box, TRUE);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(state->scroll_container), state->column_box);

    WorkspaceColumn *col = g_new0(WorkspaceColumn, 1);
    col->notebook = first_notebook;
    col->target_width = STRIP_DEFAULT_COL_WIDTH;
    col->current_width = STRIP_DEFAULT_COL_WIDTH;
    g_ptr_array_add(state->columns, col);

    g_object_set_data(G_OBJECT(first_notebook), "workspace-ptr", ws);
    gtk_widget_set_vexpand(first_notebook, TRUE);
    gtk_widget_set_size_request(first_notebook, STRIP_DEFAULT_COL_WIDTH, -1);
    gtk_box_append(GTK_BOX(state->column_box), first_notebook);

    wire_column_focus_tracking(first_notebook);
    workspace_strip_apply_layout(ws);

    return state->scroll_container;
}

/* ── Add existing notebook as a new column ─────────────────────── */

void
workspace_strip_add_notebook_column(Workspace *ws, GtkWidget *notebook)
{
    if (!ws || !ws->strip_state || !notebook)
        return;

    WorkspaceStripState *state = ws->strip_state;

    WorkspaceColumn *col = g_new0(WorkspaceColumn, 1);
    col->notebook = notebook;
    col->target_width = STRIP_DEFAULT_COL_WIDTH;
    col->current_width = STRIP_DEFAULT_COL_WIDTH;
    g_ptr_array_add(state->columns, col);

    g_object_set_data(G_OBJECT(notebook), "workspace-ptr", ws);
    gtk_widget_set_vexpand(notebook, TRUE);
    gtk_widget_set_size_request(notebook, STRIP_DEFAULT_COL_WIDTH, -1);

    if (state->column_box)
        gtk_box_append(GTK_BOX(state->column_box), notebook);

    wire_column_focus_tracking(notebook);
}

gboolean
workspace_strip_insert_column_after_active(Workspace *ws, GtkWidget *notebook)
{
    WorkspaceStripState *state;
    WorkspaceColumn *new_col;
    int active_col = 0;
    int insert_col;
    GtkWidget *after_widget = NULL;

    if (!ws || !ws->strip_state || !notebook)
        return FALSE;

    state = ws->strip_state;
    if (workspace_strip_resolve_focused_col(ws, &active_col)) {
        insert_col = active_col + 1;
        if (insert_col > (int)state->columns->len)
            insert_col = (int)state->columns->len;
        if (active_col >= 0 && active_col < (int)state->columns->len) {
            WorkspaceColumn *active = g_ptr_array_index(state->columns, active_col);
            after_widget = active ? active->notebook : NULL;
        }
    } else {
        insert_col = 0;
    }

    new_col = g_new0(WorkspaceColumn, 1);
    new_col->notebook = notebook;
    new_col->target_width = STRIP_DEFAULT_COL_WIDTH;
    new_col->current_width = STRIP_INSERT_START_WIDTH;

    g_ptr_array_insert(state->columns, insert_col, new_col);

    g_object_set_data(G_OBJECT(notebook), "workspace-ptr", ws);
    gtk_widget_set_vexpand(notebook, TRUE);
    gtk_widget_set_size_request(notebook, STRIP_DEFAULT_COL_WIDTH, -1);

    if (state->column_box) {
        gtk_box_insert_child_after(GTK_BOX(state->column_box),
                                   notebook, after_widget);
    }

    wire_column_focus_tracking(notebook);

    workspace_strip_set_anim_profile(state, WORKSPACE_STRIP_ANIM_INSERT);
    workspace_strip_focus_column(ws, insert_col);
    workspace_strip_apply_layout(ws);
    return TRUE;
}

gboolean
workspace_strip_remove_active_column(Workspace *ws)
{
    WorkspaceStripState *state;
    WorkspaceColumn *col;
    GtkWidget *notebook;
    double removed_width;
    int remove_col = 0;

    if (!ws || !ws->strip_state)
        return FALSE;

    state = ws->strip_state;
    if (state->columns->len <= 1)
        return FALSE;
    if (!workspace_strip_resolve_focused_col(ws, &remove_col))
        return FALSE;

    col = g_ptr_array_index(state->columns, remove_col);
    notebook = col ? col->notebook : NULL;
    removed_width = col
        ? ((col->current_width > 0.0) ? col->current_width
                                      : (double)col->target_width)
        : 0.0;
    if (notebook && state->column_box &&
        gtk_widget_get_parent(notebook) == state->column_box) {
        gtk_box_remove(GTK_BOX(state->column_box), notebook);
    }

    g_ptr_array_remove_index(state->columns, remove_col);

    if (remove_col >= (int)state->columns->len)
        remove_col = (int)state->columns->len - 1;
    state->focused_col = remove_col;

    if (state->focused_col >= 0 &&
        state->focused_col < (int)state->columns->len) {
        WorkspaceColumn *focused = g_ptr_array_index(state->columns,
                                                      state->focused_col);
        if (focused && focused->notebook && GTK_IS_NOTEBOOK(focused->notebook))
            ws->active_pane = GTK_NOTEBOOK(focused->notebook);
    }

    workspace_strip_settle_after_remove(state, state->focused_col,
                                        removed_width);
    workspace_strip_set_anim_profile(state, WORKSPACE_STRIP_ANIM_REMOVE);
    workspace_strip_apply_layout(ws);
    return TRUE;
}

/* ── Focus primary ──────────────────────────────────────────────── */

void
workspace_strip_focus_primary(Workspace *ws)
{
    if (!ws || !ws->strip_state)
        return;

    workspace_strip_focus_column(ws, ws->strip_state->focused_col);

    WorkspaceStripState *state = ws->strip_state;
    if (state->focused_col >= 0 &&
        state->focused_col < (int)state->columns->len) {
        WorkspaceColumn *col = g_ptr_array_index(state->columns,
                                                  state->focused_col);
        if (col->notebook && GTK_IS_NOTEBOOK(col->notebook) &&
            workspace_focus_pane(ws, GTK_NOTEBOOK(col->notebook)))
            return;
    }

    workspace_focus_first_terminal(ws);
}

/* ── Toggle zoom (maximize / unmaximize active column) ─────────── */

void
workspace_strip_toggle_maximize_column(Workspace *ws)
{
    if (!ws || !ws->strip_state)
        return;

    WorkspaceStripState *state = ws->strip_state;
    if (state->focused_col < 0 ||
        state->focused_col >= (int)state->columns->len)
        return;

    WorkspaceColumn *col = g_ptr_array_index(state->columns,
                                              state->focused_col);
    col->maximized = !col->maximized;

    int viewport_width = 0;
    if (state->scroll_container)
        viewport_width = gtk_widget_get_width(state->scroll_container);
    if (viewport_width <= 0)
        viewport_width = STRIP_DEFAULT_COL_WIDTH;

    if (col->maximized)
        col->target_width = viewport_width;
    else
        col->target_width = STRIP_DEFAULT_COL_WIDTH;

    workspace_strip_set_anim_profile(state, WORKSPACE_STRIP_ANIM_MAXIMIZE);
    workspace_strip_pan_to_focused_column(ws);
    ensure_tick_running(ws);
}

void
workspace_strip_toggle_zoom(Workspace *ws)
{
    workspace_strip_toggle_maximize_column(ws);
}
