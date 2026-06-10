#include "platform/platform_factory.hpp"
#include "core/services/data_store.hpp"
#include "core/services/history_store.hpp"
#include "ui/imgui/imgui_app.hpp"
#include "core/services/single_instance.hpp"
#include <iostream>
#include <csignal>
#include <memory>

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    // Ignore SIGCHLD to avoid zombies when killing processes
    struct sigaction sa{};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa, nullptr);

    pex::SingleInstance instance;
    if (!instance.try_become_primary()) {
        // Another instance is running, signal sent to raise its window
        return 0;
    }

    try {
        // Create platform-specific providers (owned here in main).
        // IMPORTANT: Declaration order matters for destruction safety.
        // C++ destroys locals in reverse declaration order, so:
        //   1. app is destroyed first (stops UI loop)
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

        pex::ImGuiApp app(&data_store, system_provider.get(), details_provider.get(), killer.get(),
                          &history_store);

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
