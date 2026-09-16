#include "teal-api-gen/convert.hpp"
#include <iostream>
#include <string>
#include <yaml-cpp/yaml.h>
namespace {
// Helper: sanitize module name
std::string sanitize_module_name(const std::string &s) {
  std::string out;
  for (char character : s) {
    if (isalnum(character) != 0) {
      out += character;
    }
  }
  return out.empty() ? "InstrumentAPI" : out;
}

// Helper: camelCase from command
std::string camel_from_cmd(const std::string &cmd) {
  std::string out;
  bool upper = false;
  for (char character : cmd) {
    if (character == '_') {
      upper = true;
    } else {
      out += static_cast<char>(upper ? toupper(character) : tolower(character));
      upper = false;
    }
  }
  return out;
}

// Helper: Teal type from schema
std::string teal_type_from_schema(const std::string &t) {
  if (t == "int" || t == "float") {
    return "number";
  }
  if (t == "string") {
    return "string";
  }
  if (t == "bool") {
    return "boolean";
  }
  if (t == "array") {
    return "MeasureResponse";
  }
  return "any";
}

// Emit helpers (as in Lua)
void emit_helpers(std::ostream &os) {
  os << "global instrument_call_stack: any\n\n";
}
// Helper: get string or empty
std::string get_str(const YAML::Node &n, const char *key) {
  return (n[key] && n[key].IsScalar()) ? n[key].as<std::string>() : "";
}

// Helper: join vector with separator
std::string join(const std::vector<std::string> &vec, const std::string &sep) {
  std::ostringstream oss;
  for (size_t i = 0; i < vec.size(); ++i) {
    if (i != 0U) {
      oss << sep;
    }
    oss << vec[i];
  }
  return oss.str();
}
} // namespace
namespace teal_api_gen {
void convert_yml(const YAML::Node &instrument, std::ostream &os) {
  // Extract metadata
  auto inst_meta = instrument["instrument"];
  std::string vendor = get_str(inst_meta, "vendor");
  std::string model = get_str(inst_meta, "model");
  std::string identifier = get_str(inst_meta, "identifier");
  std::string module_name = sanitize_module_name(vendor + model + identifier);

  os << "-- Auto-generated Teal module from instrument YAML (vendor=" << vendor
     << " model=" << model << " id=" << identifier << ")\n\n";
  emit_helpers(os);
  os << module_name << " = {}\n\n";

  // Gather channel_groups by name for lookup
  std::map<std::string, YAML::Node> channel_groups_by_name;
  if (instrument["channel_groups"]) {
    for (const auto &cg : instrument["channel_groups"]) {
      channel_groups_by_name[get_str(cg, "name")] = YAML::Node(cg);
    }
  }
  const YAML::Node &io_list =
      instrument["io"] ? instrument["io"] : YAML::Node();

  if (instrument["commands"]) {
    for (const auto &cmd_pair : instrument["commands"]) {
      auto cmd_key = cmd_pair.first.as<std::string>();
      const YAML::Node &cmd_def = cmd_pair.second;
      std::string func_name = camel_from_cmd(cmd_key);
      std::string template_str = get_str(cmd_def, "template");

      // Collect placeholders in order
      std::vector<std::string> placeholders;
      std::string::size_type pos = 0;
      while ((pos = template_str.find('{', pos)) != std::string::npos) {
        auto end = template_str.find('}', pos);
        if (end != std::string::npos) {
          placeholders.push_back(template_str.substr(pos + 1, end - pos - 1));
          pos = end + 1;
        } else {
          break;
        }
      }

      std::string channel_group_name = get_str(cmd_def, "channel_group");
      std::string channel_param_name;
      YAML::Node channel_param_def;
      if (!channel_group_name.empty() &&
          (channel_groups_by_name.count(channel_group_name) != 0U)) {
        const auto &cg = channel_groups_by_name[channel_group_name];
        if (cg["channel_parameter"]) {
          channel_param_name = cg["channel_parameter"]["name"]
                                   ? get_str(cg["channel_parameter"], "name")
                                   : channel_group_name;
          channel_param_def = cg["channel_parameter"];
        }
      }

      // Map param defs (by name or io reference)
      std::map<std::string, YAML::Node> params_defs;
      if (cmd_def["parameters"]) {
        for (const auto &p : cmd_def["parameters"]) {
          if (p["name"]) {
            params_defs[get_str(p, "name")] = YAML::Node(p);
          }
          if (p["io"]) {
            params_defs[get_str(p, "io")] = YAML::Node(p);
          }
        }
      }

      // Detect usage of channel
      bool uses_channel = false;
      std::string channel_placeholder_found;
      if (!channel_group_name.empty()) {
        for (const auto &ph : placeholders) {
          if (ph == channel_group_name) {
            uses_channel = true;
            channel_placeholder_found = ph;
            break;
          }
        }
        if (!uses_channel && (params_defs.count(channel_group_name) != 0U)) {
          uses_channel = true;
          channel_placeholder_found = channel_group_name;
        }
      }

      // Build function parameter order
      std::vector<std::string> func_params = {"id"};
      if (uses_channel) {
        func_params.push_back(channel_param_name.empty() ? "channel"
                                                         : channel_param_name);
      }

      std::set<std::string> seen = {"id"};
      if (uses_channel && !channel_param_name.empty()) {
        seen.insert(channel_param_name);
      }

      for (const auto &ph : placeholders) {
        std::string pname = (uses_channel && ph == channel_placeholder_found)
                                ? channel_param_name
                                : ph;
        if (seen.count(pname) == 0U) {
          func_params.push_back(pname);
          seen.insert(pname);
        }
      }
      if (cmd_def["parameters"]) {
        for (const auto &p : cmd_def["parameters"]) {
          std::string pname = p["name"] ? get_str(p, "name") : get_str(p, "io");
          if (!pname.empty() && (seen.count(pname) == 0U)) {
            func_params.push_back(pname);
            seen.insert(pname);
          }
        }
      }

      // Documentation comments
      os << "--- "
         << (get_str(cmd_def, "description").empty()
                 ? cmd_key
                 : get_str(cmd_def, "description"))
         << "\n";

      // Param doclines and signature
      std::vector<std::string> sig_parts;
      std::vector<std::string> doc_lines;
      doc_lines.emplace_back(
          "--- @param id string Instrument instance identifier");
      sig_parts.emplace_back("id: string");
      if (uses_channel) {
        std::string chdesc =
            (channel_param_def && channel_param_def["description"])
                ? " " + get_str(channel_param_def, "description")
                : "";
        doc_lines.push_back("--- @param " + channel_param_name + " number" +
                            chdesc);
        sig_parts.push_back(channel_param_name + ": number");
      }
      // Other params
      for (const auto &p : func_params) {
        if (p == "id" || (uses_channel && p == channel_param_name)) {
          continue;
        }
        YAML::Node pdef =
            (params_defs.count(p) != 0U) ? params_defs[p] : YAML::Node();
        std::string ptype = "any";
        std::string pdesc;
        if (pdef) {
          if (pdef["type"]) {
            ptype = teal_type_from_schema(get_str(pdef, "type"));
          }
          if (pdef["io"] && io_list) {
            for (const auto &io : io_list) {
              if (get_str(io, "name") == get_str(pdef, "io") && io["type"]) {
                ptype = teal_type_from_schema(get_str(io, "type"));
              }
            }
          }
          if (pdef["description"]) {
            pdesc = get_str(pdef, "description");
          }
        }
        doc_lines.push_back("--- @param " + p + " " + ptype +
                            (pdesc.empty() ? "" : " " + pdesc));
        sig_parts.push_back(p + ": " + ptype);
      }

      // Outputs -> return type(s)
      std::string return_sig = "any";
      if (cmd_def["outputs"] && cmd_def["outputs"].IsSequence() &&
          cmd_def["outputs"].size() > 0) {
        if (cmd_def["outputs"].size() == 1) {
          // Try to find type from io or channel_group io_types
          auto out_name = cmd_def["outputs"][0].as<std::string>();
          std::string otype = "any";
          if (!channel_group_name.empty() &&
              (channel_groups_by_name.count(channel_group_name) != 0U)) {
            const auto &cg = channel_groups_by_name[channel_group_name];
            if (cg["io_types"]) {
              for (const auto &iot : cg["io_types"]) {
                if (get_str(iot, "suffix") == out_name && iot["type"]) {
                  otype = teal_type_from_schema(get_str(iot, "type"));
                }
              }
            }
          }
          if (otype == "any" && io_list) {
            for (const auto &io : io_list) {
              if (get_str(io, "name") == out_name && io["type"]) {
                otype = teal_type_from_schema(get_str(io, "type"));
              }
            }
          }
          return_sig = otype;
          doc_lines.push_back("--- @return " + otype + " " + out_name);
        } else {
          // Multiple outputs: build record
          std::vector<std::string> rec_parts;
          for (const auto &out : cmd_def["outputs"]) {
            auto out_name = out.as<std::string>();
            std::string otype = "any";
            if (!channel_group_name.empty() &&
                (channel_groups_by_name.count(channel_group_name) != 0U)) {
              const auto &cg = channel_groups_by_name[channel_group_name];
              if (cg["io_types"]) {
                for (const auto &iot : cg["io_types"]) {
                  if (get_str(iot, "suffix") == out_name && iot["type"]) {
                    otype = teal_type_from_schema(get_str(iot, "type"));
                  }
                }
              }
            }
            if (otype == "any" && io_list) {
              for (const auto &io : io_list) {
                if (get_str(io, "name") == out_name && io["type"]) {
                  otype = teal_type_from_schema(get_str(io, "type"));
                }
              }
            }
            rec_parts.push_back(out_name + ": " + otype);
          }
          return_sig = "{" + join(rec_parts, ", ") + "}";
          doc_lines.push_back("--- @return table " + return_sig);
        }
      } else {
        doc_lines.emplace_back("--- @return any");
      }

      // Emit doc lines
      for (const auto &dl : doc_lines) {
        os << dl << "\n";
      }

      // Function signature
      os << "function " << module_name << ":" << func_name << "("
         << join(sig_parts, ", ") << "): " << return_sig << "\n";

      // Build named params table. Channel-group commands still require the
      // channel parameter in the command payload; CallStack.channel carries the
      // same value as target metadata for the gRPC API.
      std::vector<std::pair<std::string, std::string>> named_params;
      for (const auto &param_name : func_params) {
        if (param_name == "id") {
          continue;
        }
        named_params.push_back({param_name, param_name});
      }

      os << "  local cs = instrument_call_stack.new({\n"
         << "    instrument = id,\n"
         << "    command = \"" << cmd_key << "\",\n";
      if (uses_channel && !channel_param_name.empty()) {
        os << "    channel = " << channel_param_name << ",\n";
      }
      os << "  })\n";

      if (uses_channel) {
        std::vector<std::string> positional_params;
        for (const auto &param_name : func_params) {
          if (param_name != "id" && param_name != channel_param_name) {
            positional_params.push_back(param_name);
          }
        }

        if (!positional_params.empty()) {
          os << "  return context:call(cs, " << join(positional_params, ", ")
             << ")\n";
        } else {
          os << "  return context:call(cs)\n";
        }
      } else if (!named_params.empty()) {
        std::string table_entries;
        for (size_t i = 0; i < named_params.size(); ++i) {
          if (i > 0)
            table_entries += ", ";
          table_entries +=
              named_params[i].first + " = " + named_params[i].second;
        }
        os << "  return context:call(cs, {" << table_entries << "})\n";
      } else {
        os << "  return context:call(cs)\n";
      }

      os << "end\n\n";
    }
  }
  os << "return " << module_name << "\n";
}
} // namespace teal_api_gen
