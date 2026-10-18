// media-gallery.cpp  v9.5.1
//
// Changes from v9.2:
//   - Tile size reduced ~20% (CARD_W 140→112, CARD_H 110→88)
//   - Real video thumbnails via Windows Shell IShellItemImageFactory
//   - Fade duration spinbox at bottom (0-3000ms, step 50)
//   - lyrics_renderer_media_push now receives fadeMs

#include "media-gallery.hpp"
#include "lyrics-renderer.hpp"
#include "websocket-server.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QStyle>
#include <QMenu>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEvent>
#include <QResizeEvent>
#include <QFontMetrics>

// Windows Shell for video thumbnails
#include <windows.h>
#include <shobjidl.h>
#include <gdiplus.h>
#pragma comment(lib,"gdiplus.lib")

static const QString ALL_FILTER =
    "Media Files (*.jpg *.jpeg *.png *.gif *.bmp *.webp "
    "*.mp4 *.mov *.avi *.mkv *.webm *.flv *.wmv *.m4v);;"
    "Images (*.jpg *.jpeg *.png *.gif *.bmp *.webp);;"
    "Videos (*.mp4 *.mov *.avi *.mkv *.webm *.flv *.wmv *.m4v)";

static const QSet<QString> IMAGE_EXTS = {
    "jpg","jpeg","png","gif","bmp","webp","tga","tiff"
};
static bool isVideoFile(const QString &path)
{
    return !IMAGE_EXTS.contains(QFileInfo(path).suffix().toLower());
}

// ── HBITMAP → QPixmap (Qt6 compatible, no QtWin needed) ──────
static QPixmap hbitmapToPixmap(HBITMAP hBmp, int targetW, int targetH)
{
    if (!hBmp) return {};

    BITMAP bm = {};
    if (!GetObject(hBmp, sizeof(bm), &bm)) return {};

    // Create a DC and select the bitmap
    HDC     hdc    = CreateCompatibleDC(nullptr);
    HBITMAP hOld   = (HBITMAP)SelectObject(hdc, hBmp);

    // Build a BITMAPINFO for 32-bit BGRA
    BITMAPINFOHEADER bih = {};
    bih.biSize        = sizeof(bih);
    bih.biWidth       = bm.bmWidth;
    bih.biHeight      = -bm.bmHeight; // top-down
    bih.biPlanes      = 1;
    bih.biBitCount    = 32;
    bih.biCompression = BI_RGB;

    QImage img(bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32);
    GetDIBits(hdc, hBmp, 0, (UINT)bm.bmHeight,
              img.bits(),
              reinterpret_cast<BITMAPINFO*>(&bih),
              DIB_RGB_COLORS);

    SelectObject(hdc, hOld);
    DeleteDC(hdc);

    // Swap R and B channels (Windows stores BGRA, QImage ARGB32 is BGRA on LE)
    // Actually GetDIBits with BI_RGB returns BGR — we need to swap
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<uint32_t*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            uint32_t p = line[x];
            uint8_t r = (p >> 16) & 0xFF;
            uint8_t g = (p >>  8) & 0xFF;
            uint8_t b = (p      ) & 0xFF;
            uint8_t a = (p >> 24) & 0xFF;
            // Shell thumbnails are opaque — force full alpha
            line[x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }

    QPixmap px = QPixmap::fromImage(img);
    if (!px.isNull() && (px.width()!=targetW || px.height()!=targetH))
        px = px.scaled(targetW, targetH,
                       Qt::KeepAspectRatio,
                       Qt::SmoothTransformation);
    return px;
}

// ── Get thumbnail from Windows Shell ─────────────────────────
static QPixmap shellThumbnail(const QString &path, int w, int h)
{
    // Convert to native Windows path (backslashes)
    QString nativePath = QDir::toNativeSeparators(path);
    std::wstring wpath = nativePath.toStdWString();

    IShellItemImageFactory *pFactory = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(
        wpath.c_str(), nullptr,
        IID_PPV_ARGS(&pFactory));
    if (FAILED(hr) || !pFactory) return {};

    SIZE sz = { w, h };
    HBITMAP hBmp = nullptr;
    hr = pFactory->GetImage(sz, SIIGBF_RESIZETOFIT, &hBmp);
    pFactory->Release();

    if (FAILED(hr) || !hBmp) return {};

    QPixmap result = hbitmapToPixmap(hBmp, w, h);
    DeleteObject(hBmp);
    return result;
}

