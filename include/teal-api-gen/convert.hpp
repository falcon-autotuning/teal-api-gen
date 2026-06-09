#include "teal-api-gen/teal-api-gen-export.h"
#include <yaml-cpp/yaml.h>

namespace teal_api_gen {

// Converts the YAML schema to Teal code and writes it to the output stream.
// Throws std::runtime_error on error.
TEAL_API_GEN_EXPORT void convert_yml(const YAML::Node &schema,
                                     std::ostream &os);

} // namespace teal_api_gen
