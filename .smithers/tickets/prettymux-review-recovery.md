# PrettyMux Smithers Review Recovery

Context:
- repo: `/home/pe/newnewrepos/w/yo/prettymux`
- workflow: `.smithers/workflows/prettymux-strip-layout.tsx`
- run observed: `404dde59-1a02-483f-9a06-d89775cc1661`

This document maps the failed reviews to concrete fixes so the workflow can be rerun with a narrower, approval-oriented patch set instead of continuing to age out on review.

## Recovery 0: Verification Preflight

Purpose:
- prove the container verification path still works before touching unresolved review debt

Allowed ownership:
- `packaging/containers/debian-bookworm.Dockerfile`
- `packaging/containers/verify-strip-layout.sh`

Out of scope:
- any runtime/application code under `src/gtk/*`
- any session/socket/layout/sidebar fixes
- any opportunistic cleanup outside the two allowed container-verification files

Important rule:
- if the existing verification path already works, prefer a no-op implementation and return only the exact proof commands/results

Required proof:
1. host Wayland path:
   - `bash packaging/containers/verify-strip-layout.sh`
2. Weston fallback path:
   - `env -u WAYLAND_DISPLAY -u XDG_RUNTIME_DIR bash packaging/containers/verify-strip-layout.sh`

Approval bar:
- no files outside the allowed ownership set are changed
- both verification commands are reported explicitly
- the summary states whether each path launched a live PrettyMux instance and whether `prettymux-open` reached it
- if a fix is required, it stays strictly inside the allowed container-verification files

## Current Status

Approved:
- `phase0`
- `phase1b`
- `phase3`
- `phase3b`
- `phase5b`
- `phase6`
- `phase7`
- `phase8`
- `phase9`
- `phase10`
- `phase10b`

Not approved:
- `phase1`
- `phase2`
- `phase4` (currently in progress, all 3 reviews rejected so far)
- `phase5` (2 reviews rejected so far)
- `phase6b` (3 reviews rejected so far)
- `phase8b` (3 reviews rejected so far)

Important point:
- The workflow is making real progress now, but earlier phases advanced with rejected reviews. The cleanups below are what would be needed to make those phases actually approvable.

## Cross-Cutting Problems

### 1. Phase scope drift

The biggest repeated reason for rejection is not code quality by itself. It is that a phase claims ownership of a small file set, then the implementation touches broad unrelated files.

Evidence from the current worktree:
- `src/gtk/app_actions.c`
- `src/gtk/prettymux-open.c`
- `src/gtk/session.c`
- `src/gtk/socket_commands.c`
- `src/gtk/workspace.c`
- `src/gtk/workspace_strip.c`
- `src/gtk/workspace_layout.c`
- `src/gtk/sidebar_ui.c`
- `src/gtk/theme.c`
- `packaging/containers/*`

When Phase 1 or Phase 2 is under review, this size of diff makes the submission effectively non-reviewable as a phase-scoped change.

What to do:
- build each phase as a narrow patch stack, not one evolving mega-diff
- for reruns, reset the phase branch or use per-phase worktrees/cherry-picks
- only keep files owned by that phase plus the minimum test/build wiring required

### 2. Generic smoke verification is not enough

The Docker + Wayland path is now real and approved in `phase0`, which is good. But user-visible phases still need evidence tied to the actual behavior being reviewed.

What to do:
- every user-visible phase must include:
  - exact command list
  - exact `prettymux-open` calls
  - exact assertions or expected failures
- if the behavior is not scriptable through `prettymux-open`, add the smallest possible query/action surface to make it scriptable

### 3. Tests need to exercise the real integration path

The reviewer repeatedly rejected helper-hook tests where the real production path remained untested.

What to do:
- prefer testing the public integration path over internal helper hooks
- for session work, test `session_save()` / `session_restore()`, not helper shims
- for runtime layout activation, test the rebuild/activation path that the app actually uses

### 4. Runtime behavior must fail explicitly

Some rejections are not about missing features. They are about bad failure semantics:
- generic error instead of explicit unsupported-action message
- false success on invalid targets
- success masking when verification is weak

What to do:
- make unsupported or invalid operations return explicit, layout-aware errors
- assert those exact errors in tests and `prettymux-open` smoke commands

