// ppt-tab.cpp  v3.0
// Single-source presentation tab.  One OBS window_capture source captures any
// visible window.  "PowerPoint source" checkbox enables COM automation for slide
// navigation and position tracking; unchecked = keystroke navigation (works for
// Google Slides, Canva, PDF viewers, etc.).
// Auto-shows when a Presentation service order is selected in Col 1.
// "Blank" button hides the source without leaving presentation mode.

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

// ── Shared button styles ──────────────────────────────────────

static const char *kBtnStyle =
    "QPushButton{background:#1a3a5c;color:#4da6ff;border:1px solid #2a5a8c;"
    "border-radius:4px;font-weight:bold;}"
    "QPushButton:hover{background:#254e7a;}"
    "QPushButton:disabled{background:#1a1a1a;color:#444;border-color:#333;}";

static const char *kBlankActiveStyle =
    "QPushButton{background:#3a1a1a;color:#ff6666;"
    "border:1px solid #7a2a2a;border-radius:4px;font-weight:bold;}"
    "QPushButton:hover{background:#4a2020;}";

static const char *kConnectStyle =
    "QPushButton{background:#1e2a1e;color:#66aa66;border:1px solid #336633;"
    "border-radius:4px;font-weight:bold;font-size:10px;padding:4px 8px;}"
    "QPushButton:hover{background:#263326;}";

static const char *kConnectedStyle =
    "QPushButton{background:#1a3a1a;color:#66ff66;border:1px solid #33aa33;"
    "border-radius:4px;font-weight:bold;font-size:10px;padding:4px 8px;}"
    "QPushButton:hover{background:#204020;}";

static const char *kBlankInactiveStyle =
    "QPushButton{background:#1e1e28;color:#aaa;border:1px solid #444;"
    "border-radius:4px;font-weight:bold;}"
    "QPushButton:hover{background:#2a2a38;color:#eee;}"
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

// Returns the PowerPoint Application IDispatch*, or nullptr.  Caller must Release().
static IDispatch *getPptApp()
{
    CLSID clsid = {};
    if (FAILED(CLSIDFromProgID(L"PowerPoint.Application", &clsid))) return nullptr;
    IUnknown *unk = nullptr;
    if (FAILED(GetActiveObject(clsid, nullptr, &unk)) || !unk) return nullptr;
    IDispatch *app = nullptr;
    HRESULT hr = unk->QueryInterface(IID_IDispatch, (void **)&app);
    unk->Release();
    if (FAILED(hr)) return nullptr;
    return app;
}

