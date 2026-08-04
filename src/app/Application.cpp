#include "app/Application.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

#include <cstdio>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#ifndef GLFW_INCLUDE_NONE  // also set on the command line by the imgui target
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include "gl/GL.h"
#include "util/FileDialog.h"
#include "util/Log.h"
#include "util/Version.h"

namespace fs = std::filesystem;

namespace fam {
namespace {

void GlfwErrorCallback(int code, const char* description) {
    LogError("GLFW error %d: %s", code, description);
}

const std::vector<FileFilter> kFbxFilters = {{"FBX scene", "fbx"}};

}  // namespace

int Application::Run() {
    // Opened before anything else so a failure during start-up still leaves a trace.
    Log::Get().OpenFile("fbx-anim-merger.log");
    LogInfo("%s %s (%s), %s build.", kAppName, kAppVersion, kAppCommit,
            IsPortableBuild() ? "portable" : "installed");

    if (!Initialize()) {
        Shutdown();
        return 1;
    }

    auto previous = std::chrono::steady_clock::now();
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();

        // The window manager's close button sets the flag directly, so take it back
        // and route it through the same confirmation the menu uses.
        if (glfwWindowShouldClose(m_window)) {
            glfwSetWindowShouldClose(m_window, GLFW_FALSE);
            RequestAction(PendingAction::Quit);
        }

        const auto now = std::chrono::steady_clock::now();
        const float delta = std::chrono::duration<float>(now - previous).count();
        previous = now;
        m_frameMilliseconds = m_frameMilliseconds * 0.9f + delta * 1000.0f * 0.1f;

        Frame(std::min(delta, 0.1f));
    }

    Shutdown();
    return 0;
}

bool Application::Initialize() {
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) {
        LogError("Failed to initialise GLFW.");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 0);  // the viewport does its own MSAA
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    // Pick a window size in physical pixels: scale the design size by the display's
    // reported scaling, then keep it inside the work area so a 200% display does not
    // produce a window larger than the screen.
    int width = 1600;
    int height = 950;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) {
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        glfwGetMonitorContentScale(monitor, &scaleX, &scaleY);
        m_dpiScale = std::max(scaleX, 0.25f);

        int areaX = 0;
        int areaY = 0;
        int areaWidth = 0;
        int areaHeight = 0;
        glfwGetMonitorWorkarea(monitor, &areaX, &areaY, &areaWidth, &areaHeight);
        if (areaWidth > 0 && areaHeight > 0) {
            width = std::min(static_cast<int>(1600 * m_dpiScale),
                             static_cast<int>(areaWidth * 0.9f));
            height = std::min(static_cast<int>(950 * m_dpiScale),
                              static_cast<int>(areaHeight * 0.9f));
        }
    }

    m_window = glfwCreateWindow(width, height, "FBX Animation Merger", nullptr, nullptr);
    if (!m_window) {
        LogError("Failed to create a window with an OpenGL 3.3 core context.");
        return false;
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    if (!gl::LoadFunctions(reinterpret_cast<void* (*)(const char*)>(glfwGetProcAddress))) {
        LogError("Failed to load the required OpenGL entry points.");
        return false;
    }

    LogInfo("OpenGL %s | %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    LogInfo("Display scaling %.0f%%, window %dx%d px.", m_dpiScale * 100.0f, width, height);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "fbx-anim-merger.ini";

    // Persist the interface scale alongside the docking layout. Registered before the
    // first frame, which is when ImGui reads the ini back.
    static ImGuiSettingsHandler uiSettings;
    uiSettings.TypeName = "FbxAnimMergerUI";
    uiSettings.TypeHash = ImHashStr("FbxAnimMergerUI");
    uiSettings.UserData = this;
    uiSettings.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* handler,
                               const char*) -> void* { return handler->UserData; };
    uiSettings.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry,
                               const char* line) {
        auto* app = static_cast<Application*>(entry);
        float zoom = 0.0f;
        int flag = 0;
        char text[64] = {};
        if (std::sscanf(line, "Zoom=%f", &zoom) == 1) {
            app->m_uiZoom = std::clamp(zoom, 0.5f, 3.0f);
        } else if (std::sscanf(line, "FollowSystemDpi=%d", &flag) == 1) {
            app->m_followSystemDpi = flag != 0;
        } else if (std::sscanf(line, "CheckUpdatesOnStartup=%d", &flag) == 1) {
            app->m_checkUpdatesOnStartup = flag != 0;
        } else if (std::sscanf(line, "SkipUpdateVersion=%63s", text) == 1) {
            app->m_skippedUpdateVersion = text;
        }
    };
    uiSettings.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler,
                               ImGuiTextBuffer* out) {
        auto* app = static_cast<Application*>(handler->UserData);
        out->appendf("[%s][Settings]\n", handler->TypeName);
        out->appendf("Zoom=%.3f\n", static_cast<double>(app->m_uiZoom));
        out->appendf("FollowSystemDpi=%d\n", app->m_followSystemDpi ? 1 : 0);
        out->appendf("CheckUpdatesOnStartup=%d\n", app->m_checkUpdatesOnStartup ? 1 : 0);
        if (!app->m_skippedUpdateVersion.empty()) {
            out->appendf("SkipUpdateVersion=%s\n", app->m_skippedUpdateVersion.c_str());
        }
        out->appendf("\n");
    };
    ImGui::AddSettingsHandler(&uiSettings);

    ApplyStyle();

    if (!ImGui_ImplGlfw_InitForOpenGL(m_window, true)) {
        LogError("Failed to initialise the ImGui GLFW backend.");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        LogError("Failed to initialise the ImGui OpenGL backend.");
        return false;
    }

    if (!m_renderer.Initialize()) {
        LogError("Renderer initialisation failed.");
        return false;
    }

    InitFileDialogs();
    m_pose.SetModel(&m_model);

    glfwShowWindow(m_window);
    if (!Log::Get().FilePath().empty()) {
        LogInfo("Session log: %s", fs::absolute(Log::Get().FilePath()).string().c_str());
    }
    LogInfo("Ready. File > Import base model... to begin.");
    return true;
}

