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

// ── Build one text-box settings group ────────────────────────
QWidget *LayoutDialog::buildBoxGroup(const QString &groupLabel, BoxRow &row)
{
    auto *grp  = new QGroupBox(groupLabel);
    auto *form = new QFormLayout(grp);
    form->setContentsMargins(8, 4, 8, 8);
    form->setSpacing(4);

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

    return grp;
}

// ── Constructor ───────────────────────────────────────────────
LayoutDialog::LayoutDialog(const QString &tabName, QWidget *parent)
    : QDialog(parent), m_tab(tabName)
{
    QString title = (tabName == "lyrics") ? "Lyrics Layout"
                  : (tabName == "paragraph")    ? "Call to Worship Layout"
                                          : "Scripture Layout";
    setWindowTitle(title);
    setWindowFlags(Qt::Dialog | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumWidth(360);

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
        cl->addWidget(lGrp);
    }

    cl->addStretch();
    scroll->setWidget(content);
    mainL->addWidget(scroll);

    auto *btnBox   = new QDialogButtonBox;
    auto *applyBtn = btnBox->addButton("Apply", QDialogButtonBox::ApplyRole);
    btnBox->addButton(QDialogButtonBox::Close);
    connect(applyBtn, &QPushButton::clicked, this, &LayoutDialog::onApply);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::close);
    mainL->addWidget(btnBox);

    resize(380, 620);
    loadSettings();
}

// ── Read current OBS settings into the dialog ─────────────────
void LayoutDialog::loadSettings()
{
    obs_source_t *src = lyrics_renderer_get_source();
    if (!src) return;
    obs_data_t *s = obs_source_get_settings(src);
    if (!s) return;

    for (auto &row : m_rows) {
        std::string p = row.prefix;
        QString face = QString::fromUtf8(
            obs_data_get_string(s, (p + "font_face").c_str()));
        if (!face.isEmpty())
            row.fontFace->setCurrentFont(QFont(face));
        row.fontSize->setValue((int)obs_data_get_int(s, (p + "font_size").c_str()));
        row.bold->setChecked  (obs_data_get_bool(s, (p + "bold").c_str()));
        row.italic->setChecked(obs_data_get_bool(s, (p + "italic").c_str()));
        row.hAlign->setCurrentIndex((int)obs_data_get_int(s, (p + "align").c_str()));
        row.vAlign->setCurrentIndex((int)obs_data_get_int(s, (p + "valign").c_str()));
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

    if (m_titleH  && m_titleHKey)  m_titleH->setValue( (int)obs_data_get_int(s, m_titleHKey));
    if (m_hMargin && m_hMarginKey) m_hMargin->setValue((int)obs_data_get_int(s, m_hMarginKey));

    obs_data_release(s);
}

// ── Write dialog values back to OBS and trigger re-render ─────
void LayoutDialog::onApply()
{
    obs_source_t *src = lyrics_renderer_get_source();
    if (!src) return;
    obs_data_t *s = obs_source_get_settings(src);
    if (!s) return;

    for (const auto &row : m_rows) {
        std::string p = row.prefix;
        obs_data_set_string(s, (p + "font_face").c_str(),
            row.fontFace->currentFont().family().toUtf8().constData());
        obs_data_set_int (s, (p + "font_size").c_str(), row.fontSize->value());
        obs_data_set_bool(s, (p + "bold").c_str(),      row.bold->isChecked());
        obs_data_set_bool(s, (p + "italic").c_str(),    row.italic->isChecked());
        obs_data_set_int (s, (p + "align").c_str(),     row.hAlign->currentIndex());
        obs_data_set_int (s, (p + "valign").c_str(),    row.vAlign->currentIndex());
    }

    if (m_bottomBar)
        obs_data_set_int(s, "bottom_bar_h", m_bottomBar->value());

    if (m_chPinyinY)        obs_data_set_int(s, "ch_pinyin_y",         m_chPinyinY->value());
    if (m_chPinyinSpacing)  obs_data_set_double(s, "ch_pinyin_spacing", m_chPinyinSpacing->value());
    if (m_chChineseY)       obs_data_set_int(s, "ch_chinese_y",        m_chChineseY->value());
    if (m_chChineseStripH)  obs_data_set_int(s, "ch_chinese_strip_h",  m_chChineseStripH->value());
    if (m_chChineseInterval)obs_data_set_int(s, "ch_chinese_interval", m_chChineseInterval->value());
    if (m_chEnglishY)       obs_data_set_int(s, "ch_english_y",        m_chEnglishY->value());

    if (m_titleH  && m_titleHKey)  obs_data_set_int(s, m_titleHKey,  m_titleH->value());
    if (m_hMargin && m_hMarginKey) obs_data_set_int(s, m_hMarginKey, m_hMargin->value());

    obs_source_update(src, s);
    obs_data_release(s);
}
