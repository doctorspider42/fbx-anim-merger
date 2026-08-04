// Checks GitHub for a newer release and, for installed copies, applies it.
//
// The whole flow is asynchronous: CheckAsync() and DownloadAsync() return
// immediately and the caller polls State() once a frame, so the UI thread never
// blocks on the network. Only Windows has an implementation; elsewhere a check
// resolves to Failed with an explanatory message and the UI falls back to
// pointing at the releases page.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace fam {

struct ReleaseInfo {
    std::string version;       // tag with any leading "v" removed
    std::string tag;
    std::string pageUrl;       // the release's page on github.com
    std::string notes;         // release body, as markdown
    std::string installerUrl;  // "*-setup.exe" asset, empty when the release has none
    std::string portableUrl;   // "*-portable.zip" asset
    std::string installerName;

    bool Valid() const { return !version.empty(); }
};

enum class UpdateState {
    Idle,
    Checking,
    UpToDate,
    Available,
    Downloading,
    ReadyToInstall,
    Failed,
};

// Compares dotted numeric versions component by component; missing components
// count as zero, so "0.2" < "0.2.1". Returns -1, 0 or 1. Anything non-numeric in
// a component (a "-rc1" suffix, say) is ignored, which is deliberate: this only
// ever has to order the releases this project's CI produces.
int CompareVersions(const std::string& a, const std::string& b);

// True when the running .exe is an unzipped portable copy rather than an
// installed one, decided by the PORTABLE marker file the zip carries. Portable
// copies are never overwritten in place - there is no installer to hand them to.
bool IsPortableBuild();

// True when the folder the application lives in is not writable by the current
// user - an install made machine-wide by choosing "Install for all users". The
// installer then has to run elevated, so Windows shows a UAC prompt where a
// per-user install shows nothing.
bool InstallNeedsElevation();

// Hands a URL to the system browser. Available on every platform.
bool OpenInBrowser(const std::string& url);

class Updater {
public:
    Updater() = default;
    ~Updater();

    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;

    // Queries the newest non-prerelease. Ignored while a check or download is
    // already running.
    void CheckAsync();

    // Fetches the release's installer into the temp directory. Requires State()
    // == Available and a release that carries an installer asset.
    void DownloadAsync();

    // Runs the downloaded installer silently and returns true if it started, in
    // which case the caller must quit: the installer replaces the running .exe
    // and relaunches it when it is done.
    bool LaunchInstaller();

    UpdateState State() const { return m_state.load(std::memory_order_acquire); }
    bool Busy() const;

    // Snapshots taken under the lock, so they are safe to hold across a frame.
    ReleaseInfo Release() const;
    std::string Error() const;

    // 0..1 while downloading, or -1 when the server sent no content length.
    float Progress() const { return m_progress.load(std::memory_order_relaxed); }

    // True for the check that a UI action started, as opposed to the silent one
    // at start-up: only the former reports "you are up to date".
    bool UserInitiated() const { return m_userInitiated.load(std::memory_order_relaxed); }
    void SetUserInitiated(bool value) { m_userInitiated.store(value, std::memory_order_relaxed); }

private:
    void Join();
    void Finish(UpdateState state, const std::string& error);

    std::thread m_worker;
    std::atomic<UpdateState> m_state{UpdateState::Idle};
    std::atomic<float> m_progress{0.0f};
    std::atomic<bool> m_userInitiated{false};
    std::atomic<bool> m_cancel{false};

    mutable std::mutex m_mutex;
    ReleaseInfo m_release;
    std::string m_error;
    std::string m_downloadedFile;
};

}  // namespace fam
