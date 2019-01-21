#ifndef VNCAA_H
#define VNCAA_H

#ifdef __cplusplus
extern "C" {
#endif

// speed up some branches
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) !likely(!(x))

// don't try to tell the server what to do
#define VNC_DEACTIVE_HRES 800
#define VNC_DEACTIVE_VRES 600
#define VNC_DEACTIVE_PIXEL_SIZE 4

#define VNC_DEACTIVE_IMG_HRES 400
#define VNC_DEACTIVE_IMG_VRES 200
#define VNC_DEACTIVE_IMG_X (((VNC_DEACTIVE_HRES) / 2) - ((VNC_DEACTIVE_IMG_HRES) / 2))
#define VNC_DEACTIVE_IMG_Y (((VNC_DEACTIVE_VRES) / 2) - ((VNC_DEACTIVE_IMG_VRES) / 2))
#define VNC_BUF_SIZE (4096 * 2160 * 4)

// never write to this, so no mutex needed
extern const unsigned char vm_off_bin[];

#include <stdint.h>

#ifdef WORDS_BIGENDIAN
#define ENDIAN16(s) (s)
#define ENDIAN32(l) (l)
#else
#define ENDIAN16(s) __builtin_bswap16(s)
#define ENDIAN32(l) __builtin_bswap32(l)
#endif

typedef uint8_t CARD8;
typedef int8_t INT8;
typedef uint16_t CARD16;
typedef int16_t INT16;
typedef uint32_t CARD32;
typedef int32_t INT32;

#include "rfbproto.h"

typedef struct
{
   char *name;
   unsigned int width;
   unsigned int height;
   unsigned int bpp;
   unsigned int depth;
   unsigned int bigendian;
   unsigned int truecolour;
   unsigned int redmax;
   unsigned int greenmax;
   unsigned int bluemax;
   unsigned int redshift;
   unsigned int greenshift;
   unsigned int blueshift;
   unsigned int pixelsize;
   unsigned int stride;
}
server_t;

typedef struct
{
    int updated;
    int fbsize_updated;
    unsigned int update_offset;  // offset into data to start updating
    unsigned int update_size;    // size of data to update
}
scrn_status_t;

typedef struct
{
    const char *uuid;            // used for identifying the display
    const char *socket;
    uint16_t port;
    void *buffer;                // allocated or remapped region
    int use_buffer;              // use user buffer instead
}
vnc_thread_cfg_t;

typedef struct
{
    char *path;
    int sock;                    // connected socket for xfer
    int version;                 // version of protocol between client / server
    server_t server;
    uint8_t buf[VNC_BUF_SIZE];   // buffer for storing pixel data
    rfbFramebufferUpdateRequestMsg urq;
    scrn_status_t status;
    vnc_thread_cfg_t cfg;
}
vnc_t;

void *vnc_thread(void *config);
void vnc_vm_off(vnc_t *vnc);
int rfb_connect(vnc_t *vnc, const char *socket, uint16_t port);
int rfb_grab(vnc_t *vnc, int update);
int rfb_disconnect(vnc_t *vnc);

#ifdef __cplusplus
}
#endif

#endif
