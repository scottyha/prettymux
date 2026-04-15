# PrettyMux Strip Layout + Sidebar Plan

## Goal

Add the best parts of Séance while preserving PrettyMux's current `classic` layout and keeping `Ctrl+Shift+Z` simple.

Key targets:

- richer vertical workspace tabs
- a new `strip` layout mode with horizontally scrolling columns
- focus-driven camera panning
- real maximize state for strip mode
- multi-instance support with per-instance socket routing and per-instance session state
- no regression to current `classic` split-tree behavior

## Legal Reuse

Séance is MIT-licensed. It is legal to reuse code where it makes sense, provided attribution/license notice is preserved in copied material.

Good reuse candidates:

- camera target logic
- target-width / maximize-column logic
- tick-based animation structure
- sidebar hierarchy patterns

Do not force direct reuse where a clean PrettyMux-native implementation is simpler.

## Phase 0: Docker + Wayland Preflight

Files:

- `packaging/containers/debian-bookworm.Dockerfile`
- `packaging/containers/verify-strip-layout.sh`

Deliverable:

- prove the verification environment itself is real before feature work begins

Required behavior:

- for local runs, prefer sharing the host Wayland runtime/socket into the container
- PrettyMux must actually launch inside the container against a live Wayland session
- `prettymux-open --list-workspaces` must succeed against that live instance
- if host Wayland is unavailable, the script may fall back to a headless compositor

Workflow rule:

- this phase gets one iteration only
- if review rejects it, the rest of the workflow must not proceed

## Phase 1: Sidebar Card UI

Files:

- `src/gtk/sidebar_ui.c`
- `src/gtk/sidebar_ui.h`
- `src/gtk/theme.c`

Deliverable:

- visibly upgrade workspace rows into richer summary cards
- preserve inline rename, selection, drag/reorder, and close interactions

Functions/helpers to add or refactor:

- `sidebar_ui_build_workspace_card()` or equivalent card builder

Behavior:

- bold title row
- smaller metadata rows
- optional attention badge
- optional pane/column dots
- the visual card treatment must be obvious in `sidebar_ui.c` and `theme.c`

Scope constraints:

- async branch-safety work is out of scope unless it is directly required to support card rendering
- do not let this phase turn into general workspace metadata correctness cleanup

## Phase 1B: Sidebar Data Correctness

Files:

- `src/gtk/workspace.c`
- `src/gtk/workspace.h`
- `src/gtk/sidebar_ui.c`
- `src/gtk/sidebar_ui.h`

Deliverable:

- provide the stable workspace-summary data needed by the new sidebar cards

Functions/helpers to add or refactor:

- `workspace_get_sidebar_primary_cwd()`
- `workspace_get_sidebar_primary_branch()`
- `workspace_get_sidebar_status_summary()`
- `workspace_get_sidebar_column_count()`
- `workspace_get_sidebar_attention_state()`

Behavior:

- stable primary path derived from first tab of first pane unless a stronger rule is chosen and documented
- if async branch/cwd correctness fixes are needed to make the sidebar data stable, they belong here rather than in Phase 1

## Phase 2: Layout Backend Split

Files:

- `src/gtk/workspace.h`
- `src/gtk/workspace.c`
- new: `src/gtk/workspace_layout.h`
- new: `src/gtk/workspace_layout.c`
- new: `src/gtk/workspace_strip.h`
- new: `src/gtk/workspace_strip.c`

Deliverable:

- introduce explicit workspace layout modes without changing current behavior

Types/state to add:

- `WorkspaceLayoutMode`
- `WorkspaceStripState`
- `WorkspaceColumn`

Core API:

- `workspace_get_layout_mode()`
- `workspace_set_layout_mode()`
- `workspace_layout_create_root()`
- `workspace_layout_focus_primary()`
- `workspace_layout_toggle_zoom_current()`

Constraints:

- `classic` backend must continue using the existing `GtkPaned` tree
- no user-visible behavior changes yet

## Phase 3: Strip Renderer + Camera

Files:

- `src/gtk/workspace_strip.c`
- `src/gtk/workspace_strip.h`

Deliverable:

- first working strip backend core with one notebook per column

Functions to add:

- `workspace_strip_init()`
- `workspace_strip_create_root()`
- `workspace_strip_apply_layout()`
- `workspace_strip_focus_column()`
- `workspace_strip_pan_to_focused_column()`
- `workspace_strip_clamp_camera()`
- `workspace_strip_tick_cb()`

Behavior:

- columns laid out horizontally
- active column kept in view
- target-width and camera animation state driven from model
- no Yoga dependency in the first version
- this phase is backend-focused; runtime activation and container smoke verification belong to Phase 3B

## Phase 3B: Strip Activation + Runtime Verification

Files:

- `src/gtk/workspace.c`
- `src/gtk/socket_commands.c`
- `src/gtk/workspace_strip.c`
- `src/gtk/test_workspace_layout.c`
- `src/gtk/test_workspace_strip.c`
- `packaging/containers/debian-bookworm.Dockerfile`
- `packaging/containers/verify-strip-layout.sh`

