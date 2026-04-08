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

// ══════════════════════════════════════════════════════════════
// MediaCard
// ══════════════════════════════════════════════════════════════

MediaCard::MediaCard(const MediaItem &item, int index, QWidget *parent)
    : QFrame(parent), m_item(item), m_index(index)
{
    setFixedSize(CARD_W, CARD_H);
    setFrameShape(QFrame::StyledPanel);
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
    setActive(false);
}

void MediaCard::buildThumbnail()
{
    if (!m_item.isVideo) {
        // Image: load and scale directly
        QPixmap px(m_item.path);
        if (!px.isNull()) {
            m_thumb->setPixmap(
                px.scaled(THUMB_W, THUMB_H,
                          Qt::KeepAspectRatio,
                          Qt::SmoothTransformation));
            return;
        }
    } else {
        // Video: ask Windows Shell for a real thumbnail frame
        QPixmap shellPx = shellThumbnail(m_item.path, THUMB_W, THUMB_H);
        if (!shellPx.isNull()) {
            m_thumb->setPixmap(shellPx);
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
    m_thumb->setPixmap(px);
}

void MediaCard::setActive(bool active)
{
    m_active = active;
    if (active) {
        setStyleSheet(
            "MediaCard{background:#1e6bb8;"
            "border:2px solid #4da6ff;border-radius:4px;}");
        m_nameLbl->setStyleSheet(
            "color:#fff;font-size:8px;background:transparent;");
    } else {
        setStyleSheet(
            "MediaCard{background:#2a2a2a;"
            "border:1px solid #444;border-radius:4px;}"
            "MediaCard:hover{border:1px solid #888;background:#333;}");
        m_nameLbl->setStyleSheet(
            "color:#aaa;font-size:8px;background:transparent;");
    }
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

void MediaCard::paintEvent(QPaintEvent *e){ QFrame::paintEvent(e); }

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


// Broadcast current media list to all connected manager clients
static void broadcastGalleryList(const QList<MediaItem> &items)
{
    auto *srv = lyricsWsServer();
    if (!srv) return;
    QStringList paths;
    for (const auto &item : items)
        paths << item.path;
    srv->broadcastMediaList(paths);
}

MediaGallery::MediaGallery(QWidget *parent):QWidget(parent)
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
    m_clearBtn=new QPushButton(QString::fromUtf8("\u25A0 Clear"));
    m_clearBtn->setStyleSheet(
        "QPushButton{background:#4a1a1a;color:#ff6b6b;"
        "border:1px solid #8b2020;border-radius:3px;padding:2px 6px;}"
        "QPushButton:hover{background:#6a2020;}");
    topL->addWidget(addBtn);
    topL->addStretch();
    topL->addWidget(m_clearBtn);
    root->addWidget(topBar);

    // ── Thumbnail grid ────────────────────────────────────────
    m_scrollArea=new QScrollArea;
    m_scrollArea->setWidgetResizable(true);
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
    m_timeLbl=new QLabel("0:00 / 0:00");
    m_timeLbl->setStyleSheet("color:#888;font-size:9px;");
    m_timeLbl->setFixedWidth(76);
    scrubRow->addWidget(m_playPauseBtn);
    scrubRow->addWidget(m_scrubber,1);
    scrubRow->addWidget(m_timeLbl);
    vcL->addLayout(scrubRow);
    m_videoControls->setVisible(false);
    root->addWidget(m_videoControls);

    // ── Fade duration bar ─────────────────────────────────────
    auto *fadeBar=new QWidget;
    fadeBar->setStyleSheet(
        "background:#1e1e1e;border-top:1px solid #333;");
    auto *fadeL=new QHBoxLayout(fadeBar);
    fadeL->setContentsMargins(8,4,8,4);
    auto *fadeLbl=new QLabel("Fade Duration:");
    fadeLbl->setStyleSheet("color:#aaa;font-size:9px;");
    m_fadeSpin=new QSpinBox;
    m_fadeSpin->setRange(0,3000);
    m_fadeSpin->setSingleStep(50);
    m_fadeSpin->setValue(500);
    m_fadeSpin->setSuffix(" ms");
    m_fadeSpin->setFixedWidth(90);
    fadeL->addWidget(fadeLbl);
    fadeL->addWidget(m_fadeSpin);
    fadeL->addStretch();
    root->addWidget(fadeBar);

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
    connect(m_scrubber,&QSlider::sliderPressed,
            this,[this](){ m_scrubbing=true; });
    connect(m_scrubber,&QSlider::sliderReleased,
            this,[this](){
                m_scrubbing=false;
                onScrubberMoved(m_scrubber->value());
            });
    connect(m_scrubber,&QSlider::valueChanged,
            this,[this](int v){ if(m_scrubbing) onScrubberMoved(v); });
}

int MediaGallery::fadeDurationMs() const
{
    return m_fadeSpin->value();
}

int MediaGallery::calcCols() const
{
    int available=m_container->width()-12;
    int colWidth =MediaCard::CARD_W+6;
    return qMax(1, available/colWidth);
}

bool MediaGallery::eventFilter(QObject *obj, QEvent *event)
{
    if(obj==m_container && event->type()==QEvent::Resize){
        int nc=calcCols();
        if(nc!=m_currentCols){
            m_currentCols=nc;
            rebuildGrid();
        }
    }
    return QWidget::eventFilter(obj,event);
}

void MediaGallery::rebuildGrid()
{
    for(auto *c:m_cards){ m_grid->removeWidget(c); c->deleteLater(); }
    m_cards.clear();
    int col=0,row=0;
    for(int i=0;i<m_items.size();++i){
        auto *card=new MediaCard(m_items[i],i,m_container);
        connect(card,&MediaCard::clicked,
                this,&MediaGallery::onCardClicked);
        connect(card,&MediaCard::removeRequested,
                this,&MediaGallery::onCardRemoveRequested);
        card->setActive(i==m_activeIndex);
        m_grid->addWidget(card,row,col);
        m_cards<<card;
        if(++col>=m_currentCols){ col=0; ++row; }
    }
}

void MediaGallery::setActiveCard(int index)
{
    m_activeIndex=index;
    for(auto *c:m_cards) c->setActive(c->index()==index);
    updateVideoControls();
}

void MediaGallery::updateVideoControls()
{
    bool showVid=(m_activeIndex>=0 &&
                  m_activeIndex<m_items.size() &&
                  m_items[m_activeIndex].isVideo);
    m_videoControls->setVisible(showVid);
    if(showVid){
        m_paused=false;
        m_playPauseBtn->setText(QString::fromUtf8("\u23F8"));
    }
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
}

void MediaGallery::onClearMedia()
{
    setActiveCard(-1);
    lyrics_renderer_media_clear();
    auto *srv = lyricsWsServer();
    if (srv) srv->broadcastMediaState(QString());
}

void MediaGallery::onCardClicked(int index)
{
    if(index<0||index>=m_items.size()) return;
    setActiveCard(index);
    QString path = m_items[index].path;
    lyrics_renderer_media_push(path, fadeDurationMs());
    auto *srv = lyricsWsServer();
    if (srv) srv->broadcastMediaState(path);
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
    rebuildGrid();
    saveState();
    broadcastGalleryList(m_items);
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

void MediaGallery::onScrubberMoved(int value)
{
    int64_t t=0,d=0;
    if(!lyrics_renderer_media_get_time(t,d)) return;
    if(d<=0) return;
    lyrics_renderer_media_set_time(
        (int64_t)((double)value/1000.0*(double)d));
}

void MediaGallery::onPollTimer()
{
    if(!m_videoControls->isVisible()||m_scrubbing) return;
    int64_t t=0,d=0;
    if(!lyrics_renderer_media_get_time(t,d)) return;
    if(d>0) m_scrubber->setValue((int)((double)t/(double)d*1000.0));
    auto fmt=[](int64_t ms)->QString{
        int s=(int)(ms/1000);
        return QString("%1:%2").arg(s/60).arg(s%60,2,10,QChar('0'));
    };
    m_timeLbl->setText(fmt(t)+" / "+fmt(d));
}

QString MediaGallery::galleryStatePath() const
{
    QString dir=QStandardPaths::writableLocation(
                    QStandardPaths::AppDataLocation)
                +"/obs-lyrics-dock";
    QDir().mkpath(dir);
    return dir+"/media-gallery.json";
}

void MediaGallery::saveState()
{
    QJsonArray arr;
    for(const auto &item:m_items) arr.append(item.path);
    QJsonObject obj;
    obj["items"]    = arr;
    obj["fadeMs"]   = m_fadeSpin->value();
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
    if(obj.contains("fadeMs"))
        m_fadeSpin->setValue(obj["fadeMs"].toInt());
    rebuildGrid();
    broadcastGalleryList(m_items);
}
