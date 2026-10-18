#pragma once
// layout-dialog.hpp — per-tab font/layout settings dialog

#include <QDialog>
#include <QString>
#include <vector>

class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QFontComboBox;

class LayoutDialog : public QDialog {
    Q_OBJECT
public:
    // tabName: "lyrics" | "ctw" | "scripture"
    explicit LayoutDialog(const QString &tabName, QWidget *parent = nullptr);

private slots:
    void onApply();

private:
    struct BoxRow {
        const char    *prefix    = nullptr;
        QFontComboBox *fontFace  = nullptr;
        QSpinBox      *fontSize  = nullptr;
        QCheckBox     *bold      = nullptr;
        QCheckBox     *italic    = nullptr;
        QComboBox     *hAlign    = nullptr;
        QComboBox     *vAlign    = nullptr;
    };

    QWidget *buildBoxGroup(const QString &groupLabel, BoxRow &row);
    void loadSettings();

    QString             m_tab;
    std::vector<BoxRow> m_rows;

    // Lyrics layout
    QSpinBox *m_bottomBar    = nullptr;
    // Chinese interleave layout (lyrics only)
    QSpinBox *m_chPinyinY    = nullptr;
    QDoubleSpinBox *m_chPinyinSpacing = nullptr;
    QSpinBox *m_chChineseY   = nullptr;
    QSpinBox *m_chChineseStripH = nullptr;
    QSpinBox *m_chChineseInterval = nullptr;
    QSpinBox *m_chEnglishY   = nullptr;
    // CTW / Scripture layout
    QSpinBox *m_titleH       = nullptr;
    QSpinBox *m_hMargin      = nullptr;
    const char *m_titleHKey  = nullptr;
    const char *m_hMarginKey = nullptr;
};
