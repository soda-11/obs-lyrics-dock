// song-edit-dialog.cpp  v10.2
// Trilingual section editor: Block 1 = Chinese/English lyrics,
// Block 2 = Pinyin (optional), Block 3 = English translation (optional).
// Quick Paste tab: side-by-side lyrics + pinyin with auto-generate.
#include "song-edit-dialog.hpp"
#include "pinyin-engine.hpp"
#include "icon-helpers.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QUuid>
#include <QStyle>
#include <QDropEvent>
#include <QSplitter>

// ── SectionListWidget ────────────────────────────────────────

SectionListWidget::SectionListWidget(QWidget *parent) : QListWidget(parent)
{
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragDropOverwriteMode(false); // drag onto item inserts, never replaces
}

void SectionListWidget::dropEvent(QDropEvent *e)
{
    QListWidget::dropEvent(e);
    emit orderChanged();
}

// ── SongEditDialog ────────────────────────────────────────────

SongEditDialog::SongEditDialog(QWidget *parent, int initialSectionIndex)
    : QDialog(parent)
{
    m_song.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    buildUi(initialSectionIndex);
}

SongEditDialog::SongEditDialog(const Song &song, QWidget *parent,
                               int initialSectionIndex)
    : QDialog(parent), m_song(song)
{
    buildUi(initialSectionIndex);
    m_titleEdit->setText(m_song.title);
    m_artistEdit->setText(m_song.artist);
    populateSectionList();
    int row = (initialSectionIndex >= 0 &&
               initialSectionIndex < m_song.sections.size())
              ? initialSectionIndex : 0;
    if (!m_song.sections.isEmpty()) {
        m_sectionList->setCurrentRow(row);
        // setCurrentRow() does NOT emit currentRowChanged when the list widget
        // is already at that row (which happens when row==0 after populateSectionList
        // left it at 0 with signals blocked). Force the load explicitly.
        m_currentSection = row;
        loadSectionIntoEditor(row);
    }
    syncQuickPasteFromSections();
}

