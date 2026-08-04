// Entry point for fam-cli, the headless twin of the desktop application.
#include <cstdio>

#include "cli/Cli.h"
#include "util/Log.h"

int main(int argc, char** argv) {
    using namespace fam;
    using namespace fam::cli;

    Options options;
    const ParseResult parsed = Parse(argc, argv, options);
    if (!parsed.ok) {
        std::fprintf(stderr, "fam-cli: %s\n\n", parsed.error.c_str());
        PrintUsage(stderr, {});
        return parsed.exitCode;
    }

    switch (options.command) {
        case Command::Help:
            PrintUsage(stdout, options.helpTopic);
            return 0;
        case Command::Version:
            std::printf("fam-cli %s (%s)\n", kVersion, kAppCommit);
            return 0;
        default:
            break;
    }

    // stdout carries the report and nothing else, so it can be redirected into a file
    // or a JSON parser whole. Progress belongs on stderr - and mixing the two would
    // also reorder them, since the streams buffer differently once piped.
    Log::Get().SetConsoleStreams(options.quiet ? nullptr : stderr, stderr);
    if (!options.logFile.empty()) Log::Get().OpenFile(options.logFile);

    switch (options.command) {
        case Command::Info:    return RunInfo(options);
        case Command::Check:   return RunCheck(options);
        case Command::Merge:
        case Command::Convert: return RunMerge(options);
        default:
            PrintUsage(stderr, {});
            return 2;
    }
}
