// The commands themselves. Each one drives fam_core exactly the way the interface
// does, and reports either as text for a human or as one JSON document on stdout.
#include "cli/Cli.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "core/Model.h"
#include "util/Log.h"

namespace fs = std::filesystem;

namespace fam::cli {
namespace {

// --------------------------------------------------------------------- JSON

std::string JsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
                    out += buffer;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Streaming writer: enough for the fixed shapes below, and it keeps the reports
// readable in a terminal without pulling in a JSON library.
class JsonWriter {
public:
    explicit JsonWriter(std::FILE* stream) : m_stream(stream) {}

    void BeginObject(const char* key = nullptr) { Open('{', key); }
    void EndObject() { Close('}'); }
    void BeginArray(const char* key = nullptr) { Open('[', key); }
    void EndArray() { Close(']'); }

    void Str(const char* key, const std::string& value) {
        Prefix(key);
        std::fprintf(m_stream, "\"%s\"", JsonEscape(value).c_str());
    }
    void Int(const char* key, long long value) {
        Prefix(key);
        std::fprintf(m_stream, "%lld", value);
    }
    void Num(const char* key, double value) {
        Prefix(key);
        std::fprintf(m_stream, "%.6g", value);
    }
    void Bool(const char* key, bool value) {
        Prefix(key);
        std::fputs(value ? "true" : "false", m_stream);
    }
    void StrArray(const char* key, const std::vector<std::string>& values) {
        BeginArray(key);
        for (const std::string& value : values) Str(nullptr, value);
        EndArray();
    }

    void Finish() { std::fputc('\n', m_stream); }

private:
    void Indent() {
        for (int i = 0; i < m_depth; ++i) std::fputs("  ", m_stream);
    }
    void Prefix(const char* key) {
        if (m_needComma) std::fputc(',', m_stream);
        if (m_started) std::fputc('\n', m_stream);
        m_started = true;
        Indent();
        if (key != nullptr) std::fprintf(m_stream, "\"%s\": ", key);
        m_needComma = true;
    }
    void Open(char brace, const char* key) {
        Prefix(key);
        std::fputc(brace, m_stream);
        ++m_depth;
        m_needComma = false;
    }
    void Close(char brace) {
        --m_depth;
        if (m_needComma) {
            std::fputc('\n', m_stream);
            Indent();
        }
        std::fputc(brace, m_stream);
        m_needComma = true;
    }

