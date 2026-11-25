/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <string.h>

/* A development-monitor EDID, not an Apple or panel-vendor identity. It carries
 * one preferred native timing. AirPlay still negotiates the actual frame rate.
 * Width/height are pixels; physical dimensions are millimeters.
 */
static inline void ap_display_edid(uint8_t edid[128], uint16_t width, uint16_t height,
                                  uint16_t width_mm, uint16_t height_mm, uint8_t fps) {
    memset(edid,0,128);
    const uint8_t header[]={0,255,255,255,255,255,255,0};
    memcpy(edid,header,sizeof header);
    edid[8]=0x41;edid[9]=0xc4; /* PND: P4 Native Display development identity */
    edid[10]=1;edid[17]=36;edid[18]=1;edid[19]=4;
    edid[20]=0xa0; /* digital, 8-bit, no physical connector claimed */
    edid[21]=(uint8_t)((width_mm+5)/10);edid[22]=(uint8_t)((height_mm+5)/10);
    edid[23]=120;edid[24]=6; /* gamma 2.2, sRGB, preferred timing */
    /* sRGB chromaticities, encoded as 10-bit fractions. */
    const uint16_t c[]={655,338,307,614,154,61,320,337};
    edid[25]=(uint8_t)(((c[0]&3)<<6)|((c[1]&3)<<4)|((c[2]&3)<<2)|(c[3]&3));
    edid[26]=(uint8_t)(((c[4]&3)<<6)|((c[5]&3)<<4)|((c[6]&3)<<2)|(c[7]&3));
    for(unsigned i=0;i<8;i++)edid[27+i]=(uint8_t)(c[i]>>2);
    for(unsigned i=38;i<54;i++)edid[i]=1; /* unused standard timings */
    uint8_t *d=edid+54;
    const unsigned hb=80,vb=30;
    unsigned clock=((unsigned)(width+hb)*(height+vb)*fps+5000)/10000;
    d[0]=(uint8_t)clock;d[1]=(uint8_t)(clock>>8);
    d[2]=(uint8_t)width;d[3]=hb;d[4]=(uint8_t)(((width>>8)<<4)|(hb>>8));
    d[5]=(uint8_t)height;d[6]=vb;d[7]=(uint8_t)(((height>>8)<<4)|(vb>>8));
    d[8]=24;d[9]=16;d[10]=0x35;
    d[12]=(uint8_t)width_mm;d[13]=(uint8_t)height_mm;
    d[14]=(uint8_t)(((width_mm>>8)<<4)|(height_mm>>8));d[17]=0x1e;
    edid[75]=0xfc;
    memcpy(edid+77,"P4 Display\n  ",13);
    edid[93]=0xfd;
    edid[95]=fps;edid[96]=fps;
    unsigned khz=(unsigned)(height+vb)*fps;
    edid[97]=(uint8_t)(khz/1000);edid[98]=(uint8_t)((khz+999)/1000);
    edid[99]=(uint8_t)((clock+999)/1000);edid[100]=0;
    edid[101]=0x0a;memset(edid+102,0x20,6);
    edid[111]=0x10; /* unused descriptor */
    unsigned sum=0;for(unsigned i=0;i<127;i++)sum+=edid[i];
    edid[127]=(uint8_t)(0u-sum);
}
