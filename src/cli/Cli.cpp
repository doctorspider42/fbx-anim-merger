// Argument parsing and help text for the command-line front end.
#include "cli/Cli.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace fam::cli {
namespace {

char Lower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

std::string ToLower(std::string text) {
    for (char& c : text) c = Lower(c);
    return text;
}

bool ParseFloatArg(const std::string& text, float& out) {
    try {
        size_t consumed = 0;
        const float value = std::stof(text, &consumed);
        if (consumed != text.size()) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

// Recursive wildcard match. Only `*` and `?` are special, which covers every use
// the clip flags have without dragging in <regex>.
bool MatchHere(const char* pattern, const char* text) {
    while (*pattern != '\0') {
        if (*pattern == '*') {
            ++pattern;
            if (*pattern == '\0') return true;
            for (const char* at = text; ; ++at) {
                if (MatchHere(pattern, at)) return true;
                if (*at == '\0') return false;
            }
        }
        if (*text == '\0') return false;
        if (*pattern != '?' && Lower(*pattern) != Lower(*text)) return false;
        ++pattern;
        ++text;
    }
    return *text == '\0';
}

struct CommandInfo {
    const char* name;
    Command command;
    const char* summary;
};

constexpr CommandInfo kCommands[] = {
    {"info", Command::Info, "print a file's scene statistics and clip list"},
    {"check", Command::Check, "report how well animation files match a base rig"},
    {"merge", Command::Merge, "import a base model, merge clips onto it, export"},
    {"convert", Command::Convert, "re-export one file in another format or scale"},
};

Command CommandFromName(const std::string& name) {
    for (const CommandInfo& info : kCommands) {
        if (name == info.name) return info.command;
    }
    return Command::None;
}

bool FormatFromName(const std::string& name, ExportFormat& out) {
    const std::string lowered = ToLower(name);
    if (lowered == "fbx" || lowered == "fbx-binary") {
        out = ExportFormat::FbxBinary;
    } else if (lowered == "fbx-ascii" || lowered == "fbxa") {
        out = ExportFormat::FbxAscii;
    } else if (lowered == "glb") {
        out = ExportFormat::Glb;
    } else if (lowered == "gltf") {
        out = ExportFormat::GltfSeparate;
    } else {
        return false;
    }
    return true;
}

bool TranslationFromName(const std::string& name, TranslationMode& out) {
    const std::string lowered = ToLower(name);
    if (lowered == "root" || lowered == "root-bone-only") {
        out = TranslationMode::RootBonesOnly;
    } else if (lowered == "animated" || lowered == "animated-only") {
        out = TranslationMode::AnimatedOnly;
    } else if (lowered == "all" || lowered == "copy-all") {
        out = TranslationMode::CopyAll;
    } else {
        return false;
    }
    return true;
}

}  // namespace

bool WildcardMatch(const std::string& pattern, const std::string& text) {
    return MatchHere(pattern.c_str(), text.c_str());
}

void PrintUsage(std::FILE* stream, const std::string& topic) {
    if (topic.empty() || topic == "usage") {
        std::fprintf(stream,
            "FBX Animation Merger %s - command-line interface\n"
            "\n"
            "Usage: fam-cli <command> [options]\n"
            "\n"
            "Commands:\n", kVersion);
        for (const CommandInfo& info : kCommands) {
            std::fprintf(stream, "  %-9s %s\n", info.name, info.summary);
        }
        std::fprintf(stream,
            "\n"
            "Global options:\n"
            "  --json               emit the report as JSON instead of text\n"
            "  -q, --quiet          suppress the progress log (errors still print)\n"
            "  --log FILE           mirror the log to FILE\n"
            "  -h, --help [CMD]     this text, or the detail for one command\n"
            "  --version            print the version and exit\n"
            "\n"
            "The report goes to stdout, the progress log to stderr, so either can be\n"
            "redirected on its own.\n"
            "\n"
            "Exit codes: 0 success, 1 failure, 2 bad usage.\n"
            "\n"
            "Run `fam-cli --help merge` for the full merge/export option list.\n");
        return;
    }

    const Command command = CommandFromName(topic);
    switch (command) {
        case Command::Info:
            std::fprintf(stream,
                "fam-cli info <file.fbx> [options]\n"
                "\n"
                "Imports a file and reports what is inside it: node/mesh/bone/material\n"
                "counts, triangles, textures (and how many are embedded) and every clip\n"
                "with its length, frame count and track count.\n"
                "\n"
                "Options:\n"
                "  --nodes              list the node hierarchy\n"
                "  --bones              list the skinning bones\n"
                "  --tracks             list the animated node names per clip\n"
                "  --bake-rate FPS      resample curves at FPS (default 30)\n"
                "  --json               emit the whole report as JSON\n");
            return;
        case Command::Check:
            std::fprintf(stream,
                "fam-cli check --base <model.fbx> --anim <clip.fbx> [--anim ...] [options]\n"
                "\n"
                "Reports, per animation file, the share of its animated nodes that resolve\n"
                "to a node in the base rig, plus the names that do not. Writes nothing, so\n"
                "it is the cheap way to find out whether a merge is worth attempting.\n"
                "\n"
                "Options:\n"
                "  --base FILE          base model (also the first positional argument)\n"
                "  --anim FILE|DIR      animation source; repeatable, directories are scanned\n"
                "  --recursive          descend into subdirectories of --anim directories\n"
                "  --min-match PERCENT  exit 1 if any source falls below this match\n"
                "  --[no-]strip-namespace   'mixamorig:Hips' -> 'Hips' (default on)\n"
                "  --[no-]ignore-case       case-insensitive node matching (default on)\n"
                "  --skeleton-tracks-only   ignore tracks on non-bone nodes\n"
                "  --bake-rate FPS      resample curves at FPS (default 30)\n"
                "  --json               emit the whole report as JSON\n");
            return;
        case Command::Merge:
        case Command::Convert:
            std::fprintf(stream,
                "fam-cli merge --base <model.fbx> [--anim <clip.fbx> ...] --out <file> [options]\n"
                "fam-cli convert --in <file.fbx> --out <file> [export options]\n"
                "\n"
                "merge imports the base model, merges every clip found in each animation\n"
                "source onto its skeleton and exports the result. convert is the same\n"
                "pipeline with no animation sources: a format/scale change of one file.\n"
                "\n"
                "Input:\n"
                "  --base, --in FILE    base model (also the first positional argument)\n"
                "  --anim FILE|DIR      animation source; repeatable, directories are scanned\n"
                "  --recursive          descend into subdirectories of --anim directories\n"
                "  -o, --out FILE       output path; its extension picks the format\n"
                "\n"
                "Import:\n"
                "  --bake-rate FPS      resample curves at FPS (default 30). Raise it for\n"
                "                       fast motion, lower it for smaller files\n"
                "  --epsilon-pos V      drop position tracks within V of the rest pose (1e-5)\n"
                "  --epsilon-rot V      same for rotation (1e-5)\n"
                "  --epsilon-scale V    same for scale (1e-5)\n"
                "\n"
                "Merging (these are what keep a merged character from stretching):\n"
                "  --translation MODE   root (default) | animated | all\n"
                "                         root     rotation everywhere, translation only on\n"
                "                                  root bones - keeps the target's proportions\n"
                "                         animated also drop translation that never changes\n"
                "                         all      verbatim; only for identical proportions\n"
                "  --[no-]ignore-scale      drop scale tracks (default on)\n"
                "  --[no-]retarget-root     re-anchor root motion onto the base rest pose and\n"
                "                           scale it by the hip-height ratio (default on)\n"
                "  --[no-]strip-namespace   'mixamorig:Hips' -> 'Hips' (default on)\n"
                "  --[no-]ignore-case       case-insensitive node matching (default on)\n"
                "  --skeleton-tracks-only   drop tracks on helpers, mesh nodes, cameras\n"
                "  --prefix TEXT            prepended to every merged clip name\n"
                "  --apply-policy           re-run the three settings above over every clip in\n"
                "                           the result, the base model's own included - this is\n"
                "                           how a file merged with the wrong settings is fixed\n"
                "  --min-match PERCENT      abort before writing if a source matches worse\n"
                "\n"
                "Clips:\n"
                "  --name-from-file     name each merged clip after its source file, which is\n"
                "                       what you want for Mixamo (every take is 'mixamo.com')\n"
                "  --rename OLD=NEW     rename by exact name, wildcard pattern or index\n"
                "  --drop PATTERN       delete matching clips outright; repeatable\n"
                "  --only PATTERN       export only matching clips; repeatable\n"
                "  --exclude PATTERN    do not export matching clips; repeatable\n"
                "\n"
                "Export:\n"
                "  --format FMT         fbx | fbx-ascii | glb | gltf (default: from --out)\n"
                "  --scale F            unit scale (default 100 for FBX, 1 for glTF/GLB)\n"
                "  --no-geometry        write animation only, no meshes\n"
                "  --[no-]embed-textures    embed image data (default on for glb)\n"
                "\n"
                "Control:\n"
                "  --dry-run            run everything but do not write the output\n"
                "  --keep-going         a failed animation source does not abort the run\n"
                "  --json               emit the whole report as JSON\n"
                "\n"
                "Example:\n"
                "  fam-cli merge --base char.fbx --anim clips/ --name-from-file \\\n"
                "                --out char_merged.glb --min-match 80\n");
            return;
        default:
            std::fprintf(stream, "No help topic named '%s'.\n", topic.c_str());
            return;
    }
}

ParseResult Parse(int argc, char** argv, Options& out) {
    ParseResult result;
    std::vector<std::string> positionals;

    auto fail = [&result](std::string message) {
        result.ok = false;
        result.exitCode = 2;
        result.error = std::move(message);
        return result;
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::string inlineValue;
        bool hasInline = false;

        if (arg.rfind("--", 0) == 0) {
            const size_t equals = arg.find('=');
            if (equals != std::string::npos) {
                inlineValue = arg.substr(equals + 1);
                arg = arg.substr(0, equals);
                hasInline = true;
            }
        }

        // Pulls the value of a `--flag value` / `--flag=value` pair.
        bool valueMissing = false;
        auto value = [&](void) -> std::string {
            if (hasInline) return inlineValue;
            if (i + 1 >= argc) {
                valueMissing = true;
                return {};
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            out.command = Command::Help;
            // `--help merge` reads the next token only when it is not itself a flag.
            if (hasInline) {
                out.helpTopic = inlineValue;
            } else if (i + 1 < argc && argv[i + 1][0] != '-') {
                out.helpTopic = argv[++i];
            }
            return result;
        }
        if (arg == "--version") {
            out.command = Command::Version;
            return result;
        }

        if (arg == "--json") {
            out.json = true;
        } else if (arg == "-q" || arg == "--quiet") {
            out.quiet = true;
        } else if (arg == "--log") {
            out.logFile = value();

        // ---------------------------------------------------------------- input
        } else if (arg == "--base" || arg == "--in" || arg == "--input") {
            out.base = value();
        } else if (arg == "--anim" || arg == "--animation") {
            out.anims.push_back(value());
        } else if (arg == "-o" || arg == "--out" || arg == "--output") {
            out.output = value();
        } else if (arg == "--recursive" || arg == "-r") {
            out.recursive = true;

        // --------------------------------------------------------------- import
        } else if (arg == "--bake-rate" || arg == "--sample-rate") {
            const std::string text = value();
            if (!valueMissing && !ParseFloatArg(text, out.import.sampleRate)) {
                return fail("--bake-rate expects a number, got '" + text + "'");
            }
            if (out.import.sampleRate <= 0.0f) return fail("--bake-rate must be positive");
        } else if (arg == "--epsilon-pos" || arg == "--epsilon-position") {
            const std::string text = value();
            if (!valueMissing && !ParseFloatArg(text, out.import.epsilonPosition)) {
                return fail("--epsilon-pos expects a number, got '" + text + "'");
            }
        } else if (arg == "--epsilon-rot" || arg == "--epsilon-rotation") {
            const std::string text = value();
            if (!valueMissing && !ParseFloatArg(text, out.import.epsilonRotation)) {
                return fail("--epsilon-rot expects a number, got '" + text + "'");
            }
        } else if (arg == "--epsilon-scale") {
            const std::string text = value();
            if (!valueMissing && !ParseFloatArg(text, out.import.epsilonScale)) {
                return fail("--epsilon-scale expects a number, got '" + text + "'");
            }

        // ---------------------------------------------------------------- merge
        } else if (arg == "--translation") {
            const std::string text = value();
            if (!valueMissing && !TranslationFromName(text, out.merge.translationMode)) {
                return fail("--translation expects root|animated|all, got '" + text + "'");
            }
        } else if (arg == "--ignore-scale") {
            out.merge.ignoreScaleTracks = true;
        } else if (arg == "--no-ignore-scale") {
            out.merge.ignoreScaleTracks = false;
        } else if (arg == "--retarget-root" || arg == "--retarget-root-motion") {
            out.merge.retargetRootMotion = true;
        } else if (arg == "--no-retarget-root" || arg == "--no-retarget-root-motion") {
            out.merge.retargetRootMotion = false;
        } else if (arg == "--strip-namespace") {
            out.merge.stripNamespace = true;
        } else if (arg == "--no-strip-namespace") {
            out.merge.stripNamespace = false;
        } else if (arg == "--ignore-case") {
            out.merge.caseInsensitive = true;
        } else if (arg == "--no-ignore-case") {
            out.merge.caseInsensitive = false;
        } else if (arg == "--skeleton-tracks-only") {
            out.merge.skeletonTracksOnly = true;
        } else if (arg == "--no-skeleton-tracks-only") {
            out.merge.skeletonTracksOnly = false;
        } else if (arg == "--prefix" || arg == "--name-prefix") {
            out.merge.namePrefix = value();
        } else if (arg == "--apply-policy") {
            out.applyPolicy = true;
        } else if (arg == "--min-match") {
            const std::string text = value();
            if (!valueMissing && !ParseFloatArg(text, out.minMatch)) {
                return fail("--min-match expects a percentage, got '" + text + "'");
            }

        // ---------------------------------------------------------------- clips
        } else if (arg == "--name-from-file") {
            out.nameFromFile = true;
        } else if (arg == "--rename") {
            const std::string text = value();
            const size_t equals = text.find('=');
            if (!valueMissing && equals == std::string::npos) {
                return fail("--rename expects OLD=NEW, got '" + text + "'");
            }
            if (!valueMissing) {
                out.renames.push_back({text.substr(0, equals), text.substr(equals + 1)});
            }
        } else if (arg == "--drop") {
            out.drop.push_back(value());
        } else if (arg == "--only") {
            out.only.push_back(value());
        } else if (arg == "--exclude") {
            out.exclude.push_back(value());

        // --------------------------------------------------------------- export
        } else if (arg == "--format") {
            const std::string text = value();
            if (!valueMissing && !FormatFromName(text, out.exportOptions.format)) {
                return fail("--format expects fbx|fbx-ascii|glb|gltf, got '" + text + "'");
            }
            out.formatExplicit = true;
        } else if (arg == "--scale") {
            const std::string text = value();
            if (!valueMissing && !ParseFloatArg(text, out.exportOptions.scale)) {
                return fail("--scale expects a number, got '" + text + "'");
            }
            out.scaleExplicit = true;
        } else if (arg == "--geometry") {
            out.exportOptions.includeGeometry = true;
        } else if (arg == "--no-geometry") {
            out.exportOptions.includeGeometry = false;
        } else if (arg == "--embed-textures") {
            out.exportOptions.embedTextures = true;
            out.embedExplicit = true;
        } else if (arg == "--no-embed-textures") {
            out.exportOptions.embedTextures = false;
            out.embedExplicit = true;

        // -------------------------------------------------------------- control
        } else if (arg == "--dry-run") {
            out.dryRun = true;
        } else if (arg == "--keep-going") {
            out.keepGoing = true;

        // ----------------------------------------------------------- info detail
        } else if (arg == "--nodes") {
            out.showNodes = true;
        } else if (arg == "--bones") {
            out.showBones = true;
        } else if (arg == "--tracks") {
            out.showTracks = true;

        } else if (!arg.empty() && arg[0] == '-' && arg != "-") {
            return fail("unknown option '" + arg + "'");
        } else {
            if (out.command == Command::None && positionals.empty()) {
                const Command command = CommandFromName(arg);
                if (command != Command::None) {
                    out.command = command;
                    continue;
                }
            }
            positionals.push_back(arg);
        }

        if (valueMissing) return fail("option '" + arg + "' expects a value");
    }

    if (out.command == Command::None) {
        if (positionals.empty()) {
            out.command = Command::Help;
            return result;
        }
        return fail("unknown command '" + positionals.front() + "'");
    }

    // First positional is the base model / input, the rest are animation sources.
    size_t next = 0;
    if (out.base.empty() && next < positionals.size()) out.base = positionals[next++];
    for (; next < positionals.size(); ++next) out.anims.push_back(positionals[next]);

    if (out.base.empty()) {
        return fail(out.command == Command::Info || out.command == Command::Convert
                        ? "no input file given"
                        : "no base model given (--base)");
    }
    if (out.command == Command::Convert && !out.anims.empty()) {
        return fail("convert takes a single input; use `merge` to add animation sources");
    }
    if (out.command == Command::Check && out.anims.empty()) {
        return fail("check needs at least one animation source (--anim)");
    }
    if ((out.command == Command::Merge || out.command == Command::Convert) && out.output.empty() &&
        !out.dryRun) {
        return fail("no output path given (--out), and --dry-run was not requested");
    }

    // The output extension picks the format unless --format said otherwise. `.fbx`
    // is ambiguous between the two FBX flavours; binary is the sane default.
    if (!out.formatExplicit && !out.output.empty()) {
        const std::string extension = ToLower(fs::path(out.output).extension().string());
        if (extension == ".fbx") {
            out.exportOptions.format = ExportFormat::FbxBinary;
        } else if (extension == ".glb") {
            out.exportOptions.format = ExportFormat::Glb;
        } else if (extension == ".gltf") {
            out.exportOptions.format = ExportFormat::GltfSeparate;
        } else if (!extension.empty()) {
            return fail("cannot tell the format from '" + extension + "'; pass --format");
        }
    }
    if (!out.scaleExplicit) out.exportOptions.scale = DefaultScaleFor(out.exportOptions.format);
    if (!out.embedExplicit) {
        out.exportOptions.embedTextures = out.exportOptions.format == ExportFormat::Glb;
    }

    return result;
}

}  // namespace fam::cli
