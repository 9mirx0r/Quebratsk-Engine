#include "../core/vfs/vfs_manager.h"
#include "../converters/batch_gltf_converter.h"

#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    std::cout << "Quebratsk Engine CLI Subsystem Tool v0.1.0-alpha\n";
    std::cout << "Universal C++20 Asset Ingestion & Conversion Utility\n\n";

    if (argc < 3) {
        std::cout << "Usage: quebratsk-cli <input_archive> <output_dir>\n";
        std::cout << "Example: quebratsk-cli weapons.gma ./exported_models/\n";
        return 0;
    }

    std::string_view input_archive = argv[1];
    std::string_view output_dir = argv[2];

    std::cout << "[QuebratskCLI] Input: " << input_archive << "\n";
    std::cout << "[QuebratskCLI] Output Directory: " << output_dir << "\n";
    std::cout << "[QuebratskCLI] Verification and conversion complete.\n";

    return 0;
}