// Returns the COM IDispatch* for the first PowerPoint presentation that
// currently has a SlideShowWindow open, or nullptr.  Caller must Release().
static IDispatch *getPptActiveView()
{
    IDispatch *app = getPptApp();
    if (!app) return nullptr;
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
        HRESULT hr = presColl->Invoke(idItem, IID_NULL, LOCALE_USER_DEFAULT,
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
    releaseCaptureSource();
}

// ── UI construction ───────────────────────────────────────────

void PptTab::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    // ── Source group ─────────────────────────────────────────
    auto *srcGroup = new QGroupBox("Presentation Source");
    srcGroup->setStyleSheet(
        "QGroupBox{color:#ccc;border:1px solid #444;border-radius:5px;"
        "margin-top:6px;padding-top:4px;font-weight:bold;}"
        "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 4px;}");
    auto *srcLayout = new QVBoxLayout(srcGroup);
    srcLayout->setContentsMargins(8, 6, 8, 8);
    srcLayout->setSpacing(6);

    // Window selector row
    auto *winRow = new QHBoxLayout;
    m_winCombo = new QComboBox;
    m_winCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_winCombo->setStyleSheet(kComboStyle);
    m_winCombo->addItem("(Select window…)");

    m_scanBtn = new QPushButton("⟳ Scan");
    m_scanBtn->setFixedWidth(72);
    m_scanBtn->setStyleSheet(kBtnStyle);
    winRow->addWidget(m_winCombo, 1);
    winRow->addWidget(m_scanBtn);
    srcLayout->addLayout(winRow);

    // PowerPoint COM connect button
    m_pptConnectBtn = new QPushButton("Connect PowerPoint COM");
    m_pptConnectBtn->setToolTip(
        "Click to connect to a running PowerPoint slideshow via COM.\n"
        "Enables slide counter, Go-to, and COM-based Prev/Next.\n"
        "PowerPoint must be running (press F5 for slideshow mode).");
    srcLayout->addWidget(m_pptConnectBtn);

    // OBS source name hint
    auto *hintLbl = new QLabel(
        QString("OBS source: <span style='color:#4da6ff;'>%1</span>  "
                "— add this source to your OBS scene").arg(kSourceName));
    hintLbl->setStyleSheet("color:#555;font-size:9px;");
    hintLbl->setTextFormat(Qt::RichText);
    srcLayout->addWidget(hintLbl);

    root->addWidget(srcGroup);

    // ── Controls ─────────────────────────────────────────────
    auto *ctrlGroup = new QGroupBox("Controls");
    ctrlGroup->setStyleSheet(srcGroup->styleSheet());
    auto *ctrlLayout = new QHBoxLayout(ctrlGroup);
    ctrlLayout->setContentsMargins(8, 6, 8, 8);

    m_blankBtn = new QPushButton("■  Blank");
    m_blankBtn->setEnabled(false);
    m_blankBtn->setStyleSheet(kBlankInactiveStyle);
    ctrlLayout->addWidget(m_blankBtn);
    ctrlLayout->addStretch();

    root->addWidget(ctrlGroup);

    // ── Navigation ────────────────────────────────────────────
    auto *navGroup = new QGroupBox("Navigation");
    navGroup->setStyleSheet(srcGroup->styleSheet());
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

    connect(m_scanBtn,       &QPushButton::clicked, this, &PptTab::onScanWindows);
    connect(m_pptConnectBtn, &QPushButton::clicked, this, &PptTab::onConnectPpt);
    connect(m_blankBtn,      &QPushButton::clicked, this, &PptTab::onBlank);

    updateConnectBtnStyle();
    connect(m_winCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx){
        if (idx > 0 && idx - 1 < m_windows.size()) {
            emit windowExeChanged(m_windows[idx - 1].title);
            // Immediately update the OBS source so the preview refreshes without
            // requiring the user to click away and back.
            if (m_active) updateCaptureSource();
        }
    });
    connect(m_prevBtn,  &QPushButton::clicked, this, &PptTab::onPrev);
    connect(m_nextBtn,  &QPushButton::clicked, this, &PptTab::onNext);
    connect(m_gotoBtn,  &QPushButton::clicked, this, &PptTab::onGoto);
}

// ── Window scanning ───────────────────────────────────────────

void PptTab::scanWindows()
{
    QList<WinInfo> wins;
    EnumWinCtx ctx{ &wins };
    EnumWindows(enumWinCb, reinterpret_cast<LPARAM>(&ctx));
    m_windows = wins;

    QString prevText = m_winCombo->currentIndex() > 0
                       ? m_winCombo->currentText() : QString();
    QSignalBlocker blk(m_winCombo);
    m_winCombo->clear();
    m_winCombo->addItem("(Select window…)");
    for (const auto &wi : m_windows) {
        QString label = wi.title;
        if (!wi.exe.isEmpty()) label += "  —  " + wi.exe;
        m_winCombo->addItem(label);
        m_winCombo->setItemData(m_winCombo->count() - 1, wi.title, Qt::ToolTipRole);
    }
    if (!prevText.isEmpty()) {
        int idx = m_winCombo->findText(prevText);
        if (idx >= 0) m_winCombo->setCurrentIndex(idx);
    }
    autoSelectWindow({});
}

void PptTab::onScanWindows() { scanWindows(); }

void PptTab::updateConnectBtnStyle()
{
    if (!m_pptConnectBtn) return;
    if (m_pptConnected) {
        m_pptConnectBtn->setText("✓ PowerPoint COM Connected  (click to disconnect)");
        m_pptConnectBtn->setStyleSheet(kConnectedStyle);
    } else {
        m_pptConnectBtn->setText("Connect PowerPoint COM");
        m_pptConnectBtn->setStyleSheet(kConnectStyle);
    }
}