void SongEditDialog::buildUi(int initialSectionIndex)
{
    setWindowTitle("Edit Song");
    setMinimumSize(920, 580);

    // Song Info
    auto *metaGroup = new QGroupBox("Song Info");
    auto *metaForm  = new QFormLayout(metaGroup);
    m_titleEdit  = new QLineEdit;
    m_artistEdit = new QLineEdit;
    metaForm->addRow("Title:",  m_titleEdit);
    metaForm->addRow("Artist:", m_artistEdit);

    // Tabs
    auto *tabs = new QTabWidget;

    // ── Tab 1: Sections ───────────────────────────────────────
    auto *secWidget = new QWidget;
    auto *secLayout = new QHBoxLayout(secWidget);

    // ── LEFT: section list ────────────────────────────────────
    auto *leftPanel  = new QWidget; leftPanel->setMaximumWidth(140);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0,0,0,0);
    auto *listHint = new QLabel("Sections  (drag to reorder)");
    listHint->setStyleSheet("color:#888;font-size:9px;");
    m_sectionList = new SectionListWidget;

    auto *btnRow = new QHBoxLayout;
    m_addSectionBtn       = new QPushButton;
    m_removeSectionBtn    = new QPushButton;
    m_duplicateSectionBtn = new QPushButton;
    m_moveSectionUpBtn    = new QPushButton;
    m_moveSectionDownBtn  = new QPushButton;

    m_addSectionBtn->setIcon(
        style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    m_addSectionBtn->setToolTip("Add new section");
    m_removeSectionBtn->setIcon(
        style()->standardIcon(QStyle::SP_TrashIcon));
    m_removeSectionBtn->setToolTip("Remove section");
    m_duplicateSectionBtn->setIcon(makeDuplicateIcon());
    m_duplicateSectionBtn->setToolTip("Duplicate section");
    m_moveSectionUpBtn->setIcon(
        style()->standardIcon(QStyle::SP_ArrowUp));
    m_moveSectionUpBtn->setToolTip("Move up");
    m_moveSectionDownBtn->setIcon(
        style()->standardIcon(QStyle::SP_ArrowDown));
    m_moveSectionDownBtn->setToolTip("Move down");

    for (auto *b : {m_addSectionBtn, m_removeSectionBtn,
                    m_duplicateSectionBtn,
                    m_moveSectionUpBtn, m_moveSectionDownBtn})
        b->setFixedSize(32, 32);

    btnRow->addWidget(m_addSectionBtn);
    btnRow->addWidget(m_removeSectionBtn);
    btnRow->addWidget(m_duplicateSectionBtn);
    btnRow->addWidget(m_moveSectionUpBtn);
    btnRow->addWidget(m_moveSectionDownBtn);
    btnRow->addStretch();

    leftLayout->addWidget(listHint);
    leftLayout->addWidget(m_sectionList, 1);
    leftLayout->addLayout(btnRow);

    // ── RIGHT: editor panel ───────────────────────────────────
    auto *rightPanel  = new QWidget;
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0,0,0,0);
    rightLayout->setSpacing(4);

    // Row 1: Type + Label only
    auto *typeRow = new QHBoxLayout;
    m_sectionTypeCombo = new QComboBox;
    for (auto t : {SectionType::Verse, SectionType::Chorus,
                   SectionType::Bridge, SectionType::PreChorus,
                   SectionType::Intro,  SectionType::Outro,
                   SectionType::Tag,    SectionType::Custom})
        m_sectionTypeCombo->addItem(sectionTypeLabel(t), static_cast<int>(t));
    m_sectionLabelEdit = new QLineEdit;
    m_sectionLabelEdit->setPlaceholderText("e.g. Verse 1");

    typeRow->addWidget(new QLabel("Type:"));
    typeRow->addWidget(m_sectionTypeCombo);
    typeRow->addSpacing(8);
    typeRow->addWidget(new QLabel("Label:"));
    typeRow->addWidget(m_sectionLabelEdit, 1);

    // Row 2: Chinese Lyrics checkbox + Pinyin + Translation checkboxes on same row.
    // Visible at all times; the Pinyin/Translation sub-checkboxes only appear when Chinese is on.
    m_chineseOptionsRow = new QWidget;
    auto *optRow = new QHBoxLayout(m_chineseOptionsRow);
    optRow->setContentsMargins(0,0,0,0);
    m_chineseCheck = new QCheckBox("Chinese Lyrics");
    m_chineseCheck->setToolTip(
        "Check this when the section lyrics are in Chinese.\n"
        "Enables separate Pinyin and English translation blocks.");
    m_pinyinCheck      = new QCheckBox("Show Pinyin");
    m_translationCheck = new QCheckBox("Show English Translation");
    m_pinyinCheck->setToolTip(
        "When enabled, pinyin is shown above the Chinese characters on the OBS slide.");
    m_translationCheck->setToolTip(
        "When enabled, the English translation is shown below the Chinese characters.");
    m_pinyinCheck->setVisible(false);
    m_translationCheck->setVisible(false);
    optRow->addWidget(m_chineseCheck);
    optRow->addSpacing(16);
    optRow->addWidget(m_pinyinCheck);
    optRow->addSpacing(8);
    optRow->addWidget(m_translationCheck);
    optRow->addStretch();

    // Row 3: Content area — 1 or 3 columns depending on Chinese mode
    auto *contentArea   = new QWidget;
    auto *contentLayout = new QHBoxLayout(contentArea);
    contentLayout->setContentsMargins(0,0,0,0);
    contentLayout->setSpacing(6);

    // Block 1: Main lyrics / Chinese text
    auto *col1   = new QWidget;
    auto *col1L  = new QVBoxLayout(col1);
    col1L->setContentsMargins(0,0,0,0);
    m_col1Label = new QLabel("Lyrics:");
    m_col1Label->setStyleSheet("font-size:9px;color:#aaa;");
    m_sectionTextEdit = new QTextEdit;
    m_sectionTextEdit->setPlaceholderText(
        "Enter lyrics for this section...\n\n"
        "Blank line = slide boundary.");
    m_sectionTextEdit->setFont(QFont("Courier New", 10));
    col1L->addWidget(m_col1Label);
    col1L->addWidget(m_sectionTextEdit, 1);

    // Block 2: Pinyin (hidden when not Chinese)
    m_pinyinWidget  = new QWidget;
    auto *col2L     = new QVBoxLayout(m_pinyinWidget);
    col2L->setContentsMargins(0,0,0,0);
    auto *pinyinLbl = new QLabel("Pinyin:");
    pinyinLbl->setStyleSheet("font-size:9px;color:#aaa;");
    auto *pinyinHint = new QLabel("1 pinyin line per Chinese line.\nBlank line = slide boundary.");
    pinyinHint->setStyleSheet("font-size:8px;color:#666;");
    m_pinyinEdit = new QTextEdit;
    m_pinyinEdit->setFont(QFont("Courier New", 10));
    col2L->addWidget(pinyinLbl);
    col2L->addWidget(pinyinHint);
    col2L->addWidget(m_pinyinEdit, 1);
    m_pinyinWidget->setVisible(false);

    // Block 3: English translation (hidden when not Chinese)
    m_englishWidget  = new QWidget;
    auto *col3L      = new QVBoxLayout(m_englishWidget);
    col3L->setContentsMargins(0,0,0,0);
    auto *englishLbl = new QLabel("English Translation:");
    englishLbl->setStyleSheet("font-size:9px;color:#aaa;");
    auto *englishHint = new QLabel("May have more lines than Chinese.\nBlank line = slide boundary.");
    englishHint->setStyleSheet("font-size:8px;color:#666;");
    m_englishEdit = new QTextEdit;
    m_englishEdit->setFont(QFont("Courier New", 10));
    col3L->addWidget(englishLbl);
    col3L->addWidget(englishHint);
    col3L->addWidget(m_englishEdit, 1);
    m_englishWidget->setVisible(false);

    contentLayout->addWidget(col1,           1);
    contentLayout->addWidget(m_pinyinWidget, 1);
    contentLayout->addWidget(m_englishWidget,1);

    rightLayout->addLayout(typeRow);
    rightLayout->addWidget(m_chineseOptionsRow);
    rightLayout->addWidget(contentArea, 1);

    secLayout->addWidget(leftPanel);
    secLayout->addSpacing(8);
    secLayout->addWidget(rightPanel, 1);
    tabs->addTab(secWidget, "Sections");

    // ── Tab 2: Quick Paste ────────────────────────────────────
    auto *qpWidget = new QWidget;
    auto *qpLayout = new QVBoxLayout(qpWidget);
    qpLayout->setContentsMargins(6,6,6,6);
    qpLayout->setSpacing(4);

    // Hint row
    auto *qpHint = new QLabel(
        "Paste lyrics in [Section] format below. "
        "For Chinese songs, check the box and click Generate Pinyin.");
    qpHint->setWordWrap(true);
    qpHint->setStyleSheet("color:#aaa;font-size:9px;");
    qpLayout->addWidget(qpHint);

    // Side-by-side text boxes
    auto *qpBoxRow    = new QHBoxLayout;
    auto *lyricsCol   = new QWidget;
    auto *lyricsColL  = new QVBoxLayout(lyricsCol);
    lyricsColL->setContentsMargins(0,0,0,0);
    auto *lyricsHdr   = new QLabel("Lyrics ([Section] format):");
    lyricsHdr->setStyleSheet("font-size:9px;color:#aaa;");
    m_quickPasteEdit  = new QTextEdit;
    m_quickPasteEdit->setFont(QFont("Courier New", 10));
    lyricsColL->addWidget(lyricsHdr);
    lyricsColL->addWidget(m_quickPasteEdit, 1);

    auto *pinyinCol   = new QWidget;
    auto *pinyinColL  = new QVBoxLayout(pinyinCol);
    pinyinColL->setContentsMargins(0,0,0,0);
    auto *pinyinHdr   = new QLabel("Pinyin (auto-generated / editable):");
    pinyinHdr->setStyleSheet("font-size:9px;color:#aaa;");
    m_qpPinyinEdit    = new QTextEdit;
    m_qpPinyinEdit->setFont(QFont("Courier New", 10));
    m_qpPinyinEdit->setPlaceholderText(
        "Pinyin will appear here after clicking \"Generate Pinyin\".\n"
        "You may edit individual lines before importing.");
    pinyinColL->addWidget(pinyinHdr);
    pinyinColL->addWidget(m_qpPinyinEdit, 1);
    pinyinCol->setVisible(false); // shown only when Chinese checked

    qpBoxRow->addWidget(lyricsCol,  1);
    qpBoxRow->addSpacing(4);
    qpBoxRow->addWidget(pinyinCol,  1);
    qpLayout->addLayout(qpBoxRow, 1);

    // Button row: [✓ Chinese Song] [Generate Pinyin]  ──  [Import]
    auto *qpBtnRow = new QHBoxLayout;
    m_qpChineseCheck  = new QCheckBox("Chinese Song");
    m_qpChineseCheck->setToolTip(
        "Check this when the pasted lyrics are in Chinese.\n"
        "Enables the pinyin column and Generate Pinyin button.");
    m_qpGenPinyinBtn  = new QPushButton("Generate Pinyin");
    m_qpGenPinyinBtn->setIcon(
        style()->standardIcon(QStyle::SP_BrowserReload));
    m_qpGenPinyinBtn->setEnabled(false);
    m_qpGenPinyinBtn->setToolTip(
        "Generate plain ASCII pinyin from the lyrics on the left.\n"
        "Existing pinyin will be cleared and regenerated.");
    m_quickPasteBtn   = new QPushButton("Import");
    m_quickPasteBtn->setIcon(
        style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_quickPasteBtn->setDefault(false);

    qpBtnRow->addWidget(m_qpChineseCheck);
    qpBtnRow->addSpacing(8);
    qpBtnRow->addWidget(m_qpGenPinyinBtn);
    qpBtnRow->addStretch();
    qpBtnRow->addWidget(m_quickPasteBtn);
    qpLayout->addLayout(qpBtnRow);

    tabs->addTab(qpWidget, "Quick Paste");

    // Connect Quick Paste tab signals
    connect(m_qpChineseCheck, &QCheckBox::toggled, this, [pinyinCol, this](bool on){
        pinyinCol->setVisible(on);
        m_qpGenPinyinBtn->setEnabled(on);
    });
    connect(m_qpGenPinyinBtn, &QPushButton::clicked, this, [this](){
        // Extract only the lyric lines (strip [Section] headers and blanks)
        // then run through PinyinEngine, preserving blank-line boundaries.
        QString raw = m_quickPasteEdit->toPlainText();
        // Rebuild a version with section headers replaced by blank lines
        // so the line-count structure is preserved for the pinyin box.
        QStringList inLines  = raw.split('\n');
        QStringList chLines; // lines fed to engine (headers → blank)
        for (const QString &ln : inLines) {
            QString t = ln.trimmed();
            if (t.startsWith('[') && t.endsWith(']'))
                chLines << QString(); // header → blank so pinyin aligns
            else
                chLines << ln;
        }
        QString pinyin = PinyinEngine::generate(chLines.join('\n'));
        m_qpPinyinEdit->setPlainText(pinyin);
    });

    // Custom save buttons
    auto *btnBox     = new QHBoxLayout;
    auto *cancelBtn  = new QPushButton("Cancel");
    cancelBtn->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));
    auto *saveNewBtn = new QPushButton("Save as New Song");
    saveNewBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    auto *saveLibBtn = new QPushButton("Save to Library");
    saveLibBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    saveLibBtn->setDefault(true);
    btnBox->addWidget(cancelBtn);
    btnBox->addStretch();
    btnBox->addWidget(saveNewBtn);
    btnBox->addWidget(saveLibBtn);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(metaGroup);
    mainLayout->addWidget(tabs, 1);
    mainLayout->addLayout(btnBox);

    // Connections
    connect(m_sectionList, &QListWidget::currentRowChanged,
            this, &SongEditDialog::onSectionSelected);
    connect(m_sectionList, &SectionListWidget::orderChanged,
            this, &SongEditDialog::onSectionOrderChanged);
    connect(m_addSectionBtn,       &QPushButton::clicked,
            this, &SongEditDialog::onAddSection);
    connect(m_removeSectionBtn,    &QPushButton::clicked,
            this, &SongEditDialog::onRemoveSection);
    connect(m_duplicateSectionBtn, &QPushButton::clicked,
            this, &SongEditDialog::onDuplicateSection);
    connect(m_moveSectionUpBtn,    &QPushButton::clicked,
            this, &SongEditDialog::onMoveSectionUp);
    connect(m_moveSectionDownBtn,  &QPushButton::clicked,
            this, &SongEditDialog::onMoveSectionDown);
    connect(m_sectionTypeCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SongEditDialog::onSectionTypeChanged);
    connect(m_chineseCheck, &QCheckBox::toggled,
            this, &SongEditDialog::onChineseToggled);
    connect(m_quickPasteBtn, &QPushButton::clicked,
            this, &SongEditDialog::onQuickPasteClicked);
    connect(cancelBtn,  &QPushButton::clicked, this, &QDialog::reject);
    connect(saveNewBtn, &QPushButton::clicked, this, &SongEditDialog::onSaveAsNew);
    connect(saveLibBtn, &QPushButton::clicked, this, &SongEditDialog::onSaveToLibrary);

    // Switch to sections tab if initial section given
    if (initialSectionIndex >= 0)
        tabs->setCurrentIndex(0);
}