## Phase 1 Recovery: Sidebar Card UI

Observed review failures:
- phase ownership violated
- async/sidebar data work bundled into the UI phase
- missing validation of preserved interactions

Files Phase 1 is allowed to own:
- `src/gtk/sidebar_ui.c`
- `src/gtk/sidebar_ui.h`
- `src/gtk/theme.c`

What the phase should contain:
- visible card layout only
- bold title row
- smaller metadata row styling
- optional attention badge container
- optional compact structural hint container
- inline rename/selection/drag/close behavior preserved

What must be removed from the Phase 1 diff:
- all `workspace_get_sidebar_*` helpers
- async branch detection changes
- layout mode / strip state additions
- `workspace.c` / `workspace.h` metadata refactors
- socket/session/container changes

Concrete fixes:
1. Rebuild Phase 1 as a pure UI-only patch.
2. Keep the new card builder isolated in `sidebar_ui.c/.h`.
3. Keep styling isolated in `theme.c`.
4. Preserve existing callback wiring instead of refactoring ownership or data flow in this phase.
5. Add focused validation for preserved interactions:
   - row selection still works
   - inline rename still enters edit mode and commits/cancels correctly
   - close/delete control still removes the workspace
   - drag/reorder still works

Recommended validation:
- `meson compile -C builddir`
- `meson test -C builddir --print-errorlogs`
- a focused GTK-side regression test if possible for row behavior
- Docker + Wayland live launch proof for the app
- if `prettymux-open` cannot exercise rename/drag directly, explicitly say so and add a local GTK test rather than pretending a generic smoke test covers it

Approval bar:
- the diff must look obviously phase-scoped
- no `workspace.c` correctness cleanup unless absolutely necessary for rendering

## Phase 2 Recovery: Layout Backend Split

Observed review failures:
- backend split APIs not wired into real runtime paths
- phase included Phase 3/4 behavior and Phase 1 UI changes
- strip lifecycle cleanup bug
- classic-only zoom assumptions remained

Files Phase 2 is allowed to own:
- `src/gtk/workspace.h`
- `src/gtk/workspace.c`
- `src/gtk/workspace_layout.h`
- `src/gtk/workspace_layout.c`
- `src/gtk/workspace_strip.h`
- `src/gtk/workspace_strip.c`

What the phase should contain:
- `WorkspaceLayoutMode`
- `WorkspaceStripState`
- `WorkspaceColumn`
- backend API surface
- runtime wiring for the backend boundary
- no meaningful new user-visible strip behavior yet

What must be removed from the Phase 2 diff:
- sidebar card rendering changes
- sidebar metadata correctness work
- full strip camera/polish work beyond the minimal scaffold
- action semantics that belong to Phase 4

Concrete fixes:
1. Wire `workspace_layout_create_root()` and `workspace_layout_focus_primary()` into the actual runtime path, not just tests.
2. Route `pane.zoom` through `workspace_layout_toggle_zoom_current()` everywhere.
3. Route layout-sensitive split/close paths through the layout dispatcher where practical.
4. Free strip lifecycle state before widget teardown:
   - fix `workspace_remove()` ordering so strip tick state cannot dereference destroyed widgets.
5. Keep classic mode behavior unchanged.

Specific rejected issues to close:
- `pane.zoom` must not call `workspace_toggle_zoom()` directly in layout-aware paths.
- `workspace_split_pane_target()` and `workspace_close_pane()` must not hardcode classic-only zoom assumptions.
- `workspace_remove()` must not destroy the strip container before strip cleanup runs.

Recommended validation:
- clean Meson build
- targeted tests:
  - `workspace-layout`
  - `workspace-strip`
- a runtime smoke that proves the app still starts in classic mode and the backend split is actually present in production code

Approval bar:
- no sidebar UI changes in this phase
- no full strip feature semantics in this phase
- backend boundary visible in production code, not only in tests

## Phase 4 Recovery: Actions + Ctrl+Shift+Z

Observed review failures:
- initially: action behavior not production-ready
- currently known structured blocker:
  - unsupported strip split action is not reported explicitly

Additional user-reported strip-mode UX debt to close in the recovery run:
- `Ctrl+Shift+O` vertical split in strip mode currently behaves inconsistently / appears broken
- `Ctrl+Tab` / `Ctrl+Shift+Tab` semantics in strip mode are confusing
- pane focus traversal in strip mode can skip a pane/column

