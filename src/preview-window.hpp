#pragma once
#include <QWidget>
#include <obs.h>

// Fullscreen preview window — renders the OBS program output on a chosen
// monitor. Uses obs_display_create so it is an independent render surface.
class PreviewWindow : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWindow();
    ~PreviewWindow();

    void openOnScreen(QScreen *screen);
    void setAlwaysOnTop(bool top);

signals:
    void closed();

protected:
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void closeEvent(QCloseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void createDisplay();
    void destroyDisplay();
    static void drawCallback(void *data, uint32_t cx, uint32_t cy);

    obs_display_t *m_display    = nullptr;
    bool           m_alwaysOnTop = false;
};
