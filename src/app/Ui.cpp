// All ImGui panel code lives here to keep Application.cpp about behaviour.
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>

#include <imgui.h>
#include <imgui_internal.h>

#ifndef GLFW_INCLUDE_NONE  // also set on the command line by the imgui target
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include "app/Application.h"
#include "util/Log.h"
#include "util/Version.h"

namespace fs = std::filesystem;

namespace fam {
namespace {

constexpr const char* kViewportWindow = "Viewport";
constexpr const char* kSceneWindow = "Scene";
constexpr const char* kAnimationWindow = "Animations";
constexpr const char* kTimelineWindow = "Timeline";
constexpr const char* kSettingsWindow = "Settings";
constexpr const char* kLogWindow = "Log";

// The unscaled reference style. Every scale change re-derives from this rather than
// scaling the live style, which would compound on each adjustment.
ImGuiStyle g_baseStyle;

ImVec4 LevelColor(LogLevel level) {
    switch (level) {
        case LogLevel::Success: return ImVec4(0.45f, 0.85f, 0.50f, 1.0f);
        case LogLevel::Warning: return ImVec4(0.98f, 0.75f, 0.30f, 1.0f);
        case LogLevel::Error:   return ImVec4(0.95f, 0.42f, 0.42f, 1.0f);
        default:                return ImVec4(0.78f, 0.80f, 0.84f, 1.0f);
    }
}

void HelpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Modals default to wherever the cursor left them; centre them on the viewport.
void CenterNextPopup() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

std::string FormatDuration(const Animation& animation) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2fs / %d f",
                  static_cast<double>(animation.duration), animation.FrameCount());
    return buffer;
}

}  // namespace

void Application::ApplyStyle() {
    ImGuiIO& io = ImGui::GetIO();

    const char* fontCandidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/SFNS.ttf",
    };
    for (const char* candidate : fontCandidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec)) {
            io.Fonts->AddFontFromFileTTF(candidate, 17.0f);
            break;
        }
    }

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();

    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;
    style.SeparatorTextBorderSize = 2.0f;

    ImVec4* colors = style.Colors;
    const ImVec4 accent(0.29f, 0.56f, 0.94f, 1.0f);
    const ImVec4 accentDim(0.29f, 0.56f, 0.94f, 0.55f);

    colors[ImGuiCol_WindowBg]            = ImVec4(0.086f, 0.090f, 0.105f, 1.00f);
    colors[ImGuiCol_ChildBg]             = ImVec4(0.098f, 0.102f, 0.118f, 1.00f);
    colors[ImGuiCol_PopupBg]             = ImVec4(0.118f, 0.125f, 0.145f, 0.98f);
    colors[ImGuiCol_Border]              = ImVec4(0.20f, 0.21f, 0.24f, 0.60f);
    colors[ImGuiCol_FrameBg]             = ImVec4(0.157f, 0.165f, 0.192f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]      = ImVec4(0.204f, 0.216f, 0.251f, 1.00f);
    colors[ImGuiCol_FrameBgActive]       = ImVec4(0.243f, 0.259f, 0.302f, 1.00f);
    colors[ImGuiCol_TitleBg]             = ImVec4(0.071f, 0.075f, 0.086f, 1.00f);
    colors[ImGuiCol_TitleBgActive]       = ImVec4(0.098f, 0.106f, 0.125f, 1.00f);
    colors[ImGuiCol_MenuBarBg]           = ImVec4(0.071f, 0.075f, 0.086f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]         = ImVec4(0.071f, 0.075f, 0.086f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]       = ImVec4(0.24f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.31f, 0.33f, 0.38f, 1.00f);
    colors[ImGuiCol_CheckMark]           = accent;
    colors[ImGuiCol_SliderGrab]          = accent;
    colors[ImGuiCol_SliderGrabActive]    = ImVec4(0.40f, 0.66f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]              = ImVec4(0.180f, 0.192f, 0.227f, 1.00f);
    colors[ImGuiCol_ButtonHovered]       = ImVec4(0.239f, 0.263f, 0.318f, 1.00f);
    colors[ImGuiCol_ButtonActive]        = accentDim;
    colors[ImGuiCol_Header]              = ImVec4(0.20f, 0.24f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderHovered]       = ImVec4(0.24f, 0.31f, 0.43f, 1.00f);
    colors[ImGuiCol_HeaderActive]        = accentDim;
    colors[ImGuiCol_Separator]           = ImVec4(0.19f, 0.20f, 0.23f, 1.00f);
    colors[ImGuiCol_Tab]                 = ImVec4(0.106f, 0.114f, 0.133f, 1.00f);
    colors[ImGuiCol_TabHovered]          = ImVec4(0.22f, 0.29f, 0.40f, 1.00f);
    colors[ImGuiCol_TabSelected]         = ImVec4(0.157f, 0.176f, 0.216f, 1.00f);
    colors[ImGuiCol_TabDimmed]           = ImVec4(0.086f, 0.090f, 0.105f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]   = ImVec4(0.129f, 0.141f, 0.169f, 1.00f);
    colors[ImGuiCol_DockingPreview]      = accentDim;
    colors[ImGuiCol_TableHeaderBg]       = ImVec4(0.129f, 0.137f, 0.161f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt]       = ImVec4(1.00f, 1.00f, 1.00f, 0.020f);
    colors[ImGuiCol_TextSelectedBg]      = accentDim;
    colors[ImGuiCol_Text]                = ImVec4(0.878f, 0.890f, 0.910f, 1.00f);
    colors[ImGuiCol_TextDisabled]        = ImVec4(0.478f, 0.494f, 0.529f, 1.00f);

    g_baseStyle = style;
}

