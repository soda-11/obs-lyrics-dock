// overlay-tab.cpp  v1.2
// Floating touch overlay window + its dock settings tab.

#include "overlay-tab.hpp"
#include "preview-window.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QApplication>
#include <QScreen>

// ── Static label/key tables ───────────────────────────────────
const char *PptOverlayWindow::kModeKeys[]   = { "ctw", "scr", "lyr" };
const char *PptOverlayWindow::kModeLabels[] = { "CTW", "SCR", "LYR" };
const char *PptOverlayWindow::kPresLabels[] = { "PPT", "Google", "Canva" };

// ── OBS scene helpers (file-scope) ────────────────────────────

static QStringList obsSceneNames()
{
    QStringList names;
    names << QString(); // blank = (no scene switch)
    struct obs_frontend_source_list list = {};
    obs_frontend_get_scenes(&list);
    for (size_t i = 0; i < list.sources.num; ++i)
        names << QString::fromUtf8(obs_source_get_name(list.sources.array[i]));
    obs_frontend_source_list_free(&list);
    return names;
}

static void switchToScene(const QString &name)
{
    if (name.isEmpty()) return;
    obs_source_t *src = obs_get_source_by_name(name.toUtf8().constData());
    if (src) {
        obs_frontend_set_current_scene(src);
        obs_source_release(src);
    }
}

// ── Colour helpers (all derived from m_hue) ───────────────────

static QString rgbaStr(QColor c)
{
    return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

QColor PptOverlayWindow::colBg()                const { return QColor::fromHsv(m_hue, 200,  25, 190); }
QColor PptOverlayWindow::colBgBorder()          const { return QColor::fromHsv(m_hue, 130, 130, 130); }
QColor PptOverlayWindow::colNavBg()             const { return QColor::fromHsv(m_hue, 220,  60, 215); }
QColor PptOverlayWindow::colNavBgPressed()      const { return QColor::fromHsv(m_hue, 200, 120, 240); }
QColor PptOverlayWindow::colNavAccent()         const { return QColor::fromHsv(m_hue, 120, 230, 200); }
QColor PptOverlayWindow::colModeActive()        const { return QColor::fromHsv(m_hue, 190, 170, 235); }
QColor PptOverlayWindow::colModeActivePressed() const { return QColor::fromHsv(m_hue, 160, 210, 255); }
QColor PptOverlayWindow::colModeInactive()      const { return QColor::fromHsv(m_hue, 210,  45, 185); }
QColor PptOverlayWindow::colModeInactiveHover() const { return QColor::fromHsv(m_hue, 190,  75, 200); }
QColor PptOverlayWindow::colModeInactiveTxt()   const { return QColor::fromHsv(m_hue,  80, 180, 255); }

// ══════════════════════════════════════════════════════════════
// PptOverlayWindow
// ══════════════════════════════════════════════════════════════

PptOverlayWindow::PptOverlayWindow(QWidget *parent)
    : QWidget(parent,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    for (int i = 0; i < kModes; ++i) {
        m_modeBtns[i] = new QPushButton(kModeLabels[i], this);
        connect(m_modeBtns[i], &QPushButton::clicked, this, [this, i]() {
            emit modeRequested(QString(kModeKeys[i]));
        });
    }

    for (int i = 0; i < kPresTypes; ++i) {
        m_presBtns[i] = new QPushButton(kPresLabels[i], this);
        m_presBtns[i]->setEnabled(false); // disabled until PptTab reports enabled
        const int ci = i;
        connect(m_presBtns[i], &QPushButton::clicked, this, [this, ci]() {
            emit presRequested(ci);
        });
    }

    m_prevBtn = new QPushButton(QString::fromUtf8("\xe2\x97\x80"), this);
    m_nextBtn = new QPushButton(QString::fromUtf8("\xe2\x96\xb6"), this);
    connect(m_prevBtn, &QPushButton::clicked, this, [this]() {
        emit prevRequested(m_activeMode);
    });
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() {
        emit nextRequested(m_activeMode);
    });

    setOverlaySize(m_totalWidth);
    refreshModeBtnStyles();
    refreshPresBtnStyles();
}

// ── Style generation ──────────────────────────────────────────

