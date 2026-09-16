#include "teal-api-gen/convert.hpp"
#include "test_utils/MockLuaContext.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <regex>
#include <sol/sol.hpp>
#include <sstream>
#include <string>
#include <yaml-cpp/yaml.h>

using namespace teal_api_gen;

class TealApiGenExecutionTest : public ::testing::Test {
protected:
  std::string instrument_yaml;
  std::unique_ptr<MockLuaContext> mock_context;
  std::unique_ptr<sol::state> lua;
  std::string temp_dir;

  void SetUp() override {
    // Create temporary directory for test files
    temp_dir = "/tmp/teal_api_gen_tests";
    std::filesystem::create_directories(temp_dir);

    // Find the directory containing this source file
    std::filesystem::path test_file_path = __FILE__;
    std::filesystem::path yaml_path =
        test_file_path.parent_path() / "example_instrument.yml";

    // Load the YAML file content
    std::ifstream yaml_in(yaml_path);
    if (!yaml_in) {
      throw std::runtime_error("Could not open YAML file: " +
                               yaml_path.string());
    }
    // Load YAML file content as text (avoid long-lived YAML::Node lifetime
    // issues)
    std::stringstream yaml_buffer;
    yaml_buffer << yaml_in.rdbuf();
    instrument_yaml = yaml_buffer.str();
    if (instrument_yaml.empty()) {
      throw std::runtime_error("YAML file is empty: " + yaml_path.string());
    }

    // Initialize Lua state
    lua = std::make_unique<sol::state>();
    lua->open_libraries(sol::lib::base, sol::lib::math);

    // Create mock context
    mock_context = std::make_unique<MockLuaContext>();
    MockLuaContext::register_with_sol(*lua);
    (*lua)["context"] = mock_context.get();
    sol::table instrument_call_stack = lua->create_table();
    instrument_call_stack.set_function("new", [](sol::table spec) {
      return spec.get_or("instrument", std::string{}) + "." +
             spec.get_or("command", std::string{});
    });
    (*lua)["instrument_call_stack"] = instrument_call_stack;
  }

  void TearDown() override {
    if (lua) {
      (*lua)["context"] = sol::nil;
      lua->collect_garbage();
    }
    mock_context.reset();
    lua.reset();
    std::filesystem::remove_all(temp_dir);
  }

  std::string stripTealPrelude(const std::string &generated_lua) {
    static const std::string compat_prefix = "local _tl_compat;";
    if (generated_lua.rfind(compat_prefix, 0) == 0) {
      const std::size_t first_newline = generated_lua.find('\n');
      if (first_newline != std::string::npos) {
        return generated_lua.substr(first_newline + 1);
      }
    }
    return generated_lua;
  }

  // Step 1: Generate Teal code from YAML
  std::string generateTealCode() {
    YAML::Node instrument = YAML::Load(instrument_yaml);
    std::stringstream teal_stream;
    convert_yml(instrument, teal_stream);
    return teal_stream.str();
  }

  // Step 2: Save Teal code to file
  std::string saveTealCode(const std::string &teal_code,
                           const std::string &base_filename = "instrument") {
    std::string teal_path = temp_dir + "/" + base_filename + ".tl";
    std::ofstream teal_file(teal_path);
    teal_file << teal_code;
    teal_file.close();
    return teal_path;
  }

  // Step 3: Compile Teal to Lua using the Teal compiler
  std::string
  compileTealToLua(const std::string &teal_path,
                   const std::string &base_filename = "instrument") {
    std::string lua_path = temp_dir + "/" + base_filename + ".lua";
    std::string cmd = "tl gen " + teal_path + " -o " + lua_path;
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
      throw std::runtime_error("Teal compilation failed: " + cmd);
    }
    return lua_path;
  }

  // Step 4: Load compiled Lua code from file
  std::string loadLuaCode(const std::string &lua_path) {
    std::ifstream lua_file(lua_path);
    if (!lua_file) {
      throw std::runtime_error("Could not open compiled Lua file: " + lua_path);
    }
    std::stringstream lua_stream;
    lua_stream << lua_file.rdbuf();
    return lua_stream.str();
  }

  // Build executable Lua by wrapping the generated chunk, keeping locals
  // visible to the appended snippet.
  std::string buildExecutableLua(const std::string &generated_lua,
                                 const std::string &snippet) {
    const std::string body = stripTealPrelude(generated_lua);

    static const std::regex return_module_re(
        R"(\nreturn\s+([A-Za-z_][A-Za-z0-9_]*)\s*$)");
    std::smatch match;
    if (!std::regex_search(body, match, return_module_re)) {
      throw std::runtime_error(
          "Generated Lua does not end with `return <module>`");
    }

    const std::string module_name = match[1].str();
    const std::size_t return_pos = static_cast<std::size_t>(match.position());

    std::string script = "local inst = (function()\n";
    script += body.substr(0, return_pos);
    script += "\nlocal inst = " + module_name + "\n";
    script += snippet;
    script += "\nreturn inst\nend)()";
    return script;
  }

  bool executeGeneratedLuaWithSnippet(const std::string &generated_lua,
                                      const std::string &snippet) {
    return executeLuaCode(buildExecutableLua(generated_lua, snippet));
  }

  // Step 5: Execute Lua code in the Lua state
  bool executeLuaCode(const std::string &code) {
    try {
      auto result = lua->safe_script(code);
      if (!result.valid()) {
        sol::error err = result;
        std::cerr << "Lua execution error: " << err.what() << "\n";
        return false;
      }
      return true;
    } catch (const std::exception &e) {
      std::cerr << "Exception during Lua execution: " << e.what() << "\n";
      return false;
    }
  }

  // Convenience method: Generate, compile, and load Lua code
  std::string
  generateCompileAndLoadLua(const std::string &base_filename = "instrument") {
    std::string teal_code = generateTealCode();
    std::string teal_path = saveTealCode(teal_code, base_filename);
    std::string lua_path = compileTealToLua(teal_path, base_filename);
    return loadLuaCode(lua_path);
  }
};

