// layout-dialog.cpp — per-tab font/layout settings dialog
#include "layout-dialog.hpp"
#include "lyrics-renderer.hpp"

#include <obs-module.h>

#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFrame>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QJsonDocument>

// ── OBS ABGR ↔ QColor helpers ─────────────────────────────────
static QColor obsToQt(long long raw)
{
    uint32_t c = (uint32_t)raw;
    int r = (c >>  0) & 0xFF;
    int g = (c >>  8) & 0xFF;
    int b = (c >> 16) & 0xFF;
    int a = (c >> 24) & 0xFF;
    return QColor(r, g, b, a ? a : 255);
}
static long long qtToObs(const QColor &c)
{
    return (long long)((uint32_t)c.red()
                      | ((uint32_t)c.green()  <<  8)
                      | ((uint32_t)c.blue()   << 16)
                      | ((uint32_t)c.alpha()  << 24));
}

// ── Colour-swatch button helpers ──────────────────────────────
static void applyColorStyle(QPushButton *btn, const QColor &c)
{
    btn->setProperty("userColor", c);
    QString bg = c.name(QColor::HexRgb);
    // Pick a contrasting label colour
    int luma = c.red()*299 + c.green()*587 + c.blue()*114;
    QString fg = (luma > 128000) ? "#000" : "#fff";
    btn->setStyleSheet(QString(
        "QPushButton{background:%1;color:%2;border:1px solid #666;"
        "border-radius:2px;font-size:10px;padding:1px 4px;}").arg(bg, fg));
    btn->setText(c.name(QColor::HexRgb).toUpper());
}
static QPushButton *makeColorBtn(QWidget *parent, const QColor &init = Qt::white)
{
    auto *btn = new QPushButton(parent);
    btn->setFixedHeight(24);
    btn->setMinimumWidth(80);
    applyColorStyle(btn, init);
    QObject::connect(btn, &QPushButton::clicked, btn, [btn]() {
        QColor cur = btn->property("userColor").value<QColor>();
        QColor c = QColorDialog::getColor(cur, btn, "Pick Colour",
                                          QColorDialog::ShowAlphaChannel);
        if (c.isValid()) applyColorStyle(btn, c);
    });
    return btn;
}
static QColor getColorBtn(QPushButton *btn)
{
    return btn ? btn->property("userColor").value<QColor>() : Qt::white;
}
static void setColorBtn(QPushButton *btn, const QColor &c)
{
    if (btn) applyColorStyle(btn, c);
}

// ── Where onApply() writes: straight into the OBS settings (the DEFAULT layout), or into a
// JSON object holding the segment's complete layout.
struct LayoutSink {
    obs_data_t  *global = nullptr;   // current global settings (the baseline)
    obs_data_t  *direct = nullptr;   // global mode: write here
    QJsonObject *diff   = nullptr;   // segment mode: collect the values here
    bool         storeAll = false;   // segment mode: store every value (a complete layout)

    void setInt(const std::string &k, long long v) {
        if (direct) obs_data_set_int(direct, k.c_str(), v);
        else if (storeAll || obs_data_get_int(global, k.c_str()) != v)
            diff->insert(QString::fromStdString(k), QJsonValue((qint64)v));
    }
    void setBool(const std::string &k, bool v) {
        if (direct) obs_data_set_bool(direct, k.c_str(), v);
        else if (storeAll || obs_data_get_bool(global, k.c_str()) != v)
            diff->insert(QString::fromStdString(k), v);
    }
    void setStr(const std::string &k, const QString &v) {
        if (direct) obs_data_set_string(direct, k.c_str(), v.toUtf8().constData());
        else if (storeAll || QString::fromUtf8(obs_data_get_string(global, k.c_str())) != v)
            diff->insert(QString::fromStdString(k), v);
    }
    void setDouble(const std::string &k, double v) {
        if (direct) obs_data_set_double(direct, k.c_str(), v);
        else if (storeAll || obs_data_get_double(global, k.c_str()) != v)
            diff->insert(QString::fromStdString(k), v);
    }
};

// ── Build one text-box settings group ────────────────────────
QWidget *LayoutDialog::buildBoxGroup(const QString &groupLabel, BoxRow &row)
{
    auto *grp  = new QGroupBox(groupLabel);
    auto *form = new QFormLayout(grp);
    form->setContentsMargins(8, 4, 8, 8);
    form->setSpacing(4);
    row.form = form;

    row.fontFace = new QFontComboBox;
    row.fontFace->setMaximumWidth(220);
    form->addRow("Font:", row.fontFace);

    row.fontSize = new QSpinBox;
    row.fontSize->setRange(6, 300);
    row.fontSize->setSuffix(" pt");
    row.fontSize->setFixedWidth(80);
    form->addRow("Size:", row.fontSize);

    auto *styleRow = new QWidget;
    auto *sl = new QHBoxLayout(styleRow);
    sl->setContentsMargins(0,0,0,0); sl->setSpacing(8);
    row.bold   = new QCheckBox("Bold");
    row.italic = new QCheckBox("Italic");
    sl->addWidget(row.bold); sl->addWidget(row.italic); sl->addStretch();
    form->addRow("Style:", styleRow);

    row.hAlign = new QComboBox;
    row.hAlign->addItems({"Left", "Center", "Right"});
    form->addRow("H-Align:", row.hAlign);

    row.vAlign = new QComboBox;
    row.vAlign->addItems({"Top", "Center", "Bottom"});
    form->addRow("V-Align:", row.vAlign);

    // ── Colour & opacity ──────────────────────────────────────
    row.colorBtn = makeColorBtn(grp, Qt::white);
    form->addRow("Text colour:", row.colorBtn);

    row.opacity = new QSpinBox;
    row.opacity->setRange(0, 100); row.opacity->setSuffix(" %"); row.opacity->setFixedWidth(70);
    row.opacity->setValue(100);
    form->addRow("Text opacity:", row.opacity);

    // ── Outline ───────────────────────────────────────────────
    row.outline = new QCheckBox("Enable");
    form->addRow("Outline:", row.outline);

    row.outlineSize = new QSpinBox;
    row.outlineSize->setRange(1, 20); row.outlineSize->setSuffix(" px"); row.outlineSize->setFixedWidth(70);
    form->addRow("Outline width:", row.outlineSize);

    row.outlineColorBtn = makeColorBtn(grp, Qt::black);
    form->addRow("Outline colour:", row.outlineColorBtn);

    // ── Background ────────────────────────────────────────────
    row.bgCheck = new QCheckBox("Enable");
    form->addRow("Background:", row.bgCheck);

    row.bgColorBtn = makeColorBtn(grp, Qt::black);
    form->addRow("BG colour:", row.bgColorBtn);

    row.bgOpacity = new QSpinBox;
    row.bgOpacity->setRange(0, 100); row.bgOpacity->setSuffix(" %"); row.bgOpacity->setFixedWidth(70);
    form->addRow("BG opacity:", row.bgOpacity);

    return grp;
}

