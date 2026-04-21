#pragma once

#include <string>
#include <nlohmann/json.hpp>

using ordered_json = nlohmann::ordered_json;

// Replicates Lua 5.4 tostring() for numbers decoded by cjson as floats.
// cjson decodes all JSON numbers as Lua floats, so JSON integer 1 becomes
// the float 1.0, and tostring(1.0) in Lua 5.4 yields "1.0".
std::string lua_number_tostring(double v);

// Returns true if v is exactly representable as an integer (floor(v) == v).
bool is_integer_value(double v);

// Strips non-alphanumeric characters to form a valid Teal identifier.
std::string sanitize_module_name(const std::string& s);

// Converts a command key such as "SET_SAMPLE_RATE" to camelCase "setSampleRate".
// Keys without underscores (e.g. "SETSAMPLERATE") become all-lowercase.
std::string camel_from_cmd(const std::string& cmd);

// Maps an instrument schema type string to the corresponding Teal type.
// "int"/"float" -> "number", "string" -> "string", "bool" -> "boolean", else "any".
std::string teal_type_from_schema(const std::string& t);

// Generates the complete Teal module text from a parsed instrument JSON object.
std::string build_module(const ordered_json& instrument);

// Reads an instrument JSON file from disk and returns the generated Teal module text.
std::string generate_from_file(const std::string& path);
