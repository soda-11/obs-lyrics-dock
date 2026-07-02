// call-to-worship-tab.cpp  v1.0

#include "call-to-worship-tab.hpp"
#include "lyrics-renderer.hpp"
#include "websocket-server.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSignalBlocker>

static constexpr int kCtwHdrBtnH = 38;
static constexpr int kCtwBtnW    = 68;

// ══════════════════════════════════════════════════════════════
// CallToWorshipTab
// ══════════════════════════════════════════════════════════════

CallToWorshipTab::CallToWorshipTab(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    loadState();
}

void CallToWorshipTab::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *splitter = new QSplitter(Qt::Horizontal);

    // ── LEFT: Text Input Panel ────────────────────────────────
    auto *textGroup  = new QGroupBox("Text");
    textGroup->setMaximumWidth(300);
    auto *textLayout = new QVBoxLayout(textGroup);
    textLayout->setSpacing(6);

    auto *titleLbl = new QLabel("Title:");
    titleLbl->setStyleSheet("color:#aaa;font-size:9px;font-weight:bold;");
    m_titleEdit = new QLineEdit;
    m_titleEdit->setPlaceholderText("e.g. Call to Worship");
    m_titleEdit->setStyleSheet(
        "QLineEdit{background:#1e1e1e;color:#eee;border:1px solid #444;"
        "border-radius:3px;padding:3px 5px;}"
        "QLineEdit:focus{border:1px solid #4da6ff;}");

    auto *bodyLbl = new QLabel("Text:");
    bodyLbl->setStyleSheet("color:#aaa;font-size:9px;font-weight:bold;");
    m_textEdit = new QPlainTextEdit;
    m_textEdit->setPlaceholderText(
        "Paste Call to Worship text here...\n\n"
        "Blank lines create new slide groups.");
    m_textEdit->setStyleSheet(
        "QPlainTextEdit{background:#1e1e1e;color:#eee;border:1px solid #444;"
        "border-radius:3px;padding:4px;}"
        "QPlainTextEdit:focus{border:1px solid #4da6ff;}");

    auto *applyBtn = new QPushButton("▶ Apply");
    applyBtn->setFixedHeight(32);
    applyBtn->setStyleSheet(
        "QPushButton{background:#1a3a5c;color:#4da6ff;"
        "border:1px solid #2a5a8c;border-radius:4px;"
        "font-size:11px;font-weight:bold;}"
        "QPushButton:hover{background:#254e7a;}"
        "QPushButton:pressed{background:#0e2a44;}");
    applyBtn->setToolTip("Generate slides from the text above and broadcast to remote clients");

    textLayout->addWidget(titleLbl);
    textLayout->addWidget(m_titleEdit);
    textLayout->addWidget(bodyLbl);
    textLayout->addWidget(m_textEdit, 1);
    textLayout->addWidget(applyBtn);

    // ── CENTER: Slides Panel ──────────────────────────────────
    auto *slidesGroup  = new QGroupBox("Slides");
    auto *slidesLayout = new QVBoxLayout(slidesGroup);
    slidesLayout->setContentsMargins(0, 4, 0, 0);
    slidesLayout->setSpacing(0);

    auto *slidesHeader = new QWidget;
    slidesHeader->setFixedHeight(kCtwHdrBtnH + 4);
    slidesHeader->setStyleSheet("background:#2a2a2a;border-bottom:1px solid #333;");
    auto *slidesHeaderL = new QHBoxLayout(slidesHeader);
    slidesHeaderL->setContentsMargins(8, 1, 6, 1);
    slidesHeaderL->setSpacing(4);

    auto *slidesTitleLbl = new QLabel("Slides");
    QFont hf = slidesTitleLbl->font();
    hf.setBold(true); hf.setPointSize(9);
    slidesTitleLbl->setFont(hf);
    slidesTitleLbl->setStyleSheet("color:#ccc;");

    for (int i = 0; i < 3; ++i) {
        m_colorBtn[i] = new ColorPresetButton(i + 1, slidesHeader);
        m_colorBtn[i]->setFixedSize(kCtwBtnW, kCtwHdrBtnH);
    }

    auto *blankBtn = new QPushButton(QString::fromUtf8("■ Blank"));
    blankBtn->setFixedSize(kCtwBtnW, kCtwHdrBtnH);
    blankBtn->setToolTip("Clear text from OBS");
    blankBtn->setStyleSheet(
        "QPushButton{background:#4a1a1a;color:#ff6b6b;"
        "border:1px solid #8b2020;border-radius:3px;"
        "font-size:12px;padding:0 2px;}"
        "QPushButton:hover{background:#6a2020;}");

    slidesHeaderL->addWidget(slidesTitleLbl);
    slidesHeaderL->addSpacing(6);
    for (int i = 0; i < 3; ++i)
        slidesHeaderL->addWidget(m_colorBtn[i]);
    slidesHeaderL->addStretch();
    slidesHeaderL->addWidget(blankBtn);
    slidesLayout->addWidget(slidesHeader);

    m_slideArea = new QScrollArea;
    m_slideArea->setWidgetResizable(true);
    m_slideArea->setStyleSheet("QScrollArea{background:#1a1a1a;border:none;}");
    m_slideContainer = new QWidget;
    m_slideContainer->setStyleSheet("background:#1a1a1a;");
    m_slideGrid = new QGridLayout(m_slideContainer);
    m_slideGrid->setContentsMargins(8, 8, 8, 8);
    m_slideGrid->setSpacing(8);
    m_slideGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_slideArea->setWidget(m_slideContainer);
    slidesLayout->addWidget(m_slideArea, 1);

    auto *ctrlBar = new QWidget;
    ctrlBar->setStyleSheet("background:#222;border-top:1px solid #333;");
    auto *ctrlL = new QHBoxLayout(ctrlBar);
    ctrlL->setContentsMargins(6, 4, 6, 4);
    ctrlL->setSpacing(4);

    m_prevBtn = new QPushButton(QString::fromUtf8("◄"));
    m_prevBtn->setFixedWidth(36);
    m_prevBtn->setToolTip("Previous slide");

    m_slideCounter = new QLabel(QString::fromUtf8("—"));
    m_slideCounter->setAlignment(Qt::AlignCenter);
    m_slideCounter->setMinimumWidth(52);
    m_slideCounter->setStyleSheet("color:#888;font-size:9px;");

    m_nextBtn = new QPushButton(QString::fromUtf8("►"));
    m_nextBtn->setFixedWidth(36);
    m_nextBtn->setToolTip("Next slide");

    ctrlL->addWidget(m_prevBtn);
    ctrlL->addWidget(m_slideCounter);
    ctrlL->addWidget(m_nextBtn);
    ctrlL->addStretch();
    slidesLayout->addWidget(ctrlBar);

    // ── RIGHT: Background Media Gallery ──────────────────────
    auto *galleryGroup  = new QGroupBox("Background");
    auto *galleryLayout = new QVBoxLayout(galleryGroup);
    galleryLayout->setContentsMargins(0, 0, 0, 0);
    m_gallery = new MediaGallery(this, "ctw-gallery.json");
    galleryLayout->addWidget(m_gallery);

    splitter->addWidget(textGroup);
    splitter->addWidget(slidesGroup);
    splitter->addWidget(galleryGroup);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({240, 500, 250});

    root->addWidget(splitter, 1);

    // ── Connections ───────────────────────────────────────────
    connect(applyBtn, &QPushButton::clicked, this, &CallToWorshipTab::onApply);
    connect(blankBtn, &QPushButton::clicked, this, [this]() {
        if (m_activeSlide >= 0 && m_activeSlide < m_slideBtns.size())
            m_slideBtns[m_activeSlide]->setActive(false);
        m_activeSlide = -1;
        m_slideCounter->setText(QString::fromUtf8("—"));
        m_prevBtn->setEnabled(false);
        m_nextBtn->setEnabled(!m_slideBtns.isEmpty());
        lyrics_renderer_scripture_clear();
        emit slideActivated(QString(), QString());
    });
    connect(m_prevBtn, &QPushButton::clicked, this, &CallToWorshipTab::onPrevSlide);
    connect(m_nextBtn, &QPushButton::clicked, this, &CallToWorshipTab::onNextSlide);
    for (int i = 0; i < 3; ++i) {
        connect(m_colorBtn[i], &ColorPresetButton::colorsApplied,
                this, [this, i](QColor fc, QColor oc) {
                    for (int j = 0; j < 3; ++j)
                        m_colorBtn[j]->setActive(j == i);
                    m_presetFont[i]    = fc;
                    m_presetOutline[i] = oc;
                    onColorPresetApplied(fc, oc);
                    saveState();
                });
    }
    connect(m_gallery, &MediaGallery::mediaActivated,
            this, &CallToWorshipTab::playbackStarted);
}

