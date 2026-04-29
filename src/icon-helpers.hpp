#pragma once
// icon-helpers.hpp — custom drawn icons for the OBS Lyrics Dock UI.
// All functions return QIcon drawn at 32×32 with antialiasing.
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

// ── Edit icon ─────────────────────────────────────────────────────────────────
// Rounded-rectangle notepad (light blue) with a diagonal pencil overlaid:
//   green rounded eraser cap → red barrel → skin-tone triangular nib.
// Thick black outlines throughout, matching common flat-icon style.
inline QIcon makePenIcon()
{
    const int SZ = 64;   // draw at 2× for crispness, downscale later
    QPixmap px(SZ, SZ);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);

    // ── Rounded-rectangle background ────────────────────────────────────────
    const QColor bgFill(223, 229, 243);   // light periwinkle-blue
    const QColor bgBorder(20, 20, 20);
    p.setPen(QPen(bgBorder, 4.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(bgFill);
    p.drawRoundedRect(QRectF(4, 10, 50, 50), 9, 9);

    // ── Pencil (drawn at -45°, nib pointing bottom-left) ────────────────────
    // We translate to pencil centre and rotate.
    p.save();
    p.translate(38, 26);   // anchor near top-right area
    p.rotate(45.0);        // rotate so nib faces bottom-left of canvas

    const qreal hw  = 5.5;   // half-width of barrel
    const QPen  outline(QColor(20, 20, 20), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    // Eraser cap — green, rounded top
    const QColor capFill(62, 188, 128);
    p.setPen(outline);
    p.setBrush(capFill);
    QPainterPath capPath;
    capPath.moveTo(-hw, -22);
    capPath.lineTo( hw, -22);
    capPath.arcTo(QRectF(-hw, -30, hw * 2, 16), 0, 180);
    capPath.closeSubpath();
    p.drawPath(capPath);

    // Ferrule — dark band between cap and barrel
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 30, 30));
    p.drawRect(QRectF(-hw, -14, hw * 2, 3.5));

    // Barrel — red
    const QColor barrelFill(224, 60, 80);
    p.setPen(outline);
    p.setBrush(barrelFill);
    p.drawRect(QRectF(-hw, -10.5, hw * 2, 22));

    // Nib — skin-tone triangle
    const QColor nibFill(248, 203, 160);
    QPolygonF nib;
    nib << QPointF(-hw, 11.5) << QPointF(hw, 11.5) << QPointF(0, 23);
    p.setPen(outline);
    p.setBrush(nibFill);
    p.drawPolygon(nib);

    // Tip dot — dark
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 30, 30));
    p.drawEllipse(QPointF(0, 23), 2.0, 2.0);

    p.restore();
    p.end();

    // Scale down to 32×32 for the actual icon
    return QIcon(px.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