void PptOverlayWindow::applyNavBtnStyle(int navW, int navH)
{
    int navSize   = qMin(navW, navH);
    int fontSize  = qMax(14, navSize * 32 / 100);
    QString ss = QString(
        "QPushButton{"
        "  background:%1;"
        "  color:%2;"
        "  border:2px solid %3;"
        "  border-radius:10px;"
        "  font-size:%4px;"
        "  font-weight:bold;"
        "  padding:0px;"
        "}"
        "QPushButton:pressed{"
        "  background:%5;"
        "  color:#ffffff;"
        "}")
        .arg(rgbaStr(colNavBg()))
        .arg(rgbaStr(colNavAccent()))
        .arg(rgbaStr(colNavAccent()))
        .arg(fontSize)
        .arg(rgbaStr(colNavBgPressed()));
    m_prevBtn->setStyleSheet(ss);
    m_nextBtn->setStyleSheet(ss);
}

void PptOverlayWindow::refreshModeBtnStyles()
{
    for (int i = 0; i < kModes; ++i) {
        bool active = (m_activeMode == kModeKeys[i]);
        QString ss;
        if (active) {
            ss = QString(
                "QPushButton{"
                "  background:%1;"
                "  color:#ffffff;"
                "  border:none;"
                "  font-weight:bold;"
                "  font-size:11px;"
                "  border-radius:3px;"
                "}"
                "QPushButton:pressed{background:%2;}")
                .arg(rgbaStr(colModeActive()))
                .arg(rgbaStr(colModeActivePressed()));
        } else {
            ss = QString(
                "QPushButton{"
                "  background:%1;"
                "  color:%2;"
                "  border:none;"
                "  font-size:11px;"
                "  border-radius:3px;"
                "}"
                "QPushButton:hover{background:%3;}"
                "QPushButton:pressed{background:%4;}")
                .arg(rgbaStr(colModeInactive()))
                .arg(rgbaStr(colModeInactiveTxt()))
                .arg(rgbaStr(colModeInactiveHover()))
                .arg(rgbaStr(colModeActive()));
        }
        m_modeBtns[i]->setStyleSheet(ss);
    }
}

void PptOverlayWindow::refreshAllStyles()
{
    const int pad   = 6;
    int navSize     = qMax(50, (m_totalWidth - 3 * pad) / 2);
    applyNavBtnStyle(navSize, navSize);
    refreshModeBtnStyles();
    refreshPresBtnStyles();
    update();
}

// ── Geometry ──────────────────────────────────────────────────

void PptOverlayWindow::rebuildGeometry()
{
    const int pad  = 6;
    int navSize    = qMax(50, (m_totalWidth - 3 * pad) / 2);
    int modeBtnH   = qMax(22, navSize * 30 / 100);
    int presBtnH   = modeBtnH;
    int modeBtnW   = m_totalWidth / kModes;
    int presBtnW   = m_totalWidth / kPresTypes;

    // Row 1: content mode buttons (CTW / SCR / LYR)
    for (int i = 0; i < kModes; ++i) {
        int x = i * modeBtnW;
        int w = (i == kModes - 1) ? (m_totalWidth - i * modeBtnW) : modeBtnW;
        m_modeBtns[i]->setGeometry(x, pad, w, modeBtnH);
    }

    // Row 2: presentation source buttons (PPT / Google / Canva)
    int presY = pad + modeBtnH + pad;
    for (int i = 0; i < kPresTypes; ++i) {
        int x = i * presBtnW;
        int w = (i == kPresTypes - 1) ? (m_totalWidth - i * presBtnW) : presBtnW;
        m_presBtns[i]->setGeometry(x, presY, w, presBtnH);
    }

    // Row 3: navigation arrows
    int navY = presY + presBtnH + pad;
    m_prevBtn->setFixedSize(navSize, navSize);
    m_prevBtn->move(pad, navY);
    m_nextBtn->setFixedSize(navSize, navSize);
    m_nextBtn->move(pad + navSize + pad, navY);

    applyNavBtnStyle(navSize, navSize);
    setFixedSize(m_totalWidth, navY + navSize + pad);
}

void PptOverlayWindow::setOverlaySize(int totalWidthPx)
{
    m_totalWidth = qMax(120, totalWidthPx);
    rebuildGeometry();
}

void PptOverlayWindow::setOverlayOpacity(int percent)
{
    setWindowOpacity(qBound(20, percent, 100) / 100.0);
}