// ── Helpers ───────────────────────────────────────────────────

void SongEditDialog::syncQuickPasteFromSections()
{
    // Detect if any section is Chinese
    bool hasChinese = std::any_of(m_song.sections.begin(), m_song.sections.end(),
                                  [](const LyricsSection &s){ return s.isChinese; });

    // Block signals to avoid toggled() firing and hiding/showing the pinyin column
    // before we finish populating both boxes.
    m_qpChineseCheck->blockSignals(true);
    m_qpChineseCheck->setChecked(hasChinese);
    m_qpChineseCheck->blockSignals(false);
    // Manually fire the visibility toggle
    m_qpChineseCheck->toggled(hasChinese); // triggers the lambda connected above

    QString lyricsOut, pinyinOut;
    for (const auto &sec : m_song.sections) {
        QString lbl = sec.label.isEmpty()
                      ? sectionTypeLabel(sec.type) : sec.label;
        lyricsOut += "[" + lbl + "]\n" + sec.text.trimmed() + "\n\n";
        if (hasChinese)
            pinyinOut += sec.pinyin.trimmed() + "\n\n";
    }
    m_quickPasteEdit->setPlainText(lyricsOut.trimmed());
    if (hasChinese)
        m_qpPinyinEdit->setPlainText(pinyinOut.trimmed());
    else
        m_qpPinyinEdit->clear();
}

