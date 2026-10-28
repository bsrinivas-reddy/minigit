#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <system_error>
#include <set>
#include <cstring>

namespace fs = std::filesystem;

struct CommitNode {
    int id;
    std::string message;
    CommitNode* next;
};

CommitNode* createNode(int id, const std::string& message) {
    CommitNode* newNode = new CommitNode;
    newNode->id = id;
    newNode->message = message;
    newNode->next = nullptr;
    return newNode;
}

void insertHead(CommitNode*& head, int id, const std::string& message) {
    CommitNode* newNode = createNode(id, message);
    newNode->next = head;
    head = newNode;
}

CommitNode* searchCommit(CommitNode* head, int id) {
    CommitNode* current = head;
    while (current != nullptr) {
        if (current->id == id) {
            return current;
        }
        current = current->next;
    }
    return nullptr;
}

void freeList(CommitNode*& head) {
    CommitNode* current = head;
    while (current != nullptr) {
        CommitNode* nextNode = current->next;
        delete current;
        current = nextNode;
    }
    head = nullptr;
}

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

bool isRepositoryInitialized() {
    std::error_code ec;
    return fs::exists(".minigit", ec) &&
           fs::is_directory(".minigit", ec) &&
           fs::exists(".minigit/history.txt", ec);
}

bool isIgnored(const fs::path& relPath) {
    std::string p = relPath.generic_string();
    if (p.empty() || p == ".") {
        return true;
    }
    if (p == ".minigit" || p.rfind(".minigit/", 0) == 0) {
        return true;
    }
    if (p == ".git" || p.rfind(".git/", 0) == 0) {
        return true;
    }
    if (p == "minigit" || p == "minigit_asan") {
        return true;
    }
    if (relPath.extension() == ".o") {
        return true;
    }
    return false;
}

