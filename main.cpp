#include <QApplication>
#include <QMainWindow>
#include <QPainter>
#include <QTimer>
#include <QShortcut>
#include "vnc.h"

#include <sys/stat.h>

//#define VNC_TCP

#define VNC_HRES 1280
#define VNC_VRES 1024

#ifdef VNC_TCP
#define VNC_PATH "127.0.0.1"
#define VNC_PORT 5900
#else
#define VNC_PATH "/var/run/vnc-0"
#define VNC_PORT 0
#endif

class Screen : public QWidget {
protected:
    virtual void paintEvent(QPaintEvent *)
    {
        QPainter c(this);
        const QRect& cw = c.window();
        c.drawImage(cw, m_image);
    }
public:
    void refresh(scrn_status_t *status)
    {
        memcpy(m_image.bits() + status->update_offset, vnc.buf + status->update_offset, static_cast<size_t>(status->update_size));
        update();
    }
    void setsize(int w, int h)
    {
        m_w = w;
        m_h = h;
        m_image = QImage(m_w, m_h, QImage::Format_RGBA8888);
    }
    Screen(QWidget *parent = Q_NULLPTR) : QWidget(parent)
    {
        setsize(0, 0);
        m_image.fill(Qt::white);
    }
private:
    QImage m_image;
    int m_w, m_h;
};

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QMainWindow w;
    Screen scrn;
    scrn_status_t status;
    int value;

    w.setCentralWidget(&scrn);
    w.setFixedSize(0, 0);
    w.show();

    value = rfb_connect(static_cast<const char*>(VNC_PATH), VNC_PORT, &status);
    if( value == 0 )
    {
        return a.exec();
    }

    while( 1 ) {
        if( !rfb_grab(0, &status) )
        {
            break;
        }
        if( status.fbsize_updated )
        {
            status.fbsize_updated = 0;
            w.setFixedSize(vnc.server.width, vnc.server.height);
            scrn.setsize(vnc.server.width, vnc.server.height);
        }
        if( status.updated )
        {
            status.updated = 0;
            scrn.refresh(&status);
        }
        qApp->processEvents();
    }

    rfb_disconnect();

    return a.exec();
}
