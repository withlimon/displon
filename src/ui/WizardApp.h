#pragma once
#include <chrono>
#include <memory>
#include <vector>
#include <string>

#include "ui/UiModel.h"
#include "ui/Controls/CarbonButton.h"
#include "ui/Controls/CarbonCheckbox.h"
#include "ui/Controls/CarbonSegmented.h"
#include "ui/Controls/CarbonStepper.h"
#include "ui/gl/Renderer.h"
#include "ui/gl/TextRenderer.h"
#include "ui/Theme.h"
#include "ui/gl/IconAtlas.h"
#include "ui/Notify.h"

class WizardApp {
public:
    void Init(TextRenderer& text);
    void SetIcons(IconAtlas* icons) { icons_ = icons; }
    void SetOwnerHwnd(void* hwnd) { ownerHwnd_ = hwnd; }
    void SetDpiScale(float s) { dpiScale_ = s; }
    void SetTheme(const carbon::Theme& t) { theme_ = t; }
    void Resize(int w, int h);
    void Draw(Renderer& r, TextRenderer& text);
    void OnPointer(PointerState s, float x, float y);
    void OnKey(const KeyEvent& k);
    void OnWheel(float wheelNotches);   // +1 = one notch up
    bool IsLocked() const { return sessionLocked_; }
    // UI thread. Records "X was blocked"; returns false if it is a duplicate within 2 s or notifications are off
    // (the in-window log is always updated).
    bool OnBlockedApp(const std::wstring& exeName);
    // Popup text for "this app is blocked by the lock" (reason + time left included).
    ToastInfo BuildBlockedToast(const std::wstring& exeName, const std::wstring& imagePath, bool onBlockList) const;
    // 1 Hz from the UI timer: detects the end of a block (toast, window becomes closable, "new block" button).
    void Tick();

    UiSessionConfig& Config() { return config_; }
    WizardStep Step() const { return step_; }
    bool WantsCountdownTimer() const { return step_ == WizardStep::Countdown; }

private:
    void BuildStepControls();
    void DrawTopNav(Renderer& r, TextRenderer& text);
    void DrawSettings(Renderer& r, TextRenderer& text);
    void DrawWhitelist(Renderer& r, TextRenderer& text);
    void DrawDuration(Renderer& r, TextRenderer& text);
    void DrawBreaks(Renderer& r, TextRenderer& text);
    void DrawStart(Renderer& r, TextRenderer& text);
    void DrawCountdown(Renderer& r, TextRenderer& text);
    void GoNext();
    void GoBack();
    void OnAddRecentlyOpened();
    void OnAddExeFile();
    void OnImportWhitelist();
    void OnExportWhitelist();
    void StartLockSession();
    void ResetForNewSession();
    std::wstring RemainingText() const;
    void RelayoutWhitelist();
    void DrawChrome(Renderer& r, TextRenderer& text);
    Rect WhitelistCard() const;

    UiSessionConfig config_;
    WizardStep step_ = WizardStep::Settings;
    float dpiScale_ = 1.f;
    int width_ = 960, height_ = 640;
    carbon::Theme theme_ = carbon::kG100;
    IconAtlas* icons_ = nullptr;
    void* ownerHwnd_ = nullptr;
    std::wstring statusMsg_;
    struct BlockedEntry { std::wstring name; std::chrono::system_clock::time_point at; };
    std::vector<BlockedEntry> blocked_;          // newest last
    int blockedTotal_ = 0;
    std::chrono::system_clock::time_point lockStart_{};
    float wlScroll_ = 0.f;          // pixels scrolled in the whitelist
    Rect  wlView_{};                // visible list viewport (physical px)
    float wlContentH_ = 0.f;
    bool sessionLocked_ = false;
    bool sessionEnded_ = false;     // the block ran out; the window is free again
    bool showDatePicker_ = false;
    // DateTime picker fields (local civil)
    int pickYear_ = 2026, pickMonth_ = 10, pickDay_ = 13;
    int pickHour_ = 18, pickMinute_ = 0;
    CarbonStepper stepYear_, stepMonth_, stepDay_, stepHour_, stepMinute_;
    CarbonButton btnPickerOk_, btnPickerCancel_, btnOpenPicker_;

    CarbonSegmented topNav_;
    CarbonButton primaryBtn_;
    CarbonButton secondaryBtn_;
    CarbonSegmented durationMode_;
    CarbonSegmented breakMode_;
    CarbonSegmented blockAction_;
    CarbonCheckbox chkTaskMgr_;
    CarbonCheckbox chkWinSettings_;
    CarbonCheckbox chkNotifyClose_;
    CarbonCheckbox chkNotifyEnd_;
    CarbonButton btnAddRecent_;
    CarbonButton btnAddExe_;
    CarbonButton btnImport_;
    CarbonButton btnExport_;
    CarbonStepper stepDuration_;
    CarbonStepper stepPomodoroWork_;
    CarbonStepper stepPomodoroBreak_;
    CarbonStepper stepAllowance_;
    std::vector<std::unique_ptr<CarbonCheckbox>> whitelistChecks_;
    std::vector<std::unique_ptr<CarbonButton>> durationChips_;
    std::vector<ICarbonControl*> hitTargets_;
};