bool SongEditDialog::checkDuplicateTitle(const QString &title)
{
    for (const auto &song : SongManager::instance().songs()) {
        if (song.id == m_song.id) continue; // same song, skip
        if (song.title.compare(title, Qt::CaseInsensitive) == 0) {
            auto btn = QMessageBox::warning(
                this, "Duplicate Title",
                QString("A song named \"%1\" already exists in the library.\n"
                        "Do you still want to save with this title?").arg(title),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            return (btn == QMessageBox::Yes);
        }
    }
    return true;
}

void SongEditDialog::populateSectionList()
{
    int cur = m_sectionList->currentRow();
    m_sectionList->blockSignals(true);
    m_sectionList->clear();
    for (int i = 0; i < m_song.sections.size(); ++i) {
        const auto &sec = m_song.sections[i];
        QString label = sec.label.isEmpty() ? sectionTypeLabel(sec.type) : sec.label;
        if (sec.isChinese) label = QString::fromUtf8("中 ") + label;
        auto *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, i);
        m_sectionList->addItem(item);
    }
    m_sectionList->setCurrentRow(qBound(0, cur, m_sectionList->count()-1));
    m_sectionList->blockSignals(false);
}

void SongEditDialog::loadSectionIntoEditor(int idx)
{
    if (idx < 0 || idx >= m_song.sections.size()) {
        m_sectionTextEdit->clear();
        m_sectionLabelEdit->clear();
        m_pinyinEdit->clear();
        m_englishEdit->clear();
        m_chineseCheck->blockSignals(true);
        m_chineseCheck->setChecked(false);
        m_chineseCheck->blockSignals(false);
        onChineseToggled(false);
        return;
    }
    const auto &sec = m_song.sections[idx];
    m_sectionTypeCombo->blockSignals(true);
    m_sectionTextEdit->blockSignals(true);
    m_pinyinEdit->blockSignals(true);
    m_englishEdit->blockSignals(true);
    m_chineseCheck->blockSignals(true);

    m_sectionTypeCombo->setCurrentIndex(
        m_sectionTypeCombo->findData(static_cast<int>(sec.type)));
    m_sectionLabelEdit->setText(sec.label);
    m_sectionTextEdit->setPlainText(sec.text);
    m_pinyinEdit->setPlainText(sec.pinyin);
    m_englishEdit->setPlainText(sec.english);

    // Load Chinese flags
    m_chineseCheck->setChecked(sec.isChinese);
    m_pinyinCheck->setChecked(sec.showPinyin);
    m_translationCheck->setChecked(sec.showTranslation);

    m_sectionTypeCombo->blockSignals(false);
    m_sectionTextEdit->blockSignals(false);
    m_pinyinEdit->blockSignals(false);
    m_englishEdit->blockSignals(false);
    m_chineseCheck->blockSignals(false);

    // Show/hide Chinese extra blocks
    onChineseToggled(sec.isChinese);
}