QList<QString> CallToWorshipTab::parseSlides() const
{
    const int lpp = 4;
    const QStringList allLines = m_textEdit->toPlainText().split('\n');
    QList<QString> result;
    QStringList paragraph;

    auto flushParagraph = [&]() {
        if (paragraph.isEmpty()) return;
        for (int i = 0; i < paragraph.size(); i += lpp) {
            QStringList chunk;
            for (int j = i; j < qMin(i + lpp, paragraph.size()); ++j)
                chunk << paragraph[j];
            if (!chunk.isEmpty())
                result << chunk.join('\n');
        }
        paragraph.clear();
    };

    for (const QString &line : allLines) {
        if (line.trimmed().isEmpty())
            flushParagraph();
        else
            paragraph << line;
    }
    flushParagraph();
    return result;
}

void CallToWorshipTab::rebuildSlides()
{
    while (QLayoutItem *item = m_slideGrid->takeAt(0)) {
        if (item->widget()) delete item->widget();
        delete item;
    }
    m_slideBtns.clear();
    m_activeSlide = -1;

    QList<QString> slides = parseSlides();
    if (slides.isEmpty()) {
        m_slideCounter->setText(QString::fromUtf8("—"));
        m_prevBtn->setEnabled(false);
        m_nextBtn->setEnabled(false);
        m_slideContainer->adjustSize();
        return;
    }

    int areaW = m_slideArea->viewport()->width();
    int cols  = qMax(1, areaW / (ScriptureSlideBtn::BASE_W + 8));

    int row = 0, col = 0;
    for (int i = 0; i < slides.size(); ++i) {
        auto *btn = new ScriptureSlideBtn(i, slides[i], m_slideContainer);
        connect(btn, &ScriptureSlideBtn::clicked,
                this, &CallToWorshipTab::onSlideClicked);
        m_slideGrid->addWidget(btn, row, col);
        m_slideBtns << btn;
        if (++col >= cols) { col = 0; ++row; }
    }

    m_slideCounter->setText(QString("0 / %1").arg(slides.size()));
    m_prevBtn->setEnabled(false);
    m_nextBtn->setEnabled(slides.size() > 1);
    m_slideContainer->adjustSize();
    m_slideContainer->update();
}

