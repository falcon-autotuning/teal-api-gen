#include "generator.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <instrument.json>\n";
        return 1;
    }
    try {
        std::string result = generate_from_file(argv[1]);
        std::cout << result;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