Design decision for the recovery run:
- keep multiple tabs inside a strip column
- align strip-mode shortcuts and split behavior with Séance where they fit cleanly:
  - `Ctrl+Tab` / `Ctrl+Shift+Tab` switch tabs inside the active strip column
  - `Ctrl+Shift+Left/Right/Up/Down` move pane focus directionally
  - `Ctrl+Shift+Enter` creates a new horizontal column
- vertical split in strip mode must be supported for real:
  - not as a classic arbitrary paned tree
  - but as strip-compatible vertical stacking inside the active column
- horizontal split/new-column behavior must also be normalized to the chosen Séance-style shortcut scheme

Structured issue from the DB:
- file: `src/gtk/app_actions.c:426`
- description:
  - strip split and focus semantics are still inconsistent across direct actions, shortcuts, and automation
  - earlier recovery logic treated vertical split as unsupported, but product direction now requires real strip-compatible vertical support

What to fix:
1. Implement real vertical split support in strip mode as vertical stacking inside the active column.
2. Normalize horizontal split/new-column behavior to the Séance-style model.
3. Keep tab semantics coherent in strip mode:
   - `Ctrl+Tab` / `Ctrl+Shift+Tab` switch tabs in the active strip column
4. Keep pane semantics coherent in strip mode:
   - `Ctrl+Shift+Left/Right/Up/Down` move focus directionally
   - directional focus must not skip a pane/column
5. Ensure direct action dispatch, socket command handling, and `prettymux-open --action ... --non-interactive` all use the same strip split/focus semantics.
6. Add tests and live verification for:
   - horizontal split/new column
   - vertical split inside the active strip column
   - tab-next / tab-prev inside strip columns
   - deterministic directional pane focus without skips

Likely files:
- `src/gtk/app_actions.c`
- `src/gtk/socket_commands.c`
- `src/gtk/prettymux-open.c`
- `src/gtk/test_workspace_phase4_actions.c`
- `packaging/containers/verify-strip-layout.sh`

Recommended validation:
- `meson test -C builddir workspace-phase4-actions --print-errorlogs`
- live container command that exercises both horizontal and vertical strip split through `prettymux-open`

Approval bar:
- classic `Ctrl+Shift+Z` remains unchanged
- strip `Ctrl+Shift+Z` maximize/unmaximize works
- strip horizontal split/new-column behavior is coherent and scriptable
- strip vertical split is supported and scriptable
- strip-mode tab shortcuts stay tab-scoped
- strip-mode directional pane navigation is deterministic and does not skip panes/columns

## Phase 5 Recovery: Session + Settings

Observed review failures:
- no automated coverage for real strip session persistence
- missing Docker + Wayland + live `prettymux-open` evidence
- maximize restore lost state
- helper-hook tests instead of real integration tests
- no scriptable query surface for restored strip state

Additional user-reported layout-mode consistency debt to close in the recovery run:
- switching to strip mode currently affects only the current workspace
- the expected recovery behavior is:
  - changing the layout mode from the app settings / default-layout surface should update the default for future workspaces
  - and apply coherently across existing workspaces in the current instance unless an explicit per-workspace override exists

What to fix:
1. Test the real integration path:
   - use `session_save()` / `session_restore()`
   - do not rely on special helper-only test hooks
2. Keep classic-schema safety explicitly covered.
3. Add scriptable strip-state inspection:
   - socket command such as `workspace.get_strip_state`
   - `prettymux-open --get-strip-state`
4. Assert through automation:
   - layout mode
   - focused column
   - widths
   - maximize state
5. Ensure restore does not drop per-column maximize flags.
6. Make layout-mode switching semantics explicit and coherent:
   - if the product keeps layout mode as a global/current-instance preference, switching it must update other workspaces too
   - if the product keeps per-workspace overrides, that must be explicit in UI and tests; silent “current workspace only” behavior is not acceptable

Likely files:
- `src/gtk/session.c`
- `src/gtk/prettymux-open.c`
- `src/gtk/socket_commands.c`
- `src/gtk/test_session_strip_persistence.c`
- `packaging/containers/verify-strip-layout.sh`