float Application::EffectiveUiScale() const {
    const float base = m_followSystemDpi ? m_dpiScale : 1.0f;
    return std::clamp(base * m_uiZoom, 0.5f, 4.0f);
}

void Application::SetUiZoom(float zoom) {
    m_uiZoom = std::clamp(zoom, 0.5f, 3.0f);
    ImGui::MarkIniSettingsDirty();
}

void Application::UpdateUiScale() {
    const float scale = EffectiveUiScale();
    if (std::fabs(scale - m_appliedUiScale) < 0.001f) return;

    // ImGui 1.92 rasterises fonts on demand, so changing the scale mid-session needs
    // no atlas rebuild: FontScaleMain covers text, ScaleAllSizes covers the metrics.
    ImGuiStyle& style = ImGui::GetStyle();
    style = g_baseStyle;
    style.ScaleAllSizes(scale);
    style.FontScaleMain = scale;

    const bool first = m_appliedUiScale == 0.0f;
    m_appliedUiScale = scale;
    if (!first) {
        LogInfo("Interface scale %.2fx (display %.0f%%, zoom %.2fx).", scale, m_dpiScale * 100.0f,
                m_uiZoom);
    }
}

void Application::DrawUi() {
    DrawDockSpace();
    DrawViewport();
    DrawScenePanel();
    DrawAnimationPanel();
    DrawTimeline();
    DrawSettingsPanel();
    DrawLogPanel();
    DrawExportPopup();
    DrawDiscardPopup();
    DrawUpdatePopup();

    if (m_showAbout) {
        ImGui::OpenPopup("About##dialog");
        m_showAbout = false;
    }
    CenterNextPopup();
    ImGui::SetNextWindowSizeConstraints(ImVec2(380.0f, 0.0f), ImVec2(460.0f, FLT_MAX));
    if (ImGui::BeginPopupModal("About##dialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SeparatorText("FBX Animation Merger");
        ImGui::Text("Version %s", kAppVersion);
        ImGui::SameLine();
        ImGui::TextDisabled("(%s, %s build)", kAppCommit, IsPortableBuild() ? "portable" : "installed");
        ImGui::Spacing();
        ImGui::TextUnformatted("Merge animation clips from separate FBX files onto one rig,\n"
                               "preview them, rename them, export to FBX or glTF/GLB.");
        ImGui::Spacing();
        ImGui::SeparatorText("Third-party components");
        ImGui::BulletText("ufbx - MIT");
        ImGui::BulletText("Assimp - BSD 3-Clause");
        ImGui::BulletText("Dear ImGui - MIT");
        ImGui::BulletText("GLFW - zlib/libpng");
        ImGui::BulletText("GLM - MIT");
        ImGui::BulletText("nativefiledialog-extended - zlib");
        ImGui::BulletText("stb_image - MIT / public domain");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Application::DrawDockSpace() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##dockhost", nullptr, flags);
    ImGui::PopStyleVar(2);

    const ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");

    // Build the default layout on a fresh profile, or whenever the user asks for it
    // back. An existing .ini already carries a node, so a saved layout is preserved.
    const bool firstRun = !m_layoutInitialized && ImGui::DockBuilderGetNode(dockspaceId) == nullptr;
    if (firstRun || m_resetLayout) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID center = dockspaceId;
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);
        const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
        ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);
        const ImGuiID bottomRight = ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.42f, nullptr, &bottom);

        ImGui::DockBuilderDockWindow(kViewportWindow, center);
        // Same node, so these two come up as tabs on top of each other.
        ImGui::DockBuilderDockWindow(kAnimationWindow, left);
        ImGui::DockBuilderDockWindow(kSceneWindow, left);
        ImGui::DockBuilderDockWindow(kSettingsWindow, right);
        ImGui::DockBuilderDockWindow(kTimelineWindow, bottom);
        ImGui::DockBuilderDockWindow(kLogWindow, bottomRight);
        ImGui::DockBuilderFinish(dockspaceId);
    }
    m_layoutInitialized = true;
    m_resetLayout = false;

    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    DrawMenuBar();
    ImGui::End();
}

