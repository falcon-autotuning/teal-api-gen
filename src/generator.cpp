#include "generator.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------

std::string lua_number_tostring(double v) {
    // Replicate Lua 5.4 tostring() for float values decoded by cjson.
    // cjson in Lua 5.4 decodes every JSON number (integer or float) as a Lua
    // float.  Lua formats floats with "%.14g", then appends ".0" when the
    // result contains neither '.' nor 'e'/'E', to distinguish them from
    // integer values.
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.14g", v);
    std::string s = buf;
    if (s.find('.') == std::string::npos &&
        s.find('e') == std::string::npos &&
        s.find('E') == std::string::npos) {
        s += ".0";
    }
    return s;
}

bool is_integer_value(double v) {
    return std::floor(v) == v;
}

std::string sanitize_module_name(const std::string& s) {
    std::string result;
    for (unsigned char c : s) {
        if (std::isalnum(c)) result += static_cast<char>(c);
    }
    if (result.empty()) return "InstrumentAPI";
    return result;
}

std::string camel_from_cmd(const std::string& cmd) {
    // Strip leading underscores
    std::string s = cmd;
    std::size_t start = s.find_first_not_of('_');
    if (start == std::string::npos) {
        std::string r = cmd;
        std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return r;
    }
    if (start > 0) s = s.substr(start);

    // Normalise multiple consecutive underscores to a single one
    std::string normalised;
    bool last_under = false;
    for (char c : s) {
        if (c == '_') {
            if (!last_under) normalised += '_';
            last_under = true;
        } else {
            normalised += c;
            last_under = false;
        }
    }

    // Split on underscore
    std::vector<std::string> parts;
    std::istringstream ss(normalised);
    std::string part;
    while (std::getline(ss, part, '_')) {
        if (!part.empty()) {
            std::transform(part.begin(), part.end(), part.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            parts.push_back(part);
        }
    }

    if (parts.empty()) {
        std::string r = cmd;
        std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return r;
    }

    std::string out = parts[0];
    for (std::size_t i = 1; i < parts.size(); ++i) {
        std::string p = parts[i];
        if (!p.empty()) {
            p[0] = static_cast<char>(
                std::toupper(static_cast<unsigned char>(p[0])));
        }
        out += p;
    }
    return out;
}

std::string teal_type_from_schema(const std::string& t) {
    if (t == "int" || t == "float") return "number";
    if (t == "string") return "string";
    if (t == "bool") return "boolean";
    return "any";
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

const ordered_json* find_io_by_name(const ordered_json& io_list,
                                    const std::string& name) {
    for (const auto& io : io_list) {
        if (io.contains("name") && io["name"].get<std::string>() == name)
            return &io;
    }
    return nullptr;
}

struct OutputType {
    std::string name;
    std::string type; // empty = nil/unknown
};

std::vector<OutputType> find_output_types(
    const std::vector<std::string>& outputs,
    const std::string& channel_group_name,
    const ordered_json& top_io,
    const std::map<std::string, const ordered_json*>& channel_groups_by_name) {

    std::vector<OutputType> result;
    if (outputs.empty()) return result;

    if (!channel_group_name.empty()) {
        auto it = channel_groups_by_name.find(channel_group_name);
        if (it != channel_groups_by_name.end() &&
            it->second->contains("io_types")) {
            const auto& io_types = (*it->second)["io_types"];
            for (const auto& suffix : outputs) {
                OutputType ot;
                ot.name = suffix;
                for (const auto& iot : io_types) {
                    if (iot.contains("suffix") &&
                        iot["suffix"].get<std::string>() == suffix) {
                        if (iot.contains("type"))
                            ot.type = iot["type"].get<std::string>();
                        break;
                    }
                }
                result.push_back(ot);
            }
            return result;
        }
    }

    // No channel group match — search top-level io list
    for (const auto& out : outputs) {
        OutputType ot;
        ot.name = out;
        const auto* io_entry = find_io_by_name(top_io, out);
        if (io_entry && io_entry->contains("type"))
            ot.type = (*io_entry)["type"].get<std::string>();
        result.push_back(ot);
    }
    return result;
}

void emit_helpers(std::vector<std::string>& lines) {
    lines.push_back("-- Helper numeric utilities for precision handling");
    lines.push_back("local function _log10(x: number): number");
    lines.push_back(
        "  -- math.log10 might not be available; use change-of-base");
    lines.push_back("  return math.log(x) / math.log(10)");
    lines.push_back("end");
    lines.push_back("");
    lines.push_back("local function _round_to_sig(x: number, n: number): number");
    lines.push_back("  if x == 0 then return 0 end");
    lines.push_back("  local d = math.floor(_log10(math.abs(x)))");
    lines.push_back("  local scale = 10 ^ (d - n + 1)");
    lines.push_back("  return math.floor((x / scale) + 0.5) * scale");
    lines.push_back("end");
    lines.push_back("");
    lines.push_back(
        "local function _round_to_multiple(x: number, step: number): number");
    lines.push_back("  if step == 0 then return x end");
    lines.push_back("  return math.floor((x / step) + 0.5) * step");
    lines.push_back("end");
    lines.push_back("");
    lines.push_back(
        "local function _int_keep_significant(v: number, n: number): number");
    lines.push_back("  if v == 0 then return 0 end");
    lines.push_back("  local neg = v < 0");
    lines.push_back("  local a = math.abs(v)");
    lines.push_back("  local digits = math.floor(_log10(a)) + 1");
    lines.push_back("  if digits <= n then return v end");
    lines.push_back("  local factor = 10 ^ (digits - n)");
    lines.push_back("  local res = math.floor((a / factor) + 0.5) * factor");
    lines.push_back("  if neg then res = -res end");
    lines.push_back("  return res");
    lines.push_back("end");
    lines.push_back("");
}

// Emit clamping and precision-handling code for a single parameter, exactly
// replicating the Lua script's emit_clamp_and_precision() function.
void emit_clamp_and_precision(std::vector<std::string>& lines,
                               const std::string& pname,
                               const std::string& p_schema_type,
                               const double* pmin, const double* pmax,
                               const double* precision,
                               const std::string& indent = "  ") {
    if (!pmin && !pmax && !precision) return;

    auto clamp_str = [&](double v) { return lua_number_tostring(v); };

    // Save original value
    lines.push_back(indent + "local _old_" + pname + " = " + pname);

    // Initial clamp
    if (pmin && pmax) {
        lines.push_back(indent + "if " + pname + " < " + clamp_str(*pmin) +
                         " then");
        lines.push_back(indent + "  " + pname + " = " + clamp_str(*pmin));
        lines.push_back(indent + "  context:log(\"Clamped " + pname +
                         " from \" .. tostring(_old_" + pname +
                         ") .. \" to \" .. tostring(" + pname + "))");
        lines.push_back(indent + "elseif " + pname + " > " +
                         clamp_str(*pmax) + " then");
        lines.push_back(indent + "  " + pname + " = " + clamp_str(*pmax));
        lines.push_back(indent + "  context:log(\"Clamped " + pname +
                         " from \" .. tostring(_old_" + pname +
                         ") .. \" to \" .. tostring(" + pname + "))");
        lines.push_back(indent + "end");
    } else if (pmin) {
        lines.push_back(indent + "if " + pname + " < " + clamp_str(*pmin) +
                         " then");
        lines.push_back(indent + "  " + pname + " = " + clamp_str(*pmin));
        lines.push_back(indent + "  context:log(\"Clamped " + pname +
                         " from \" .. tostring(_old_" + pname +
                         ") .. \" to \" .. tostring(" + pname + "))");
        lines.push_back(indent + "end");
    } else {
        lines.push_back(indent + "if " + pname + " > " + clamp_str(*pmax) +
                         " then");
        lines.push_back(indent + "  " + pname + " = " + clamp_str(*pmax));
        lines.push_back(indent + "  context:log(\"Clamped " + pname +
                         " from \" .. tostring(_old_" + pname +
                         ") .. \" to \" .. tostring(" + pname + "))");
        lines.push_back(indent + "end");
    }

    // Floor integer parameters before precision
    bool is_int_schema = (p_schema_type == "int");
    if (is_int_schema) {
        lines.push_back(indent + pname + " = math.floor(" + pname + ")");
    }

    // Precision
    if (precision) {
        bool prec_is_int = is_integer_value(*precision);

        if (p_schema_type == "float") {
            if (prec_is_int) {
                char ibuf[32];
                std::snprintf(ibuf, sizeof(ibuf), "%d",
                              static_cast<int>(*precision));
                lines.push_back(indent + "local _pre_" + pname + " = " +
                                 pname);
                lines.push_back(indent + pname + " = _round_to_sig(" + pname +
                                 ", " + ibuf + ")");
                lines.push_back(
                    indent + "if " + pname + " ~= _pre_" + pname +
                    " then context:log(\"Applied significant-digit precision to " +
                    pname + ": \" .. tostring(_pre_" + pname +
                    ") .. \" -> \" .. tostring(" + pname + ")) end");
            } else {
                std::string ps = lua_number_tostring(*precision);
                lines.push_back(indent + "local _pre_" + pname + " = " +
                                 pname);
                lines.push_back(indent + pname + " = _round_to_multiple(" +
                                 pname + ", " + ps + ")");
                lines.push_back(
                    indent + "if " + pname + " ~= _pre_" + pname +
                    " then context:log(\"Rounded " + pname +
                    " to nearest multiple " + ps + ": \" .. tostring(_pre_" +
                    pname + ") .. \" -> \" .. tostring(" + pname + ")) end");
            }
        } else if (p_schema_type == "int") {
            if (prec_is_int) {
                char ibuf[32];
                std::snprintf(ibuf, sizeof(ibuf), "%d",
                              static_cast<int>(*precision));
                lines.push_back(indent + "local _pre_" + pname + " = " +
                                 pname);
                lines.push_back(indent + pname +
                                 " = _int_keep_significant(" + pname + ", " +
                                 ibuf + ")");
                lines.push_back(
                    indent + "if " + pname + " ~= _pre_" + pname +
                    " then context:log(\"Applied integer significant-digit "
                    "precision to " +
                    pname + ": \" .. tostring(_pre_" + pname +
                    ") .. \" -> \" .. tostring(" + pname + ")) end");
            } else {
                lines.push_back(
                    indent + "context:log(\"Warning: precision " +
                    lua_number_tostring(*precision) + " for integer parameter " +
                    pname + " is fractional - ignoring precision\")");
            }
        } else {
            lines.push_back(
                indent +
                "context:log(\"Warning: precision specified for " + pname +
                " but parameter type unknown - ignoring precision\")");
        }

        // Post-precision re-clamp
        if (pmin || pmax) {
            lines.push_back(indent + "local _post_" + pname + " = " + pname);
            if (pmin && pmax) {
                lines.push_back(indent + "if " + pname + " < " +
                                 clamp_str(*pmin) + " then");
                lines.push_back(indent + "  " + pname + " = " +
                                 clamp_str(*pmin));
                lines.push_back(
                    indent + "  context:log(\"Clamped " + pname +
                    " after precision from \" .. tostring(_post_" + pname +
                    ") .. \" to \" .. tostring(" + pname + "))");
                lines.push_back(indent + "elseif " + pname + " > " +
                                 clamp_str(*pmax) + " then");
                lines.push_back(indent + "  " + pname + " = " +
                                 clamp_str(*pmax));
                lines.push_back(
                    indent + "  context:log(\"Clamped " + pname +
                    " after precision from \" .. tostring(_post_" + pname +
                    ") .. \" to \" .. tostring(" + pname + "))");
                lines.push_back(indent + "end");
            } else if (pmin) {
                lines.push_back(indent + "if " + pname + " < " +
                                 clamp_str(*pmin) + " then");
                lines.push_back(indent + "  " + pname + " = " +
                                 clamp_str(*pmin));
                lines.push_back(
                    indent + "  context:log(\"Clamped " + pname +
                    " after precision from \" .. tostring(_post_" + pname +
                    ") .. \" to \" .. tostring(" + pname + "))");
                lines.push_back(indent + "end");
            } else {
                lines.push_back(indent + "if " + pname + " > " +
                                 clamp_str(*pmax) + " then");
                lines.push_back(indent + "  " + pname + " = " +
                                 clamp_str(*pmax));
                lines.push_back(
                    indent + "  context:log(\"Clamped " + pname +
                    " after precision from \" .. tostring(_post_" + pname +
                    ") .. \" to \" .. tostring(" + pname + "))");
                lines.push_back(indent + "end");
            }
        }
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string build_module(const ordered_json& instrument) {
    // Extract metadata
    const auto& inst_meta =
        instrument.contains("instrument") ? instrument["instrument"]
                                          : ordered_json::object();
    std::string vendor =
        inst_meta.contains("vendor") ? inst_meta["vendor"].get<std::string>()
                                     : "";
    std::string model =
        inst_meta.contains("model") ? inst_meta["model"].get<std::string>()
                                    : "";
    std::string identifier =
        inst_meta.contains("identifier")
            ? inst_meta["identifier"].get<std::string>()
            : "";
    std::string module_name = sanitize_module_name(vendor + model + identifier);
    if (module_name.empty()) module_name = "InstrumentAPI";

    const auto& io_list =
        instrument.contains("io") ? instrument["io"] : ordered_json::array();
    const auto& cg_list = instrument.contains("channel_groups")
                              ? instrument["channel_groups"]
                              : ordered_json::array();

    // Build channel_groups_by_name map (preserves pointers into cg_list)
    std::map<std::string, const ordered_json*> channel_groups_by_name;
    for (const auto& cg : cg_list) {
        if (cg.contains("name"))
            channel_groups_by_name[cg["name"].get<std::string>()] = &cg;
    }

    const auto& commands =
        instrument.contains("commands") ? instrument["commands"]
                                        : ordered_json::object();

    std::vector<std::string> lines;

    // Header comment
    lines.push_back(
        "-- Auto-generated Teal module from instrument JSON (vendor=" + vendor +
        " model=" + model + " id=" + identifier + ")");
    lines.push_back("");

    emit_helpers(lines);

    lines.push_back("local " + module_name + " = {}");
    lines.push_back("");

    // Iterate commands in JSON insertion order (ordered_json)
    for (const auto& [cmd_key, cmd_def] : commands.items()) {
        std::string func_name = camel_from_cmd(cmd_key);

        std::string template_str =
            cmd_def.contains("template")
                ? cmd_def["template"].get<std::string>()
                : "";

        // Extract placeholders in order from the template
        std::vector<std::string> placeholders;
        {
            std::size_t pos = 0;
            while ((pos = template_str.find('{', pos)) != std::string::npos) {
                std::size_t end = template_str.find('}', pos + 1);
                if (end == std::string::npos) break;
                placeholders.push_back(
                    template_str.substr(pos + 1, end - pos - 1));
                pos = end + 1;
            }
        }

        // Channel group info
        std::string channel_group_name =
            cmd_def.contains("channel_group")
                ? cmd_def["channel_group"].get<std::string>()
                : "";
        std::string channel_param_name;
        const ordered_json* channel_param_def = nullptr;
        if (!channel_group_name.empty()) {
            auto it = channel_groups_by_name.find(channel_group_name);
            if (it != channel_groups_by_name.end()) {
                const auto& cg = *it->second;
                if (cg.contains("channel_parameter")) {
                    const auto& cp = cg["channel_parameter"];
                    if (cp.contains("name")) {
                        channel_param_name = cp["name"].get<std::string>();
                        channel_param_def = &cp;
                    }
                }
            }
        }

        // Build params_defs map (by name or io reference)
        std::map<std::string, const ordered_json*> params_defs;
        const auto& parameters =
            cmd_def.contains("parameters") ? cmd_def["parameters"]
                                           : ordered_json::array();
        for (const auto& p : parameters) {
            if (p.contains("name"))
                params_defs[p["name"].get<std::string>()] = &p;
            if (p.contains("io"))
                params_defs[p["io"].get<std::string>()] = &p;
        }

        // Detect channel usage
        bool uses_channel = false;
        std::string channel_placeholder_found;
        if (!channel_group_name.empty()) {
            for (const auto& ph : placeholders) {
                if (ph == channel_group_name) {
                    uses_channel = true;
                    channel_placeholder_found = ph;
                    break;
                }
            }
            if (!uses_channel &&
                params_defs.count(channel_group_name)) {
                uses_channel = true;
                channel_placeholder_found = channel_group_name;
            }
        }

        // Build function parameter list in order: id, [channel], others
        std::vector<std::string> func_params = {"id"};
        if (uses_channel)
            func_params.push_back(
                channel_param_name.empty() ? "channel" : channel_param_name);

        std::map<std::string, bool> seen;
        seen["id"] = true;
        if (uses_channel) seen[channel_param_name] = true;

        for (const auto& ph : placeholders) {
            std::string pname = ph;
            if (uses_channel && ph == channel_placeholder_found)
                pname = channel_param_name;
            if (!seen.count(pname)) {
                func_params.push_back(pname);
                seen[pname] = true;
            }
        }
        for (const auto& p : parameters) {
            std::string pname;
            if (p.contains("name"))
                pname = p["name"].get<std::string>();
            else if (p.contains("io"))
                pname = p["io"].get<std::string>();
            if (!pname.empty() && !seen.count(pname)) {
                func_params.push_back(pname);
                seen[pname] = true;
            }
        }

        // Documentation: description
        if (cmd_def.contains("description") &&
            !cmd_def["description"].get<std::string>().empty()) {
            lines.push_back("--- " + cmd_def["description"].get<std::string>());
        } else {
            lines.push_back("--- " + cmd_key);
        }

        // Build signature parts and doc param lines
        std::vector<std::string> sig_parts;
        std::vector<std::string> doc_lines;

        doc_lines.push_back(
            "--- @param id string Instrument instance identifier");
        sig_parts.push_back("id: string");

        if (uses_channel) {
            std::string chdesc;
            if (channel_param_def &&
                channel_param_def->contains("description")) {
                chdesc = " " +
                         (*channel_param_def)["description"].get<std::string>();
            }
            doc_lines.push_back("--- @param " + channel_param_name +
                                 " number" + chdesc);
            sig_parts.push_back(channel_param_name + ": number");
        }

        // Other parameters
        for (const auto& p : func_params) {
            if (p == "id") continue;
            if (uses_channel && p == channel_param_name) continue;

            const ordered_json* pdef_ptr =
                params_defs.count(p) ? params_defs.at(p) : nullptr;
            std::string ptype;
            std::string pdesc;

            if (pdef_ptr) {
                const auto& pdef = *pdef_ptr;
                if (pdef.contains("type"))
                    ptype = teal_type_from_schema(
                        pdef["type"].get<std::string>());
                if (pdef.contains("io")) {
                    const auto* io_e =
                        find_io_by_name(io_list,
                                        pdef["io"].get<std::string>());
                    if (io_e && io_e->contains("type"))
                        ptype = teal_type_from_schema(
                            (*io_e)["type"].get<std::string>());
                }
                if (pdef.contains("description"))
                    pdesc = pdef["description"].get<std::string>();
            } else {
                // May be a channel io suffix
                if (!channel_group_name.empty()) {
                    auto it = channel_groups_by_name.find(channel_group_name);
                    if (it != channel_groups_by_name.end() &&
                        it->second->contains("io_types")) {
                        for (const auto& iot :
                             (*it->second)["io_types"]) {
                            if (iot.contains("suffix") &&
                                iot["suffix"].get<std::string>() == p) {
                                if (iot.contains("type"))
                                    ptype = teal_type_from_schema(
                                        iot["type"].get<std::string>());
                                if (iot.contains("description"))
                                    pdesc =
                                        iot["description"].get<std::string>();
                                break;
                            }
                        }
                    }
                }
            }

            if (ptype.empty()) ptype = "any";

            if (!pdesc.empty())
                doc_lines.push_back("--- @param " + p + " " + ptype + " " +
                                     pdesc);
            else
                doc_lines.push_back("--- @param " + p + " " + ptype);

            sig_parts.push_back(p + ": " + ptype);
        }

        // Outputs / return type
        std::vector<std::string> outputs;
        if (cmd_def.contains("outputs")) {
            for (const auto& o : cmd_def["outputs"])
                outputs.push_back(o.get<std::string>());
        }

        std::vector<OutputType> output_types = find_output_types(
            outputs, channel_group_name, io_list, channel_groups_by_name);

        std::string return_sig;
        if (output_types.empty()) {
            return_sig = "any";
            doc_lines.push_back("--- @return any");
        } else if (output_types.size() == 1) {
            const auto& ot = output_types[0];
            std::string otype =
                ot.type.empty() ? "any" : teal_type_from_schema(ot.type);
            return_sig = otype;
            if (!ot.name.empty())
                doc_lines.push_back("--- @return " + otype + " " + ot.name);
            else
                doc_lines.push_back("--- @return " + otype);
        } else {
            std::string rec;
            bool first = true;
            for (const auto& ot : output_types) {
                std::string otype =
                    ot.type.empty() ? "any" : teal_type_from_schema(ot.type);
                if (!first) rec += ", ";
                rec += ot.name + ": " + otype;
                first = false;
            }
            return_sig = "{" + rec + "}";
            doc_lines.push_back("--- @return table " + return_sig);
        }

        // Emit doc lines
        for (const auto& dl : doc_lines) lines.push_back(dl);

        // Function signature line
        std::string sig;
        for (std::size_t i = 0; i < sig_parts.size(); ++i) {
            if (i) sig += ", ";
            sig += sig_parts[i];
        }
        lines.push_back("function " + module_name + ":" + func_name + "(" +
                         sig + "): " + return_sig);

        // Channel parameter clamp/precision
        if (uses_channel && channel_param_def) {
            const auto& cpd = *channel_param_def;
            std::string cptype =
                cpd.contains("type") ? cpd["type"].get<std::string>() : "";
            double cmin_v = 0, cmax_v = 0, cprec_v = 0;
            const double* cmin_p = nullptr;
            const double* cmax_p = nullptr;
            const double* cprec_p = nullptr;
            if (cpd.contains("min")) {
                cmin_v = cpd["min"].get<double>();
                cmin_p = &cmin_v;
            }
            if (cpd.contains("max")) {
                cmax_v = cpd["max"].get<double>();
                cmax_p = &cmax_v;
            }
            if (cpd.contains("precision")) {
                cprec_v = cpd["precision"].get<double>();
                cprec_p = &cprec_v;
            }
            emit_clamp_and_precision(lines, channel_param_name, cptype,
                                      cmin_p, cmax_p, cprec_p, "  ");
        }

        // Other parameter clamp/precision
        for (const auto& p : parameters) {
            std::string pname;
            if (p.contains("name"))
                pname = p["name"].get<std::string>();
            else if (p.contains("io"))
                pname = p["io"].get<std::string>();
            if (pname.empty()) continue;
            if (uses_channel && pname == channel_param_name) continue;

            std::string ptype =
                p.contains("type") ? p["type"].get<std::string>() : "";
            double pmin_v = 0, pmax_v = 0, pprec_v = 0;
            const double* pmin_p = nullptr;
            const double* pmax_p = nullptr;
            const double* pprec_p = nullptr;
            if (p.contains("min")) {
                pmin_v = p["min"].get<double>();
                pmin_p = &pmin_v;
            }
            if (p.contains("max")) {
                pmax_v = p["max"].get<double>();
                pmax_p = &pmax_v;
            }
            if (p.contains("precision")) {
                pprec_v = p["precision"].get<double>();
                pprec_p = &pprec_v;
            }

            // Only emit clamp/precision when type is numeric and bounds/precision
            // are present, matching the Lua condition exactly.
            bool has_bounds = pmin_p || pmax_p;
            bool numeric_type = (ptype == "int" || ptype == "float");
            if ((has_bounds && numeric_type) || pprec_p) {
                emit_clamp_and_precision(lines, pname, ptype, pmin_p, pmax_p,
                                          pprec_p, "  ");
            }
        }

        // Build context:call command id string
        std::string cmd_id_str;
        if (uses_channel) {
            cmd_id_str = "{id}:{" + channel_param_name + "}." + cmd_key;
        } else {
            cmd_id_str = "{id}." + cmd_key;
        }

        // Build call arguments (exclude id and channel)
        std::vector<std::string> call_args;
        for (const auto& fp : func_params) {
            if (fp == "id") continue;
            if (uses_channel && fp == channel_param_name) continue;
            call_args.push_back(fp);
        }

        std::string call_args_str;
        for (std::size_t i = 0; i < call_args.size(); ++i) {
            if (i) call_args_str += ", ";
            call_args_str += call_args[i];
        }

        if (!call_args_str.empty()) {
            lines.push_back("  return context:call(\"" + cmd_id_str + "\", " +
                             call_args_str + ")");
        } else {
            lines.push_back("  return context:call(\"" + cmd_id_str + "\")");
        }

        lines.push_back("end");
        lines.push_back("");
    }

    lines.push_back("return " + module_name);

    // Join with newlines — no trailing newline (matches Lua table.concat behaviour)
    std::string result;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) result += '\n';
        result += lines[i];
    }
    return result;
}

std::string generate_from_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("Failed to open file: " + path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    ordered_json instrument = ordered_json::parse(content);
    return build_module(instrument);
}
