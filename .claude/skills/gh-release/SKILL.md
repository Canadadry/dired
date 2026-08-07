---
name: gh-release
description: Cut and publish a new GitHub release for this Love2D 2048 game — bumps the version from commit history, drafts release notes, builds the .love asset, and publishes via gh. Use only when the user explicitly runs /gh-release.
---

# gh-release

## Workflow

### 1. Preconditions
Abort and report if any of these fail:
- Current branch is `master`.
- Working tree is clean, ignoring the untracked `2048.love` build artifact.
- `make test-all` passes.

### 2. Find commits since the last tag
```
git describe --tags --abbrev=0   # last tag, e.g. v0.2.0
git log <last-tag>..HEAD --oneline
```
If there are zero commits since the last tag, abort and report there's nothing to release.

### 3. Pick the next version
Classify the commits by their conventional-commit type prefix (see git-commit skill for the type table):
- Any `feat:` commit → bump **minor**.
- No `feat:` but at least one `fix:` → bump **patch**.
- Only `refactor:`/`chore:`/`docs:`/`test:` → bump **patch**.

There is no major-bump rule — this project has never marked a breaking change. Tags follow `vMAJOR.MINOR.PATCH`.

### 4. Draft release notes
Write a short title (2-4 words, e.g. "Main menu") and a 1-2 sentence body summarizing what the commits since the last tag actually changed for the player — match the tone of past releases (`gh release view v0.2.0`), not a raw commit list.

### 5. Build the asset
```
make build   # produces 2048.love
```

### 6. Confirm before touching anything remote
Present to the user and wait for explicit go-ahead:
```
Version: v0.3.0 (minor — feat: ... since v0.2.0)
Title:   <drafted title>
Notes:   <drafted body>
Asset:   2048.love
Push:    master is N commit(s) ahead of origin/master
```
Do not proceed without confirmation.

### 7. Publish
- If local `master` is ahead of `origin/master`, `git push origin master` first.
- Then create and publish the release in one step (this also creates and pushes the tag — matches how v0.1.0/v0.2.0 were made, do not create the tag separately with `git tag`):
```
gh release create vX.Y.Z 2048.love --title "<title>" --notes "<body>"
```

### 8. Clean up
Delete the local `2048.love` after a successful upload.

## Notes
- `2048.love` is gitignored — never stage or commit it.
- Never skip the confirmation step in §6, even if everything looks obviously correct.