// ── Constructor ───────────────────────────────────────────────
LayoutDialog::LayoutDialog(const QString &tabName, QWidget *parent)
    : QDialog(parent), m_tab(tabName)
{
    QString title = (tabName == "lyrics")     ? "Lyrics Layout"
                  : (tabName == "paragraph")  ? "Paragraph Layout"
                                              : "Scripture Layout";
    setWindowTitle(title);
    setWindowFlags(Qt::Dialog | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumWidth(380);

    auto *mainL  = new QVBoxLayout(this);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *cl      = new QVBoxLayout(content);
    cl->setSpacing(8);
    cl->setContentsMargins(4, 4, 4, 4);

    if (tabName == "lyrics") {
        // ── English lyrics text boxes ─────────────────────────
        m_rows.resize(6);
        m_rows[0].prefix = "lyrics_";
        m_rows[1].prefix = "section_";
        m_rows[2].prefix = "title_";
        m_rows[3].prefix = "ch_pinyin_";
        m_rows[4].prefix = "ch_chinese_";
        m_rows[5].prefix = "ch_english_";
        cl->addWidget(buildBoxGroup("English Lyrics — Main Text",              m_rows[0]));
        cl->addWidget(buildBoxGroup("Lyrics — Section Label (bottom-left)",    m_rows[1]));
        cl->addWidget(buildBoxGroup("Lyrics — Song Title (bottom-right)",      m_rows[2]));
        cl->addWidget(buildBoxGroup("Chinese — Pinyin (top strip)",            m_rows[3]));
        cl->addWidget(buildBoxGroup("Chinese — Characters (middle)",           m_rows[4]));
        cl->addWidget(buildBoxGroup("Chinese — English Translation (bottom)",  m_rows[5]));

        // ── English lyrics layout ─────────────────────────────
        {
            auto *lGrp  = new QGroupBox("Lyrics Layout");
            auto *lForm = new QFormLayout(lGrp);
            lForm->setContentsMargins(8, 4, 8, 8);
            m_bottomBar = new QSpinBox;
            m_bottomBar->setRange(0, 400);
            m_bottomBar->setSuffix(" px");
            m_bottomBar->setFixedWidth(80);
            lForm->addRow("Bottom bar height:", m_bottomBar);
            cl->addWidget(lGrp);
        }

        // ── Chinese interleave layout ─────────────────────────
        {
            auto *cGrp  = new QGroupBox("Chinese Interleave Layout");
            auto *cForm = new QFormLayout(cGrp);
            cForm->setContentsMargins(8, 4, 8, 8);
            cForm->setSpacing(4);

            auto mkSpin = [&](int lo, int hi) {
                auto *s = new QSpinBox;
                s->setRange(lo, hi); s->setSuffix(" px"); s->setFixedWidth(90);
                return s;
            };
            m_chPinyinY        = mkSpin(0, 4320);
            m_chPinyinSpacing  = new QDoubleSpinBox;
            m_chPinyinSpacing->setRange(0.0, 20.0);
            m_chPinyinSpacing->setSingleStep(0.1);
            m_chPinyinSpacing->setDecimals(1);
            m_chPinyinSpacing->setFixedWidth(90);
            m_chChineseY       = mkSpin(0, 4320);
            m_chChineseStripH  = mkSpin(10, 400);
            m_chChineseInterval= mkSpin(0, 400);
            m_chEnglishY       = mkSpin(0, 4320);

            cForm->addRow("Pinyin starting Y:",            m_chPinyinY);
            cForm->addRow("Pinyin line spacing (blanks):", m_chPinyinSpacing);
            cForm->addRow("Characters starting Y:",        m_chChineseY);
            cForm->addRow("Characters strip height:",      m_chChineseStripH);
            cForm->addRow("Characters strip interval:",    m_chChineseInterval);
            cForm->addRow("English translation starting Y:", m_chEnglishY);
            cl->addWidget(cGrp);
        }
    } else {
        m_rows.resize(2);
        if (tabName == "paragraph") {
            m_rows[0].prefix = "paragraph_title_";
            m_rows[1].prefix = "paragraph_text_";
            m_titleHKey  = "paragraph_title_h";
            m_hMarginKey = "paragraph_h_margin";
        } else {
            m_rows[0].prefix = "scr_title_";
            m_rows[1].prefix = "scr_text_";
            m_titleHKey  = "scrip_title_h";
            m_hMarginKey = "scrip_h_margin";
        }
        cl->addWidget(buildBoxGroup("Title Strip", m_rows[0]));
        cl->addWidget(buildBoxGroup("Body Text",   m_rows[1]));

        auto *lGrp  = new QGroupBox("Layout");
        auto *lForm = new QFormLayout(lGrp);
        lForm->setContentsMargins(8, 4, 8, 8);
        m_titleH = new QSpinBox;
        m_titleH->setRange(10, 400); m_titleH->setSuffix(" px"); m_titleH->setFixedWidth(80);
        m_hMargin = new QSpinBox;
        m_hMargin->setRange(0, 400); m_hMargin->setSuffix(" px"); m_hMargin->setFixedWidth(80);
        lForm->addRow("Title strip height:", m_titleH);
        lForm->addRow("Left/right margin:",  m_hMargin);
        if (tabName == "paragraph") {
            m_paragraphTextY = new QSpinBox;
            m_paragraphTextY->setRange(0, 1080); m_paragraphTextY->setSuffix(" px"); m_paragraphTextY->setFixedWidth(80);
            m_paragraphTextY->setToolTip("Extra space between the title strip and the body text box");
            lForm->addRow("Body text top offset:", m_paragraphTextY);
        }
        cl->addWidget(lGrp);
    }

    cl->addStretch();
    scroll->setWidget(content);
    mainL->addWidget(scroll);

    auto *btnBox   = new QDialogButtonBox;
    auto *applyBtn = btnBox->addButton("Apply", QDialogButtonBox::ApplyRole);
    m_resetBtn = btnBox->addButton("Load Default Layout", QDialogButtonBox::ResetRole);
    m_resetBtn->setToolTip("Fill the form with the current default paragraph layout. Press Apply to keep it.");
    m_resetBtn->hide();   // only shown in segment mode
    m_applyBtn = applyBtn;
    btnBox->addButton(QDialogButtonBox::Close);
    connect(applyBtn, &QPushButton::clicked, this, &LayoutDialog::onApply);
    connect(m_resetBtn, &QPushButton::clicked, this, &LayoutDialog::onResetToGlobal);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::close);
    mainL->addWidget(btnBox);

    resize(400, 700);
    loadSettings();
}