void Application::DrawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Import base model...", "Ctrl+O")) {
            RequestAction(PendingAction::ImportBaseModel);
        }
        if (ImGui::MenuItem("Import animations...", "Ctrl+I", false, m_model.Valid())) {
            ImportAnimationFiles();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Export...", "Ctrl+E", false, m_model.Valid())) m_openExportPopup = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) RequestAction(PendingAction::Quit);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Mesh", nullptr, &m_renderSettings.showMesh);
        ImGui::MenuItem("Wireframe", nullptr, &m_renderSettings.wireframe);
        ImGui::MenuItem("Grid", nullptr, &m_renderSettings.showGrid);
        ImGui::MenuItem("Skeleton", nullptr, &m_renderSettings.showSkeleton);
        ImGui::MenuItem("Bind pose", nullptr, &m_bindPose);
        ImGui::Separator();
        if (ImGui::MenuItem("Frame model", "F", false, m_model.Valid())) FrameCamera();
        if (ImGui::MenuItem("Reset panel layout")) m_resetLayout = true;

        ImGui::Separator();
        if (ImGui::MenuItem("Zoom in", "Ctrl+=")) SetUiZoom(m_uiZoom * 1.1f);
        if (ImGui::MenuItem("Zoom out", "Ctrl+-")) SetUiZoom(m_uiZoom / 1.1f);
        if (ImGui::MenuItem("Reset zoom", "Ctrl+0")) SetUiZoom(1.0f);
        if (ImGui::MenuItem("Follow system DPI", nullptr, &m_followSystemDpi)) {
            ImGui::MarkIniSettingsDirty();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        const bool busy = m_updater.Busy();
        if (ImGui::MenuItem(busy ? "Checking for updates..." : "Check for updates...", nullptr,
                            false, !busy)) {
            CheckForUpdates(true);
        }
        if (m_updateState == UpdateState::Available || m_updateState == UpdateState::ReadyToInstall) {
            if (ImGui::MenuItem("Update available...")) m_openUpdatePopup = true;
        }
        if (ImGui::MenuItem("Releases on GitHub")) OpenInBrowser(kAppReleasesUrl);
        ImGui::Separator();
        if (ImGui::MenuItem("About")) m_showAbout = true;
        ImGui::EndMenu();
    }

    // Right-aligned status strip.
    const float fps = m_frameMilliseconds > 0.0f ? 1000.0f / m_frameMilliseconds : 0.0f;
    char status[256];
    std::snprintf(status, sizeof(status), "%s   |   %zu tris  %zu draws   |   %.1f ms (%.0f fps)",
                  m_statusText.c_str(), m_renderer.LastTriangles(), m_renderer.LastDrawCalls(),
                  static_cast<double>(m_frameMilliseconds), static_cast<double>(fps));
    const float width = ImGui::CalcTextSize(status).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - width - 16.0f);
    ImGui::TextDisabled("%s", status);

    ImGui::EndMenuBar();

    // Shortcuts (menu bar is always present, so this is a convenient home).
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        RequestAction(PendingAction::ImportBaseModel);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_I, false) && m_model.Valid()) ImportAnimationFiles();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E, false) && m_model.Valid()) m_openExportPopup = true;
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space, false)) m_playing = !m_playing;
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false) && m_model.Valid()) FrameCamera();

    if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Equal, false) ||
                       ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false))) {
        SetUiZoom(m_uiZoom * 1.1f);
    }
    if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Minus, false) ||
                       ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false))) {
        SetUiZoom(m_uiZoom / 1.1f);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0, false)) SetUiZoom(1.0f);
}

void Application::DrawViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(kViewportWindow, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    const ImVec2 size = ImGui::GetContentRegionAvail();
    m_viewportWidth = std::max(1, static_cast<int>(size.x));
    m_viewportHeight = std::max(1, static_cast<int>(size.y));

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##viewport-input", ImVec2(std::max(size.x, 1.0f), std::max(size.y, 1.0f)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (m_renderer.ColorTexture() != 0) {
        // The framebuffer is bottom-up, so V is flipped.
        drawList->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_renderer.ColorTexture())),
                           origin, ImVec2(origin.x + size.x, origin.y + size.y), ImVec2(0.0f, 1.0f),
                           ImVec2(1.0f, 0.0f));
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemActive()) {
        const ImVec2 delta = io.MouseDelta;
        const bool pan = io.MouseDown[ImGuiMouseButton_Middle] ||
                         (io.MouseDown[ImGuiMouseButton_Left] && io.KeyShift) ||
                         io.MouseDown[ImGuiMouseButton_Right];
        if (pan) {
            m_camera.Pan(delta.x, delta.y);
        } else if (io.MouseDown[ImGuiMouseButton_Left]) {
            m_camera.Orbit(-delta.x * 0.008f, delta.y * 0.008f);
        }
    }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
        m_camera.Dolly(io.MouseWheel);
    }

    // Overlay
    if (!m_model.Valid()) {
        const char* hint = "Drop in a base FBX via  File > Import base model...";
        const ImVec2 textSize = ImGui::CalcTextSize(hint);
        drawList->AddText(ImVec2(origin.x + (size.x - textSize.x) * 0.5f,
                                 origin.y + (size.y - textSize.y) * 0.5f),
                          IM_COL32(150, 155, 165, 255), hint);
    } else {
        const Animation* animation = CurrentAnimation();
        char overlay[192];
        std::snprintf(overlay, sizeof(overlay), "%s   %.2f s",
                      m_bindPose ? "Bind pose" : (animation ? animation->name.c_str() : "No clip"),
                      static_cast<double>(m_playhead));
        drawList->AddRectFilled(ImVec2(origin.x + 10.0f, origin.y + 10.0f),
                                ImVec2(origin.x + 22.0f + ImGui::CalcTextSize(overlay).x,
                                       origin.y + 20.0f + ImGui::GetTextLineHeight()),
                                IM_COL32(0, 0, 0, 110), 4.0f);
        drawList->AddText(ImVec2(origin.x + 16.0f, origin.y + 15.0f), IM_COL32(230, 233, 240, 220),
                          overlay);
    }

    ImGui::End();
}