// ── Public static thumbnail generator (shared by slide cards etc.) ──
QPixmap MediaGallery::generateThumbnail(const QString &path, int w, int h, bool crop)
{
    auto finalScale = [&](QPixmap px) -> QPixmap {
        if (px.isNull()) return {};
        if (crop)
            return px.scaled(w, h, Qt::KeepAspectRatioByExpanding,
                             Qt::SmoothTransformation).copy(0, 0, w, h);
        return px.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    };

    if (!isVideoFile(path))
        return finalScale(QPixmap(path));

    // Video: try Windows Shell for a real frame
    return finalScale(shellThumbnail(path, w, h));
}

// ══════════════════════════════════════════════════════════════
// MediaCard
// ══════════════════════════════════════════════════════════════

MediaCard::MediaCard(const MediaItem &item, int index, QWidget *parent)
    : QFrame(parent), m_item(item), m_index(index)
{
    setFixedSize(CARD_W, CARD_H);
    setFrameShape(QFrame::NoFrame);
    setAutoFillBackground(false);
    setAcceptDrops(true);
    setCursor(Qt::PointingHandCursor);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 2);
    layout->setSpacing(2);

    m_thumb = new QLabel;
    m_thumb->setFixedSize(THUMB_W, THUMB_H);
    m_thumb->setAlignment(Qt::AlignCenter);
    // Prevent parent card stylesheet from bleeding into the label
    // and painting over the pixmap
    m_thumb->setAutoFillBackground(false);
    m_thumb->setAttribute(Qt::WA_NoSystemBackground);
    m_thumb->setStyleSheet("border: none; background: transparent;");

    m_nameLbl = new QLabel;
    m_nameLbl->setAlignment(Qt::AlignCenter);
    QString name = QFileInfo(item.path).fileName();
    QFontMetrics fm(m_nameLbl->font());
    m_nameLbl->setText(
        fm.elidedText(name, Qt::ElideMiddle, CARD_W - 8));
    m_nameLbl->setToolTip(item.path);

    layout->addWidget(m_thumb);
    layout->addWidget(m_nameLbl);

    buildThumbnail();

    // Badge overlay — raised above the thumbnail so it is never obscured.
    m_badgeLbl = new QLabel(this);
    m_badgeLbl->setFixedSize(18, 18);
    m_badgeLbl->setAlignment(Qt::AlignCenter);
    m_badgeLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_badgeLbl->setStyleSheet(
        "background:#e8a020;color:white;border-radius:9px;"
        "font-size:9px;font-weight:bold;border:none;");
    m_badgeLbl->hide();
    // Position: top-right corner of the card, just inside the thumb area.
    m_badgeLbl->move(CARD_W - 18 - 2, 3);
    m_badgeLbl->raise();

    setActive(false);
}