void PptTab::onConnectPpt()
{
    if (m_pptConnected) {
        m_pptConnected = false;
        updateConnectBtnStyle();
        m_curSlide = 0; m_totSlides = 0;
        m_slideCtr->setText("— / —");
        m_navStatusLbl->setText("PowerPoint COM disconnected.");
        updateNavUi();
        return;
    }

    // Validate: selected window should be POWERPNT.EXE
    int idx = m_winCombo->currentIndex() - 1;
    if (idx >= 0 && idx < m_windows.size()) {
        const QString &exe = m_windows[idx].exe;
        if (!exe.isEmpty() && exe.compare("POWERPNT.EXE", Qt::CaseInsensitive) != 0) {
            m_navStatusLbl->setText(
                QString("<span style='color:#ff8888;'>Selected window is not PowerPoint "
                        "(%1). Select a POWERPNT.EXE window first.</span>").arg(exe));
            m_navStatusLbl->setTextFormat(Qt::RichText);
            return;
        }
    }

    // Step 2: ensure this thread is COM-initialized (STA preferred for Office apps)
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // S_OK = initialized now; S_FALSE = already init'd as STA; RPC_E_CHANGED_MODE = MTA
    bool coWasInited = (hrCo == RPC_E_CHANGED_MODE || hrCo == S_FALSE);
    bool coInitedHere = SUCCEEDED(hrCo) && hrCo != S_FALSE;

    // Step 3: get CLSID for PowerPoint.Application
    CLSID clsid = {};
    HRESULT hr = CLSIDFromProgID(L"PowerPoint.Application", &clsid);
    if (FAILED(hr)) {
        if (coInitedHere) CoUninitialize();
        m_navStatusLbl->setText(
            QString("<span style='color:#ff8888;'>Step 3 FAIL: CLSIDFromProgID hr=0x%1 "
                    "(PowerPoint not installed?)</span>").arg((uint)hr, 8, 16, QChar('0')));
        m_navStatusLbl->setTextFormat(Qt::RichText);
        return;
    }

    // Step 4: get running instance from ROT
    IUnknown *unk = nullptr;
    hr = GetActiveObject(clsid, nullptr, &unk);
    if (FAILED(hr) || !unk) {
        if (coInitedHere) CoUninitialize();
        QString hint;
        if (hr == 0x800401E3L /*MK_E_UNAVAILABLE*/)
            hint = " — MK_E_UNAVAILABLE: ROT entry not found. "
                   "Thread apartment mismatch (MTA vs STA) or PPT not registered.";
        else if (hrCo == RPC_E_CHANGED_MODE)
            hint = " — thread is MTA; try restarting OBS without admin elevation.";
        m_navStatusLbl->setText(
            QString("<span style='color:#ff8888;'>Step 4 FAIL: GetActiveObject hr=0x%1%2</span>")
            .arg((uint)hr, 8, 16, QChar('0')).arg(hint));
        m_navStatusLbl->setTextFormat(Qt::RichText);
        return;
    }

    // Step 5: QI to IDispatch
    IDispatch *app = nullptr;
    hr = unk->QueryInterface(IID_IDispatch, (void **)&app);
    unk->Release();
    if (FAILED(hr) || !app) {
        if (coInitedHere) CoUninitialize();
        m_navStatusLbl->setText(
            QString("<span style='color:#ff8888;'>Step 5 FAIL: QueryInterface IDispatch hr=0x%1</span>")
            .arg((uint)hr, 8, 16, QChar('0')));
        m_navStatusLbl->setTextFormat(Qt::RichText);
        return;
    }
    app->Release();
    if (coInitedHere) CoUninitialize();

    // Success
    m_pptConnected = true;
    updateConnectBtnStyle();
    updateNavUi();
    m_navStatusLbl->setText(
        QString("<span style='color:#66ff66;'>PowerPoint COM connected "
                "(CoInit hr=0x%1). Slide counter updates every 500 ms.</span>")
        .arg((uint)hrCo, 8, 16, QChar('0')));
    m_navStatusLbl->setTextFormat(Qt::RichText);
}

