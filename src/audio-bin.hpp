#pragma once
// audio-bin.hpp — background music player for the right panel
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QStringList>

class WebSocketServer;

class AudioBin : public QWidget {
    Q_OBJECT
public:
    explicit AudioBin(QWidget *parent = nullptr);
    void saveState();
    void loadState();
    void setWsServer(WebSocketServer *srv) { m_wsServer = srv; }

    // Called from LyricsDock when remote sends audioBinPlay command.
    void playFileFromRemote(int index);

    QStringList files() const { return m_files; }
    int  activeIndex() const  { return m_activeIndex; }
    bool isPlaying()   const  { return m_playing; }

public slots:
    // Stop playback — called when a video starts or user hits Stop.
    void stopPlayback();

signals:
    // Emitted when audio starts playing so LyricsDock can pause other media if needed.
    void playbackStarted();

private slots:
    void onAddAudio();
    void onItemActivated(QListWidgetItem *item);
    void onStop();
    void onContextMenu(const QPoint &pos);

private:
    void playFile(int index, bool fromRemote = false);
    void rebuildList();
    QString statePath() const;

    WebSocketServer *m_wsServer  = nullptr;
    QListWidget *m_list      = nullptr;
    QPushButton *m_addBtn    = nullptr;
    QPushButton *m_stopBtn   = nullptr;
    QLabel      *m_statusLbl = nullptr;

    QStringList m_files;
    int         m_activeIndex = -1;
    bool        m_playing     = false;
};