void MediaCard::setCardWidth(int w)
{
    if (width() == w) return;
    setFixedWidth(w);
    int thumbW = qMax(16, w - 8);
    m_thumb->setFixedSize(thumbW, THUMB_H);
    if (!m_origThumb.isNull())
        m_thumb->setPixmap(m_origThumb.scaled(thumbW, THUMB_H,
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
    QFontMetrics fm(m_nameLbl->font());
    m_nameLbl->setText(fm.elidedText(
        QFileInfo(m_item.path).fileName(), Qt::ElideMiddle, thumbW));
    m_badgeLbl->move(w - 18 - 2, 3);
}

void MediaCard::buildThumbnail()
{
    auto storeAndShow = [this](const QPixmap &px){
        m_origThumb = px;
        m_thumb->setPixmap(px);
    };

    if (!m_item.isVideo) {
        // Image: load and scale directly
        QPixmap px(m_item.path);
        if (!px.isNull()) {
            storeAndShow(px.scaled(THUMB_W, THUMB_H,
                Qt::KeepAspectRatio, Qt::SmoothTransformation));
            return;
        }
    } else {
        // Video: ask Windows Shell for a real thumbnail frame
        QPixmap shellPx = shellThumbnail(m_item.path, THUMB_W, THUMB_H);
        if (!shellPx.isNull()) {
            storeAndShow(shellPx);
            return;
        }
    }

    // Fallback placeholder for failed images or unsupported video formats
    QPixmap px(THUMB_W, THUMB_H);
    px.fill(QColor("#0d1117"));
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_item.isVideo) {
        // Film perforations top
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#2d333b"));
        for (int i = 0; i < 5; ++i)
            p.drawRect(3 + i*20, 2, 12, 7);
        // Film perforations bottom
        for (int i = 0; i < 5; ++i)
            p.drawRect(3 + i*20, THUMB_H-9, 12, 7);
        // Play circle
        int cx=THUMB_W/2, cy=THUMB_H/2;
        p.setBrush(QColor("#1e6bb8"));
        p.setPen(QPen(QColor("#4da6ff"),1.5));
        p.drawEllipse(QPoint(cx,cy),16,16);
        // Play triangle
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        QPolygon tri;
        tri << QPoint(cx-5,cy-8) << QPoint(cx+10,cy) << QPoint(cx-5,cy+8);
        p.drawPolygon(tri);
    } else {
        p.setPen(QColor("#555"));
        p.drawText(px.rect(), Qt::AlignCenter, "?");
    }
    p.end();
    storeAndShow(px);
}

void MediaCard::setActive(bool active)
{
    m_active = active;
    m_nameLbl->setStyleSheet(active
        ? "color:#fff;font-size:8px;background:transparent;"
        : "color:#aaa;font-size:8px;background:transparent;");
    update(); // repaints via paintEvent — no stylesheet, no layout change
}

void MediaCard::mousePressEvent(QMouseEvent *e)
{
    if(e->button()==Qt::LeftButton){
        m_dragStartPos=e->pos(); m_dragging=false;
    }
    QFrame::mousePressEvent(e);
}

void MediaCard::mouseReleaseEvent(QMouseEvent *e)
{
    if(e->button()==Qt::LeftButton && !m_dragging)
        emit clicked(m_index);
    m_dragging=false;
    QFrame::mouseReleaseEvent(e);
}

void MediaCard::mouseMoveEvent(QMouseEvent *e)
{
    if(!(e->buttons()&Qt::LeftButton)) return;
    if((e->pos()-m_dragStartPos).manhattanLength()<10) return;
    m_dragging=true;
    auto *drag=new QDrag(this);
    auto *mime=new QMimeData;
    mime->setText(QString::number(m_index));
    drag->setMimeData(mime);
    QPixmap px=grab().scaled(CARD_W/2,CARD_H/2,
                              Qt::KeepAspectRatio,
                              Qt::SmoothTransformation);
    drag->setPixmap(px);
    drag->setHotSpot(QPoint(px.width()/2,px.height()/2));
    drag->exec(Qt::MoveAction);
}

void MediaCard::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu menu(this);
    auto *act=menu.addAction(
        style()->standardIcon(QStyle::SP_TrashIcon),
        "Remove from Gallery");
    if(menu.exec(e->globalPos())==act)
        emit removeRequested(m_index);
}

void MediaCard::setPositionBadge(int badge)
{
    m_badge = badge;
    if (!m_badgeLbl) return;
    if (badge > 0) {
        m_badgeLbl->setText(QString::number(badge));
        m_badgeLbl->show();
        m_badgeLbl->raise(); // keep above thumbnail after any repaint
    } else {
        m_badgeLbl->hide();
    }
}

void MediaCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    if (m_active) {
        p.setBrush(QColor("#1e6bb8"));
        p.setPen(QPen(QColor("#4da6ff"), 1));
    } else if (m_hovered) {
        p.setBrush(QColor("#333"));
        p.setPen(QPen(QColor("#666"), 1));
    } else {
        p.setBrush(QColor("#2a2a2a"));
        p.setPen(QPen(QColor("#444"), 1));
    }
    p.drawRoundedRect(r, 4, 4);
}

void MediaCard::enterEvent(QEnterEvent *e)
{
    m_hovered = true;
    update();
    QFrame::enterEvent(e);
}

void MediaCard::leaveEvent(QEvent *e)
{
    m_hovered = false;
    update();
    QFrame::leaveEvent(e);
}

void MediaCard::dragEnterEvent(QDragEnterEvent *e)
{ if(e->mimeData()->hasText()) e->acceptProposedAction(); }

void MediaCard::dragMoveEvent(QDragMoveEvent *e)
{ if(e->mimeData()->hasText()) e->acceptProposedAction(); }

