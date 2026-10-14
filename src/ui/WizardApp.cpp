#include "ui/WizardApp.h"
#include "ui/Win32Dialogs.h"
#include "ui/SessionClient.h"
#include "ui/Notify.h"
#include "ui/LocalEngine.h"
#include "common/PathUtil.h"

#include <algorithm>
#include <functional>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <cwchar>

namespace {

// "chrome.exe" fits or becomes "chrome.e..." - never spills out of its card.
std::wstring FitText(TextRenderer& t, std::wstring s, const carbon::TypeStyle& st, float maxW) {
    if (t.MeasureWidth(s, st) <= maxW) return s;
    while (s.size() > 1 && t.MeasureWidth(s + L"\u2026", st) > maxW) s.pop_back();
    return s + L"\u2026";
}

float TagWidth(TextRenderer& t, const std::wstring& label, float s) {
    return t.MeasureWidth(label, carbon::kLabel01) + s * 16;
}

// Carbon "tag": a small flat label.
void DrawTag(Renderer& r, TextRenderer& t, const std::wstring& label, float x, float y, float s,
             const carbon::Color& bg, const carbon::Color& fg) {
    const float h = s * 24, w = TagWidth(t, label, s);
    r.FillRect({x, y, w, h}, bg);
    t.Draw(r, label, x + s * 8, y + (h - t.LineHeight(carbon::kLabel01)) * 0.5f, carbon::kLabel01, fg);
}

std::tm LocalTm(std::chrono::system_clock::time_point tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    return tmv;
}

// "19:42" today, "Wed 14 Oct, 19:42" otherwise.
std::wstring FormatLocal(std::chrono::system_clock::time_point tp) {
    const std::tm a = LocalTm(tp), b = LocalTm(std::chrono::system_clock::now());
    wchar_t buf[64];
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) std::wcsftime(buf, 64, L"%H:%M", &a);
    else std::wcsftime(buf, 64, L"%a %d %b, %H:%M", &a);
    return buf;
}

std::wstring FormatDuration(long long mins) {
    wchar_t buf[48];
    if (mins >= 24 * 60 && mins % (24 * 60) == 0) swprintf(buf, 48, L"%lld day%ls", mins / 1440, mins >= 2880 ? L"s" : L"");
    else if (mins >= 60 && mins % 60 == 0) swprintf(buf, 48, L"%lld h", mins / 60);
    else if (mins >= 60) swprintf(buf, 48, L"%lld h %lld min", mins / 60, mins % 60);
    else swprintf(buf, 48, L"%lld min", mins);
    return buf;
}

// Digits in fixed-width cells so the countdown does not jitter every second (proportional digits would).
void DrawDigits(Renderer& r, TextRenderer& t, const std::wstring& str, float cx, float y,
                const carbon::TypeStyle& st, const carbon::Color& c) {
    const float cell = t.MeasureWidth(L"0", st);
    auto adv = [&](wchar_t ch) { return (ch >= L'0' && ch <= L'9') ? cell : t.MeasureWidth(std::wstring(1, ch), st); };
    float total = 0;
    for (wchar_t ch : str) total += adv(ch);
    float x = cx - total * 0.5f;
    for (wchar_t ch : str) {
        const float w = adv(ch);
        const float gw = t.MeasureWidth(std::wstring(1, ch), st);
        t.Draw(r, std::wstring(1, ch), x + (w - gw) * 0.5f, y, st, c);
        x += w;
    }
}

}  // namespace

void WizardApp::Init(TextRenderer&) {
    // The whitelist starts EMPTY: only apps the user adds (recent / exe picker / import) appear.
#ifdef _WIN32
    // If a lock survived a restart / crash of the UI, show it again instead of an unlocked wizard.
    const auto st = LocalEngine::Instance().GetStatus();
    if (st.locked && st.unlockTime) {
        sessionLocked_ = true;
        config_.unlockTime = *st.unlockTime;
        config_.durationMode = SessionDurationMode::Timer;
        lockStart_ = std::chrono::system_clock::now();
        if (st.config) {   // the persisted session: show what it REALLY enforces, not defaults
            const SessionConfig& c = *st.config;
            config_.durationMode = c.durationMode;
            config_.timerMinutes = c.duration;
            config_.breakMode = c.breakMode;
            config_.pomodoroBlockMin = c.pomodoroWorkMin;
            config_.pomodoroBreakMin = c.pomodoroBreakMin;
            config_.allowanceMinutes = c.allowanceMin;
            config_.blockTaskManager = c.blockTaskManager;
            config_.blockWindowsSettings = c.blockWindowsSettings;
            config_.websiteBlocks = c.websiteBlockRules;
            if (c.sessionStartTime.time_since_epoch().count() != 0) lockStart_ = c.sessionStartTime;
            config_.whitelist.clear();
            for (const auto& p : c.allowListExePaths) config_.whitelist.push_back({FileNameOf(p), p, true});
        }
        step_ = WizardStep::Countdown;
    }
#endif
    BuildStepControls();
}

void WizardApp::Resize(int w, int h) {
    width_ = w;
    height_ = h;
    BuildStepControls();
}

