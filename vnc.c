#include "rfbproto.h"
#include "vnc.h"

#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

vnc_t vnc;

// used for regulating the refresh rate
long time_diff(struct timespec start, struct timespec end)
{
    return (end.tv_sec - start.tv_sec) * (long)1e9 + (end.tv_nsec - start.tv_nsec);
}

// blocks until all bytes are read from socket
static inline int rfb_read(void *out, unsigned int n)
{
    size_t len = recv(vnc.sock, out, (size_t)n, MSG_WAITALL);
    if( len != n )
    {
        return 0;
    }
    return 1;
}

// attempts to write data to socket
static int rfb_write(void *out, size_t n)
{
    fd_set fds;
    uint8_t *buf = out;
    int i = 0;
    int j;

    while (i < n) {
        j = write(vnc.sock, buf + i, n - i);
        if (j <= 0)
        {
            if (j < 0)
            {
                if (errno == EWOULDBLOCK || errno == EAGAIN)
                {
                    FD_ZERO(&fds);
                    FD_SET(vnc.sock, &fds);

                    if (select(vnc.sock + 1, NULL, &fds, NULL, NULL) <= 0)
                    {
                        fprintf(stderr, "select failed.\n");
                        return 0;
                    }

                    j = 0;
                } else {
                    fprintf(stderr, "write failed.\n");
                    return 0;
                }
            } else {
                fprintf(stderr, "write failed bad.\n");
                return 0;
            }
        }
        i += j;
    }
    return 1;
}

// negotiate protocol used between client and server
// 3.8 is perferred
int rfb_negotiate_link(void)
{
    uint16_t server_major, server_minor;
    rfbProtocolVersionMsg msg;
    char major_str[4];
    char minor_str[4];

    // read the protocol version
    if( !rfb_read(&msg, sz_rfbProtocolVersionMsg) )
    {
        return 0;
    }

    // check socket header match
    if( msg[0] != 'R' || msg[1] != 'F' || msg[2] != 'B' || msg[3] != ' ' || msg[7] != '.' )
    {
        fprintf(stderr, "not a valid rfb socket.\n");
        return 0;
    }
    msg[11] = 0;

    // negotiate protocol version
    major_str[0] = msg[4];
    major_str[1] = msg[5];
    major_str[2] = msg[6];
    major_str[3] = 0;

    minor_str[0] = msg[8];
    minor_str[1] = msg[9];
    minor_str[2] = msg[10];
    minor_str[3] = 0;

    server_major = strtol((const char*)&major_str, NULL, 10);
    server_minor = strtol((const char*)&minor_str, NULL, 10);

    fprintf(stdout, "server protocol version: %s\n", msg);

    if( server_major == 3 && server_minor >= 8 )
    {
        vnc.version = 8;
    }
    else if( server_major == 3 && server_minor == 7 )
    {
        vnc.version = 7;
    }
    else
    {
        vnc.version = 3;
    }

    sprintf(msg, rfbProtocolVersionFormat, rfbProtocolMajorVersion, vnc.version);
    fprintf(stdout, "client tries protocol: %s", msg);

    if( !rfb_write(msg, sz_rfbProtocolVersionMsg) )
    {
        return 0;
    }

    return 1;
}