void MediaCard::dropEvent(QDropEvent *e)
{
    bool ok=false;
    int srcIdx=e->mimeData()->text().toInt(&ok);
    if(!ok||srcIdx==m_index){ e->ignore(); return; }
    QWidget *p=parentWidget();
    while(p){
        if(auto *g=qobject_cast<MediaGallery*>(p)){
            g->reorderCards(srcIdx,m_index); break;
        }
        p=p->parentWidget();
    }
    e->acceptProposedAction();
}

// ══════════════════════════════════════════════════════════════
// MediaGallery
// ══════════════════════════════════════════════════════════════


// broadcastGalleryList is intentionally a no-op here.
// LyricsDock owns the combined Lyrics+Scripture media list broadcast
// via MediaGallery::mediaListChanged signals connected in buildUi().
static void broadcastGalleryList(const QList<MediaItem> &) {}

MediaGallery::MediaGallery(QWidget *parent, const QString &stateFileName)
    : QWidget(parent), m_stateFileName(stateFileName)
{
    // Initialise COM for Shell API (needed for IShellItemImageFactory)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    buildUi();
    loadState();
}

void MediaGallery::buildUi()
{
    auto *root=new QVBoxLayout(this);
    root->setContentsMargins(0,0,0,0);
    root->setSpacing(0);

    // ── Top bar ───────────────────────────────────────────────
    auto *topBar=new QWidget;
    topBar->setStyleSheet("background:#222;border-bottom:1px solid #333;");
    auto *topL=new QHBoxLayout(topBar);
    topL->setContentsMargins(6,4,6,4);
    auto *addBtn=new QPushButton("+ Add Media");
    addBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    m_upBtn=new QToolButton;
    m_upBtn->setText(QString::fromUtf8("\u25B2"));
    m_upBtn->setFixedSize(24,22);
    m_upBtn->setToolTip("Move selected media up");
    m_upBtn->setEnabled(false);
    m_downBtn=new QToolButton;
    m_downBtn->setText(QString::fromUtf8("\u25BC"));
    m_downBtn->setFixedSize(24,22);
    m_downBtn->setToolTip("Move selected media down");
    m_downBtn->setEnabled(false);
    m_clearBtn=new QPushButton(QString::fromUtf8("\u25A0 Stop"));
    m_clearBtn->setFixedSize(68, 22);
    m_clearBtn->setStyleSheet(
        "QPushButton{background:#4a1a1a;color:#ff6b6b;"
        "border:1px solid #8b2020;border-radius:3px;padding:0 2px;}"
        "QPushButton:hover{background:#6a2020;}");
    topL->addWidget(addBtn);
    topL->addWidget(m_upBtn);
    topL->addWidget(m_downBtn);
    topL->addStretch();
    topL->addWidget(m_clearBtn);
    root->addWidget(topBar);

    // ── Thumbnail grid ────────────────────────────────────────
    m_scrollArea=new QScrollArea;
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_scrollArea->setStyleSheet(
        "QScrollArea{background:#181818;border:none;}");
    m_container=new QWidget;
    m_container->setStyleSheet("background:#181818;");
    m_container->setAcceptDrops(true);
    m_container->installEventFilter(this);
    m_grid=new QGridLayout(m_container);
    m_grid->setContentsMargins(6,6,6,6);
    m_grid->setSpacing(6);
    m_grid->setAlignment(Qt::AlignTop|Qt::AlignLeft);
    m_scrollArea->setWidget(m_container);
    root->addWidget(m_scrollArea,1);

    // ── Video controls ────────────────────────────────────────
    m_videoControls=new QWidget;
    m_videoControls->setStyleSheet(
        "background:#222;border-top:1px solid #333;");
    auto *vcL=new QVBoxLayout(m_videoControls);
    vcL->setContentsMargins(6,4,6,4);
    vcL->setSpacing(4);
    auto *scrubRow=new QHBoxLayout;
    m_playPauseBtn=new QToolButton;
    m_playPauseBtn->setText(QString::fromUtf8("\u25B6"));
    m_playPauseBtn->setFixedSize(26,22);
    m_scrubber=new QSlider(Qt::Horizontal);
    m_scrubber->setRange(0,1000);
    m_scrubber->setStyleSheet(
        "QSlider::groove:horizontal{background:#333;height:4px;border-radius:2px;}"
        "QSlider::sub-page:horizontal{background:#4da6ff;border-radius:2px;}"
        "QSlider::handle:horizontal{background:#4da6ff;width:12px;height:12px;"
        "margin:-4px 0;border-radius:6px;}"
        "QSlider::handle:horizontal:hover{background:#7dc4ff;}");
    m_timeLbl=new QLabel("0:00 / 0:00");
    m_timeLbl->setStyleSheet("color:#888;font-size:9px;");
    m_timeLbl->setFixedWidth(76);
    scrubRow->addWidget(m_playPauseBtn);
    scrubRow->addWidget(m_scrubber,1);
    scrubRow->addWidget(m_timeLbl);
    vcL->addLayout(scrubRow);
    m_videoControls->setVisible(false);
    m_videoControls->hide(); // seek bar not needed in media bin
    root->addWidget(m_videoControls);

    // ── Poll timer ────────────────────────────────────────────
    m_pollTimer=new QTimer(this);
    m_pollTimer->setInterval(300);
    connect(m_pollTimer,&QTimer::timeout,
            this,&MediaGallery::onPollTimer);
    m_pollTimer->start();

    connect(addBtn,      &QPushButton::clicked,
            this,&MediaGallery::onAddMedia);
    connect(m_clearBtn,  &QPushButton::clicked,
            this,&MediaGallery::onClearMedia);
    connect(m_playPauseBtn,&QToolButton::clicked,
            this,&MediaGallery::onPlayPause);
    connect(m_upBtn,   &QToolButton::clicked,
            this,&MediaGallery::onMoveUp);
    connect(m_downBtn, &QToolButton::clicked,
            this,&MediaGallery::onMoveDown);
    // OBS-style seek: seek immediately on user drag; timer respects isSliderDown()
    connect(m_scrubber,&QSlider::sliderMoved,this,[this](int v){
        int64_t t=0,d=0;
        if(!lyrics_renderer_media_get_time(t,d)||d<=0) return;
        int64_t seekMs=(int64_t)((double)v/1000.0*(double)d);
        lyrics_renderer_media_set_time(seekMs);
        m_seekCooldown=5;
        auto fmt=[](int64_t ms)->QString{
            int s=(int)(ms/1000);
            return QString("%1:%2").arg(s/60).arg(s%60,2,10,QChar('0'));
        };
        m_timeLbl->setText(fmt(seekMs)+" / "+fmt(d));
    });
}

