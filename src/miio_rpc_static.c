/* miio_rpc - self-contained miIO local RPC client (MD5 + AES-128-CBC built in)
 * usage: miio_rpc <method> <params-json> [token-file]
 * prints JSON reply on stdout. Static, no external libraries.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>

/* ---------------- MD5 ---------------- */
typedef struct { uint32_t s[4]; uint64_t len; unsigned char b[64]; } MD5_CTX;

static const uint32_t md5_K[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
static const unsigned char md5_S[64] = {
    7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
    5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
    6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};

static uint32_t rotl32(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

static void md5_transform(uint32_t st[4], const unsigned char blk[64])
{
    uint32_t a = st[0], b = st[1], c = st[2], d = st[3], m[16], f, g, tmp;
    for (int i = 0; i < 16; i++)
        m[i] = (uint32_t)blk[i*4] | ((uint32_t)blk[i*4+1] << 8) | ((uint32_t)blk[i*4+2] << 16) | ((uint32_t)blk[i*4+3] << 24);
    for (int i = 0; i < 64; i++) {
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5*i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d; g = (3*i + 5) & 15; }
        else { f = c ^ (b | ~d); g = (7*i) & 15; }
        tmp = d; d = c; c = b;
        b = b + rotl32(a + f + md5_K[i] + m[g], md5_S[i]);
        a = tmp;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
}

static void md5_init(MD5_CTX *c) { c->s[0]=0x67452301; c->s[1]=0xefcdab89; c->s[2]=0x98badcfe; c->s[3]=0x10325476; c->len=0; }

static void md5_update(MD5_CTX *c, const void *data, size_t len)
{
    const unsigned char *p = data;
    size_t idx = (size_t)(c->len & 63), i = 0;
    c->len += len;
    if (idx) {
        size_t need = 64 - idx;
        if (len < need) { memcpy(c->b + idx, p, len); return; }
        memcpy(c->b + idx, p, need); md5_transform(c->s, c->b); i = need;
    }
    for (; i + 64 <= len; i += 64) md5_transform(c->s, p + i);
    if (i < len) memcpy(c->b, p + i, len - i);
}

static void md5_final(MD5_CTX *c, unsigned char out[16])
{
    uint64_t bits = c->len * 8;
    size_t idx = (size_t)(c->len & 63);
    unsigned char pad = 0x80;
    md5_update(c, &pad, 1);
    unsigned char z = 0;
    while ((c->len & 63) != 56) md5_update(c, &z, 1);
    unsigned char lenb[8];
    for (int i = 0; i < 8; i++) lenb[i] = (unsigned char)((bits >> (8*i)) & 0xff);
    md5_update(c, lenb, 8);
    for (int i = 0; i < 4; i++) {
        out[i*4] = c->s[i] & 0xff; out[i*4+1] = (c->s[i] >> 8) & 0xff;
        out[i*4+2] = (c->s[i] >> 16) & 0xff; out[i*4+3] = (c->s[i] >> 24) & 0xff;
    }
}

static void md5(const void *data, size_t len, unsigned char out[16])
{
    MD5_CTX c; md5_init(&c); md5_update(&c, data, len); md5_final(&c, out);
}

/* ---------------- AES-128 ---------------- */
static const unsigned char aes_sbox[256] = {
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};

static const unsigned char aes_rsbox[256] = {
0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d};

static const unsigned char aes_rcon[11] = {0x8d,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

static void aes_key_expand(const unsigned char key[16], unsigned char rk[176])
{
    memcpy(rk, key, 16);
    for (int i = 16; i < 176; i += 4) {
        unsigned char t[4] = {rk[i-4], rk[i-3], rk[i-2], rk[i-1]};
        if (i % 16 == 0) {
            unsigned char tmp = t[0];
            t[0] = aes_sbox[t[1]] ^ aes_rcon[i/16];
            t[1] = aes_sbox[t[2]];
            t[2] = aes_sbox[t[3]];
            t[3] = aes_sbox[tmp];
        }
        for (int j = 0; j < 4; j++) rk[i+j] = rk[i-16+j] ^ t[j];
    }
}

static void add_round_key(unsigned char s[16], const unsigned char *rk) { for (int i = 0; i < 16; i++) s[i] ^= rk[i]; }

static void sub_bytes(unsigned char s[16]) { for (int i = 0; i < 16; i++) s[i] = aes_sbox[s[i]]; }
static void inv_sub_bytes(unsigned char s[16]) { for (int i = 0; i < 16; i++) s[i] = aes_rsbox[s[i]]; }

static void shift_rows(unsigned char s[16])
{
    unsigned char t;
    t = s[1]; s[1]=s[5]; s[5]=s[9]; s[9]=s[13]; s[13]=t;
    t = s[2]; s[2]=s[10]; s[10]=t; t = s[6]; s[6]=s[14]; s[14]=t;
    t = s[15]; s[15]=s[11]; s[11]=s[7]; s[7]=s[3]; s[3]=t;
}
static void inv_shift_rows(unsigned char s[16])
{
    unsigned char t;
    t = s[13]; s[13]=s[9]; s[9]=s[5]; s[5]=s[1]; s[1]=t;
    t = s[2]; s[2]=s[10]; s[10]=t; t = s[6]; s[6]=s[14]; s[14]=t;
    t = s[3]; s[3]=s[7]; s[7]=s[11]; s[11]=s[15]; s[15]=t;
}

static unsigned char gmul(unsigned char a, unsigned char b)
{
    unsigned char p = 0;
    for (int i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        unsigned char hi = a & 0x80;
        a <<= 1;
        if (hi) a ^= 0x1b;
        b >>= 1;
    }
    return p;
}

static void mix_columns(unsigned char s[16])
{
    for (int c = 0; c < 4; c++) {
        unsigned char *p = s + c*4;
        unsigned char a0=p[0],a1=p[1],a2=p[2],a3=p[3];
        p[0] = gmul(a0,2) ^ gmul(a1,3) ^ a2 ^ a3;
        p[1] = a0 ^ gmul(a1,2) ^ gmul(a2,3) ^ a3;
        p[2] = a0 ^ a1 ^ gmul(a2,2) ^ gmul(a3,3);
        p[3] = gmul(a0,3) ^ a1 ^ a2 ^ gmul(a3,2);
    }
}

static void inv_mix_columns(unsigned char s[16])
{
    for (int c = 0; c < 4; c++) {
        unsigned char *p = s + c*4;
        unsigned char a0=p[0],a1=p[1],a2=p[2],a3=p[3];
        p[0] = gmul(a0,14) ^ gmul(a1,11) ^ gmul(a2,13) ^ gmul(a3,9);
        p[1] = gmul(a0,9) ^ gmul(a1,14) ^ gmul(a2,11) ^ gmul(a3,13);
        p[2] = gmul(a0,13) ^ gmul(a1,9) ^ gmul(a2,14) ^ gmul(a3,11);
        p[3] = gmul(a0,11) ^ gmul(a1,13) ^ gmul(a2,9) ^ gmul(a3,14);
    }
}

static void aes_encrypt_block(const unsigned char rk[176], const unsigned char in[16], unsigned char out[16])
{
    unsigned char s[16];
    memcpy(s, in, 16);
    add_round_key(s, rk);
    for (int r = 1; r <= 9; r++) { sub_bytes(s); shift_rows(s); mix_columns(s); add_round_key(s, rk + r*16); }
    sub_bytes(s); shift_rows(s); add_round_key(s, rk + 160);
    memcpy(out, s, 16);
}

static void aes_decrypt_block(const unsigned char rk[176], const unsigned char in[16], unsigned char out[16])
{
    unsigned char s[16];
    memcpy(s, in, 16);
    add_round_key(s, rk + 160);
    for (int r = 9; r >= 1; r--) { inv_shift_rows(s); inv_sub_bytes(s); add_round_key(s, rk + r*16); inv_mix_columns(s); }
    inv_shift_rows(s); inv_sub_bytes(s); add_round_key(s, rk);
    memcpy(out, s, 16);
}

static int aes_cbc(int enc, const unsigned char key[16], const unsigned char iv[16],
                   const unsigned char *in, int inlen, unsigned char *out)
{
    unsigned char rk[176], prev[16], blk[16];
    int i;
    if (inlen % 16) return -1;
    aes_key_expand(key, rk);
    memcpy(prev, iv, 16);
    for (i = 0; i < inlen; i += 16) {
        if (enc) {
            for (int j = 0; j < 16; j++) blk[j] = in[i+j] ^ prev[j];
            aes_encrypt_block(rk, blk, out + i);
            memcpy(prev, out + i, 16);
        } else {
            aes_decrypt_block(rk, in + i, blk);
            for (int j = 0; j < 16; j++) out[i+j] = blk[j] ^ prev[j];
            memcpy(prev, in + i, 16);
        }
    }
    return inlen;
}

static int read_token(const char *path, unsigned char tok[16])
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fread(tok, 1, 16, f) != 16) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

static int udp_xchg(const unsigned char *pkt, int plen, unsigned char *rep, int rmax, int timeout_ms)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0), n;
    struct sockaddr_in sa;
    struct timeval tv;
    const char *host = getenv("MIIO_HOST");
    static char _auto_ip[32];
    if (!host || !*host) {
        struct ifaddrs *ifa, *it;
        host = NULL;
        if (getifaddrs(&ifa) == 0) {
            for (it = ifa; it; it = it->ifa_next) {
                if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
                if (!(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK)) continue;
                struct sockaddr_in *sin = (struct sockaddr_in *)it->ifa_addr;
                inet_ntop(AF_INET, &sin->sin_addr, _auto_ip, sizeof(_auto_ip));
                host = _auto_ip;
                break;
            }
            freeifaddrs(ifa);
        }
        if (!host) host = "127.0.0.1";
    }
    if (fd < 0) return -1;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(54321);
    sa.sin_addr.s_addr = inet_addr(host);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    {
        int sr = sendto(fd, pkt, plen, 0, (struct sockaddr *)&sa, sizeof(sa));
        if (sr != plen) { close(fd); return -1; }
    }
    n = recv(fd, rep, rmax, 0);
    close(fd);
    return n;
}

