#include "platform/platform_factory.hpp"
#include "core/services/data_store.hpp"
#include "ui/tui/tui_app.hpp"
#include <iostream>
#include <csignal>
#include <cstdio>
#include <memory>

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    // Ignore SIGCHLD to avoid zombies when killing processes
    struct sigaction sa{};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa, nullptr);

    try {
        // Create platform-specific providers (owned here in main)
        const auto process_provider = pex::make_process_data_provider();
        const auto details_provider = pex::make_details_data_provider();
        const auto system_provider = pex::make_system_data_provider();
        const auto killer = pex::make_process_killer();

        // Create DataStore - the data layer that can be shared across UIs
        pex::DataStore data_store(process_provider.get(), system_provider.get());

        // Create and run the TUI application (UI layer)
        pex::TuiApp app(&data_store, system_provider.get(), details_provider.get(), killer.get());
        app.run();
        return 0;
    } catch (const std::exception& e) {
        // Make sure we restore terminal state before printing error
        // Only call endwin() if ncurses was initialized
        if (stdscr != nullptr) {
            // Disable mouse tracking
            std::printf("\033[?1000l");
            // Reset terminal title
            std::printf("\033]0;\007");
            std::fflush(stdout);
            endwin();
        }
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
