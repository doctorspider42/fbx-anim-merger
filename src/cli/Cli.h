// Command-line front end over the same fam_core pipeline the GUI drives.
//
// Everything the interface can do to a scene -- import, merge, re-apply the track
// policy, rename/drop/select clips, export -- is reachable here; only the parts
// that are inherently interactive (viewport, playback, camera) are absent.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "core/AnimMerge.h"
#include "core/Export.h"
#include "core/FbxImport.h"
#include "util/Version.h"

namespace fam::cli {

// One version number for the whole project; see cmake/Version.cmake.
inline constexpr const char* kVersion = kAppVersion;

enum class Command {
    None,
    Info,     // inspect one file
    Check,    // rig compatibility, writes nothing
    Merge,    // the full pipeline
    Convert,  // merge with no animation sources: re-export one file
    Help,
    Version,
};

struct Rename {
    std::string from;  // clip name, wildcard pattern, or index
    std::string to;
};

struct Options {
    Command command = Command::None;

    std::string base;                // base model / the single input for info+convert
    std::vector<std::string> anims;  // animation files or directories
    std::string output;

    ImportOptions import;
    MergeOptions merge;
    ExportOptions exportOptions;
    bool formatExplicit = false;  // otherwise inferred from the output extension
    bool scaleExplicit = false;   // otherwise DefaultScaleFor(format)
    bool embedExplicit = false;   // otherwise on for GLB, off elsewhere

    // Clip bookkeeping, the CLI's stand-in for the animation panel.
    std::vector<Rename> renames;
    bool nameFromFile = false;
    std::vector<std::string> drop;
    std::vector<std::string> only;
    std::vector<std::string> exclude;
    bool applyPolicy = false;

    bool recursive = false;   // descend into directories passed as animation sources
    bool keepGoing = false;   // a failed source file does not abort the run
    bool dryRun = false;      // do everything except write the output
    float minMatch = 0.0f;    // fail below this rig match, in percent

    // info detail
    bool showNodes = false;
    bool showBones = false;
    bool showTracks = false;

    bool json = false;
    bool quiet = false;
    std::string logFile;

    std::string helpTopic;  // `--help merge`
};

struct ParseResult {
    bool ok = true;
    int exitCode = 0;
    std::string error;
};

ParseResult Parse(int argc, char** argv, Options& out);

void PrintUsage(std::FILE* stream, const std::string& topic);

// Case-insensitive glob over `*` and `?`. Used for --only/--exclude/--drop/--rename.
bool WildcardMatch(const std::string& pattern, const std::string& text);

int RunInfo(const Options& options);
int RunCheck(const Options& options);
int RunMerge(const Options& options);

}  // namespace fam::cli
