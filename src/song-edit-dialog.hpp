#pragma once
// song-edit-dialog.hpp  v5.0
// Changes: duplicate without "(copy)", no key/tempo/copyright,
// duplicate title warning, quick paste shows existing lyrics,
// optional initial section index to open.

#include "song-manager.hpp"
#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>

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
    QTextEdit          *m_sectionTextEdit;
    QTextEdit          *m_quickPasteEdit;
    QPushButton        *m_quickPasteBtn;
};