Deliverable:

- make strip mode activatable in the real app flow
- prove it works through containerized runtime verification

Required behavior:

- `workspace.set_layout` and any equivalent entrypoints rebuild/activate the strip backend correctly
- focus lands on the real terminal, not a placeholder widget
- container verification launches a real PrettyMux instance and exercises it with `prettymux-open`
- prefer sharing the host Wayland session via mounted socket for local verification
- use headless Weston only as a fallback for isolated CI / no host Wayland

Verification must prove:

- PrettyMux actually launches in the container
- `prettymux-open` talks to a running instance, not just parses CLI flags
- strip mode can be activated and queried against a live instance

## Phase 4: Actions + Ctrl+Shift+Z

Files:

- `src/gtk/app_actions.c`
- `src/gtk/shortcuts.c`
- `src/gtk/workspace.c`
- `src/gtk/workspace_strip.c`

Deliverable:

- current actions work in strip mode with minimal surprises

Functions to add or route:

- `workspace_split_current_for_layout()`
- `workspace_close_current_for_layout()`
- `workspace_focus_next_for_layout()`
- `workspace_focus_prev_for_layout()`
- `workspace_strip_insert_column_after_active()`
- `workspace_strip_remove_active_column()`
- `workspace_strip_toggle_maximize_column()`

Shortcut contract:

- `classic`: keep current `Ctrl+Shift+Z` behavior unchanged
- `strip`: `Ctrl+Shift+Z` toggles maximize/unmaximize of the active column

## Phase 5: Session + Settings

Files:

- `src/gtk/session.c`
- `src/gtk/app_settings.h`
- `src/gtk/app_settings.c`
- `src/gtk/settings_dialog.c`

Deliverable:

- persist layout mode and strip state safely

Functions to add or extend:

- `session_save_workspace_layout_mode()`
- `session_save_workspace_strip_state()`
- `session_restore_workspace_strip_state()`
- `app_settings_get_default_layout_mode()`
- `app_settings_set_default_layout_mode()`

Behavior:

- `classic` sessions continue to use the existing schema
- `strip` sessions store columns, widths, focused column, and maximize state

## Phase 5B: Layout Settings Surface

Files:

- `src/gtk/app_settings.h`
- `src/gtk/app_settings.c`
- `src/gtk/settings_dialog.c`

Deliverable:

- expose the layout-mode setting cleanly in the UI and settings layer

Behavior:

- default remains `classic`
- layout-mode choices are understandable and do not imply strip is the only mode

## Phase 6: Multi-Instance Runtime Isolation

Files:

- `src/gtk/main.c`
- `src/gtk/app_actions.c`
- `src/gtk/socket_commands.c`
- `src/gtk/socket_server.c`
- `src/gtk/session.c`
- `src/gtk/app_state.c`
- any launcher / CLI files that address the socket directly

Deliverable:

- allow more than one PrettyMux instance at the same time
- scope sockets and remote commands per instance

Problems to solve:

- global socket naming causes cross-instance command delivery
- shortcut/action routing must only affect the intended instance
- session restore/save must not use a single shared last-session file when multiple instances are open

Required behavior:

- each running instance gets a stable instance identifier
- each instance owns its own socket path / command target
- socket clients can address:
  - a specific instance
  - or a sensible default / active instance when no target is provided
- no shortcut/action should apply to multiple instances

Suggested API / state:

- `app_state_get_instance_id()`
- `socket_server_get_instance_socket_path()`
- `socket_server_route_command_to_instance()`
- `socket_server_list_instances()`
- `prettymux_open_target_instance()`
- `prettymux_open_list_instances()`
- `prettymux_open_default_instance_resolution()`
- expose new instance-aware actions through `prettymux-open` where that is a sensible automation boundary

## Phase 6B: Per-Instance Session Persistence

Files:

- `src/gtk/session.c`
- `src/gtk/app_state.c`
- `src/gtk/main.c`

Deliverable:

- session save/restore is per instance rather than globally shared

Required behavior:

- each instance uses its own session path / last-session record
- restoring one instance does not overwrite another instance's state
- per-instance session naming survives restarts predictably

Suggested API:

- `session_get_instance_session_path()`
- `session_save_for_instance()`
- `session_restore_for_instance()`
- `session_get_instance_session_path()`
- `session_save_for_instance()`
- `session_restore_for_instance()`

Open design preference:

- keep local in-process shortcuts entirely local
- use socket routing only for explicit remote control / CLI paths
- avoid singleton global app state assumptions

## Phase 7: Agent Metadata + Status System

Files:

- `src/gtk/workspace.c`
- `src/gtk/workspace.h`
- `src/gtk/sidebar_ui.c`
- `src/gtk/sidebar_ui.h`
- `src/gtk/notifications.c`
- `src/gtk/socket_commands.c`
- any CLI / hook integration files that already surface agent state

Deliverable:

- a richer per-workspace metadata model inspired by Séance
- sidebar-visible status lines for agent sessions and recent activity

Required behavior:

- store structured status entries, not only a flat branch/cwd summary
- support multiple agent/status entries per workspace
- surface recent status in the sidebar without making rows unreadable
- integrate cleanly with existing PrettyMux notification behavior

Suggested API:

- `workspace_status_entry`
- `workspace_set_status_entry()`
- `workspace_clear_status_entry()`
- `workspace_get_sorted_status_entries()`
- `sidebar_ui_build_workspace_status_section()`

Scope note:

- do not hardcode only one agent vendor
- keep the model generic enough for Claude, Codex, Pi, or future tools

## Phase 8: Sidebar Expansion To Full Rich Sections

Files:

- `src/gtk/sidebar_ui.c`
- `src/gtk/sidebar_ui.h`
- `src/gtk/theme.c`
- `src/gtk/workspace.c`
- `src/gtk/workspace.h`

Deliverable:

- expand the sidebar cards into a fuller workspace summary system inspired by Séance

Sections to support:

- title row
- attention / unread indicator
- status entries
- recent notification preview
- branch + cwd

Requirements:

- rows must remain compact and scannable
- sections should be configurable / suppressible if they become noisy
- preserve rename, drag/reorder, selection, and close interactions

Suggested API:

- `sidebar_ui_build_notification_preview_section()`
- `sidebar_ui_build_branch_cwd_section()`
- keep this phase focused on the core rich sections above; ports, structure indicators, and progress belong to Phase 8B

## Phase 8B: Sidebar Auxiliary Sections

Files:

- `src/gtk/sidebar_ui.c`
- `src/gtk/sidebar_ui.h`
- `src/gtk/theme.c`
- `src/gtk/workspace.c`
- `src/gtk/workspace.h`

Deliverable:

- extend the richer sidebar cards with the more auxiliary sections inspired by Séance

Sections to support:

- ports summary
- pane / column count indicator
- optional compact progress visualization if there is a clean PrettyMux signal for it

Requirements:

- keep rows compact and suppress noisy sections when data is absent
- preserve rename, drag/reorder, selection, and close interactions

Suggested API:

- `sidebar_ui_build_ports_section()`
- `sidebar_ui_build_progress_section()`
- `sidebar_ui_build_structure_indicator_section()`

## Phase 9: Animation + Detail Pass

Files:

- `src/gtk/workspace_strip.c`
- `src/gtk/sidebar_ui.c`
- `src/gtk/theme.c`
- any focused animation helpers added during earlier phases

Deliverable:

- refine strip mode and sidebar behavior with more polished animation/detail behavior inspired by Séance

Targets:

- smoother camera easing
- cleaner column insertion/removal transitions
- more deliberate maximize/unmaximize transitions
- subtle sidebar row motion or reveal behavior where it adds clarity

Constraints:

- do not add animation that obscures state or harms input responsiveness
- keep the code maintainable and avoid decorative overreach

## Phase 10: Multi-Window Workspace Movement

Files:

- `src/gtk/main.c`
- `src/gtk/app_state.c`
- `src/gtk/app_actions.c`
- `src/gtk/workspace.c`
- `src/gtk/sidebar_ui.c`
- `src/gtk/socket_server.c`
- any window-management modules involved in instance tracking

Deliverable:

- move workspaces between PrettyMux windows/instances in a controlled way at the backend/protocol level

Required behavior:

- user can move a workspace from one window/instance to another
- workspace identity and session data survive the move
- sidebar, notifications, and socket targeting update correctly after the move
- no action bleed between instances

Suggested API:

- `app_state_list_instances()`
- `workspace_detach_from_instance()`
- `workspace_attach_to_instance()`
- `workspace_move_to_instance()`

## Phase 10B: Move Modal Extension For Other Windows

Files:

- `src/gtk/pane_move_overlay.c`
- `src/gtk/sidebar_ui.c`
- any move-target modal/controller code involved today

Deliverable:

- extend the existing move modal so it can target other windows/instances

Required behavior:

- do not create a separate move-to-window dialog
- show other windows/instances as additional move targets in the existing modal
- implement workspace-to-window first; tab/pane-to-window can follow later if serialization is not ready

## Explicit Non-Goals For Initial Rollout

- Yoga integration
- stacked/tabbed hybrid column mode
- replacing all classic-layout code

## Implementation Notes

- Favor additive architecture over rewriting current code.
- Reuse Séance logic where it is clearly valuable, but adapt it to PrettyMux naming and ownership.
- Preserve existing keyboard shortcuts and user expectations.
- For user-visible behavior, prefer features to be reachable through `prettymux-open` when that is a sensible automation boundary.
- Add or extend a Docker-based GUI smoke path for review:
  - run PrettyMux inside a container
  - prefer sharing the existing host Wayland runtime/socket for local verification
  - use a Wayland-capable fallback compositor such as Weston only when host Wayland sharing is unavailable
  - ensure PrettyMux actually launches and stays up
  - exercise new features through `prettymux-open` against a live instance, not just CLI parsing
  - reviewers should treat missing container smoke verification as a real gap for user-visible phases