void PptOverlayWindow::setActiveMode(const QString &mode)
{
    m_activeMode = mode;
    refreshModeBtnStyles();
}

void PptOverlayWindow::setHue(int hue)
{
    m_hue = qBound(0, hue, 359);
    refreshAllStyles();
}

void PptOverlayWindow::setPresActive(int type)
{
    m_activePresType = type;
    refreshPresBtnStyles();
}

void PptOverlayWindow::setPresEnabled(int type, bool on)
{
    if (type >= 0 && type < kPresTypes)
        m_presBtns[type]->setEnabled(on);
}

void PptOverlayWindow::refreshPresBtnStyles()
{
    for (int i = 0; i < kPresTypes; ++i) {
        bool active = (m_activePresType == i);
        QString ss;
        if (active) {
            ss = QString(
                "QPushButton{"
                "  background:%1;"
                "  color:#ffffff;"
                "  border:none;"
                "  font-weight:bold;"
                "  font-size:11px;"
                "  border-radius:3px;"
                "}"
                "QPushButton:pressed{background:%2;}")
                .arg(rgbaStr(colModeActive()))
                .arg(rgbaStr(colModeActivePressed()));
        } else {
            ss = QString(
                "QPushButton{"
                "  background:%1;"
                "  color:%2;"
                "  border:none;"
                "  font-size:11px;"
                "  border-radius:3px;"
                "}"
                "QPushButton:hover{background:%3;}"
                "QPushButton:pressed{background:%4;}"
                "QPushButton:disabled{background:%1;color:%5;}")
                .arg(rgbaStr(colModeInactive()))
                .arg(rgbaStr(colModeInactiveTxt()))
                .arg(rgbaStr(colModeInactiveHover()))
                .arg(rgbaStr(colModeActive()))
                .arg(rgbaStr(QColor(colModeInactiveTxt().red(),
                                    colModeInactiveTxt().green(),
                                    colModeInactiveTxt().blue(), 80)));
        }
        m_presBtns[i]->setStyleSheet(ss);
    }
}

// ── Paint ─────────────────────────────────────────────────────

void PptOverlayWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(colBg());
    p.setPen(QPen(colBgBorder(), 1));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);
}

// ── Drag to move ──────────────────────────────────────────────

void PptOverlayWindow::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        m_dragPos = e->globalPosition().toPoint() - frameGeometry().topLeft();
#else
        m_dragPos = e->globalPos() - frameGeometry().topLeft();
#endif
        e->accept();
    }
}

void PptOverlayWindow::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::LeftButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        move(e->globalPosition().toPoint() - m_dragPos);
#else
        move(e->globalPos() - m_dragPos);
#endif
        e->accept();
    }
}

// ══════════════════════════════════════════════════════════════
// OverlayTab
// ══════════════════════════════════════════════════════════════

// Frontend event callback — repopulate + reload once OBS scenes are ready
static void overlayFrontendEvent(enum obs_frontend_event event, void *data)
{
    if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING &&
        event != OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED)
        return;
    auto *tab = static_cast<OverlayTab *>(data);
    // Must run on the Qt main thread
    QMetaObject::invokeMethod(tab, "onScenesReady", Qt::QueuedConnection);
}

OverlayTab::OverlayTab(QWidget *parent) : QWidget(parent)
{
    buildUi();
    obs_frontend_add_event_callback(overlayFrontendEvent, this);
}

OverlayTab::~OverlayTab()
{
    obs_frontend_remove_event_callback(overlayFrontendEvent, this);
    for (int i = 0; i < kPreviews; ++i) {
        if (m_previewWin[i]) {
            m_previewWin[i]->close();
            m_previewWin[i] = nullptr;
        }
    }
}

// Rainbow gradient CSS for the hue slider groove
static const char *kHueGrooveCss =
    "QSlider::groove:horizontal{"
    "  height:8px;border-radius:4px;"
    "  background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
    "    stop:0.000 #ff0000,"
    "    stop:0.167 #ffff00,"
    "    stop:0.333 #00ff00,"
    "    stop:0.500 #00ffff,"
    "    stop:0.667 #0000ff,"
    "    stop:0.833 #ff00ff,"
    "    stop:1.000 #ff0000);}"
    "QSlider::handle:horizontal{"
    "  background:#ffffff;border:2px solid #aaa;"
    "  width:16px;height:16px;margin:-4px 0;border-radius:8px;}"
    "QSlider::sub-page:horizontal{background:transparent;}"
    "QSlider::add-page:horizontal{background:transparent;}";