int MediaGallery::fadeDurationMs() const
{
    return m_fadeDurationMs;
}

void MediaGallery::setFadeDurationMs(int ms)
{
    m_fadeDurationMs = ms;
}

int MediaGallery::calcCols() const
{
    return 2; // fixed 2-column layout; scroll vertically
}

bool MediaGallery::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_container && event->type() == QEvent::Resize)
        relayoutCards(); // update widths in-place, no rebuild
    return QWidget::eventFilter(obj, event);
}

void MediaGallery::rebuildGrid()
{
    m_currentCols = calcCols(); // always 2
    // Use scroll area total width (minus scrollbar reserve) to keep card
    // width stable regardless of whether the vertical scrollbar is visible.
    int saW = m_scrollArea->width();
    if (saW < 40) saW = 200; // fallback during construction
    int cardW = qMax(80, (saW - 20 - 12 - 6) / 2);
    if (!m_cards.isEmpty() && m_cards.size() == (int)m_items.size() && abs(m_cards.first()->width() - cardW) <= 1) return;
    for(auto *c:m_cards){ m_grid->removeWidget(c); c->deleteLater(); }
    m_cards.clear();
    int col=0,row=0;
    for(int i=0;i<m_items.size();++i){
        auto *card=new MediaCard(m_items[i],i,m_container);
        card->setCardWidth(cardW);
        connect(card,&MediaCard::clicked,
                this,&MediaGallery::onCardClicked);
        connect(card,&MediaCard::removeRequested,
                this,&MediaGallery::onCardRemoveRequested);
        card->setPositionBadge(i < 6 ? i + 1 : 0);
        card->setActive(i==m_activeIndex);
        m_grid->addWidget(card,row,col);
        m_cards<<card;
        if(++col>=m_currentCols){ col=0; ++row; }
    }
}