void WizardApp::BuildStepControls() {
    hitTargets_.clear();
    const float s = dpiScale_;
    auto S = [s](float v) { return v * s; };

    topNav_.SetOptions({L"Settings", L"Whitelist", L"Duration", L"Breaks", L"Start"});
    topNav_.SetSelected(static_cast<int>(step_ <= WizardStep::Start ? step_ : WizardStep::Start));
    topNav_.Layout({0, 0, static_cast<float>(width_), S(48)});
    topNav_.SetOnChange([this](int i) {
        if (sessionLocked_) return;  // cannot change once started
        if (i >= 0 && i <= 4) {
            step_ = static_cast<WizardStep>(i);
            BuildStepControls();
        }
    });
    if (step_ != WizardStep::Countdown) hitTargets_.push_back(&topNav_);

    const float btnW = S(170), btnH = S(40);
    const float footerY = height_ - S(72);
    primaryBtn_.SetKind(CarbonButton::Kind::Primary);
    secondaryBtn_.SetKind(CarbonButton::Kind::Ghost);
    primaryBtn_.Layout({width_ - S(24) - btnW, footerY, btnW, btnH});
    secondaryBtn_.Layout({width_ - S(24) - btnW * 2 - S(12), footerY, btnW, btnH});
    primaryBtn_.SetOnClick([this] { GoNext(); });
    secondaryBtn_.SetOnClick([this] { GoBack(); });
    primaryBtn_.SetEnabled(!sessionLocked_ || step_ == WizardStep::Countdown);
    secondaryBtn_.SetEnabled(!sessionLocked_);

    switch (step_) {
    case WizardStep::Settings:
        primaryBtn_.SetLabel(L"Continue to whitelist");
        secondaryBtn_.SetLabel(L"");
        chkTaskMgr_.SetLabel(L"Block Windows Task Manager");
        chkTaskMgr_.SetChecked(config_.blockTaskManager);
        chkTaskMgr_.Layout({S(48), S(160), S(360), S(28)});
        chkTaskMgr_.SetOnChange([this](bool v) { config_.blockTaskManager = v; });
        chkWinSettings_.SetLabel(L"Block Windows Settings");
        chkWinSettings_.SetChecked(config_.blockWindowsSettings);
        chkWinSettings_.Layout({S(48), S(196), S(360), S(28)});
        chkWinSettings_.SetOnChange([this](bool v) { config_.blockWindowsSettings = v; });
        blockAction_.SetOptions({L"Minimize", L"Force close"});
        blockAction_.SetSelected(config_.blockAction == WindowTitleAction::Close ? 1 : 0);
        blockAction_.Layout({S(400), S(176), S(280), S(36)});
        blockAction_.SetOnChange([this](int i) {
            config_.blockAction = (i == 1) ? WindowTitleAction::Close : WindowTitleAction::Suppress;
        });
        chkNotifyClose_.SetLabel(L"Notify when an app is blocked");
        chkNotifyClose_.SetChecked(config_.notifyOnAppBlocked);
        chkNotifyClose_.Layout({S(48), S(250), S(360), S(28)});
        chkNotifyClose_.SetOnChange([this](bool v) { config_.notifyOnAppBlocked = v; });
        chkNotifyEnd_.SetLabel(L"Notify when the block ends");
        chkNotifyEnd_.SetChecked(config_.notifyOnBlockEnd);
        chkNotifyEnd_.Layout({S(48), S(286), S(360), S(28)});
        chkNotifyEnd_.SetOnChange([this](bool v) { config_.notifyOnBlockEnd = v; });
        hitTargets_.push_back(&chkTaskMgr_);
        hitTargets_.push_back(&chkWinSettings_);
        hitTargets_.push_back(&blockAction_);
        hitTargets_.push_back(&chkNotifyClose_);
        hitTargets_.push_back(&chkNotifyEnd_);
        hitTargets_.push_back(&primaryBtn_);
        break;

    case WizardStep::Whitelist:
        primaryBtn_.SetLabel(L"Continue to duration");
        secondaryBtn_.SetLabel(L"Change settings");
        whitelistChecks_.clear();
        for (size_t i = 0; i < config_.whitelist.size(); ++i) {
            auto cb = std::make_unique<CarbonCheckbox>();
            cb->SetLabel(config_.whitelist[i].name);
            if (config_.whitelist[i].path != config_.whitelist[i].name) cb->SetSubLabel(config_.whitelist[i].path);
            cb->SetChecked(config_.whitelist[i].checked);
            size_t idx = i;
            cb->SetOnChange([this, idx](bool v) {
                if (idx < config_.whitelist.size()) config_.whitelist[idx].checked = v;
            });
            hitTargets_.push_back(cb.get());
            whitelistChecks_.push_back(std::move(cb));
        }
        {
            const Rect card = WhitelistCard();
            const float bw = S(230), bh = S(44), gap = S(12);
            const float bx = card.x + card.w - bw - S(16);
            float by = card.y + S(16);
            auto place = [&](CarbonButton& b, const wchar_t* label, std::function<void()> fn) {
                b.SetLabel(label);
                b.SetKind(CarbonButton::Kind::Secondary);
                b.Layout({bx, by, bw, bh});
                b.SetOnClick(std::move(fn));
                hitTargets_.push_back(&b);
                by += bh + gap;
            };
            place(btnAddRecent_, L"Add recently opened apps", [this] { OnAddRecentlyOpened(); });
            place(btnAddExe_, L"Add exe file...", [this] { OnAddExeFile(); });
            place(btnImport_, L"Import from file...", [this] { OnImportWhitelist(); });
            place(btnExport_, L"Export to file...", [this] { OnExportWhitelist(); });
        }
        RelayoutWhitelist();
        hitTargets_.push_back(&secondaryBtn_);
        hitTargets_.push_back(&primaryBtn_);
        break;

    case WizardStep::Duration:
        primaryBtn_.SetLabel(L"Continue to breaks");
        secondaryBtn_.SetLabel(L"Change whitelist");
        durationMode_.SetOptions({L"Timer", L"Restart", L"Until date"});
        durationMode_.SetSelected(
            config_.durationMode == SessionDurationMode::Timer ? 0 :
            config_.durationMode == SessionDurationMode::UntilRestart ? 1 : 2);
        durationMode_.Layout({S(48), S(140), S(360), S(40)});
        durationMode_.SetOnChange([this](int i) {
            if (i == 0) config_.durationMode = SessionDurationMode::Timer;
            else if (i == 1) config_.durationMode = SessionDurationMode::UntilRestart;
            else {
                config_.durationMode = SessionDurationMode::UntilDateTime;
                showDatePicker_ = true;
            }
            BuildStepControls();
        });
        hitTargets_.push_back(&durationMode_);

        // Quick chips: 1, 5, 15, 30, 60, 120, 180, 360, 720, 1440
        {
            static const int kMins[] = {1, 5, 15, 30, 60, 120, 180, 360, 720, 1440};
            static const wchar_t* kLabels[] = {L"1m", L"5m", L"15m", L"30m", L"1h", L"2h", L"3h", L"6h", L"12h", L"1d"};
            durationChips_.clear();
            const float chipW = S(56), chipH = S(30), gap = S(6);
            float cx = S(48), cy = S(200);
            for (int i = 0; i < 10; ++i) {
                if (cx + chipW > width_ - S(48)) { cx = S(48); cy += chipH + gap; }
                auto btn = std::make_unique<CarbonButton>();
                btn->SetLabel(kLabels[i]);
                const bool sel = config_.timerMinutes.count() == kMins[i];
                btn->SetKind(sel ? CarbonButton::Kind::Primary : CarbonButton::Kind::Secondary);
                btn->Layout({cx, cy, chipW, chipH});
                int mins = kMins[i];
                btn->SetOnClick([this, mins] {
                    config_.timerMinutes = std::chrono::minutes(mins);
                    BuildStepControls();
                });
                hitTargets_.push_back(btn.get());
                durationChips_.push_back(std::move(btn));
                cx += chipW + gap;
            }
        }
        stepDuration_.SetLabel(L"Custom duration");
        stepDuration_.SetRange(1, 24 * 60);
        stepDuration_.SetValue(static_cast<int>(config_.timerMinutes.count()));
        stepDuration_.SetSuffix(L"min");
        stepDuration_.Layout({S(48), S(300), S(220), S(52)});
        stepDuration_.SetOnChange([this](int v) {
            config_.timerMinutes = std::chrono::minutes(v);
        });
        hitTargets_.push_back(&stepDuration_);
        btnOpenPicker_.SetLabel(L"Pick stop date & time...");
        btnOpenPicker_.SetKind(CarbonButton::Kind::Secondary);
        btnOpenPicker_.Layout({S(300), S(310), S(240), S(36)});
        btnOpenPicker_.SetOnClick([this] {
            config_.durationMode = SessionDurationMode::UntilDateTime;
            showDatePicker_ = true;
            BuildStepControls();
        });
        hitTargets_.push_back(&btnOpenPicker_);

        if (showDatePicker_) {
            // Carbon modal steppers for Y-M-D H:M
            stepYear_.SetLabel(L"Year"); stepYear_.SetRange(2024, 2099); stepYear_.SetValue(pickYear_);
            stepYear_.SetSuffix(L""); stepYear_.Layout({S(80), S(200), S(160), S(52)});
            stepYear_.SetOnChange([this](int v) { pickYear_ = v; });
            stepMonth_.SetLabel(L"Month"); stepMonth_.SetRange(1, 12); stepMonth_.SetValue(pickMonth_);
            stepMonth_.SetSuffix(L""); stepMonth_.Layout({S(260), S(200), S(140), S(52)});
            stepMonth_.SetOnChange([this](int v) { pickMonth_ = v; });
            stepDay_.SetLabel(L"Day"); stepDay_.SetRange(1, 31); stepDay_.SetValue(pickDay_);
            stepDay_.SetSuffix(L""); stepDay_.Layout({S(420), S(200), S(140), S(52)});
            stepDay_.SetOnChange([this](int v) { pickDay_ = v; });
            stepHour_.SetLabel(L"Hour"); stepHour_.SetRange(0, 23); stepHour_.SetValue(pickHour_);
            stepHour_.SetSuffix(L""); stepHour_.Layout({S(80), S(280), S(140), S(52)});
            stepHour_.SetOnChange([this](int v) { pickHour_ = v; });
            stepMinute_.SetLabel(L"Minute"); stepMinute_.SetRange(0, 59); stepMinute_.SetValue(pickMinute_);
            stepMinute_.SetSuffix(L""); stepMinute_.Layout({S(240), S(280), S(140), S(52)});
            stepMinute_.SetOnChange([this](int v) { pickMinute_ = v; });
            btnPickerOk_.SetLabel(L"Set stop time");
            btnPickerOk_.SetKind(CarbonButton::Kind::Primary);
            btnPickerOk_.Layout({S(400), S(360), S(160), S(40)});
            btnPickerOk_.SetOnClick([this] {
                // Convert local civil → system_clock (approximate: treat as local via time_t)
                std::tm tm{};
                tm.tm_year = pickYear_ - 1900;
                tm.tm_mon = pickMonth_ - 1;
                tm.tm_mday = pickDay_;
                tm.tm_hour = pickHour_;
                tm.tm_min = pickMinute_;
                tm.tm_isdst = -1;
                const std::time_t tt = std::mktime(&tm);
                if (tt != -1) {
                    config_.unlockTime = std::chrono::system_clock::from_time_t(tt);
                    config_.durationMode = SessionDurationMode::UntilDateTime;
                    config_.timerMinutes = std::chrono::duration_cast<std::chrono::minutes>(
                        config_.unlockTime - std::chrono::system_clock::now());
                    if (config_.timerMinutes.count() < 1)
                        config_.timerMinutes = std::chrono::minutes(1);
                }
                showDatePicker_ = false;
                BuildStepControls();
            });
            btnPickerCancel_.SetLabel(L"Cancel");
            btnPickerCancel_.SetKind(CarbonButton::Kind::Ghost);
            btnPickerCancel_.Layout({S(240), S(360), S(140), S(40)});
            btnPickerCancel_.SetOnClick([this] {
                showDatePicker_ = false;
                BuildStepControls();
            });
            hitTargets_.clear();
            hitTargets_.push_back(&stepYear_);
            hitTargets_.push_back(&stepMonth_);
            hitTargets_.push_back(&stepDay_);
            hitTargets_.push_back(&stepHour_);
            hitTargets_.push_back(&stepMinute_);
            hitTargets_.push_back(&btnPickerOk_);
            hitTargets_.push_back(&btnPickerCancel_);
        }

        hitTargets_.push_back(&secondaryBtn_);
        hitTargets_.push_back(&primaryBtn_);
        break;

    case WizardStep::Breaks:
        primaryBtn_.SetLabel(L"View summary");
        secondaryBtn_.SetLabel(L"Change duration");
        breakMode_.SetOptions({L"No Breaks", L"Pomodoro", L"Allowance"});
        breakMode_.SetSelected(static_cast<int>(config_.breakMode));
        breakMode_.Layout({S(48), S(140), S(420), S(40)});
        breakMode_.SetOnChange([this](int i) {
            config_.breakMode = static_cast<SessionBreakMode>(i);
            BuildStepControls();
        });
        hitTargets_.push_back(&breakMode_);

        if (config_.breakMode == SessionBreakMode::Pomodoro) {
            stepPomodoroWork_.SetLabel(L"Block for");
            stepPomodoroWork_.SetRange(1, 120);
            stepPomodoroWork_.SetValue(config_.pomodoroBlockMin);
            stepPomodoroWork_.Layout({S(48), S(200), S(200), S(52)});
            stepPomodoroWork_.SetOnChange([this](int v) { config_.pomodoroBlockMin = v; });
            stepPomodoroBreak_.SetLabel(L"Then break");
            stepPomodoroBreak_.SetRange(1, 60);
            stepPomodoroBreak_.SetValue(config_.pomodoroBreakMin);
            stepPomodoroBreak_.Layout({S(280), S(200), S(200), S(52)});
            stepPomodoroBreak_.SetOnChange([this](int v) { config_.pomodoroBreakMin = v; });
            hitTargets_.push_back(&stepPomodoroWork_);
            hitTargets_.push_back(&stepPomodoroBreak_);
        } else if (config_.breakMode == SessionBreakMode::Allowance) {
            stepAllowance_.SetLabel(L"Allowance before block");
            stepAllowance_.SetRange(1, 120);
            stepAllowance_.SetValue(config_.allowanceMinutes);
            stepAllowance_.Layout({S(48), S(200), S(240), S(52)});
            stepAllowance_.SetOnChange([this](int v) { config_.allowanceMinutes = v; });
            hitTargets_.push_back(&stepAllowance_);
        }
        hitTargets_.push_back(&secondaryBtn_);
        hitTargets_.push_back(&primaryBtn_);
        break;

    case WizardStep::Start:
        primaryBtn_.SetLabel(sessionLocked_ ? L"Locked" : L"Start");
        secondaryBtn_.SetLabel(L"Change breaks");
        primaryBtn_.SetEnabled(!sessionLocked_);
        hitTargets_.push_back(&secondaryBtn_);
        hitTargets_.push_back(&primaryBtn_);
        break;

    case WizardStep::Countdown:
        if (sessionEnded_) {   // the block is over: offer a way back instead of a dead-end screen
            primaryBtn_.SetKind(CarbonButton::Kind::Primary);
            primaryBtn_.SetLabel(L"Start a new block");
            primaryBtn_.SetEnabled(true);
            primaryBtn_.Layout({(width_ - btnW) * 0.5f, height_ - S(88), btnW, btnH});
            hitTargets_.push_back(&primaryBtn_);
        }
        break;
    }
}