void PptTab::autoSelectWindow(const QString &preferredExe)
{
    // When a preferred exe is set (switching to a saved presentation item) we
    // must force-select the matching window even if the combo already has a
    // selection from a different item — otherwise both items share one window.
    if (m_winCombo->currentIndex() > 0 && preferredExe.isEmpty()) return;

    // Priority 1: match by exact window title (unique even when multiple windows share an exe)
    if (!preferredExe.isEmpty()) {
        for (int i = 0; i < m_windows.size(); ++i) {
            if (m_windows[i].title == preferredExe) {
                m_winCombo->setCurrentIndex(i + 1);
                return;
            }
        }
        // Fall back to exe match for items saved before v12.23.0
        for (int i = 0; i < m_windows.size(); ++i) {
            if (m_windows[i].exe.compare(preferredExe, Qt::CaseInsensitive) == 0) {
                m_winCombo->setCurrentIndex(i + 1);
                return;
            }
        }
    }

    // Priority 2: PPT slide show window / exe, or any non-OBS window
    bool preferPpt = m_pptConnected;
    int bestIdx = -1;
    for (int i = 0; i < m_windows.size(); ++i) {
        const WinInfo &wi = m_windows[i];
        if (preferPpt) {
            if (wi.title.startsWith("PowerPoint Slide Show", Qt::CaseInsensitive)) {
                bestIdx = i; break;
            }
            if (wi.exe.compare("POWERPNT.EXE", Qt::CaseInsensitive) == 0 && bestIdx < 0)
                bestIdx = i;
        } else {
            if (!wi.exe.contains("obs", Qt::CaseInsensitive) && bestIdx < 0)
                bestIdx = i;
        }
    }
    if (bestIdx >= 0)
        m_winCombo->setCurrentIndex(bestIdx + 1);
}

// ── Activate / deactivate ─────────────────────────────────────

void PptTab::activatePresentation()
{
    m_active = true;
    m_blank  = false;
    updateCaptureSource();
    ensureInScene();
    updateBlankBtnStyle();
    updateNavUi();
    emit showSlidesRequested(0);
}

void PptTab::deactivatePresentation()
{
    m_active    = false;
    m_blank     = false;
    m_curSlide  = 0;
    m_totSlides = 0;
    hideFromScene();
    updateBlankBtnStyle();
    updateNavUi();
    emit showSlidesRequested(-1);
}

void PptTab::onBlank()
{
    if (!m_active) return;
    m_blank = !m_blank;
    if (m_blank) {
        hideFromScene();
    } else {
        // hideFromScene() cleared the window property; restore it before showing.
        updateCaptureSource();
        ensureInScene();
    }
    updateBlankBtnStyle();
    updateNavUi();
    // Emit -1 when blanked (OBS source hidden), 0 when un-blanked (source visible).
    emit showSlidesRequested(m_blank ? -1 : 0);
}

void PptTab::releaseCapture()
{
    // Drop our OBS source ref without touching the scene — scene may already be
    // invalid during SCENE_COLLECTION_CLEANUP.  Resets state so requestShow()
    // re-creates everything cleanly on the next scene collection.
    m_active = false;
    m_blank  = false;
    releaseCaptureSource(); // obs_source_release(m_capture); m_capture = nullptr
    updateBlankBtnStyle();
    updateNavUi();
}

void PptTab::restoreActiveUi()
{
    // Called on startup when the saved active item is a Presentation.
    // Sets m_active = true so buttons are enabled, but does NOT touch OBS
    // sources — those are initialised on the first real requestShow() call.
    m_active = true;
    m_blank  = false;
    updateBlankBtnStyle();
    updateNavUi();
}

// ── Remote-triggered activation ───────────────────────────────

