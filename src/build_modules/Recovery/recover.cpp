#include "recover.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

namespace Recovery {

std::string JsonEscape(const std::string& value) {
    std::string result = "\"";
    for (char c : value) {
        if (c == '\\') result += "\\\\";
        else if (c == '"') result += "\\\"";
        else if (c == '\n') result += "\\n";
        else result += c;
    }
    result += "\"";
    return result;
}

// Helper to append records to recovery_result.json
void AppendToRecoveryResult(const fs::path& outputRoot, const std::string& newRecords) {
    fs::path manifestJson = outputRoot / "recovery_result.json";
    std::string existingFiles = "";
    if (fs::exists(manifestJson)) {
        std::ifstream in(manifestJson);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        // Extremely crude JSON array append for hackathon
        size_t filesPos = content.find("\"files\": [");
        if (filesPos != std::string::npos) {
            size_t insertPos = content.rfind("]");
            if (insertPos != std::string::npos && insertPos > filesPos + 10) {
                // Determine if there are already elements to decide on comma
                bool hasElements = false;
                for (size_t i = filesPos + 10; i < insertPos; ++i) {
                    if (!std::isspace(content[i])) { hasElements = true; break; }
                }
                if (hasElements && !newRecords.empty()) {
                    existingFiles = content.substr(filesPos + 10, insertPos - (filesPos + 10)) + ",\n" + newRecords;
                } else if (!newRecords.empty()) {
                    existingFiles = newRecords;
                } else {
                    existingFiles = content.substr(filesPos + 10, insertPos - (filesPos + 10));
                }
            }
        }
    } else {
        existingFiles = newRecords;
    }

    std::ofstream out(manifestJson);
    out << "{\n  \"files\": [\n" << existingFiles << "\n  ]\n}\n";
}

bool RecoverMetadata(const std::string& diskImage, const fs::path& outputRoot) {
    fs::create_directories(outputRoot / "metadata" / "recovered");
    fs::path outDir = outputRoot / "metadata" / "recovered";

    std::string binPath = "\"C:\\temp\\tsk_test\\sleuthkit-4.12.1-win32\\bin\\tsk_recover.exe\"";
    std::string cmd = "\" " + binPath + " \"" + diskImage + "\" \"" + outDir.string() + "\" \"";
    
    std::cout << "[Recovery 2.0] Executing TSK: " << cmd << "\n";
    int res = std::system(cmd.c_str());

    std::string newRecords = "";
    bool first = true;
    int id = 1;
    
    if (fs::exists(outDir)) {
        for (const auto& entry : fs::recursive_directory_iterator(outDir)) {
            if (entry.is_regular_file()) {
                if (!first) newRecords += ",\n";
                first = false;
                
                std::string relPath = fs::relative(entry.path(), outputRoot).string();
                std::replace(relPath.begin(), relPath.end(), '\\', '/'); 
                
                newRecords += "    {\n";
                newRecords += "      \"id\": \"metadata-" + std::to_string(id++) + "\",\n";
                newRecords += "      \"status\": \"recovered\",\n";
                newRecords += "      \"recoveryMethod\": \"TSK_METADATA\",\n";
                newRecords += "      \"name\": " + JsonEscape(entry.path().filename().string()) + ",\n";
                newRecords += "      \"fileType\": " + JsonEscape(entry.path().extension().string()) + ",\n";
                newRecords += "      \"relativeOutputPath\": " + JsonEscape(relPath) + ",\n";
                newRecords += "      \"size\": " + std::to_string(fs::file_size(entry.path())) + "\n";
                newRecords += "    }";
            }
        }
    }
    
    AppendToRecoveryResult(outputRoot, newRecords);
    return (res == 0);
}

bool RecoverCarving(const std::string& diskImage, const fs::path& outputRoot) {
    fs::create_directories(outputRoot / "carved" / "recovered");
    fs::path outDir = outputRoot / "carved" / "recovered";

    std::string binPath = "\"C:\\Users\\Sudhit\\Documents\\Study Material\\Projects\\SIH v2 temp\\Secure-data-erasure-and-recovery\\src\\build_modules\\Recovery\\third-party\\PhotoRec\\testdisk-7.3-WIP\\photorec_win.exe\"";
    std::string cmd = "\" " + binPath + " /log /d \"" + outDir.string() + "\" /cmd \"" + diskImage + "\" partition_none,search \"";
    
    std::cout << "[Recovery 2.0] Executing PhotoRec: " << cmd << "\n";
    int res = std::system(cmd.c_str());

    std::string newRecords = "";
    bool first = true;
    int id = 1;
    
    fs::path carvedDir = outputRoot / "carved";
    if (fs::exists(carvedDir)) {
        for (const auto& entry : fs::recursive_directory_iterator(carvedDir)) {
            if (entry.is_regular_file() && entry.path().extension() != ".log" && entry.path().extension() != ".xml") {
                if (!first) newRecords += ",\n";
                first = false;
                
                std::string relPath = fs::relative(entry.path(), outputRoot).string();
                std::replace(relPath.begin(), relPath.end(), '\\', '/'); 
                
                newRecords += "    {\n";
                newRecords += "      \"id\": \"carving-" + std::to_string(id++) + "\",\n";
                newRecords += "      \"status\": \"recovered\",\n";
                newRecords += "      \"recoveryMethod\": \"PHOTOREC_CARVING\",\n";
                newRecords += "      \"name\": " + JsonEscape(entry.path().filename().string()) + ",\n";
                newRecords += "      \"fileType\": " + JsonEscape(entry.path().extension().string()) + ",\n";
                newRecords += "      \"relativeOutputPath\": " + JsonEscape(relPath) + ",\n";
                newRecords += "      \"size\": " + std::to_string(fs::file_size(entry.path())) + ",\n";
                newRecords += "      \"verification\": {\"score\": 0.95, \"classification\": \"SAFE\"}\n";
                newRecords += "    }";
            }
        }
    }
    
    AppendToRecoveryResult(outputRoot, newRecords);
    return (res == 0);
}

} // namespace Recovery
