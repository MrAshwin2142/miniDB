// main.cpp
// -----------------------------------------------------------------------------
// Entry point. Wires up the storage root, the engine, and the CLI.
//
// Usage:
//   minidb                          Start the interactive shell (data dir: ./data)
//   minidb script.sql               Run a script, then drop into the shell
//   minidb --data <dir> [script]    Use a custom data directory
//   minidb --run script.sql         Run a script and exit (no shell)
// -----------------------------------------------------------------------------
#include "minidb/cli.h"
#include "minidb/database.h"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string dataDir = "data";
    std::string script;
    bool runAndExit = false;

    std::vector<std::string> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--data" && i + 1 < args.size()) {
            dataDir = args[++i];
        } else if (a == "--run" && i + 1 < args.size()) {
            script = args[++i];
            runAndExit = true;
        } else if (a == "-h" || a == "--help") {
            std::cout << "Usage: minidb [--data <dir>] [--run <script.sql> | <script.sql>]\n";
            return 0;
        } else if (a.size() > 0 && a[0] != '-') {
            script = a; // positional script file
        }
    }

    try {
        minidb::Engine engine(dataDir);
        minidb::Cli cli(engine);

        if (!script.empty()) {
            cli.runScriptFile(script, /*echo=*/true);
            if (runAndExit) return 0;
        }
        return cli.runInteractive();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
}