void SongEditDialog::saveCurrentSection()
{
    if (m_currentSection < 0 ||
        m_currentSection >= m_song.sections.size()) return;
    auto &sec = m_song.sections[m_currentSection];
    sec.type            = static_cast<SectionType>(
        m_sectionTypeCombo->currentData().toInt());
    sec.label           = m_sectionLabelEdit->text();
    sec.text            = m_sectionTextEdit->toPlainText();
    sec.isChinese       = m_chineseCheck->isChecked();
    sec.showPinyin      = m_pinyinCheck->isChecked();
    sec.showTranslation = m_translationCheck->isChecked();
    sec.pinyin          = m_pinyinEdit->toPlainText();
    sec.english         = m_englishEdit->toPlainText();

    // Update list item text
    if (auto *item = m_sectionList->item(m_currentSection)) {
        QString label = sec.label.isEmpty()
                        ? sectionTypeLabel(sec.type) : sec.label;
        if (sec.isChinese) label = QString::fromUtf8("中 ") + label;
        item->setText(label);
    }
}

// ── Slots ─────────────────────────────────────────────────────

void SongEditDialog::onSectionSelected(int row)
{
    saveCurrentSection();
    m_currentSection = row;
    loadSectionIntoEditor(row);
}

void SongEditDialog::onChineseToggled(bool checked)
{
    // Pinyin & Translation sub-checkboxes appear only in Chinese mode
    m_pinyinCheck->setVisible(checked);
    m_translationCheck->setVisible(checked);
    // The two extra text blocks only show in Chinese mode
    m_pinyinWidget->setVisible(checked);
    m_englishWidget->setVisible(checked);
    m_col1Label->setText(checked ? "Chinese:" : "Lyrics:");
    m_sectionTextEdit->setPlaceholderText(
        checked
            ? "Enter Chinese characters here...\n\n"
              "Blank line = slide boundary.\n"
              "Same blank line structure as Pinyin block."
            : "Enter lyrics for this section...\n\n"
              "Blank line = slide boundary.");
}

