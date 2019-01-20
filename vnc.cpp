#include "vnc.h"
#include "rfbproto.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>


typedef struct
{
    int sock;    // connected socket for xfer
    int version; // version of protocol between client / server
    server_t server;
}
vnc_t;
static vnc_t vnc;

// blocks until all bytes are read from socket
int rfb_read(void *out, size_t n)
{
    size_t len = recv(vnc.sock, out, n, MSG_WAITALL);
    if( len != n )
    {
        return 0;
    }
    return 1;
}

// attempts to write data to socket
int rfb_write(void *out, size_t n)
{
    int status = write(vnc.sock, out, n);
    if( status == -1 )
    {
        fprintf(stderr, "write failed: %s", strerror(errno));
        return 0;
    }
    return 1;
}

// reports any errors during link negotitation
int rfb_report_err(void)
{
    CARD32 reason_len;
    CARD8 *reason_str;

    if( !rfb_read(&reason_len, sizeof(CARD32)))
    {
        return 0;
    }

    reason_len = ENDIAN32(reason_len);
    reason_str = (CARD8*)malloc(sizeof(CARD8) * reason_len);

    if( !reason_str )
    {
        return 0;
    }

    if( !rfb_read(reason_str, reason_len) )
    {
        free(reason_str);
        return 0;
    }

    fprintf(stdout, "connection error: %s\n", reason_str);
    free(reason_str);
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
            rfb_report_err();
            return 0;
        }

        sec_types = (CARD8*)malloc(num_sec_types);

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
            rfb_report_err();
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
                    case rfbAuthOK:
                        fprintf(stdout, "authentication ok!\n");
                        return 1;
                    case rfbAuthFailed:
                        fprintf(stdout, "authentication failed.\n");
                        break;
                    case rfbAuthTooMany:
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
    int len;
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
    vnc.server.name = (char*)malloc(sizeof(char) * len + 1);
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

typedef struct
{
    rfbSetEncodingsMsg msg;
    CARD32 enc[20];
}
encoding_t;

// just use the server formats
// we want this to be as fast as possible, so don't let the server translate
int rfb_negotiate_frame_format(void)
{
    uint16_t num_enc = 0;
    encoding_t em;

    em.msg.type = rfbSetEncodings;

    em.enc[num_enc++] = ENDIAN32(rfbEncodingCopyRect);
    em.enc[num_enc++] = ENDIAN32(rfbEncodingRaw);
    em.enc[num_enc++] = ENDIAN32(rfbEncodingContinuousUpdates);

    em.msg.nEncodings = ENDIAN16(num_enc);

    fprintf(stdout, "set encoding types: %d, %lu\n", num_enc, num_enc * sizeof(CARD32));

    if( !rfb_write(&em, sz_rfbSetEncodingsMsg + (num_enc * sizeof(CARD32))) )
    {
        return 0;
    }

    fprintf(stdout, "configured encoding types.\n");

    return 1;
}

// tell the server to just push to us
int rfb_set_continuous_updates(int enable) {
    rfbEnableContinuousUpdatesMsg urq = { 0 };

    urq.type = rfbEnableContinuousUpdates;
    urq.enable = enable;
    urq.x = 0;
    urq.y = 0;
    urq.w = vnc.server.width;
    urq.h = vnc.server.height;
    urq.x = ENDIAN16(urq.x);
    urq.y = ENDIAN16(urq.y);
    urq.w = ENDIAN16(urq.w);
    urq.h = ENDIAN16(urq.h);

    if( !rfb_write(&urq, sz_rfbEnableContinuousUpdatesMsg) )
    {
        return 0;
    }

    return 1;
}

// tell the vnc server to update us
int rfb_update_request(int incr)
{
    rfbFramebufferUpdateRequestMsg urq = { 0 };

    urq.type = rfbFramebufferUpdateRequest;
    urq.incremental = incr;
    urq.x = 0;
    urq.y = 0;
    urq.w = vnc.server.width;
    urq.h = vnc.server.height;
    urq.x = ENDIAN16(urq.x);
    urq.y = ENDIAN16(urq.y);
    urq.w = ENDIAN16(urq.w);
    urq.h = ENDIAN16(urq.h);

    if( !rfb_write(&urq, sz_rfbFramebufferUpdateRequestMsg) ) 
    {
        return 0;
    }

    fprintf(stdout, "successfully connected to vnc server.\n");

    return 1;
}

int rfb_disconnect(void)
{
    fprintf(stdout, "shutting down socket.\n");
    close(vnc.sock);
    return 1;
}