Recommended validation:
- dedicated session integration test target
- live Docker restart smoke:
  - create strip layout
  - persist
  - restart
  - query strip state through `prettymux-open --get-strip-state`
  - assert layout mode, column count, widths, focused column, maximize state

Approval bar:
- no state loss on restore
- classic sessions still restore safely
- persistence is automation-verifiable through the preferred socket/CLI boundary

## Phase 5B Recovery: Layout Settings Surface

Observed review history:
- `phase5b` was rejected once, then approved on iteration `1`

Initial rejection reasons:
- no direct automated coverage for the new layout-mode settings path
- missing required Docker + Wayland verification evidence
- missing `prettymux-open` coverage for the new behavior

What was missing in the rejected attempt:
1. Tests for:
   - default fallback to `classic`
   - save/load round-trip for the default layout mode
   - invalid persisted value fallback
2. Concrete user-visible verification evidence:
   - exact Docker/script invocation
   - Wayland mode used
   - proof that PrettyMux launched and `prettymux-open` talked to a live instance
3. Scriptable coverage for the setting behavior:
   - if default layout choice is reflected in live workspace creation, that should be exercised through `prettymux-open`
   - if not directly scriptable, the summary must say so explicitly and point to the GTK/settings-layer tests

Why this matters:
- `phase5b` is now approved, so this is no longer an active blocker
- but the rejection pattern is useful for later settings/UI phases:
  - user-visible settings work still needs both direct tests and concrete live verification evidence

What to preserve from the approved version:
- neutral UI wording:
  - `Classic (split panes)`
  - `Strip (horizontal columns)`
- default remains `Classic`
- no implication that strip is replacing classic

## Phase 6 Recovery: Multi-Instance Runtime Isolation

Observed review history:
- `phase6` was rejected on iteration `0`
- `phase6` was rejected on iteration `1`
- `phase6` was approved on iteration `2`

What is already good:
- build and targeted unit tests pass
- Docker + Wayland multi-instance validation is now real enough to expose actual routing bugs

Current blockers from the review DB:

1. Default remote-command target drifts between instances

- file: `src/gtk/prettymux-open.c:186`
- current behavior:
  - `scan_latest_running_socket()` picks the default target by socket file `st_mtime`
  - in live multi-instance runs, unrelated command traffic changes socket mtimes
  - untargeted `prettymux-open` calls can therefore switch to a different instance over time

Why this blocks approval:
- Phase 6 requires deterministic instance isolation
- a mutable mtime-based default policy is not deterministic

What to do:
- replace mtime-based default resolution with a stable rule
- options are acceptable if they are explicit and deterministic:
  - a recorded default/primary instance file
  - a deterministic oldest-running instance rule
  - explicit env/current-process preference first, then deterministic fallback
- the chosen rule must not drift from ordinary socket traffic

2. Agent CLI uses the same unstable default-instance heuristic

- file: `src/gtk/prettymux_agent_cli.c:196`
- current behavior:
  - `find_socket_path()` mirrors the same mtime-based “latest socket” fallback

What to do:
- unify default instance resolution across:
  - `prettymux-open`
  - agent CLI
  - any other socket clients
- ideally centralize this in one shared helper instead of duplicating policy

3. Explicit env targeting currently fails open

Structured issues from the earlier rejection:
- `src/gtk/prettymux-open.c:229`
- `src/gtk/prettymux_agent_cli.c:174`

Current behavior:
- unreachable `PRETTYMUX_SOCKET` / `PRETTYMUX_INSTANCE_ID` is treated as a soft failure
- code then falls back to scanning `/tmp` and may hit another instance

Why this blocks approval:
- if the caller explicitly targeted an instance, falling through to another instance is unsafe

What to do:
- when explicit env targeting is present and unreachable:
  - fail immediately
  - return a clear error
  - do not scan for a different instance

4. Routed IPC send path does not handle partial writes

- file: `src/gtk/socket_server.c:121`

Current behavior:
- `send_message_to_socket_path()` uses a single `write()` and assumes full payload delivery

Why this matters:
- Unix stream sockets can short-write
- routed JSON can be truncated and produce intermittent routing failures

What to do:
- replace the one-shot `write()` with a full send loop
- handle:
  - short writes
  - `EINTR`
  - closed socket errors cleanly

