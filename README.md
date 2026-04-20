# PES-VCS Lab Report

## Analysis Questions

### Q5.1: Implementing `pes checkout <branch>`

**Question:** A branch in Git is just a file in `.git/refs/heads/` containing a commit hash. Creating a branch is creating a file. Given this, how would you implement `pes checkout <branch>` — what files need to change in `.pes/`, and what must happen to the working directory? What makes this operation complex?

**Answer:**

To implement `pes checkout <branch>`, the following steps are required:

**Files that need to change in `.pes/`:**
1. `.pes/HEAD` — Update to contain `ref: refs/heads/<branch>`, pointing to the new branch.
2. `.pes/index` — Rebuild the index to reflect the tree snapshot of the target branch's latest commit.

**What must happen to the working directory:**
1. Read the commit hash from `.pes/refs/heads/<branch>`.
2. Load the commit object to get its root tree hash.
3. Recursively walk the tree, comparing it against the current working directory.
4. Remove files that exist in the current tree but not in the target tree.
5. Create/overwrite files that differ between the current and target trees (restoring content from blobs in the object store).
6. Create or remove directories as needed.

**What makes this operation complex:**
- **Dirty working directory detection:** Before switching, we must check if any tracked files have uncommitted modifications. If a file that differs between branches has local changes, checkout must abort to prevent data loss.
- **Partial overlaps:** Some files may be identical between branches (same blob hash), so they don't need to be touched. Efficiently determining which files actually need updating requires comparing tree objects.
- **Directory conflicts:** A path might be a file on one branch and a directory on another (e.g., `foo` as a file vs. `foo/bar.c`). Handling these transitions requires careful ordering (delete file before creating directory, or vice versa).
- **Atomicity:** Ideally, checkout should be all-or-nothing. If it fails midway (e.g., permission error), the working directory is in an inconsistent state. Real Git uses a two-phase approach: first verify everything can be written, then perform the updates.
