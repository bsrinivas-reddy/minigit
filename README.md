# MiniGit

MiniGit is a Git-like command-line version control system implemented in standard C++17. It tracks entire multi-file project directories, implements an active staging index, computes line-by-line diffs via Dynamic Programming, and manages commit history using an in-memory singly linked list paired with on-disk persistent snapshots.

---

## 1. Problem Statement

Commercial version control systems (like Git) utilize complex Directed Acyclic Graphs (DAGs), cryptographic Merkle trees, and delta-compression packfiles. While powerful, their internal architecture can obscure fundamental data structure principles.

MiniGit demonstrates these principles with a transparent, clean implementation:
- Multi-file repository tracking supporting nested directories.
- Staging area index (`index.txt`) for incremental commit preparation.
- Custom singly linked list for commit history navigation.
- Line-level diff calculation using the Longest Common Subsequence (LCS) dynamic programming algorithm.
- Complete snapshot persistence and rollback capabilities.

---

## 2. Technologies Used

- **Language:** C++17 (`<iostream>`, `<fstream>`, `<filesystem>`, `<string>`, `<vector>`, `<set>`)
- **Compiler:** `g++` (GCC 16.2+ or any C++17 compliant compiler)
- **Data Structures:** 
  - Manually implemented pointer-based **Singly Linked List** (`CommitNode`)
  - Dynamic 2D matrix for **LCS Dynamic Programming**
- **Persistence:** Local filesystem directory (`.minigit/`) with plain-text indices and directory trees

---

## 3. Command-Line Interface (CLI)

MiniGit uses authentic Git-style subcommands:

```text
Usage:
  minigit init                    Create an empty MiniGit repository
  minigit add <path... | .>       Add file contents to the staging index
  minigit status                  Show the working tree status
  minigit commit -m "<msg>"       Record staged changes to the repository
  minigit log                     Show commit history (newest first)
  minigit diff [file]             Show line-by-line diff against HEAD
  minigit restore <commit_id>     Restore working directory from a commit
```

### Detailed Command Specifications

1. **`minigit init`**:
   - Creates the hidden `.minigit/` directory structure with `commits/`, `history.txt`, and `index.txt`.
   - Prevents destructive re-initialization if a repository already exists.

2. **`minigit add <path... | .>`**:
   - Recursively resolves file paths or the whole directory (`.`).
   - Ignores internal metadata (`.minigit`, `.git`) and build artifacts (`minigit` binary, `*.o`).
   - Writes staged relative paths into `.minigit/index.txt`.

3. **`minigit status`**:
   - Scans the working directory, staging index, and HEAD commit snapshot.
   - Categorizes files into:
     - **Changes to be committed** (staged new/modified files)
     - **Changes not staged for commit** (modified/deleted since staging or HEAD)
     - **Untracked files** (files present in working tree but unindexed)

4. **`minigit commit -m "<msg>"`**:
   - Validates that staged changes exist.
   - Generates sequential integer IDs (`1, 2, 3, ...`).
   - Creates a full snapshot directory in `.minigit/commits/<id>/`, copying previous commit files and overlaying newly staged files.
   - Appends commit metadata to `.minigit/history.txt`.
   - Clears the staging index.
   - Inserts the new commit at the head of the in-memory linked list ($O(1)$).

5. **`minigit log`**:
   - Reconstructs the linked list from `history.txt` in $O(n)$ time.
   - Traverses nodes from `head` to tail, printing commit logs in reverse-chronological order.

6. **`minigit diff [file]`**:
   - Computes differences between working files and the HEAD commit snapshot.
   - Uses the Longest Common Subsequence (LCS) DP algorithm to output unified diffs (` ` common, `+` added, `-` removed).

7. **`minigit restore <commit_id>`**:
   - Searches the commit chain for the target ID.
   - Copies all files from `.minigit/commits/<commit_id>/` back into the working directory tree.

---

## 4. How the Singly Linked List Works

Commit history is maintained in RAM using a custom pointer-based singly linked list:

```cpp
struct CommitNode {
    int id;              // Sequential commit identifier (1, 2, 3, ...)
    std::string message; // Commit description
    CommitNode* next;    // Pointer to preceding commit node
};
```

```text
[Head] -> [Commit 3] -> [Commit 2] -> [Commit 1] -> nullptr
```

1. **Head Insertion ($O(1)$):**
   - New commits are prepended to `head` in constant time without array shifting.
2. **Reverse Chronological Log ($O(n)$):**
   - Traversing from `head` naturally shows newest commits first.
3. **Reconstruction from Disk ($O(n)$):**
   - At startup, `loadHistory()` reads `history.txt` (oldest-to-newest) and performs successive head insertions, placing the latest commit at `head`.
4. **Clean Deallocation ($O(n)$):**
   - `freeList(CommitNode*& head)` deletes every allocated heap node to guarantee zero memory leaks.

---

## 5. Repository Storage Format

