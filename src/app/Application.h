#pragma once

#include <string>
#include <vector>

#include "core/AnimMerge.h"
#include "core/Export.h"
#include "core/FbxImport.h"
#include "core/Model.h"
#include "core/Pose.h"
#include "render/Camera.h"
#include "render/GpuModel.h"
#include "render/Renderer.h"
#include "update/Updater.h"

struct GLFWwindow;

namespace fam {

class Application {
public:
    int Run();

private:
    // ------------------------------------------------------------- lifecycle
    bool Initialize();
    void Shutdown();
    void Frame(float deltaSeconds);

    // -------------------------------------------------------------- actions
    // Anything that would throw away merged clips goes through here first, so the
    // user gets a chance to export before the work disappears.
    enum class PendingAction { None, ImportBaseModel, Quit };
    void RequestAction(PendingAction action);
    void RunPendingAction();

    void ImportBaseModel();
    void ImportAnimationFiles();
    void ImportAnimationsFrom(const std::string& path);
    void RunExport();
    void ApplyMergePolicyToLoadedClips();
    void DeleteAnimation(int index);
    void SelectAnimation(int index);
    void ResetPlayback();
    void FrameCamera();

    // -------------------------------------------------------------- updates
    void CheckForUpdates(bool userInitiated);
    // Turns the worker thread's state changes into log lines and popups. Called
    // once per frame; returns immediately while a check is still in flight.
    void PollUpdater();

    const Animation* CurrentAnimation() const;

    // -------------------------------------------------------------------- UI
    void DrawUi();
    void DrawDockSpace();
    void DrawMenuBar();
    void DrawViewport();
    void DrawScenePanel();
    void DrawAnimationPanel();
    void DrawTimeline();
    void DrawSettingsPanel();
    void DrawLogPanel();
    void DrawExportPopup();
    void DrawDiscardPopup();
    void DrawUpdatePopup();
    void ApplyStyle();

    // Interface scaling
    void UpdateUiScale();
    void SetUiZoom(float zoom);
    float EffectiveUiScale() const;

    GLFWwindow* m_window = nullptr;

    Model m_model;
    GpuModel m_gpu;
    PoseEvaluator m_pose;
    Renderer m_renderer;
    OrbitCamera m_camera;
    RenderSettings m_renderSettings;

    ImportOptions m_importOptions;
    MergeOptions m_mergeOptions;
    ExportOptions m_exportOptions;

    // Playback
    int m_currentAnimation = -1;
    float m_playhead = 0.0f;
    float m_playbackSpeed = 1.0f;
    bool m_playing = false;
    bool m_loop = true;
    bool m_bindPose = false;

    // UI state
    int m_renamingAnimation = -1;
    int m_selectedLogLine = -1;
    char m_renameBuffer[128] = {};
    bool m_openExportPopup = false;
    bool m_layoutInitialized = false;
    bool m_resetLayout = false;

    // Merged clips only exist in memory until they are exported.
    bool m_unsavedChanges = false;
    PendingAction m_pendingAction = PendingAction::None;
    bool m_openDiscardPopup = false;

    // Updates. The check runs on a worker thread; PollUpdater watches for the
    // state it lands on, which is why the previous one is kept here.
    Updater m_updater;
    UpdateState m_updateState = UpdateState::Idle;
    bool m_checkUpdatesOnStartup = true;
    bool m_startupCheckDone = false;
    bool m_openUpdatePopup = false;
    // A version the user dismissed; suppresses the popup until a newer one appears.
    std::string m_skippedUpdateVersion;

    // Windows reports its display scaling through the window content scale. Without
    // honouring it the whole interface renders 1:1 in pixels and is physically half
    // the intended size on a 200% display.
    float m_dpiScale = 1.0f;         // reported by the monitor the window is on
    float m_uiZoom = 1.0f;           // user multiplier on top of that
    bool m_followSystemDpi = true;
    float m_appliedUiScale = 0.0f;   // last value pushed into the style
    bool m_showAbout = false;
    std::string m_lastDirectory;
    std::string m_statusText = "Import an FBX model to get started.";
    float m_frameMilliseconds = 0.0f;
    int m_viewportWidth = 1;
    int m_viewportHeight = 1;

    MergeReport m_lastMergeReport;
    bool m_hasMergeReport = false;
};

}  // namespace fam
