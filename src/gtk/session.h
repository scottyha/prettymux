#pragma once

#include <gtk/gtk.h>
#include "ghostty.h"

/* Callback for adding a browser tab during session restore.
 * Allows the caller (main.c) to wire up signal handlers. */
typedef void (*SessionAddBrowserTabFunc)(const char *url);

char *session_get_instance_session_path(const char *instance_id);
gboolean session_exists_for_instance(const char *instance_id);
void session_save_for_instance(const char *instance_id,
                               GtkWindow *window, GtkWidget *browser_notebook,
                               GtkWidget *terminal_stack,
                               GtkWidget *workspace_list);
void session_restore_for_instance(const char *instance_id,
                                  GtkWindow *window,
                                  GtkWidget *browser_notebook,
                                  GtkWidget *terminal_stack,
                                  GtkWidget *workspace_list,
                                  ghostty_app_t ghostty_app,
                                  SessionAddBrowserTabFunc add_browser_tab_func);
void session_set_context(GtkWindow *window, GtkWidget *browser_notebook,
                         GtkWidget *terminal_stack, GtkWidget *workspace_list);
void session_begin_shutdown(void);
void session_queue_save(void);
void session_save(GtkWindow *window, GtkWidget *browser_notebook,
                  GtkWidget *terminal_stack, GtkWidget *workspace_list);
void session_restore(GtkWindow *window, GtkWidget *browser_notebook,
                     GtkWidget *terminal_stack, GtkWidget *workspace_list,
                     ghostty_app_t ghostty_app,
                     SessionAddBrowserTabFunc add_browser_tab_func);
gboolean session_exists(void);

#ifdef PRETTYMUX_TEST_HOOKS
#include "workspace.h"
#include <json-glib/json-glib.h>

void session_test_save_workspace_layout_mode(JsonBuilder *builder,
                                             Workspace *ws);
void session_test_save_workspace_strip_state(JsonBuilder *builder,
                                             Workspace *ws);
void session_test_restore_workspace_strip_state(Workspace *ws,
                                                JsonObject *ws_obj);
#endif