void SongEditDialog::onSectionOrderChanged()
{
    // Save editor content into the DATA for the current section.
    // Do NOT call saveCurrentSection() here — it also updates item text via
    // m_sectionList->item(m_currentSection)->setText(...), but after a drag
    // the item that now occupies row m_currentSection is a DIFFERENT section,
    // so that write corrupts the wrong item's label.
    if (m_currentSection >= 0 && m_currentSection < m_song.sections.size()) {
        auto &sec = m_song.sections[m_currentSection];
        sec.type            = static_cast<SectionType>(
            m_sectionTypeCombo->currentData().toInt());
        sec.label           = m_sectionLabelEdit->text();
        sec.text            = m_sectionTextEdit->toPlainText();
        sec.isChinese       = m_chineseCheck->isChecked();
        sec.showPinyin      = m_pinyinCheck->isChecked();
        sec.showTranslation = m_translationCheck->isChecked();
        sec.pinyin          = m_pinyinEdit->toPlainText();
        sec.english         = m_englishEdit->toPlainText();
    }
    // Rebuild sections array in the new visual order using UserRole indices.
    QList<LyricsSection> reordered;
    for (int i = 0; i < m_sectionList->count(); ++i) {
        int origIdx = m_sectionList->item(i)->data(Qt::UserRole).toInt();
        if (origIdx >= 0 && origIdx < m_song.sections.size())
            reordered << m_song.sections[origIdx];
    }
    m_song.sections = reordered;
    // Refresh UserRole indices AND item labels from the now-correct data array.
    for (int i = 0; i < m_sectionList->count(); ++i) {
        m_sectionList->item(i)->setData(Qt::UserRole, i);
        const auto &sec = m_song.sections[i];
        QString label = sec.label.isEmpty() ? sectionTypeLabel(sec.type) : sec.label;
        if (sec.isChinese) label = QString::fromUtf8("中 ") + label;
        m_sectionList->item(i)->setText(label);
    }
    m_currentSection = m_sectionList->currentRow();
    loadSectionIntoEditor(m_currentSection);
}

