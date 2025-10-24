#include <iostream>
#include <string>
#include <cstring>

int main(int argc, char* argv[]) {
    std::cout << "UI Sandbox - Component Testing & Demo Environment" << std::endl;

    // Parse command line arguments
    std::string demo = "all";
    int httpPort = 0;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--component") == 0 && i + 1 < argc) {
            demo = argv[++i];
        } else if (std::strcmp(argv[i], "--http-port") == 0 && i + 1 < argc) {
            httpPort = std::stoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: ui-sandbox [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --component <name>   Show specific component (button, shapes, layout)" << std::endl;
            std::cout << "  --http-port <port>   Enable HTTP debug server on port" << std::endl;
            std::cout << "  --help               Show this help message" << std::endl;
            return 0;
        }
    }

    std::cout << "Demo: " << demo << std::endl;
    if (httpPort > 0) {
        std::cout << "HTTP debug server will run on port " << httpPort << std::endl;
    }

    // TODO: Initialize renderer
    // TODO: Load requested demo
    // TODO: Start HTTP server if requested
    // TODO: Run main loop with UI inspector enabled

    return 0;
}
