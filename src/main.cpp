#include "app.hpp"
#include <iostream>
#include <csignal>

int main(int argc, char* argv[]) {
    // Ignore SIGCHLD to avoid zombies when killing processes
    signal(SIGCHLD, SIG_IGN);

    try {
        pex::App app;
        app.run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
