#include "PhotoRecProcessRunner.h"

#include <array>
#include <cstdio>

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

namespace Recovery {
namespace Carving {

    PhotoRecProcessResult PhotoRecProcessRunner::RunCommand(
        const std::string& command,
        const std::vector<std::string>& envVars) {
        PhotoRecProcessResult result;
        std::string fullCommand;

        for (const auto& env : envVars) {
#ifdef _WIN32
            fullCommand += "set \"" + env + "\" && ";
#else
            fullCommand += "export \"" + env + "\" && ";
#endif
        }
#ifdef _WIN32
        fullCommand += "cmd.exe /d /c \"" + command + "\" 2>&1";
#else
        fullCommand += command + " 2>&1";
#endif

        FILE* pipe = POPEN(fullCommand.c_str(), "r");
        if (pipe == nullptr) {
            result.stdErr = "Failed to launch PhotoRec command: " + command;
            return result;
        }

        std::array<char, 512> buffer{};
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            result.stdOut += buffer.data();
        }

        result.exitCode = PCLOSE(pipe);
        result.executed = true;
        return result;
    }

} // namespace Carving
} // namespace Recovery