5. Missing Phase 6 test coverage for instance-aware routing

- file: `src/gtk/meson.build:222`

What is missing:
- direct tests for:
  - `app_state_get_instance_id()`
  - `socket_server_route_command_to_instance()`
  - `prettymux-open --instance`
  - deterministic default-instance resolution under multiple live sockets

What to add:
- explicit-instance routing tests
- invalid explicit target fails-closed tests
- default-target stability tests under traffic
- `--list-instances` tests

6. Docker verification must assert multi-instance isolation semantics

Current status:
- the script is now good enough to expose the default-target drift bug, which is progress
- it should remain strict and assert the intended semantics, not just “two instances launched”

Required live assertions:
- launch two PrettyMux instances
- capture both instance ids
- verify `prettymux-open --list-instances`
- verify `--instance <id>` reaches the intended one
- verify untargeted default resolution stays stable across traffic
- verify dead explicit targets fail with error, not fallback to another instance

Recommended validation:
- `meson compile -C builddir`
- `meson test -C builddir app-state-instance socket-server-route prettymux-open-instance --print-errorlogs`
- `PRETTYMUX_VERIFY_RUN_TESTS=0 PRETTYMUX_VERIFY_STRIP=0 PRETTYMUX_VERIFY_MULTI_INSTANCE=1 packaging/containers/verify-strip-layout.sh`

Approval bar:
- no cross-instance drift
- no mtime-based default ambiguity
- no fail-open behavior for explicit env targets
- routed IPC path is robust against partial writes

What to preserve from the approved version:
- instance-scoped socket paths
- `G_APPLICATION_NON_UNIQUE`
- explicit `instanceId` command routing through `socket_commands.c`
- targeted tests for:
  - `app-state-instance`
  - `socket-server-route`
  - `prettymux-open-instance`

## Phase 6B Recovery: Per-Instance Session Persistence

Observed review history:
- `phase6b` was rejected on iteration `0`
- `phase6b` was rejected on iteration `1`
- `phase6b` was rejected on iteration `2`

Current blockers from the review DB:

1. Instance naming is still not restart-stable

- iteration `0` blocker:
  - `src/gtk/app_state.c:17`
  - default instance id was PID-based, so session file names changed every restart
- iteration `1` blocker:
  - `src/gtk/app_state.c:75`
  - instance selection became occupancy-based (`default`, `default-2`, etc.), which still is not restart-stable

What to do:
- pick a restart-stable per-instance identity model
- it cannot depend on:
  - current PID
  - current live socket occupancy
  - mutable runtime ordering

Acceptable direction:
- generate and persist a stable local window/instance identity token
- reuse that token on restart for the same logical instance/session lane

2. Nested launches still have collision paths

- iteration `0` blocker:
  - `src/gtk/main.c:922`
  - blindly inheriting `PRETTYMUX_INSTANCE_ID` lets a child instance reuse the parent identity
- iteration `2` blocker:
  - `src/gtk/app_state.c:92`
  - nested launches now map to `<parent>-child`, but multiple children from the same parent collide on the same id/session file

What to do:
- nested launch behavior must allocate unique, restart-safe child identities
- never let two concurrent windows share one session file

3. Restore path still allows cross-instance bleed

- iteration `1` blocker:
  - `src/gtk/session.c:117`
  - `session_get_restore_path_for_instance()` can fall back to shared `last.json` even for non-default instances

Why this blocks approval:
- a non-default instance can restore another instance’s state

What to do:
- only allow legacy `last.json` fallback as a one-time migration path for the true default instance
- non-default instances must not silently restore shared legacy state

4. Tests still do not prove per-instance save/restore isolation strongly enough

- iteration `0` blocker:
  - `src/gtk/test_session_strip_persistence.c:34`
  - no direct tests for:
    - `session_get_instance_session_path()`
    - `session_save_for_instance()`
    - `session_restore_for_instance()`
    - multiple instance ids across restart scenarios

What to add:
- tests for:
  - default instance path
  - non-default instance path
  - restart reusing the same intended session lane
  - nested child instances not colliding with each other
  - no fallback from non-default instance to shared `last.json`

5. Required live Docker + Wayland verification evidence is still missing