void MediaGallery::relayoutCards()
{
    if (m_relayouting || m_cards.isEmpty()) return;
    // Use scroll area total width minus scrollbar reserve so the computed
    // card width stays constant regardless of whether the scrollbar is shown.
    int saW = m_scrollArea->width();
    if (saW < 40) return;
    int usable  = saW - 20; // 20 = scrollbar width + small buffer
    int cardW   = qMax(80, (usable - 12 - 6) / 2); // 12=margins, 6=gap between 2 cols
    if (abs(m_cards.first()->width() - cardW) <= 1) return; // 1px dead-zone
    m_relayouting = true;
    for (auto *c : m_cards)
        c->setCardWidth(cardW);
    m_relayouting = false;
}

void MediaGallery::setActiveCard(int index)
{
    m_activeIndex=index;
    for(auto *c:m_cards) c->setActive(c->index()==index);
    updateVideoControls();
    updateUpDownButtons();
}

void MediaGallery::updateVideoControls()
{
    // Seek bar hidden in media bin \u2014 play/pause state is tracked internally only.
    bool showVid=(m_activeIndex>=0 &&
                  m_activeIndex<m_items.size() &&
                  m_items[m_activeIndex].isVideo);
    if(showVid){
        m_paused=false;
        m_playPauseBtn->setText(QString::fromUtf8("\u23F8"));
    }
    m_videoControls->setVisible(false);
}

void MediaGallery::onAddMedia()
{
    QStringList files=QFileDialog::getOpenFileNames(
        this,"Add Media to Gallery",QString(),ALL_FILTER);
    if(files.isEmpty()) return;
    for(const auto &f:files){
        MediaItem item; item.path=f; item.isVideo=isVideoFile(f);
        m_items<<item;
    }
    rebuildGrid();
    saveState();
    broadcastGalleryList(m_items);
    emit mediaListChanged();
}

void MediaGallery::onClearMedia()
{
    m_lastActiveIndex = -1;
    setActiveCard(-1);
    lyrics_renderer_media_clear();
    auto *srv = lyricsWsServer();
    if (srv) srv->broadcastMediaState(QString());
    emit stopAllRequested(); // let LyricsDock stop MV tab UI
}

void MediaGallery::clearActiveCard()
{
    setActiveCard(-1); // UI only — no renderer call
}

void MediaGallery::onCardClicked(int index)
{
    if(index<0||index>=m_items.size()) return;
    m_lastActiveIndex = index;
    setActiveCard(index);
    QString path = m_items[index].path;
    if (m_items[index].isVideo) emit videoActivated(); // stop AudioBin before video starts
    lyrics_renderer_media_push(path, fadeDurationMs());
    auto *srv = lyricsWsServer();
    if (srv) srv->broadcastMediaState(path);
    emit mediaActivated(); // let LyricsDock stop MV tab
}

void MediaGallery::suspendPlayback()
{
    // Clears the renderer but keeps m_lastActiveIndex so replayActive() can restore.
    lyrics_renderer_media_clear();
    auto *srv = lyricsWsServer();
    if (srv) srv->broadcastMediaState(QString());
}

void MediaGallery::stopPlayback()
{
    m_lastActiveIndex = -1;
    setActiveCard(-1);
    lyrics_renderer_media_clear();
    auto *srv = lyricsWsServer();
    if (srv) srv->broadcastMediaState(QString());
}

void MediaGallery::onCardRemoveRequested(int index)
{
    if(index<0||index>=m_items.size()) return;
    m_items.removeAt(index);
    if(m_activeIndex==index){
        m_activeIndex=-1;
        lyrics_renderer_media_clear();
        auto *srv=lyricsWsServer();
        if(srv) srv->broadcastMediaState(QString());
    } else if(m_activeIndex>index){
        m_activeIndex--;
    }
    if(m_lastActiveIndex==index)       m_lastActiveIndex=-1;
    else if(m_lastActiveIndex>index)   m_lastActiveIndex--;
    rebuildGrid();
    saveState();
    broadcastGalleryList(m_items);
    emit mediaListChanged();
}

void MediaGallery::reorderCards(int from,int to)
{
    if(from<0||from>=m_items.size()||
       to  <0||to  >=m_items.size()||from==to) return;
    m_items.move(from,to);
    if     (m_activeIndex==from)                     m_activeIndex=to;
    else if(from<to&&m_activeIndex>from&&
                     m_activeIndex<=to)              m_activeIndex--;
    else if(from>to&&m_activeIndex>=to&&
                     m_activeIndex<from)             m_activeIndex++;
    if     (m_lastActiveIndex==from)                 m_lastActiveIndex=to;
    else if(from<to&&m_lastActiveIndex>from&&
                     m_lastActiveIndex<=to)          m_lastActiveIndex--;
    else if(from>to&&m_lastActiveIndex>=to&&
                     m_lastActiveIndex<from)         m_lastActiveIndex++;
    rebuildGrid();
    saveState();
}