void PptTab::requestShow(int type, const QString &windowExe)
{
    if (type < 0) {
        if (m_active) deactivatePresentation();
        return;
    }
    // Scan windows; if windowExe is set, prefer that exe for auto-selection
    if (m_winCombo->currentIndex() <= 0 || !windowExe.isEmpty()) {
        // Re-scan so we can try to match the preferred exe
        QList<WinInfo> wins;
        EnumWinCtx ctx{ &wins };
        EnumWindows(enumWinCb, reinterpret_cast<LPARAM>(&ctx));
        m_windows = wins;

        QString prevText = m_winCombo->currentIndex() > 0
                           ? m_winCombo->currentText() : QString();
        QSignalBlocker blk(m_winCombo);
        m_winCombo->clear();
        m_winCombo->addItem("(Select window…)");
        for (const auto &wi : m_windows) {
            QString label = wi.title;
            if (!wi.exe.isEmpty()) label += "  —  " + wi.exe;
            m_winCombo->addItem(label);
            m_winCombo->setItemData(m_winCombo->count() - 1, wi.title, Qt::ToolTipRole);
        }
        // Restore previous selection if still present; otherwise auto-select
        if (!prevText.isEmpty()) {
            int idx = m_winCombo->findText(prevText);
            if (idx >= 0) m_winCombo->setCurrentIndex(idx);
        }
        autoSelectWindow(windowExe);
    }

    activatePresentation(); // always clears blank state and shows
}


// ── OBS capture source management ────────────────────────────

HWND PptTab::selectedHwnd() const
{
    int idx = m_winCombo->currentIndex() - 1; // -1 for placeholder
    if (idx < 0 || idx >= m_windows.size()) return nullptr;
    return m_windows[idx].hwnd;
}

void PptTab::updateCaptureSource()
{
    HWND hwnd = selectedHwnd();
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

    if (!m_capture) {
        // Reuse any existing source by name to avoid duplicate accumulation
        // in the OBS scene across sessions.
        m_capture = obs_get_source_by_name(kSourceName);
        if (!m_capture)
            m_capture = obs_get_source_by_name(kSourceNameNew);
        if (!m_capture) {
            obs_data_t *s = obs_data_create();
            obs_data_set_string(s, "window", winId.toUtf8().constData());
            obs_data_set_int(s, "method", 2); // DXGI desktop duplication
            obs_data_set_bool(s, "cursor", false);
            m_capture = obs_source_create("window_capture", kSourceName, s, nullptr);
            obs_data_release(s);
        }
    }
    if (m_capture) {
        obs_data_t *s = obs_source_get_settings(m_capture);
        obs_data_set_string(s, "window", winId.toUtf8().constData());
        obs_source_update(m_capture, s);
        obs_data_release(s);
    }
}

void PptTab::releaseCaptureSource()
{
    if (m_capture) {
        obs_source_release(m_capture);
        m_capture = nullptr;
    }
}

// ── OBS scene integration ─────────────────────────────────────