int rfb_cut_text_message(rfbServerToClientMsg *msg) {
    CARD32 size;
    char *buf;

    if( !rfb_read(((char*)&msg->sct) + 1, sz_rfbServerCutTextMsg - 1) )
    {
        return 0;
    }
    size = ENDIAN32(msg->sct.length);

    buf = (char*)malloc((sizeof(char) * size) + 1);
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

int rfb_enc_copy_rect(rfbFramebufferUpdateRectHeader rectheader) {
    int src_x, src_y;

    if( !rfb_read(&src_x, 2) )
    {
        return 0;
    }

    if( !rfb_read(&src_y, 2) )
    {
        return 0;
    }

    fprintf(stdout, "x: %d y: %d xx: %d yy: %d w: %d h: %d\n", ENDIAN16(src_x), ENDIAN16(src_y), rectheader.r.x, rectheader.r.y, rectheader.r.w, rectheader.r.h);
    return 1;
}
static uint8_t buf[4610 * 2160 * 4];
int rfb_enc_raw(rfbFramebufferUpdateRectHeader rectheader)
{
    uint32_t msgPixelTotal = rectheader.r.w * rectheader.r.h;
    uint32_t msgPixel = msgPixelTotal;
    uint32_t msgSize = msgPixel * (32 / 8); // bpp / components
    FILE *fd;

    fprintf(stdout, "x: %d y: %d w: %d h: %d\n", rectheader.r.x, rectheader.r.y, rectheader.r.w, rectheader.r.h);
    fprintf(stdout, "pixels: %d data size: %d\n", msgPixel, msgSize);

    while( msgPixelTotal ) {
        fprintf(stdout, "pixel lefts: %d\n", msgPixelTotal);

        if( msgPixelTotal < msgPixel ) {
            msgPixel = msgPixelTotal;
            msgSize = (msgPixel * (32 / 8));
        }

        if(!rfb_read(buf, msgSize)) {
            return 0;
        }

        msgPixelTotal -= msgPixel;
    }

    fd = fopen("test.bin","wb");
    fwrite(buf, msgSize, 1, fd);
    fclose(fd);
    
    fprintf(stdout, "wrote frame.\n");

    return 1;
}

// handles incomming messages from the vnc server
// this is where the dma operations should take place
int rfb_handle_message(void)
{
    int count;

    ioctl(vnc.sock, FIONREAD, &count);
    if( count > 0 )
    {
        rfbFramebufferUpdateRectHeader rectheader = { 0 };
        rfbServerToClientMsg msg = { 0 };
        uint16_t i;

        if( !rfb_read(&msg, 1) )
        {
            return 0;
        }

        switch( msg.type )
        {
            case rfbFramebufferUpdate:
                rfb_read(((char*)&msg.fu) + 1, sz_rfbFramebufferUpdateMsg - 1);
                msg.fu.nRects = ENDIAN16(msg.fu.nRects);
                for( i = 0; i < msg.fu.nRects; i++ )
                {
                    int result = 0;
                    rfb_read(&rectheader, sz_rfbFramebufferUpdateRectHeader);

                    rectheader.r.x = ENDIAN16(rectheader.r.x);
                    rectheader.r.y = ENDIAN16(rectheader.r.y);
                    rectheader.r.w = ENDIAN16(rectheader.r.w);
                    rectheader.r.h = ENDIAN16(rectheader.r.h);
                    rectheader.encoding = ENDIAN32(rectheader.encoding);

                    switch( rectheader.encoding )
                    {
                        case rfbEncodingRaw:
                            result = rfb_enc_raw(rectheader);
                            break;
                        case rfbEncodingCopyRect:
                            result = rfb_enc_copy_rect(rectheader);
                            break;
                        case rfbEncodingLastRect:
                            result = 1;
                            break;
                        default:
                            break;
                    }

                    if( !result )
                    {
                        fprintf(stdout, "encoding failed: 0x%08X\n", rectheader.encoding);
                        rfb_disconnect();
                        return 0;
                    }
                }
                break;
            case rfbSetColourMapEntries:
                rfb_read(((char*)&msg.scme) + 1, sz_rfbSetColourMapEntriesMsg - 1);
                break;
            case rfbBell:
                break;
            case rfbServerCutText:
                if( !rfb_cut_text_message(&msg) )
                {
                    rfb_disconnect();
                    return 0;
                }
                break;
            default:
                fprintf(stdout, "encoding failed.\n");
                rfb_disconnect();
                return 0;
        }
    }
    return 1;
}

// connects to the hosted socket addresses
// returns the socket file descriptor
int rfb_connect(const char *ip, unsigned port)
{
    struct sockaddr_in serv_addr;
    int one = 1;

    fprintf(stdout, "attempting to start socket... ");
    if( (vnc.sock = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
    {
        printf("error.\n");
        return 0;
    }
    fprintf(stdout, "complete.\n");
    fprintf(stdout, "attempting to connect to %s:%d... ", ip, port);

    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if( inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0 ) 
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

    // configure the socket for speedz
    if( setsockopt(vnc.sock, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one)) < 0 )
    {
        fprintf(stdout, "error.");
        rfb_disconnect();
        return 0;
    }
    fprintf(stdout, "complete.\n");

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

    while( 1 )
    {
        if(!rfb_handle_message())
        {
            return 0;
        }
        for( int i = 0; i < 200000000; i++ );
        fprintf(stdout, "requesting...\n");
        if( !rfb_update_request(1) )
        {
            fprintf(stdout, "frame format error.");
            rfb_disconnect();
            return 0;
        }

    }

    return 1;
}