void MediaGallery::onPlayPause()
{
    m_paused=!m_paused;
    lyrics_renderer_media_play_pause(m_paused);
    m_playPauseBtn->setText(
        m_paused
        ? QString::fromUtf8("\u25B6")
        : QString::fromUtf8("\u23F8"));
}

// (seek bar now uses sliderMoved lambda in buildUi)

void MediaGallery::onPollTimer()
{
    if(!m_videoControls->isVisible()) return;
    // isSliderDown() is Qt's native drag-detection (OBS approach)
    if(!m_scrubber->isSliderDown()){
        int64_t t=0,d=0;
        if(lyrics_renderer_media_get_time(t,d)&&d>0){
            m_scrubber->setValue((int)((double)t/(double)d*1000.0));
            auto fmt=[](int64_t ms)->QString{
                int s=(int)(ms/1000);
                return QString("%1:%2").arg(s/60).arg(s%60,2,10,QChar('0'));
            };
            m_timeLbl->setText(fmt(t)+" / "+fmt(d));
        }
    }
    if(m_seekCooldown>0) --m_seekCooldown;
}

QStringList MediaGallery::itemPaths() const
{
    QStringList out;
    for (const auto &item : m_items) out << item.path;
    return out;
}

QString MediaGallery::activeItemPath() const
{
    // Prefer the currently highlighted card; fall back to the last one played.
    // This allows right-click "Apply background" to work even after the gallery
    // card was visually deselected by a slide push (clearActiveCard).
    int idx = (m_activeIndex >= 0) ? m_activeIndex : m_lastActiveIndex;
    if (idx >= 0 && idx < m_items.size())
        return m_items[idx].path;
    return {};
}

void MediaGallery::updateUpDownButtons()
{
    bool sel = (m_activeIndex >= 0);
    if (m_upBtn)   m_upBtn->setEnabled(sel && m_activeIndex > 0);
    if (m_downBtn) m_downBtn->setEnabled(sel && m_activeIndex < m_items.size() - 1);
}

void MediaGallery::onMoveUp()
{
    if (m_activeIndex <= 0) return;
    reorderCards(m_activeIndex, m_activeIndex - 1);
    updateUpDownButtons();
}

void MediaGallery::onMoveDown()
{
    if (m_activeIndex < 0 || m_activeIndex >= m_items.size() - 1) return;
    reorderCards(m_activeIndex, m_activeIndex + 1);
    updateUpDownButtons();
}

void MediaGallery::activateByPosition(int oneBased)
{
    int idx = oneBased - 1;
    if (idx < 0 || idx >= m_items.size()) return;
    onCardClicked(idx);
}

void MediaGallery::replayActive()
{
    if (m_lastActiveIndex < 0 || m_lastActiveIndex >= m_items.size()) return;
    lyrics_renderer_media_push(m_items[m_lastActiveIndex].path, fadeDurationMs());
}

QString MediaGallery::galleryStatePath() const
{
    QString dir=QStandardPaths::writableLocation(
                    QStandardPaths::AppDataLocation)
                +"/obs-lyrics-dock";
    QDir().mkpath(dir);
    return dir + "/" + m_stateFileName;
}

void MediaGallery::saveState()
{
    QJsonArray arr;
    for(const auto &item:m_items) arr.append(item.path);
    QJsonObject obj;
    obj["items"]    = arr;
    QFile f(galleryStatePath());
    if(f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson());
}

void MediaGallery::loadState()
{
    QFile f(galleryStatePath());
    if(!f.open(QIODevice::ReadOnly)) return;
    auto doc=QJsonDocument::fromJson(f.readAll());
    if(doc.isNull()) return;
    QJsonObject obj=doc.object();
    m_items.clear();
    for(const auto &v:obj["items"].toArray()){
        QString path=v.toString();
        if(!QFile::exists(path)) continue;
        MediaItem item; item.path=path; item.isVideo=isVideoFile(path);
        m_items<<item;
    }
    rebuildGrid();
    broadcastGalleryList(m_items);
    emit mediaListChanged();
}
