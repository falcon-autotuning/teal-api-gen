#include "teal-api-gen/export.h"
#include <yaml-cpp/yaml.h>

namespace teal_api_gen {

// Converts the YAML schema to Teal code and writes it to the output stream.
// Throws std::runtime_error on error.
void TEAL_API_GEN_API convert_yml(const YAML::Node &schema, std::ostream &os);

} // namespace teal_api_gen
