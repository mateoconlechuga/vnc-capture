#include <QApplication>
#include <QMainWindow>
#include <QPainter>
#include <QTime>
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
    void refresh(void)
    {
        memcpy(m_image.bits() + vnc.status.update_offset, vnc.buf + vnc.status.update_offset, static_cast<size_t>(vnc.status.update_size));
        update();
    }
    void setsize(int w, int h)
    {
        m_w = w;
        m_h = h;
        m_image = QImage(m_w, m_h, QImage::Format_RGBX8888);
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

void update(QMainWindow &w, Screen &scrn)
{
    if( vnc.status.fbsize_updated )
    {
        vnc.status.fbsize_updated = 0;
        w.setFixedSize(vnc.server.width, vnc.server.height);
        scrn.setsize(vnc.server.width, vnc.server.height);
    }
    if( vnc.status.updated )
    {
        vnc.status.updated = 0;
        scrn.refresh();
    }
    qApp->processEvents();
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QMainWindow w;
    Screen scrn;
    int value;

    w.setCentralWidget(&scrn);
    w.setFixedSize(0, 0);
    w.show();

    while( 1 )
    {
        while( 1 )
        {
            value = rfb_connect(static_cast<const char*>(VNC_PATH), VNC_PORT);
            if( value == 0 )
            {
                return a.exec();
            }
            if( value == 1 )
            {
                break;
            }
            if( value == 2 )
            {
                vnc_vm_off();
                update(w, scrn);
                QTime dt = QTime::currentTime().addMSecs(2000);
                while (QTime::currentTime() < dt) {
                    qApp->processEvents();
                }
                continue;
            }
        }

        while( 1 )
        {
            if( !rfb_grab(0) )
            {
                break;
            }
            update(w, scrn);
        }

        fprintf(stdout, "vnc connection lost.\n");
        fflush(stdout);
    }

    return a.exec();
}