void Application::Shutdown() {
    m_gpu.Destroy();
    m_renderer.Destroy();

    ShutdownFileDialogs();

    if (ImGui::GetCurrentContext()) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}

void Application::Frame(float deltaSeconds) {
    // ------------------------------------------------------------- playback
    const Animation* animation = CurrentAnimation();
    if (m_playing && animation && animation->duration > 0.0f) {
        m_playhead += deltaSeconds * m_playbackSpeed;
        if (m_playhead > animation->duration) {
            if (m_loop) {
                m_playhead = std::fmod(m_playhead, animation->duration);
            } else {
                m_playhead = animation->duration;
                m_playing = false;
            }
        } else if (m_playhead < 0.0f) {
            m_playhead = m_loop ? animation->duration : 0.0f;
        }
    }

    // Track the scaling of whichever monitor the window currently sits on, and apply
    // any scale change between frames rather than mid-frame.
    float contentScaleX = 1.0f;
    float contentScaleY = 1.0f;
    glfwGetWindowContentScale(m_window, &contentScaleX, &contentScaleY);
    m_dpiScale = std::max(contentScaleX, 0.25f);
    UpdateUiScale();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // NewFrame is where ImGui reads the ini back, so the preference below is only
    // trustworthy from here on - hence the check waiting until after the first one.
    if (!m_startupCheckDone) {
        m_startupCheckDone = true;
        if (m_checkUpdatesOnStartup) CheckForUpdates(false);
    }
    PollUpdater();

    DrawUi();

    ImGui::Render();

    // The UI can have replaced the whole model or deleted a clip while it ran
    // (importing a base model, Delete on a clip), which frees the animation the
    // playback block above was looking at. Anything held across DrawUi has to be
    // fetched again rather than reused.
    animation = CurrentAnimation();

    // The viewport texture is sampled while ImGui's draw data is replayed, so
    // rendering it here keeps the image in sync with this frame's UI layout.
    if (m_viewportWidth > 0 && m_viewportHeight > 0) {
        m_renderer.Resize(m_viewportWidth, m_viewportHeight);
        m_pose.Evaluate(m_bindPose ? nullptr : animation, m_playhead);
        m_renderer.Render(m_model, m_gpu, m_pose, m_camera, m_renderSettings);
    }

    int displayWidth = 0;
    int displayHeight = 0;
    glfwGetFramebufferSize(m_window, &displayWidth, &displayHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, displayWidth, displayHeight);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(m_window);
}

const Animation* Application::CurrentAnimation() const {
    if (m_currentAnimation < 0 || m_currentAnimation >= static_cast<int>(m_model.animations.size())) {
        return nullptr;
    }
    return &m_model.animations[static_cast<size_t>(m_currentAnimation)];
}

void Application::SelectAnimation(int index) {
    m_currentAnimation = index;
    m_playhead = 0.0f;
    m_pose.InvalidateBinding();
}

void Application::ResetPlayback() {
    m_playhead = 0.0f;
    m_playing = false;
    m_pose.InvalidateBinding();
}

void Application::FrameCamera() {
    glm::vec3 min;
    glm::vec3 max;
    m_model.ComputeBounds(min, max);
    m_camera.FrameBounds(min, max);
}