// ensure security protocol is valid and good
int rfb_authenticate_link(void) {
    CARD32 auth_result;
    CARD32 scheme;

    if( vnc.version >= 7 )
    {
        CARD8 sec_type = rfbSecTypeInvalid;
        CARD8 num_sec_types;
        CARD8 *sec_types;

        if( !rfb_read(&num_sec_types, sizeof num_sec_types) )
        {
            return 0;
        }

        if( num_sec_types == 0 )
        {
            fprintf(stderr, "connection error.\n");
            return 0;
        }

        sec_types = malloc(num_sec_types);

        if( !sec_types )
        {
            return 0;
        }

        if( !rfb_read(sec_types, num_sec_types) )
        {
            free(sec_types);
            return 0;
        }

        for( uint8_t x = 0; x < num_sec_types; x++ ) {
            if( sec_types[x] == rfbSecTypeNone )
            {
                sec_type = rfbSecTypeNone;
                break;
            }
        }

        free(sec_types);

        if( !rfb_write(&sec_type, sizeof sec_type) )
        {
            return 0;
        }

        scheme = sec_type;
    }
    else
    {
        if( !rfb_read(&scheme, sizeof scheme))
        {
            return 0;
        }
        scheme = ENDIAN32(scheme);
        if( scheme == rfbSecTypeInvalid )
        {
            fprintf(stderr, "connection error.\n");
            return 0;
        }
    }

    switch( scheme )
    {
        default:
        case rfbSecTypeInvalid:
            fprintf(stdout, "no supported security type available.\n");
            break;
        case rfbSecTypeNone:
            if( vnc.version >= 8 )
            {
                if( !rfb_read(&auth_result, sizeof auth_result) )
                {
                    return 0;
                }

                auth_result = ENDIAN32(auth_result);

                switch(auth_result) {
                    case rfbVncAuthOK:
                        fprintf(stdout, "authentication ok!\n");
                        return 1;
                    case rfbVncAuthFailed:
                        fprintf(stdout, "authentication failed.\n");
                        break;
                    case rfbVncAuthTooMany:
                        fprintf(stdout, "too many connections.\n");
                        break;
                    default:
                        fprintf(stdout, "unknown authentication error 0x%08X.\n", auth_result);
                        break;
                }
            }
            break;
    }

    return 0;
}

// get the server configuration
// also tell the server about us
int rfb_initialize_server(void)
{
    unsigned int len;
    rfbServerInitMsg si;
    rfbClientInitMsg cl;
    cl.shared = 1;

    if( !rfb_write(&cl, sz_rfbClientInitMsg) )
    {
        return 0;
    }

    if( !rfb_read(&si, sz_rfbServerInitMsg) )
    {
        return 0;
    }

    len = ENDIAN32(si.nameLength);
    vnc.server.name = malloc(sizeof(char) * len + 1);
    vnc.server.width = ENDIAN16(si.framebufferWidth);
    vnc.server.height = ENDIAN16(si.framebufferHeight);
    vnc.server.bpp = si.format.bitsPerPixel;
    vnc.server.depth = si.format.depth;
    vnc.server.bigendian = si.format.bigEndian;
    vnc.server.truecolour = si.format.trueColour;
    vnc.server.redmax = ENDIAN16(si.format.redMax);
    vnc.server.greenmax = ENDIAN16(si.format.greenMax);
    vnc.server.bluemax = ENDIAN16(si.format.blueMax);
    vnc.server.redshift = si.format.redShift;
    vnc.server.greenshift = si.format.greenShift;
    vnc.server.blueshift = si.format.blueShift;
    vnc.server.pixelsize = vnc.server.bpp / 8;
    vnc.server.stride = vnc.server.width * vnc.server.pixelsize;

    if( !rfb_read(vnc.server.name, len) )
    {
        return 0;
    }
    vnc.server.name[len] = 0;

    fprintf(stdout, "server \'%s\' configuration:\n", vnc.server.name);
    fprintf(stdout, "  width: %d\theight: %d\n", vnc.server.width, vnc.server.height);
    fprintf(stdout, "  bpp: %d\tdepth: %d\n", vnc.server.bpp, vnc.server.depth);
    fprintf(stdout, "  bigendian: %d\ttruecolor: %d\n", vnc.server.bigendian, vnc.server.truecolour);
    fprintf(stdout, "  redmax: %d\tgreenmax: %d\tbluemax: %d\n", vnc.server.redmax, vnc.server.greenmax, vnc.server.bluemax);
    fprintf(stdout, "  redshift: %d\tgreenshift: %d\tblueshift: %d\n", vnc.server.redshift, vnc.server.greenshift, vnc.server.blueshift);
    return 1;
}

