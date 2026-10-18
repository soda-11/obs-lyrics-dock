#include "preview-window.hpp"

#include <obs-module.h>
#include <graphics/graphics.h>

#include <QScreen>
#include <QKeyEvent>
#include <QGuiApplication>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

// ── Draw callback — scales OBS program output to fit the window ──
void PreviewWindow::drawCallback(void *data, uint32_t cx, uint32_t cy)
{
    Q_UNUSED(data)
    if (cx == 0 || cy == 0) return;

    obs_video_info ovi = {};
    if (!obs_get_video_info(&ovi)) return;
    uint32_t srcW = ovi.base_width;
    uint32_t srcH = ovi.base_height;
    if (srcW == 0 || srcH == 0) return;

    float scaleX = (float)cx / srcW;
    float scaleY = (float)cy / srcH;
    float scale  = qMin(scaleX, scaleY);
    int newW = (int)(srcW * scale);
    int newH = (int)(srcH * scale);
    int x    = ((int)cx - newW) / 2;
    int y    = ((int)cy - newH) / 2;

    gs_viewport_push();
    gs_projection_push();
    gs_ortho(0.f, (float)srcW, 0.f, (float)srcH, -100.f, 100.f);
    gs_set_viewport(x, y, newW, newH);

    obs_render_main_texture();

    gs_projection_pop();
    gs_viewport_pop();
}

// ── Constructor / Destructor ──────────────────────────────────────
PreviewWindow::PreviewWindow()
    : QWidget(nullptr,
              Qt::Window | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setStyleSheet("background:black;");
}

PreviewWindow::~PreviewWindow()
{
    destroyDisplay();
}

// ── Display lifecycle ─────────────────────────────────────────────
void PreviewWindow::createDisplay()
{
    if (m_display) return;

    gs_init_data info = {};
    QSize sz = size() * devicePixelRatioF();
    info.cx       = (uint32_t)qMax(1, sz.width());
    info.cy       = (uint32_t)qMax(1, sz.height());
    info.format   = GS_BGRA;
    info.zsformat = GS_ZS_NONE;

#ifdef _WIN32
    info.window.hwnd = (HWND)winId();
#endif

    m_display = obs_display_create(&info, 0xFF000000);
    if (m_display)
        obs_display_add_draw_callback(m_display, drawCallback, this);
}

void PreviewWindow::destroyDisplay()
{
    if (!m_display) return;
    obs_display_remove_draw_callback(m_display, drawCallback, this);
    obs_display_destroy(m_display);
    m_display = nullptr;
}

// ── Public API ────────────────────────────────────────────────────
void PreviewWindow::openOnScreen(QScreen *screen)
{
    if (!screen) return;
    destroyDisplay(); // recreate for new screen/HWND
    setGeometry(screen->geometry());
    show();
    setAlwaysOnTop(m_alwaysOnTop);
}

void PreviewWindow::setAlwaysOnTop(bool top)
{
    m_alwaysOnTop = top;
    if (!isVisible()) return;
#ifdef _WIN32
    HWND hwnd = (HWND)winId();
    SetWindowPos(hwnd, top ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
    setWindowFlag(Qt::WindowStaysOnTopHint, top);
    if (isVisible()) { hide(); show(); }
#endif
}

// ── Event handlers ────────────────────────────────────────────────
void PreviewWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    createDisplay();
    setAlwaysOnTop(m_alwaysOnTop);
}

void PreviewWindow::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    if (m_display) {
        QSize sz = size() * devicePixelRatioF();
        obs_display_resize(m_display,
            (uint32_t)qMax(1, sz.width()),
            (uint32_t)qMax(1, sz.height()));
    }
}

void PreviewWindow::closeEvent(QCloseEvent *e)
{
    destroyDisplay();
    QWidget::closeEvent(e);
    emit closed();
}

void PreviewWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_F) {
        close();
    } else {
        QWidget::keyPressEvent(e);
    }
}
