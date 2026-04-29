#pragma once
// icon-helpers.hpp — custom drawn icons for the OBS Lyrics Dock UI.
// All functions return QIcon drawn at 32×32 with antialiasing.
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

// ── Pen / Edit icon ───────────────────────────────────────────────────────────
// Diagonal pen pointing bottom-left: cap → body → golden nib → ink tip.
inline QIcon makePenIcon()
{
    const int SZ = 32;
    QPixmap px(SZ, SZ);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);

    // Coordinate system: centre the pen, rotate -45° so nib faces bottom-left.
    p.translate(SZ / 2.0, SZ / 2.0);
    p.rotate(-45.0);

    const int hw = 3;   // half-width of pen barrel

    // ── Eraser / cap (top) ───────────────────
    const QColor capFill(155, 160, 172);
    p.setPen(QPen(capFill.darker(160), 0.8));
    p.setBrush(capFill);
    p.drawRect(-hw, -15, hw * 2, 4);

    // ── Ferrule (metal ring) ─────────────────
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(125, 130, 142));
    p.drawRect(-hw, -11, hw * 2, 2);

    // ── Barrel ──────────────────────────────
    const QColor barrelFill(200, 205, 215);
    p.setPen(QPen(barrelFill.darker(145), 0.8));
    p.setBrush(barrelFill);
    p.drawRect(-hw, -9, hw * 2, 14);

    // ── Nib (golden triangle) ────────────────
    const QColor nibFill(225, 178, 48);
    QPolygonF nib;
    nib << QPointF(-hw, 5) << QPointF(hw, 5) << QPointF(0, 13);
    p.setPen(QPen(nibFill.darker(160), 0.8));
    p.setBrush(nibFill);
    p.drawPolygon(nib);

    // ── Ink dot at very tip ──────────────────
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(35, 35, 35));
    p.drawEllipse(QPointF(0, 13), 1.4, 1.4);

    p.end();
    return QIcon(px);
}

// ── Duplicate / Copy icon ─────────────────────────────────────────────────────
// Two overlapping document pages stacked diagonally (back page darker,
// front page lighter, each with a folded top-right corner).
inline QIcon makeDuplicateIcon()
{
    const int SZ = 32;
    QPixmap px(SZ, SZ);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);

    // Helper: draw one document page at (x,y) with given fill colour.
    auto drawPage = [&](int x, int y, int w, int h, const QColor &fill)
    {
        const int   fold   = 5;
        const QColor border = fill.darker(165);

        // Body (pentagon: top-right corner cut off)
        QPainterPath body;
        body.moveTo(x,           y);
        body.lineTo(x + w - fold, y);
        body.lineTo(x + w,        y + fold);
        body.lineTo(x + w,        y + h);
        body.lineTo(x,            y + h);
        body.closeSubpath();
        p.setPen(QPen(border, 1.0));
        p.setBrush(fill);
        p.drawPath(body);

        // Fold triangle (top-right)
        QPainterPath foldTri;
        foldTri.moveTo(x + w - fold, y);
        foldTri.lineTo(x + w - fold, y + fold);
        foldTri.lineTo(x + w,        y + fold);
        foldTri.closeSubpath();
        p.setPen(QPen(border, 0.8));
        p.setBrush(fill.darker(118));
        p.drawPath(foldTri);

        // Content lines (representing text on the page)
        p.setPen(QPen(border, 1.0));
        int lx0 = x + 3, lx1 = x + w - 3, ly0 = y + fold + 4;
        for (int i = 0; i < 3; ++i)
            p.drawLine(lx0, ly0 + i * 4, lx1, ly0 + i * 4);
    };

    // Back page: darker, peeking out from top-left
    drawPage(2, 1, 17, 21, QColor(135, 140, 155));

    // Front page: lighter, offset diagonally down-right
    drawPage(11, 9, 17, 21, QColor(200, 205, 218));

    p.end();
    return QIcon(px);
}