This appears in all three `phase6b` rejections.

What the review still expects:
- concrete Docker/script command
- concrete Wayland setup
- live PrettyMux launch
- `prettymux-open` commands proving per-instance persistence across restart

Required live assertions:
- launch multiple instances
- save distinguishable state per instance
- restart targeted instance(s)
- verify each instance restores only its own session
- verify nested launches do not overwrite sibling child instance state

Recommended validation:
- `meson compile -C builddir`
- `meson test -C builddir app-state-instance session-strip-persistence socket-server-route prettymux-open-instance prettymux-agent-cli-instance --print-errorlogs`
- live Docker + Wayland script path extended to:
  - launch at least two instances
  - persist different session state in each
  - restart and query restored state via `prettymux-open`

Approval bar:
- per-instance session naming is restart-stable
- nested launches never collide on one session file
- non-default instances do not restore shared legacy state
- live Docker verification proves isolation across restart

## Phase 7 Recovery: Agent Metadata + Status System

Observed review history:
- `phase7` was rejected on iteration `0`
- `phase7` was approved on iteration `1`

Initial rejection reasons:
- missing required live Docker + Wayland + `prettymux-open` verification for the new status feature
- no integration tests for server-side status command handling
- detail-only status notifications could degrade to low-information text

What the approved version proved:
- structured multi-entry workspace status is in place
- compact sidebar status rendering is in place
- notification-path integration is in place
- required verification and test coverage were added

What to preserve from the approved version:
- `workspace.status.set/list/clear` command path
- `sidebar_ui_build_workspace_status_section`
- `notifications_publish_workspace_status`
- server-side command tests, not just client serialization tests

## Phase 8 Recovery: Sidebar Auxiliary Sections

Observed review history:
- `phase8` was rejected on iteration `0`
- `phase8` was approved on iteration `1`

Current blockers from the review DB:

1. Missing required live Docker + Wayland verification evidence

Current issue:
- no concrete runtime proof was provided for this user-visible phase

What to do:
- include:
  - exact Docker/script command
  - Wayland/container setup
  - exact `prettymux-open` commands used against a live instance
- if a specific new sidebar section cannot be driven through `prettymux-open`, say that explicitly and cover it with direct GTK/UI tests instead

2. No direct tests for new sidebar section rendering and suppression behavior

Structured issue:
- file: `src/gtk/sidebar_ui.c`

What is missing:
- UI-level tests for:
  - visibility toggles
  - suppression behavior
  - sanitization/truncation
  - rendering when sections are absent vs present

What to do:
- add direct tests around:
  - `sidebar_ui_build_notification_preview_section`
  - `sidebar_ui_build_branch_cwd_section`
  - `sidebar_ui_build_workspace_status_section`
- assert both:
  - the section appears when data exists
  - the section is suppressed when data should not be shown

Recommended validation:
- `meson compile -C builddir`
- targeted UI/sidebar tests covering section rendering/suppression
- live Docker + Wayland smoke with exact `prettymux-open` commands and expected visible/runtime effects

Approval bar:
- phase-specific live verification evidence is present
- section rendering/suppression behavior is directly tested

What to preserve from the approved version:
- core sidebar sections are implemented and suppressible
- targeted UI section tests are present
- Docker + Wayland live verification with `prettymux-open` was added for this phase

## Phase 8B Recovery: Sidebar Auxiliary Polish / Secondary Sections

Observed review history:
- `phase8b` was rejected on iteration `0`
- `phase8b` was rejected on iteration `1`
- `phase8b` was rejected on iteration `2`

Current blockers from the review DB:

1. Sidebar progress section does not refresh on live progress reports

- iteration `0` blocker:
  - file: `src/gtk/ghostty_actions.c`
  - `GHOSTTY_ACTION_PROGRESS_REPORT` updates progress state and tab labels but does not refresh the sidebar label/card

Why this matters:
- the new progress section is sidebar-driven
- live progress can remain stale or invisible until some unrelated refresh occurs

What to do:
- on progress report:
  - call the sidebar refresh path as well
- add regression coverage proving the sidebar progress section updates when progress events arrive

2. Async git-branch handling still had a real use-after-free

- iteration `1` blocker:
  - file: `src/gtk/workspace.c`
  - ASan reported a use-after-free in async branch callback handling