void WizardApp::GoNext() {
    if (step_ == WizardStep::Countdown && sessionEnded_) {
        ResetForNewSession();
        return;
    }
    if (sessionLocked_ && step_ != WizardStep::Start) return;
    if (step_ == WizardStep::Start) {
        StartLockSession();
        return;
    }
    if (step_ != WizardStep::Countdown) {
        step_ = static_cast<WizardStep>(static_cast<int>(step_) + 1);
        BuildStepControls();
    }
}

void WizardApp::GoBack() {
    if (sessionLocked_) return;
    if (step_ != WizardStep::Settings && step_ != WizardStep::Countdown) {
        step_ = static_cast<WizardStep>(static_cast<int>(step_) - 1);
        BuildStepControls();
    }
}

void WizardApp::StartLockSession() {
    if (sessionLocked_) return;

    // An allow-list with nothing checked would engage NO rule at all (the engine only enforces an allow-list
    // when it is non-empty) - the session would "lock" and block nothing. Refuse instead of faking it.
    bool anyChecked = false;
    for (const auto& e : config_.whitelist) anyChecked = anyChecked || e.checked;
    if (!anyChecked) {
        statusMsg_ = L"Pick at least one app on the Whitelist step - everything else will be closed.";
        step_ = WizardStep::Whitelist;
        BuildStepControls();
        return;
    }

    if (config_.durationMode == SessionDurationMode::Timer) {
        config_.unlockTime = std::chrono::system_clock::now() + config_.timerMinutes;
    } else if (config_.durationMode == SessionDurationMode::UntilRestart) {
        config_.unlockTime = std::chrono::system_clock::now() + std::chrono::hours(24 * 365);
    } else {
        // UntilDateTime - unlockTime already set by picker
        if (config_.unlockTime <= std::chrono::system_clock::now())
            config_.unlockTime = std::chrono::system_clock::now() + config_.timerMinutes;
    }

    SessionConfig eng = config_.ToEngineConfig();
    if (config_.blockWindowsSettings) {
        // SystemSettings.exe is the Settings app itself. (ApplicationFrameHost.exe hosts EVERY UWP window and
        // is needed by Windows - it must never be a deny rule.)
        eng.processBlockRules.push_back(MakeRule(RuleType::ProcessExe, L"SystemSettings.exe"));
    }

    bool started = false;
    std::wstring detail;
#ifdef _WIN32
    // 1) A real Windows service, if one is installed and running (survives the UI being closed).
    auto remote = SessionClient::StartRemote(eng, config_.sessionPassword);
    if (remote.ok) {
        started = true;
        detail = L"Session started via service.";
        if (remote.unlockEpochSecs) {
            config_.unlockTime = std::chrono::system_clock::time_point(
                std::chrono::seconds(*remote.unlockEpochSecs));
        }
    } else {
        // 2) Otherwise enforce in-process. This is real enforcement, not just a timer.
        auto local = LocalEngine::Instance().StartSession(eng, config_.sessionPassword);
        started = local.ok;
        detail = local.detail;
        if (local.ok && local.unlockTime) config_.unlockTime = *local.unlockTime;
    }
#else
    detail = L"Enforcement is only available on Windows.";
#endif

    if (!started) {
        // Do NOT pretend to be locked.
        statusMsg_ = L"Could not start the lock: " + detail;
        BuildStepControls();
        return;
    }

    // Freeze configuration - once you start, you can't go back.
    sessionLocked_ = true;
    lockStart_ = std::chrono::system_clock::now();
    statusMsg_ = detail;
    step_ = WizardStep::Countdown;
    BuildStepControls();
}

void WizardApp::ResetForNewSession() {
    sessionLocked_ = false;
    sessionEnded_ = false;
    statusMsg_.clear();
    blocked_.clear();
    blockedTotal_ = 0;
    lockStart_ = {};
    step_ = WizardStep::Settings;
    BuildStepControls();
}

std::wstring WizardApp::RemainingText() const {
    if (config_.durationMode == SessionDurationMode::UntilRestart) return L"Unlocks when you restart the computer";
    auto left = std::chrono::duration_cast<std::chrono::seconds>(config_.unlockTime - std::chrono::system_clock::now());
    if (left.count() < 0) left = std::chrono::seconds(0);
    const long long m = left.count() / 60;
    if (m >= 60) return L"Unlocks in " + FormatDuration(m);
    wchar_t buf[48];
    swprintf(buf, 48, L"Unlocks in %lld min %02lld s", m, static_cast<long long>(left.count() % 60));
    return buf;
}

ToastInfo WizardApp::BuildBlockedToast(const std::wstring& name, const std::wstring& imagePath,
                                       bool onBlockList) const {
    ToastInfo t;
    t.title = L"App blocked";
    t.message = name + L" is blocked by the lock.";
    t.detail = (onBlockList ? L"It is on the block list" : L"It is not on your whitelist") + std::wstring(L"  \u2022  ") +
               RemainingText();
    t.imagePath = imagePath;
    t.dark = !(theme_.background.r > 0.5f);
    t.danger = true;
    return t;
}

void WizardApp::Tick() {
    if (!sessionLocked_ || config_.durationMode == SessionDurationMode::UntilRestart) return;
    if (std::chrono::system_clock::now() < config_.unlockTime) return;
    sessionLocked_ = false;      // WM_CLOSE works again
    sessionEnded_ = true;
    BuildStepControls();
    if (config_.notifyOnBlockEnd) {
        ToastInfo t;
        t.title = L"Block ended";
        t.message = L"You're free to use your other apps again.";
        t.detail = L"Displon lock finished";
        t.dark = !(theme_.background.r > 0.5f);
        t.danger = false;
        ShowToast(t);
    }
}

void WizardApp::DrawChrome(Renderer& r, TextRenderer& text) {
    const float s = dpiScale_;
    const float W = static_cast<float>(width_), H = static_cast<float>(height_);
    r.FillRect({0, s * 48, W, 1.f}, theme_.border);                       // under the nav
    const float stripY = H - s * 88;
    r.FillRect({0, stripY, W, H - stripY}, theme_.layer);                 // footer strip
    r.FillRect({0, stripY, W, 1.f}, theme_.border);
    const int idx = static_cast<int>(step_ <= WizardStep::Start ? step_ : WizardStep::Start);

    // Five-segment step rail on the strip's top edge: finished + current in blue, the rest grey.
    const float gap = s * 2, segW = (W - gap * 4) / 5.f;
    for (int i = 0; i < 5; ++i)
        r.FillRect({i * (segW + gap), stripY, segW, s * 3}, i <= idx ? theme_.accent : theme_.border);

    wchar_t buf[32];
    swprintf(buf, 32, L"Step %d of 5", idx + 1);
    const float lh = text.LineHeight(carbon::kLabel01);
    const float ty = H - s * 72 + (s * 40 - lh) * 0.5f;
    text.Draw(r, buf, s * 32, ty, carbon::kLabel01, theme_.textSecondary);
    const float w1 = text.MeasureWidth(buf, carbon::kLabel01);
    text.Draw(r, StepTitle(static_cast<WizardStep>(idx)), s * 32 + w1 + s * 12, ty, carbon::kLabel01, theme_.textPrimary);
}

