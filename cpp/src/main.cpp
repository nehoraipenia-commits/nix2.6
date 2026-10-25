// ==============================================================================
// NIX NATIVE CLI & DRIVER (C++ Implementation)
// Commands:
//   nix run <file.nx>        -> Fast C++ native interpreter
//   nix build <file.nx> [-o out] [--emit-cpp] [-O2/-O3] -> Native C++ compilation
// ==============================================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <filesystem>

#include "nix_runtime.hpp"
#include "frontend.hpp"
#include "interpreter.hpp"
#include "compiler.hpp"

namespace fs = std::filesystem;

static void print_usage() {
    std::cout << "Usage:\n";
    std::cout << "  nix run <filename.nx> [args...]\n";
    std::cout << "  nix build <filename.nx> [-o output_binary] [-O2|-O3] [--emit-cpp]\n";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    std::string cmd = argv[1];
    std::string filepath = argv[2];

    if (!fs::exists(filepath)) {
        std::cerr << "Error: Source file '" << filepath << "' not found.\n";
        return 1;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Failed to read file '" << filepath << "'\n";
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source_code = buffer.str();
    file.close();

    fs::path p(filepath);
    std::string script_dir = fs::absolute(p).parent_path().string();

    if (cmd == "run") {
        // Forward extra arguments to Nix program
        for (int i = 2; i < argc; ++i) {
            nix::program_args().push_back(argv[i]);
        }

        try {
            nix::Lexer lexer(source_code);
            auto tokens = lexer.tokenize();

            nix::Parser parser(std::move(tokens));
            auto ast = parser.parse();

            nix::Interpreter interpreter;
            interpreter.script_dir = script_dir;
            interpreter.run(ast.get());
            nix::qt_gui::auto_exec_if_windows_open();
        } catch (const nix::TopLevelReturn&) {
            nix::qt_gui::auto_exec_if_windows_open();
            return 0;
        } catch (const nix::SyntaxError& se) {
            std::cout << "\n" NIX_RED NIX_BOLD "[Nix Compilation Failed]" NIX_RESET "\n";
            std::cerr << NIX_YELLOW << se.what() << NIX_RESET << "\n";
            return 1;
        } catch (const nix::RuntimeError& re) {
            nix::report_runtime_error(re);
            return 1;
        } catch (const std::exception& e) {
            nix::report_internal(e.what());
            return 1;
        }
        return 0;
    } 
    else if (cmd == "build") {
        std::string output_bin = "out";
        std::string opt_level = "-O2";
        bool emit_cpp = false;

        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-o" && i + 1 < argc) {
                output_bin = argv[++i];
            } else if (arg == "--emit-cpp") {
                emit_cpp = true;
            } else if (arg == "-O0" || arg == "-O1" || arg == "-O2" || arg == "-O3") {
                opt_level = arg;
            }
        }

        try {
            nix::Lexer lexer(source_code);
            auto tokens = lexer.tokenize();

            nix::Parser parser(std::move(tokens));
            auto ast = parser.parse();

            // Find nix_runtime.hpp path
            // Look relative to binary or in current workspace
            std::string runtime_header = "cpp/include/nix_runtime.hpp";
            if (!fs::exists(runtime_header)) {
                runtime_header = "nix_runtime.hpp";
            }

            nix::Compiler compiler;
            compiler.script_dir = script_dir;
            std::string cpp_source = compiler.generate_cpp(ast.get(), runtime_header, true);

            std::string temp_cpp = output_bin + ".cpp";
            std::ofstream out_cpp(temp_cpp);
            if (!out_cpp.is_open()) {
                std::cerr << "Error: Failed to write intermediate file '" << temp_cpp << "'\n";
                return 1;
            }
            out_cpp << cpp_source;
            out_cpp.close();

            std::cout << "[Nix Compiler] Compiling native binary with " << opt_level << "...\n";
            std::string qt_link_flags = "";
            if (fs::exists("lib/libnix_qt.a")) {
                qt_link_flags = " lib/libnix_qt.a -F/opt/homebrew/lib -framework QtWidgets -framework QtGui -framework QtCore -Wl,-rpath,/opt/homebrew/lib";
            } else if (fs::exists("lib/libnix_qt.dylib")) {
                qt_link_flags = " -Llib -lnix_qt -Wl,-rpath,lib -F/opt/homebrew/lib -framework QtWidgets -framework QtGui -framework QtCore -Wl,-rpath,/opt/homebrew/lib";
            }
            std::string compile_cmd = "c++ -std=c++17 " + opt_level + " \"" + temp_cpp + "\"" + qt_link_flags + " -o \"" + output_bin + "\"";
            int ret = std::system(compile_cmd.c_str());

            if (ret != 0) {
                std::cerr << "[Nix Compilation Error]: Native C++ compilation failed.\n";
                return 1;
            }

            if (!emit_cpp) {
                fs::remove(temp_cpp);
            }

            std::cout << "[Nix Compiler] Binary successfully produced: '" << output_bin << "'\n";
            return 0;

        } catch (const nix::SyntaxError& se) {
            std::cout << "\n" NIX_RED NIX_BOLD "[Nix Compilation Failed]" NIX_RESET "\n";
            std::cerr << NIX_YELLOW << se.what() << NIX_RESET << "\n";
            return 1;
        } catch (const std::exception& e) {
            std::cerr << "[Nix Compiler Error]: " << e.what() << "\n";
            return 1;
        }
    }

    print_usage();
    return 1;
}
