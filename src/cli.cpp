
#include "teal-api-gen/convert.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/parse.h>
namespace {
void print_help(const char *progname) {
  std::cerr << "Usage: " << progname << " <instrument_api.yml> [output.tl]\n";
  std::cerr << "  Converts an instrument YAML schema to a Teal (.tl) module.\n";
  std::cerr << "  If output.tl is omitted, output is written to stdout.\n";
}
} // namespace
int main(int argc, char *argv[]) {
  std::vector<std::string> args(argv, argv + argc);

  if (args.size() < 2) {
    print_help(args[0].c_str());
    return 1;
  }
  const std::string &input_path = args[1];
  std::string output_path;
  if (args.size() >= 3) {
    output_path = args[2];
  }

  try {
    YAML::Node schema = YAML::LoadFile(input_path);
    if (!output_path.empty()) {
      std::ofstream out(output_path);
      if (!out) {
        std::cerr << "Error: Could not open output file: " << output_path
                  << "\n";
        return 1;
      }
      teal_api_gen::convert_yml(schema, out);
    } else {
      teal_api_gen::convert_yml(schema, std::cout);
    }
  } catch (const std::exception &ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