void WizardApp::DrawTopNav(Renderer& r, TextRenderer& text) {
    const float s = dpiScale_;
    r.FillRect({0, 0, static_cast<float>(width_), s * 48}, theme_.layer);
    if (icons_) icons_->Draw(r, IconId::App, {s * 12, s * 10, s * 28, s * 28}, theme_.accent);
    text.Draw(r, L"Displon", s * 48, s * 14, carbon::kHeading05, theme_.textPrimary);
    topNav_.Layout({s * 200, 0, static_cast<float>(width_) - s * 200, s * 48});
    topNav_.Draw(r, text, theme_);
}

void WizardApp::DrawSettings(Renderer& r, TextRenderer& text) {
    const float s = dpiScale_;
    if (icons_) icons_->Draw(r, IconId::Gear, {s * 48, s * 72, s * 22, s * 22}, theme_.textPrimary);
    text.Draw(r, L"Settings", s * 78, s * 72, carbon::kHeading03, theme_.textPrimary);
    text.Draw(r, L"Choose how strictly the lock behaves while it is running",
              s * 48, s * 108, carbon::kHelper01, theme_.textSecondary);

    Rect card{s * 32, s * 130, static_cast<float>(width_) - s * 64, s * 220};
    r.FillRect(card, theme_.layer);
    r.StrokeRect(card, theme_.border, 1.f);

    text.Draw(r, L"Blocking", s * 48, s * 140, carbon::kLabel01, theme_.textSecondary);
    chkTaskMgr_.Draw(r, text, theme_);
    chkWinSettings_.Draw(r, text, theme_);
    text.Draw(r, L"Action when blocking apps", s * 400, s * 148, carbon::kLabel01, theme_.textSecondary);
    blockAction_.Draw(r, text, theme_);
    r.FillRect({card.x + s * 16, s * 224, card.w - s * 32, 1.f}, theme_.border);   // group divider
    text.Draw(r, L"Notifications", s * 48, s * 232, carbon::kLabel01, theme_.textSecondary);
    chkNotifyClose_.Draw(r, text, theme_);
    chkNotifyEnd_.Draw(r, text, theme_);
}

Rect WizardApp::WhitelistCard() const {
    const float s = dpiScale_;
    const float top = s * 136;
    const float bottom = static_cast<float>(height_) - s * 96;   // footer buttons live below this
    return {s * 32, top, static_cast<float>(width_) - s * 64, std::max(s * 200, bottom - top)};
}

void WizardApp::RelayoutWhitelist() {
    const float s = dpiScale_;
    const Rect card = WhitelistCard();
    const float rowH = s * 54;
    const float listW = card.w - s * 230 - s * 64;   // leave the button column free
    wlView_ = {card.x + s * 16, card.y + s * 8, std::max(s * 120, listW), card.h - s * 16};
    wlContentH_ = static_cast<float>(whitelistChecks_.size()) * rowH;
    const float maxScroll = std::max(0.f, wlContentH_ - wlView_.h);
    wlScroll_ = std::min(std::max(wlScroll_, 0.f), maxScroll);
    for (size_t i = 0; i < whitelistChecks_.size(); ++i) {
        const float y = wlView_.y + static_cast<float>(i) * rowH - wlScroll_;
        const bool visible = (y >= wlView_.y - 1.f) && (y + rowH <= wlView_.y + wlView_.h + 1.f);
        // Rows outside the viewport get an empty rect so they can never be clicked.
        whitelistChecks_[i]->Layout(visible ? Rect{wlView_.x, y, wlView_.w, rowH}
                                            : Rect{-10000.f, -10000.f, 0.f, 0.f});
    }
}

bool WizardApp::OnBlockedApp(const std::wstring& name) {
    const auto now = std::chrono::system_clock::now();
    for (auto it = blocked_.rbegin(); it != blocked_.rend(); ++it) {
        if (it->name == name && now - it->at < std::chrono::seconds(2)) return false;
        if (now - it->at >= std::chrono::seconds(2)) break;
    }
    ++blockedTotal_;
    blocked_.push_back({name, now});
    if (blocked_.size() > 20) blocked_.erase(blocked_.begin());
    return config_.notifyOnAppBlocked;
}

void WizardApp::OnWheel(float notches) {
    if (step_ != WizardStep::Whitelist || showDatePicker_ || sessionLocked_) return;
    wlScroll_ -= notches * dpiScale_ * 54.f;
    RelayoutWhitelist();
}

void WizardApp::DrawWhitelist(Renderer& r, TextRenderer& text) {
    const float s = dpiScale_;
    if (icons_) icons_->Draw(r, IconId::ListCheck, {s * 48, s * 72, s * 24, s * 24}, theme_.textPrimary);
    text.Draw(r, L"Whitelist", s * 82, s * 70, carbon::kHeading03, theme_.textPrimary);
    text.Draw(r, L"Only the apps that are checked will be allowed to open",
              s * 48, s * 104, carbon::kHelper01, theme_.textSecondary);

    const Rect card = WhitelistCard();
    r.FillRect(card, theme_.layer);
    r.StrokeRect(card, theme_.border, 1.f);

    RelayoutWhitelist();
    if (whitelistChecks_.empty()) {
        if (icons_) icons_->Draw(r, IconId::ListCheck, {wlView_.x + s * 8, wlView_.y + s * 24, s * 32, s * 32}, theme_.borderStrong);
        text.Draw(r, L"No apps yet", wlView_.x + s * 56, wlView_.y + s * 22, carbon::kBody02, theme_.textPrimary);
        text.Draw(r, L"Add the apps you need. Everything else will be closed while the lock is on.",
                  wlView_.x + s * 56, wlView_.y + s * 52, carbon::kHelper01, theme_.textSecondary);
    }
    r.SetScissor(wlView_);               // rows can never spill out of the card
    for (auto& cb : whitelistChecks_) {
        const Rect& b = cb->Bounds();
        if (b.w > 0 && cb->Hovered()) r.FillRect(b, theme_.layerAccent);              // row hover
        if (b.w > 0) r.FillRect({b.x, b.y + b.h - 1.f, b.w, 1.f}, theme_.border);   // row separator
        cb->Draw(r, text, theme_);
    }
    r.ClearScissor();

    if (wlContentH_ > wlView_.h) {       // thin scrollbar
        const float trackX = wlView_.x + wlView_.w + s * 6;
        const float thumbH = std::max(s * 32, wlView_.h * wlView_.h / wlContentH_);
        const float maxScroll = wlContentH_ - wlView_.h;
        const float thumbY = wlView_.y + (wlView_.h - thumbH) * (maxScroll > 0 ? wlScroll_ / maxScroll : 0.f);
        r.FillRect({trackX, wlView_.y, s * 4, wlView_.h}, theme_.border);
        r.FillRect({trackX, thumbY, s * 4, thumbH}, theme_.textSecondary);
    }

    int checked = 0;
    for (const auto& e : config_.whitelist) checked += e.checked ? 1 : 0;
    wchar_t buf[96];
    swprintf(buf, 96, L"%d of %d apps allowed", checked, static_cast<int>(config_.whitelist.size()));
    text.Draw(r, buf, card.x + card.w - s * 246, card.y + card.h - s * 34, carbon::kLabel01, theme_.textSecondary);

    btnAddRecent_.Draw(r, text, theme_);
    btnAddExe_.Draw(r, text, theme_);
    btnImport_.Draw(r, text, theme_);
    btnExport_.Draw(r, text, theme_);
}

