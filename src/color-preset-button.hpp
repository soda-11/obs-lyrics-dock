#pragma once
// color-preset-button.hpp  v1.0
// Shared color-preset button used by both Lyrics and Scripture slide panels.

#include <QToolButton>
#include <QColor>
#include <QMouseEvent>

// Left-click  → apply stored font+outline colors (emits colorsApplied).
// Right-click → open two QColorDialog pickers to re-assign the preset.
class ColorPresetButton : public QToolButton {
    Q_OBJECT
public:
    explicit ColorPresetButton(int presetNum, QWidget *parent = nullptr);
    QColor fontColor()    const { return m_fontColor; }
    QColor outlineColor() const { return m_outlineColor; }
    void   setColors(QColor font, QColor outline);
    void   setActive(bool active);
signals:
    void colorsApplied(QColor fontColor, QColor outlineColor);
protected:
    void mousePressEvent(QMouseEvent *e) override;
private:
    void openEditor();
    void updateAppearance();
    int    m_presetNum;
    QColor m_fontColor    { Qt::white };
    QColor m_outlineColor { Qt::black };
    bool   m_active = false;
};