// ── Segment mode ──────────────────────────────────────────────
void LayoutDialog::setSegmentMode(const QString &segmentTitle, const QByteArray &overrideJson)
{
    m_segmentMode = true;
    m_override    = QJsonDocument::fromJson(overrideJson).object();
    // Used modally on the stack, so it must not delete itself when it closes.
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(QString("Layout for \"%1\" (this segment only)").arg(segmentTitle));
    if (m_applyBtn) m_applyBtn->setText("Apply to This Segment");
    if (m_resetBtn) m_resetBtn->show();

    // The body text's colour and outline colour always come from the colour presets.
    if (m_rows.size() > 1) {
        BoxRow &body = m_rows[1];
        for (QWidget *w : { (QWidget *)body.colorBtn, (QWidget *)body.outlineColorBtn }) {
            if (!w) continue;
            w->hide();
            if (body.form)
                if (QWidget *lbl = body.form->labelForField(w)) lbl->hide();
        }
    }
    loadSettings();
}

// Segment mode: replace the form's values with the current DEFAULT layout (the segment keeps
// its own layout until Apply is pressed).
void LayoutDialog::onResetToGlobal()
{
    m_override = QJsonObject();
    loadSettings();
}

// ── Read current OBS settings into the dialog ─────────────────
// In segment mode a value present in m_override wins over the global setting.
void LayoutDialog::loadSettings()
{
    obs_source_t *src = lyrics_renderer_get_source();
    if (!src) return;
    obs_data_t *s = obs_source_get_settings(src);
    if (!s) return;

    auto I = [&](const std::string &k) -> long long {
        const QString qk = QString::fromStdString(k);
        return m_override.contains(qk) ? (long long)m_override.value(qk).toDouble()
                                       : (long long)obs_data_get_int(s, k.c_str());
    };
    auto B = [&](const std::string &k) -> bool {
        const QString qk = QString::fromStdString(k);
        return m_override.contains(qk) ? m_override.value(qk).toBool()
                                       : obs_data_get_bool(s, k.c_str());
    };
    auto Str = [&](const std::string &k) -> QString {
        const QString qk = QString::fromStdString(k);
        return m_override.contains(qk) ? m_override.value(qk).toString()
                                       : QString::fromUtf8(obs_data_get_string(s, k.c_str()));
    };

    for (auto &row : m_rows) {
        std::string p = row.prefix;
        QString face = Str(p + "font_face");
        if (!face.isEmpty())
            row.fontFace->setCurrentFont(QFont(face));
        row.fontSize->setValue((int)I(p + "font_size"));
        row.bold->setChecked  (B(p + "bold"));
        row.italic->setChecked(B(p + "italic"));
        row.hAlign->setCurrentIndex((int)I(p + "align"));
        row.vAlign->setCurrentIndex((int)I(p + "valign"));

        // Colour / style
        if (row.colorBtn)
            setColorBtn(row.colorBtn, obsToQt(I(p + "color")));
        if (row.opacity)
            row.opacity->setValue((int)I(p + "opacity"));
        if (row.outline)
            row.outline->setChecked(B(p + "outline"));
        if (row.outlineSize)
            row.outlineSize->setValue((int)I(p + "outline_size"));
        if (row.outlineColorBtn)
            setColorBtn(row.outlineColorBtn, obsToQt(I(p + "outline_color")));
        if (row.bgCheck)
            row.bgCheck->setChecked(B(p + "bg"));
        if (row.bgColorBtn)
            setColorBtn(row.bgColorBtn, obsToQt(I(p + "bg_color")));
        if (row.bgOpacity)
            row.bgOpacity->setValue((int)I(p + "bg_opacity"));
    }

    if (m_bottomBar)
        m_bottomBar->setValue((int)obs_data_get_int(s, "bottom_bar_h"));

    auto readOrDef = [&](const char *key, int def) -> int {
        return obs_data_has_user_value(s, key)
               ? (int)obs_data_get_int(s, key) : def;
    };
    if (m_chPinyinY)        m_chPinyinY->setValue(       readOrDef("ch_pinyin_y",          0));
    if (m_chPinyinSpacing)  m_chPinyinSpacing->setValue(
        obs_data_has_user_value(s, "ch_pinyin_spacing")
        ? obs_data_get_double(s, "ch_pinyin_spacing") : 2.0);
    if (m_chChineseY)       m_chChineseY->setValue(      readOrDef("ch_chinese_y",         34));
    if (m_chChineseStripH)  m_chChineseStripH->setValue( readOrDef("ch_chinese_strip_h",  100));
    if (m_chChineseInterval)m_chChineseInterval->setValue(readOrDef("ch_chinese_interval", 100));
    if (m_chEnglishY)       m_chEnglishY->setValue(      readOrDef("ch_english_y",        280));

    if (m_titleH  && m_titleHKey)  m_titleH->setValue( (int)I(m_titleHKey));
    if (m_hMargin && m_hMarginKey) m_hMargin->setValue((int)I(m_hMarginKey));
    if (m_paragraphTextY)
        m_paragraphTextY->setValue(m_override.contains("paragraph_text_y")
                                   ? m_override.value("paragraph_text_y").toInt()
                                   : readOrDef("paragraph_text_y", 0));

    obs_data_release(s);
}

