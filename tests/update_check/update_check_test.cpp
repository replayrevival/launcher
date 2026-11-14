// Version comparison used by the update check (src/core/update_check.cpp). Exit code 0 = all pass.
#include "core/update_check.h"

#include <cstdio>

static int failures = 0;
static void expect(const char* a, const char* b, int want) {
    int got = compareVersions(a, b);
    if (got != want) { std::printf("FAIL compare(%s, %s) = %d, want %d\n", a, b, got, want); ++failures; }
}

int main() {
    expect("v0.1.3a-alpha", "0.1.3", 1);      // the hotfix is newer than 0.1.3
    expect("v0.1.3-alpha", "0.1.3a", -1);
    expect("v0.1.3a-alpha", "0.1.3a", 0);
    expect("v0.1.4-alpha", "0.1.3a", 1);
    expect("v0.1.3b-alpha", "0.1.3a", 1);
    expect("v0.1.3-alpha", "0.1.3", 0);
    expect("v0.1.10", "0.1.9", 1);
    expect("v1.0", "0.9.9z", 1);
    expect("v0.2.0beta", "0.2.0", 0);         // a word is not a hotfix letter
    if (!isNewerVersion("v0.1.3a-alpha", "0.1.3")) { std::printf("FAIL isNewerVersion\n"); ++failures; }
    std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