void WizardApp::DrawDuration(Renderer& r, TextRenderer& text) {
    const float s = dpiScale_;
    if (icons_) icons_->Draw(r, IconId::Clock, {s * 48, s * 72, s * 22, s * 22}, theme_.textPrimary);
    text.Draw(r, L"Duration", s * 78, s * 72, carbon::kHeading03, theme_.textPrimary);
    text.Draw(r, L"Only allow whitelisted apps until",
              s * 48, s * 108, carbon::kHelper01, theme_.textSecondary);

    Rect card{s * 32, s * 130, static_cast<float>(width_) - s * 64, s * 260};
    r.FillRect(card, theme_.layer);
    r.StrokeRect(card, theme_.border, 1.f);

    durationMode_.Draw(r, text, theme_);
    text.Draw(r, L"Quick pick (1 min \u2013 1 day)", s * 48, s * 180, carbon::kLabel01, theme_.textSecondary);
    for (auto& chip : durationChips_) chip->Draw(r, text, theme_);
    stepDuration_.Draw(r, text, theme_);

    btnOpenPicker_.Draw(r, text, theme_);

    if (config_.durationMode == SessionDurationMode::UntilRestart) {
        text.Draw(r, L"Block lasts until you restart the computer",
                  s * 48, s * 370, carbon::kHelper01, theme_.textSecondary);
    } else if (config_.durationMode == SessionDurationMode::UntilDateTime) {
        wchar_t buf[128];
        swprintf(buf, 128, L"Stops at %04d-%02d-%02d %02d:%02d (local)",
                 pickYear_, pickMonth_, pickDay_, pickHour_, pickMinute_);
        text.Draw(r, buf, s * 48, s * 370, carbon::kBody01, theme_.textPrimary);
    } else {
        const std::wstring line = L"Lasts " + FormatDuration(config_.timerMinutes.count()) + L"  \u2022  ends at " +
                                  FormatLocal(std::chrono::system_clock::now() + config_.timerMinutes);
        text.Draw(r, line, s * 48, s * 370, carbon::kBody01, theme_.textPrimary);
    }

    if (showDatePicker_) {
        // Dim overlay + Carbon modal
        r.FillRect({0, 0, static_cast<float>(width_), static_cast<float>(height_)},
                   carbon::Color{0, 0, 0, 0.45f});
        Rect modal{s * 48, s * 120, static_cast<float>(width_) - s * 96, s * 320};
        r.FillRect(modal, theme_.layer);
        r.StrokeRect(modal, theme_.borderStrong, 1.f);
        text.Draw(r, L"Stop date & time", s * 72, s * 140, carbon::kHeading05, theme_.textPrimary);
        text.Draw(r, L"No upper limit \u2013 pick any future date (open-source infinity)",
                  s * 72, s * 168, carbon::kHelper01, theme_.textSecondary);
        stepYear_.Draw(r, text, theme_);
        stepMonth_.Draw(r, text, theme_);
        stepDay_.Draw(r, text, theme_);
        stepHour_.Draw(r, text, theme_);
        stepMinute_.Draw(r, text, theme_);
        btnPickerOk_.Draw(r, text, theme_);
        btnPickerCancel_.Draw(r, text, theme_);
    }
}

void WizardApp::DrawBreaks(Renderer& r, TextRenderer& text) {
    const float s = dpiScale_;
    if (icons_) icons_->Draw(r, IconId::Hourglass, {s * 48, s * 72, s * 22, s * 22}, theme_.textPrimary);
    text.Draw(r, L"Breaks", s * 78, s * 72, carbon::kHeading03, theme_.textPrimary);
    text.Draw(r, L"All apps on your computer are allowed during breaks",
              s * 48, s * 108, carbon::kHelper01, theme_.textSecondary);

    Rect card{s * 32, s * 130, static_cast<float>(width_) - s * 64, s * 200};
    r.FillRect(card, theme_.layer);
    r.StrokeRect(card, theme_.border, 1.f);

    breakMode_.Draw(r, text, theme_);
    if (config_.breakMode == SessionBreakMode::Pomodoro) {
        stepPomodoroWork_.Draw(r, text, theme_);
        stepPomodoroBreak_.Draw(r, text, theme_);
        wchar_t buf[128];
        swprintf(buf, 128, L"Block for %d minutes, then allow a %d minute break.",
                 config_.pomodoroBlockMin, config_.pomodoroBreakMin);
        text.Draw(r, buf, s * 48, s * 280, carbon::kBody01, theme_.textPrimary);
    } else if (config_.breakMode == SessionBreakMode::Allowance) {
        stepAllowance_.Draw(r, text, theme_);
        wchar_t buf[128];
        swprintf(buf, 128, L"%d minutes of allowance before the block starts.",
                 config_.allowanceMinutes);
        text.Draw(r, buf, s * 48, s * 280, carbon::kBody01, theme_.textPrimary);
    } else {
        text.Draw(r, L"No breaks \u2013 continuous block until the timer ends.",
                  s * 48, s * 200, carbon::kBody01, theme_.textPrimary);
    }
}