// ── Collect the dialog values into a sink ─────────────────────
void LayoutDialog::writeValues(LayoutSink &k) const
{
    for (const auto &row : m_rows) {
        std::string p = row.prefix;
        // Segment mode: the body text's colours come from the colour presets, never stored here.
        const bool skipColors = m_segmentMode && m_rows.size() > 1 && &row == &m_rows[1];

        k.setStr (p + "font_face", row.fontFace->currentFont().family());
        k.setInt (p + "font_size", row.fontSize->value());
        k.setBool(p + "bold",      row.bold->isChecked());
        k.setBool(p + "italic",    row.italic->isChecked());
        k.setInt (p + "align",     row.hAlign->currentIndex());
        k.setInt (p + "valign",    row.vAlign->currentIndex());

        // Colour / style
        if (row.colorBtn && !skipColors)
            k.setInt(p + "color", qtToObs(getColorBtn(row.colorBtn)));
        if (row.opacity)
            k.setInt(p + "opacity", row.opacity->value());
        if (row.outline)
            k.setBool(p + "outline", row.outline->isChecked());
        if (row.outlineSize)
            k.setInt(p + "outline_size", row.outlineSize->value());
        if (row.outlineColorBtn && !skipColors)
            k.setInt(p + "outline_color", qtToObs(getColorBtn(row.outlineColorBtn)));
        if (row.bgCheck)
            k.setBool(p + "bg", row.bgCheck->isChecked());
        if (row.bgColorBtn)
            k.setInt(p + "bg_color", qtToObs(getColorBtn(row.bgColorBtn)));
        if (row.bgOpacity)
            k.setInt(p + "bg_opacity", row.bgOpacity->value());
    }

    if (m_bottomBar)
        k.setInt("bottom_bar_h", m_bottomBar->value());

    if (m_chPinyinY)        k.setInt("ch_pinyin_y",         m_chPinyinY->value());
    if (m_chPinyinSpacing)  k.setDouble("ch_pinyin_spacing", m_chPinyinSpacing->value());
    if (m_chChineseY)       k.setInt("ch_chinese_y",        m_chChineseY->value());
    if (m_chChineseStripH)  k.setInt("ch_chinese_strip_h",  m_chChineseStripH->value());
    if (m_chChineseInterval)k.setInt("ch_chinese_interval", m_chChineseInterval->value());
    if (m_chEnglishY)       k.setInt("ch_english_y",        m_chEnglishY->value());

    if (m_titleH  && m_titleHKey)  k.setInt(m_titleHKey,  m_titleH->value());
    if (m_hMargin && m_hMarginKey) k.setInt(m_hMarginKey, m_hMargin->value());
    if (m_paragraphTextY) k.setInt("paragraph_text_y", m_paragraphTextY->value());
}

// ── Apply ─────────────────────────────────────────────────────
// Default mode: write to OBS (the default copied into NEW paragraph segments; existing segments
// are not affected).  Segment mode: return this segment's COMPLETE layout and close.
void LayoutDialog::onApply()
{
    obs_source_t *src = lyrics_renderer_get_source();
    if (!src) return;
    obs_data_t *s = obs_source_get_settings(src);
    if (!s) return;

    if (m_segmentMode) {
        QJsonObject all;
        LayoutSink k{ s, nullptr, &all, true };   // store every value: a complete layout
        writeValues(k);
        m_resultJson = all.isEmpty() ? QByteArray()
                                     : QJsonDocument(all).toJson(QJsonDocument::Compact);
        obs_data_release(s);
        accept();
        return;
    }

    LayoutSink k{ s, s, nullptr };
    writeValues(k);
    obs_source_update(src, s);
    obs_data_release(s);
}