```text
project_root/
├── main.cpp
├── README.md
├── notes.txt
├── src/
│   └── helper.cpp
└── .minigit/
    ├── history.txt            <-- Commit records (ID and message on separate lines)
    ├── index.txt              <-- Staging index (list of staged relative paths)
    └── commits/
        ├── 1/                 <-- Full snapshot of project tree at Commit 1
        │   ├── main.cpp
        │   └── notes.txt
        └── 2/                 <-- Full snapshot of project tree at Commit 2
            ├── main.cpp
            ├── notes.txt
            └── src/
                └── helper.cpp
```

---

## 6. Time and Space Complexity Analysis

| Command / Operation | Time Complexity | Auxiliary Space (RAM) | Disk Space | Explanation |
| :--- | :---: | :---: | :---: | :--- |
| **`minigit init`** | $O(1)$ | $O(1)$ | $O(1)$ | Creates directory structure and metadata files. |
| **`minigit add`** | $O(K \log K)$ | $O(K)$ | $O(K)$ | Scans working tree for $K$ files, sorts, and saves index. |
| **`minigit status`** | $O(K \cdot B)$ | $O(K)$ | $0$ | Compares $K$ files across working tree, index, and HEAD snapshot ($B$ = file byte size). |
| **`minigit commit`** | $O(F_{total} + M)$ | $O(M)$ | $O(F_{total} + M)$ | Copies tree files ($F_{total}$ total bytes). Linked list head insertion is $O(1)$. |
| **`minigit log`** | $O(n)$ | $O(1)$ | $0$ | Traverses $n$ nodes in the linked list from head to tail. |
| **`minigit diff` (LCS)**| $O(L_1 \cdot L_2)$ | $O(L_1 \cdot L_2)$ | $0$ | Dynamic programming matrix over file lines ($L_1, L_2$) with backtracking. |
| **`minigit restore`** | $O(n + F_{total})$ | $O(1)$ | $0$ | $O(n)$ to locate commit in linked list, then $O(F_{total})$ to restore snapshot files. |
| **`freeList`** | $O(n)$ | $O(1)$ | $0$ | Iterates through $n$ nodes and deallocates each with `delete`. |

---

## 7. Build and Verification

### Build Command

Compile with strict C++17 standard compliance and all warnings enabled:

```bash
g++ -std=c++17 -Wall -Wextra -pedantic main.cpp -o minigit
```

### Memory Safety Check (ASan / UBSan)

```bash
g++ -std=c++17 -Wall -Wextra -pedantic -fsanitize=address,undefined main.cpp -o minigit_asan
```

---

## 8. Practical Demonstration Workflow

```bash
# 1. Initialize a repository
./minigit init

# 2. Check status (all files are untracked)
./minigit status

# 3. Stage all files in the directory
./minigit add .

# 4. Check status (files are now staged for commit)
./minigit status

# 5. Commit staged changes
./minigit commit -m "Initial project snapshot"

# 6. View commit log
./minigit log

# 7. Modify a tracked file
echo "New research findings" >> notes.txt

# 8. View line-by-line diff
./minigit diff

# 9. Stage and commit the modification
./minigit add notes.txt
./minigit commit -m "Added research notes"

# 10. Restore back to commit 1
./minigit restore 1
```

---

## 9. Viva Questions and Answers

### Q1: Why did you use a singly linked list for commit history?
**Answer:** A singly linked list provides $O(1)$ constant time insertion at the head without memory reallocations or element shifts. Because commit logs display the newest commit first, prepending new nodes at the head is both mathematically and structurally optimal.

### Q2: How does `loadHistory()` reconstruct a newest-first linked list from disk?
**Answer:** `history.txt` stores commits chronologically (1, 2, 3). `loadHistory()` reads from top to bottom and inserts each record at the head of the list. Inserting 1 makes `head -> 1`. Inserting 2 makes `head -> 2 -> 1`. Consequently, the highest commit ID naturally sits at `head`.

### Q3: How is commit history maintained across process restarts?
**Answer:** In-memory pointer structures are ephemeral and disappear on process termination. MiniGit achieves persistence by writing sequential commit records to `.minigit/history.txt` and full snapshot trees to `.minigit/commits/<id>/`. On reboot, `loadHistory()` hydrades disk records back into heap-allocated `CommitNode` structures.

### Q4: How does the diff engine compute line differences?
**Answer:** MiniGit implements line-level diffs using the textbook Longest Common Subsequence (LCS) Dynamic Programming algorithm. Given two files with $L_1$ and $L_2$ lines, it builds an $(L_1+1) \times (L_2+1)$ table to find the maximum common line sequence, then backtracks to generate unified diff notation (` ` common, `+` added, `-` removed) in $O(L_1 \cdot L_2)$ time.

### Q5: What is the purpose of the staging area (`index.txt`)?
**Answer:** The staging index decouples working tree changes from repository commits. Users can selectively stage specific files with `minigit add`, inspect staged versus unstaged changes with `minigit status`, and commit only the curated subset into a snapshot.

### Q6: How do you prevent memory leaks?
**Answer:** Before every command terminates, `freeList(CommitNode*& head)` traverses the linked list, deletes each heap-allocated node with `delete`, and sets `head = nullptr`. The application was validated under GCC AddressSanitizer with zero leaks.