std::vector<fs::path> getWorkingFiles() {
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(".", fs::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            continue;
        }
        if (entry.is_regular_file(ec)) {
            fs::path rel = fs::relative(entry.path(), ".", ec);
            if (!ec && !isIgnored(rel)) {
                files.push_back(rel);
            }
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<fs::path> getSnapshotFiles(int commitId) {
    std::vector<fs::path> files;
    fs::path commitDir = fs::path(".minigit/commits") / std::to_string(commitId);
    std::error_code ec;
    if (!fs::exists(commitDir, ec) || !fs::is_directory(commitDir, ec)) {
        return files;
    }
    for (const auto& entry : fs::recursive_directory_iterator(commitDir, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            continue;
        }
        if (entry.is_regular_file(ec)) {
            fs::path rel = fs::relative(entry.path(), commitDir, ec);
            if (!ec) {
                files.push_back(rel);
            }
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool filesIdentical(const fs::path& p1, const fs::path& p2) {
    std::error_code ec;
    if (!fs::exists(p1, ec) || !fs::exists(p2, ec)) {
        return false;
    }
    if (fs::file_size(p1, ec) != fs::file_size(p2, ec)) {
        return false;
    }
    std::ifstream f1(p1, std::ios::binary);
    std::ifstream f2(p2, std::ios::binary);
    if (!f1.is_open() || !f2.is_open()) {
        return false;
    }
    char buf1[4096];
    char buf2[4096];
    while (f1 && f2) {
        f1.read(buf1, sizeof(buf1));
        f2.read(buf2, sizeof(buf2));
        if (f1.gcount() != f2.gcount()) {
            return false;
        }
        if (std::memcmp(buf1, buf2, static_cast<size_t>(f1.gcount())) != 0) {
            return false;
        }
    }
    return f1.eof() && f2.eof();
}

std::vector<fs::path> loadIndex() {
    std::vector<fs::path> indexFiles;
    std::ifstream file(".minigit/index.txt");
    if (!file.is_open()) {
        return indexFiles;
    }
    std::string line;
    while (std::getline(file, line)) {
        std::string p = trim(line);
        if (!p.empty()) {
            indexFiles.push_back(fs::path(p).lexically_normal());
        }
    }
    std::sort(indexFiles.begin(), indexFiles.end());
    auto last = std::unique(indexFiles.begin(), indexFiles.end());
    indexFiles.erase(last, indexFiles.end());
    return indexFiles;
}

bool saveIndex(const std::vector<fs::path>& indexFiles) {
    std::ofstream file(".minigit/index.txt");
    if (!file.is_open()) {
        return false;
    }
    for (const auto& p : indexFiles) {
        file << p.generic_string() << "\n";
    }
    return true;
}

void loadHistory(CommitNode*& head) {
    freeList(head);

    std::ifstream historyFile(".minigit/history.txt");
    if (!historyFile.is_open()) {
        return;
    }

    std::string idLine;
    while (std::getline(historyFile, idLine)) {
        std::string trimmedId = trim(idLine);
        if (trimmedId.empty()) {
            continue;
        }

        int id = 0;
        try {
            id = std::stoi(trimmedId);
        } catch (...) {
            continue;
        }

        std::string message;
        if (!std::getline(historyFile, message)) {
            break;
        }

        if (!message.empty() && message.back() == '\r') {
            message.pop_back();
        }

        insertHead(head, id, message);
    }
}

bool readFileLines(const fs::path& filePath, std::vector<std::string>& lines) {
    lines.clear();
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return true;
}

struct DiffLine {
    char type;
    std::string text;
};

std::vector<DiffLine> computeLcsDiff(const std::vector<std::string>& oldLines,
                                    const std::vector<std::string>& newLines) {
    size_t m = oldLines.size();
    size_t n = newLines.size();

    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
    for (size_t i = 1; i <= m; ++i) {
        for (size_t j = 1; j <= n; ++j) {
            if (oldLines[i - 1] == newLines[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1] + 1;
            } else {
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
            }
        }
    }

    std::vector<DiffLine> diff;
    size_t i = m;
    size_t j = n;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && oldLines[i - 1] == newLines[j - 1]) {
            diff.push_back({' ', oldLines[i - 1]});
            --i;
            --j;
        } else if (j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j])) {
            diff.push_back({'+', newLines[j - 1]});
            --j;
        } else if (i > 0 && (j == 0 || dp[i][j - 1] < dp[i - 1][j])) {
            diff.push_back({'-', oldLines[i - 1]});
            --i;
        }
    }

    std::reverse(diff.begin(), diff.end());
    return diff;
}

bool printFileDiff(const fs::path& oldPath, const fs::path& newPath, const std::string& label) {
    std::vector<std::string> oldLines;
    std::vector<std::string> newLines;
    readFileLines(oldPath, oldLines);
    readFileLines(newPath, newLines);

    auto diff = computeLcsDiff(oldLines, newLines);
    bool hasChanges = false;
    for (const auto& line : diff) {
        if (line.type != ' ') {
            hasChanges = true;
            break;
        }
    }
    if (!hasChanges) {
        return false;
    }

    std::cout << "diff --minigit a/" << label << " b/" << label << "\n";
    std::cout << "--- a/" << label << "\n";
    std::cout << "+++ b/" << label << "\n";
    for (const auto& line : diff) {
        std::cout << line.type << " " << line.text << "\n";
    }
    std::cout << "\n";
    return true;
}

int cmdInit() {
    std::error_code ec;
    if (fs::exists(".minigit", ec)) {
        std::cout << "Reinitialized existing MiniGit repository in " << fs::absolute(".minigit", ec).generic_string() << "/\n";
        return 0;
    }
    if (!fs::create_directories(".minigit/commits", ec) && ec) {
        std::cerr << "fatal: cannot create directory .minigit/commits: " << ec.message() << "\n";
        return 1;
    }
    std::ofstream history(".minigit/history.txt", std::ios::app);
    std::ofstream index(".minigit/index.txt", std::ios::app);
    std::cout << "Initialized empty MiniGit repository in " << fs::absolute(".minigit", ec).generic_string() << "/\n";
    return 0;
}

int cmdAdd(const std::vector<std::string>& targets) {
    if (!isRepositoryInitialized()) {
        std::cerr << "fatal: not a minigit repository (or any of the parent directories): .minigit\n";
        return 1;
    }
    if (targets.empty()) {
        std::cerr << "Nothing specified, nothing added.\n";
        return 0;
    }

    auto index = loadIndex();
    std::set<std::string> indexSet;
    for (const auto& p : index) {
        indexSet.insert(p.generic_string());
    }

    std::error_code ec;
    for (const auto& target : targets) {
        if (target == ".") {
            auto working = getWorkingFiles();
            for (const auto& p : working) {
                if (indexSet.insert(p.generic_string()).second) {
                    index.push_back(p);
                }
            }
        } else {
            fs::path p(target);
            if (!fs::exists(p, ec)) {
                std::cerr << "fatal: pathspec '" << target << "' did not match any files\n";
                return 1;
            }
            if (fs::is_directory(p, ec)) {
                for (const auto& entry : fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec)) {
                    if (ec) continue;
                    if (entry.is_regular_file(ec)) {
                        fs::path rel = fs::relative(entry.path(), ".", ec);
                        if (!ec && !isIgnored(rel)) {
                            if (indexSet.insert(rel.generic_string()).second) {
                                index.push_back(rel);
                            }
                        }
                    }
                }
            } else if (fs::is_regular_file(p, ec)) {
                fs::path rel = fs::relative(p, ".", ec);
                if (isIgnored(rel)) {
                    std::cerr << "The following paths are ignored by minigit: " << rel.generic_string() << "\n";
                } else {
                    if (indexSet.insert(rel.generic_string()).second) {
                        index.push_back(rel);
                    }
                }
            }
        }
    }

    std::sort(index.begin(), index.end());
    saveIndex(index);
    return 0;
}

