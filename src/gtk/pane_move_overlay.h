/*
 * pane_move_overlay.h - Overlay for moving tabs and workspaces
 *
 * Shows a searchable list of destination panes across workspaces and
 * destination windows/instances for workspace moves.
 */
#pragma once

#include <gtk/gtk.h>

void pane_move_overlay_toggle(GtkOverlay *overlay,
                              GtkWidget *terminal_stack,
                              GtkWidget *workspace_list);
void pane_move_overlay_toggle_workspace_targets(GtkOverlay *overlay,
                                                GtkWidget *terminal_stack,
                                                GtkWidget *workspace_list,
                                                int source_workspace_idx);
