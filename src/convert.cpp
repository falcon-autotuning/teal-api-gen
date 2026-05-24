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
  os << "-- Helper numeric utilities for precision handling\n";
  os << "local function _log10(x: number): number\n"
        "  return math.log(x) / math.log(10)\n"
        "end\n\n";
  os << "local function _round_to_sig(x: number, n: number): number\n"
        "  if x == 0 then return 0 end\n"
        "  local d = math.floor(_log10(math.abs(x)))\n"
        "  local scale = 10 ^ (d - n + 1)\n"
        "  return math.floor((x / scale) + 0.5) * scale\n"
        "end\n\n";
  os << "local function _round_to_multiple(x: number, step: number): number\n"
        "  if step == 0 then return x end\n"
        "  return math.floor((x / step) + 0.5) * step\n"
        "end\n\n";
  os << "local function _int_keep_significant(v: number, n: number): number\n"
        "  if v == 0 then return 0 end\n"
        "  local neg = v < 0\n"
        "  local a = math.abs(v)\n"
        "  local digits = math.floor(_log10(a)) + 1\n"
        "  if digits <= n then return v end\n"
        "  local factor = 10 ^ (digits - n)\n"
        "  local res = math.floor((a / factor) + 0.5) * factor\n"
        "  if neg then res = -res end\n"
        "  return res\n"
        "end\n\n";
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
void emit_clamp_block(std::ostream &os, const std::string &indent,
                      const std::string &pname, const std::string &op,
                      const std::string &bound,
                      const std::string &log_from = "_old_", // or "_post_"
                      const std::string &log_suffix = "") {
  os << indent << "if " << pname << " " << op << " " << bound << " then\n"
     << indent << "  " << pname << " = " << bound << "\n"
     << indent << "  context:log(\"Clamped " << pname << log_suffix
     << " from \" .. tostring(" << log_from << pname
     << ") .. \" to \" .. tostring(" << pname << "))\n"
     << indent << "end\n";
}

void emit_clamp_and_precision(std::ostream &os, const std::string &pname,
                              const std::string &p_schema_type,
                              const YAML::Node &pdef,
                              const std::string &indent = "  ") {
  bool has_min = pdef["min"].IsDefined();
  bool has_max = pdef["max"].IsDefined();
  bool has_prec = pdef["precision"].IsDefined();
  if (!has_min && !has_max && !has_prec) {
    return;
  }

  std::string pmin = has_min ? std::to_string(pdef["min"].as<double>()) : "";
  std::string pmax = has_max ? std::to_string(pdef["max"].as<double>()) : "";
  std::string prec =
      has_prec ? std::to_string(pdef["precision"].as<double>()) : "";

  os << indent << "local _old_" << pname << " = " << pname << "\n";
  if (has_min) {
    emit_clamp_block(os, indent, pname, "<", pmin);
  }
  if (has_max) {
    emit_clamp_block(os, indent, pname, ">", pmax);
  }

  bool is_int_schema = (p_schema_type == "int");
  if (is_int_schema) {
    os << indent << pname << " = math.floor(" << pname << ")\n";
  }
  if (has_prec) {
    bool prec_is_int =
        (pdef["precision"].IsScalar() &&
         pdef["precision"].as<double>() == (int)pdef["precision"].as<double>());
    if (p_schema_type == "float") {
      if (prec_is_int) {
        os << indent << "local _pre_" << pname << " = " << pname << "\n"
           << indent << pname << " = _round_to_sig(" << pname << ", " << prec
           << ")\n"
           << indent << "if " << pname << " ~= _pre_" << pname
           << " then context:log(\"Applied significant-digit precision to "
           << pname << ": \" .. tostring(_pre_" << pname
           << ") .. \" -> \" .. tostring(" << pname << ")) end\n";
      } else {
        os << indent << "local _pre_" << pname << " = " << pname << "\n"
           << indent << pname << " = _round_to_multiple(" << pname << ", "
           << prec << ")\n"
           << indent << "if " << pname << " ~= _pre_" << pname
           << " then context:log(\"Rounded " << pname << " to nearest multiple "
           << prec << ": \" .. tostring(_pre_" << pname
           << ") .. \" -> \" .. tostring(" << pname << ")) end\n";
      }
    } else if (p_schema_type == "int") {
      if (prec_is_int) {
        os << indent << "local _pre_" << pname << " = " << pname << "\n"
           << indent << pname << " = _int_keep_significant(" << pname << ", "
           << prec << ")\n"
           << indent << "if " << pname << " ~= _pre_" << pname
           << " then context:log(\"Applied integer significant-digit precision "
              "to "
           << pname << ": \" .. tostring(_pre_" << pname
           << ") .. \" -> \" .. tostring(" << pname << ")) end\n";
      } else {
        os << indent << "context:log(\"Warning: precision " << prec
           << " for integer parameter " << pname
           << " is fractional - ignoring precision\")\n";
      }
    } else {
      os << indent << "context:log(\"Warning: precision specified for " << pname
         << " but parameter type unknown - ignoring precision\")\n";
    }
    // Re-clamp after precision
    if (has_min || has_max) {
      os << indent << "local _post_" << pname << " = " << pname << "\n";
      if (has_min) {
        emit_clamp_block(os, indent, pname, "<", pmin, "_post_",
                         " after precision");
      }
      if (has_max) {
        emit_clamp_block(os, indent, pname, ">", pmax, "_post_",
                         " after precision");
      }
    }
  }
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
        if (cg["channel_parameter"] && cg["channel_parameter"]["name"]) {
          channel_param_name = get_str(cg["channel_parameter"], "name");
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

      // Emit clamping + precision for channel param if used
      if (uses_channel && !channel_param_name.empty() && channel_param_def) {
        emit_clamp_and_precision(os, channel_param_name,
                                 get_str(channel_param_def, "type"),
                                 channel_param_def);
      }

      // Emit clamping + precision for other params
      if (cmd_def["parameters"]) {
        for (const auto &p : cmd_def["parameters"]) {
          std::string pname = p["name"] ? get_str(p, "name") : get_str(p, "io");
          if (pname.empty() || (uses_channel && pname == channel_param_name)) {
            continue;
          }
          emit_clamp_and_precision(os, pname, get_str(p, "type"), p);
        }
      }

      // Build command id string - channel is passed as a named param in a table,
      // not encoded in the command name, so the plugin receives it by name.
      std::string cmd_id_expr = "id .. '." + cmd_key + "'";

      // Build named params table: channel first (using placeholder name as key),
      // then remaining params.
      std::vector<std::pair<std::string, std::string>> named_params;
      if (uses_channel && !channel_param_name.empty() &&
          !channel_placeholder_found.empty()) {
        // Key = template placeholder (e.g. "analog"), value = Lua param name
        named_params.push_back({channel_placeholder_found, channel_param_name});
      }
      for (const auto &param_name : func_params) {
        if (param_name == "id" ||
            (uses_channel && param_name == channel_param_name)) {
          continue;
        }
        named_params.push_back({param_name, param_name});
      }

      if (!named_params.empty()) {
        std::string table_entries;
        for (size_t i = 0; i < named_params.size(); ++i) {
          if (i > 0)
            table_entries += ", ";
          table_entries +=
              named_params[i].first + " = " + named_params[i].second;
        }
        os << "  return context:call(" << cmd_id_expr << ", {" << table_entries
           << "})\n";
      } else {
        os << "  return context:call(" << cmd_id_expr << ")\n";
      }

      os << "end\n\n";
    }
  }
  os << "return " << module_name << "\n";
}
} // namespace teal_api_gen
