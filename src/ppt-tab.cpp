// ppt-tab.cpp  v2.0
// Presentation tab — three capture sections (PowerPoint, Google Slides, Canva).
// Each section lets the user pick any visible window as the capture target.
// The plugin creates an OBS window_capture source per enabled section so the
// user can add "OBS Lyrics - PowerPoint" (etc.) to their scene.
// Navigation: PowerPoint uses COM automation; Google Slides and Canva use
// keystroke injection to the selected window handle.

#include "ppt-tab.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileInfo>
#include <QTimer>
#include <QApplication>

#include <oleauto.h>            // IDispatch, VARIANT helpers, GetActiveObject
#include <psapi.h>              // QueryFullProcessImageNameW, EnumProcesses
#include <obs-frontend-api.h>   // obs_frontend_get_current_scene

// ── Shared button style ───────────────────────────────────────

static const char *kBtnStyle =
    "QPushButton{background:#1a3a5c;color:#4da6ff;border:1px solid #2a5a8c;"
    "border-radius:4px;font-weight:bold;}"
    "QPushButton:hover{background:#254e7a;}"
    "QPushButton:disabled{background:#1a1a1a;color:#444;border-color:#333;}";

static const char *kShowActiveStyle =
    "QPushButton{background:#1a4a1a;color:#66cc66;"
    "border:1px solid #2a7a2a;border-radius:4px;font-weight:bold;}"
    "QPushButton:hover{background:#256025;}";

static const char *kShowInactiveStyle =
    "QPushButton{background:#1a3a5c;color:#4da6ff;border:1px solid #2a5a8c;"
    "border-radius:4px;font-weight:bold;}"
    "QPushButton:hover{background:#254e7a;}"
    "QPushButton:disabled{background:#1a1a1a;color:#444;border-color:#333;}";

static const char *kComboStyle =
    "QComboBox{background:#1e1e1e;color:#eee;border:1px solid #444;"
    "border-radius:3px;padding:3px 6px;}"
    "QComboBox::drop-down{border:none;}"
    "QComboBox QAbstractItemView{background:#1e1e1e;color:#eee;"
    "selection-background-color:#1e4a7a;}";

// ── COM late-binding helpers ──────────────────────────────────

static HRESULT pptId(IDispatch *p, LPCWSTR name, DISPID &id)
{
    if (!p) return E_POINTER;
    LPWSTR mn = const_cast<LPWSTR>(name);
    return p->GetIDsOfNames(IID_NULL, &mn, 1, LOCALE_USER_DEFAULT, &id);
}

static IDispatch *pptObj(IDispatch *p, LPCWSTR name)
{
    if (!p) return nullptr;
    DISPID id; if (FAILED(pptId(p, name, id))) return nullptr;
    DISPPARAMS dp{}; VARIANT v; VariantInit(&v);
    HRESULT hr = p->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT,
                           DISPATCH_PROPERTYGET, &dp, &v, nullptr, nullptr);
    if (FAILED(hr) || V_VT(&v) != VT_DISPATCH) { VariantClear(&v); return nullptr; }
    return V_DISPATCH(&v);
}

static int pptInt(IDispatch *p, LPCWSTR name, int def = 0)
{
    if (!p) return def;
    DISPID id; if (FAILED(pptId(p, name, id))) return def;
    DISPPARAMS dp{}; VARIANT v; VariantInit(&v);
    if (FAILED(p->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT,
               DISPATCH_PROPERTYGET, &dp, &v, nullptr, nullptr))) return def;
    VARIANT vi; VariantInit(&vi); int r = def;
    if (SUCCEEDED(VariantChangeType(&vi, &v, 0, VT_I4))) r = V_I4(&vi);
    VariantClear(&vi); VariantClear(&v);
    return r;
}

static HRESULT pptCall(IDispatch *p, LPCWSTR name)
{
    if (!p) return E_POINTER;
    DISPID id; HRESULT hr = pptId(p, name, id);
    if (FAILED(hr)) return hr;
    DISPPARAMS dp{};
    return p->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT,
                     DISPATCH_METHOD, &dp, nullptr, nullptr, nullptr);
}

static HRESULT pptCallInt(IDispatch *p, LPCWSTR name, int arg)
{
    if (!p) return E_POINTER;
    DISPID id; HRESULT hr = pptId(p, name, id);
    if (FAILED(hr)) return hr;
    VARIANT va; VariantInit(&va); V_VT(&va) = VT_I4; V_I4(&va) = arg;
    DISPPARAMS dp{&va, nullptr, 1, 0};
    return p->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT,
                     DISPATCH_METHOD, &dp, nullptr, nullptr, nullptr);
}

static bool isPowerPointRunning()
{
    DWORD pids[1024]; DWORD needed = 0;
    if (!EnumProcesses(pids, sizeof(pids), &needed)) return false;
    DWORD count = needed / sizeof(DWORD);
    for (DWORD i = 0; i < count; ++i) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
        if (!h) continue;
        wchar_t name[MAX_PATH] = {}; DWORD len = MAX_PATH; bool match = false;
        if (QueryFullProcessImageNameW(h, 0, name, &len)) {
            QString exe = QFileInfo(QString::fromWCharArray(name)).fileName();
            match = (exe.compare("POWERPNT.EXE", Qt::CaseInsensitive) == 0);
        }
        CloseHandle(h);
        if (match) return true;
    }
    return false;
}

// Returns the COM IDispatch* for the first PowerPoint presentation that
// currently has a SlideShowWindow open, or nullptr.  Caller must Release().
static IDispatch *getPptActiveView()
{
    if (!isPowerPointRunning()) return nullptr;
    CLSID clsid = {};
    if (FAILED(CLSIDFromProgID(L"PowerPoint.Application", &clsid))) return nullptr;
    IUnknown *unk = nullptr;
    if (FAILED(GetActiveObject(clsid, nullptr, &unk)) || !unk) return nullptr;
    IDispatch *app = nullptr;
    HRESULT hr = unk->QueryInterface(IID_IDispatch, (void **)&app);
    unk->Release();
    if (FAILED(hr) || !app) return nullptr;
    IDispatch *presColl = pptObj(app, L"Presentations");
    app->Release();
    if (!presColl) return nullptr;
    int count = pptInt(presColl, L"Count");
    IDispatch *result = nullptr;
    for (int i = 1; i <= count && !result; ++i) {
        DISPID idItem = 0;
        wchar_t itemBuf[] = L"Item"; LPWSTR itemPtr = itemBuf;
        if (FAILED(presColl->GetIDsOfNames(IID_NULL, &itemPtr, 1,
                                           LOCALE_USER_DEFAULT, &idItem)))
            continue;
        VARIANT vArg; VariantInit(&vArg); V_VT(&vArg) = VT_I4; V_I4(&vArg) = i;
        DISPPARAMS dp{ &vArg, nullptr, 1, 0 };
        VARIANT vRes; VariantInit(&vRes);
        hr = presColl->Invoke(idItem, IID_NULL, LOCALE_USER_DEFAULT,
                              DISPATCH_METHOD | DISPATCH_PROPERTYGET,
                              &dp, &vRes, nullptr, nullptr);
        if (SUCCEEDED(hr) && V_VT(&vRes) == VT_DISPATCH && V_DISPATCH(&vRes)) {
            IDispatch *pres = V_DISPATCH(&vRes); V_VT(&vRes) = VT_EMPTY;
            VariantClear(&vRes);
            IDispatch *ssWin = pptObj(pres, L"SlideShowWindow");
            if (ssWin) {
                result = pptObj(ssWin, L"View");
                ssWin->Release();
            }
            pres->Release();
        } else {
            VariantClear(&vRes);
        }
    }
    presColl->Release();
    return result;
}

// ── Window enumeration ────────────────────────────────────────

struct EnumWinCtx { QList<WinInfo> *list; };

static BOOL CALLBACK enumWinCb(HWND hwnd, LPARAM lp)
{
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindowLong(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, 511);
    if (!title[0]) return TRUE;
    wchar_t cls[256] = {};
    GetClassNameW(hwnd, cls, 255);
    QString exeName;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h) {
            wchar_t exePath[MAX_PATH] = {}; DWORD len = MAX_PATH;
            if (QueryFullProcessImageNameW(h, 0, exePath, &len))
                exeName = QFileInfo(QString::fromWCharArray(exePath)).fileName();
            CloseHandle(h);
        }
    }
    auto *ctx = reinterpret_cast<EnumWinCtx *>(lp);
    WinInfo wi;
    wi.hwnd  = hwnd;
    wi.title = QString::fromWCharArray(title);
    wi.cls   = QString::fromWCharArray(cls);
    wi.exe   = exeName;
    ctx->list->append(wi);
    return TRUE;
}

// ── OBS source names per section ─────────────────────────────

static const char *kSourceName[3] = {
    "SSO - PowerPoint",
    "SSO - Google Slides",
    "SSO - Canva"
};

// Legacy names from before the SSO v2 rename — checked as fallback so
// existing OBS scenes keep working without manual source replacement.
static const char *kSourceNameLegacy[3] = {
    "OBS Lyrics - PowerPoint",
    "OBS Lyrics - Google Slides",
    "OBS Lyrics - Canva"
};

// ══════════════════════════════════════════════════════════════
// PptTab
// ══════════════════════════════════════════════════════════════

PptTab::PptTab(QWidget *parent) : QWidget(parent)
{
    buildUi();
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(500);
    connect(m_pollTimer, &QTimer::timeout, this, &PptTab::onPollTimer);
    m_pollTimer->start();
}

PptTab::~PptTab()
{
    for (int t = 0; t < kTypes; ++t)
        releaseCaptureSource(t);
}

// ── UI construction ───────────────────────────────────────────

