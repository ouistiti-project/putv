#ifndef __RTP_H__
#define __RTP_H__

struct rtpbits {
    int     seqnum:16;     // sequence number: random
    int     pt:7;            // payload type: 14 for MPEG audio
    int     m:1;             // marker: 0
    int     cc:4;            // number of CSRC identifiers: 0
    int     x:1;             // number of extension headers: 0
    int     p:1;             // is there padding appended: 0
    int     v:2;             // version: 2
};

struct rtpheader_s {           // in network byte order
    struct rtpbits b;
    int     timestamp;       // start: random
    int     ssrc;            // random
    int     iAudioHeader;    // =0?!
};

typedef struct rtpheader_s rtpheader_t;
#endif
