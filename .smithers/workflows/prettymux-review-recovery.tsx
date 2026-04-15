// smithers-display-name: PrettyMux Review Recovery
/** @jsxImportSource smithers-orchestrator */
import { createSmithers } from "smithers-orchestrator";
import { z } from "zod/v4";
import { agents } from "~/agents";
import { ValidationLoop, implementOutputSchema, validateOutputSchema } from "~/components/ValidationLoop";
import { reviewOutputSchema } from "~/components/Review";

const inputSchema = z.object({
  prompt: z.string().default(
    "Fix the unresolved PrettyMux review debt from .smithers/tickets/prettymux-review-recovery.md without regressing already approved phases.",
  ),
});

const { Workflow, Sequence, smithers } = createSmithers({
  input: inputSchema,
  implement: implementOutputSchema,
  validate: validateOutputSchema,
  review: reviewOutputSchema,
});

function phaseState(ctx: any, idPrefix: string) {
  const allReviews = ctx.outputs.review ?? [];
  const reviews = allReviews.filter((r: any) => {
    const nid: string | undefined = r?.__nodeId ?? r?.nodeId;
    return nid ? nid.startsWith(`${idPrefix}:`) : true;
  });

  const done = reviews.length > 0 && reviews.some((r: any) => r.approved === true);
  const parts: string[] = [];
  for (const review of reviews) {
    if (review.approved === false) {
      parts.push(`REVIEWER REJECTED:\n${review.feedback}`);
      for (const issue of review.issues ?? []) {
        parts.push(
          `  [${issue.severity}] ${issue.title}: ${issue.description}${issue.file ? ` (${issue.file})` : ""}`,
        );
      }
    }
  }
  return { done, feedback: parts.length ? parts.join("\n\n") : null };
}

function preflightState(ctx: any, idPrefix: string) {
  const allReviews = ctx.outputs.review ?? [];
  const reviews = allReviews.filter((r: any) => {
    const nid: string | undefined = r?.__nodeId ?? r?.nodeId;
    return nid ? nid.startsWith(`${idPrefix}:`) : true;
  });
  const anyApproved = reviews.some((r: any) => r.approved === true);
  const anyRejected = reviews.some((r: any) => r.approved === false);
  return {
    done: anyApproved,
    failed: !anyApproved && anyRejected,
    feedback:
      reviews
        .filter((r: any) => r.approved === false)
        .map((r: any) => `REVIEWER REJECTED:\n${r.feedback}`)
        .join("\n\n") || null,
  };
}

const sharedContext = `
SOURCE DOCUMENTS:
- Read .smithers/tickets/prettymux-review-recovery.md first.
- Use .smithers/tickets/prettymux-strip-layout-plan.md only as a supporting reference for original scope/invariants.

MISSION:
- Fix only the unresolved review debt from the finished strip-layout run.
- Preserve already approved phases unless an unresolved blocker explicitly requires touching them.

ALREADY APPROVED / DO NOT REOPEN WITHOUT CAUSE:
- phase0
- phase1b
- phase3
- phase3b
- phase5b
- phase6
- phase7
- phase8
- phase9
- phase10
- phase10b

UNRESOLVED PHASES TO FIX:
- phase1
- phase2
- phase4
- phase5
- phase6b
- phase8b

CROSS-PHASE RULES:
- Keep diffs phase-scoped and reviewable.
- Do not bundle unrelated fixes across phases.
- For user-visible phases, Docker + Wayland live verification is mandatory.
- Use prettymux-open for scriptable behavior whenever sensible.
- If a feature is not reasonably scriptable, say so explicitly and cover it with focused tests instead.
- Do not regress the approved host-Wayland container verification path in packaging/containers/verify-strip-layout.sh.
- For every user-visible phase, the summary must include an explicit "Live verification" section with:
  - the exact command(s) run
  - whether host-Wayland or Weston fallback was used
  - the exact prettymux-open commands/assertions used
  - a one-line statement of what was visibly or programmatically proven
- Saying only "Docker smoke passed" is insufficient and should be treated as incomplete.

IMPLEMENTATION/RERUN HYGIENE:
- Treat .smithers/tickets/prettymux-review-recovery.md as the source of truth for exact unresolved blockers.
- Close the exact structured review issues before adding opportunistic refactors.
- Prefer narrow fixes over sweeping cleanup.
- Preserve classic mode behavior.
- Preserve already approved strip/runtime behavior.
`;

