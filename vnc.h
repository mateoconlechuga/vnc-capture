#ifndef VNCAA_H
#define VNCAA_H

#ifdef __cplusplus
extern "C" {
#endif

// don't try to tell the server what to do
#define VNC_DEACTIVE_HRES 800
#define VNC_DEACTIVE_VRES 600
#define VNC_DEACTIVE_PIXEL_SIZE 4

#define VNC_DEACTIVE_IMG_HRES 400
#define VNC_DEACTIVE_IMG_VRES 200
#define VNC_DEACTIVE_IMG_X (((VNC_DEACTIVE_HRES) / 2) - ((VNC_DEACTIVE_IMG_HRES) / 2))
#define VNC_DEACTIVE_IMG_Y (((VNC_DEACTIVE_VRES) / 2) - ((VNC_DEACTIVE_IMG_VRES) / 2))

extern unsigned char vm_off_bin[];

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
    char *path;
    int sock;                         // connected socket for xfer
    int version;                      // version of protocol between client / server
    server_t server;
    uint8_t buf[4610 * 2160 * 4 * 2]; // buffer for storing pixel data
    rfbFramebufferUpdateRequestMsg urq;
    scrn_status_t status;
}
vnc_t;
extern vnc_t vnc;

void vnc_vm_off(void);
int rfb_connect(const char *ip, uint16_t port);
int rfb_grab(int update);
int rfb_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif
