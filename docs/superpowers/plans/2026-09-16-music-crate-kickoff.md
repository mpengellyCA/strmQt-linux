# Kickoff prompt: Music "Crate" implementation

Paste everything below the line into a fresh Claude Code session at the repository root.

---

Implement the Music "Crate" redesign of StrmQt, following the approved spec and plan. The next release is held for this entire refactor, so the old music code may be deleted as soon as a phase replaces it.

**Read first, in this order:**
1. `AGENTS.md`: repo rules (build presets, per-agent build dirs, conventional commits, no secrets, TLS errors fatal, never push/amend/rebase).
2. `docs/superpowers/specs/2026-09-16-music-crate-design.md`: the approved design. Mockups are in `docs/superpowers/specs/2026-09-16-music-crate-mockups/`.
3. `docs/superpowers/plans/2026-09-16-music-crate.md`: the plan index. Its **Global Constraints**, **Shared vocabulary**, **Phase 2–5 contract** and **Execution notes** bind every task.
4. The phase files, in order, each read when its phase starts:
   - `…-phase1-data.md` (19 tasks)
   - `…-phase2-home.md`
   - `…-phase3-browse.md`
   - `…-phase4-pages.md`
   - `…-phase5-player.md`
   - `…-phase6-docs.md`

**How to execute:** use the `superpowers:subagent-driven-development` skill.
- Give each task to a fresh subagent. Pass it the task text verbatim, plus the index's Global Constraints, Shared vocabulary and (for Phases 2–5) the contract.
- Review between tasks, as the skill describes.
- Phases run in order. Inside a phase, follow its **Waves** table.
- If a phase file opens with "Contract notes", those notes override the index for that phase and every later one. Tell each subagent about them.

**Parallel waves (AGENTS.md "Agent-fleet builds"):**
- Before a wave, run `df -h /tmp`. `/tmp` is a 16 GB tmpfs and each build directory is about 1 GB.
- Each parallel agent builds in `/tmp/w<wave><agent>` with `TMPDIR=/tmp/w<wave><agent>/tmp`, configured with `cmake -S . -B /tmp/w<wave><agent> --preset dev`. Moving only the build directory is not enough; `TMPDIR` must move too.
- Agents share one working tree. In a multi-agent wave they do **not** commit. At the gate you:
  1. build and test the integrated tree in `build/dev`;
  2. commit each task separately, in task order, with its own message;
  3. run `rm -rf /tmp/w<wave>*` in the same step as the last commit.
- A task run by a single agent commits its own step.

**Phase 1, Task 2 needs the live server and runs in this session, not in a subagent.**
- First run `./build/dev/strmqt-cli libraries`.
- If it prints `error: not logged in. Run: strmqt-cli login --user NAME`, stop and ask me to run `! ./build/dev/strmqt-cli login --user <name>`. Continue only after I confirm.
- Measure every V-item and record the outcomes in `docs/superpowers/plans/2026-09-16-music-crate-verifications.md` and `src/server/emby/MusicServerCapabilities.h`.
- Never write the server URL, my user name or any token into a file, a commit or a test fixture. Fixtures use only the ids already in the tests.
- Later tasks branch on these constants. Do not guess an outcome.

**Gates:**
- **Every task:** its own tests pass and the build has no warnings (`STRMQT_WERROR` is on in the dev preset).
- **Every phase:** `cmake --build --preset dev`, `ctest --preset dev`, and `bash scripts/check-qmllint-baseline.sh build/dev` (no *new* warnings). Then `STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt` must exit 0.
- **After Phases 2, 3, 4 and 5:** stop and ask me for a visual check of the running app before starting the next phase. Note the outcome in the phase's final commit message.
- A failing test is fixed at its cause. Never skip it, loosen it or delete it to get green. If a plan step is wrong against the real code, fix it the smallest correct way, keep the plan's names and contracts, and tell me what differed.

**Commits:**
- Conventional commits, one per task, using the message given in the task.
- End every message with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`.
- Never push, force, amend, rebase, tag or bump the version. The release is my call after Phase 6.

**Progress:** tick the checkboxes in the phase files as steps complete. Update them in the commit that completes the task, not in a separate commit. After each phase, give me a short report: the tasks done, the test count, and any deviations from the plan.

Start with Phase 1, wave 1a (Task 1).
