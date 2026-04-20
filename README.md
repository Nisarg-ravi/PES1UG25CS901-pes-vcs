# PES-VCS Lab Report

## Analysis Questions

#Phase 1: Object Storage
##Screenshot 1A — ./test_objects output showing all tests passing:
<img width="1229" height="171" alt="image" src="https://github.com/user-attachments/assets/98c33433-6934-47a7-9354-0d6ac882f441" />


##Screenshot 1B — find .pes/objects -type f showing sharded directory structure:
<img width="1001" height="72" alt="image" src="https://github.com/user-attachments/assets/57caa0d5-cd24-4a7a-ac85-b5c70bd3ec3b" />

#Phase 2: Tree Objects
##Screenshot 2A — ./test_tree output showing all tests passing:
<img width="719" height="120" alt="image" src="https://github.com/user-attachments/assets/c89b6baf-7084-450f-84d4-a186be4c73bd" />

##Screenshot 2B — xxd of a raw tree object (first 20 lines):


#Phase 3: Staging Area
##Screenshot 3A — pes init → pes add → pes status sequence:
<img width="1458" height="612" alt="image" src="https://github.com/user-attachments/assets/15219176-4464-4c56-a3d6-7e78741dbc89" />

##Screenshot 3B — cat .pes/index showing the text-format index:
<img width="1194" height="49" alt="image" src="https://github.com/user-attachments/assets/82401672-6485-4c04-8087-ae6641df14a5" />

#Phase 4: Commits and History
##Screenshot 4A — pes log output with three commits:
<img width="1038" height="423" alt="image" src="https://github.com/user-attachments/assets/c98cb836-9556-4519-8217-70e22f9f80e2" />

##Screenshot 4B — find .pes -type f | sort showing object store growth:

<img width="1076" height="340" alt="image" src="https://github.com/user-attachments/assets/0043ac6e-7a4f-4eec-a960-fca95118cbce" />

##Screenshot 4C — cat .pes/refs/heads/main and cat .pes/HEAD showing the reference chain:
<img width="869" height="72" alt="image" src="https://github.com/user-attachments/assets/1cc0a621-5070-415d-bf7c-3ceb9af683ee" />


#Integration Test
##Final — Full integration test (make test-integration):

<img width="790" height="888" alt="image" src="https://github.com/user-attachments/assets/2a4f9f93-8dc6-47a3-9da4-bdb2dfcc3768" />




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

---

### Q5.3: Detached HEAD and Recovering Commits

**Question:** "Detached HEAD" means HEAD contains a commit hash directly instead of a branch reference. What happens if you make commits in this state? How could a user recover those commits?

**Answer:**

**What happens when you commit in detached HEAD state:**

In normal operation, `.pes/HEAD` contains `ref: refs/heads/main`, so commits update the branch file. In detached HEAD state, `.pes/HEAD` contains a raw commit hash (e.g., `a1b2c3d4...`) instead of a symbolic reference.