void PptTab::buildUi()
{
    static const char *kLabels[kTypes] = {
        "📊  PowerPoint",
        "🌐  Google Slides",
        "🎨  Canva"
    };
    static const char *kPlaceholder[kTypes] = {
        "Select PowerPoint window…",
        "Select browser window with Google Slides…",
        "Select browser window with Canva…"
    };

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    for (int t = 0; t < kTypes; ++t) {
        PresSection &sec = m_sections[t];

        sec.group = new QGroupBox(kLabels[t]);
        sec.group->setCheckable(true);
        sec.group->setChecked(false);
        sec.group->setStyleSheet(
            "QGroupBox{color:#ccc;border:1px solid #444;border-radius:5px;"
            "margin-top:6px;padding-top:4px;font-weight:bold;}"
            "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 4px;}"
            "QGroupBox::indicator{width:14px;height:14px;}"
            "QGroupBox::indicator:unchecked{border:1px solid #555;border-radius:3px;"
            "background:#222;}"
            "QGroupBox::indicator:checked{border:1px solid #4da6ff;border-radius:3px;"
            "background:#1a3a5c;}");

        auto *gl = new QVBoxLayout(sec.group);
        gl->setContentsMargins(8, 6, 8, 8);
        gl->setSpacing(6);

        // Window selector row
        auto *winRow = new QHBoxLayout;
        sec.winCombo = new QComboBox;
        sec.winCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sec.winCombo->setStyleSheet(kComboStyle);
        sec.winCombo->addItem(kPlaceholder[t]);

        sec.scanBtn = new QPushButton("⟳ Scan");
        sec.scanBtn->setFixedWidth(72);
        sec.scanBtn->setStyleSheet(kBtnStyle);

        winRow->addWidget(sec.winCombo, 1);
        winRow->addWidget(sec.scanBtn);
        gl->addLayout(winRow);

        // Info + show button row
        auto *btnRow = new QHBoxLayout;
        QLabel *srcLbl = new QLabel(
            QString("OBS source: <span style='color:#4da6ff;'>%1</span>")
            .arg(kSourceName[t]));
        srcLbl->setStyleSheet("color:#666;font-size:10px;");
        srcLbl->setTextFormat(Qt::RichText);

        sec.showBtn = new QPushButton("▶ Show Slides");
        sec.showBtn->setFixedWidth(120);
        sec.showBtn->setStyleSheet(kShowInactiveStyle);

        btnRow->addWidget(srcLbl, 1);
        btnRow->addWidget(sec.showBtn);
        gl->addLayout(btnRow);

        root->addWidget(sec.group);

        // Connect signals — capture t by value
        const int ct = t;
        connect(sec.scanBtn, &QPushButton::clicked, this,
                [this, ct]() { onScanWindows(ct); });
        connect(sec.showBtn, &QPushButton::clicked, this,
                [this, ct]() { onShowSlides(ct); });

        // When the group is unchecked, deactivate if it was showing
        connect(sec.group, &QGroupBox::toggled, this, [this, ct](bool on) {
            if (!on && m_activeType == ct)
                deactivatePresentation();
            if (!on)
                releaseCaptureSource(ct);
            emit typeEnabledChanged(ct, on);
        });
    }

    // ── Navigation ────────────────────────────────────────────
    auto *navGroup  = new QGroupBox("Navigation");
    navGroup->setStyleSheet(
        "QGroupBox{color:#ccc;border:1px solid #444;border-radius:5px;"
        "margin-top:6px;padding-top:4px;font-weight:bold;}"
        "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 4px;}");
    auto *navLayout = new QVBoxLayout(navGroup);
    navLayout->setContentsMargins(8, 6, 8, 8);
    navLayout->setSpacing(6);

    m_navStatusLbl = new QLabel("Now showing: None");
    m_navStatusLbl->setAlignment(Qt::AlignCenter);
    m_navStatusLbl->setStyleSheet("color:#555;font-size:10px;");
    navLayout->addWidget(m_navStatusLbl);

    auto *navRow = new QHBoxLayout;
    m_prevBtn = new QPushButton("◄ Prev");
    m_prevBtn->setFixedHeight(36);
    m_prevBtn->setStyleSheet(kBtnStyle);
    m_prevBtn->setEnabled(false);

    m_slideCtr = new QLabel("— / —");
    m_slideCtr->setAlignment(Qt::AlignCenter);
    m_slideCtr->setMinimumWidth(80);
    m_slideCtr->setStyleSheet("color:#ccc;font-size:13px;font-weight:bold;");

    m_nextBtn = new QPushButton("Next ►");
    m_nextBtn->setFixedHeight(36);
    m_nextBtn->setStyleSheet(kBtnStyle);
    m_nextBtn->setEnabled(false);

    navRow->addWidget(m_prevBtn, 1);
    navRow->addWidget(m_slideCtr);
    navRow->addWidget(m_nextBtn, 1);
    navLayout->addLayout(navRow);

    auto *gotoRow = new QHBoxLayout;
    auto *gotoLbl = new QLabel("Go to slide:");
    gotoLbl->setStyleSheet("color:#aaa;font-size:9px;");
    m_gotoSpin = new QSpinBox;
    m_gotoSpin->setMinimum(1);
    m_gotoSpin->setMaximum(999);
    m_gotoSpin->setFixedWidth(64);
    m_gotoSpin->setEnabled(false);
    m_gotoSpin->setStyleSheet(
        "QSpinBox{background:#1e1e1e;color:#eee;border:1px solid #444;"
        "border-radius:3px;padding:2px 4px;}");
    m_gotoBtn = new QPushButton("Go");
    m_gotoBtn->setFixedWidth(48);
    m_gotoBtn->setEnabled(false);
    m_gotoBtn->setStyleSheet(
        "QPushButton{background:#2a2a2a;color:#aaa;border:1px solid #444;"
        "border-radius:3px;}"
        "QPushButton:hover{background:#333;}"
        "QPushButton:disabled{background:#1a1a1a;color:#444;border-color:#333;}");
    gotoRow->addWidget(gotoLbl);
    gotoRow->addWidget(m_gotoSpin);
    gotoRow->addWidget(m_gotoBtn);
    gotoRow->addStretch();
    navLayout->addLayout(gotoRow);

    root->addWidget(navGroup);
    root->addStretch();

    connect(m_prevBtn, &QPushButton::clicked, this, &PptTab::onPrev);
    connect(m_nextBtn, &QPushButton::clicked, this, &PptTab::onNext);
    connect(m_gotoBtn, &QPushButton::clicked, this, &PptTab::onGoto);
}

// ── Window scanning ───────────────────────────────────────────

