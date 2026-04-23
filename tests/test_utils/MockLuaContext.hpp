#ifndef MOCK_LUA_CONTEXT_HPP
#define MOCK_LUA_CONTEXT_HPP
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sol/sol.hpp>
#include <string>
#include <vector>

class MockLuaContext {
public:
  struct LogEntry {
    std::string level; // "log", "error"
    std::string message;
  };
  struct CallEntry {
    std::string command_id;
    std::map<std::string, sol::object> args;
    sol::object return_value;
  };
  std::vector<LogEntry> logs;
  std::vector<CallEntry> calls;

  static void
  log_to_file(const std::string &func, const std::string &id,
              const std::map<std::string, std::string> &str_args = {},
              const std::map<std::string, sol::object> &obj_args = {}) {
    std::ofstream out("mock_lua_context_calls.txt", std::ios::app);
    out << "Function: " << func << "\n";
    if (!id.empty()) {
      out << "Command: " << id << "\n";
    }
    if (!str_args.empty()) {
      out << "Args:";
      for (const auto &[key, value] : str_args) {
        out << " [" << key << "]=" << value;
      }
      out << "\n";
    } else if (!obj_args.empty()) {
      out << "Args:";
      for (const auto &[key, value] : obj_args) {
        out << " [" << key << "]=";
        if (value.is<std::string>()) {
          out << value.as<std::string>();
        } else if (value.is<double>()) {
          out << value.as<double>();
        } else if (value.is<int>()) {
          out << value.as<int>();
        } else if (value.is<bool>()) {
          out << (value.as<bool>() ? "true" : "false");
        } else {
          out << "<unknown>";
        }
      }
      out << "\n";
    }
    out.close();
  }
  sol::object lua_callk(sol::this_state s, const std::string &command_id,
                        sol::variadic_args args) {
    sol::state_view lua(s);
    std::map<std::string, sol::object> arg_map;
    for (int i = 0; i < args.size(); ++i) {
      arg_map[std::to_string(i)] = args[i];
    }
    log_to_file("call", command_id, {}, arg_map);
    sol::object return_val = lua.create_table();
    calls.push_back({command_id, arg_map, return_val});
    return return_val;
  }

  // Register with sol2
  static void register_with_sol(sol::state &lua) {
    lua.new_usertype<MockLuaContext>(
        "MockContext", "log", &MockLuaContext::lua_log, "error",
        &MockLuaContext::lua_error, "call", &MockLuaContext::lua_callk);
  }

  void lua_log(const std::string &message) {
    logs.push_back({"log", message});
    log_to_file("log", "", {{"message", message}});
    std::cout << "[LOG] " << message << '\n';
  }
  void lua_error(const std::string &message) {
    logs.push_back({"error", message});
    log_to_file("error", "", {{"message", message}});
    std::cout << "[ERROR] " << message << '\n';
  }

  [[nodiscard]] bool has_log_matching(const std::regex &pattern) const {
    return std::any_of(logs.begin(), logs.end(), [&](const auto &entry) {
      return std::regex_search(entry.message, pattern);
    });
  }
  [[nodiscard]] bool has_clamped_log(const std::string &param_name) const {
    std::regex clamp_pattern("Clamped " + param_name);
    return has_log_matching(clamp_pattern);
  }
  [[nodiscard]] bool has_precision_log(const std::string &param_name) const {
    std::regex prec_pattern("Applied.*precision to " + param_name + "|" +
                            "Rounded " + param_name);
    return has_log_matching(prec_pattern);
  }
  [[nodiscard]] bool has_call_to(const std::regex &pattern) const {
    return std::any_of(calls.begin(), calls.end(), [&](const auto &call) {
      return std::regex_search(call.command_id, pattern);
    });
  }
  void clear() {
    logs.clear();
    calls.clear();
  }
  void print_logs() const {
    std::cout << "\n=== Captured Logs ===" << '\n';
    for (const auto &entry : logs) {
      std::cout << "[" << entry.level << "] " << entry.message << '\n';
    }
  }
  void print_calls() const {
    std::cout << "\n=== Captured Calls ===" << '\n';
    for (const auto &call : calls) {
      std::cout << "Command: " << call.command_id << '\n';
    }
  }
};
#endif
