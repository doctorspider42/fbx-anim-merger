#pragma once

#include <string>
#include <vector>

namespace fam {

struct FileFilter {
    const char* name;  // "FBX scene"
    const char* spec;  // "fbx" or "fbx,dae"
};

bool InitFileDialogs();
void ShutdownFileDialogs();

// All three return empty on cancel.
std::string OpenFileDialog(const std::vector<FileFilter>& filters, const std::string& defaultPath = {});
std::vector<std::string> OpenFilesDialog(const std::vector<FileFilter>& filters,
                                         const std::string& defaultPath = {});
std::string SaveFileDialog(const std::vector<FileFilter>& filters, const std::string& defaultName,
                           const std::string& defaultPath = {});

}  // namespace fam
