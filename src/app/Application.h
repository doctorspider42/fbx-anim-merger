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
    void ImportBaseModel();
    void ImportAnimationFiles();
    void ImportAnimationsFrom(const std::string& path);
    void RunExport();
    void ApplyMergePolicyToLoadedClips();
    void DeleteAnimation(int index);
    void SelectAnimation(int index);
    void ResetPlayback();
    void FrameCamera();

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
    void ApplyStyle();

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
    char m_renameBuffer[128] = {};
    bool m_openExportPopup = false;
    bool m_layoutInitialized = false;
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
