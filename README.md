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

---

### Q5.2: Detecting Dirty Working Directory Conflicts

**Question:** When switching branches, the working directory must be updated to match the target branch's tree. If the user has uncommitted changes to a tracked file, and that file differs between branches, checkout must refuse. Describe how you would detect this "dirty working directory" conflict using only the index and the object store.

**Answer:**

The detection algorithm uses a three-way comparison between the **current index**, the **working directory**, and the **target branch's tree**:

**Step 1: Load required data**
- Load the current index (`.pes/index`) which contains the blob hash for each staged file.
- Read the target branch's commit, then load its root tree object. Recursively flatten the tree into a mapping of `path → blob_hash`.

**Step 2: For each file in the current index, check for conflicts**
For each entry in the index with path `P` and hash `H_index`:
1. Look up `P` in the target tree to get `H_target`.
2. If `H_index == H_target`, the file is the same on both branches — no conflict possible regardless of working directory state.
3. If `H_index != H_target` (file differs between branches):
   - `stat()` the working directory file to get its current `mtime` and `size`.
   - Compare these against the index entry's stored `mtime_sec` and `size`.
   - If they differ, the file has been modified locally → **conflict detected**, refuse checkout.
   - For extra safety, re-hash the file content and compare against `H_index`. If `H_file != H_index`, the file has uncommitted changes → refuse.

**Step 3: Check for untracked file conflicts**
- For each path in the target tree that does NOT exist in the current index, check if that path exists in the working directory. If it does, checkout would overwrite an untracked file → refuse.

This approach requires only the index (already in memory) and object store reads (to resolve the target tree), with no expensive full-directory scans beyond targeted `stat()` calls.
