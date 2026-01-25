#include "platform_factory.hpp"
#include "data_store.hpp"
#include "imgui/imgui_app.hpp"
#include "single_instance.hpp"
#include <iostream>
#include <csignal>
#include <memory>

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    // Ignore SIGCHLD to avoid zombies when killing processes
    signal(SIGCHLD, SIG_IGN);

    pex::SingleInstance instance;
    if (!instance.try_become_primary()) {
        // Another instance is running, signal sent to raise its window
        return 0;
    }

    try {
        // Create platform-specific providers (owned here in main)
        auto process_provider = pex::make_process_data_provider();
        auto details_provider = pex::make_details_data_provider();
        auto system_provider = pex::make_system_data_provider();
        auto killer = pex::make_process_killer();

        // Create DataStore - the data layer that can be shared across UIs
        pex::DataStore data_store(process_provider.get(), system_provider.get());

        // Create and run the ImGui application (UI layer)
        // ImGuiApp does not own these resources - they're managed here
        pex::ImGuiApp app(&data_store, system_provider.get(), details_provider.get(), killer.get());

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