static const char *kSliderCss =
    "QSlider::groove:horizontal{background:#333;height:4px;border-radius:2px;}"
    "QSlider::handle:horizontal{background:#5ab4ff;width:14px;height:14px;"
    "margin:-5px 0;border-radius:7px;}"
    "QSlider::sub-page:horizontal{background:#3a6a9a;border-radius:2px;}";

static const char *kComboSS =
    "QComboBox{background:#1e1e1e;color:#ddd;border:1px solid #444;"
    "border-radius:3px;padding:2px 6px;}"
    "QComboBox::drop-down{border:none;}"
    "QComboBox QAbstractItemView{background:#1e1e1e;color:#ddd;"
    "selection-background-color:#1e4a7a;}";

void OverlayTab::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    // ── Touch Overlay controls ────────────────────────────────
    auto *overlayGroup  = new QGroupBox("Touch Overlay");
    auto *overlayLayout = new QVBoxLayout(overlayGroup);
    overlayLayout->setSpacing(8);

    m_toggleBtn = new QPushButton("Show Touch Overlay");
    m_toggleBtn->setFixedHeight(32);
    m_toggleBtn->setCheckable(true);
    m_toggleBtn->setStyleSheet(
        "QPushButton{"
        "  background:#2a3a2a;color:#88cc88;"
        "  border:1px solid #3a6a3a;border-radius:4px;font-weight:bold;}"
        "QPushButton:hover{background:#354535;}"
        "QPushButton:checked{"
        "  background:#1a4a1a;color:#66ff66;border-color:#2a8a2a;}");

    // Size row
    auto *sizeRow = new QHBoxLayout;
    auto *sizeLbl = new QLabel("Size:");
    sizeLbl->setStyleSheet("color:#aaa;font-size:10px;");
    sizeLbl->setFixedWidth(52);
    m_sizeSlider = new QSlider(Qt::Horizontal);
    m_sizeSlider->setRange(120, 500);
    m_sizeSlider->setValue(240);
    m_sizeSlider->setStyleSheet(kSliderCss);
    m_sizeLbl = new QLabel("240 px");
    m_sizeLbl->setStyleSheet("color:#888;font-size:10px;");
    m_sizeLbl->setFixedWidth(48);
    m_sizeLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sizeRow->addWidget(sizeLbl);
    sizeRow->addWidget(m_sizeSlider, 1);
    sizeRow->addWidget(m_sizeLbl);

    // Opacity row
    auto *opacityRow = new QHBoxLayout;
    auto *opacityLbl = new QLabel("Opacity:");
    opacityLbl->setStyleSheet("color:#aaa;font-size:10px;");
    opacityLbl->setFixedWidth(52);
    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(20, 100);
    m_opacitySlider->setValue(85);
    m_opacitySlider->setStyleSheet(kSliderCss);
    m_opacityLbl = new QLabel("85%");
    m_opacityLbl->setStyleSheet("color:#888;font-size:10px;");
    m_opacityLbl->setFixedWidth(48);
    m_opacityLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    opacityRow->addWidget(opacityLbl);
    opacityRow->addWidget(m_opacitySlider, 1);
    opacityRow->addWidget(m_opacityLbl);

    // Hue row
    auto *hueRow = new QHBoxLayout;
    auto *hueLbl = new QLabel("Colour:");
    hueLbl->setStyleSheet("color:#aaa;font-size:10px;");
    hueLbl->setFixedWidth(52);
    m_hueSlider = new QSlider(Qt::Horizontal);
    m_hueSlider->setRange(0, 359);
    m_hueSlider->setValue(215);
    m_hueSlider->setStyleSheet(kHueGrooveCss);
    m_hueSwatch = new QLabel;
    m_hueSwatch->setFixedSize(32, 14);
    hueRow->addWidget(hueLbl);
    hueRow->addWidget(m_hueSlider, 1);
    hueRow->addWidget(m_hueSwatch);

    overlayLayout->addWidget(m_toggleBtn);
    overlayLayout->addLayout(sizeRow);
    overlayLayout->addLayout(opacityRow);
    overlayLayout->addLayout(hueRow);

    // ── Scene Switching ───────────────────────────────────────
    auto *sceneGroup  = new QGroupBox("Scene Switching");
    auto *sceneLayout = new QVBoxLayout(sceneGroup);
    sceneLayout->setSpacing(6);

    static const char *kModeLabels[3] = { "CTW", "SCR", "LYR" };
    for (int i = 0; i < 3; ++i) {
        auto *row    = new QHBoxLayout;
        auto *lbl    = new QLabel(QString(kModeLabels[i]) + ":");
        lbl->setStyleSheet("color:#aaa;font-size:10px;font-weight:bold;");
        lbl->setFixedWidth(36);
        m_sceneCombo[i] = new QComboBox;
        m_sceneCombo[i]->setStyleSheet(kComboSS);
        m_sceneCombo[i]->setToolTip(
            QString("Scene to switch to when %1 mode is activated")
            .arg(kModeLabels[i]));
        row->addWidget(lbl);
        row->addWidget(m_sceneCombo[i], 1);
        sceneLayout->addLayout(row);
    }

    m_refreshScenesBtn = new QPushButton("↺  Refresh Scene List");
    m_refreshScenesBtn->setFixedHeight(26);
    m_refreshScenesBtn->setStyleSheet(
        "QPushButton{background:#2a2a2a;color:#aaa;"
        "border:1px solid #444;border-radius:3px;font-size:10px;}"
        "QPushButton:hover{background:#333;}");
    sceneLayout->addWidget(m_refreshScenesBtn);

    // ── Info ──────────────────────────────────────────────────
    auto *infoLbl = new QLabel(
        "Mode highlights automatically based on the last pushed slide.\n"
        "Select (none) to skip scene switching for that mode.\n"
        "Drag the floating overlay to any monitor.");
    infoLbl->setStyleSheet("color:#555;font-size:9px;");
    infoLbl->setWordWrap(true);

    // ── Preview Outputs ───────────────────────────────────────
    auto *prevGroup  = new QGroupBox("Preview Outputs");
    auto *prevLayout = new QVBoxLayout(prevGroup);
    prevLayout->setSpacing(6);

    static const char *kBtnSS =
        "QPushButton{background:#1a2a1a;color:#6aff6a;border:1px solid #3a5a3a;"
        "border-radius:3px;font-size:10px;padding:2px 6px;}"
        "QPushButton:checked{background:#1a3a1a;color:#fff;border-color:#6aff6a;}"
        "QPushButton:hover{background:#223322;}";

    for (int i = 0; i < kPreviews; ++i) {
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(QString("Preview %1").arg(i + 1));
        lbl->setStyleSheet("color:#aaa;font-size:10px;");
        lbl->setFixedWidth(58);

        m_previewMonitor[i] = new QComboBox;
        m_previewMonitor[i]->setStyleSheet(kComboSS);
        m_previewMonitor[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        m_previewOpenBtn[i] = new QPushButton("Open");
        m_previewOpenBtn[i]->setFixedWidth(52);
        m_previewOpenBtn[i]->setCheckable(true);
        m_previewOpenBtn[i]->setStyleSheet(kBtnSS);

        m_previewTopChk[i] = new QCheckBox("On Top");
        m_previewTopChk[i]->setStyleSheet("color:#aaa;font-size:10px;");

        row->addWidget(lbl);
        row->addWidget(m_previewMonitor[i], 1);
        row->addWidget(m_previewOpenBtn[i]);
        row->addWidget(m_previewTopChk[i]);
        prevLayout->addLayout(row);

        connect(m_previewOpenBtn[i], &QPushButton::toggled,
                this, [this, i](bool){ onPreviewOpen(i); });
        connect(m_previewTopChk[i], &QCheckBox::toggled,
                this, [this, i](bool top){ onPreviewTopChanged(i, top); });
    }

    root->addWidget(overlayGroup);
    root->addWidget(sceneGroup);
    root->addWidget(prevGroup);
    root->addWidget(infoLbl);
    root->addStretch();

    // ── Create overlay ────────────────────────────────────────
    m_overlay = new PptOverlayWindow(nullptr);
    m_overlay->setOverlaySize(m_sizeSlider->value());
    m_overlay->setOverlayOpacity(m_opacitySlider->value());
    m_overlay->setHue(m_hueSlider->value());
    updateHueSwatch();
    populateSceneCombos();
    populateMonitorCombos();
    loadSettings();

    // Overlay nav signals forward directly
    connect(m_overlay, &PptOverlayWindow::prevRequested, this, &OverlayTab::prevRequested);
    connect(m_overlay, &PptOverlayWindow::nextRequested, this, &OverlayTab::nextRequested);
    // Mode requests go through internal slot so we can switch scene first
    connect(m_overlay, &PptOverlayWindow::modeRequested,
            this, &OverlayTab::onModeRequestedInternal);
    // Pres source buttons forward directly (LyricsDock wires to PptTab::requestShow)
    connect(m_overlay, &PptOverlayWindow::presRequested,
            this, &OverlayTab::presRequested);

    connect(m_toggleBtn,       &QPushButton::toggled,    this, &OverlayTab::onToggle);
    connect(m_sizeSlider,      &QSlider::valueChanged,   this, &OverlayTab::onSizeChanged);
    connect(m_opacitySlider,   &QSlider::valueChanged,   this, &OverlayTab::onOpacityChanged);
    connect(m_hueSlider,       &QSlider::valueChanged,   this, &OverlayTab::onHueChanged);
    connect(m_refreshScenesBtn,&QPushButton::clicked,    this, &OverlayTab::onRefreshScenes);
    for (int i = 0; i < 3; ++i)
        connect(m_sceneCombo[i], QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int){ saveSettings(); });
}

