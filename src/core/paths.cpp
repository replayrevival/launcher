#include "core/paths.h"

namespace fs = std::filesystem;

bool isPortable(const fs::path& exeDir) { return fs::exists(exeDir / "portable.txt"); }

fs::path dataDir(const fs::path& exeDir, const fs::path& userPrefDir) {
    return isPortable(exeDir) ? exeDir / "data" : userPrefDir;
}