void Application::DrawScenePanel() {
    ImGui::Begin(kSceneWindow);

    if (!m_model.Valid()) {
        ImGui::TextDisabled("No model loaded.");
        ImGui::Spacing();
        if (ImGui::Button("Import base model...", ImVec2(-FLT_MIN, 0))) {
            RequestAction(PendingAction::ImportBaseModel);
        }
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Source");
    ImGui::TextWrapped("%s", fs::path(m_model.sourcePath).filename().string().c_str());
    ImGui::TextDisabled("%s", fs::path(m_model.sourcePath).parent_path().string().c_str());

    ImGui::SeparatorText("Statistics");
    if (ImGui::BeginTable("##stats", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto row = [](const char* label, const std::string& value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", label);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value.c_str());
        };
        row("Nodes", std::to_string(m_model.nodes.size()));
        row("Meshes", std::to_string(m_model.meshes.size()));
        row("Triangles", std::to_string(m_model.totalTriangles));
        row("Vertices", std::to_string(m_model.totalVertices));
        row("Bones", std::to_string(m_model.skeleton.bones.size()));
        row("Materials", std::to_string(m_model.materials.size()));

        std::set<std::string> textureKeys;
        int embeddedCount = 0;
        for (const Material& material : m_model.materials) {
            for (const TextureSource* source : {&material.baseColorTexture, &material.normalTexture}) {
                if (source->Empty()) continue;
                if (textureKeys.insert(source->Key()).second && source->Embedded()) ++embeddedCount;
            }
        }
        row("Textures", textureKeys.empty()
                            ? std::string("none")
                            : std::to_string(textureKeys.size()) + " (" +
                                  std::to_string(embeddedCount) + " embedded)");

        row("Clips", std::to_string(m_model.animations.size()));
        ImGui::EndTable();
    }

    if (m_model.skeleton.bones.size() > static_cast<size_t>(kMaxBones)) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(LogLevel::Warning));
        ImGui::TextWrapped("Skeleton exceeds the %d-bone preview limit; export is unaffected.", kMaxBones);
        ImGui::PopStyleColor();
    }

    if (m_hasMergeReport) {
        ImGui::SeparatorText("Last merge");
        ImGui::Text("%d clip(s), %d track(s) bound", m_lastMergeReport.animationsAdded,
                    m_lastMergeReport.tracksMatched);
        if (m_lastMergeReport.tracksDropped > 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(LogLevel::Warning));
            ImGui::Text("%d track(s) dropped", m_lastMergeReport.tracksDropped);
            ImGui::PopStyleColor();
        }
        if (!m_lastMergeReport.unmatchedNodes.empty() &&
            ImGui::TreeNode("Unmatched nodes")) {
            for (const std::string& name : m_lastMergeReport.unmatchedNodes) {
                ImGui::BulletText("%s", name.c_str());
            }
            ImGui::TreePop();
        }
    }

    ImGui::End();
}