int cmdStatus() {
    if (!isRepositoryInitialized()) {
        std::cerr << "fatal: not a minigit repository (or any of the parent directories): .minigit\n";
        return 1;
    }

    CommitNode* head = nullptr;
    loadHistory(head);

    auto index = loadIndex();
    auto working = getWorkingFiles();
    std::vector<fs::path> headFiles;
    if (head != nullptr) {
        headFiles = getSnapshotFiles(head->id);
    }

    std::set<std::string> indexSet;
    for (const auto& p : index) indexSet.insert(p.generic_string());

    std::set<std::string> workingSet;
    for (const auto& p : working) workingSet.insert(p.generic_string());

    std::set<std::string> headSet;
    for (const auto& p : headFiles) headSet.insert(p.generic_string());

    std::vector<std::string> stagedNew;
    std::vector<std::string> stagedMod;
    std::vector<std::string> unstagedMod;
    std::vector<std::string> unstagedDel;
    std::vector<std::string> untracked;

    for (const auto& p : index) {
        std::string ps = p.generic_string();
        fs::path headSnap = head ? (fs::path(".minigit/commits") / std::to_string(head->id) / p) : fs::path();
        if (headSet.find(ps) == headSet.end()) {
            stagedNew.push_back(ps);
        } else if (!filesIdentical(p, headSnap)) {
            stagedMod.push_back(ps);
        }
    }

    for (const auto& p : index) {
        std::string ps = p.generic_string();
        if (workingSet.find(ps) == workingSet.end()) {
            unstagedDel.push_back(ps);
        }
    }

    for (const auto& p : working) {
        std::string ps = p.generic_string();
        if (indexSet.find(ps) == indexSet.end() && headSet.find(ps) == headSet.end()) {
            untracked.push_back(ps);
        }
    }

    if (head != nullptr) {
        for (const auto& p : headFiles) {
            std::string ps = p.generic_string();
            if (indexSet.find(ps) == indexSet.end()) {
                if (workingSet.find(ps) == workingSet.end()) {
                    unstagedDel.push_back(ps);
                } else {
                    fs::path headSnap = fs::path(".minigit/commits") / std::to_string(head->id) / p;
                    if (!filesIdentical(p, headSnap)) {
                        unstagedMod.push_back(ps);
                    }
                }
            }
        }
    }

    std::cout << "On commit " << (head ? std::to_string(head->id) : "(initial)") << "\n";

    if (!stagedNew.empty() || !stagedMod.empty()) {
        std::cout << "\nChanges to be committed:\n";
        std::cout << "  (use \"minigit commit -m <msg>\" to commit)\n";
        for (const auto& f : stagedNew) {
            std::cout << "\tnew file:   " << f << "\n";
        }
        for (const auto& f : stagedMod) {
            std::cout << "\tmodified:   " << f << "\n";
        }
    }

    if (!unstagedMod.empty() || !unstagedDel.empty()) {
        std::cout << "\nChanges not staged for commit:\n";
        std::cout << "  (use \"minigit add <file>...\" to update what will be committed)\n";
        for (const auto& f : unstagedMod) {
            std::cout << "\tmodified:   " << f << "\n";
        }
        for (const auto& f : unstagedDel) {
            std::cout << "\tdeleted:    " << f << "\n";
        }
    }

    if (!untracked.empty()) {
        std::cout << "\nUntracked files:\n";
        std::cout << "  (use \"minigit add <file>...\" to include in what will be committed)\n";
        for (const auto& f : untracked) {
            std::cout << "\t" << f << "\n";
        }
    }

    if (stagedNew.empty() && stagedMod.empty() && unstagedMod.empty() && unstagedDel.empty() && untracked.empty()) {
        std::cout << "nothing to commit, working tree clean\n";
    }

    freeList(head);
    return 0;
}

