#include "platform/platform_factory.hpp"
#include "core/services/data_store.hpp"
#include "core/services/history_store.hpp"
#include "ui/tui/tui_app.hpp"
#include <iostream>
#include <csignal>
#include <cstdio>
#include <memory>

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#ifndef _WIN32
    // Ignore SIGCHLD to avoid zombies when killing processes
    struct sigaction sa{};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa, nullptr);
#endif

    try {
        // Create platform-specific providers (owned here in main).
        // IMPORTANT: Declaration order matters for destruction safety.
        // C++ destroys locals in reverse declaration order, so:
        //   1. app is destroyed first (stops UI loop, restores terminal)
        //   2. data_store is destroyed next (joins background thread)
        //   3. providers are destroyed last (safe, no longer referenced)
        // Do NOT reorder these declarations without verifying destruction safety.
        const auto process_provider = pex::make_process_data_provider();
        const auto details_provider = pex::make_details_data_provider();
        const auto system_provider = pex::make_system_data_provider();
        const auto killer = pex::make_process_killer();

        pex::HistoryStore history_store;
        pex::DataStore data_store(process_provider.get(), system_provider.get());
        data_store.set_history_store(&history_store);

        pex::TuiApp app(&data_store, system_provider.get(), details_provider.get(), killer.get(),
                        &history_store);
        app.run();
        return 0;
    } catch (const std::exception& e) {
        // Make sure we restore terminal state before printing error
        // Only call endwin() if curses was initialized
        if (stdscr != nullptr) {
#ifndef _WIN32
            // Disable mouse tracking + reset terminal title
            std::printf("\033[?1000l");
            std::printf("\033]0;\007");
            std::fflush(stdout);
#endif
            endwin();
        }
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
