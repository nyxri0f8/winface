// WinFace CP - runs the face engine on a worker thread while the lock screen / prompt is up.
// Camera is on only while scanning; models stay loaded only as long as this DLL is loaded.
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../engine/decide.h"
#include "common.h"

namespace fgcp {

struct Snapshot {
    fg::State state = fg::State::Search;
    std::wstring hint = L"Starting camera...";
    float progress = 0;
    int direction = 0;
    bool has_face = false;
    std::vector<fg::Pt> mesh;   // 478 points normalised: centre (0,0), face height ~= 1
    bool running = false;
    bool waiting = false;       // recognised you behind the lock-screen curtain, waiting to show the head-turn prompt
};

class Scanner {
public:
    // on_done(unlocked, reason) is called from the worker thread
    using Done = std::function<void(bool, const std::string&)>;
    Scanner(Done on_done, const Config& cfg);
    ~Scanner();

    void start();        // no-op if already scanning
    void stop();         // camera off, thread joined
    void set_hold(bool h) { hold_ = h; }   // true: recognise but don't ask for the head turn (prompt not visible)
    Snapshot snapshot();

private:
    void run();
    bool ensure_models(std::string& err);

    Done done_;
    Config cfg_;
    std::thread th_;
    std::atomic<bool> stop_{false}, running_{false}, hold_{false};
    std::mutex mu_;
    Snapshot snap_;

    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<fg::FaceMesh> mesh_;
    std::unique_ptr<fg::Recognizer> rec_;
    std::unique_ptr<fg::Texture> tex_;
};

}  // namespace fgcp
