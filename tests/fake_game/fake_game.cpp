// Stand-in for the game, to test the launcher's crash handling: prints a [CRASH] line (with a personal path in it), writes a small
// fake crash dump into STE_DUMP_DIR and exits with a Windows crash code. Developer tool, not shipped; contains no game code.
// FAKE_GAME_MODE=code with FAKE_GAME_CODE=<n> just exits with that code instead.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

int main() {
    const char* env = std::getenv("FAKE_GAME_MODE");
    const std::string m = env ? env : "crash";
    std::printf("fake game started, path C:\\Users\\SomeoneReal\\Documents\\x\n");
    if (m == "code") {
        const char* c = std::getenv("FAKE_GAME_CODE");
        return c ? std::atoi(c) : 1;
    }
    std::fprintf(stderr, "[CRASH] fake access violation at 0x1234, module C:\\Users\\SomeoneReal\\AppData\\x.dll\n");
    if (const char* d = std::getenv("STE_DUMP_DIR")) {
        std::ofstream f(std::string(d) + "\\fake.dmp", std::ios::binary);
        f << "MDMP fake dump C:\\Users\\SomeoneReal\\secret.txt";
    }
    return static_cast<int>(0xC0000005u);
}