// ============================================================================
// Test: Generate and execute basic function call
// ============================================================================
TEST_F(TealApiGenExecutionTest, GenerateAndExecuteBasicCall) {
  const std::string generated_lua = generateCompileAndLoadLua();
  const std::string snippet = R"(
    -- Call setSampleRate with valid parameters
    inst:setSampleRate("inst1", 1, 1000.0)
  )";
  mock_context->clear();
  EXPECT_TRUE(executeGeneratedLuaWithSnippet(generated_lua, snippet))
      << "Failed to execute Lua script";
  // Verify the command was called
  EXPECT_GT(mock_context->calls.size(), 0) << "No context:call made";
  EXPECT_TRUE(mock_context->has_call_to(std::regex("SET_SAMPLE_RATE")))
      << "SET_SAMPLE_RATE command not found in calls";
  mock_context->print_calls();
}

// ============================================================================
// Test: Command without channel group
// ============================================================================
TEST_F(TealApiGenExecutionTest, CommandWithoutChannelGroup) {
  const std::string generated_lua = generateCompileAndLoadLua();
  const std::string snippet = R"(
    -- SET_MODE doesn't use channel_group
    inst:setMode("inst1", "SingleShot")
  )";
  mock_context->clear();
  EXPECT_TRUE(executeGeneratedLuaWithSnippet(generated_lua, snippet))
      << "Failed to execute Lua script";
  // Verify the command was called without channel in command ID
  EXPECT_GT(mock_context->calls.size(), 0) << "No context:call made";
  // Command ID should be {id}.SET_MODE, not {id}:{channel}.SET_MODE
  bool found_correct_command = false;
  for (const auto &call : mock_context->calls) {
    if (call.command_id.find("inst1.SET_MODE") != std::string::npos) {
      found_correct_command = true;
      break;
    }
  }
  EXPECT_TRUE(found_correct_command)
      << "Command ID format incorrect for non-channel command";
  mock_context->print_calls();
}

// ============================================================================
// Test: Boundary condition - minimum valid value
// ============================================================================
TEST_F(TealApiGenExecutionTest, BoundaryMinimumValidValue) {
  const std::string generated_lua = generateCompileAndLoadLua();
  const std::string snippet = R"(
    -- Set sample_rate to exactly min (1.0)
    inst:setSampleRate("inst1", 1, 1.0)
  )";
  mock_context->clear();
  EXPECT_TRUE(executeGeneratedLuaWithSnippet(generated_lua, snippet))
      << "Failed to execute Lua script";
  // Should NOT have clamping log for minimum valid value
  EXPECT_FALSE(mock_context->has_clamped_log("sample_rate"))
      << "Should not clamp at minimum boundary";
  mock_context->print_logs();
}

// ============================================================================
// Test: Boundary condition - maximum valid value
// ============================================================================
TEST_F(TealApiGenExecutionTest, BoundaryMaximumValidValue) {
  const std::string generated_lua = generateCompileAndLoadLua();
  const std::string snippet = R"(
    -- Set sample_rate to exactly max (125000000)
    inst:setSampleRate("inst1", 1, 125000000.0)
  )";
  mock_context->clear();
  EXPECT_TRUE(executeGeneratedLuaWithSnippet(generated_lua, snippet))
      << "Failed to execute Lua script";
  // Should NOT have clamping log for maximum valid value
  EXPECT_FALSE(mock_context->has_clamped_log("sample_rate"))
      << "Should not clamp at maximum boundary";
  mock_context->print_logs();
}

// ============================================================================
// Test: Verify context:call receives correct parameters
// ============================================================================
TEST_F(TealApiGenExecutionTest, ContextCallParameters) {
  const std::string generated_lua = generateCompileAndLoadLua();
  const std::string snippet = R"(
    -- Call with specific parameters
    local result = inst:setSampleRate("myinst", 2, 5000.0)
  )";
  mock_context->clear();
  EXPECT_TRUE(executeGeneratedLuaWithSnippet(generated_lua, snippet))
      << "Failed to execute Lua script";
  // Verify we have at least one call
  ASSERT_GE(mock_context->calls.size(), 1)
      << "Expected at least one context:call";
  // Check the command ID format. Channel is passed as an argument, not encoded
  // in the command target.
  const auto &call = mock_context->calls[0];
  EXPECT_NE(call.command_id.find("myinst.SET_SAMPLE_RATE"), std::string::npos)
      << "Command ID should be in format {id}.COMMAND";

  ASSERT_EQ(call.args.size(), 1U)
      << "Channel metadata must not be duplicated in command parameters";
  const auto args_it = call.args.find("0");
  ASSERT_NE(args_it, call.args.end()) << "Expected sample_rate argument";
  ASSERT_TRUE(args_it->second.is<double>());
  EXPECT_EQ(args_it->second.as<double>(), 5000.0);
  mock_context->print_calls();
}

// ============================================================================
// Test: Verify generated code has no syntax errors
// ============================================================================
TEST_F(TealApiGenExecutionTest, GeneratedCodeSyntaxValid) {
  std::string lua_code = stripTealPrelude(generateCompileAndLoadLua());
  // Simply try to load the code - if there's a syntax error, it will fail
  try {
    auto result = lua->safe_script(lua_code);
    EXPECT_TRUE(result.valid()) << "Generated code has syntax errors";
  } catch (const std::exception &e) {
    FAIL() << "Generated code failed to load: " << e.what();
  }
}
