#pragma once
// song-edit-dialog.hpp  v10.0
// Changes: adds Chinese / Pinyin / Translation trilingual editor per section

#include "song-manager.hpp"
#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QCheckBox>

class SectionListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit SectionListWidget(QWidget *parent = nullptr);
signals:
    void orderChanged();
protected:
    void dropEvent(QDropEvent *e) override;
};

class SongEditDialog : public QDialog {
    Q_OBJECT
public:
    // initialSectionIndex: if >= 0, that section tab is pre-selected
    explicit SongEditDialog(QWidget *parent = nullptr,
                            int initialSectionIndex = -1);
    explicit SongEditDialog(const Song &song, QWidget *parent = nullptr,
                            int initialSectionIndex = -1);

    Song result()      const { return m_song; }
    bool savedAsNew()  const { return m_savedAsNew; }

private slots:
    void onSectionSelected(int row);
    void onAddSection();
    void onRemoveSection();
    void onDuplicateSection();
    void onMoveSectionUp();
    void onMoveSectionDown();
    void onSectionOrderChanged();
    void onSectionTypeChanged(int idx);
    void onChineseToggled(bool checked);
    void onQuickPasteClicked();
    void onSaveToLibrary();
    void onSaveAsNew();

private:
    void buildUi(int initialSectionIndex);
    void populateSectionList();
    void loadSectionIntoEditor(int idx);
    void saveCurrentSection();
    void syncQuickPasteFromSections();
    bool checkDuplicateTitle(const QString &title);

    Song  m_song;
    int   m_currentSection = -1;
    bool  m_savedAsNew     = false;

    QLineEdit          *m_titleEdit, *m_artistEdit;
    SectionListWidget  *m_sectionList;
    QPushButton        *m_addSectionBtn, *m_removeSectionBtn,
                       *m_duplicateSectionBtn,
                       *m_moveSectionUpBtn, *m_moveSectionDownBtn;
    QComboBox          *m_sectionTypeCombo;
    QLineEdit          *m_sectionLabelEdit;

    // ── Lyrics content area ────────────────────────────────────
    // English mode: m_sectionTextEdit only.
    // Chinese mode: all three text edits visible.
    QLabel    *m_col1Label;           // "Lyrics:" or "Chinese:" label
    QTextEdit *m_sectionTextEdit;     // Block 1: English lyrics OR Chinese text
    QCheckBox *m_chineseCheck;        // "Chinese Lyrics" toggle
    QWidget   *m_chineseOptionsRow;   // row with Pinyin + Translation checkboxes
    QCheckBox *m_pinyinCheck;         // "Show Pinyin" (block 2 enabled)
    QCheckBox *m_translationCheck;    // "Show Translation" (block 3 enabled)
    QWidget   *m_pinyinWidget;        // block 2 container (hidden in English mode)
    QTextEdit *m_pinyinEdit;          // Block 2: Pinyin text
    QWidget   *m_englishWidget;       // block 3 container (hidden in English mode)
    QTextEdit *m_englishEdit;         // Block 3: English translation

    // ── Quick Paste tab ────────────────────────────────────────
    QCheckBox          *m_qpChineseCheck  = nullptr; // "Chinese Song" toggle
    QTextEdit          *m_quickPasteEdit  = nullptr; // left: lyrics with [Section] headers
    QTextEdit          *m_qpPinyinEdit    = nullptr; // right: pinyin (visible in Chinese mode)
    QPushButton        *m_qpGenPinyinBtn  = nullptr; // Generate Pinyin button
    QPushButton        *m_quickPasteBtn   = nullptr; // Import button
};
