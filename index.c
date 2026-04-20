// index.c — Staging area implementation
//
// Text format of .pes/index (one entry per line, sorted by path):
//
//   <mode-octal> <64-char-hex-hash> <mtime-seconds> <size> <path>
//
// Example:
//   100644 a1b2c3d4e5f6...  1699900000 42 README.md
//   100644 f7e8d9c0b1a2...  1699900100 128 src/main.c
//
// This is intentionally a simple text format. No magic numbers, no
// binary parsing. The focus is on the staging area CONCEPT (tracking
// what will go into the next commit) and ATOMIC WRITES (temp+rename).
//
// PROVIDED functions: index_find, index_remove, index_status
// TODO functions:     index_load, index_save, index_add

#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

// Find an index entry by path (linear scan).
IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

// Remove a file from the index.
// Returns 0 on success, -1 if path not in index.
int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i], &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

// Print the status of the working directory.
//
// Identifies files that are staged, unstaged (modified/deleted in working dir),
// and untracked (present in working dir but not in index).
// Returns 0.
int index_status(const Index *index) {
    printf("Staged changes:\n");
    int staged_count = 0;
    // Note: A true Git implementation deeply diffs against the HEAD tree here. 
    // For this lab, displaying indexed files represents the staging intent.
    for (int i = 0; i < index->count; i++) {
        printf("  staged:     %s\n", index->entries[i].path);
        staged_count++;
    }
    if (staged_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    printf("Unstaged changes:\n");
    int unstaged_count = 0;
    for (int i = 0; i < index->count; i++) {
        struct stat st;
        if (stat(index->entries[i].path, &st) != 0) {
            printf("  deleted:    %s\n", index->entries[i].path);
            unstaged_count++;
        } else {
            // Fast diff: check metadata instead of re-hashing file content
            if (st.st_mtime != (time_t)index->entries[i].mtime_sec || st.st_size != (off_t)index->entries[i].size) {
                printf("  modified:   %s\n", index->entries[i].path);
                unstaged_count++;
            }
        }
    }
    if (unstaged_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    printf("Untracked files:\n");
    int untracked_count = 0;
    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            // Skip hidden directories, parent directories, and build artifacts
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            if (strcmp(ent->d_name, ".pes") == 0) continue;
            if (strcmp(ent->d_name, "pes") == 0) continue; // compiled executable
            if (strstr(ent->d_name, ".o") != NULL) continue; // object files

            // Check if file is tracked in the index
            int is_tracked = 0;
            for (int i = 0; i < index->count; i++) {
                if (strcmp(index->entries[i].path, ent->d_name) == 0) {
                    is_tracked = 1; 
                    break;
                }
            }
            
            if (!is_tracked) {
                struct stat st;
                stat(ent->d_name, &st);
                if (S_ISREG(st.st_mode)) { // Only list regular files for simplicity
                    printf("  untracked:  %s\n", ent->d_name);
                    untracked_count++;
                }
            }
        }
        closedir(dir);
    }
    if (untracked_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    return 0;
}

// ─── TODO: Implement these ───────────────────────────────────────────────────

// Load the index from .pes/index.
//
// HINTS - Useful functions:
//   - fopen (with "r"), fscanf, fclose : reading the text file line by line
//   - hex_to_hash                      : converting the parsed string to ObjectID
//
// Returns 0 on success, -1 on error.
int index_load(Index *index) {
    index->count = 0;

    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) {
        // File doesn't exist yet — empty index is valid (not an error)
        return 0;
    }

    char line[1024];
    while (fgets(line, sizeof(line), f) && index->count < MAX_INDEX_ENTRIES) {
        IndexEntry *entry = &index->entries[index->count];
        char hex[HASH_HEX_SIZE + 1];
        unsigned int mode;
        uint64_t mtime;
        unsigned int size;
        char path[512];

        // Parse: <mode-octal> <64-char-hex-hash> <mtime-seconds> <size> <path>
        if (sscanf(line, "%o %64s %lu %u %511[^\n]", &mode, hex, &mtime, &size, path) != 5) {
            continue; // Skip malformed lines
        }

        entry->mode = mode;
        entry->mtime_sec = mtime;
        entry->size = size;
        snprintf(entry->path, sizeof(entry->path), "%s", path);

        if (hex_to_hash(hex, &entry->hash) != 0) {
            continue; // Skip entries with invalid hashes
        }

        index->count++;
    }

    fclose(f);
    return 0;
}