int cmdCommit(const std::string& message) {
    if (!isRepositoryInitialized()) {
        std::cerr << "fatal: not a minigit repository (or any of the parent directories): .minigit\n";
        return 1;
    }
    if (trim(message).empty()) {
        std::cerr << "error: commit message cannot be empty\n";
        return 1;
    }

    auto index = loadIndex();
    if (index.empty()) {
        std::cout << "no changes added to commit (use \"minigit add\")\n";
        return 0;
    }

    CommitNode* head = nullptr;
    loadHistory(head);
    int nextId = (head != nullptr) ? (head->id + 1) : 1;

    fs::path newCommitDir = fs::path(".minigit/commits") / std::to_string(nextId);
    std::error_code ec;
    if (!fs::create_directories(newCommitDir, ec) && ec) {
        std::cerr << "fatal: cannot create commit snapshot directory: " << ec.message() << "\n";
        freeList(head);
        return 1;
    }

    if (head != nullptr) {
        fs::path prevCommitDir = fs::path(".minigit/commits") / std::to_string(head->id);
        fs::copy(prevCommitDir, newCommitDir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    }

    int committedCount = 0;
    for (const auto& rel : index) {
        if (fs::exists(rel, ec) && fs::is_regular_file(rel, ec)) {
            fs::path dest = newCommitDir / rel;
            fs::create_directories(dest.parent_path(), ec);
            fs::copy_file(rel, dest, fs::copy_options::overwrite_existing, ec);
            if (!ec) {
                committedCount++;
            }
        }
    }

    std::ofstream historyFile(".minigit/history.txt", std::ios::app);
    if (!historyFile.is_open()) {
        std::cerr << "fatal: cannot open history file for appending\n";
        fs::remove_all(newCommitDir, ec);
        freeList(head);
        return 1;
    }
    historyFile << nextId << "\n" << message << "\n";
    historyFile.close();

    saveIndex({});

    std::cout << "[commit " << nextId << "] " << message << "\n";
    std::cout << " " << committedCount << " file(s) committed\n";

    freeList(head);
    return 0;
}

int cmdLog() {
    if (!isRepositoryInitialized()) {
        std::cerr << "fatal: not a minigit repository (or any of the parent directories): .minigit\n";
        return 1;
    }
    CommitNode* head = nullptr;
    loadHistory(head);
    if (head == nullptr) {
        std::cout << "No commits yet.\n";
        return 0;
    }

    CommitNode* current = head;
    while (current != nullptr) {
        std::cout << "commit " << current->id << "\n";
        std::cout << "    " << current->message << "\n\n";
        current = current->next;
    }
    freeList(head);
    return 0;
}

int cmdDiff(const std::string& targetFile) {
    if (!isRepositoryInitialized()) {
        std::cerr << "fatal: not a minigit repository: .minigit\n";
        return 1;
    }
    CommitNode* head = nullptr;
    loadHistory(head);
    if (head == nullptr) {
        std::cout << "No commits yet to compare against.\n";
        return 0;
    }

    fs::path headDir = fs::path(".minigit/commits") / std::to_string(head->id);
    std::error_code ec;

    if (!targetFile.empty()) {
        fs::path rel = fs::path(targetFile).lexically_normal();
        fs::path snapPath = headDir / rel;
        if (!fs::exists(rel, ec)) {
            std::cerr << "fatal: file '" << targetFile << "' does not exist\n";
            freeList(head);
            return 1;
        }
        printFileDiff(snapPath, rel, rel.generic_string());
    } else {
        auto working = getWorkingFiles();
        for (const auto& rel : working) {
            fs::path snapPath = headDir / rel;
            printFileDiff(snapPath, rel, rel.generic_string());
        }
    }

    freeList(head);
    return 0;
}

int cmdRestore(int commitId) {
    if (!isRepositoryInitialized()) {
        std::cerr << "fatal: not a minigit repository: .minigit\n";
        return 1;
    }
    CommitNode* head = nullptr;
    loadHistory(head);

    CommitNode* target = searchCommit(head, commitId);
    if (target == nullptr) {
        std::cerr << "error: commit " << commitId << " not found\n";
        freeList(head);
        return 1;
    }

    fs::path commitDir = fs::path(".minigit/commits") / std::to_string(commitId);
    std::error_code ec;
    if (!fs::exists(commitDir, ec) || !fs::is_directory(commitDir, ec)) {
        std::cerr << "error: snapshot for commit " << commitId << " is missing\n";
        freeList(head);
        return 1;
    }

    auto snapFiles = getSnapshotFiles(commitId);
    int restored = 0;
    for (const auto& rel : snapFiles) {
        fs::path src = commitDir / rel;
        fs::path dest = rel;
        fs::create_directories(dest.parent_path(), ec);
        fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            restored++;
        }
    }

    std::cout << "Restored " << restored << " file(s) from commit " << commitId << " (" << target->message << ").\n";
    freeList(head);
    return 0;
}

