#include "app.hpp"
#include "single_instance.hpp"
#include <iostream>
#include <csignal>

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    // Ignore SIGCHLD to avoid zombies when killing processes
    signal(SIGCHLD, SIG_IGN);

    pex::SingleInstance instance;
    if (!instance.try_become_primary()) {
        // Another instance is running, signal sent to raise its window
        return 0;
    }

    try {
        pex::App app;
        instance.set_raise_callback([&app]() {
            app.request_focus();
        });
        app.run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
