# AGENTS.md — Maintenance & PR Rules (`objc.gsa`)

Native Objective-C Runtime & Apple Blocks ABI bridge for Geisha (`macOS` / `iOS` / `iOS Simulator`).

## Branch, Worktree & Pull Request Workflow (No Direct Push to `main`)

`main` is protected on GitHub and gated by `Test & iOS Sim Check (darwin-arm64)`.

1. **Never edit or commit directly on `main` in a shared `~/code/objc.gsa` checkout**:
   - Keep `~/code/objc.gsa` strictly on clean `main` (`git pull --ff-only`).
   - Always open an isolated `git worktree` on a feature/fix branch (`git -C ~/code/objc.gsa worktree add -b <type>/<topic> <workspace>/objc-wt origin/main`).
2. **Open a Pull Request & wait for CI before merging**:
   - Push your branch (`git push -u origin <branch>`), open a PR via `gh pr create`, and merge only after `Test & iOS Sim Check (darwin-arm64)` passes (or enable `gh pr merge --squash --auto --delete-branch`).
   - Do not self-merge breaking public API changes without user review.

## Engineering Rules & Verification

- Keep `///` doc comments accurate on all `pub` items in `src/objc.gsa` (`geisha doc objc`).
- Every ObjC Block descriptor/trampoline or associated-object helper in `native/gs_objc.c` must preserve GC root lifetime (`gc_handle_new` / `gc_handle_free`) across `extern init`, `dealloc`, and `_Block_copy` / `_Block_release`.
- Before opening a PR, run locally:
  ```sh
  MACOSX_DEPLOYMENT_TARGET=14.0 geisha check
  MACOSX_DEPLOYMENT_TARGET=14.0 geisha test
  MACOSX_DEPLOYMENT_TARGET=14.0 GEISHA_GC_HEAP_WORDS=4096 geisha test
  ```