void CallToWorshipTab::onApply()
{
    rebuildSlides();
    saveState();
    emit stateChanged(m_titleEdit->text(), m_textEdit->toPlainText());
}

void CallToWorshipTab::onSlideClicked(int slideIndex)
{
    setActiveSlide(slideIndex);
    QString title     = m_titleEdit->text();
    QString slideText = m_slideBtns[slideIndex]->slideText();
    lyrics_renderer_scripture_push(title, slideText);
    emit slideActivated(title, slideText);
}

void CallToWorshipTab::setActiveSlide(int index)
{
    if (m_activeSlide >= 0 && m_activeSlide < m_slideBtns.size())
        m_slideBtns[m_activeSlide]->setActive(false);
    m_activeSlide = index;
    if (index >= 0 && index < m_slideBtns.size()) {
        m_slideBtns[index]->setActive(true);
        m_slideCounter->setText(
            QString("%1 / %2").arg(index + 1).arg(m_slideBtns.size()));
    }
    m_prevBtn->setEnabled(index > 0);
    m_nextBtn->setEnabled(index >= 0 && index < m_slideBtns.size() - 1);
}

void CallToWorshipTab::onPrevSlide()
{
    if (m_activeSlide > 0)
        onSlideClicked(m_activeSlide - 1);
    else if (!m_slideBtns.isEmpty())
        onSlideClicked(0);
}