void Application::RequestAction(PendingAction action) {
    m_pendingAction = action;
    // Clips merged onto the rig live nowhere but in memory, so replacing the model
    // or closing the window throws them away for good.
    if (m_model.Valid() && m_unsavedChanges) {
        m_openDiscardPopup = true;
        return;
    }
    RunPendingAction();
}

void Application::RunPendingAction() {
    const PendingAction action = m_pendingAction;
    m_pendingAction = PendingAction::None;
    switch (action) {
        case PendingAction::ImportBaseModel: ImportBaseModel(); break;
        case PendingAction::Quit:            glfwSetWindowShouldClose(m_window, GLFW_TRUE); break;
        case PendingAction::None:            break;
    }
}

void Application::ImportBaseModel() {
    const std::string path = OpenFileDialog(kFbxFilters, m_lastDirectory);
    if (path.empty()) return;
    m_lastDirectory = fs::path(path).parent_path().string();

    Model imported;
    ImportOptions options = m_importOptions;
    options.importGeometry = true;
    options.importAnimations = true;

    const ImportResult result = ImportFbx(path, options, imported);
    if (!result.ok) {
        LogError("Import failed: %s", result.error.c_str());
        m_statusText = "Import failed - see the log.";
        return;
    }

    m_model = std::move(imported);
    m_gpu.Upload(m_model);
    m_pose.SetModel(&m_model);
    m_currentAnimation = m_model.animations.empty() ? -1 : 0;
    ResetPlayback();
    FrameCamera();
    m_hasMergeReport = false;
    m_unsavedChanges = false;

    LogSuccess("Loaded '%s': %zu nodes, %zu meshes, %zu bones, %zu clips, %zu tris (source unit %.4g m).",
               fs::path(path).filename().string().c_str(), result.nodeCount, result.meshCount,
               result.boneCount, result.animationCount, m_model.totalTriangles,
               static_cast<double>(result.sourceUnitMeters));
    m_statusText = fs::path(path).filename().string();
}

void Application::ImportAnimationFiles() {
    if (!m_model.Valid()) {
        LogWarn("Load a base model before importing animations.");
        return;
    }

    const std::vector<std::string> paths = OpenFilesDialog(kFbxFilters, m_lastDirectory);
    if (paths.empty()) return;
    m_lastDirectory = fs::path(paths.front()).parent_path().string();

    for (const std::string& path : paths) ImportAnimationsFrom(path);
}

void Application::ImportAnimationsFrom(const std::string& path) {
    Model source;
    ImportOptions options = m_importOptions;
    options.importGeometry = false;
    options.importAnimations = true;

    const ImportResult result = ImportFbx(path, options, source);
    if (!result.ok) {
        LogError("Import failed for '%s': %s", path.c_str(), result.error.c_str());
        return;
    }
    if (source.animations.empty()) {
        LogWarn("'%s' contains no animation stacks.", fs::path(path).filename().string().c_str());
        return;
    }

    const float compatibility = EstimateCompatibility(m_model, source, m_mergeOptions);
    const MergeReport report = MergeAnimations(m_model, source, m_mergeOptions);

    m_lastMergeReport = report;
    m_hasMergeReport = true;
    m_pose.InvalidateBinding();
    if (report.animationsAdded > 0) m_unsavedChanges = true;

    if (report.animationsAdded == 0) {
        LogError("'%s': nothing merged (%.0f%% of animated nodes matched the base rig).",
                 fs::path(path).filename().string().c_str(), compatibility * 100.0f);
        return;
    }

    LogSuccess("'%s': merged %d clip(s), %d track(s) bound, %d dropped (%.0f%% rig match).",
               fs::path(path).filename().string().c_str(), report.animationsAdded,
               report.tracksMatched, report.tracksDropped, compatibility * 100.0f);
    if (report.translationChannelsStripped > 0 || report.scaleChannelsStripped > 0) {
        LogInfo("  kept target proportions: stripped %d translation and %d scale channel(s).",
                report.translationChannelsStripped, report.scaleChannelsStripped);
    }
    if (report.rootTracksRetargeted > 0) {
        LogInfo("  retargeted %d root track(s) onto the base rest pose (hip height x%.3f).",
                report.rootTracksRetargeted, static_cast<double>(report.rootMotionScale));
    }

    for (const std::string& name : report.unmatchedNodes) {
        LogWarn("  unmatched node: %s", name.c_str());
    }

    if (m_currentAnimation < 0 && !m_model.animations.empty()) {
        SelectAnimation(static_cast<int>(m_model.animations.size()) - report.animationsAdded);
    }
}

