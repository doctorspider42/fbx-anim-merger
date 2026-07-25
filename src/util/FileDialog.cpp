#include "util/FileDialog.h"

#include <nfd.h>

#include "util/Log.h"

namespace fam {
namespace {

std::vector<nfdu8filteritem_t> ToNfd(const std::vector<FileFilter>& filters) {
    std::vector<nfdu8filteritem_t> out;
    out.reserve(filters.size());
    for (const FileFilter& filter : filters) {
        nfdu8filteritem_t item;
        item.name = filter.name;
        item.spec = filter.spec;
        out.push_back(item);
    }
    return out;
}

const char* OrNull(const std::string& s) {
    return s.empty() ? nullptr : s.c_str();
}

}  // namespace

bool InitFileDialogs() {
    if (NFD_Init() != NFD_OKAY) {
        LogError("Native file dialogs unavailable: %s", NFD_GetError());
        return false;
    }
    return true;
}

void ShutdownFileDialogs() {
    NFD_Quit();
}

std::string OpenFileDialog(const std::vector<FileFilter>& filters, const std::string& defaultPath) {
    auto items = ToNfd(filters);
    nfdu8char_t* path = nullptr;
    const nfdresult_t result = NFD_OpenDialogU8(&path, items.data(),
                                                static_cast<nfdfiltersize_t>(items.size()),
                                                OrNull(defaultPath));
    if (result == NFD_ERROR) LogError("File dialog failed: %s", NFD_GetError());
    if (result != NFD_OKAY || !path) return {};

    std::string out(path);
    NFD_FreePathU8(path);
    return out;
}

std::vector<std::string> OpenFilesDialog(const std::vector<FileFilter>& filters,
                                         const std::string& defaultPath) {
    auto items = ToNfd(filters);
    const nfdpathset_t* set = nullptr;
    const nfdresult_t result = NFD_OpenDialogMultipleU8(&set, items.data(),
                                                        static_cast<nfdfiltersize_t>(items.size()),
                                                        OrNull(defaultPath));
    if (result == NFD_ERROR) LogError("File dialog failed: %s", NFD_GetError());
    if (result != NFD_OKAY || !set) return {};

    std::vector<std::string> paths;
    nfdpathsetsize_t count = 0;
    NFD_PathSet_GetCount(set, &count);
    paths.reserve(count);
    for (nfdpathsetsize_t i = 0; i < count; ++i) {
        nfdu8char_t* path = nullptr;
        if (NFD_PathSet_GetPathU8(set, i, &path) == NFD_OKAY && path) {
            paths.emplace_back(path);
            NFD_PathSet_FreePathU8(path);
        }
    }
    NFD_PathSet_Free(set);
    return paths;
}

std::string SaveFileDialog(const std::vector<FileFilter>& filters, const std::string& defaultName,
                           const std::string& defaultPath) {
    auto items = ToNfd(filters);
    nfdu8char_t* path = nullptr;
    const nfdresult_t result = NFD_SaveDialogU8(&path, items.data(),
                                                static_cast<nfdfiltersize_t>(items.size()),
                                                OrNull(defaultPath), OrNull(defaultName));
    if (result == NFD_ERROR) LogError("File dialog failed: %s", NFD_GetError());
    if (result != NFD_OKAY || !path) return {};

    std::string out(path);
    NFD_FreePathU8(path);
    return out;
}

}  // namespace fam