#define NUM_ENCODINGS 2

typedef struct
{
    rfbSetEncodingsMsg msg;
    CARD32 enc[MAX_ENCODINGS];
}
encoding_t;

// just use the server formats
// we want this to be as fast as possible, so don't let the server translate
int rfb_negotiate_frame_format(void)
{
    encoding_t em;
    em.msg.type = rfbSetEncodings;

    em.enc[0] = ENDIAN32(rfbEncodingRaw);
    em.enc[1] = ENDIAN32(rfbEncodingNewFBSize);

    em.msg.nEncodings = ENDIAN16(NUM_ENCODINGS);

    fprintf(stdout, "set encoding types: %d, %lu\n", NUM_ENCODINGS, NUM_ENCODINGS * sizeof(CARD32));

    if( !rfb_write(&em, sz_rfbSetEncodingsMsg + (NUM_ENCODINGS * sizeof(CARD32))) )
    {
        return 0;
    }

    fprintf(stdout, "configured encoding types.\n");

    return 1;
}

void vnc_vm_off(void)
{
    unsigned int stride;
    unsigned int height;
    uint8_t *dst;
    uint8_t *src;

    // update the screen status anyway
    memset(vnc.buf, 0, sizeof vnc.buf);

    // draw the 'off' image
    vnc.server.stride = VNC_DEACTIVE_HRES * VNC_DEACTIVE_PIXEL_SIZE;
    vnc.server.width = VNC_DEACTIVE_HRES;
    vnc.server.height = VNC_DEACTIVE_VRES;
    vnc.server.pixelsize = VNC_DEACTIVE_PIXEL_SIZE;

    src = vm_off_bin;
    dst = vnc.buf + (VNC_DEACTIVE_IMG_Y * vnc.server.stride) + (VNC_DEACTIVE_IMG_X * vnc.server.pixelsize);
    height = VNC_DEACTIVE_IMG_VRES;
    stride = VNC_DEACTIVE_IMG_HRES * vnc.server.pixelsize;

    while( height-- )
    {
        memcpy(dst, src, stride);
        dst += vnc.server.stride;
        src += stride;
    }

    vnc.status.fbsize_updated = 1;
    vnc.status.updated = 1;
    vnc.status.update_size = VNC_DEACTIVE_HRES * VNC_DEACTIVE_VRES * VNC_DEACTIVE_PIXEL_SIZE;
    vnc.status.update_offset = 0;
}

int rfb_disconnect(void)
{
    int status = close(vnc.sock);
    fprintf(stdout, "disconnected.\n");
    fflush(stdout);

    // update the screen status anyway
    memset(vnc.buf, 0, sizeof vnc.buf);

    // show the off state
    vnc_vm_off();

    return status;
}

int rfb_cut_text_message(rfbServerToClientMsg *msg) {
    CARD32 size;
    char *buf;

    if( !rfb_read(((char*)&msg->sct) + 1, sz_rfbServerCutTextMsg - 1) )
    {
        return 0;
    }
    size = ENDIAN32(msg->sct.length);

    buf = malloc((sizeof(char) * size) + 1);
    if( !buf )
    {
        return 0;
    }

    if( !rfb_read(buf, size) )
    {
        free(buf);
        return 0;
    }

    buf[size] = 0;

    fprintf(stdout, "text msg: %s\n", buf);

    free(buf);
    return 1;
}

static int rfb_enc_raw(rfbFramebufferUpdateRectHeader rectheader)
{
    unsigned int height = rectheader.r.h;
    unsigned int stride = rectheader.r.w * vnc.server.pixelsize;
    uint8_t *buf;

    buf = vnc.buf + (rectheader.r.y * vnc.server.stride) + (rectheader.r.x * vnc.server.pixelsize);

    // optimize the case where data spans the whole screen
    // this doesn't actually help; because packets are huge
    /*if( rectheader.r.x == 0 && stride == vnc.server.width )
    {
        if( !rfb_read(buf, height * stride))
        {
            return 0;
        }
    }*/
    while( height-- )
    {
        if( !rfb_read(buf, stride))
        {
            return 0;
        }

        buf += vnc.server.stride;
    }

    return 1;
}