void SongEditDialog::onAddSection()
{
    saveCurrentSection();
    LyricsSection s;
    s.type  = SectionType::Verse;
    s.label = "Verse " + QString::number(m_song.sections.size() + 1);
    m_song.sections << s;
    // Must store index as UserRole so drag-reorder can match by index, not label.
    auto *item = new QListWidgetItem(s.label);
    item->setData(Qt::UserRole, m_song.sections.size() - 1);
    m_sectionList->addItem(item);
    m_sectionList->setCurrentRow(m_song.sections.size() - 1);
}

void SongEditDialog::onRemoveSection()
{
    int row = m_sectionList->currentRow();
    if (row < 0) return;
    m_currentSection = -1;
    m_song.sections.removeAt(row);
    populateSectionList();
    m_sectionList->setCurrentRow(qMin(row, m_sectionList->count()-1));
}

void SongEditDialog::onDuplicateSection()
{
    int row = m_sectionList->currentRow();
    if (row < 0 || row >= m_song.sections.size()) return;
    saveCurrentSection();
    LyricsSection copy = m_song.sections[row]; // no "(copy)" suffix
    m_song.sections.insert(row + 1, copy);
    populateSectionList();
    m_sectionList->setCurrentRow(row + 1);
}

void SongEditDialog::onMoveSectionUp()
{
    int row = m_sectionList->currentRow();
    if (row <= 0) return;
    saveCurrentSection();
    m_currentSection = -1; // prevent populateSectionList re-triggering a save
    m_song.sections.swapItemsAt(row, row-1);
    populateSectionList();
    m_sectionList->setCurrentRow(row-1);
}