// Save the index to .pes/index atomically.
//
// HINTS - Useful functions and syscalls:
//   - qsort                            : sorting the entries array by path
//   - fopen (with "w"), fprintf        : writing to the temporary file
//   - hash_to_hex                      : converting ObjectID for text output
//   - fflush, fileno, fsync, fclose    : flushing userspace buffers and syncing to disk
//   - rename                           : atomically moving the temp file over the old index
//
// Returns 0 on success, -1 on error.
// Comparison function for sorting index entries by path
static int cmp_index_entries(const void *a, const void *b) {
    const IndexEntry *ea = (const IndexEntry *)a;
    const IndexEntry *eb = (const IndexEntry *)b;
    return strcmp(ea->path, eb->path);
}

int index_save(const Index *index) {
    // Create a heap-allocated mutable copy to sort (Index is too large for stack)
    Index *sorted = malloc(sizeof(Index));
    if (!sorted) return -1;
    memcpy(sorted, index, sizeof(Index));
    qsort(sorted->entries, sorted->count, sizeof(IndexEntry), cmp_index_entries);

    // Write to a temporary file
    const char *tmp_path = INDEX_FILE ".tmp";
    FILE *f = fopen(tmp_path, "w");
    if (!f) { free(sorted); return -1; }

    for (int i = 0; i < sorted->count; i++) {
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&sorted->entries[i].hash, hex);
        fprintf(f, "%o %s %lu %u %s\n",
                sorted->entries[i].mode,
                hex,
                (unsigned long)sorted->entries[i].mtime_sec,
                sorted->entries[i].size,
                sorted->entries[i].path);
    }
    free(sorted);

    // Flush and sync to ensure data reaches disk
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    // Atomically replace the index file
    if (rename(tmp_path, INDEX_FILE) != 0) {
        unlink(tmp_path);
        return -1;
    }

    return 0;
}

// Stage a file for the next commit.
//
// HINTS - Useful functions and syscalls:
//   - fopen, fread, fclose             : reading the target file's contents
//   - object_write                     : saving the contents as OBJ_BLOB
//   - stat / lstat                     : getting file metadata (size, mtime, mode)
//   - index_find                       : checking if the file is already staged
//
// Returns 0 on success, -1 on error.
// Forward declaration
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

int index_add(Index *index, const char *path) {
    // Step 1: Read the file contents
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "error: '%s': no such file\n", path);
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open '%s'\n", path);
        return -1;
    }

    size_t file_size = (size_t)st.st_size;
    void *content = malloc(file_size);
    if (!content && file_size > 0) {
        fclose(f);
        return -1;
    }

    if (file_size > 0 && fread(content, 1, file_size, f) != file_size) {
        free(content);
        fclose(f);
        return -1;
    }
    fclose(f);

    // Step 2: Write the contents as a blob to the object store
    ObjectID blob_id;
    if (object_write(OBJ_BLOB, content, file_size, &blob_id) != 0) {
        free(content);
        return -1;
    }
    free(content);

    // Step 3: Determine file mode
    uint32_t mode = (st.st_mode & S_IXUSR) ? 0100755 : 0100644;

    // Step 4: Update or add the index entry
    IndexEntry *existing = index_find(index, path);
    if (existing) {
        // Update existing entry
        existing->hash = blob_id;
        existing->mode = mode;
        existing->mtime_sec = (uint64_t)st.st_mtime;
        existing->size = (uint32_t)st.st_size;
    } else {
        // Add new entry
        if (index->count >= MAX_INDEX_ENTRIES) {
            fprintf(stderr, "error: index is full\n");
            return -1;
        }
        IndexEntry *entry = &index->entries[index->count++];
        entry->hash = blob_id;
        entry->mode = mode;
        entry->mtime_sec = (uint64_t)st.st_mtime;
        entry->size = (uint32_t)st.st_size;
        snprintf(entry->path, sizeof(entry->path), "%s", path);
    }

    // Step 5: Save the index
    return index_save(index);
}