void printUsage() {
    std::cout << "MiniGit - A minimalist Git-like version control system\n\n";
    std::cout << "Usage:\n";
    std::cout << "  minigit init                    Create an empty MiniGit repository\n";
    std::cout << "  minigit add <path...>           Add file contents to the staging index\n";
    std::cout << "  minigit status                  Show the working tree status\n";
    std::cout << "  minigit commit -m <msg>         Record staged changes to the repository\n";
    std::cout << "  minigit log                     Show commit history (newest first)\n";
    std::cout << "  minigit diff [file]             Show line-by-line diff against HEAD\n";
    std::cout << "  minigit restore <commit_id>     Restore working directory from a commit\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string cmd = argv[1];
    if (cmd == "init") {
        return cmdInit();
    } else if (cmd == "add") {
        std::vector<std::string> targets;
        for (int i = 2; i < argc; ++i) {
            targets.push_back(argv[i]);
        }
        return cmdAdd(targets);
    } else if (cmd == "status") {
        return cmdStatus();
    } else if (cmd == "commit") {
        std::string message;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-m" && i + 1 < argc) {
                message = argv[++i];
            }
        }
        if (message.empty()) {
            std::cerr << "error: commit requires a message (-m \"<message>\")\n";
            return 1;
        }
        return cmdCommit(message);
    } else if (cmd == "log") {
        return cmdLog();
    } else if (cmd == "diff") {
        std::string targetFile = (argc >= 3) ? argv[2] : "";
        return cmdDiff(targetFile);
    } else if (cmd == "restore" || cmd == "checkout") {
        if (argc < 3) {
            std::cerr << "error: specify commit ID to restore (e.g. minigit restore 1)\n";
            return 1;
        }
        int commitId = 0;
        try {
            commitId = std::stoi(argv[2]);
        } catch (...) {
            std::cerr << "error: invalid commit ID '" << argv[2] << "'\n";
            return 1;
        }
        return cmdRestore(commitId);
    } else if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        printUsage();
        return 0;
    } else {
        std::cerr << "minigit: '" << cmd << "' is not a minigit command. See 'minigit --help'.\n";
        return 1;
    }
}