void PptTab::scanWindows(int type)
{
    PresSection &sec = m_sections[type];
    QList<WinInfo> wins;
    EnumWinCtx ctx{ &wins };
    EnumWindows(enumWinCb, reinterpret_cast<LPARAM>(&ctx));
    sec.windows = wins;

    QString prevText = sec.winCombo->currentIndex() > 0
                       ? sec.winCombo->currentText() : QString();
    QSignalBlocker blk(sec.winCombo);
    sec.winCombo->clear();
    sec.winCombo->addItem("(Select window…)");
    for (const auto &wi : sec.windows) {
        QString label = wi.title;
        if (!wi.exe.isEmpty())
            label += "  —  " + wi.exe;
        sec.winCombo->addItem(label);
        sec.winCombo->setItemData(sec.winCombo->count() - 1,
                                  wi.title, Qt::ToolTipRole);
    }
    if (!prevText.isEmpty()) {
        int idx = sec.winCombo->findText(prevText);
        if (idx >= 0) sec.winCombo->setCurrentIndex(idx);
    }
    autoSelectWindow(type); // auto-pick if still on placeholder
}

void PptTab::onScanWindows(int type) { scanWindows(type); }

void PptTab::autoSelectWindow(int type)
{
    PresSection &sec = m_sections[type];
    if (sec.winCombo->currentIndex() > 0) return; // already has a selection

    int bestIdx = -1;
    for (int i = 0; i < sec.windows.size(); ++i) {
        const WinInfo &wi = sec.windows[i];
        if (type == 0) {
            // Prefer the actual slide show window (title starts with
            // "PowerPoint Slide Show") over any other POWERPNT.EXE window.
            if (wi.title.startsWith("PowerPoint Slide Show", Qt::CaseInsensitive)) {
                bestIdx = i;
                break; // best possible match — stop
            }
            if (wi.exe.compare("POWERPNT.EXE", Qt::CaseInsensitive) == 0 && bestIdx < 0)
                bestIdx = i; // fallback: any PPT window
        } else if (type == 1) {
            if (wi.title.contains("Google Slides", Qt::CaseInsensitive)) { bestIdx = i; break; }
        } else if (type == 2) {
            if (wi.title.contains("Canva", Qt::CaseInsensitive)) { bestIdx = i; break; }
        }
    }
    if (bestIdx >= 0)
        sec.winCombo->setCurrentIndex(bestIdx + 1); // +1 for placeholder row
}

// ── Activate / deactivate ─────────────────────────────────────

void PptTab::activatePresentation(int type)
{
    int prev = m_activeType;
    m_activeType = type;
    updateCaptureSource(type); // create/update the OBS source
    ensureInScene(type);       // add to current OBS scene (auto-hides others)
    if (prev >= 0 && prev != type)
        hideFromScene(prev);   // explicitly hide the previous type
    updateShowBtnStyles();
    updateNavUi();
    emit showSlidesRequested(type);
}

void PptTab::deactivatePresentation()
{
    int prev = m_activeType;
    m_activeType = -1;
    m_curSlide   = 0;
    m_totSlides  = 0;
    if (prev >= 0) hideFromScene(prev);
    updateShowBtnStyles();
    updateNavUi();
    emit showSlidesRequested(-1);
}

void PptTab::onShowSlides(int type)
{
    if (!m_sections[type].group->isChecked()) return;

    if (m_activeType == type) {
        deactivatePresentation();
    } else {
        activatePresentation(type);
    }
}

// ── OBS capture source management ────────────────────────────

HWND PptTab::selectedHwnd(int type) const
{
    const PresSection &sec = m_sections[type];
    int idx = sec.winCombo->currentIndex() - 1; // -1 for placeholder
    if (idx < 0 || idx >= sec.windows.size()) return nullptr;
    return sec.windows[idx].hwnd;
}