int main(int argc, char **argv)
{
    unsigned char token[16], key[16], iv[16], hello[32], rep[8192];
    unsigned char body[4096], ct[4096], pkt[8192], plain[8192];
    char bodytxt[4096];
    const char *method, *params, *tokfile;
    int n, blen, ctlen, clen, pad;
    uint32_t stamp;

    if (argc < 3) { fprintf(stderr, "usage: %s <method> <params-json> [token-file]\n", argv[0]); return 2; }
    method = argv[1];
    params = argv[2];
    tokfile = (argc > 3) ? argv[3] : getenv("MIIO_TOKEN_FILE");
    if (!tokfile) tokfile = "/mnt/sdcard/hacks/etc/miio.token";
    if (read_token(tokfile, token) != 0) { fprintf(stderr, "token file error: %s\n", tokfile); return 3; }

    memset(hello, 0xff, sizeof(hello));
    hello[0] = 0x21; hello[1] = 0x31; hello[2] = 0x00; hello[3] = 0x20;
    n = udp_xchg(hello, 32, rep, sizeof(rep), 2000);
    if (n < 32 || rep[0] != 0x21 || rep[1] != 0x31) { fprintf(stderr, "hello failed\n"); return 4; }
    stamp = ((uint32_t)rep[12] << 24) | ((uint32_t)rep[13] << 16) | ((uint32_t)rep[14] << 8) | rep[15];

    md5(token, 16, key);
    {
        unsigned char buf[32];
        memcpy(buf, key, 16); memcpy(buf + 16, token, 16);
        md5(buf, 32, iv);
    }

    snprintf(bodytxt, sizeof(bodytxt), "{\"id\":1,\"method\":\"%s\",\"params\":%s}", method, params);
    blen = strlen(bodytxt);
    pad = 16 - (blen % 16);
    memcpy(body, bodytxt, blen);
    memset(body + blen, pad, pad);
    ctlen = aes_cbc(1, key, iv, body, blen + pad, ct);
    if (ctlen < 0) { fprintf(stderr, "encrypt failed\n"); return 5; }

    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x21; pkt[1] = 0x31;
    clen = 32 + ctlen;
    pkt[2] = (clen >> 8) & 0xff; pkt[3] = clen & 0xff;
    memcpy(pkt + 4, rep + 4, 8);
    stamp++;
    pkt[12] = (stamp >> 24) & 0xff; pkt[13] = (stamp >> 16) & 0xff;
    pkt[14] = (stamp >> 8) & 0xff; pkt[15] = stamp & 0xff;
    memcpy(pkt + 16, token, 16);
    memcpy(pkt + 32, ct, ctlen);
    {
        unsigned char cs[16];
        md5(pkt, clen, cs);
        memcpy(pkt + 16, cs, 16);
    }

    n = udp_xchg(pkt, clen, rep, sizeof(rep), 3000);
    if (n < 32) { fprintf(stderr, "no reply\n"); return 6; }
    {
        int rlen = n - 32;
        int dlen = aes_cbc(0, key, iv, rep + 32, rlen, plain);
        if (dlen < 0) { fprintf(stderr, "decrypt failed\n"); return 7; }
        pad = plain[dlen - 1];
        if (pad < 1 || pad > 16) pad = 0;
        fwrite(plain, 1, dlen - pad, stdout);
        fputc('\n', stdout);
    }
    return 0;
}