static int rfb_request_frame(uint8_t incr)
{
    static rfbFramebufferUpdateRequestMsg fur = { 0 };

    fur.type = rfbFramebufferUpdateRequest;
    fur.incremental = incr;
    fur.x = 0;
    fur.y = 0;
    fur.w = ENDIAN16(vnc.server.width);
    fur.h = ENDIAN16(vnc.server.height);

    if( !rfb_write(&fur, sz_rfbFramebufferUpdateRequestMsg) )
    {
        fprintf(stdout, "request error.\n");
        rfb_disconnect();
        return 0;
    }
    return 1;
}

// handles incomming messages from the vnc server
// this is where the dma operations should take place
// pass it a pointer and it will tell you if you need to
// refresh the screen that is in the buffer
static int rfb_handle_message(void)
{
    rfbFramebufferUpdateRectHeader rectheader;
    rfbServerToClientMsg msg;
    uint16_t i;
    int miny = INT_MAX;
    int maxy = INT_MIN;

    if( !rfb_read(&msg, 1) )
    {
        return 0;
    }

    switch( msg.type )
    {
        case rfbFramebufferUpdate:
            if( !rfb_read(((char*)&msg.fu) + 1, sz_rfbFramebufferUpdateMsg - 1) )
            {
                return 0;
            }
            msg.fu.nRects = ENDIAN16(msg.fu.nRects);
            for( i = 0; i < msg.fu.nRects; i++ )
            {
                int result = 0;
                if( !rfb_read(&rectheader, sz_rfbFramebufferUpdateRectHeader) )
                {
                        return 0;
                }

                rectheader.r.x = ENDIAN16(rectheader.r.x);
                rectheader.r.y = ENDIAN16(rectheader.r.y);
                rectheader.r.w = ENDIAN16(rectheader.r.w);
                rectheader.r.h = ENDIAN16(rectheader.r.h);

                // used for determining what region of the screen to update
                if( rectheader.r.y < miny )
                {
                    miny = rectheader.r.y;
                }
                if( (rectheader.r.y + rectheader.r.h) > maxy )
                {
                    maxy = rectheader.r.y + rectheader.r.h;
                }

                switch( ENDIAN32(rectheader.encoding) )
                {
                    case rfbEncodingRaw:
                        result = rfb_enc_raw(rectheader);
                        break;
                    case rfbEncodingNewFBSize:
                        vnc.server.width = rectheader.r.w;
                        vnc.server.height = rectheader.r.h;
                        vnc.server.stride = vnc.server.width * vnc.server.pixelsize;
                        vnc.urq.w = ENDIAN16(vnc.server.width);
                        vnc.urq.h = ENDIAN16(vnc.server.height);
                        vnc.status.fbsize_updated = 1;
                        vnc.status.updated = 1;

                        // update the screen on resize
                        memset(vnc.buf, 0, sizeof vnc.buf);
                        miny = 0;
                        maxy = vnc.server.height;
                        fprintf(stdout, "resize requested: %dx%d\n", rectheader.r.w, rectheader.r.h);
                        fflush(stdout);
                        result = 1;
                        break;
                    case rfbEncodingLastRect:
                        result = 1;
                        break;
                    default:
                        fprintf(stdout, "unknwon request: %d", ENDIAN32(rectheader.encoding));
                        break;
                }

                if( !result )
                {
                    fprintf(stdout, "encoding failed.\n");
                    return 0;
                }
            }
            rfb_request_frame(1);

            // inform the user of the updated range
            // this prevents copying of the entire buffer, and instead just the updated rectangle
            vnc.status.updated = 1;
            vnc.status.update_offset = miny * vnc.server.stride;
            vnc.status.update_size = (maxy - miny) * vnc.server.stride;
            break;
        case rfbSetColourMapEntries:
            rfb_read(((char*)&msg.scme) + 1, sz_rfbSetColourMapEntriesMsg - 1);
            break;
        case rfbBell:
            break;
        case rfbServerCutText:
            if( !rfb_cut_text_message(&msg) )
            {
                return 0;
            }
            break;
        default:
            fprintf(stdout, "encoding failed.\n");
            return 0;
    }
    return 1;
}