void Application::DrawAnimationPanel() {
    ImGui::Begin(kAnimationWindow);

    const bool hasModel = m_model.Valid();
    ImGui::BeginDisabled(!hasModel);
    if (ImGui::Button("Import animations...")) ImportAnimationFiles();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_model.animations.empty());
    if (ImGui::Button("Export...")) m_openExportPopup = true;
    ImGui::EndDisabled();

    if (m_model.animations.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled(hasModel ? "No clips yet. Import FBX files containing takes."
                                     : "Load a base model first.");
        ImGui::End();
        return;
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("All")) {
        for (Animation& anim : m_model.animations) anim.exportSelected = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) {
        for (Animation& anim : m_model.animations) anim.exportSelected = false;
    }
    HelpMarker("Checkbox column selects which clips are written on export.");

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

    int deleteRequest = -1;

    if (ImGui::BeginTable("##clips", 3, flags, ImVec2(0.0f, -1.0f))) {
        ImGui::TableSetupColumn("##export", ImGuiTableColumnFlags_WidthFixed, 26.0f);
        ImGui::TableSetupColumn("Clip", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(m_model.animations.size()); ++i) {
            Animation& anim = m_model.animations[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Checkbox("##sel", &anim.exportSelected);

            ImGui::TableNextColumn();
            if (m_renamingAnimation == i) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) {
                    ImGui::SetKeyboardFocusHere();
                }
                if (ImGui::InputText("##rename", m_renameBuffer, sizeof(m_renameBuffer),
                                     ImGuiInputTextFlags_EnterReturnsTrue |
                                         ImGuiInputTextFlags_AutoSelectAll)) {
                    anim.name = m_model.MakeUniqueAnimationName(m_renameBuffer, i);
                    m_renamingAnimation = -1;
                    m_unsavedChanges = true;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_renamingAnimation = -1;
            } else {
                const bool selected = (i == m_currentAnimation);
                if (ImGui::Selectable(anim.name.c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns |
                                          ImGuiSelectableFlags_AllowOverlap)) {
                    SelectAnimation(i);
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    m_renamingAnimation = i;
                    std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", anim.name.c_str());
                }
                if (ImGui::BeginItemTooltip()) {
                    ImGui::Text("%s", anim.name.c_str());
                    ImGui::TextDisabled("%zu tracks, %.1f fps", anim.tracks.size(),
                                        static_cast<double>(anim.sampleRate));
                    if (!anim.sourceFile.empty()) {
                        ImGui::TextDisabled("%s", fs::path(anim.sourceFile).filename().string().c_str());
                    }
                    ImGui::EndTooltip();
                }
                if (ImGui::BeginPopupContextItem("##ctx")) {
                    if (ImGui::MenuItem("Preview")) SelectAnimation(i);
                    if (ImGui::MenuItem("Rename")) {
                        m_renamingAnimation = i;
                        std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", anim.name.c_str());
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete")) deleteRequest = i;
                    ImGui::EndPopup();
                }
            }

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", FormatDuration(anim).c_str());

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (deleteRequest >= 0) DeleteAnimation(deleteRequest);

    ImGui::End();
}

void Application::DrawTimeline() {
    ImGui::Begin(kTimelineWindow);

    Animation* animation =
        m_currentAnimation >= 0 && m_currentAnimation < static_cast<int>(m_model.animations.size())
            ? &m_model.animations[static_cast<size_t>(m_currentAnimation)]
            : nullptr;
    const bool active = animation != nullptr && !m_bindPose;

    ImGui::BeginDisabled(!active);

    if (ImGui::Button("|<")) m_playhead = 0.0f;
    ImGui::SameLine();
    if (ImGui::Button(m_playing ? "Pause" : "Play", ImVec2(80, 0))) m_playing = !m_playing;
    ImGui::SameLine();
    if (ImGui::Button(">|") && animation) m_playhead = animation->duration;
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &m_loop);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderFloat("Speed", &m_playbackSpeed, 0.05f, 3.0f, "%.2fx");
    ImGui::SameLine();
    if (ImGui::Button("1x")) m_playbackSpeed = 1.0f;

    ImGui::Spacing();

    const float duration = animation ? std::max(animation->duration, 0.0001f) : 1.0f;
    const float rate = animation ? animation->sampleRate : 30.0f;
    const int frame = static_cast<int>(m_playhead * rate + 0.5f);
    const int frameCount = animation ? animation->FrameCount() : 1;

    ImGui::SetNextItemWidth(-220.0f);
    char label[64];
    std::snprintf(label, sizeof(label), "%.3f s  (frame %d / %d)", static_cast<double>(m_playhead),
                  frame, frameCount - 1);
    if (ImGui::SliderFloat("##playhead", &m_playhead, 0.0f, duration, label)) {
        m_playing = false;
    }

    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(animation == nullptr);
    ImGui::SetNextItemWidth(200.0f);
    float editedRate = rate;
    const bool rateEntered = ImGui::InputFloat("##rate", &editedRate, 1.0f, 10.0f,
                                               "%.0f fps",
                                               ImGuiInputTextFlags_EnterReturnsTrue);
    if ((rateEntered || ImGui::IsItemDeactivatedAfterEdit()) && animation) {
        editedRate = std::clamp(editedRate, kMinAnimationSampleRate, kMaxAnimationSampleRate);
        if (std::fabs(editedRate - animation->sampleRate) > 1.0e-4f) {
            const float oldRate = animation->sampleRate;
            ResampleAnimation(*animation, editedRate);
            m_playhead = std::min(m_playhead, animation->duration);
            m_pose.InvalidateBinding();
            m_unsavedChanges = true;
            LogSuccess("Resampled '%s' from %.3g to %.3g fps.", animation->name.c_str(),
                       static_cast<double>(oldRate), static_cast<double>(animation->sampleRate));
        }
    }
    HelpMarker("Type an FPS value and press Enter (or click elsewhere). The loaded clip is "
               "resampled immediately, so preview and export use the new frame rate.");
    ImGui::EndDisabled();

    if (!active) {
        ImGui::SameLine();
        ImGui::TextDisabled(m_bindPose ? "Bind pose preview" : "Select a clip to preview");
    }

    ImGui::End();
}

void Application::DrawSettingsPanel() {
    ImGui::Begin(kSettingsWindow);

    if (ImGui::CollapsingHeader("Interface", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Checkbox("Follow system DPI", &m_followSystemDpi)) ImGui::MarkIniSettingsDirty();
        ImGui::SameLine();
        ImGui::TextDisabled("(display reports %.0f%%)", m_dpiScale * 100.0f);
        HelpMarker("Windows display scaling. With this off the interface renders one "
                   "interface pixel per screen pixel, which is physically tiny at 150% or 200%.");

        float zoom = m_uiZoom;
        ImGui::SetNextItemWidth(-110.0f);
        if (ImGui::SliderFloat("Zoom", &zoom, 0.5f, 3.0f, "%.2fx")) SetUiZoom(zoom);
        ImGui::SameLine();
        if (ImGui::SmallButton("1x")) SetUiZoom(1.0f);

        ImGui::TextDisabled("Effective scale: %.2fx   (Ctrl +/- , Ctrl+0)", EffectiveUiScale());
    }

    if (ImGui::CollapsingHeader("Updates")) {
        ImGui::Text("Version %s", kAppVersion);
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", IsPortableBuild() ? "portable" : "installed");

        if (ImGui::Checkbox("Check on start-up", &m_checkUpdatesOnStartup)) {
            ImGui::MarkIniSettingsDirty();
        }
        HelpMarker("Asks GitHub once per launch whether a newer release exists. Nothing is "
                   "downloaded until you say so, and no information about you is sent.");

        ImGui::BeginDisabled(m_updater.Busy());
        if (ImGui::Button("Check now", ImVec2(-FLT_MIN, 0.0f))) CheckForUpdates(true);
        ImGui::EndDisabled();

        switch (m_updateState) {
            case UpdateState::Checking:
                ImGui::TextDisabled("Checking...");
                break;
            case UpdateState::UpToDate:
                ImGui::TextDisabled("Up to date.");
                break;
            case UpdateState::Available:
            case UpdateState::Downloading:
            case UpdateState::ReadyToInstall:
                if (ImGui::SmallButton("Show update")) m_openUpdatePopup = true;
                break;
            case UpdateState::Failed:
                ImGui::TextDisabled("Last check failed.");
                break;
            default:
                break;
        }

        if (!m_skippedUpdateVersion.empty()) {
            ImGui::TextDisabled("Skipping %s", m_skippedUpdateVersion.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) {
                m_skippedUpdateVersion.clear();
                ImGui::MarkIniSettingsDirty();
            }
        }
    }

    if (ImGui::CollapsingHeader("Import", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(-110.0f);
        const bool rateEntered = ImGui::InputFloat("Bake rate", &m_importOptions.sampleRate,
                                                   1.0f, 10.0f, "%.0f fps",
                                                   ImGuiInputTextFlags_EnterReturnsTrue);
        if (rateEntered || ImGui::IsItemDeactivatedAfterEdit()) {
            m_importOptions.sampleRate =
                std::clamp(m_importOptions.sampleRate, kMinAnimationSampleRate,
                           kMaxAnimationSampleRate);
        }
        HelpMarker("Curves from every source file are resampled at this rate. Higher is more "
                   "faithful for fast motion, lower keeps files small. This is the default for "
                   "future imports; a loaded clip's FPS can also be edited in the Timeline.");
    }

    if (ImGui::CollapsingHeader("Merging", ImGuiTreeNodeFlags_DefaultOpen)) {
        static const char* kTranslationModes[] = {"Root bone only (keep proportions)",
                                                  "Only if animated", "Copy everything"};
        int translationMode = static_cast<int>(m_mergeOptions.translationMode);
        ImGui::SetNextItemWidth(-110.0f);
        if (ImGui::Combo("Translation", &translationMode, kTranslationModes,
                         IM_ARRAYSIZE(kTranslationModes))) {
            m_mergeOptions.translationMode = static_cast<TranslationMode>(translationMode);
        }
        HelpMarker("A clip's per-bone translation keys are the SOURCE rig's bone lengths. "
                   "Copying them onto a different rig overwrites its rest offsets and stretches "
                   "the mesh. 'Root bone only' takes rotation everywhere and keeps translation "
                   "just on the hips, where the actual motion lives.");

        ImGui::Checkbox("Ignore scale tracks", &m_mergeOptions.ignoreScaleTracks);
        HelpMarker("Character clips almost never animate scale, and a mismatched bind scale "
                   "distorts the mesh the same way stray translation does.");

        ImGui::Checkbox("Retarget root motion", &m_mergeOptions.retargetRootMotion);
        HelpMarker("Root translation is authored at the source rig's hip height, so a clip from "
                   "a taller rig leaves the character hovering. Re-anchors it to the base "
                   "model's rest pose and scales the displacement by the hip-height ratio.");

        ImGui::BeginDisabled(m_model.animations.empty());
        if (ImGui::Button("Apply to loaded clips", ImVec2(-FLT_MIN, 0))) {
            ApplyMergePolicyToLoadedClips();
        }
        ImGui::EndDisabled();
        HelpMarker("Re-applies the two settings above to clips already merged, so you do not "
                   "have to re-import them. Clips that came with the base model are left alone.");

        ImGui::Spacing();
        ImGui::Checkbox("Strip namespace", &m_mergeOptions.stripNamespace);
        HelpMarker("Matches 'mixamorig:Hips' or 'Armature|Hips' against 'Hips'.");
        ImGui::Checkbox("Ignore case", &m_mergeOptions.caseInsensitive);
        ImGui::Checkbox("Skeleton tracks only", &m_mergeOptions.skeletonTracksOnly);
        HelpMarker("Drops tracks targeting nodes that are not skinning bones - helpers, "
                   "mesh nodes, cameras.");

        char prefix[64];
        std::snprintf(prefix, sizeof(prefix), "%s", m_mergeOptions.namePrefix.c_str());
        ImGui::SetNextItemWidth(-110.0f);
        if (ImGui::InputText("Name prefix", prefix, sizeof(prefix))) {
            m_mergeOptions.namePrefix = prefix;
        }
    }

    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Mesh", &m_renderSettings.showMesh);
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &m_renderSettings.showGrid);
        ImGui::Checkbox("Skeleton", &m_renderSettings.showSkeleton);
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_renderSettings.showSkeleton);
        ImGui::Checkbox("X-ray", &m_renderSettings.xray);
        ImGui::EndDisabled();
        ImGui::Checkbox("Wireframe", &m_renderSettings.wireframe);
        ImGui::SameLine();
        ImGui::Checkbox("Cull backfaces", &m_renderSettings.backfaceCulling);
        ImGui::Checkbox("Bind pose", &m_bindPose);

        ImGui::Spacing();
        ImGui::SetNextItemWidth(-110.0f);
        ImGui::ColorEdit3("Background", &m_renderSettings.background.x, ImGuiColorEditFlags_NoInputs);
        ImGui::SetNextItemWidth(-110.0f);
        ImGui::SliderFloat("Light yaw", &m_renderSettings.lightYaw, -3.1416f, 3.1416f, "%.2f");
        ImGui::SetNextItemWidth(-110.0f);
        ImGui::SliderFloat("Light pitch", &m_renderSettings.lightPitch, -1.5f, 1.5f, "%.2f");
        ImGui::SetNextItemWidth(-110.0f);
        ImGui::SliderFloat("Ambient", &m_renderSettings.ambient, 0.0f, 1.5f, "%.2f");
        ImGui::SetNextItemWidth(-110.0f);
        ImGui::SliderFloat("Exposure", &m_renderSettings.exposure, 0.2f, 3.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Camera")) {
        ImGui::SetNextItemWidth(-110.0f);
        ImGui::SliderFloat("FOV", &m_camera.fovDegrees, 15.0f, 90.0f, "%.0f deg");
        if (ImGui::Button("Frame model", ImVec2(-FLT_MIN, 0)) && m_model.Valid()) FrameCamera();
        ImGui::TextDisabled("LMB orbit - MMB/RMB pan - wheel zoom");
    }

    ImGui::End();
}