void WizardApp::DrawStart(Renderer& r, TextRenderer& text) {
    const float s = dpiScale_;
    const float W = static_cast<float>(width_);
    text.Draw(r, L"Ready to lock?", s * 48, s * 70, carbon::kHeading03, theme_.textPrimary);
    text.Draw(r, L"Review your session. Everything not allowed below will be closed.",
              s * 48, s * 106, carbon::kHelper01, theme_.textSecondary);

    // Summary card: label column + value column
    std::vector<std::pair<std::wstring, std::wstring>> rows;
    {
        std::vector<std::wstring> names;
        for (const auto& e : config_.whitelist) if (e.checked) names.push_back(e.name);
        std::wstring v = std::to_wstring(names.size()) + (names.size() == 1 ? L" app: " : L" apps: ");
        for (size_t i = 0; i < names.size() && i < 3; ++i) v += (i ? L", " : L"") + names[i];
        if (names.size() > 3) v += L"  + " + std::to_wstring(names.size() - 3) + L" more";
        rows.emplace_back(L"Allowed apps", v + L"  (and Displon)");
    }
    {
        std::wstring v;
        if (config_.durationMode == SessionDurationMode::UntilRestart) {
            v = L"Until you restart the computer";
        } else {
            const auto end = config_.durationMode == SessionDurationMode::UntilDateTime
                                 ? config_.unlockTime
                                 : std::chrono::system_clock::now() + config_.timerMinutes;
            const auto mins = std::chrono::duration_cast<std::chrono::minutes>(end - std::chrono::system_clock::now()).count();
            v = FormatDuration(std::max<long long>(1, mins)) + L"  •  ends at " + FormatLocal(end);
        }
        rows.emplace_back(L"Duration", v);
    }
    {
        wchar_t b[96];
        if (config_.breakMode == SessionBreakMode::Pomodoro) swprintf(b, 96, L"Pomodoro: %d min block, %d min break", config_.pomodoroBlockMin, config_.pomodoroBreakMin);
        else if (config_.breakMode == SessionBreakMode::Allowance) swprintf(b, 96, L"%d min free before the block starts", config_.allowanceMinutes);
        else swprintf(b, 96, L"None - continuous block");
        rows.emplace_back(L"Breaks", b);
    }
    {
        std::wstring v;
        if (config_.blockTaskManager) v = L"Task Manager";
        if (config_.blockWindowsSettings) v += (v.empty() ? L"" : L", ") + std::wstring(L"Windows Settings");
        rows.emplace_back(L"Also blocked", v.empty() ? L"Nothing extra" : v);
    }

    const float rowH = s * 48, labelW = s * 170;
    Rect card{s * 32, s * 136, W - s * 64, rowH * rows.size()};
    r.FillRect(card, theme_.layer);
    r.StrokeRect(card, theme_.border, 1.f);
    for (size_t i = 0; i < rows.size(); ++i) {
        const float y = card.y + rowH * i;
        if (i) r.FillRect({card.x + s * 16, y, card.w - s * 32, 1.f}, theme_.border);
        const float ty = y + (rowH - text.LineHeight(carbon::kBody01)) * 0.5f;
        text.Draw(r, rows[i].first, card.x + s * 16, ty, carbon::kLabel01, theme_.textSecondary);
        text.Draw(r, FitText(text, rows[i].second, carbon::kBody01, card.w - labelW - s * 40), card.x + s * 16 + labelW, ty,
                  carbon::kBody01, theme_.textPrimary);
    }

    // Inline notification (Carbon): red bar + tinted surface.
    const float ny = card.y + card.h + s * 24;
    const Rect note{s * 32, ny, W - s * 64, s * 56};
    r.FillRect(note, theme_.layerAccent);
    r.FillRect({note.x, note.y, s * 4, note.h}, theme_.danger);
    text.Draw(r, L"Once you start, you can't go back.", note.x + s * 20, note.y + s * 8, carbon::kBody01, theme_.textPrimary);
    text.Draw(r, L"The lock keeps running if you close this window or restart the computer.",
              note.x + s * 20, note.y + s * 30, carbon::kHelper01, theme_.textSecondary);
    if (!statusMsg_.empty())
        text.Draw(r, FitText(text, statusMsg_, carbon::kBody01, W - s * 64), s * 32, note.y + note.h + s * 20,
                  carbon::kBody01, theme_.danger);
}