void Application::ApplyMergePolicyToLoadedClips() {
    const MergeReport report = ApplyTrackPolicy(m_model, m_mergeOptions);
    m_pose.InvalidateBinding();

    if (report.animationsAdded == 0) {
        LogInfo("No merged clips to adjust (base-model clips are left untouched).");
        return;
    }
    m_unsavedChanges = true;
    LogSuccess("Re-applied track policy to %d clip(s): %d translation and %d scale channel(s) "
               "stripped, %d root track(s) re-anchored, %d empty track(s) removed.",
               report.animationsAdded, report.translationChannelsStripped,
               report.scaleChannelsStripped, report.rootTracksRetargeted, report.tracksDropped);
}

void Application::DeleteAnimation(int index) {
    if (index < 0 || index >= static_cast<int>(m_model.animations.size())) return;

    m_model.animations.erase(m_model.animations.begin() + index);
    if (m_currentAnimation == index) {
        m_currentAnimation = m_model.animations.empty() ? -1 : std::min(index, static_cast<int>(m_model.animations.size()) - 1);
        m_playhead = 0.0f;
    } else if (m_currentAnimation > index) {
        --m_currentAnimation;
    }
    m_renamingAnimation = -1;
    m_unsavedChanges = true;
    m_pose.InvalidateBinding();
}

void Application::RunExport() {
    if (!m_model.Valid()) {
        LogWarn("Nothing to export.");
        return;
    }

    const char* extension = DefaultExtension(m_exportOptions.format);
    std::string suggested = m_model.sourcePath.empty()
                                ? std::string("merged")
                                : fs::path(m_model.sourcePath).stem().string() + "_merged";
    suggested += ".";
    suggested += extension;

    const std::vector<FileFilter> filters = {{extension, extension}};
    const std::string path = SaveFileDialog(filters, suggested, m_lastDirectory);
    if (path.empty()) return;
    m_lastDirectory = fs::path(path).parent_path().string();

    ExportOptions options = m_exportOptions;
    options.animations.clear();
    for (int i = 0; i < static_cast<int>(m_model.animations.size()); ++i) {
        if (m_model.animations[static_cast<size_t>(i)].exportSelected) options.animations.push_back(i);
    }
    // An empty list means "everything" downstream, so guard the all-deselected case.
    if (options.animations.empty()) options.animations.push_back(-1);

    const ExportResult result = ExportModel(m_model, path, options);
    if (!result.ok) {
        LogError("Export failed: %s", result.error.c_str());
        m_statusText = "Export failed - see the log.";
        return;
    }

    m_unsavedChanges = false;
    LogSuccess("Exported '%s' (%zu mesh part(s), %zu clip(s), scale x%.4g).",
               fs::path(path).filename().string().c_str(), result.meshCount, result.animationCount,
               static_cast<double>(options.scale));
    m_statusText = "Exported " + fs::path(path).filename().string();
}

void Application::CheckForUpdates(bool userInitiated) {
    if (m_updater.Busy()) return;
    m_updater.SetUserInitiated(userInitiated);
    if (userInitiated) {
        // An explicit check means the user wants an answer about *this* version,
        // whatever they dismissed last time.
        m_skippedUpdateVersion.clear();
        ImGui::MarkIniSettingsDirty();
        m_statusText = "Checking for updates...";
    }
    m_updateState = UpdateState::Idle;
    m_updater.CheckAsync();
}

void Application::PollUpdater() {
    const UpdateState state = m_updater.State();
    if (state == m_updateState) return;
    m_updateState = state;

    switch (state) {
        case UpdateState::Available: {
            const ReleaseInfo release = m_updater.Release();
            LogInfo("Version %s is available; this is %s.", release.version.c_str(), kAppVersion);
            // A silent start-up check stays out of the way for a version the user
            // already said no to. Anything newer than that gets to ask again.
            if (CompareVersions(release.version, m_skippedUpdateVersion) > 0) {
                m_openUpdatePopup = true;
            }
            m_statusText = "Version " + release.version + " is available.";
            break;
        }
        case UpdateState::UpToDate:
            if (m_updater.UserInitiated()) {
                LogSuccess("Up to date - %s is the newest release.", kAppVersion);
                m_statusText = "Up to date.";
            }
            break;
        case UpdateState::ReadyToInstall:
            LogSuccess("Update downloaded.");
            m_openUpdatePopup = true;
            break;
        case UpdateState::Failed:
            // A machine that is simply offline should not open with an error box.
            if (m_updater.UserInitiated()) {
                LogError("Update failed: %s", m_updater.Error().c_str());
                m_statusText = "Update failed - see the log.";
            } else {
                LogInfo("Update check skipped: %s", m_updater.Error().c_str());
            }
            break;
        default:
            break;
    }
}

}  // namespace fam