// ── Helpers ───────────────────────────────────────────────────

int OverlayTab::modeIndex(const QString &mode) const
{
    if (mode == "ctw") return 0;
    if (mode == "scr") return 1;
    if (mode == "lyr") return 2;
    return -1;
}

void OverlayTab::populateSceneCombos()
{
    QStringList scenes = obsSceneNames();
    for (int i = 0; i < 3; ++i) {
        if (!m_sceneCombo[i]) continue;
        QString cur = m_sceneCombo[i]->currentText();
        m_sceneCombo[i]->blockSignals(true);
        m_sceneCombo[i]->clear();
        m_sceneCombo[i]->addItem("(none)");
        for (const QString &s : scenes)
            if (!s.isEmpty()) m_sceneCombo[i]->addItem(s);
        // Restore previous selection if still present
        int idx = m_sceneCombo[i]->findText(cur);
        m_sceneCombo[i]->setCurrentIndex(idx >= 0 ? idx : 0);
        m_sceneCombo[i]->blockSignals(false);
    }
}

static QString overlaySettingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/obs-lyrics-dock/overlay-settings.json";
}

static const char *kSceneKeys[3] = { "sceneCtw", "sceneScr", "sceneLyr" };

void OverlayTab::saveSettings()
{
    QJsonObject obj;
    for (int i = 0; i < 3; ++i) {
        if (!m_sceneCombo[i]) continue;
        QString val = m_sceneCombo[i]->currentText();
        obj[kSceneKeys[i]] = (val == "(none)") ? "" : val;
    }
    QString path = overlaySettingsPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(obj).toJson());
}