void SongEditDialog::onMoveSectionDown()
{
    int row = m_sectionList->currentRow();
    if (row < 0 || row >= m_song.sections.size()-1) return;
    saveCurrentSection();
    m_currentSection = -1; // prevent populateSectionList re-triggering a save
    m_song.sections.swapItemsAt(row, row+1);
    populateSectionList();
    m_sectionList->setCurrentRow(row+1);
}

void SongEditDialog::onSectionTypeChanged(int)
{
    if (m_currentSection >= 0 && m_sectionLabelEdit->text().isEmpty()) {
        auto t = static_cast<SectionType>(
            m_sectionTypeCombo->currentData().toInt());
        m_sectionLabelEdit->setText(sectionTypeLabel(t));
    }
}

void SongEditDialog::onQuickPasteClicked()
{
    QString raw = m_quickPasteEdit->toPlainText().trimmed();
    if (raw.isEmpty()) return;
    saveCurrentSection();

    bool isChinese = m_qpChineseCheck->isChecked();
    Song parsed    = SongManager::parseQuickLyrics(m_titleEdit->text(), raw);
    m_song.sections = parsed.sections;

    // If Chinese mode: distribute pinyin lines to each section.
    // The pinyin box uses blank lines as section separators (one group per section,
    // in the same order as the [Section] headers in the lyrics box).
    // This group-based approach is immune to re-import drift: it never depends on
    // a per-header blank placeholder, so pressing Import multiple times is safe.
    if (isChinese && !m_qpPinyinEdit->toPlainText().trimmed().isEmpty()) {
        // Split pinyin box into groups separated by one or more blank lines.
        QStringList pinyinAllLines = m_qpPinyinEdit->toPlainText().split('\n');
        QList<QStringList> pinyinGroups;
        QStringList curGroup;
        for (const QString &line : pinyinAllLines) {
            if (line.trimmed().isEmpty()) {
                if (!curGroup.isEmpty()) {
                    pinyinGroups << curGroup;
                    curGroup.clear();
                }
            } else {
                curGroup << line;
            }
        }
        if (!curGroup.isEmpty())
            pinyinGroups << curGroup;

        // Assign group i → section i pinyin
        for (int i = 0; i < m_song.sections.size(); ++i) {
            m_song.sections[i].isChinese  = true;
            m_song.sections[i].showPinyin = true;
            m_song.sections[i].pinyin     = (i < pinyinGroups.size())
                                            ? pinyinGroups[i].join('\n')
                                            : QString();
        }
    }

    populateSectionList();
    if (!m_song.sections.isEmpty()) m_sectionList->setCurrentRow(0);
    syncQuickPasteFromSections();

    QMessageBox::information(this, "Import Complete",
        QString("%1 section(s) imported successfully.")
            .arg(m_song.sections.size()));
}

void SongEditDialog::onSaveToLibrary()
{
    saveCurrentSection();
    m_song.title  = m_titleEdit->text().trimmed();
    m_song.artist = m_artistEdit->text().trimmed();
    if (m_song.title.isEmpty()) {
        QMessageBox::warning(this, "Missing Title", "Please enter a song title.");
        return;
    }
    m_savedAsNew = false;
    accept();
}

void SongEditDialog::onSaveAsNew()
{
    saveCurrentSection();
    m_song.title  = m_titleEdit->text().trimmed();
    m_song.artist = m_artistEdit->text().trimmed();
    if (m_song.title.isEmpty()) {
        QMessageBox::warning(this, "Missing Title", "Please enter a song title.");
        return;
    }
    bool ok = false;
    QString newTitle = QInputDialog::getText(
        this, "Save as New Song",
        "Enter title for the new song:",
        QLineEdit::Normal, m_song.title, &ok);
    if (!ok || newTitle.trimmed().isEmpty()) return;
    if (!checkDuplicateTitle(newTitle.trimmed())) return;
    m_song.id    = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_song.title = newTitle.trimmed();
    m_savedAsNew = true;
    accept();
}