Why this matters:
- this is a real correctness bug, not just missing coverage

What to do:
- add proper lifetime guard / cancellation around async git branch callback state
- ensure destroyed workspaces cannot be written to by late subprocess callbacks
- keep the fix covered by a regression test if possible

3. Progress section mislabels error/paused states when percent is unknown

- iteration `2` blocker:
  - file: `src/gtk/sidebar_ui.c:321`
  - `progress_percent < 0` is treated as generic in-progress, so error/paused state distinctions disappear

What to do:
- preserve distinct UI/tooltip behavior for:
  - error
  - paused
  - indeterminate active progress
- do not collapse them all into one generic placeholder

4. Required live verification for Phase 8B is still incomplete

This appears across the rejected iterations in slightly different forms.

What is missing:
- either:
  - live `prettymux-open` coverage for the new auxiliary section behaviors
- or:
  - a concrete constraint explanation for why a given auxiliary section cannot be directly script-driven

Specifically called out:
- progress / ports / structure indicators are not explicitly asserted in live verification

What to do:
- if these values can be queried, expose/query them through the socket/CLI boundary
- if they are derived-only UI and not directly scriptable, say so explicitly and back them with direct UI/integration tests

5. Interaction-preservation coverage is still incomplete on the expanded card path

Across iterations, review called out missing regression coverage for:
- rename
- drag/reorder
- selection switching
- close/delete behavior

What to do:
- extend `test_sidebar_ui_sections.c` or equivalent coverage so the expanded card/aux path proves those interactions still work
- especially cover:
  - row activation while renaming
  - drag/drop and reorder behavior
  - close/delete on the wrapped card path

Recommended validation:
- `meson compile -C build-phase8b`
- `meson test -C build-phase8b --print-errorlogs`
- include focused UI/sidebar tests for:
  - progress refresh
  - progress state rendering for paused/error/indeterminate
  - interaction preservation on the new card path
- live Docker + Wayland verification with exact commands and explicit assertions for whatever auxiliary behavior is scriptable

Approval bar:
- no async lifetime bugs in the sidebar metadata path
- progress section refreshes correctly
- progress state rendering is semantically correct
- required user-visible verification evidence is explicit
- interaction-preservation coverage is no longer partial

## Latest Phase Progress After The Earlier Recovery Note

Current run state when this document was updated:
- Smithers run finished

Latest notable review outcomes:
- `phase4`: still not approved
- `phase5`: still not approved
- `phase5b`: approved on iteration `1`
- `phase6`: approved on iteration `2`
- `phase6b`: still not approved
- `phase7`: approved on iteration `1`
- `phase8`: approved on iteration `1`
- `phase8b`: still not approved
- `phase9`: approved on iteration `0`
- `phase10`: approved on iteration `1`
- `phase10b`: approved on iteration `2`

That means the currently relevant unresolved review debt is still:
- `phase1`
- `phase2`
- `phase4`
- `phase5`
- `phase6b`
- `phase8b`

## What Is Already Good And Should Be Preserved

These should not be reworked unless a new blocker appears:
- `phase0` Docker + host-Wayland verification path
- `phase3` strip renderer/camera approval
- `phase3b` strip activation + runtime proof approval
- host-Wayland container launch plus Weston fallback
- live `prettymux-open` interaction against a running instance

## Recommended Recovery Order

1. Fix the narrow Phase 4 blocker first.
   - it is concrete and probably one small patch
2. Finish Phase 5 with real integration tests and `--get-strip-state`.
3. Fix Phase 6 default-target isolation before adding more instance features on top.
4. If you rerun from scratch later, rebuild Phase 1 and Phase 2 as truly scoped patches.
5. Keep every phase branch narrow enough that `git status` does not show cross-phase files during review.

## Practical Branching Recommendation

If Smithers is allowed to keep using one evolving branch, it will keep re-triggering scope complaints.

Better approach:
- one clean worktree or patch stack per phase
- merge or cherry-pick only the approved phase payload
- keep cross-phase files out of scope during review

Minimum discipline if keeping one branch:
- before each phase review, stage only the owned files plus the minimal test/build wiring
- drop or stash unrelated changes
- include the exact validation commands and expected assertions in the summary
