// main_tui_vms.cpp -- OpenVMS entry point for pexc (TUI process explorer)
//
// This is the VMS-specific equivalent of main_tui.cpp. It differs from the
// Unix version in that:
//   - No POSIX signals (SIGCHLD, sigaction) -- VMS uses different mechanisms
//   - No ncurses cleanup (stdscr, endwin) -- SMG$ handles terminal restoration
//   - No single-instance check (Unix domain sockets not available)

#include "platform/platform_factory.hpp"
#include "core/services/data_store.hpp"
#include "ui/tui_vms/smg_app.hpp"
#include <iostream>
#include <memory>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    try {
        std::cerr << "[M1] Creating providers..." << std::endl;
        const auto process_provider = pex::make_process_data_provider();
        const auto details_provider = pex::make_details_data_provider();
        const auto system_provider = pex::make_system_data_provider();
        const auto killer = pex::make_process_killer();
        std::cerr << "[M2] Providers created" << std::endl;

        std::cerr << "[M3] Creating DataStore..." << std::endl;
        pex::DataStore data_store(process_provider.get(), system_provider.get());
        std::cerr << "[M4] DataStore created" << std::endl;

        std::cerr << "[M5] Creating SmgApp..." << std::endl;
        pex::SmgApp app(&data_store, system_provider.get(), details_provider.get(), killer.get());
        std::cerr << "[M6] SmgApp created" << std::endl;

        std::cerr << "[M7] Calling app.run()..." << std::endl;
        app.run();
        std::cerr << "[M8] app.run() returned" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