void OverlayTab::loadSettings()
{
    QFile f(overlaySettingsPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    for (int i = 0; i < 3; ++i) {
        if (!m_sceneCombo[i]) continue;
        QString saved = obj[kSceneKeys[i]].toString();
        int idx = saved.isEmpty() ? 0 : m_sceneCombo[i]->findText(saved);
        m_sceneCombo[i]->setCurrentIndex(idx >= 0 ? idx : 0);
    }
}

void OverlayTab::switchToSceneForMode(const QString &mode)
{
    int idx = modeIndex(mode);
    if (idx < 0 || !m_sceneCombo[idx]) return;
    QString scene = m_sceneCombo[idx]->currentText();
    if (scene == "(none)" || scene.isEmpty()) return;
    switchToScene(scene);
}

void OverlayTab::updateHueSwatch()
{
    if (!m_hueSwatch) return;
    QColor c = QColor::fromHsv(m_hueSlider->value(), 220, 200);
    m_hueSwatch->setStyleSheet(QString(
        "background:%1;border:1px solid #555;border-radius:3px;")
        .arg(c.name()));
}

// ── Slots ─────────────────────────────────────────────────────

void OverlayTab::setActiveMode(const QString &mode)
{
    if (m_overlay) m_overlay->setActiveMode(mode);
}

void OverlayTab::setPresActive(int type)
{
    if (m_overlay) m_overlay->setPresActive(type);
}

void OverlayTab::setPresEnabled(int type, bool on)
{
    if (m_overlay) m_overlay->setPresEnabled(type, on);
}

void OverlayTab::onModeRequestedInternal(const QString &mode)
{
    switchToSceneForMode(mode);   // switch OBS scene first
    emit modeRequested(mode);     // then notify LyricsDock
}

void OverlayTab::onToggle(bool checked)
{
    if (!m_overlay) return;
    if (checked) {
        m_overlay->show();
        m_toggleBtn->setText("Hide Touch Overlay");
    } else {
        m_overlay->hide();
        m_toggleBtn->setText("Show Touch Overlay");
    }
}

void OverlayTab::onSizeChanged(int val)
{
    m_sizeLbl->setText(QString("%1 px").arg(val));
    if (m_overlay) m_overlay->setOverlaySize(val);
}

void OverlayTab::onOpacityChanged(int val)
{
    m_opacityLbl->setText(QString("%1%").arg(val));
    if (m_overlay) m_overlay->setOverlayOpacity(val);
}

void OverlayTab::onHueChanged(int val)
{
    updateHueSwatch();
    if (m_overlay) m_overlay->setHue(val);
}

void OverlayTab::onRefreshScenes()
{
    populateSceneCombos();
    loadSettings(); // re-apply saved scene names after repopulation
}

void OverlayTab::onScenesReady()
{
    populateSceneCombos();
    loadSettings();
}

void OverlayTab::populateMonitorCombos()
{
    QList<QScreen *> screens = QGuiApplication::screens();
    for (int i = 0; i < kPreviews; ++i) {
        if (!m_previewMonitor[i]) continue;
        int cur = m_previewMonitor[i]->currentIndex();
        m_previewMonitor[i]->blockSignals(true);
        m_previewMonitor[i]->clear();
        for (int j = 0; j < screens.size(); ++j) {
            QRect g = screens[j]->geometry();
            m_previewMonitor[i]->addItem(
                QString("Monitor %1  (%2 × %3)").arg(j + 1).arg(g.width()).arg(g.height()));
        }
        // Default: Preview 1 → monitor 0, Preview 2 → monitor 1 (if available)
        int def = qMin(i, screens.size() - 1);
        m_previewMonitor[i]->setCurrentIndex(cur >= 0 && cur < screens.size() ? cur : def);
        m_previewMonitor[i]->blockSignals(false);
    }
}

void OverlayTab::onPreviewOpen(int idx)
{
    bool open = m_previewOpenBtn[idx]->isChecked();
    if (open) {
        QList<QScreen *> screens = QGuiApplication::screens();
        int monIdx = m_previewMonitor[idx]->currentIndex();
        QScreen *screen = (monIdx >= 0 && monIdx < screens.size())
                          ? screens[monIdx] : QGuiApplication::primaryScreen();

        if (!m_previewWin[idx]) {
            m_previewWin[idx] = new PreviewWindow;
            connect(m_previewWin[idx], &PreviewWindow::closed, this, [this, idx]() {
                m_previewWin[idx] = nullptr;
                m_previewOpenBtn[idx]->blockSignals(true);
                m_previewOpenBtn[idx]->setChecked(false);
                m_previewOpenBtn[idx]->setText("Open");
                m_previewOpenBtn[idx]->blockSignals(false);
            });
        }
        m_previewWin[idx]->setAlwaysOnTop(m_previewTopChk[idx]->isChecked());
        m_previewWin[idx]->openOnScreen(screen);
        m_previewOpenBtn[idx]->setText("Close");
    } else {
        if (m_previewWin[idx]) {
            m_previewWin[idx]->close();
            m_previewWin[idx] = nullptr;
        }
        m_previewOpenBtn[idx]->setText("Open");
    }
}

void OverlayTab::onPreviewTopChanged(int idx, bool top)
{
    if (m_previewWin[idx])
        m_previewWin[idx]->setAlwaysOnTop(top);
}
