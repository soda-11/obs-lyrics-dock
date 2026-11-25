#pragma once
// layout-dialog.hpp — per-tab font/layout settings dialog

#include <QDialog>
#include <QString>
#include <QByteArray>
#include <QJsonObject>
#include <vector>

class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QFontComboBox;
class QPushButton;
class QFormLayout;
struct LayoutSink;   // defined in layout-dialog.cpp

class LayoutDialog : public QDialog {
    Q_OBJECT
public:
    // tabName: "lyrics" | "paragraph" | "scripture"
    explicit LayoutDialog(const QString &tabName, QWidget *parent = nullptr);

    // Segment mode (paragraph only; create on the stack and use exec()).  The dialog edits ONE
    // segment's layout as overrides on top of the global default.  `overrideJson` = that
    // segment's current overrides ("" = none).
    void setSegmentMode(const QString &segmentTitle, const QByteArray &overrideJson);
    // After exec() returned Accepted: JSON of only the settings that differ from the global
    // default ("" = this segment follows the global layout completely).
    QByteArray segmentLayoutJson() const { return m_resultJson; }

private slots:
    void onApply();
    void onResetToGlobal();

private:
    struct BoxRow {
        QFormLayout   *form           = nullptr;
        const char    *prefix         = nullptr;
        QFontComboBox *fontFace       = nullptr;
        QSpinBox      *fontSize       = nullptr;
        QCheckBox     *bold           = nullptr;
        QCheckBox     *italic         = nullptr;
        QComboBox     *hAlign         = nullptr;
        QComboBox     *vAlign         = nullptr;
        // Colour / style
        QPushButton   *colorBtn       = nullptr;
        QSpinBox      *opacity        = nullptr;
        QCheckBox     *outline        = nullptr;
        QSpinBox      *outlineSize    = nullptr;
        QPushButton   *outlineColorBtn= nullptr;
        QCheckBox     *bgCheck        = nullptr;
        QPushButton   *bgColorBtn     = nullptr;
        QSpinBox      *bgOpacity      = nullptr;
    };

    QWidget *buildBoxGroup(const QString &groupLabel, BoxRow &row);
    void loadSettings();
    void writeValues(LayoutSink &k) const;

    QString             m_tab;
    bool                m_segmentMode = false;
    QJsonObject         m_override;      // segment mode: the segment's current overrides
    QByteArray          m_resultJson;    // segment mode: result after Apply
    QPushButton        *m_applyBtn = nullptr;
    QPushButton        *m_resetBtn = nullptr;
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
    QSpinBox *m_titleH          = nullptr;
    QSpinBox *m_hMargin         = nullptr;
    QSpinBox *m_paragraphTextY  = nullptr; // paragraph only: body text top offset
    const char *m_titleHKey     = nullptr;
    const char *m_hMarginKey    = nullptr;
};
