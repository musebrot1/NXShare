#pragma once
// Minimal QR Code generator — Version 1-3, ECC Level M, Byte mode
// Single-header, no dependencies. Renders as block art on Switch console.
// Supports URLs up to ~50 chars (Version 3 = 44 data bytes)

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

namespace QR {

// ── GF(256) arithmetic ───────────────────────────────────────────────────────
static uint8_t GF_EXP[512], GF_LOG[256];
static bool gfReady = false;
static void gfInit() {
    if (gfReady) return;
    uint8_t x = 1;
    for (int i = 0; i < 255; i++) {
        GF_EXP[i] = GF_EXP[i+255] = x;
        GF_LOG[x] = i;
        x = (uint8_t)(x<<1 ^ (x&0x80 ? 0x11D : 0));
    }
    gfReady = true;
}
static uint8_t gfMul(uint8_t a, uint8_t b) {
    return (!a||!b) ? 0 : GF_EXP[GF_LOG[a]+GF_LOG[b]];
}

// ── Reed-Solomon encoder ─────────────────────────────────────────────────────
static std::vector<uint8_t> rsEC(const std::vector<uint8_t>& data, int n) {
    gfInit();
    // Build generator polynomial
    std::vector<uint8_t> g(n+1, 0); g[0] = 1;
    for (int i = 0; i < n; i++) {
        uint8_t a = GF_EXP[i];
        for (int j = n; j > 0; j--)
            g[j] = (uint8_t)(gfMul(g[j],a) ^ g[j-1]);
        g[0] = gfMul(g[0], a);
    }
    // Divide
    std::vector<uint8_t> r(n, 0);
    for (uint8_t b : data) {
        uint8_t f = b ^ r[0];
        for (int i = 0; i < n-1; i++) r[i] = r[i+1] ^ gfMul(g[n-1-i], f);
        r[n-1] = gfMul(g[0], f);
    }
    return r;
}

// ── QR matrix ────────────────────────────────────────────────────────────────
struct Mat {
    int N;
    std::vector<uint8_t> mod, fn;
    Mat() : N(0) {}
    Mat(int n) : N(n), mod(n*n,0), fn(n*n,0) {}
    bool get(int x,int y)  const { return mod[y*N+x]; }
    bool isF(int x,int y)  const { return fn[y*N+x]; }
    void setM(int x,int y,bool v){ mod[y*N+x]=v; }
    void setF(int x,int y,bool v){ mod[y*N+x]=v; fn[y*N+x]=1; }
};

static void finder(Mat& m, int ox, int oy) {
    for (int dy=0;dy<=6;dy++) for (int dx=0;dx<=6;dx++) {
        bool v=dx==0||dx==6||dy==0||dy==6||(dx>=2&&dx<=4&&dy>=2&&dy<=4);
        int x=ox+dx,y=oy+dy;
        if(x>=0&&x<m.N&&y>=0&&y<m.N) m.setF(x,y,v);
    }
    // separator
    for (int i=-1;i<=7;i++) {
        auto sp=[&](int x,int y){ if(x>=0&&x<m.N&&y>=0&&y<m.N&&!m.isF(x,y)) m.setF(x,y,false); };
        sp(ox+i,oy-1); sp(ox-1,oy+i); sp(ox+7,oy+i); sp(ox+i,oy+7);
    }
}
static void align(Mat& m, int cx, int cy) {
    for (int dy=-2;dy<=2;dy++) for (int dx=-2;dx<=2;dx++)
        m.setF(cx+dx,cy+dy, abs(dx)==2||abs(dy)==2||(dx==0&&dy==0));
}
static void timing(Mat& m) {
    for (int i=8;i<m.N-8;i++) { m.setF(i,6,i%2==0); m.setF(6,i,i%2==0); }
}
// Format bits for ECC-M, masks 0-7
static const uint16_t FMT[8]={0x5412,0x5125,0x5E7C,0x5B4B,0x45F9,0x40CE,0x4F97,0x4AA0};
static void formatBits(Mat& m, int mask) {
    uint16_t bits = FMT[mask&7];
    for (int i=0;i<=5;i++) m.setF(8,i,(bits>>i)&1);
    m.setF(8,7,(bits>>6)&1); m.setF(8,8,(bits>>7)&1); m.setF(7,8,(bits>>8)&1);
    for (int i=9;i<=14;i++) m.setF(14-i,8,(bits>>i)&1);
    for (int i=0;i<=7;i++) m.setF(m.N-1-i,8,(bits>>i)&1);
    for (int i=8;i<=14;i++) m.setF(8,m.N-15+i,(bits>>i)&1);
    m.setF(8,m.N-8,true);
}
static bool maskFn(int mask,int x,int y) {
    switch(mask){
        case 0: return (x+y)%2==0;
        case 1: return y%2==0;
        case 2: return x%3==0;
        case 3: return (x+y)%3==0;
        case 4: return (y/2+x/3)%2==0;
        case 5: return (x*y)%2+(x*y)%3==0;
        case 6: return ((x*y)%2+(x*y)%3)%2==0;
        case 7: return ((x+y)%2+(x*y)%3)%2==0;
    }
    return false;
}

// Version info: size, total data bytes (ECC-M), EC bytes
struct Ver { int size, dataB, ecB; };
static const Ver VINFO[3]={{21,16,10},{25,28,16},{29,44,26}};

static bool build(const std::string& text, Mat& out) {
    int len = (int)text.size();
    int vi = -1;
    for (int i=0;i<3;i++) { if (4+8+8*len <= VINFO[i].dataB*8) { vi=i; break; } }
    if (vi<0) return false;
    const Ver& v = VINFO[vi];

    // Encode: mode(4)=0100, length(8), data bytes
    std::vector<bool> bits;
    auto pushBits=[&](int val,int n){ for(int i=n-1;i>=0;i--) bits.push_back((val>>i)&1); };
    pushBits(4,4); pushBits(len,8);
    for (char c : text) pushBits((uint8_t)c, 8);
    pushBits(0,4); // terminator
    while ((int)bits.size()%8) bits.push_back(false);
    std::vector<uint8_t> data;
    for (int i=0;i<(int)bits.size();i+=8) {
        uint8_t b=0; for(int j=0;j<8;j++) b=(b<<1)|bits[i+j];
        data.push_back(b);
    }
    static const uint8_t PAD[]={0xEC,0x11};
    for (int i=0;(int)data.size()<v.dataB;i++) data.push_back(PAD[i%2]);

    // EC
    auto ec = rsEC(data, v.ecB);
    std::vector<uint8_t> cw; cw.insert(cw.end(),data.begin(),data.end()); cw.insert(cw.end(),ec.begin(),ec.end());

    // Build matrix
    Mat m(v.size);
    finder(m,0,0); finder(m,m.N-7,0); finder(m,0,m.N-7);
    timing(m);
    if (vi>=1) align(m,m.N-7,m.N-7);
    formatBits(m,0);

    // Place data
    int bi=0, total=(int)cw.size()*8;
    for (int right=m.N-1;right>=1;right-=2) {
        if (right==6) right=5;
        for (int vert=0;vert<m.N;vert++) {
            for (int j=0;j<2;j++) {
                int x=right-j;
                bool up=((right+1)&2)==0;
                int y=up?(m.N-1-vert):vert;
                if (m.isF(x,y)) continue;
                bool bit=false;
                if (bi<total) { bit=(cw[bi/8]>>(7-bi%8))&1; bi++; }
                m.setM(x,y,bit);
            }
        }
    }

    // Choose best mask
    int bestMask=0, bestPen=0x7FFFFFFF;
    for (int mask=0;mask<8;mask++) {
        Mat tmp=m;
        for (int y=0;y<tmp.N;y++) for (int x=0;x<tmp.N;x++)
            if (!tmp.isF(x,y)&&maskFn(mask,x,y)) tmp.setM(x,y,!tmp.get(x,y));
        formatBits(tmp,mask);
        // Penalty rule 1 only (good enough for selection)
        int pen=0;
        for (int y=0;y<tmp.N;y++) {
            int run=1;
            for (int x=1;x<tmp.N;x++) {
                if (tmp.get(x,y)==tmp.get(x-1,y)){ run++; if(run==5)pen+=3; else if(run>5)pen++; }
                else run=1;
            }
        }
        if (pen<bestPen) { bestPen=pen; bestMask=mask; }
    }
    for (int y=0;y<m.N;y++) for (int x=0;x<m.N;x++)
        if (!m.isF(x,y)&&maskFn(bestMask,x,y)) m.setM(x,y,!m.get(x,y));
    formatBits(m,bestMask);

    out = m;
    return true;
}


} // namespace QR
