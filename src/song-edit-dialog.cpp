// song-edit-dialog.cpp  v10.0
// Trilingual section editor: Block 1 = Chinese/English lyrics,
// Block 2 = Pinyin (optional), Block 3 = English translation (optional).
#include "song-edit-dialog.hpp"
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
    if (!m_song.sections.isEmpty())
        m_sectionList->setCurrentRow(row);
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
    auto *leftPanel  = new QWidget; leftPanel->setMaximumWidth(230);
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
    m_duplicateSectionBtn->setIcon(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView));
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

    // Row 1: Type + Label + Chinese checkbox
    auto *typeRow = new QHBoxLayout;
    m_sectionTypeCombo = new QComboBox;
    for (auto t : {SectionType::Verse, SectionType::Chorus,
                   SectionType::Bridge, SectionType::PreChorus,
                   SectionType::Intro,  SectionType::Outro,
                   SectionType::Tag,    SectionType::Custom})
        m_sectionTypeCombo->addItem(sectionTypeLabel(t), static_cast<int>(t));
    m_sectionLabelEdit = new QLineEdit;
    m_sectionLabelEdit->setPlaceholderText("e.g. Verse 1");

    m_chineseCheck = new QCheckBox("Chinese Lyrics");
    m_chineseCheck->setToolTip(
        "Check this when the section lyrics are in Chinese.\n"
        "Enables separate Pinyin and English translation blocks.");

    typeRow->addWidget(new QLabel("Type:"));
    typeRow->addWidget(m_sectionTypeCombo);
    typeRow->addSpacing(8);
    typeRow->addWidget(new QLabel("Label:"));
    typeRow->addWidget(m_sectionLabelEdit, 1);
    typeRow->addSpacing(8);
    typeRow->addWidget(m_chineseCheck);

    // Row 2: Chinese options (Pinyin + Translation checkboxes) — hidden when not Chinese
    m_chineseOptionsRow = new QWidget;
    m_chineseOptionsRow->setVisible(false);
    auto *optRow = new QHBoxLayout(m_chineseOptionsRow);
    optRow->setContentsMargins(0,0,0,0);
    m_pinyinCheck      = new QCheckBox("Show Pinyin (Block 2)");
    m_translationCheck = new QCheckBox("Show English Translation (Block 3)");
    m_pinyinCheck->setToolTip(
        "When enabled, pinyin is shown above the Chinese characters on the OBS slide.");
    m_translationCheck->setToolTip(
        "When enabled, the English translation is shown below the Chinese characters.");
    optRow->addWidget(m_pinyinCheck);
    optRow->addSpacing(16);
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
    m_sectionTextEdit->setFontFamily("Courier New");
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
    m_pinyinEdit->setFontFamily("Courier New");
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
    m_englishEdit->setFontFamily("Courier New");
    col3L->addWidget(englishLbl);
    col3L->addWidget(englishHint);
    col3L->addWidget(m_englishEdit, 1);
    m_englishWidget->setVisible(false);

    contentLayout->addWidget(col1,           3);
    contentLayout->addWidget(m_pinyinWidget, 2);
    contentLayout->addWidget(m_englishWidget,3);

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
    auto *qpHint   = new QLabel(
        "Current song in [Section] format — edit here and click Import, "
        "or paste new lyrics:");
    qpHint->setWordWrap(true);
    qpHint->setStyleSheet("color:#aaa;font-size:9px;");
    m_quickPasteEdit = new QTextEdit;
    m_quickPasteEdit->setFontFamily("Courier New");
    m_quickPasteBtn  = new QPushButton("Import Lyrics from Above");
    m_quickPasteBtn->setIcon(
        style()->standardIcon(QStyle::SP_DialogApplyButton));
    qpLayout->addWidget(qpHint);
    qpLayout->addWidget(m_quickPasteEdit);
    qpLayout->addWidget(m_quickPasteBtn);
    tabs->addTab(qpWidget, "Quick Paste");

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
    // Build [Label]\ntext representation of current sections (English text only)
    QString out;
    for (const auto &sec : m_song.sections) {
        QString lbl = sec.label.isEmpty()
                      ? sectionTypeLabel(sec.type) : sec.label;
        out += "[" + lbl + "]\n" + sec.text.trimmed() + "\n\n";
    }
    m_quickPasteEdit->setPlainText(out.trimmed());
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
    m_chineseOptionsRow->setVisible(checked);
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
    Song parsed = SongManager::parseQuickLyrics(m_titleEdit->text(), raw);
    m_song.sections = parsed.sections;
    populateSectionList();
    if (!m_song.sections.isEmpty()) m_sectionList->setCurrentRow(0);
    syncQuickPasteFromSections();
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
