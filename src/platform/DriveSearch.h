#pragma once
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/identify.h"

struct FoundGame {
    std::filesystem::path path;
    IdResult id;
};

// Looks through the user's drives for Xbox disc images and game folders, on a worker
// thread. Files are only read. Results arrive through poll().
class DriveSearch {
public:
    ~DriveSearch() { cancel(); }
    void start(const IdExpect& expect);
    void cancel();
    bool running() const { return running_; }
    // Moves newly found games into `out`; returns the folder currently being scanned.
    std::string poll(std::vector<FoundGame>& out);

private:
    void run(IdExpect expect);
    std::thread thread_;
    std::atomic<bool> running_{false}, cancel_{false};
    std::mutex mu_;
    std::vector<FoundGame> found_;
    std::string current_;
};
