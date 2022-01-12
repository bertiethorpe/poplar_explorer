// Copyright (c) 2021 Graphcore Ltd. All rights reserved.

#include <cstdlib>
#include <vector>
#include <limits>
#include <chrono>
#include <memory>

#include <boost/program_options.hpp>
#include "io_utils.hpp"
#include "tool_registry.hpp"

#include "../include/cmake_discovered_tools.hpp"

/// Parse the tool name and return the tool name and a
/// factory function that will create the tool specified
/// in the command line.
std::pair<std::string, ToolFactoryFunction> parseToolName(int argc, char** argv) {
  namespace po = boost::program_options;

  // We only want to get the tool name here:
  po::options_description desc("Tool Selection Options");
  desc.add_options()
  ("list-tools", po::value<std::string>(),
   "Print a list of available tools and exit."
  )
  ("tool-name", po::value<std::string>(),
   "Choose the tool to be executed."
  )
  ("misc-positional", po::value<std::vector<std::string>>(),
   "Not a real option: mops up excess positional args."
  )
  ;

  // Allow arbitrary number of positional arguments otherwise
  // command line must use = to set all other arguments:
  po::positional_options_description p;
  p.add("tool-name", 1);
  p.add("misc-positional", -1);

  boost::program_options::variables_map args;
  auto parsed = po::command_line_parser(argc, argv).options(desc).positional(p).allow_unregistered().run();
  po::store(parsed, args);

  if (args.count("tool-name") == 0) {
    std::cout << "Usage: " << argv[0] << " tool-name [--help]\n\n";
    std::cerr << "Please choose a tool to run from the following:\n"
              << enumerateToolNames(globalTools) << "\n\n";
    throw std::runtime_error("No tool specified.");
  }

  auto toolName = args.at("tool-name").as<std::string>();

  if (globalTools.count(toolName) == 0) {
    std::cerr << "Unrecognised tool: '" << toolName << "'\n\n";
    std::cerr << "Please choose a tool to run from the following:\n"
              << enumerateToolNames(globalTools) << "\n\n";
    throw std::runtime_error("Unrecognised tool name.");
  }

  ipu_utils::logger()->info("Selected tool {}", toolName);
  return std::make_pair(toolName, globalTools.at(toolName));
}

/// Parse the general options and options for the selected tool in one go:
boost::program_options::variables_map
parseOptions(int argc, char** argv,
             boost::program_options::options_description& toolOptions) {
  namespace po = boost::program_options;
  po::options_description desc("General Options");

  desc.add_options()
  ("help", "Show help for the specified tool."
  )
  ("model",
   po::bool_switch()->default_value(false),
   "If set then use IPU model instead of hardware."
  )
  ("ipus",
   po::value<std::size_t>()->default_value(1),
   "Number of IPUs to use."
  )
  ("save-exe",
   po::value<std::string>()->default_value(""),
   "Save the Poplar graph executable after compilation using this name (prefix)."
  )
  ("load-exe",
   po::value<std::string>()->default_value(""),
   "Load a previously saved executable with this name (prefix) and skip graph and program construction. "
  )
  ("compile-only", po::bool_switch()->default_value(false),
   "If set and save-exe is also set then exit after compiling and saving the graph."
  )
  ("defer-attach", po::bool_switch()->default_value(false),
  "If false (default) then a device is reserved before compilation, otherwise the device is not acquired until the program is ready to run."
  )
  ;

  po::options_description all("All Options");
  all.add(desc).add(toolOptions);

  boost::program_options::variables_map args;
  auto parsed = po::command_line_parser(argc, argv).options(all).run();
  po::store(parsed, args);
  if (args.count("help")) {
    std::cout << all << "\n";
    std::exit(0);
  }

  po::notify(args);

  auto saveExe = !args.at("save-exe").as<std::string>().empty();
  auto loadExe = !args.at("load-exe").as<std::string>().empty();
  if (saveExe && loadExe) {
    throw std::logic_error("You can not set both save-exe and load-exe.");
  }

  return args;
}

ipu_utils::RuntimeConfig getRuntimeConfig(const boost::program_options::variables_map& args) {
  auto exeName = args.at("save-exe").as<std::string>();
  if (exeName.empty()) { exeName = args.at("load-exe").as<std::string>(); }

  return ipu_utils::RuntimeConfig{
    args.at("ipus").as<std::size_t>(),
    exeName,
    args.at("model").as<bool>(),
    !args.at("save-exe").as<std::string>().empty(),
    !args.at("load-exe").as<std::string>().empty(),
    args.at("compile-only").as<bool>(),
    args.at("compile-only").as<bool>() || args.at("defer-attach").as<bool>()
  };
}

int main(int argc, char** argv) {
  spdlog::set_level(spdlog::level::trace);
  spdlog::set_pattern("[%H:%M:%S.%f] [%L] [%t] %v");

  std::string toolName;
  ToolFactoryFunction factoryFunc;
  std::tie(toolName, factoryFunc) = parseToolName(argc, argv);
  std::unique_ptr<ToolInterface> tool = factoryFunc();

  boost::program_options::options_description desc(toolName + " Options");
  tool->addToolOptions(desc);
  auto allOpts = parseOptions(argc, argv, desc);
  tool->setRuntimeConfig(getRuntimeConfig(allOpts));
  tool->init(allOpts);

  return ipu_utils::GraphManager().run(tool->getGraphBuilder());
}