void CallToWorshipTab::onNextSlide()
{
    if (m_activeSlide >= 0 && m_activeSlide < m_slideBtns.size() - 1)
        onSlideClicked(m_activeSlide + 1);
}

void CallToWorshipTab::onColorPresetApplied(QColor fontColor, QColor outlineColor)
{
    uint32_t fc = ((uint32_t)fontColor.blue()  << 16)
                | ((uint32_t)fontColor.green() <<  8)
                | ((uint32_t)fontColor.red());
    uint32_t oc = ((uint32_t)outlineColor.blue()  << 16)
                | ((uint32_t)outlineColor.green() <<  8)
                | ((uint32_t)outlineColor.red());
    lyrics_renderer_set_scripture_text_color(fc, oc);
}

void CallToWorshipTab::stopPlayback()    { m_gallery->stopPlayback(); }
void CallToWorshipTab::clearActiveCard() { m_gallery->clearActiveCard(); }

QString CallToWorshipTab::statePath() const
{
    QString dir = QStandardPaths::writableLocation(
                      QStandardPaths::AppDataLocation)
                  + "/obs-lyrics-dock";
    QDir().mkpath(dir);
    return dir + "/call-to-worship-tab.json";
}

void CallToWorshipTab::saveState()
{
    QJsonObject obj;
    obj["title"] = m_titleEdit ? m_titleEdit->text() : QString();
    obj["text"]  = m_textEdit  ? m_textEdit->toPlainText() : QString();

    QJsonArray presets;
    for (int i = 0; i < 3; ++i) {
        QJsonObject p;
        p["font"]    = (qint64)(m_colorBtn[i]
                            ? (m_colorBtn[i]->fontColor().rgb() & 0x00FFFFFF)
                            : 0xFFFFFF);
        p["outline"] = (qint64)(m_colorBtn[i]
                            ? (m_colorBtn[i]->outlineColor().rgb() & 0x00FFFFFF)
                            : 0x000000);
        presets.append(p);
    }
    obj["colorPresets"] = presets;

    QFile f(statePath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson());
}

void CallToWorshipTab::loadState()
{
    QFile f(statePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    auto obj = doc.object();

    if (m_titleEdit) {
        QSignalBlocker blk(m_titleEdit);
        m_titleEdit->setText(obj["title"].toString());
    }
    if (m_textEdit) {
        QSignalBlocker blk(m_textEdit);
        m_textEdit->setPlainText(obj["text"].toString());
    }

    auto pa = obj["colorPresets"].toArray();
    for (int i = 0; i < 3 && i < pa.size(); ++i) {
        auto po = pa[i].toObject();
        int frgb = po["font"].toInt(0xFFFFFF);
        int orgb = po["outline"].toInt(0x000000);
        QColor fc = QColor((frgb >> 16) & 0xFF, (frgb >> 8) & 0xFF, frgb & 0xFF);
        QColor oc = QColor((orgb >> 16) & 0xFF, (orgb >> 8) & 0xFF, orgb & 0xFF);
        if (m_colorBtn[i]) m_colorBtn[i]->setColors(fc, oc);
        m_presetFont[i]    = fc;
        m_presetOutline[i] = oc;
    }
}