int rfb_grab(int update)
{
    uint32_t buf;

    // check if the server disconnected
    ssize_t connected = recv(vnc.sock, &buf, sizeof buf, MSG_PEEK | MSG_DONTWAIT);
    if( connected == 0 )
    {
        rfb_disconnect();
        return 0;
    }
    else
    {
        // check for frames
        if( !rfb_handle_message() )
        {
            rfb_disconnect();
            return 0;
        }

        // this is only for requesting a full frame update
        if (update) {
            rfb_request_frame(0);
        }
    }

    return 1;
}

// connects to the hosted socket addresses
// returns the socket file descriptor
int rfb_connect(const char *path, uint16_t port)
{
    // if port is used, assume tcp
    if (port) {

        struct sockaddr_in serv_addr;
        int one = 1;

        fprintf(stdout, "attempting to start tcp socket... ");
        if( (vnc.sock = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
        {
            printf("error.\n");
            return 0;
        }
        fprintf(stdout, "complete.\n");
        fprintf(stdout, "attempting to connect to \'%s:%d\'... ", path, port);

        memset(&serv_addr, '0', sizeof(serv_addr));

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);

        if( inet_pton(AF_INET, path, &serv_addr.sin_addr) <= 0 )
        {
            fprintf(stdout, "invalid address.\n");
            return 0;
        }

        // actually try to connect
        if( connect(vnc.sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0 )
        {
            fprintf(stdout, "could not open.\n");
            return 0;
        }
        fprintf(stdout, "complete.\n");
        fprintf(stdout, "attempting to set socket options... ");

    }
    else
    {
        struct sockaddr_un serv_addr;
        struct stat sb;

        fprintf(stdout, "attempting to connect to \'%s\'\n", path);

        while( stat(path, &sb) == -1 || (sb.st_mode & S_IFMT) != S_IFSOCK )
        {
            return 2;
        }

        if( (vnc.sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0 )
        {
            printf("error.\n");
            return 0;
        }

        memset(&serv_addr, '0', sizeof(serv_addr));

        serv_addr.sun_family = AF_UNIX;
        if (path)
        {
             strncpy(serv_addr.sun_path, path, sizeof(serv_addr.sun_path) - 1);
        }

        if( (connect(vnc.sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) )
        {
            return 2;
        }
        fprintf(stdout, "connected.\n");
    }

    // next, attempt to link to rfb
    fprintf(stdout, "attempting to negotiate link.\n");
    if( !rfb_negotiate_link() )
    {
        fprintf(stdout, "negotiate error.");
        rfb_disconnect();
        return 0;
    }
    if( !rfb_authenticate_link() )
    {
        fprintf(stdout, "authenticate error.");
        rfb_disconnect();
        return 0;
    }
    if( !rfb_initialize_server() )
    {
        fprintf(stdout, "server error.");
        rfb_disconnect();
        return 0;
    }
    if( !rfb_negotiate_frame_format() )
    {
        fprintf(stdout, "frame format error.");
        rfb_disconnect();
        return 0;
    }

    fprintf(stdout, "successfully connected to vnc server.\n");

    // just wait for data, not needed but it makes me feel better
    sleep(1);

    // request the first frame
    rfb_request_frame(0);

    // inform the drawer to set the new size
    vnc.status.fbsize_updated = 1;
    vnc.status.updated = 0;

    fflush(stdout);
    fflush(stderr);

    return 1;
}