    std::FILE* m_stream;
    int m_depth = 0;
    bool m_needComma = false;
    bool m_started = false;
};

// ------------------------------------------------------------------ helpers

std::string FileName(const std::string& path) {
    return fs::path(path).filename().string();
}

std::string LowerExtension(const fs::path& path) {
    std::string extension = path.extension().string();
    for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension;
}

const char* FormatName(ExportFormat format) {
    switch (format) {
        case ExportFormat::FbxBinary:    return "fbx";
        case ExportFormat::FbxAscii:     return "fbx-ascii";
        case ExportFormat::Glb:          return "glb";
        case ExportFormat::GltfSeparate: return "gltf";
    }
    return "fbx";
}

const char* TranslationName(TranslationMode mode) {
    switch (mode) {
        case TranslationMode::RootBonesOnly: return "root";
        case TranslationMode::AnimatedOnly:  return "animated";
        case TranslationMode::CopyAll:       return "all";
    }
    return "root";
}

struct TextureStats {
    size_t total = 0;
    size_t embedded = 0;
};

TextureStats CountTextures(const Model& model) {
    TextureStats stats;
    std::set<std::string> keys;
    for (const Material& material : model.materials) {
        for (const TextureSource* source : {&material.baseColorTexture, &material.normalTexture}) {
            if (source->Empty()) continue;
            if (keys.insert(source->Key()).second && source->Embedded()) ++stats.embedded;
        }
    }
    stats.total = keys.size();
    return stats;
}

// Expands the --anim list: files are taken as they are, directories contribute
// every .fbx inside them, sorted so a run is reproducible.
bool CollectSources(const Options& options, std::vector<std::string>& out, std::string& error) {
    for (const std::string& entry : options.anims) {
        std::error_code ec;
        const fs::path path(entry);
        if (!fs::exists(path, ec)) {
            error = "no such file or directory: " + entry;
            return false;
        }
        if (!fs::is_directory(path, ec)) {
            out.push_back(path.string());
            continue;
        }

        std::vector<std::string> found;
        auto consider = [&found](const fs::directory_entry& item) {
            if (item.is_regular_file() && LowerExtension(item.path()) == ".fbx") {
                found.push_back(item.path().string());
            }
        };
        if (options.recursive) {
            for (const auto& item : fs::recursive_directory_iterator(path, ec)) consider(item);
        } else {
            for (const auto& item : fs::directory_iterator(path, ec)) consider(item);
        }
        if (ec) {
            error = "could not read directory " + entry + ": " + ec.message();
            return false;
        }
        if (found.empty()) {
            error = "no .fbx files in " + entry;
            return false;
        }
        std::sort(found.begin(), found.end());
        out.insert(out.end(), found.begin(), found.end());
    }
    return true;
}

bool ClipMatches(const std::string& pattern, const Model& model, int index) {
    const std::string& name = model.animations[static_cast<size_t>(index)].name;
    if (pattern == name) return true;
    if (WildcardMatch(pattern, name)) return true;
    // An all-digit pattern addresses a clip by its position in the list.
    if (!pattern.empty() && pattern.size() < 10 &&
        std::all_of(pattern.begin(), pattern.end(),
                    [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return std::stoi(pattern) == index;
    }
    return false;
}

void PrintClipTable(const Model& model, bool showSelection, bool showTracks) {
    if (model.animations.empty()) {
        std::printf("  (no clips)\n");
        return;
    }
    std::printf("  %-3s %-3s %-32s %9s %8s %8s  %s\n", "", "#", "Clip", "Length", "Frames",
                "Tracks", "Source");
    for (size_t i = 0; i < model.animations.size(); ++i) {
        const Animation& anim = model.animations[i];
        const char* mark = showSelection ? (anim.exportSelected ? "[x]" : "[ ]") : "";
        std::printf("  %-3s %-3zu %-32s %8.2fs %8d %8zu  %s\n", mark, i, anim.name.c_str(),
                    static_cast<double>(anim.duration), anim.FrameCount(), anim.tracks.size(),
                    FileName(anim.sourceFile).c_str());
        if (showTracks) {
            for (const NodeTrack& track : anim.tracks) {
                std::printf("        %s\n", track.nodeName.c_str());
            }
        }
    }
}

void WriteClipsJson(JsonWriter& json, const Model& model, bool includeTracks) {
    json.BeginArray("clips");
    for (size_t i = 0; i < model.animations.size(); ++i) {
        const Animation& anim = model.animations[i];
        json.BeginObject();
        json.Int("index", static_cast<long long>(i));
        json.Str("name", anim.name);
        json.Num("duration", static_cast<double>(anim.duration));
        json.Int("frames", anim.FrameCount());
        json.Num("sampleRate", static_cast<double>(anim.sampleRate));
        json.Int("tracks", static_cast<long long>(anim.tracks.size()));
        json.Str("sourceFile", anim.sourceFile);
        json.Bool("selected", anim.exportSelected);
        if (includeTracks) {
            std::vector<std::string> names;
            names.reserve(anim.tracks.size());
            for (const NodeTrack& track : anim.tracks) names.push_back(track.nodeName);
            json.StrArray("trackNodes", names);
        }
        json.EndObject();
    }
    json.EndArray();
}

void WriteStatsJson(JsonWriter& json, const Model& model, float sourceUnitMeters) {
    const TextureStats textures = CountTextures(model);
    json.BeginObject("stats");
    json.Int("nodes", static_cast<long long>(model.nodes.size()));
    json.Int("meshes", static_cast<long long>(model.meshes.size()));
    json.Int("triangles", static_cast<long long>(model.totalTriangles));
    json.Int("vertices", static_cast<long long>(model.totalVertices));
    json.Int("bones", static_cast<long long>(model.skeleton.bones.size()));
    json.Int("materials", static_cast<long long>(model.materials.size()));
    json.Int("textures", static_cast<long long>(textures.total));
    json.Int("embeddedTextures", static_cast<long long>(textures.embedded));
    json.Int("clips", static_cast<long long>(model.animations.size()));
    json.Num("sourceUnitMeters", static_cast<double>(sourceUnitMeters));
    json.EndObject();
}

void PrintStats(const Model& model, const ImportResult& result) {
    const TextureStats textures = CountTextures(model);
    std::printf("  nodes %zu   meshes %zu   triangles %zu   vertices %zu\n", model.nodes.size(),
                model.meshes.size(), model.totalTriangles, model.totalVertices);
    std::printf("  bones %zu   materials %zu   textures %zu (%zu embedded)   clips %zu\n",
                model.skeleton.bones.size(), model.materials.size(), textures.total,
                textures.embedded, model.animations.size());
    std::printf("  source unit %.4g m\n", static_cast<double>(result.sourceUnitMeters));
}

// A failure early enough that there is no report to fill in. In --json mode it still
// has to come back as a document, or a caller that parses stdout gets nothing at all.
int Fail(const char* command, const std::string& message, bool json) {
    if (json) {
        JsonWriter writer(stdout);
        writer.BeginObject();
        writer.Str("command", command);
        writer.Bool("ok", false);
        writer.Str("error", message);
        writer.EndObject();
        writer.Finish();
    } else {
        std::fprintf(stderr, "error: %s\n", message.c_str());
    }
    return 1;
}

// The result of merging one animation source, kept so the text and JSON reports can
// both be produced from the same data.
struct SourceOutcome {
    std::string path;
    bool ok = false;
    std::string error;
    size_t clipsFound = 0;
    float compatibility = 0.0f;
    MergeReport report;
};

}  // namespace

// ---------------------------------------------------------------------- info

int RunInfo(const Options& options) {
    ImportOptions importOptions = options.import;
    importOptions.importGeometry = true;
    importOptions.importAnimations = true;

    Model model;
    const ImportResult result = ImportFbx(options.base, importOptions, model);
    if (!result.ok) return Fail("info", result.error, options.json);

    if (options.json) {
        JsonWriter json(stdout);
        json.BeginObject();
        json.Str("command", "info");
        json.Bool("ok", true);
        json.Str("file", model.sourcePath);
        WriteStatsJson(json, model, result.sourceUnitMeters);
        WriteClipsJson(json, model, options.showTracks);
        if (options.showNodes) {
            std::vector<std::string> names;
            names.reserve(model.nodes.size());
            for (const Node& node : model.nodes) names.push_back(node.name);
            json.StrArray("nodes", names);
        }
        if (options.showBones) {
            std::vector<std::string> names;
            names.reserve(model.skeleton.bones.size());
            for (const Bone& bone : model.skeleton.bones) {
                if (bone.nodeIndex >= 0 && bone.nodeIndex < static_cast<int>(model.nodes.size())) {
                    names.push_back(model.nodes[static_cast<size_t>(bone.nodeIndex)].name);
                }
            }
            json.StrArray("bones", names);
        }
        json.EndObject();
        json.Finish();
        return 0;
    }

    std::printf("%s\n", model.sourcePath.c_str());
    PrintStats(model, result);

    if (options.showNodes) {
        std::printf("\nNodes (%zu):\n", model.nodes.size());
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            const Node& node = model.nodes[i];
            int depth = 0;
            for (int parent = node.parent; parent >= 0; parent = model.nodes[static_cast<size_t>(parent)].parent) {
                ++depth;
            }
            std::printf("  %*s%s\n", depth * 2, "", node.name.c_str());
        }
    }
    if (options.showBones) {
        std::printf("\nBones (%zu):\n", model.skeleton.bones.size());
        for (const Bone& bone : model.skeleton.bones) {
            if (bone.nodeIndex >= 0 && bone.nodeIndex < static_cast<int>(model.nodes.size())) {
                std::printf("  %s\n", model.nodes[static_cast<size_t>(bone.nodeIndex)].name.c_str());
            }
        }
    }

    std::printf("\nClips (%zu):\n", model.animations.size());
    PrintClipTable(model, false, options.showTracks);
    return 0;
}

// --------------------------------------------------------------------- check

int RunCheck(const Options& options) {
    std::vector<std::string> sources;
    std::string error;
    if (!CollectSources(options, sources, error)) return Fail("check", error, options.json);

    ImportOptions baseImport = options.import;
    baseImport.importGeometry = true;   // the skeleton comes off the skin clusters
    baseImport.importAnimations = true;

    Model base;
    const ImportResult baseResult = ImportFbx(options.base, baseImport, base);
    if (!baseResult.ok) return Fail("check", baseResult.error, options.json);

    ImportOptions clipImport = options.import;
    clipImport.importGeometry = false;
    clipImport.importAnimations = true;

    std::vector<SourceOutcome> outcomes;
    bool belowThreshold = false;

    for (const std::string& path : sources) {
        SourceOutcome outcome;
        outcome.path = path;

        Model source;
        const ImportResult result = ImportFbx(path, clipImport, source);
        if (!result.ok) {
            outcome.error = result.error;
            outcomes.push_back(std::move(outcome));
            continue;
        }
        if (source.animations.empty()) {
            outcome.error = "no animation stacks";
            outcomes.push_back(std::move(outcome));
            continue;
        }

        outcome.clipsFound = source.animations.size();
        outcome.compatibility = EstimateCompatibility(base, source, options.merge);

        // A trial merge is the only thing that knows exactly which node names fail to
        // bind, so run one and drop the clips it appended again: nothing else in the
        // model is touched, which leaves `base` reusable for the next source.
        const size_t before = base.animations.size();
        outcome.report = MergeAnimations(base, source, options.merge);
        base.animations.resize(before);

        outcome.ok = true;
        if (outcome.compatibility * 100.0f < options.minMatch) belowThreshold = true;
        outcomes.push_back(std::move(outcome));
    }

    const bool anyFailed =
        std::any_of(outcomes.begin(), outcomes.end(), [](const SourceOutcome& o) { return !o.ok; });

    if (options.json) {
        JsonWriter json(stdout);
        json.BeginObject();
        json.Str("command", "check");
        json.Bool("ok", !anyFailed && !belowThreshold);
        json.Str("base", base.sourcePath);
        json.Int("baseBones", static_cast<long long>(base.skeleton.bones.size()));
        json.Num("minMatch", static_cast<double>(options.minMatch));
        json.BeginArray("sources");
        for (const SourceOutcome& outcome : outcomes) {
            json.BeginObject();
            json.Str("path", outcome.path);
            json.Bool("ok", outcome.ok);
            if (!outcome.ok) json.Str("error", outcome.error);
            json.Int("clipsFound", static_cast<long long>(outcome.clipsFound));
            json.Num("match", static_cast<double>(outcome.compatibility));
            json.Int("tracksMatched", outcome.report.tracksMatched);
            json.Int("tracksDropped", outcome.report.tracksDropped);
            json.StrArray("unmatchedNodes", outcome.report.unmatchedNodes);
            json.EndObject();
        }
        json.EndArray();
        json.EndObject();
        json.Finish();
    } else {
        std::printf("Base: %s (%zu bones)\n\n", base.sourcePath.c_str(),
                    base.skeleton.bones.size());
        for (const SourceOutcome& outcome : outcomes) {
            if (!outcome.ok) {
                std::printf("  %-40s FAILED  %s\n", FileName(outcome.path).c_str(),
                            outcome.error.c_str());
                continue;
            }
            std::printf("  %-40s %3.0f%% match, %zu clip(s), %d track(s) bind, %d drop\n",
                        FileName(outcome.path).c_str(),
                        static_cast<double>(outcome.compatibility * 100.0f), outcome.clipsFound,
                        outcome.report.tracksMatched, outcome.report.tracksDropped);
            for (const std::string& name : outcome.report.unmatchedNodes) {
                std::printf("        unmatched: %s\n", name.c_str());
            }
        }
        if (belowThreshold) {
            std::printf("\nAt least one source is below the %.0f%% threshold.\n",
                        static_cast<double>(options.minMatch));
        }
    }

    return (anyFailed || belowThreshold) ? 1 : 0;
}

// --------------------------------------------------------- merge and convert

int RunMerge(const Options& options) {
    const char* commandName = options.command == Command::Convert ? "convert" : "merge";

    std::vector<std::string> sources;
    std::string collectError;
    if (!CollectSources(options, sources, collectError)) {
        return Fail(commandName, collectError, options.json);
    }

    ImportOptions baseImport = options.import;
    baseImport.importGeometry = true;
    baseImport.importAnimations = true;

    Model model;
    const ImportResult baseResult = ImportFbx(options.base, baseImport, model);
    if (!baseResult.ok) return Fail(commandName, baseResult.error, options.json);
    LogSuccess("Loaded '%s': %zu nodes, %zu meshes, %zu bones, %zu clips, %zu tris (source unit %.4g m).",
               FileName(options.base).c_str(), baseResult.nodeCount, baseResult.meshCount,
               baseResult.boneCount, baseResult.animationCount, model.totalTriangles,
               static_cast<double>(baseResult.sourceUnitMeters));

    ImportOptions clipImport = options.import;
    clipImport.importGeometry = false;
    clipImport.importAnimations = true;

    std::vector<SourceOutcome> outcomes;
    bool failed = false;
    bool belowThreshold = false;

    for (const std::string& path : sources) {
        SourceOutcome outcome;
        outcome.path = path;

        Model source;
        const ImportResult result = ImportFbx(path, clipImport, source);
        if (!result.ok) {
            outcome.error = result.error;
        } else if (source.animations.empty()) {
            outcome.error = "no animation stacks";
        }

        if (!outcome.error.empty()) {
            LogError("'%s': %s", FileName(path).c_str(), outcome.error.c_str());
            outcomes.push_back(std::move(outcome));
            failed = true;
            if (!options.keepGoing) break;
            continue;
        }

        outcome.clipsFound = source.animations.size();
        outcome.compatibility = EstimateCompatibility(model, source, options.merge);

        const size_t before = model.animations.size();
        outcome.report = MergeAnimations(model, source, options.merge);

        // Every Mixamo take is called "mixamo.com", so the file name is the only thing
        // that tells merged clips apart. Renaming here, before the next source lands,
        // keeps MakeUniqueAnimationName's disambiguation local to this file.
        if (options.nameFromFile && outcome.report.animationsAdded > 0) {
            const std::string stem = fs::path(path).stem().string();
            outcome.report.addedNames.clear();
            for (size_t i = before; i < model.animations.size(); ++i) {
                model.animations[i].name = model.MakeUniqueAnimationName(
                    options.merge.namePrefix + stem, static_cast<int>(i));
                outcome.report.addedNames.push_back(model.animations[i].name);
            }
        }

        if (outcome.report.animationsAdded == 0) {
            outcome.error = "nothing merged";
            LogError("'%s': nothing merged (%.0f%% of animated nodes matched the base rig).",
                     FileName(path).c_str(), static_cast<double>(outcome.compatibility * 100.0f));
            outcomes.push_back(std::move(outcome));
            failed = true;
            if (!options.keepGoing) break;
            continue;
        }

        outcome.ok = true;
        LogSuccess("'%s': merged %d clip(s), %d track(s) bound, %d dropped (%.0f%% rig match).",
                   FileName(path).c_str(), outcome.report.animationsAdded,
                   outcome.report.tracksMatched, outcome.report.tracksDropped,
                   static_cast<double>(outcome.compatibility * 100.0f));
        if (outcome.report.translationChannelsStripped > 0 ||
            outcome.report.scaleChannelsStripped > 0) {
            LogInfo("  kept target proportions: stripped %d translation and %d scale channel(s).",
                    outcome.report.translationChannelsStripped,
                    outcome.report.scaleChannelsStripped);
        }
        if (outcome.report.rootTracksRetargeted > 0) {
            LogInfo("  retargeted %d root track(s) onto the base rest pose (hip height x%.3f).",
                    outcome.report.rootTracksRetargeted,
                    static_cast<double>(outcome.report.rootMotionScale));
        }
        for (const std::string& name : outcome.report.unmatchedNodes) {
            LogWarn("  unmatched node: %s", name.c_str());
        }

        if (outcome.compatibility * 100.0f < options.minMatch) {
            belowThreshold = true;
            LogError("'%s': %.0f%% rig match is below the %.0f%% required.",
                     FileName(path).c_str(),
                     static_cast<double>(outcome.compatibility * 100.0f),
                     static_cast<double>(options.minMatch));
        }
        outcomes.push_back(std::move(outcome));
    }

    // ------------------------------------------------------------ clip edits
    if (options.applyPolicy) {
        const MergeReport report = ApplyTrackPolicy(model, options.merge, true);
        LogInfo("Re-applied the track policy to %d clip(s): %d translation and %d scale "
                "channel(s) stripped, %d root track(s) re-anchored.",
                report.animationsAdded, report.translationChannelsStripped,
                report.scaleChannelsStripped, report.rootTracksRetargeted);
    }

    for (const Rename& rename : options.renames) {
        int renamed = 0;
        for (int i = 0; i < static_cast<int>(model.animations.size()); ++i) {
            if (!ClipMatches(rename.from, model, i)) continue;
            model.animations[static_cast<size_t>(i)].name =
                model.MakeUniqueAnimationName(rename.to, i);
            ++renamed;
        }
        if (renamed == 0) {
            LogWarn("--rename '%s': no clip matched.", rename.from.c_str());
        }
    }

    for (const std::string& pattern : options.drop) {
        int dropped = 0;
        for (int i = static_cast<int>(model.animations.size()) - 1; i >= 0; --i) {
            if (!ClipMatches(pattern, model, i)) continue;
            model.animations.erase(model.animations.begin() + i);
            ++dropped;
        }
        if (dropped > 0) {
            LogInfo("--drop '%s': removed %d clip(s).", pattern.c_str(), dropped);
        } else {
            LogWarn("--drop '%s': no clip matched.", pattern.c_str());
        }
    }

    if (!options.only.empty()) {
        for (int i = 0; i < static_cast<int>(model.animations.size()); ++i) {
            bool keep = false;
            for (const std::string& pattern : options.only) {
                if (ClipMatches(pattern, model, i)) keep = true;
            }
            model.animations[static_cast<size_t>(i)].exportSelected = keep;
        }
    }
    for (const std::string& pattern : options.exclude) {
        for (int i = 0; i < static_cast<int>(model.animations.size()); ++i) {
            if (ClipMatches(pattern, model, i)) {
                model.animations[static_cast<size_t>(i)].exportSelected = false;
            }
        }
    }

    // ---------------------------------------------------------------- export
    ExportOptions exportOptions = options.exportOptions;
    exportOptions.animations.clear();
    for (int i = 0; i < static_cast<int>(model.animations.size()); ++i) {
        if (model.animations[static_cast<size_t>(i)].exportSelected) {
            exportOptions.animations.push_back(i);
        }
    }
    const size_t selectedClips = exportOptions.animations.size();
    // An empty list means "everything" downstream, so guard the all-deselected case.
    if (exportOptions.animations.empty()) exportOptions.animations.push_back(-1);

    ExportResult exportResult;
    bool written = false;
    // A source that failed under --keep-going still leaves a usable result, so it only
    // colours the exit code. A rig match below the threshold always stops the write:
    // asking for the gate is asking not to get the file when it is not met.
    const bool blocked = belowThreshold || (failed && !options.keepGoing);

    if (options.dryRun) {
        LogInfo("Dry run: nothing written.");
    } else if (blocked) {
        LogError("Not writing '%s': the run reported failures.", options.output.c_str());
    } else {
        exportResult = ExportModel(model, options.output, exportOptions);
        written = exportResult.ok;
        if (!exportResult.ok) {
            LogError("Export failed: %s", exportResult.error.c_str());
        } else {
            LogSuccess("Exported '%s' (%zu mesh part(s), %zu clip(s), scale x%.4g).",
                       FileName(options.output).c_str(), exportResult.meshCount,
                       exportResult.animationCount, static_cast<double>(exportOptions.scale));
        }
    }

    const bool ok = !failed && !belowThreshold && (options.dryRun || written);

    // ---------------------------------------------------------------- report
    if (options.json) {
        JsonWriter json(stdout);
        json.BeginObject();
        json.Str("command", commandName);
        json.Bool("ok", ok);
        json.BeginObject("base");
        json.Str("path", model.sourcePath);
        json.Int("clips", static_cast<long long>(baseResult.animationCount));
        json.EndObject();
        WriteStatsJson(json, model, baseResult.sourceUnitMeters);

        json.BeginObject("merge");
        json.Str("translation", TranslationName(options.merge.translationMode));
        json.Bool("ignoreScaleTracks", options.merge.ignoreScaleTracks);
        json.Bool("retargetRootMotion", options.merge.retargetRootMotion);
        json.Bool("stripNamespace", options.merge.stripNamespace);
        json.Bool("caseInsensitive", options.merge.caseInsensitive);
        json.Bool("skeletonTracksOnly", options.merge.skeletonTracksOnly);
        json.Str("namePrefix", options.merge.namePrefix);
        json.Num("bakeRate", static_cast<double>(options.import.sampleRate));
        json.EndObject();

        json.BeginArray("sources");
        for (const SourceOutcome& outcome : outcomes) {
            json.BeginObject();
            json.Str("path", outcome.path);
            json.Bool("ok", outcome.ok);
            if (!outcome.error.empty()) json.Str("error", outcome.error);
            json.Int("clipsFound", static_cast<long long>(outcome.clipsFound));
            json.Num("match", static_cast<double>(outcome.compatibility));
            json.Int("animationsAdded", outcome.report.animationsAdded);
            json.Int("tracksMatched", outcome.report.tracksMatched);
            json.Int("tracksDropped", outcome.report.tracksDropped);
            json.Int("translationChannelsStripped", outcome.report.translationChannelsStripped);
            json.Int("scaleChannelsStripped", outcome.report.scaleChannelsStripped);
            json.Int("rootTracksRetargeted", outcome.report.rootTracksRetargeted);
            json.Num("rootMotionScale", static_cast<double>(outcome.report.rootMotionScale));
            json.StrArray("unmatchedNodes", outcome.report.unmatchedNodes);
            json.StrArray("addedNames", outcome.report.addedNames);
            json.EndObject();
        }
        json.EndArray();

        WriteClipsJson(json, model, false);

        json.BeginObject("output");
        json.Str("path", options.output);
        json.Str("format", FormatName(exportOptions.format));
        json.Num("scale", static_cast<double>(exportOptions.scale));
        json.Bool("includeGeometry", exportOptions.includeGeometry);
        json.Bool("embedTextures", exportOptions.embedTextures);
        json.Int("selectedClips", static_cast<long long>(selectedClips));
        json.Bool("dryRun", options.dryRun);
        json.Bool("written", written);
        if (!exportResult.ok && !exportResult.error.empty()) {
            json.Str("error", exportResult.error);
        }
        json.EndObject();
        json.EndObject();
        json.Finish();
    } else {
        std::printf("\nResult: %zu clip(s), %zu selected for export\n", model.animations.size(),
                    selectedClips);
        PrintClipTable(model, true, false);
        if (options.dryRun) {
            std::printf("\nWould write %s as %s at scale x%.4g (dry run).\n",
                        options.output.empty() ? "(no --out given)" : options.output.c_str(),
                        FormatName(exportOptions.format),
                        static_cast<double>(exportOptions.scale));
        } else if (written) {
            std::printf("\nWrote %s (%zu mesh part(s), %zu clip(s)).\n", options.output.c_str(),
                        exportResult.meshCount, exportResult.animationCount);
        }
    }

    return ok ? 0 : 1;
}

}  // namespace fam::cli