void PptTab::updateCaptureSource(int type)
{
    PresSection &sec = m_sections[type];
    HWND hwnd = selectedHwnd(type);
    if (!hwnd || !IsWindow(hwnd)) return;

    wchar_t title[512] = {}, cls[256] = {};
    GetWindowTextW(hwnd, title, 511);
    GetClassNameW(hwnd, cls, 255);

    QString exeName;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h) {
            wchar_t exePath[MAX_PATH] = {}; DWORD len = MAX_PATH;
            if (QueryFullProcessImageNameW(h, 0, exePath, &len))
                exeName = QFileInfo(QString::fromWCharArray(exePath)).fileName();
            CloseHandle(h);
        }
    }

    // OBS window_capture "window" format: "Title:ClassName:Executable"
    QString winId = QString("%1:%2:%3")
        .arg(QString::fromWCharArray(title))
        .arg(QString::fromWCharArray(cls))
        .arg(exeName);

    if (!sec.capture) {
        // Reuse an existing source if OBS already has one with our name.
        // Without this, obs_source_create would auto-rename duplicates
        // ("PowerPoint 2", "PowerPoint 3", ...) causing one new source to
        // accumulate in the scene every session, eventually crashing OBS
        // at shutdown with a use-after-free in obs_sceneitem_remove_internal.
        sec.capture = obs_get_source_by_name(kSourceName[type]);
        if (!sec.capture)
            sec.capture = obs_get_source_by_name(kSourceNameLegacy[type]);
        if (!sec.capture) {
            obs_data_t *s = obs_data_create();
            obs_data_set_string(s, "window", winId.toUtf8().constData());
            obs_data_set_int(s, "method", 2); // DXGI desktop duplication
            obs_data_set_bool(s, "cursor", false);
            sec.capture = obs_source_create("window_capture",
                                            kSourceName[type], s, nullptr);
            obs_data_release(s);
        }
    }
    if (sec.capture) {
        obs_data_t *s = obs_source_get_settings(sec.capture);
        obs_data_set_string(s, "window", winId.toUtf8().constData());
        obs_source_update(sec.capture, s);
        obs_data_release(s);
    }
}

void PptTab::releaseCaptureSource(int type)
{
    PresSection &sec = m_sections[type];
    if (sec.capture) {
        obs_source_release(sec.capture);
        sec.capture = nullptr;
    }
}

// ── OBS scene integration ─────────────────────────────────────
// Adds the capture source to the current OBS scene (if not already there),
// scales it to fill the canvas, pushes it to the bottom, and hides every
// other presentation capture source in that scene.

void PptTab::ensureInScene(int type)
{
    obs_source_t *cap = m_sections[type].capture;
    if (!cap) return;

    obs_source_t *sceneSrc = obs_frontend_get_current_scene();
    if (!sceneSrc) return;
    obs_scene_t *scene = obs_scene_from_source(sceneSrc); // weak ref

    // Hide every other presentation source and stop their DXGI acquisition
    for (int t = 0; t < kTypes; ++t) {
        if (t == type || !m_sections[t].capture) continue;
        obs_data_t *sd = obs_source_get_settings(m_sections[t].capture);
        obs_data_set_string(sd, "window", "");
        obs_source_update(m_sections[t].capture, sd);
        obs_data_release(sd);
        obs_sceneitem_t *si = obs_scene_find_source(
            scene, obs_source_get_name(m_sections[t].capture));
        if (si) obs_sceneitem_set_visible(si, false);
    }

    // Find or add our source
    obs_sceneitem_t *item = obs_scene_find_source(scene, obs_source_get_name(cap));
    if (item) {
        obs_sceneitem_set_visible(item, true);
    } else {
        item = obs_scene_add(scene, cap);
        if (item) {
            obs_video_info ovi = {};
            obs_get_video_info(&ovi);
            float cw = (ovi.base_width  > 0) ? (float)ovi.base_width  : 1920.0f;
            float ch = (ovi.base_height > 0) ? (float)ovi.base_height : 1080.0f;

            struct obs_transform_info tinfo;
            memset(&tinfo, 0, sizeof(tinfo));
            tinfo.pos.x            = 0.0f;
            tinfo.pos.y            = 0.0f;
            tinfo.alignment        = OBS_ALIGN_TOP | OBS_ALIGN_LEFT;
            tinfo.rot              = 0.0f;
            tinfo.scale.x          = 1.0f;
            tinfo.scale.y          = 1.0f;
            tinfo.bounds_type      = OBS_BOUNDS_SCALE_INNER;
            tinfo.bounds_alignment = OBS_ALIGN_CENTER;
            tinfo.bounds.x         = cw;
            tinfo.bounds.y         = ch;
            obs_sceneitem_set_info2(item, &tinfo);
            obs_sceneitem_set_order(item, OBS_ORDER_MOVE_BOTTOM);
            obs_sceneitem_release(item);
        }
    }

    obs_source_release(sceneSrc);
}