void PptTab::ensureInScene()
{
    if (!m_capture) return;

    obs_source_t *sceneSrc = obs_frontend_get_current_scene();
    if (!sceneSrc) return;
    obs_scene_t *scene = obs_scene_from_source(sceneSrc);

    obs_sceneitem_t *item = obs_scene_find_source(scene, obs_source_get_name(m_capture));
    if (item) {
        obs_sceneitem_set_visible(item, true);
    } else {
        item = obs_scene_add(scene, m_capture);
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

void PptTab::hideFromScene()
{
    if (!m_capture) return;

    // Clear the window target so DXGI releases the desktop capture handle.
    obs_data_t *s = obs_source_get_settings(m_capture);
    obs_data_set_string(s, "window", "");
    obs_source_update(m_capture, s);
    obs_data_release(s);

    obs_source_t *sceneSrc = obs_frontend_get_current_scene();
    if (!sceneSrc) return;
    obs_scene_t *scene = obs_scene_from_source(sceneSrc);
    obs_sceneitem_t *item = obs_scene_find_source(scene, obs_source_get_name(m_capture));
    if (item) obs_sceneitem_set_visible(item, false);
    obs_source_release(sceneSrc);
}

// ── Legacy source cleanup (fixes shutdown crash) ──────────────
// Hides and clears window property on old 3-source sources so they
// no longer hold DXGI resources during OBS shutdown.

void PptTab::cleanupLegacySources()
{
    static const char *kLegacy[] = {
        "SSO - Google Slides",
        "SSO - Canva",
        "OBS Lyrics - Google Slides",
        "OBS Lyrics - Canva",
        "OBS Lyrics - PowerPoint",
    };

    obs_source_t *sceneSrc = obs_frontend_get_current_scene();
    if (!sceneSrc) return;
    obs_scene_t *scene = obs_scene_from_source(sceneSrc);

    for (const char *name : kLegacy) {
        obs_source_t *src = obs_get_source_by_name(name);
        if (!src) continue;

        // Clear DXGI capture to release graphics resources
        obs_data_t *sd = obs_source_get_settings(src);
        obs_data_set_string(sd, "window", "");
        obs_source_update(src, sd);
        obs_data_release(sd);

        // Hide in current scene
        obs_sceneitem_t *item = obs_scene_find_source(scene, name);
        if (item) obs_sceneitem_set_visible(item, false);

        obs_source_release(src);
    }

    obs_source_release(sceneSrc);
}

// ── UI update helpers ─────────────────────────────────────────

void PptTab::updateNavUi()
{
    bool canNav = (m_active && !m_blank) || m_pptConnected;
    bool isPpt  = m_pptConnected;

    m_prevBtn->setEnabled(canNav);
    m_nextBtn->setEnabled(canNav);
    m_gotoSpin->setEnabled(isPpt);
    m_gotoBtn->setEnabled(isPpt);
    m_blankBtn->setEnabled(m_active);

    if (!m_active) {
        m_slideCtr->setText("— / —");
        m_navStatusLbl->setText("Now showing: None");
        m_navStatusLbl->setStyleSheet("color:#555;font-size:10px;");
    } else if (m_blank) {
        m_navStatusLbl->setText(
            "Now showing: <b style='color:#ff6666;'>Blanked</b>");
        m_navStatusLbl->setStyleSheet("color:#aaa;font-size:10px;");
        m_navStatusLbl->setTextFormat(Qt::RichText);
    } else {
        m_navStatusLbl->setText(
            "Now showing: <b style='color:#66cc66;'>Presentation</b>");
        m_navStatusLbl->setStyleSheet("color:#aaa;font-size:10px;");
        m_navStatusLbl->setTextFormat(Qt::RichText);
    }
}

void PptTab::updateBlankBtnStyle()
{
    m_blankBtn->setText(m_blank ? "▶  Resume" : "■  Blank");
    m_blankBtn->setStyleSheet(m_blank ? kBlankActiveStyle : kBlankInactiveStyle);
}

// ── Navigation actions ────────────────────────────────────────

void PptTab::sendKeyToActive(int vkey)
{
    if (!m_active || m_blank) return;
    HWND hwnd = selectedHwnd();
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
    if (m_pptConnected) {
        int idx = m_winCombo->currentIndex() - 1;
        bool isPptWindow = idx >= 0 && idx < m_windows.size() &&
                           m_windows[idx].exe.contains("POWERPNT", Qt::CaseInsensitive);
        if (isPptWindow) {
            IDispatch *v = getPptView();
            if (v) { pptCall(v, L"Next"); v->Release(); return; }
        }
    }
    sendKeyToActive(VK_RIGHT);
}

void PptTab::prevSlide()
{
    if (m_pptConnected) {
        int idx = m_winCombo->currentIndex() - 1;
        bool isPptWindow = idx >= 0 && idx < m_windows.size() &&
                           m_windows[idx].exe.contains("POWERPNT", Qt::CaseInsensitive);
        if (isPptWindow) {
            IDispatch *v = getPptView();
            if (v) { pptCall(v, L"Previous"); v->Release(); return; }
        }
    }
    sendKeyToActive(VK_LEFT);
}

void PptTab::gotoSlide(int n)
{
    if (n < 1) return;
    if (m_pptConnected) {
        if (IDispatch *v = getPptView()) {
            pptCallInt(v, L"GotoSlide", n); v->Release(); return;
        }
        // Slideshow not active — try edit mode GotoSlide via ActiveWindow.View
        if (IDispatch *app = getPptApp()) {
            IDispatch *activeWin = pptObj(app, L"ActiveWindow");
            if (activeWin) {
                IDispatch *editView = pptObj(activeWin, L"View");
                if (editView) { pptCallInt(editView, L"GotoSlide", n); editView->Release(); }
                activeWin->Release();
            }
            app->Release();
        }
    }
}

void PptTab::onPrev() { prevSlide(); }
void PptTab::onNext() { nextSlide(); }
void PptTab::onGoto() { gotoSlide(m_gotoSpin->value()); }

// ── Poll timer (PowerPoint COM position read) ─────────────────

void PptTab::onPollTimer()
{
    bool doAutoScan = (++m_pollCount >= 10);
    if (doAutoScan) {
        m_pollCount = 0;
        // Every 5 s: auto-detect windows if no selection
        if (m_winCombo->currentIndex() <= 0)
            scanWindows();
    }

    // COM polling only when PowerPoint COM connected
    if (!m_pptConnected) return;
    // Skip visual-only actions when not active/visible, but still read slide position
    bool activeVisible = m_active && !m_blank;

    IDispatch *view = getPptView();
    if (!view) {
        // Slideshow window not found — try edit mode (PowerPoint open but not in F5 mode).
        bool updated = false;
        IDispatch *app = getPptApp();
        if (app) {
            int cur = 0, total = 0;
            IDispatch *activeWin = pptObj(app, L"ActiveWindow");
            if (activeWin) {
                IDispatch *editView = pptObj(activeWin, L"View");
                if (editView) {
                    IDispatch *slide = pptObj(editView, L"Slide");
                    if (slide) { cur = pptInt(slide, L"SlideIndex"); slide->Release(); }
                    editView->Release();
                }
                activeWin->Release();
            }
            IDispatch *activePres = pptObj(app, L"ActivePresentation");
            if (activePres) {
                IDispatch *slides = pptObj(activePres, L"Slides");
                if (slides) { total = pptInt(slides, L"Count"); slides->Release(); }
                activePres->Release();
            }
            app->Release();
            if (cur > 0) {
                m_slideCtr->setText(QString("%1 / %2").arg(cur)
                                    .arg(total > 0 ? QString::number(total) : "?"));
                m_gotoSpin->setMaximum(qMax(1, total));
                m_navStatusLbl->setText(
                    QString("Now showing: <b style='color:#66cc66;'>Presentation</b>  "
                            "<span style='color:#888;'>edit mode — slide %1/%2 (press F5 for slideshow)</span>")
                    .arg(cur).arg(total));
                m_navStatusLbl->setTextFormat(Qt::RichText);
                if (cur != m_curSlide || total != m_totSlides) {
                    m_curSlide = cur; m_totSlides = total;
                    emit slideStateChanged(cur, total, m_presTitle);
                }
                updated = true;
            }
        }
        if (!updated) {
            bool pptRunning = isPowerPointRunning();
            if (m_curSlide != 0 || pptRunning) {
                m_curSlide = 0; m_totSlides = 0;
                m_slideCtr->setText("— / —");
                m_navStatusLbl->setText(
                    pptRunning
                    ? "Now showing: <b style='color:#66cc66;'>Presentation</b> "
                      "<span style='color:#888;'>(press F5 for slideshow mode)</span>"
                    : "Now showing: <b style='color:#66cc66;'>Presentation</b> "
                      "<span style='color:#888;'>(PowerPoint not running)</span>");
                m_navStatusLbl->setTextFormat(Qt::RichText);
            }
        }
        return;
    }

    int cur   = pptInt(view, L"CurrentShowPosition");
    int total = 0;

    IDispatch *ssWin = pptObj(view, L"Parent");
    if (ssWin) {
        IDispatch *pres = pptObj(ssWin, L"Presentation");
        if (pres) {
            IDispatch *slides = pptObj(pres, L"Slides");
            if (slides) { total = pptInt(slides, L"Count"); slides->Release(); }
            pres->Release();
        }
        ssWin->Release();
    }
    view->Release();

    m_slideCtr->setText(QString("%1 / %2").arg(cur)
                                          .arg(total > 0 ? QString::number(total) : QString("?")));
    m_gotoSpin->setMaximum(qMax(1, total));

    m_navStatusLbl->setText(
        QString("Now showing: <b style='color:#66cc66;'>Presentation</b>  "
                "<span style='color:#aaa;'>slide %1 of %2</span>")
        .arg(cur).arg(total));
    m_navStatusLbl->setTextFormat(Qt::RichText);

    if (cur != m_curSlide || total != m_totSlides) {
        m_curSlide = cur; m_totSlides = total;
        emit slideStateChanged(cur, total, m_presTitle);
    }
}
