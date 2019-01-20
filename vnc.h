#ifndef VNCAA_H
#define VNCAA_H

#ifdef __cplusplus
extern "C" {
#endif

// don't try to tell the server what to do
#define USE_SERVER_FORMAT

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
   int width;
   int height;
   int bpp;
   int depth;
   int bigendian;
   int truecolour;
   int redmax;
   int greenmax;
   int bluemax;
   int redshift;
   int greenshift;
   int blueshift;
   int pixelsize;
}
server_t;

typedef struct
{
    int sock;                         // connected socket for xfer
    int version;                      // version of protocol between client / server
    server_t server;
    uint8_t buf[4610 * 2160 * 4 * 2]; // buffer for storing pixel data
    rfbFramebufferUpdateRequestMsg urq;
}
vnc_t;
extern vnc_t vnc;

typedef struct
{
    int updated;
    int fbsize_updated;
}
scrn_status_t;

int rfb_connect(const char *ip, uint16_t port, scrn_status_t *status);
int rfb_grab(int update, scrn_status_t *status);

#ifdef __cplusplus
}
#endif

#endif