void Application::DrawLogPanel() {
    ImGui::Begin(kLogWindow);

    const std::deque<LogEntry> entries = Log::Get().Snapshot();

    if (ImGui::SmallButton("Copy all")) ImGui::SetClipboardText(Log::Get().ToText().c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        Log::Get().Clear();
        m_selectedLogLine = -1;
    }
    ImGui::SameLine();
    static bool autoScroll = true;
    ImGui::Checkbox("Auto-scroll", &autoScroll);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Click a line to select it, Ctrl+C copies it.\n"
                               "Right-click for a menu. With nothing selected Ctrl+C takes "
                               "the whole log.");
        if (!Log::Get().FilePath().empty()) {
            ImGui::Separator();
            ImGui::Text("Also written to: %s", Log::Get().FilePath().c_str());
        }
        ImGui::EndTooltip();
    }

    ImGui::Separator();
    ImGui::BeginChild("##logscroll", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);

    if (m_selectedLogLine >= static_cast<int>(entries.size())) m_selectedLogLine = -1;

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(entries.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const LogEntry& entry = entries[static_cast<size_t>(i)];
            ImGui::PushID(i);

            // The Selectable carries an empty label and the text is drawn over it:
            // using the message as the label would let '##' inside a path or a
            // message swallow part of the line.
            const ImVec2 cursor = ImGui::GetCursorPos();
            if (ImGui::Selectable("##line", m_selectedLogLine == i)) m_selectedLogLine = i;

            if (ImGui::BeginPopupContextItem("##logctx")) {
                m_selectedLogLine = i;
                if (ImGui::MenuItem("Copy line")) ImGui::SetClipboardText(entry.text.c_str());
                if (ImGui::MenuItem("Copy all")) {
                    ImGui::SetClipboardText(Log::Get().ToText().c_str());
                }
                ImGui::EndPopup();
            }

            ImGui::SetCursorPos(cursor);
            ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(entry.level));
            ImGui::TextUnformatted(entry.text.c_str());
            ImGui::PopStyleColor();

            ImGui::PopID();
        }
    }
    clipper.End();

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        if (m_selectedLogLine >= 0 && m_selectedLogLine < static_cast<int>(entries.size())) {
            ImGui::SetClipboardText(entries[static_cast<size_t>(m_selectedLogLine)].text.c_str());
        } else {
            ImGui::SetClipboardText(Log::Get().ToText().c_str());
        }
    }

    if (autoScroll && Log::Get().dirty) {
        ImGui::SetScrollHereY(1.0f);
        Log::Get().dirty = false;
    }

    ImGui::EndChild();
    ImGui::End();
}

// Guards the two things that throw the session away: loading another base model over
// this one, and closing the window. Merged clips are not written anywhere until the
// user exports, so both are a one-way door.
void Application::DrawDiscardPopup() {
    if (m_openDiscardPopup) {
        ImGui::OpenPopup("Unsaved clips##discard");
        m_openDiscardPopup = false;
    }

    CenterNextPopup();
    ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 0.0f), ImVec2(500.0f, FLT_MAX));
    if (!ImGui::BeginPopupModal("Unsaved clips##discard", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const bool quitting = m_pendingAction == PendingAction::Quit;
    ImGui::TextWrapped("%s", quitting ? "Closing now loses the merged clips."
                                      : "Loading another base model replaces this one and loses "
                                        "the merged clips.");
    ImGui::Spacing();
    ImGui::TextWrapped("They only exist in memory - nothing is written until you export.");
    ImGui::Spacing();

    if (ImGui::Button("Export first...", ImVec2(150.0f, 0.0f))) {
        m_pendingAction = PendingAction::None;
        ImGui::CloseCurrentPopup();
        m_openExportPopup = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(quitting ? "Close anyway" : "Discard and load", ImVec2(150.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
        RunPendingAction();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_pendingAction = PendingAction::None;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void Application::DrawUpdatePopup() {
    if (m_openUpdatePopup) {
        ImGui::OpenPopup("Update available##update");
        m_openUpdatePopup = false;
    }

    CenterNextPopup();
    ImGui::SetNextWindowSizeConstraints(ImVec2(480.0f, 0.0f), ImVec2(620.0f, FLT_MAX));
    if (!ImGui::BeginPopupModal("Update available##update", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const ReleaseInfo release = m_updater.Release();
    const UpdateState state = m_updater.State();
    const bool portable = IsPortableBuild();

    ImGui::Text("Version %s is available.", release.version.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(this is %s)", kAppVersion);

    if (!release.notes.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("What changed");
        // The notes are whatever the release carries, so they get a fixed box with
        // its own scrollbar rather than being allowed to size the dialog.
        ImGui::BeginChild("##notes", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 8.0f),
                          ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(release.notes.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
    }

    ImGui::Spacing();

    if (state == UpdateState::Downloading) {
        const float progress = m_updater.Progress();
        if (progress < 0.0f) {
            // No content length came back, so there is no fraction to show.
            ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()),
                               ImVec2(-FLT_MIN, 0.0f), "Downloading...");
        } else {
            ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0.0f));
        }
        ImGui::Spacing();
        if (ImGui::Button("Hide", ImVec2(140.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    if (state == UpdateState::ReadyToInstall) {
        ImGui::TextWrapped("The installer is downloaded. Installing closes this window and "
                           "reopens it on the new version.");
        if (InstallNeedsElevation()) {
            ImGui::Spacing();
            ImGui::TextWrapped("This copy is installed for all users, so Windows will ask for "
                               "administrator rights.");
        }
        if (m_unsavedChanges) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.98f, 0.75f, 0.30f, 1.0f),
                               "Export your merged clips first - they are not saved anywhere.");
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(m_unsavedChanges);
        if (ImGui::Button("Install and restart", ImVec2(180.0f, 0.0f))) {
            if (m_updater.LaunchInstaller()) {
                ImGui::CloseCurrentPopup();
                RequestAction(PendingAction::Quit);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Later", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    if (state == UpdateState::Failed) {
        ImGui::TextColored(ImVec4(0.95f, 0.42f, 0.42f, 1.0f), "Update failed: %s",
                           m_updater.Error().c_str());
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    // A portable copy is a folder the user unzipped wherever they liked; there is
    // no installer that owns it, so the honest move is to hand them the download.
    const bool canInstall = !portable && !release.installerUrl.empty();
    if (portable) {
        ImGui::TextWrapped("This is a portable copy, so it cannot update itself. Download the "
                           "new portable zip and unpack it over this folder.");
    } else if (release.installerUrl.empty()) {
        ImGui::TextWrapped("That release ships no installer. Download it from the release page.");
    }

    ImGui::Spacing();
    if (canInstall) {
        if (ImGui::Button("Download and install", ImVec2(180.0f, 0.0f))) {
            // Whatever started the check, the download is the user's own doing, so
            // anything that goes wrong from here is worth reporting loudly.
            m_updater.SetUserInitiated(true);
            m_updater.DownloadAsync();
        }
        ImGui::SameLine();
    } else {
        const std::string& url = portable && !release.portableUrl.empty() ? release.portableUrl
                                                                          : release.pageUrl;
        if (ImGui::Button("Open download page", ImVec2(180.0f, 0.0f))) OpenInBrowser(url);
        ImGui::SameLine();
    }
    if (ImGui::Button("Skip this version", ImVec2(150.0f, 0.0f))) {
        m_skippedUpdateVersion = release.version;
        ImGui::MarkIniSettingsDirty();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Later", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void Application::DrawExportPopup() {
    if (m_openExportPopup) {
        ImGui::OpenPopup("Export##dialog");
        m_openExportPopup = false;
    }

    // AlwaysAutoResize fits the window to its content, and the widgets below ask for
    // "all remaining width" - left alone the two feed each other and the dialog ends
    // up as wide as the display. Constraints bound the width; height still auto-fits.
    CenterNextPopup();
    ImGui::SetNextWindowSizeConstraints(ImVec2(460.0f, 0.0f), ImVec2(520.0f, FLT_MAX));
    if (!ImGui::BeginPopupModal("Export##dialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    static const char* kFormatNames[] = {"FBX (binary 7.4)", "FBX (ASCII)", "glTF 2.0 binary (.glb)",
                                         "glTF 2.0 (.gltf + .bin)"};
    int formatIndex = static_cast<int>(m_exportOptions.format);
    ImGui::SetNextItemWidth(-140.0f);
    if (ImGui::Combo("Format", &formatIndex, kFormatNames, IM_ARRAYSIZE(kFormatNames))) {
        m_exportOptions.format = static_cast<ExportFormat>(formatIndex);
        m_exportOptions.scale = DefaultScaleFor(m_exportOptions.format);
        m_exportOptions.embedTextures = m_exportOptions.format == ExportFormat::Glb;
    }

    ImGui::SetNextItemWidth(-140.0f);
    ImGui::InputFloat("Unit scale", &m_exportOptions.scale, 0.0f, 0.0f, "%.4g");
    HelpMarker("The scene is held internally in metres. FBX conventionally stores centimetres "
               "(x100), glTF mandates metres (x1).");
    ImGui::SameLine();
    if (ImGui::SmallButton("Default")) {
        m_exportOptions.scale = DefaultScaleFor(m_exportOptions.format);
    }

    ImGui::Checkbox("Include geometry", &m_exportOptions.includeGeometry);
    HelpMarker("Turn off to write an animation-only file.");
    ImGui::Checkbox("Embed textures", &m_exportOptions.embedTextures);

    bool overrideRate = m_exportOptions.sampleRate > 0.0f;
    if (ImGui::Checkbox("Override clip FPS on export", &overrideRate)) {
        if (overrideRate) {
            const Animation* current = CurrentAnimation();
            m_exportOptions.sampleRate =
                current ? current->sampleRate : m_importOptions.sampleRate;
        } else {
            m_exportOptions.sampleRate = 0.0f;
        }
    }
    ImGui::BeginDisabled(!overrideRate);
    float exportRate = overrideRate ? m_exportOptions.sampleRate : m_importOptions.sampleRate;
    ImGui::SetNextItemWidth(-140.0f);
    const bool exportRateEntered = ImGui::InputFloat("Output FPS", &exportRate, 1.0f, 10.0f,
                                                     "%.0f fps",
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
    if (overrideRate && (exportRateEntered || ImGui::IsItemDeactivatedAfterEdit())) {
        m_exportOptions.sampleRate =
            std::clamp(exportRate, kMinAnimationSampleRate, kMaxAnimationSampleRate);
    }
    ImGui::EndDisabled();
    HelpMarker("Resamples selected clips in the exported file only. With the override off, each "
               "clip keeps the FPS shown in the Timeline.");

    ImGui::SeparatorText("Clips");
    int selected = 0;
    for (const Animation& anim : m_model.animations) {
        if (anim.exportSelected) ++selected;
    }
    ImGui::Text("%d of %zu clip(s) selected", selected, m_model.animations.size());
    if (selected == 0 && !m_model.animations.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(LogLevel::Warning));
        ImGui::TextWrapped("No clips selected - the file will contain geometry only.");
        ImGui::PopStyleColor();
    }

    if (ImGui::BeginChild("##cliplist", ImVec2(0.0f, 140.0f), ImGuiChildFlags_Borders)) {
        for (int i = 0; i < static_cast<int>(m_model.animations.size()); ++i) {
            Animation& anim = m_model.animations[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::Checkbox(anim.name.c_str(), &anim.exportSelected);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", FormatDuration(anim).c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button("Choose file and export", ImVec2(220.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
        RunExport();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

}  // namespace fam