When you create a new commit in detached HEAD:
1. The commit is created normally — tree is built, commit object is written to the object store with the current HEAD hash as its parent.
2. `head_update()` writes the new commit hash directly into `.pes/HEAD` (since there's no `ref:` prefix, it updates HEAD itself).
3. The new commit exists in the object store and HEAD points to it.

**The danger:** If you then checkout a branch (`pes checkout main`), HEAD is overwritten with `ref: refs/heads/main`. The commits you made in detached state are now **orphaned** — they exist in the object store but no branch reference points to them. They are unreachable through normal traversal.

**How to recover those commits:**

1. **If you remember the hash:** Simply create a new branch pointing to it: write the hash into `.pes/refs/heads/recovery`.

2. **Using a reflog (if implemented):** Git maintains `.git/logs/HEAD` which records every HEAD change. By scanning this log, you can find the hash of the orphaned commit. PES-VCS doesn't implement a reflog, but one could be added by appending to a log file on every HEAD update.

3. **Brute-force scan of object store:** Walk every object in `.pes/objects/`, read those of type `commit`, and check if any commit's hash is not reachable from any branch. This is O(n) in the number of objects but guarantees finding all orphaned commits.

4. **Time-based safety:** Git's garbage collector doesn't delete unreachable objects until they're older than 2 weeks (by default), giving users time to notice and recover. The `gc.pruneExpire` config controls this grace period.

---

### Q6.1: Garbage Collection Algorithm

**Question:** Over time, the object store accumulates unreachable objects — blobs, trees, or commits that no branch points to (directly or transitively). Describe an algorithm to find and delete these objects. What data structure would you use to track "reachable" hashes efficiently? For a repository with 100,000 commits and 50 branches, estimate how many objects you'd need to visit.

**Answer:**

**Algorithm: Mark-and-Sweep Garbage Collection**

The algorithm has two phases, analogous to mark-and-sweep in memory garbage collection:

**Phase 1 — Mark (find all reachable objects):**
1. Initialize a **hash set** (e.g., a hash table keyed by ObjectID, 32 bytes each) to store all reachable hashes.
2. For each branch ref file in `.pes/refs/heads/`:
   - Read the commit hash from the file.
   - Add it to the reachable set.
   - Walk the commit's parent chain (following `parent` pointers) until reaching a root commit (no parent) or a commit already in the set:
     - For each commit, add the commit hash to the reachable set.
     - Read the commit's `tree` hash, and recursively walk the tree:
       - Add the tree hash to the reachable set.
       - For each entry in the tree: if it's a blob, add its hash; if it's a subtree, recurse into it.
3. Also process any tags or other refs if they exist.

**Phase 2 — Sweep (delete unreachable objects):**
1. Walk the entire `.pes/objects/` directory (all shard directories `00/` through `ff/`).
2. For each object file, reconstruct its full hash from the directory name + filename.
3. If the hash is NOT in the reachable set, delete the file.
4. Remove empty shard directories.

**Data structure:** A **hash set** (hash table with O(1) lookup) is ideal. Since ObjectIDs are already well-distributed hashes (SHA-256), the first few bytes can serve as the bucket index directly. Memory usage: ~32 bytes per entry + overhead. For 1 million objects, this is ~40 MB — easily fits in RAM.

**Estimate for 100,000 commits and 50 branches:**
- **Commits visited:** At most 100,000 (each commit visited once due to the hash set dedup). In practice, branches share history, so the total unique commits is already 100,000.
- **Trees visited:** Each commit has at least one root tree. Assuming an average of 5 tree objects per commit (root + subdirectories), that's ~500,000 tree objects.
- **Blobs visited:** Assuming an average of 20 files per tree snapshot but heavy deduplication, perhaps ~200,000 unique blobs.
- **Total objects to visit:** Approximately **800,000 objects** for the mark phase. The sweep phase visits every physical file in the object store (which may be larger if there are unreachable objects).

---

### Q6.2: GC and Concurrent Commit Race Condition

**Question:** Why is it dangerous to run garbage collection concurrently with a commit operation? Describe a race condition where GC could delete an object that a concurrent commit is about to reference. How does Git's real GC avoid this?

**Answer:**

**Why concurrent GC is dangerous:**

Garbage collection and commit operations both interact with the object store, but they have opposing goals: GC removes objects it deems unreachable, while commit creates new objects that reference existing ones. If these run simultaneously without coordination, GC might delete an object between the time commit decides to reference it and the time the reference is actually written.

**Race condition scenario:**

Consider this interleaved execution:

```
Time    GC Process                          Commit Process
────    ──────────                          ──────────────
T1      Start mark phase                    
T2      Walk all branches, build            
        reachable set (blob X is            
        NOT reachable from any branch)      
T3                                          User runs: pes add file.txt
                                            (file.txt has same content as
                                            the old blob X → deduplication
                                            means no new object is written,
                                            just references existing blob X)
T4                                          Index now references blob X
T5      Begin sweep phase                   
T6      Delete blob X (it was               
        unreachable at mark time T2)        
T7                                          User runs: pes commit -m "msg"
                                            Commit references tree → blob X
                                            But blob X is GONE → corrupt repo!
```

The fundamental issue: GC's reachability snapshot (taken at T2) becomes stale by the time sweep executes (T6). The commit process created a new reference to an object that GC had already decided to delete.

**How Git's real GC avoids this:**

1. **Grace period (`gc.pruneExpire`):** Git only deletes unreachable objects that are older than 2 weeks (default). Newly created objects are safe because their mtime is recent. This handles most cases — a concurrent commit creates new objects with current timestamps, so even if they're momentarily unreachable, they won't be pruned.

2. **Lock files:** Git uses `.git/gc.pid` lock files to prevent multiple GC processes. The commit process itself doesn't lock against GC, but the grace period protects it.

3. **Packfile atomicity:** When GC repacks objects into packfiles, it writes the new pack atomically (write temp file, then rename). Old loose objects are only deleted AFTER the pack containing them is fully written and referenced.

4. **Object creation before reference update:** Git always writes objects to the store BEFORE updating any ref that points to them. This means an object exists on disk before any branch can reach it. Combined with the grace period, newly created objects are never candidates for deletion.
