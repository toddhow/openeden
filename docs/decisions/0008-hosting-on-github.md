# 0008 — OpenEden is hosted on GitHub

Date: 2026-10-31 · Decided by: toddhow

Numbered 0008 because a draft 0004 on Slang shaders exists on another branch.

## Decision
- **OpenEden's repository is `https://github.com/toddhow/openeden`.** That answers the guide's open question on hosting and CI: GitHub, with CreativeMotion (its decision 0003).
- **CI is GitHub Actions.** It follows from the hosting. The GitLab job list in `.gitlab-ci.yml` is the reference for what to port (task 0022).

## State on the day
- The repository exists and holds a copy of upstream OpenMW's `master`: 39,233 commits, the last a merge on 2026-10-23 (`4e718b500f`). None of OpenEden's work is on it.
- Our history shares ancestry with it up to a merge of 2026-03-14 (`a035bcad2a`). The `vulkan` branch is 264 commits past that point; the copy of `master` is 1,611 commits past it. Pushing `vulkan` as a new branch is a plain push with nothing to resolve.
- Nothing has been pushed from this checkout. The guide's rule stands: no push unless toddhow asks in that session.

## Still open
- **What to push first, and what `master` becomes.** `vulkan` is to become the main line after V4 (task 0012 answers; toddhow merges it), and the copy of OpenMW's `master` is useful as the upstream to audit merges against (guide, upstream merges). Neither is decided here.
- **The GitLab files** (`.gitlab-ci.yml`, `.gitlab/`) stay until the GitHub jobs replace them.
- **The `origin` remote** of this checkout still points at cykoder's GitLab fork. Changing it is a separate step that needs toddhow's word.

## Consequences
- **M7's CI box and V6's "CI green" box** wait for task 0022 and for a push; neither can be ticked before.
- **Links** to the repository in the README and docs can use the GitHub address once the work is there. The README's licence link points at the local file until then.
- **Gates that exist only in CI today** (sanitizers, clang-tidy, clang-format 14, the translation check) get their first run on GitHub.