void PptTab::hideFromScene(int type)
{
    obs_source_t *cap = m_sections[type].capture;
    if (!cap) return;

    // Clear the window target so DXGI releases the desktop capture handle.
    // Without this the source keeps capturing even while the scene item is
    // hidden; two sources on the same monitor simultaneously hold DXGI
    // resources and cause device-lost crashes in OBS.
    obs_data_t *s = obs_source_get_settings(cap);
    obs_data_set_string(s, "window", "");
    obs_source_update(cap, s);
    obs_data_release(s);

    obs_source_t *sceneSrc = obs_frontend_get_current_scene();
    if (!sceneSrc) return;
    obs_scene_t *scene = obs_scene_from_source(sceneSrc);
    obs_sceneitem_t *item = obs_scene_find_source(scene, obs_source_get_name(cap));
    if (item) obs_sceneitem_set_visible(item, false);
    obs_source_release(sceneSrc);
}

bool PptTab::isTypeEnabled(int type) const
{
    if (type < 0 || type >= kTypes) return false;
    return m_sections[type].group && m_sections[type].group->isChecked();
}

// ── Remote-triggered activation ───────────────────────────────

void PptTab::requestShow(int type)
{
    if (type < 0) {
        if (m_activeType >= 0) deactivatePresentation();
        return;
    }
    if (type >= kTypes) return;

    if (m_activeType == type) {
        deactivatePresentation();
        return;
    }

    // Auto-enable the section if needed
    if (!m_sections[type].group->isChecked())
        m_sections[type].group->setChecked(true);

    // If no window is selected try an auto-scan so there's something in the list
    if (m_sections[type].winCombo->currentIndex() <= 0)
        scanWindows(type);

    activatePresentation(type);
}

// ── Navigation UI helpers ─────────────────────────────────────

void PptTab::updateNavUi()
{
    bool active = (m_activeType >= 0);
    bool isPpt  = (m_activeType == 0);

    m_prevBtn->setEnabled(active);
    m_nextBtn->setEnabled(active);
    m_gotoSpin->setEnabled(isPpt);
    m_gotoBtn->setEnabled(isPpt);

    if (!active) {
        m_slideCtr->setText("— / —");
        m_navStatusLbl->setText("Now showing: None");
        m_navStatusLbl->setStyleSheet("color:#555;font-size:10px;");
    } else {
        static const char *kNames[kTypes] = {
            "PowerPoint", "Google Slides", "Canva"
        };
        m_navStatusLbl->setText(
            QString("Now showing: <b style='color:#66cc66;'>%1</b>")
            .arg(kNames[m_activeType]));
        m_navStatusLbl->setStyleSheet("color:#aaa;font-size:10px;");
        m_navStatusLbl->setTextFormat(Qt::RichText);
    }
}

void PptTab::updateShowBtnStyles()
{
    for (int t = 0; t < kTypes; ++t) {
        bool isActive = (m_activeType == t);
        m_sections[t].showBtn->setText(isActive ? "■ Showing" : "▶ Show Slides");
        m_sections[t].showBtn->setStyleSheet(
            isActive ? kShowActiveStyle : kShowInactiveStyle);
    }
}

// ── Navigation actions ────────────────────────────────────────

void PptTab::sendKeyToActive(int vkey)
{
    if (m_activeType < 0) return;
    HWND hwnd = selectedHwnd(m_activeType);
    if (!hwnd || !IsWindow(hwnd)) return;

    HWND prevFg = GetForegroundWindow();
    SetForegroundWindow(hwnd);

    INPUT inp[2] = {};
    inp[0].type = inp[1].type = INPUT_KEYBOARD;
    inp[0].ki.wVk = inp[1].ki.wVk = (WORD)vkey;
    inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inp, sizeof(INPUT));

    if (prevFg && prevFg != hwnd) {
        HWND fg = prevFg;
        QTimer::singleShot(150, this, [fg]() { SetForegroundWindow(fg); });
    }
}