export default smithers((ctx) => {
  const p0 = preflightState(ctx, "recovery0");
  const p1 = phaseState(ctx, "recover-phase1");
  const p2 = phaseState(ctx, "recover-phase2");
  const p4 = phaseState(ctx, "recover-phase4");
  const p5 = phaseState(ctx, "recover-phase5");
  const p6b = phaseState(ctx, "recover-phase6b");
  const p8b = phaseState(ctx, "recover-phase8b");

  return (
    <Workflow name="prettymux-review-recovery">
      <Sequence>
        <Sequence>
          <ValidationLoop
            idPrefix="recovery0"
            prompt={`${sharedContext}

RECOVERY 0: Verification preflight

Read the "Recovery 0: Verification Preflight" section in .smithers/tickets/prettymux-review-recovery.md.

Own these files only if a fix is required:
- packaging/containers/debian-bookworm.Dockerfile
- packaging/containers/verify-strip-layout.sh

Goal:
- prove the approved verification path still works before touching unresolved review debt

Required outcomes:
- local build works
- host-Wayland Docker verification still launches a live PrettyMux instance
- prettymux-open still talks to that live instance
- if host Wayland is unavailable, fallback Weston path still works
- if the current verification path already works, prefer a no-op and report proof only

Hard rules:
- do not modify any file outside the two allowed ownership files
- do not touch src/gtk/* runtime code in this preflight
- do not bundle opportunistic cleanup or behavior changes here
- you must run and report both exact commands:
  - bash packaging/containers/verify-strip-layout.sh
  - env -u WAYLAND_DISPLAY -u XDG_RUNTIME_DIR bash packaging/containers/verify-strip-layout.sh

This preflight gets one iteration only. If it fails review, stop the recovery workflow.

Return exact commands used in the summary.`}
            implementAgents={agents.smartTool}
            reviewAgents={agents.reviewer}
            feedback={p0.feedback}
            done={p0.done}
            maxIterations={1}
          />
        </Sequence>

        {p0.failed ? null : (
          <>
            <Sequence>
              <ValidationLoop
                idPrefix="recover-phase1"
                prompt={`${sharedContext}

RECOVER PHASE 1: Sidebar card UI only

Read the "Phase 1 Recovery" section in .smithers/tickets/prettymux-review-recovery.md.

Own only:
- src/gtk/sidebar_ui.c
- src/gtk/sidebar_ui.h
- src/gtk/theme.c

Goal:
- make Phase 1 approvable as a pure UI-only patch

Required outcomes:
- card UI remains visibly upgraded
- no Phase 1B/sidebar-data or layout-backend work is bundled here
- preserved interactions are directly validated:
  - inline rename
  - selection
  - drag/reorder
  - close/delete

Hard rules:
- do not create new files in this phase
- do not move code into new modules in this phase
- do not touch workspace/layout/socket/session files unless strictly unavoidable for build wiring, and if you do, explain why
- because this is user-visible, include an explicit "Live verification" section:
  - exact command(s) run
  - whether host-Wayland or Weston fallback was used
  - exact prettymux-open assertions if any
  - if rename/drag are not scriptable, say that explicitly and state the focused GTK test that covers them

Return exact validation commands and what each one proves.`}
                implementAgents={agents.smartTool}
                reviewAgents={agents.reviewer}
                feedback={p1.feedback}
                done={p1.done}
                maxIterations={3}
              />
            </Sequence>

            <Sequence>
              <ValidationLoop
                idPrefix="recover-phase2"
                prompt={`${sharedContext}

RECOVER PHASE 2: Layout backend split only

Read the "Phase 2 Recovery" section in .smithers/tickets/prettymux-review-recovery.md.

Own only:
- src/gtk/workspace.h
- src/gtk/workspace.c
- src/gtk/workspace_layout.h
- src/gtk/workspace_layout.c
- src/gtk/workspace_strip.h
- src/gtk/workspace_strip.c

Goal:
- make the backend split approvable without bundling later strip/sidebar behavior

Required outcomes:
- runtime backend split is wired into production paths
- classic-only zoom assumptions are removed from layout-aware paths
- strip teardown/lifecycle is safe
- no sidebar UI/user-visible phase work is bundled here

Hard rules:
- do not create new files in this phase unless they are already listed in the owned file set
- do not move unrelated code across modules in this phase
- do not bundle Phase 3/4 semantics here
- if runtime smoke is reported, include the exact command and what path it proves

Return exact validation commands and which rejected issues they close.`}
                implementAgents={agents.smartTool}
                reviewAgents={agents.reviewer}
                feedback={p2.feedback}
                done={p2.done}
                maxIterations={3}
              />
            </Sequence>

            <Sequence>
              <ValidationLoop
                idPrefix="recover-phase4"
                prompt={`${sharedContext}

RECOVER PHASE 4: Action semantics

Read the "Phase 4 Recovery" section in .smithers/tickets/prettymux-review-recovery.md.

Focus on the remaining strip-mode action/shortcut debt:
- strip mode must support both horizontal and vertical splitting
- if tabs remain supported inside strip columns, Ctrl+Tab / Ctrl+Shift+Tab must switch tabs in the active column, not move between columns
- pane focus traversal in strip mode must be deterministic and must not skip a pane/column
- keep semantics aligned with Séance where they fit:
  - tabs are tab-scoped
  - pane focus is separate and directional
  - Ctrl+Shift+Enter creates a new horizontal column
  - vertical split is a real stacked-in-column operation, not a generic classic split-tree fallback

Likely files:
- src/gtk/app_actions.c
- src/gtk/socket_commands.c
- src/gtk/prettymux-open.c
- src/gtk/test_workspace_phase4_actions.c
- packaging/containers/verify-strip-layout.sh

Required outcomes:
- horizontal split/new-column works in strip mode and is reachable through prettymux-open/socket automation
- vertical split works in strip mode and is reachable through prettymux-open/socket automation
- tests and live prettymux-open verification assert the exact split behavior
- strip tab-switch shortcuts are tab-scoped inside the active column
- strip pane-focus shortcuts are directional and do not skip panes/columns

Hard rules:
- because this is user-visible, include an explicit "Live verification" section:
  - exact command(s) run
  - whether host-Wayland or Weston fallback was used
  - exact prettymux-open commands/assertions for horizontal split, vertical split, and tab/focus behavior
- do not leave split/focus behavior at "manually checked" only if it is scriptable

Return the exact split/focus behavior and exact verification commands in the summary.`}
                implementAgents={agents.smartTool}
                reviewAgents={agents.reviewer}
                feedback={p4.feedback}
                done={p4.done}
                maxIterations={3}
              />
            </Sequence>

            <Sequence>
              <ValidationLoop
                idPrefix="recover-phase5"
                prompt={`${sharedContext}

RECOVER PHASE 5: Strip session persistence

Read the "Phase 5 Recovery" section in .smithers/tickets/prettymux-review-recovery.md.

Likely files:
- src/gtk/session.c
- src/gtk/prettymux-open.c
- src/gtk/socket_commands.c
- src/gtk/test_session_strip_persistence.c
- packaging/containers/verify-strip-layout.sh

Goal:
- make strip persistence approvable through real integration coverage and live automation proof

Required outcomes:
- tests exercise session_save/session_restore integration, not helper-only shims
- strip state is queryable via prettymux-open
- live Docker restart smoke proves strip persistence across restart
- classic restore safety remains intact
- layout-mode switching semantics are coherent:
  - if layout mode is meant to be global/current-instance, switching it must update other workspaces coherently
  - if layout mode is meant to be per-workspace, that must be explicit in UI/behavior and covered by tests
- silent "only current workspace changed" behavior is not acceptable unless that is an explicit, tested product decision

Hard rules:
- because this is user-visible, include an explicit "Live verification" section:
  - exact restart command(s)
  - whether host-Wayland or Weston fallback was used
  - exact prettymux-open queries/assertions before and after restart
- do not rely on helper-only test hooks as the primary evidence

Return exact build/tests/runtime commands and the queried strip-state assertions.`}
                implementAgents={agents.smartTool}
                reviewAgents={agents.reviewer}
                feedback={p5.feedback}
                done={p5.done}
                maxIterations={3}
              />
            </Sequence>

            <Sequence>
              <ValidationLoop
                idPrefix="recover-phase6b"
                prompt={`${sharedContext}

RECOVER PHASE 6B: Per-instance session persistence

Read the "Phase 6B Recovery" section in .smithers/tickets/prettymux-review-recovery.md.

Likely files:
- src/gtk/app_state.c
- src/gtk/main.c
- src/gtk/session.c
- src/gtk/test_session_strip_persistence.c
- packaging/containers/verify-strip-layout.sh

Goal:
- make per-instance session persistence restart-stable and collision-free

Required outcomes:
- per-instance naming is restart-stable
- nested child instances do not collide on one session file
- non-default instances do not restore shared legacy state
- live Docker + Wayland verification proves isolation across restart

Hard rules:
- include an explicit "Live verification" section:
  - exact command(s)
  - whether host-Wayland or Weston fallback was used
  - exact assertions showing two instances remain isolated across restart

Return the chosen identity policy, exact commands, and explicit live assertions.`}
                implementAgents={agents.smartTool}
                reviewAgents={agents.reviewer}
                feedback={p6b.feedback}
                done={p6b.done}
                maxIterations={3}
              />
            </Sequence>

            <Sequence>
              <ValidationLoop
                idPrefix="recover-phase8b"
                prompt={`${sharedContext}

RECOVER PHASE 8B: Sidebar auxiliary polish

Read the "Phase 8B Recovery" section in .smithers/tickets/prettymux-review-recovery.md.

Likely files:
- src/gtk/ghostty_actions.c
- src/gtk/workspace.c
- src/gtk/sidebar_ui.c
- src/gtk/test_sidebar_ui_sections.c
- packaging/containers/verify-strip-layout.sh

Goal:
- close the remaining correctness and verification gaps in the Phase 8B auxiliary-section work

Required outcomes:
- sidebar progress section refreshes correctly on live progress updates
- no async workspace git-branch lifetime bugs
- progress section renders paused/error/indeterminate states correctly
- interaction-preservation coverage is no longer partial
- live verification explicitly covers whatever auxiliary behavior is scriptable, or clearly documents why a given behavior is test-only

Hard rules:
- because this is user-visible, include an explicit "Live verification" section:
  - exact command(s)
  - whether host-Wayland or Weston fallback was used
  - exact prettymux-open assertions for any scriptable auxiliary behavior
- if a behavior is not scriptable, say that explicitly and point to the focused test that covers it

Return exact tests, exact live verification commands, and exact assertions.`}
                implementAgents={agents.smartTool}
                reviewAgents={agents.reviewer}
                feedback={p8b.feedback}
                done={p8b.done}
                maxIterations={3}
              />
            </Sequence>
          </>
        )}
      </Sequence>
    </Workflow>
  );
});
