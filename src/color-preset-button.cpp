// color-preset-button.cpp  v1.0
#include "color-preset-button.hpp"
#include <QColorDialog>

static constexpr int kBtnW    = 68;
static constexpr int kBtnH    = 22;

ColorPresetButton::ColorPresetButton(int presetNum, QWidget *parent)
    : QToolButton(parent), m_presetNum(presetNum)
{
    setFixedSize(kBtnW, kBtnH);
    setText(QString::number(presetNum));
    setToolTip(QString("Color Preset %1\nLeft-click: apply  |  Right-click: edit")
                   .arg(presetNum));
    connect(this, &QToolButton::clicked, this, [this](){
        emit colorsApplied(m_fontColor, m_outlineColor);
    });
    updateAppearance();
}

void ColorPresetButton::setColors(QColor font, QColor outline)
{
    m_fontColor    = font;
    m_outlineColor = outline;
    updateAppearance();
}

void ColorPresetButton::updateAppearance()
{
    int lum = (m_fontColor.red() * 299 + m_fontColor.green() * 587
               + m_fontColor.blue() * 114) / 1000;
    QString fg = lum > 128 ? "#000000" : "#ffffff";
    if (m_active) {
        setStyleSheet(QString(
            "QToolButton{"
            "  background:%1;"
            "  border:2px inset %2;"
            "  border-radius:3px;"
            "  color:%3;"
            "  font-size:12px;font-weight:bold;"
            "}")
            .arg(m_fontColor.darker(140).name())
            .arg(m_outlineColor.name())
            .arg(fg));
    } else {
        setStyleSheet(QString(
            "QToolButton{"
            "  background:%1;"
            "  border:2px solid %2;"
            "  border-radius:3px;"
            "  color:%3;"
            "  font-size:12px;font-weight:bold;"
            "}"
            "QToolButton:hover{"
            "  border:2px solid %4;"
            "  background:%5;"
            "}")
            .arg(m_fontColor.name())
            .arg(m_outlineColor.name())
            .arg(fg)
            .arg(m_outlineColor.lighter(160).name())
            .arg(m_fontColor.lighter(120).name()));
    }
}

void ColorPresetButton::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton) {
        openEditor();
        e->accept();
    } else {
        QToolButton::mousePressEvent(e);
    }
}

void ColorPresetButton::openEditor()
{
    QColor fc = QColorDialog::getColor(
        m_fontColor, this, QString("Preset %1 — Font Color").arg(m_presetNum));
    if (!fc.isValid()) return;
    QColor oc = QColorDialog::getColor(
        m_outlineColor, this, QString("Preset %1 — Outline Color").arg(m_presetNum));
    if (!oc.isValid()) return;
    setColors(fc, oc);
    emit colorsApplied(fc, oc);
}

void ColorPresetButton::setActive(bool active)
{
    m_active = active;
    updateAppearance();
}