void WizardApp::DrawCountdown(Renderer& r, TextRenderer& text) {
    const float s = dpiScale_;
    const float W = static_cast<float>(width_), H = static_cast<float>(height_);
    r.FillRect({0, 0, W, H}, theme_.background);

    // ---- header strip with status tag
    r.FillRect({0, 0, W, s * 48}, theme_.layer);
    r.FillRect({0, s * 48, W, 1.f}, theme_.border);
    if (icons_) icons_->Draw(r, IconId::App, {s * 12, s * 10, s * 28, s * 28}, theme_.accent);
    text.Draw(r, L"Displon", s * 48, s * 14, carbon::kHeading05, theme_.textPrimary);
    {
        const std::wstring tag = sessionEnded_ ? L"BLOCK ENDED" : L"LOCKED";
        DrawTag(r, text, tag, W - s * 24 - TagWidth(text, tag, s), s * 12, s,
                sessionEnded_ ? theme_.success : theme_.accent, theme_.textOnColor);
    }

    const auto now = std::chrono::system_clock::now();
    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(config_.unlockTime - now);
    if (remaining.count() < 0) remaining = std::chrono::seconds(0);
    const bool untilRestart = config_.durationMode == SessionDurationMode::UntilRestart;
    const bool ended = sessionEnded_;

    // ---- icon + big time
    float y = s * 76;
    if (icons_)
        icons_->Draw(r, ended ? IconId::Check : IconId::Lock, {(W - s * 36) * 0.5f, y, s * 36, s * 36},
                     ended ? theme_.success : theme_.accent);
    y += s * 50;

    if (untilRestart) {
        const std::wstring t = L"Until restart";
        const float tw = text.MeasureWidth(t, carbon::kDisplay01);
        text.Draw(r, t, (W - tw) * 0.5f, y, carbon::kDisplay01, theme_.textPrimary);
    } else {
        const long long total = remaining.count();
        wchar_t buf[32];
        swprintf(buf, 32, L"%02lld:%02lld:%02lld", total / 3600, (total / 60) % 60, total % 60);
        DrawDigits(r, text, buf, W * 0.5f, y, carbon::kDisplay01, ended ? theme_.success : theme_.textPrimary);
    }
    y += s * 86;
    {
        const std::wstring cap = ended ? L"You're free to use your apps again."
                                       : untilRestart ? L"The lock ends when this computer restarts."
                                                      : L"remaining";
        const float cw = text.MeasureWidth(cap, carbon::kBody01);
        text.Draw(r, cap, (W - cw) * 0.5f, y, carbon::kBody01, theme_.textSecondary);
    }
    y += s * 34;

    // ---- progress (timed sessions) with start / unlock labels
    if (!untilRestart && lockStart_.time_since_epoch().count() != 0 && config_.unlockTime > lockStart_) {
        const double total = std::chrono::duration<double>(config_.unlockTime - lockStart_).count();
        const double done = std::chrono::duration<double>(now - lockStart_).count();
        const float frac = static_cast<float>(std::min(1.0, std::max(0.0, done / total)));
        const float bw = std::min(W - s * 96, s * 600);
        const Rect bar{(W - bw) * 0.5f, y, bw, s * 4};
        r.FillRect(bar, theme_.layerAccent);
        r.FillRect({bar.x, bar.y, bar.w * frac, bar.h}, ended ? theme_.success : theme_.accent);
        const std::wstring a = L"Started " + FormatLocal(lockStart_), b = L"Unlocks " + FormatLocal(config_.unlockTime);
        text.Draw(r, a, bar.x, bar.y + s * 10, carbon::kHelper01, theme_.textSecondary);
        text.Draw(r, b, bar.x + bar.w - text.MeasureWidth(b, carbon::kHelper01), bar.y + s * 10, carbon::kHelper01,
                  theme_.textSecondary);
    }
    y += s * 48;

    // ---- two cards: allowed apps / blocked this session
    const float colW = std::min(W - s * 64, s * 900);
    const float gap = s * 16, cardW = (colW - gap) * 0.5f, x0 = (W - colW) * 0.5f;
    const float bottom = H - s * (ended ? 104 : 56);
    const float cardH = std::max(s * 120, bottom - y);
    const Rect left{x0, y, cardW, cardH}, right{x0 + cardW + gap, y, cardW, cardH};
    const float pad = s * 16, headH = s * 44;

    auto cardFrame = [&](const Rect& c, const std::wstring& title, const std::wstring& count,
                         const carbon::Color& countColor) {
        r.FillRect(c, theme_.layer);
        r.StrokeRect(c, theme_.border, 1.f);
        text.Draw(r, title, c.x + pad, c.y + (headH - text.LineHeight(carbon::kBody01)) * 0.5f, carbon::kBody01,
                  theme_.textPrimary);
        text.Draw(r, count, c.x + c.w - pad - text.MeasureWidth(count, carbon::kBody01),
                  c.y + (headH - text.LineHeight(carbon::kBody01)) * 0.5f, carbon::kBody01, countColor);
        r.FillRect({c.x, c.y + headH, c.w, 1.f}, theme_.border);
    };

    // Allowed apps as tags (Displon first, it is always allowed)
    {
        std::vector<std::wstring> names;
        for (const auto& e : config_.whitelist) if (e.checked) names.push_back(e.name);
        cardFrame(left, L"Allowed apps", std::to_wstring(names.size()), theme_.textSecondary);
        const float inner = left.w - pad * 2, th = s * 24, rowGap = s * 8;
        float cx = left.x + pad, cy = left.y + headH + pad;
        const float maxY = left.y + left.h - pad - th;
        auto place = [&](const std::wstring& label, const carbon::Color& bg, const carbon::Color& fg) {
            const std::wstring fit = FitText(text, label, carbon::kLabel01, inner - s * 16);
            const float w = TagWidth(text, fit, s);
            if (cx + w > left.x + pad + inner) { cx = left.x + pad; cy += th + rowGap; }
            if (cy > maxY) return false;
            DrawTag(r, text, fit, cx, cy, s, bg, fg);
            cx += w + s * 8;
            return true;
        };
        place(L"Displon", theme_.accent, theme_.textOnColor);
        size_t shown = 0;
        for (; shown < names.size(); ++shown)
            if (!place(names[shown], theme_.layerAccent, theme_.textPrimary)) break;
        if (shown < names.size()) {
            const std::wstring more = L"+" + std::to_wstring(names.size() - shown) + L" more";
            text.Draw(r, more, left.x + pad, left.y + left.h - pad - text.LineHeight(carbon::kHelper01), carbon::kHelper01,
                      theme_.textSecondary);
        }
    }

    // Blocked this session
    {
        cardFrame(right, L"Blocked this session", std::to_wstring(blockedTotal_),
                  blockedTotal_ > 0 ? theme_.danger : theme_.textSecondary);
        float ry = right.y + headH + pad;
        if (blocked_.empty()) {
            text.Draw(r, L"Nothing blocked yet.", right.x + pad, ry, carbon::kBody01, theme_.textPrimary);
            text.Draw(r, L"Apps that are not allowed are closed the moment they open.", right.x + pad,
                      ry + s * 26, carbon::kHelper01, theme_.textSecondary);
        } else {
            const float rowH = text.LineHeight(carbon::kBody01) + s * 8;
            const float maxY = right.y + right.h - pad - rowH;
            for (auto it = blocked_.rbegin(); it != blocked_.rend() && ry <= maxY; ++it, ry += rowH) {
                const std::tm tmv = LocalTm(it->at);
                wchar_t tbuf[16];
                swprintf(tbuf, 16, L"%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
                r.FillRect({right.x + pad, ry + s * 3, s * 3, rowH - s * 14}, theme_.danger);
                text.Draw(r, tbuf, right.x + pad + s * 12, ry, carbon::kHelper01, theme_.textSecondary);
                const float nx = right.x + pad + s * 12 + s * 76;
                text.Draw(r, FitText(text, it->name, carbon::kBody01, right.x + right.w - pad - nx), nx, ry,
                          carbon::kBody01, theme_.textPrimary);
            }
        }
    }

    // ---- footer hint / new-block button
    if (ended) {
        primaryBtn_.Draw(r, text, theme_);
    } else {
        const std::wstring hint = L"You can minimize this window. The lock keeps running, even after a restart.";
        const float hw = text.MeasureWidth(hint, carbon::kHelper01);
        text.Draw(r, hint, (W - hw) * 0.5f, H - s * 40, carbon::kHelper01, theme_.textSecondary);
    }
}

void WizardApp::Draw(Renderer& r, TextRenderer& text) {
    r.BeginFrame(width_, height_, theme_.background);
    if (step_ == WizardStep::Countdown) {
        DrawCountdown(r, text);
    } else {
        DrawChrome(r, text);
        DrawTopNav(r, text);
        switch (step_) {
        case WizardStep::Settings: DrawSettings(r, text); break;
        case WizardStep::Whitelist: DrawWhitelist(r, text); break;
        case WizardStep::Duration: DrawDuration(r, text); break;
        case WizardStep::Breaks: DrawBreaks(r, text); break;
        case WizardStep::Start: DrawStart(r, text); break;
        default: break;
        }
        secondaryBtn_.Draw(r, text, theme_);
        primaryBtn_.Draw(r, text, theme_);
    }
    r.EndFrame();
}

void WizardApp::OnPointer(PointerState s, float x, float y) {
    for (auto* c : hitTargets_) {
        if (c->HitTest(x, y) || s == PointerState::Leave) {
            c->OnPointer(s, x, y);
        } else if (s == PointerState::Hover) {
            c->OnPointer(PointerState::Leave, x, y);
        }
    }
}

void WizardApp::OnKey(const KeyEvent& k) {
    for (auto* c : hitTargets_) {
        if (c->Focused()) c->OnKey(k);
    }
}

void WizardApp::OnAddRecentlyOpened() {
#ifdef _WIN32
    if (sessionLocked_) return;
    const auto apps = EnumerateRunningApps(48);
    for (const auto& a : apps) {
        bool exists = false;
        for (const auto& e : config_.whitelist) {
            if (e.name == a.name || (!a.path.empty() && e.path == a.path)) {
                exists = true;
                break;
            }
        }
        if (!exists) config_.whitelist.push_back({a.name, a.path, true});
    }
    BuildStepControls();
#endif
}

void WizardApp::OnAddExeFile() {
#ifdef _WIN32
    if (sessionLocked_) return;
    const std::wstring path = PickExecutableFile(ownerHwnd_);
    if (path.empty()) return;
    std::wstring name = path;
    const auto slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name = path.substr(slash + 1);
    for (const auto& e : config_.whitelist) {
        if (e.path == path || e.name == name) return;
    }
    config_.whitelist.push_back({name, path, true});
    BuildStepControls();
#endif
}

void WizardApp::OnImportWhitelist() {
#ifdef _WIN32
    if (sessionLocked_) return;
    const std::wstring path = PickOpenTextFile(ownerHwnd_);
    if (path.empty()) return;
    std::vector<std::pair<std::wstring, std::wstring>> items;
    if (!ImportWhitelistFile(path, items)) return;
    for (const auto& [name, pth] : items) {
        bool exists = false;
        for (const auto& e : config_.whitelist) {
            if (e.path == pth || e.name == name) {
                exists = true;
                break;
            }
        }
        if (!exists) config_.whitelist.push_back({name, pth, true});
    }
    BuildStepControls();
#endif
}

void WizardApp::OnExportWhitelist() {
#ifdef _WIN32
    const std::wstring path = PickSaveTextFile(ownerHwnd_);
    if (path.empty()) return;
    std::vector<std::pair<std::wstring, std::wstring>> items;
    for (const auto& e : config_.whitelist) {
        if (e.checked) items.emplace_back(e.name, e.path.empty() ? e.name : e.path);
    }
    ExportWhitelistFile(path, items);
#endif
}