IDispatch *PptTab::getPptView() const { return getPptActiveView(); }

void PptTab::nextSlide()
{
    if (m_activeType == 0) {
        IDispatch *v = getPptView();
        if (v) { pptCall(v, L"Next"); v->Release(); return; }
        // COM returned null — PPT not in slideshow mode; fall back to keystroke
    }
    sendKeyToActive(VK_RIGHT);
}

void PptTab::prevSlide()
{
    if (m_activeType == 0) {
        IDispatch *v = getPptView();
        if (v) { pptCall(v, L"Previous"); v->Release(); return; }
        // COM returned null — PPT not in slideshow mode; fall back to keystroke
    }
    sendKeyToActive(VK_LEFT);
}

void PptTab::gotoSlide(int n)
{
    if (n < 1) return;
    if (m_activeType == 0) {
        if (IDispatch *v = getPptView()) { pptCallInt(v, L"GotoSlide", n); v->Release(); }
    }
    // Goto not supported for browser types (no reliable keystroke sequence)
}

void PptTab::onPrev() { prevSlide(); }
void PptTab::onNext() { nextSlide(); }
void PptTab::onGoto()  { gotoSlide(m_gotoSpin->value()); }

// ── Poll timer (PowerPoint COM position read) ─────────────────

void PptTab::onPollTimer()
{
    bool doAutoScan = (++m_pollCount >= 10);
    if (doAutoScan) m_pollCount = 0;

    // Every 5 s: auto-detect windows for enabled sections with no selection
    if (doAutoScan) {
        for (int t = 0; t < kTypes; ++t) {
            if (!m_sections[t].group || !m_sections[t].group->isChecked()) continue;
            if (m_sections[t].winCombo->currentIndex() > 0) continue;
            scanWindows(t);
        }
    }

    if (m_activeType != 0) return; // Only PowerPoint needs COM polling

    IDispatch *view = getPptView();
    if (!view) {
        if (m_curSlide != 0) {
            m_curSlide = 0; m_totSlides = 0;
            m_slideCtr->setText("— / —");
            m_navStatusLbl->setText(
                "Now showing: <b style='color:#66cc66;'>PowerPoint</b> "
                "<span style='color:#888;'>(no slideshow active)</span>");
            m_navStatusLbl->setTextFormat(Qt::RichText);
        }
        return;
    }

    // Need total slides — walk up to the Presentation.Slides.Count via
    // SlideShowWindow.Presentation.Slides.Count chain.
    int cur   = pptInt(view, L"CurrentShowPosition");
    int total = 0;

    // Try to get count from parent Presentation.Slides
    IDispatch *ssWin = pptObj(view, L"Parent"); // SlideShowView.Parent = SlideShowWindow
    if (ssWin) {
        IDispatch *pres = pptObj(ssWin, L"Presentation");
        if (pres) {
            IDispatch *slides = pptObj(pres, L"Slides");
            if (slides) { total = pptInt(slides, L"Count"); slides->Release(); }
            m_presTitle = QString(); // we don't cache name anymore
            pres->Release();
        }
        ssWin->Release();
    }
    view->Release();

    m_slideCtr->setText(QString("%1 / %2").arg(cur)
                                          .arg(total > 0 ? QString::number(total) : QString("?")));
    m_gotoSpin->setMaximum(qMax(1, total));

    m_navStatusLbl->setText(
        QString("Now showing: <b style='color:#66cc66;'>PowerPoint</b>  "
                "<span style='color:#aaa;'>slide %1 of %2</span>")
        .arg(cur).arg(total));
    m_navStatusLbl->setTextFormat(Qt::RichText);

    if (cur != m_curSlide || total != m_totSlides) {
        m_curSlide = cur; m_totSlides = total;
        emit slideStateChanged(cur, total, m_presTitle);
    }
}
