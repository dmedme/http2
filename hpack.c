/*
 * hpack implementation
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 2014";
#include <stdio.h>
#include <stdlib.h>
#include "webdrive.h"
#include "hashlib.h"
#ifndef MINGW32
#include "e2net.h"
#endif
#include "hpack.h"
static void lookup_index();
static struct huffman_code {
    int val;
    unsigned int ones;
    unsigned int bits;
    int bitlen;
} huffman_codes[] = {
{ /* 0 1111111111000 */ 0, 10, 0x1ff8,13},
{ /* 1 11111111111111111011000 */ 1, 17, 0x7fffd8,23},
{ /* 2 1111111111111111111111100010 */ 2, 23, 0xfffffe2,28},
{ /* 3 1111111111111111111111100011 */ 3, 23, 0xfffffe3,28},
{ /* 4 1111111111111111111111100100 */ 4, 23, 0xfffffe4,28},
{ /* 5 1111111111111111111111100101 */ 5, 23, 0xfffffe5,28},
{ /* 6 1111111111111111111111100110 */ 6, 23, 0xfffffe6,28},
{ /* 7 1111111111111111111111100111 */ 7, 23, 0xfffffe7,28},
{ /* 8 1111111111111111111111101000 */ 8, 23, 0xfffffe8,28},
{ /* 9 111111111111111111101010 */ 9, 19, 0xffffea,24},
{ /* 10 111111111111111111111111111100 */ 10, 28, 0x3ffffffc,30},
{ /* 11 1111111111111111111111101001 */ 11, 23, 0xfffffe9,28},
{ /* 12 1111111111111111111111101010 */ 12, 23, 0xfffffea,28},
{ /* 13 111111111111111111111111111101 */ 13, 28, 0x3ffffffd,30},
{ /* 14 1111111111111111111111101011 */ 14, 23, 0xfffffeb,28},
{ /* 15 1111111111111111111111101100 */ 15, 23, 0xfffffec,28},
{ /* 16 1111111111111111111111101101 */ 16, 23, 0xfffffed,28},
{ /* 17 1111111111111111111111101110 */ 17, 23, 0xfffffee,28},
{ /* 18 1111111111111111111111101111 */ 18, 23, 0xfffffef,28},
{ /* 19 1111111111111111111111110000 */ 19, 24, 0xffffff0,28},
{ /* 20 1111111111111111111111110001 */ 20, 24, 0xffffff1,28},
{ /* 21 1111111111111111111111110010 */ 21, 24, 0xffffff2,28},
{ /* 22 111111111111111111111111111110 */ 22, 29, 0x3ffffffe,30},
{ /* 23 1111111111111111111111110011 */ 23, 24, 0xffffff3,28},
{ /* 24 1111111111111111111111110100 */ 24, 24, 0xffffff4,28},
{ /* 25 1111111111111111111111110101 */ 25, 24, 0xffffff5,28},
{ /* 26 1111111111111111111111110110 */ 26, 24, 0xffffff6,28},
{ /* 27 1111111111111111111111110111 */ 27, 24, 0xffffff7,28},
{ /* 28 1111111111111111111111111000 */ 28, 25, 0xffffff8,28},
{ /* 29 1111111111111111111111111001 */ 29, 25, 0xffffff9,28},
{ /* 30 1111111111111111111111111010 */ 30, 25, 0xffffffa,28},
{ /* 31 1111111111111111111111111011 */ 31, 25, 0xffffffb,28},
{ /* 32 010100 */ 32, 0, 0x14,6},
{ /* ! 1111111000 */ 33, 7, 0x3f8,10},
{ /* " 1111111001 */ 34, 7, 0x3f9,10},
{ /* # 111111111010 */ 35, 9, 0xffa,12},
{ /* $ 1111111111001 */ 36, 10, 0x1ff9,13},
{ /* % 010101 */ 37, 0, 0x15,6},
{ /* & 11111000 */ 38, 5, 0xf8,8},
{ /* ' 11111111010 */ 39, 8, 0x7fa,11},
{ /* ( 1111111010 */ 40, 7, 0x3fa,10},
{ /* ) 1111111011 */ 41, 7, 0x3fb,10},
{ /* * 11111001 */ 42, 5, 0xf9,8},
{ /* + 11111111011 */ 43, 8, 0x7fb,11},
{ /* , 11111010 */ 44, 5, 0xfa,8},
{ /* - 010110 */ 45, 0, 0x16,6},
{ /* . 010111 */ 46, 0, 0x17,6},
{ /* / 011000 */ 47, 0, 0x18,6},
{ /* 0 00000 */ 48, 0, 0x0,5},
{ /* 1 00001 */ 49, 0, 0x1,5},
{ /* 2 00010 */ 50, 0, 0x2,5},
{ /* 3 011001 */ 51, 0, 0x19,6},
{ /* 4 011010 */ 52, 0, 0x1a,6},
{ /* 5 011011 */ 53, 0, 0x1b,6},
{ /* 6 011100 */ 54, 0, 0x1c,6},
{ /* 7 011101 */ 55, 0, 0x1d,6},
{ /* 8 011110 */ 56, 0, 0x1e,6},
{ /* 9 011111 */ 57, 0, 0x1f,6},
{ /* : 1011100 */ 58, 1, 0x5c,7},
{ /* ; 11111011 */ 59, 5, 0xfb,8},
{ /* < 111111111111100 */ 60, 13, 0x7ffc,15},
{ /* = 100000 */ 61, 1, 0x20,6},
{ /* > 111111111011 */ 62, 9, 0xffb,12},
{ /* ? 1111111100 */ 63, 8, 0x3fc,10},
{ /* @ 1111111111010 */ 64, 10, 0x1ffa,13},
{ /* A 100001 */ 65, 1, 0x21,6},
{ /* B 1011101 */ 66, 1, 0x5d,7},
{ /* C 1011110 */ 67, 1, 0x5e,7},
{ /* D 1011111 */ 68, 1, 0x5f,7},
{ /* E 1100000 */ 69, 2, 0x60,7},
{ /* F 1100001 */ 70, 2, 0x61,7},
{ /* G 1100010 */ 71, 2, 0x62,7},
{ /* H 1100011 */ 72, 2, 0x63,7},
{ /* I 1100100 */ 73, 2, 0x64,7},
{ /* J 1100101 */ 74, 2, 0x65,7},
{ /* K 1100110 */ 75, 2, 0x66,7},
{ /* L 1100111 */ 76, 2, 0x67,7},
{ /* M 1101000 */ 77, 2, 0x68,7},
{ /* N 1101001 */ 78, 2, 0x69,7},
{ /* O 1101010 */ 79, 2, 0x6a,7},
{ /* P 1101011 */ 80, 2, 0x6b,7},
{ /* Q 1101100 */ 81, 2, 0x6c,7},
{ /* R 1101101 */ 82, 2, 0x6d,7},
{ /* S 1101110 */ 83, 2, 0x6e,7},
{ /* T 1101111 */ 84, 2, 0x6f,7},
{ /* U 1110000 */ 85, 3, 0x70,7},
{ /* V 1110001 */ 86, 3, 0x71,7},
{ /* W 1110010 */ 87, 3, 0x72,7},
{ /* X 11111100 */ 88, 6, 0xfc,8},
{ /* Y 1110011 */ 89, 3, 0x73,7},
{ /* Z 11111101 */ 90, 6, 0xfd,8},
{ /* [ 1111111111011 */ 91, 10, 0x1ffb,13},
{ /* \ 1111111111111110000 */ 92, 15, 0x7fff0,19},
{ /* ] 1111111111100 */ 93, 11, 0x1ffc,13},
{ /* ^ 11111111111100 */ 94, 12, 0x3ffc,14},
{ /* _ 100010 */ 95, 1, 0x22,6},
{ /* ` 111111111111101 */ 96, 13, 0x7ffd,15},
{ /* a 00011 */ 97, 0, 0x3,5},
{ /* b 100011 */ 98, 1, 0x23,6},
{ /* c 00100 */ 99, 0, 0x4,5},
{ /* d 100100 */ 100, 1, 0x24,6},
{ /* e 00101 */ 101, 0, 0x5,5},
{ /* f 100101 */ 102, 1, 0x25,6},
{ /* g 100110 */ 103, 1, 0x26,6},
{ /* h 100111 */ 104, 1, 0x27,6},
{ /* i 00110 */ 105, 0, 0x6,5},
{ /* j 1110100 */ 106, 3, 0x74,7},
{ /* k 1110101 */ 107, 3, 0x75,7},
{ /* l 101000 */ 108, 1, 0x28,6},
{ /* m 101001 */ 109, 1, 0x29,6},
{ /* n 101010 */ 110, 1, 0x2a,6},
{ /* o 00111 */ 111, 0, 0x7,5},
{ /* p 101011 */ 112, 1, 0x2b,6},
{ /* q 1110110 */ 113, 3, 0x76,7},
{ /* r 101100 */ 114, 1, 0x2c,6},
{ /* s 01000 */ 115, 0, 0x8,5},
{ /* t 01001 */ 116, 0, 0x9,5},
{ /* u 101101 */ 117, 1, 0x2d,6},
{ /* v 1110111 */ 118, 3, 0x77,7},
{ /* w 1111000 */ 119, 4, 0x78,7},
{ /* x 1111001 */ 120, 4, 0x79,7},
{ /* y 1111010 */ 121, 4, 0x7a,7},
{ /* z 1111011 */ 122, 4, 0x7b,7},
{ /* { 111111111111110 */ 123, 14, 0x7ffe,15},
{ /* | 11111111100 */ 124, 9, 0x7fc,11},
{ /* } 11111111111101 */ 125, 12, 0x3ffd,14},
{ /* ~ 1111111111101 */ 126, 11, 0x1ffd,13},
{ /* 127 1111111111111111111111111100 */ 127, 26, 0xffffffc,28},
{ /* 128 11111111111111100110 */ 128, 15, 0xfffe6,20},
{ /* 129 1111111111111111010010 */ 129, 16, 0x3fffd2,22},
{ /* 130 11111111111111100111 */ 130, 15, 0xfffe7,20},
{ /* 131 11111111111111101000 */ 131, 15, 0xfffe8,20},
{ /* 132 1111111111111111010011 */ 132, 16, 0x3fffd3,22},
{ /* 133 1111111111111111010100 */ 133, 16, 0x3fffd4,22},
{ /* 134 1111111111111111010101 */ 134, 16, 0x3fffd5,22},
{ /* 135 11111111111111111011001 */ 135, 17, 0x7fffd9,23},
{ /* 136 1111111111111111010110 */ 136, 16, 0x3fffd6,22},
{ /* 137 11111111111111111011010 */ 137, 17, 0x7fffda,23},
{ /* 138 11111111111111111011011 */ 138, 17, 0x7fffdb,23},
{ /* 139 11111111111111111011100 */ 139, 17, 0x7fffdc,23},
{ /* 140 11111111111111111011101 */ 140, 17, 0x7fffdd,23},
{ /* 141 11111111111111111011110 */ 141, 17, 0x7fffde,23},
{ /* 142 111111111111111111101011 */ 142, 19, 0xffffeb,24},
{ /* 143 11111111111111111011111 */ 143, 17, 0x7fffdf,23},
{ /* 144 111111111111111111101100 */ 144, 19, 0xffffec,24},
{ /* 145 111111111111111111101101 */ 145, 19, 0xffffed,24},
{ /* 146 1111111111111111010111 */ 146, 16, 0x3fffd7,22},
{ /* 147 11111111111111111100000 */ 147, 18, 0x7fffe0,23},
{ /* 148 111111111111111111101110 */ 148, 19, 0xffffee,24},
{ /* 149 11111111111111111100001 */ 149, 18, 0x7fffe1,23},
{ /* 150 11111111111111111100010 */ 150, 18, 0x7fffe2,23},
{ /* 151 11111111111111111100011 */ 151, 18, 0x7fffe3,23},
{ /* 152 11111111111111111100100 */ 152, 18, 0x7fffe4,23},
{ /* 153 111111111111111011100 */ 153, 15, 0x1fffdc,21},
{ /* 154 1111111111111111011000 */ 154, 16, 0x3fffd8,22},
{ /* 155 11111111111111111100101 */ 155, 18, 0x7fffe5,23},
{ /* 156 1111111111111111011001 */ 156, 16, 0x3fffd9,22},
{ /* 157 11111111111111111100110 */ 157, 18, 0x7fffe6,23},
{ /* 158 11111111111111111100111 */ 158, 18, 0x7fffe7,23},
{ /* 159 111111111111111111101111 */ 159, 19, 0xffffef,24},
{ /* 160 1111111111111111011010 */ 160, 16, 0x3fffda,22},
{ /* 161 111111111111111011101 */ 161, 15, 0x1fffdd,21},
{ /* 162 11111111111111101001 */ 162, 15, 0xfffe9,20},
{ /* 163 1111111111111111011011 */ 163, 16, 0x3fffdb,22},
{ /* 164 1111111111111111011100 */ 164, 16, 0x3fffdc,22},
{ /* 165 11111111111111111101000 */ 165, 18, 0x7fffe8,23},
{ /* 166 11111111111111111101001 */ 166, 18, 0x7fffe9,23},
{ /* 167 111111111111111011110 */ 167, 15, 0x1fffde,21},
{ /* 168 11111111111111111101010 */ 168, 18, 0x7fffea,23},
{ /* 169 1111111111111111011101 */ 169, 16, 0x3fffdd,22},
{ /* 170 1111111111111111011110 */ 170, 16, 0x3fffde,22},
{ /* 171 111111111111111111110000 */ 171, 20, 0xfffff0,24},
{ /* 172 111111111111111011111 */ 172, 15, 0x1fffdf,21},
{ /* 173 1111111111111111011111 */ 173, 16, 0x3fffdf,22},
{ /* 174 11111111111111111101011 */ 174, 18, 0x7fffeb,23},
{ /* 175 11111111111111111101100 */ 175, 18, 0x7fffec,23},
{ /* 176 111111111111111100000 */ 176, 16, 0x1fffe0,21},
{ /* 177 111111111111111100001 */ 177, 16, 0x1fffe1,21},
{ /* 178 1111111111111111100000 */ 178, 17, 0x3fffe0,22},
{ /* 179 111111111111111100010 */ 179, 16, 0x1fffe2,21},
{ /* 180 11111111111111111101101 */ 180, 18, 0x7fffed,23},
{ /* 181 1111111111111111100001 */ 181, 17, 0x3fffe1,22},
{ /* 182 11111111111111111101110 */ 182, 18, 0x7fffee,23},
{ /* 183 11111111111111111101111 */ 183, 18, 0x7fffef,23},
{ /* 184 11111111111111101010 */ 184, 15, 0xfffea,20},
{ /* 185 1111111111111111100010 */ 185, 17, 0x3fffe2,22},
{ /* 186 1111111111111111100011 */ 186, 17, 0x3fffe3,22},
{ /* 187 1111111111111111100100 */ 187, 17, 0x3fffe4,22},
{ /* 188 11111111111111111110000 */ 188, 19, 0x7ffff0,23},
{ /* 189 1111111111111111100101 */ 189, 17, 0x3fffe5,22},
{ /* 190 1111111111111111100110 */ 190, 17, 0x3fffe6,22},
{ /* 191 11111111111111111110001 */ 191, 19, 0x7ffff1,23},
{ /* 192 11111111111111111111100000 */ 192, 21, 0x3ffffe0,26},
{ /* 193 11111111111111111111100001 */ 193, 21, 0x3ffffe1,26},
{ /* 194 11111111111111101011 */ 194, 15, 0xfffeb,20},
{ /* 195 1111111111111110001 */ 195, 15, 0x7fff1,19},
{ /* 196 1111111111111111100111 */ 196, 17, 0x3fffe7,22},
{ /* 197 11111111111111111110010 */ 197, 19, 0x7ffff2,23},
{ /* 198 1111111111111111101000 */ 198, 17, 0x3fffe8,22},
{ /* 199 1111111111111111111101100 */ 199, 20, 0x1ffffec,25},
{ /* 200 11111111111111111111100010 */ 200, 21, 0x3ffffe2,26},
{ /* 201 11111111111111111111100011 */ 201, 21, 0x3ffffe3,26},
{ /* 202 11111111111111111111100100 */ 202, 21, 0x3ffffe4,26},
{ /* 203 111111111111111111111011110 */ 203, 21, 0x7ffffde,27},
{ /* 204 111111111111111111111011111 */ 204, 21, 0x7ffffdf,27},
{ /* 205 11111111111111111111100101 */ 205, 21, 0x3ffffe5,26},
{ /* 206 111111111111111111110001 */ 206, 20, 0xfffff1,24},
{ /* 207 1111111111111111111101101 */ 207, 20, 0x1ffffed,25},
{ /* 208 1111111111111110010 */ 208, 15, 0x7fff2,19},
{ /* 209 111111111111111100011 */ 209, 16, 0x1fffe3,21},
{ /* 210 11111111111111111111100110 */ 210, 21, 0x3ffffe6,26},
{ /* 211 111111111111111111111100000 */ 211, 22, 0x7ffffe0,27},
{ /* 212 111111111111111111111100001 */ 212, 22, 0x7ffffe1,27},
{ /* 213 11111111111111111111100111 */ 213, 21, 0x3ffffe7,26},
{ /* 214 111111111111111111111100010 */ 214, 22, 0x7ffffe2,27},
{ /* 215 111111111111111111110010 */ 215, 20, 0xfffff2,24},
{ /* 216 111111111111111100100 */ 216, 16, 0x1fffe4,21},
{ /* 217 111111111111111100101 */ 217, 16, 0x1fffe5,21},
{ /* 218 11111111111111111111101000 */ 218, 21, 0x3ffffe8,26},
{ /* 219 11111111111111111111101001 */ 219, 21, 0x3ffffe9,26},
{ /* 220 1111111111111111111111111101 */ 220, 26, 0xffffffd,28},
{ /* 221 111111111111111111111100011 */ 221, 22, 0x7ffffe3,27},
{ /* 222 111111111111111111111100100 */ 222, 22, 0x7ffffe4,27},
{ /* 223 111111111111111111111100101 */ 223, 22, 0x7ffffe5,27},
{ /* 224 11111111111111101100 */ 224, 15, 0xfffec,20},
{ /* 225 111111111111111111110011 */ 225, 20, 0xfffff3,24},
{ /* 226 11111111111111101101 */ 226, 15, 0xfffed,20},
{ /* 227 111111111111111100110 */ 227, 16, 0x1fffe6,21},
{ /* 228 1111111111111111101001 */ 228, 17, 0x3fffe9,22},
{ /* 229 111111111111111100111 */ 229, 16, 0x1fffe7,21},
{ /* 230 111111111111111101000 */ 230, 16, 0x1fffe8,21},
{ /* 231 11111111111111111110011 */ 231, 19, 0x7ffff3,23},
{ /* 232 1111111111111111101010 */ 232, 17, 0x3fffea,22},
{ /* 233 1111111111111111101011 */ 233, 17, 0x3fffeb,22},
{ /* 234 1111111111111111111101110 */ 234, 20, 0x1ffffee,25},
{ /* 235 1111111111111111111101111 */ 235, 20, 0x1ffffef,25},
{ /* 236 111111111111111111110100 */ 236, 20, 0xfffff4,24},
{ /* 237 111111111111111111110101 */ 237, 20, 0xfffff5,24},
{ /* 238 11111111111111111111101010 */ 238, 21, 0x3ffffea,26},
{ /* 239 11111111111111111110100 */ 239, 19, 0x7ffff4,23},
{ /* 240 11111111111111111111101011 */ 240, 21, 0x3ffffeb,26},
{ /* 241 111111111111111111111100110 */ 241, 22, 0x7ffffe6,27},
{ /* 242 11111111111111111111101100 */ 242, 21, 0x3ffffec,26},
{ /* 243 11111111111111111111101101 */ 243, 21, 0x3ffffed,26},
{ /* 244 111111111111111111111100111 */ 244, 22, 0x7ffffe7,27},
{ /* 245 111111111111111111111101000 */ 245, 22, 0x7ffffe8,27},
{ /* 246 111111111111111111111101001 */ 246, 22, 0x7ffffe9,27},
{ /* 247 111111111111111111111101010 */ 247, 22, 0x7ffffea,27},
{ /* 248 111111111111111111111101011 */ 248, 22, 0x7ffffeb,27},
{ /* 249 1111111111111111111111111110 */ 249, 27, 0xffffffe,28},
{ /* 250 111111111111111111111101100 */ 250, 22, 0x7ffffec,27},
{ /* 251 111111111111111111111101101 */ 251, 22, 0x7ffffed,27},
{ /* 252 111111111111111111111101110 */ 252, 22, 0x7ffffee,27},
{ /* 253 111111111111111111111101111 */ 253, 22, 0x7ffffef,27},
{ /* 254 111111111111111111111110000 */ 254, 23, 0x7fffff0,27},
{ /* 255 11111111111111111111101110 */ 255, 21, 0x3ffffee,26},
{ /* EOS 111111111111111111111111111111 */ 256, 30, 0x3fffffff,30}
};
static struct huffman_code * huffman_decodes[257];
/*
 * Concatenate the strings to the entry when it is malloc()'ed
 */
struct comp_entry {
    unsigned char * label;
    unsigned char * value;
    unsigned int hlen;
    unsigned int vlen;
    struct circbuf * cbp; /* Supports hash table access */
    char ** backp;        /* Our pointer in the circular buffer */
};
/*
 * If these are added to the static table in this order, their numbers
 * will come out right.
 */
static struct comp_entry static_entries[] = {
{ /* 61 */ "www-authenticate", NULL },
{ /* 60 */ "via", NULL },
{ /* 59 */ "vary", NULL },
{ /* 58 */ "user-agent", NULL },
{ /* 57 */ "transfer-encoding", NULL },
{ /* 56 */ "strict-transport-security", NULL },
{ /* 55 */ "set-cookie", NULL },
{ /* 54 */ "server", NULL },
{ /* 53 */ "retry-after", NULL },
{ /* 52 */ "refresh", NULL },
{ /* 51 */ "referer", NULL },
{ /* 50 */ "range", NULL },
{ /* 49 */ "proxy-authorization", NULL },
{ /* 48 */ "proxy-authenticate", NULL },
{ /* 47 */ "max-forwards", NULL },
{ /* 46 */ "location", NULL },
{ /* 45 */ "link", NULL },
{ /* 44 */ "last-modified", NULL },
{ /* 43 */ "if-unmodified-since", NULL },
{ /* 42 */ "if-range", NULL },
{ /* 41 */ "if-none-match", NULL },
{ /* 40 */ "if-modified-since", NULL },
{ /* 39 */ "if-match", NULL },
{ /* 38 */ "host", NULL },
{ /* 37 */ "from", NULL },
{ /* 36 */ "expires", NULL },
{ /* 35 */ "expect", NULL },
{ /* 34 */ "etag", NULL },
{ /* 33 */ "date", NULL },
{ /* 32 */ "cookie", NULL },
{ /* 31 */ "content-type", NULL },
{ /* 30 */ "content-range", NULL },
{ /* 29 */ "content-location", NULL },
{ /* 28 */ "content-length", NULL },
{ /* 27 */ "content-language", NULL },
{ /* 26 */ "content-encoding", NULL },
{ /* 25 */ "content-disposition", NULL },
{ /* 24 */ "cache-control", NULL },
{ /* 23 */ "authorization", NULL },
{ /* 22 */ "allow", NULL },
{ /* 21 */ "age", NULL },
{ /* 20 */ "access-control-allow-origin", NULL },
{ /* 19 */ "accept", NULL },
{ /* 18 */ "accept-ranges", NULL },
{ /* 17 */ "accept-language", NULL },
{ /* 16 */ "accept-encoding", "gzip, deflate" },
{ /* 15 */ "accept-charset", NULL },
{ /* 14 */ ":status", "500" },
{ /* 13 */ ":status", "404" },
{ /* 12 */ ":status", "400" },
{ /* 11 */ ":status", "304" },
{ /* 10 */ ":status", "206" },
{ /* 9 */ ":status", "204" },
{ /* 8 */ ":status", "200" },
{ /* 7 */ ":scheme", "https" },
{ /* 6 */ ":scheme", "http" },
{ /* 5 */ ":path", "/index.html" },
{ /* 4 */ ":path", "/" },
{ /* 3 */ ":method", "POST" },
{ /* 2 */ ":method", "GET" },
{ /* 1 */ ":authority", NULL }
};

/*
 * Comparison function for huffman_decode entries.
 */
static int dec_comp(row1, row2)
struct huffman_code* row1;
struct huffman_code* row2;
{
int i;
int ret;

    if (row1->ones < row2->ones)
        return -1;
    else
    if (row1->ones == row2->ones)
    {
        if (row1->bits < row2->bits)
            return -1;
        else
        if (row1->bits == row2->bits)
            return 0;
    }
    return 1;
}
/*
 * Look up the Huffman code based on the string of ones
 */
static __inline struct huffman_code ** find_loc(low, high, onelen, bits, cmpfn)
struct huffman_code ** low;
struct huffman_code ** high;
int onelen;
int bits;
int (*cmpfn)();                        /* Comparison function (eg. strcmp())  */
{
struct huffman_code **guess;
struct huffman_code match;
int i;

    match.ones = onelen;
    match.bits = bits;
    while (low < high)
    {
        guess = low + ((high - low) >> 1);
        i = cmpfn(*guess, &match);
        if (i == 0)
            return guess; 
        else
        if (i < 0)
            low = guess + 1;
        else
            high = guess - 1;
    }
    return low;
}
static struct circbuf * hhdcp_static_table;
/*
 * Initialise static data
 */
void ini_huffman_decodes()
{
int i;

    for (i = 0; i < 257; i++)
        huffman_decodes[i] = &huffman_codes[i];
    e2swork(&huffman_decodes[0], 257, dec_comp);
    hhdcp_static_table = circbuf_cre(62, NULL);
/*
 * Populate the static table
 */
    for (i = 0; i < 61; i++)
    {
        static_entries[i].cbp = hhdcp_static_table;
        static_entries[i].backp = hhdcp_static_table->head;
        (void) circbuf_add(hhdcp_static_table, &static_entries[i]);
        static_entries[i].hlen = strlen(static_entries[i].label);
        static_entries[i].vlen = (static_entries[i].value == NULL) ? 0
                                   : strlen(static_entries[i].value);
    }
    return;
}
/**********************************************************************
 * Hash the compression entries
 */
static long int cehash_key (utp,modulo)
struct comp_entry * utp;
int modulo;
{
long int x = 0;
unsigned long int y = 0;
unsigned char * p, *top;

    y = 0xffffffffffffffffUL;
    crc64_update(utp->label, utp->hlen, &y);
    if (utp->value != NULL)
        crc64_update(utp->value, utp->vlen, &y);
    y = ((y & 0xffffffff00000000UL) >> 32) ^ y;
    x = y % modulo;
    return x;
}
/*
 * Compare compression entries for match
 */
static int cecomp_key(utp1, utp2)
struct comp_entry  * utp1;
struct comp_entry * utp2;
{
int i;
int len;

    len = (utp1->hlen < utp2->hlen) ? utp1->hlen : utp2->hlen;
    i = memcmp(utp1->label, utp2->label, len);
    if (i)
        return i;
    if (utp1->hlen < utp2->hlen)
        return -1;
    else
    if (utp1->hlen > utp2->hlen)
        return 1;
    if (utp1->vlen == 0 && utp2->vlen == 0)
        return 0;
    else
    if (utp1->vlen == 0)
        return -1;
    else
    if (utp2->vlen == 0)
        return 1;
    len = (utp1->vlen < utp2->vlen) ? utp1->vlen : utp2->vlen;
    i = memcmp(utp1->value, utp2->value, len);
    if (i)
        return i;
    if (utp1->vlen < utp2->vlen)
        return -1;
    else
    if (utp1->vlen > utp2->vlen)
        return 1;
    else
        return 0;
}
static void casei_head(p, len)
char * p;
int len;
{
char * top;

    for (top = p + len; p < top;  p++)
        if (isupper(*p))
            *p = tolower(*p); 
    return;
}
/*
 * Function that counts and de-lineates the headers.
 */
int hpack_head_delineate(hp)
struct http_req_response * hp;
{
unsigned char * ncrp;
unsigned char * crp;
unsigned char * colp;
/*
 * Loop - delineate all the headers.
 */
    hp->status = (*(hp->head_start.element) == 'H'
                && *(hp->head_start.element + 1) == 'T'
                && *(hp->head_start.element + 2) == 'T'
                && *(hp->head_start.element + 3) == 'P')
                 ? 0 : -1;   /* Works for an incoming HTTP/1.1 */
    for (ncrp = hp->head_start.element, hp->element_cnt = 0;
            ncrp < (hp->head_start.element + hp->head_start.len)
               && ((crp = memchr(ncrp,'\r',
          hp->head_start.len + 1 - (ncrp - hp->head_start.element))) != NULL);
                ncrp = crp + 2)
    {
        if (hp->element_cnt > 63)
            return -1;
        hp->headings[hp->element_cnt].label.element = ncrp;
        if ((colp = memchr(ncrp, ':', (crp - ncrp))) != NULL)
        {
            hp->headings[hp->element_cnt].label.len = (colp - ncrp);
            if (hp->element_cnt > 0)
                casei_head( hp->headings[hp->element_cnt].label.element,
                        hp->headings[hp->element_cnt].label.len);
            for (hp->headings[hp->element_cnt].value.element = colp + 2,
                 hp->headings[hp->element_cnt].value.len = (crp - colp - 2);
                       hp->headings[hp->element_cnt].value.len > 0
                   &&  hp->headings[hp->element_cnt].value.element[0] == ' ';
                             hp->headings[hp->element_cnt].value.len--,
                             hp->headings[hp->element_cnt].value.element++);
        }
        else
        {
            hp->headings[hp->element_cnt].label.len = 0;
            hp->headings[hp->element_cnt].value.element = ncrp;
            hp->headings[hp->element_cnt].value.len = (crp - ncrp);
        }
        hp->element_cnt++;
    }
    return hp->element_cnt;
}
/*
 * Separate out a variable length unsigned integer. It ends up in
 * little-endian order in the output buffer
 */
static unsigned char * decode_hpack_int(bits, ibase, itop, obase, otop, plen)
int bits;
unsigned char * ibase;
unsigned char * itop;
unsigned char * obase;
unsigned char * otop;
unsigned int * plen;
{
unsigned char msk = ((1 << bits) - 1);
unsigned char mval;
unsigned char * iptr = ibase;
unsigned char * optr = obase;
int slice;

    if (msk == 0)
    {
        if (plen != NULL)
           *plen = 0;
        return ibase;
    }
/*
 * Integer fits
 */
    if ((mval = (*ibase & msk)) != msk)
    {
        if (plen != NULL)
           *plen = 1;
        *obase = mval;
        return ibase + 1;
    }
/*
 * Otherwise, copy the bits from the input to the output. slice is the
 * number of bits currently occupied in the output byte
 */
    for (iptr = ibase + 1, optr = obase, slice = 0, *plen = 0;
            iptr < itop && optr < otop;
                iptr++, slice = (slice == 0) ? 7 : (slice - 1))
    {
        if (slice != 0)
        {          /* Need to fill in 8 - slice high bits in current output */
            *optr |= (*iptr & (((1 << (8 - slice)) - 1))) << slice; 
            optr++;
            if (slice != 1)
            {
                (*plen)++;   /* If slice == 1, we have just filled the byte */
                *optr = (*iptr & 0x7f) >> (8 - slice);
            }
        }
        else
        {
            (*plen)++;
            *optr = (*iptr & 0x7f);
        }
/*
 * There are (slice - 1) bits left
 */
        if (!(*iptr & 0x80))
            break;
    }
/*
 * Add in the number from the first byte
 */
    for (optr = obase, otop = obase + *plen;
           optr < otop && msk > 0;
               optr++)
    {
        slice = *optr + msk;
        *optr = (slice & 0xff);
        msk = slice >> 8;
    } 
    return iptr + 1;
}
/*
 * Code up an integer in HPACK integer format.
 */
static unsigned char * code_hpack_int(bits, code, obase, otop)
int bits;
unsigned int code;
char * obase;
char * otop;
{
int lim = (1 << bits) - 1;

    *obase &= ~lim;
    if (code < lim)
        *obase++ |= (code & lim);
    else
    {
        *obase++ |= lim;
        *obase = 0;
        code -= lim;
        for (;;)
        {
            *obase |= (code & 0x7f);
            code >>= 7;
            if (code != 0)
            {
                *obase++ |= 0x80;
                *obase = 0;
            }
            else
            {
                obase++;
                break;
            }
        }
    }
    return obase;
}
/*
 * Lookup the next Huffman code in the bitstream. I suspect that code based on
 * senlex.c would be more performant, and more general as well.
 */
static int huff_lookup(pibase,pslice,itop)
unsigned char ** pibase;
int * pslice;
unsigned char * itop;
{
int slice = 7 - *pslice;
unsigned char * iptr = *pibase;
int ones = 0;
unsigned int code = 0;
int residue;
struct huffman_code ** hcpp;
unsigned int msk;

newbyte:
    if (iptr >= itop)
    {
        *pibase = iptr;
        *pslice = 0;
#ifdef DEBUG_FULL
        fputs("Empty so EOS!?\n", stderr);
#endif
        return 256;   /* End of String */
    }
    while (slice == 7 && iptr < itop && *iptr == 0xff && ones < 30)
    {
        ones += 8;
        iptr++;
    }
    if (ones >= 30)
    {
        if (slice != 7)
            iptr++;
        *pibase = iptr;
        *pslice = 0;
#ifdef DEBUG_FULL
        fputs("Seen EOS!?\n", stderr);
#endif
        return 256;   /* End of String */
    }
    for (msk = 1 << slice; ones < 30;)
    {
        if (msk & *iptr)
        {
            ones++;
            slice--;
            if (slice < 0)
            {
                slice = 7;
                iptr++;
                goto newbyte;
            }
            msk >>= 1;
        }
        else
            break;
    }
    if (ones >= 30)
        goto newbyte;
/*
 * Find the correct range of codes
 */
    hcpp = find_loc(&huffman_decodes[0],&huffman_decodes[256],ones, 0, dec_comp);
    while (hcpp < &huffman_decodes[257])
    {
        if ((*hcpp)->ones < ones)
            hcpp++;
        else
            break;
    }
    if (hcpp >= &huffman_decodes[257]
      || (*hcpp)->ones > ones)
    {
#ifdef DEBUG_FULL
        fprintf(stderr, "Screwed up; can't find ones == %u\n", ones);
#endif
        *pibase = iptr;
        *pslice = 0;
        return 256;
    }
/*
 * Now work out the code we are looking for.
 */
    code = (1 << ones) - 1;         /* The sequence of ones */
    residue = ((*hcpp)->bitlen - ones);      /* Bits to fetch */
    code <<= residue;               /* Shift to obtain template */
    residue--;                      /* We know the first is a zero */
retry:
    while (residue >= 0)
    {
        if (*iptr & (1 << slice))
            code |= (1 << residue);
        slice--;
        if (slice < 0)
        {
            iptr++;
            if (iptr >= itop && residue > 0)
            {
                *pibase = iptr;
                *pslice = 0;
#ifdef DEBUG_FULL
                fputs("Not seen EOS - tail!?\n", stderr);
#endif
                return 256;
            }
            slice = 7;
        }
        residue--;
    }
/*
 * Save where we have got to
 */
    *pibase = iptr;
    *pslice = 7 - slice;
    if ((*hcpp)->bits == code)
    {
#ifdef DEBUG_FULL
        fprintf(stderr, "Seen: (%u) %c\n",  (*hcpp)->val, (*hcpp)->val);
#endif
        return (*hcpp)->val;
    }
    hcpp = find_loc(hcpp, hcpp + ((1 << ((*hcpp)->bitlen - ones)) - 1),
                      ones, code, dec_comp);
    if ((*hcpp)->bits == code)
    {
#ifdef DEBUG_FULL
        fprintf(stderr, "Seen: (%u) %c\n",  (*hcpp)->val, (*hcpp)->val);
#endif
        return (*hcpp)->val;
    }
    if (iptr < itop)
    {
        residue = 0;
        code <<= 1;
        goto retry;
    }
#ifdef DEBUG_FULL
    fputs("Not seen EOS!?\n", stderr);
#endif
    return 256;
}
/*
 * Add the Huffman code for a byte to the output
 */ 
static int huff_gen(pobase, pslice, otop, code)
unsigned char ** pobase;
int * pslice;
unsigned char * otop;
unsigned int code;
{
struct huffman_code * hcp;
int slice;
int bitlen;
int to_do;
unsigned char msk;
unsigned char * optr;

    if (code > 256)
        return 0;
#ifdef DEBUG_FULL
    fprintf(stderr, "Added: (%u) %c\n",  code);
#endif
    optr = *pobase;
    slice = *pslice;
    if (slice == 0)
        *optr = 0;
    hcp = &huffman_codes[code];
    for (bitlen = hcp->bitlen; optr < otop && bitlen > 0;)
    {
        if (bitlen <= (8 - slice))
            to_do = bitlen;
        else
            to_do = (8 - slice);
        msk = hcp->bits >> (bitlen - to_do);
        if (to_do < (8 - slice))
            msk <<= ((8 - slice) - to_do);
        *optr |= msk;
        slice += to_do;
        bitlen -= to_do;
        if (slice >= 8)
        {
            slice = 0;
            optr++;
            if (code == 256)
            {
                *pobase = optr;
                *pslice = slice;
                return 1;
            }
            if (optr >= otop && bitlen > 0)
            {
                *pobase = otop;
                *pslice = 0;
                return 0;
            }
            *optr = 0;
        }
        *pobase = optr;
        *pslice = slice;
    }
    return 1;
}
/*
 * Code a string, that is optionally Huffman coded.
 */
static unsigned char * code_hpack_string(ibase, itop, obase, otop, plen, huff_flag)
unsigned char * ibase;
unsigned char * itop;
unsigned char * obase;
unsigned char * otop;
int * plen;
int huff_flag;
{
unsigned char * optr;
unsigned char * iptr;
unsigned char * resv;
int slice;

    if (!huff_flag)
    {
        *obase = 0;
        if (((optr = code_hpack_int(7, (itop - ibase), obase, otop))
               >= otop)
         || ((otop - optr) < (itop - ibase)))
        {
             *plen = (otop - obase);
             return itop;
        }
        memcpy(optr, ibase, (itop - ibase));
        *plen = (optr + (itop - ibase)) - obase;
    }
    else
    {
        *obase = 0x80;
        if ((optr = code_hpack_int(7, (itop - ibase), obase, otop))
               >= otop)
        {
             *plen = (otop - obase);
             return itop;
        }
        resv = optr;
        for (slice = 0, iptr = ibase; iptr < itop && optr < otop;)
        {
            if (!huff_gen(&optr, &slice, otop, *iptr++))
                break;
        }
/*
 * The terminator seems to be bogus. What it really says is 'fill the final
 * octet with ones'.
 */
        if (slice != 0)
            *optr |= ((1<<(8 - slice)) - 1);
        optr++;
        *plen = (optr - resv) - ((slice == 0) ? 1 : 0);
        if ((optr = code_hpack_int(7, *plen, obase, otop))
               != resv)
        {
            memmove(optr, resv, *plen);
            *plen += (optr - obase);
            if (resv < optr)  /* Corrupted */
            {
                for (slice = 0, otop = optr + (optr - resv), iptr = ibase;
                        iptr < itop && optr < otop;)
                {
                    huff_gen(&optr, &slice, otop, *iptr++);
                    if (slice == 0)
                        break;     /* Opportunity to break out early */
                }
            }
        }
        else
            *plen += (resv - obase);
    }
    return itop;
}
/*
 * Decode a string, that is optionally Huffman coded.
 */
static unsigned char * decode_hpack_string(ibase, itop, obase, otop, plen)
unsigned char * ibase;
unsigned char * itop;
unsigned char * obase;
unsigned char * otop;
unsigned int * plen;
{
unsigned char * iptr = ibase;
unsigned char * optr = obase;
union {
    unsigned char ibuf[8];
    unsigned long int li;
} idec;
unsigned int ilen;
unsigned int decode;
unsigned int slice;
/*
 * Find the length
 */
    idec.li = 0;
    *plen = 0;
    iptr = decode_hpack_int(7, iptr, itop, &idec.ibuf[0], &idec.ibuf[8], &ilen);
    if (ilen > sizeof(idec.li))
    {
        fprintf(stderr, "Integer %u long!? (%lx)\n", ilen,idec.li);
        iptr++;
        return iptr;
    }
    else
    if (iptr + idec.li > itop)
    {
        fprintf(stderr, "String length (%u) goes beyond input!?\n",
            idec.li);
        iptr++;
        return iptr;
    }
    if (*ibase & 0x80)
    {  /* Huffman coded */
        slice = 0;
        itop = iptr + idec.li;
        while (iptr < itop
               && optr < otop
               && (decode = huff_lookup(&iptr, &slice, itop)) != 256)
            *optr++ = decode;
        *plen = (optr - obase);
    }
    else
    if ((otop - obase) < idec.li)
    {
        fprintf(stderr, "Buffer overflow; string length %u space %u\n",
                  idec.li, (otop - obase));
        return itop;
    }
    else
    {
        memcpy(optr, iptr, idec.li); 
        *plen = idec.li;
        iptr += idec.li;
    }
    return iptr;
}
void zap_http2_head_decoding_context(hhdcp)
struct http2_head_decoding_context * hhdcp;
{
    circbuf_des(hhdcp->dynamic_table);
    free(hhdcp);
    return;
}
/*
 * Initialise a decoder
 */
struct http2_head_decoding_context * ini_http2_head_decoding_context (dyn_size)
int dyn_size;
{
struct http2_head_decoding_context * hhdcp;
int i;

    if ((hhdcp = (struct http2_head_decoding_context *) malloc(sizeof(* hhdcp)))
             == NULL)
        return NULL;
    hhdcp->static_size = 61;
    hhdcp->dynamic_size = dyn_size;  /* This is set from the protocol */
    hhdcp->dynamic_cnt = dyn_size;   /* This is varied in the stream */
    hhdcp->static_table = hhdcp_static_table;
    hhdcp->dynamic_table = circbuf_cre(hhdcp->dynamic_size/32 + 1, free);
    hhdcp->dynamic_used = 0;         /* Controls evictions ... */
    return hhdcp;
}

void zap_http2_head_coding_context(hhccp)
struct http2_head_coding_context * hhccp;
{
    zap_http2_head_decoding_context(hhccp->hhdcp);
    zap_http2_head_decoding_context(hhccp->debug);
    cleanup(hhccp->sym_table);
    free(hhccp);
    return;
}
static int comp_ind(cep)
struct comp_entry * cep;
{
int i = cep->cbp->head - (volatile char **) cep->backp;

    if (i < 1)
        i += (cep->cbp->top - cep->cbp->base);
    if (cep->cbp != hhdcp_static_table)
        i += 61;
    return i;
}
static void dump_comp_entry(cep)
struct comp_entry * cep;
{
int i = comp_ind(cep);

    fprintf(stderr, "(%d) %*s %*s\n", i, cep->hlen, cep->label,
                           cep->vlen, (cep->vlen == 0)? "" : cep->value);
    return;
}
void dump_dynamic(hhdcp)
struct http2_head_decoding_context * hhdcp;
{
char ** p;
int i;
struct comp_entry * cep;
int sz = 0;

    fprintf(stderr, "Dynamic Table Contents (%lx)\n", (long) hhdcp);
    for (p = hhdcp->dynamic_table->tail;
             p != hhdcp->dynamic_table->head;
                  p++, p = (p == hhdcp->dynamic_table->top) ?
                       hhdcp->dynamic_table->base : p)
        dump_comp_entry( (struct comp_entry *) (*p));
    fputs("==========\n", stderr);
    fprintf(stderr, "Dynamic Table By Index (%lx)\n", (long) hhdcp);
    for (i = 1; i <= hhdcp->dynamic_table->buf_cnt; i++)
    {
        lookup_index(hhdcp, &cep, i + 61);
        fprintf(stderr, "Index: i = %d ", i);
        dump_comp_entry( cep);
        sz += (32 + cep->hlen + cep->vlen);
    }
    fprintf(stderr, "==== Size: %d - Computed: %d ====\n",
                   hhdcp->dynamic_used, sz);
    return;
}
void dump_symbols(hhccp)
struct http2_head_coding_context * hhccp;
{
    fprintf(stderr, "Compression Symbols (%lx)\n", (long) hhccp);
    iterate(hhccp->sym_table, NULL, dump_comp_entry);
    fputs("==========\n", stderr);
    return;
}
struct http2_head_coding_context * ini_http2_head_coding_context (dyn_size)
int dyn_size;
{
struct http2_head_coding_context * hhccp;
int i;

    if ((hhccp = (struct http2_head_coding_context *) malloc(sizeof(* hhccp)))
             == NULL)
        return NULL;
    hhccp->hhdcp =  ini_http2_head_decoding_context (dyn_size);
    hhccp->debug =  ini_http2_head_decoding_context (dyn_size);
    hhccp->sym_table = hash(dyn_size + 61, cehash_key, cecomp_key);
    for (i = 0; i < 61; i++)
        (void) insert (hhccp->sym_table, (char *) &static_entries[i],
                       (char *) &static_entries[i]);
    return hhccp;
}
static void lookup_index(hhdcp, cepp, ind)
struct http2_head_decoding_context * hhdcp;
struct comp_entry ** cepp;
unsigned int ind;
{
    if (ind <= hhdcp->static_size)
        (void) circbuf_read(hhdcp->static_table, cepp, ind);
    else
    {
        ind -= hhdcp->static_size;
        (void) circbuf_read(hhdcp->dynamic_table, cepp, ind);
    }
    return;
}
static unsigned char * label_copy(optr, cep)
unsigned char * optr;
struct comp_entry * cep;
{
    if (cep->label != NULL)
    {
        memcpy(optr, cep->label, cep->hlen);
        optr += cep->hlen;
    }
    else
        fputs("Something wrong. No label, unexpectedly!\n",
                          stderr);
    *optr++ = ':';
    *optr++ = ' ';
    return optr;
}
static unsigned char * value_copy(optr, cep)
unsigned char * optr;
struct comp_entry * cep;
{
    if (cep->value != NULL)
    {
        memcpy(optr, cep->value, cep->vlen);
        optr += cep->vlen;
    }
    else
        fputs("Something wrong. No value, unexpectedly!\n",
                          stderr);
    *optr++ = '\r';
    *optr++ = '\n';
    return optr;
}
/*
 * Version used in the decompressor
 */
static void purge_dynamic_decomp(hhdcp)
struct http2_head_decoding_context * hhdcp;
{
struct comp_entry * cep;

    while (hhdcp->dynamic_used > hhdcp->dynamic_cnt)
    {
        circbuf_take(hhdcp->dynamic_table, &cep);
        hhdcp->dynamic_used -= (32 + cep->hlen + cep->vlen);
        free(cep);
    }
    return;
}
/*
 * Version used in the compressor
 */
static void purge_dynamic_comp(hhccp)
struct http2_head_coding_context * hhccp;
{
struct comp_entry * cep;

    while (hhccp->hhdcp->dynamic_used > hhccp->hhdcp->dynamic_cnt)
    {
        circbuf_take(hhccp->hhdcp->dynamic_table, &cep);
        hhccp->hhdcp->dynamic_used -= (32 + cep->hlen + cep->vlen);
        hremove (hhccp->sym_table, cep);
        free(cep);
    }
    return;
}
/*
 * Allocate a new compression/decompression entry. Common to both
 * compression and decompression.
 */
static struct comp_entry * ini_comp_entry(hhdcp, label, hlen, value, vlen)
struct http2_head_decoding_context * hhdcp;
unsigned char * label;
unsigned int hlen;
unsigned char * value;
unsigned int vlen;
{
struct comp_entry * cep;

    if ((cep = (struct comp_entry *) malloc(sizeof(struct comp_entry) + 2 + hlen + vlen + sizeof(unsigned long int))) == NULL)
        return NULL;
    cep->label = (unsigned char *) (cep + 1);
    cep->value = cep->label + hlen + 1;
    memcpy(cep->label, label, hlen);
    *(cep->label + hlen) = '\0';
    cep->hlen = hlen;
    memcpy(cep->value, value, vlen);
    *(cep->value + vlen) = '\0';
    cep->vlen = vlen;
/*
 * Now add it to the dynamic table
 */
    cep->cbp = hhdcp->dynamic_table;
    cep->backp = hhdcp->dynamic_table->head;
    circbuf_add(hhdcp->dynamic_table, cep);
    return cep;
}
/*
 * Create a new compression header value and add it to the dynamic table;
 * decompression case.
 */
static struct comp_entry * new_decomp_entry(hhdcp, label, hlen, value, vlen)
struct http2_head_decoding_context * hhdcp;
unsigned char * label;
unsigned int hlen;
unsigned char * value;
unsigned int vlen;
{
struct comp_entry * cep;
int n;
/*
 * Set up the new compression entry
 */
    if ((cep = ini_comp_entry(hhdcp, label, hlen, value, vlen)) == NULL)
        return NULL;
/*
 * Adjust the mad allocation control mechanism.
 */
    hhdcp->dynamic_used += 32 + cep->hlen + cep->vlen;
/*
 * Purge until the space is back under control
 */
    purge_dynamic_decomp(hhdcp);
/*
 * This is jolly dodgy if purge_dynamic_decomp() has deleted our entry ...
 * Probably safe with our malloc(), but not safe with GNU.
 */
    if (cep->hlen + cep->vlen > hhdcp->dynamic_used)
        return NULL;
    else
        return cep;
}
/*
 * Create a new compression header value and add it to the dynamic table
 * Compression case; must also maintain the hash table.
 */
static struct comp_entry * new_comp_entry(hhccp, label, hlen, value, vlen)
struct http2_head_coding_context * hhccp;
unsigned char * label;
unsigned int hlen;
unsigned char * value;
unsigned int vlen;
{
struct comp_entry * cep;
int n;
/*
 * Set up the new compression entry
 */
    if ((cep = ini_comp_entry(hhccp->hhdcp, label, hlen, value, vlen)) == NULL)
        return NULL;
/*
 * Add our entry to the symbol table.
 */
    (void) insert (hhccp->sym_table, (char *) cep, (char *) cep);
/*
 * Adjust the mad allocation control mechanism.
 */
    hhccp->hhdcp->dynamic_used += 32 + cep->hlen + cep->vlen;
/*
 * Purge until the space is back under control
 */
    purge_dynamic_comp(hhccp);
/*
 * This is jolly dodgy if purge_dynamic() has deleted our entry ... probably
 * safe with our malloc(), but not safe with GNU.
 */
    if (cep->hlen + cep->vlen > hhccp->hhdcp->dynamic_used)
        return NULL;
    else
        return cep;
}
/*
 * Read an index off the stream. An index of zero isn't an index; it signals
 * there is no index.
 */
static unsigned char * get_index(hhdcp, iptr, itop, bits, pli)
struct http2_head_decoding_context * hhdcp;
unsigned char * iptr;
unsigned char * itop;
int bits;
unsigned long int * pli;
{
union {
    unsigned char ibuf[8];
    unsigned long int li;
} idec;
int ilen;

    idec.li = 0;
    *pli = 0;
    iptr = decode_hpack_int(bits, iptr, itop,
                          &idec.ibuf[0], &idec.ibuf[8], &ilen);
    if (ilen > sizeof(idec.li))
        fprintf(stderr, "Integer %u long!? (%lx)\n", ilen,idec.li);
    else
    if (idec.li < 1
       || idec.li > (hhdcp->static_size + hhdcp->dynamic_table->buf_cnt))
    {
        if (idec.li > 0)
            fprintf(stderr, "Index %u out of range!? (1 - %u)\n",
                idec.li, (hhdcp->static_size + hhdcp->dynamic_table->buf_cnt));
    }
    else
        *pli = idec.li;
    return iptr;
}
/*
 * Unpack as much as possible of the input.
 *
 * Return the number of bytes output.
 *
 * In case we run out of output space, or we haven't got the whole thing, we
 * return how far we got in ibasep. 
 *
 * At the moment, the output is an HTTP2 set of headers. We need to recast the
 * stuff that corresponds to the first line, and merge multiple cookie headers,
 * before the data can be passed to the HTTP 1-aware t3drive code.
 */
int decode_head_stream(hhdcp, ibasep, itop, obase, otop)
struct http2_head_decoding_context * hhdcp;
unsigned char ** ibasep;
unsigned char * itop;
unsigned char * obase;
unsigned char * otop;
{
unsigned char * iptr = *ibasep;
unsigned char * optr = obase;
struct comp_entry * cep;
struct comp_entry ce;
unsigned long int ind;
int bits;

#ifdef DEBUG
    fputs("Decode Head Stream\n", stderr);
    fflush(stderr);
#endif
    while (iptr < itop && optr < otop)
    {
        if ((*iptr & 0x80))
        {             /* Header and value in table */
            iptr = get_index(hhdcp, iptr, itop, 7, &ind);
            lookup_index(hhdcp, &cep, ind);
            optr = label_copy(optr, cep);
            optr = value_copy(optr, cep);
        }
        else
        if ((*iptr & 0xe0) == 0x20)
        {   /* Re-size the dynamic table, evicting as necessary */
            iptr = get_index(hhdcp, iptr, itop, 5, &ind);
            if (ind > hhdcp->dynamic_size)
                fprintf(stderr, "Attempt to exceed the limit %u with %u\n",
                           hhdcp->dynamic_size, ind);
            else
            if (ind >= hhdcp->dynamic_cnt)
                hhdcp->dynamic_cnt = ind;
            else
            {
                hhdcp->dynamic_cnt = ind;
                purge_dynamic_decomp(hhdcp);
            }
        }
        else
        {
/*
 * Header Field without accompanying value in either table. The header can be an
 * index to just the header field in the table, or the literal header value.
 * The value is always a literal value.
 * -    In the first instance (bits == 6) the pair (header label plus value) is
 *      added to the (dynamic) table.
 * -    Otherwise we don't add. There are two sub-cases, 0000 and 0001, corresponding
 *      to an instruction for further hops to not compress, but we don't ever need
 *      to worry about the distinction.
 */
            if ((*iptr & 0xc0) == 0x40)
                bits = 6; /* Header may be in table; add header + value to table */
            else
            if ((*iptr & 0xe0) == 0)
                bits = 4; /* Don't add to the table */
            iptr = get_index(hhdcp, iptr, itop, bits, &ind);
            if (ind == 0)
            {
                ce.label = optr;
                iptr = decode_hpack_string(iptr, itop,
                        optr, otop, &ce.hlen);
                optr += ce.hlen;
                *optr++ = ':';
                *optr++ = ' ';
            }
            else
            {
                lookup_index(hhdcp, &cep, ind);
                optr = label_copy(optr, cep);
                ce.label = cep->label;
                ce.hlen = cep->hlen;
            }
            ce.value = optr;
            iptr = decode_hpack_string(iptr, itop,
                        optr, otop, &ce.vlen);
            optr += ce.vlen;
            *optr++ = '\r';
            *optr++ = '\n';
/*
 * Now we may need to add a compression entry
 */
            if (bits == 6)
               (void) new_decomp_entry(hhdcp,
                            ce.label, ce.hlen, ce.value, ce.vlen);
        }
    }
    *ibasep = iptr;
    return (optr - obase);
}
/*
 * Encode a header/value pair - don't bother if the value is empty.
 */
unsigned char * code_header(
struct http2_head_coding_context * hhccp,
unsigned char * label,
unsigned int hlen,
unsigned char * value,
unsigned int vlen,
unsigned char * obase,
unsigned char * otop)
{
struct comp_entry ce;
struct comp_entry * cep;
HIPT *hip;
int i;
unsigned int olen;
unsigned char * optr = obase;

    if (vlen == 0)
        return obase;
    ce.label = label;
    ce.hlen = hlen;
    ce.value = value;
    ce.vlen = vlen;
#ifdef DEBUG
    fprintf(stderr, "Coding: %.*s %.*s\n", hlen, label, vlen, value);
#endif
    if ((hip = lookup(hhccp->sym_table, (char *) &ce)) != NULL)
    {   /* Work out the index */
        cep = (struct comp_entry *) hip->body;
        i = comp_ind(cep);
#ifdef DEBUG_FULL
        fprintf(stderr, "Found label+value: %d\n", i);
#endif
        *optr = 0x80;
        optr = code_hpack_int(7, i, optr, otop);
    }
    else
    {
        ce.value = NULL;
        ce.vlen = 0;
        if ((hip = lookup(hhccp->sym_table, (char *) &ce)) != NULL)
        {
/*
 * Work out the index for just the first bit. This must be in the static table
 * because we only store pairs in the dynamic table. Which means we don't have
 * to worry about wrapping.
 */
            cep = (struct comp_entry *) hip->body;
            i = comp_ind(cep);
            *optr = 0x40;
            optr = code_hpack_int(6, i, optr, otop);
#ifdef DEBUG_FULL
            fprintf(stderr, "Found label: %d\n", i);
#endif
        }
        else
        {
            *optr++ = 0x40;
            code_hpack_string(ce.label, ce.label+ce.hlen, optr, otop,
                               &olen, 1);
            optr += olen;
        }
        code_hpack_string(value, value+vlen, optr, otop,
                               &olen, 1);
        optr += olen;
/*
 * Now need to add to the compression table
 */
        (void) new_comp_entry(hhccp, label, hlen, value, vlen);
    }
    return optr;
}
/*
 * Code up HTTP Headers or Responses as an HPACK head stream
 * Delineate the headers, then go through them coding them up.
 * Return the number of bytes output.
 * In case we run out of output space, or we haven't got the whole thing, we
 * return how far we got in ibasep. 
 *
 * Hashing the headers and the combinations looks sensible. The tricky bit is
 * getting the correct token number for the stuff in the dynamic table. We need
 * a back pointer to the entry in the circular buffer, and we need the pointer
 * to the circular buffer itself.
 */
int code_head_stream(hhccp, ibasep, itop, obase, otop)
struct http2_head_coding_context * hhccp;
unsigned char ** ibasep;
unsigned char * itop;
unsigned char * obase;
unsigned char * otop;
{
struct http_req_response hr;
int ilen;
int olen;
struct comp_entry ce;
struct comp_entry * cep;
unsigned char * iptr = *ibasep;
unsigned char * optr = obase;
int uri_flag;
HIPT *hip;
int i;
unsigned char *xp, *xp1;

    memset((unsigned char *) &hr, 0, sizeof(hr));
    hr.head_start.element = iptr;
    hr.head_start.len = itop - iptr;
    if (hpack_head_delineate(&hr) < 1)
    {
        fprintf(stderr, "Couldn't find headers in %*s\n",
              (itop - iptr), iptr);
        return 0;
    }
/*
 * Now process the headers.
 * -   The first line needs special treatment, since it is going to multiple
 *     headers
 * -   Thereafter, we have to:
 *     -   Look up pairs
 *     -   Failing that, look up header labels
 *     -   Use back pointers to the circular buffer and to the position of the
 *         pointer in the circular buffer to compute the token number.
 *
 * Process the first line. hr.status tells us if it is a request or response.
 */
    if (hr.status == -1)
    {     /* Request */
/*
 * The first four HTTP2 headers are constructed from the initial line, and the
 * Host: header. I understand that the Host: header is compulsory for HTTP 1.1
 * but in point of fact most things work without it, and if we are going through
 * a proxy the scheme and host may be on the first line.
 *
 * Output should be something like
 * :method: VERB (e.g. GET, POST, OPTIONS etc. from the first line)
 * :scheme: http (from the Host: header)
 * :path: /      (from the first line)
 * :authority: www.example.com (from the Host header)
 */
        if (!memcmp(hr.headings[0].label.element, "GET ", 4))
            *optr++ = 0x82;
        else
        if (!memcmp(hr.headings[0].label.element, "POST ", 5))
            *optr++ = 0x83;
        else /* Search for/stash the method */
            optr = code_header(hhccp, ":method", 7,
                          hr.headings[0].label.element,
                          strcspn(hr.headings[0].label.element," \r\n"),
                                 optr, otop);
        if (hr.headings[0].label.len != 0)
        {    /* There is a ':' */
            if (hr.headings[0].label.element[hr.headings[0].label.len - 1]
                         == 's')
                *optr++ = 0x87; /* Must be https */
            else
                *optr++ = 0x86; /* Must be http  */
            xp = hr.headings[0].value.element +
                     strspn(hr.headings[0].value.element, "/");
            xp1 = xp + strcspn(xp, "/");
            optr = code_header(hhccp, ":authority", 10, xp, (xp1 - xp),
                             optr, otop);
            xp = xp1 + strcspn(xp1, " \r\n");
            optr = code_header(hhccp, ":path", 5, xp1, (xp - xp1),
                                 optr, otop);
        }
        else
        {
            xp = hr.headings[0].value.element +
                     strcspn(hr.headings[0].value.element, " ") + 1;
            xp1 = xp + strcspn(xp, " \r\n");
            optr = code_header(hhccp, ":path", 5, xp, (xp1 - xp),
                             optr, otop);
            for (i = 1; optr < otop && i < hr.element_cnt; i++)
            {
                if (!strncasecmp(hr.headings[i].label.element, "Host:", 5))
                {
                    xp = hr.headings[i].value.element;
                    if (!strncmp(xp, "http:",5))
                    {
                        *optr++ = 0x86; /* Must be http  */
                        uri_flag = 1;
                        xp += 5;
                    }
                    else
                    if (!strncmp(xp, "https:",6))
                    {
                        *optr++ = 0x87; /* Must be https  */
                        uri_flag = 1;
                        xp += 6;
                    }
                    xp += strspn(xp, "/");
                    xp1 = xp + strcspn(xp, ":\r\n");
                    if (!uri_flag)
                    {
                        if (*xp1 == ':' && (atoi(xp1 + 1) != 443))
                            *optr++ = 0x86; /* http */
                        else
                            *optr++ = 0x87; /* Default https */
                    }
                    if (*xp1 == ':')
                        xp1 += strcspn(xp1, "\r\n");
                    optr = code_header(hhccp, ":authority", 10,
                                               xp, (xp1 - xp),
                             optr, otop);
                    break;
                }
            }
        }
    }
    else    /* Response */
        optr = code_header(hhccp, ":status", 7,
                           &hr.headings[0].label.element[9], 3,
                                 optr, otop);
/*
 * Loop - for lines 1 to element_cnt - 1
 * -   Search for the header and value together in the hash table
 *     -    If found, put it in
 *     -    If not found
 *          -   Search for the header on its own.
 *          -   If the header isn't one of the forbidden ones, plan to
 *              index it in combination
 *          -   Otherwise, mark as never indexable.
 *          -   If found, output header index
 *          -   Otherwise, header string
 *          -   Then value string, Huffman coded since doesn't leak, unless
 *              Huffman code is longer. No EOS marker I think; too long.
 */
    for (i = 1; optr < otop && i < hr.element_cnt; i++)
    {
        switch ( hr.headings[i].label.len)
        {
        case 4:
            if (!strncasecmp(hr.headings[i].label.element, "Host:", 5))
                continue;
            break;
        case 10:
            if (!strncasecmp(hr.headings[i].label.element, "Connection:",11))
                continue;
            break;
#ifdef GZIP_POST
        case 14:
            if (!strncasecmp(hr.headings[i].label.element,
                             "Content-Length:",15))
                continue;
            break;
        case 16:
            if (!strncasecmp(hr.headings[i].label.element,
                             "Content-Encoding:",17))
                continue;
            break;
#endif
        default:
            break;
        }
        optr = code_header(hhccp, 
                          hr.headings[i].label.element,
                          hr.headings[i].label.len,
                          hr.headings[i].value.element,
                          hr.headings[i].value.len,
                          optr, otop);
    }
#ifdef DEBUG
    fprintf(stderr, "Coded: %d optr=%lx otop=%lx\n", i,
                            (long) optr, (long) otop);
#endif
    *ibasep = hr.head_start.element + hr.head_start.len;
    return (((optr > otop) ? otop : optr) - obase);
}
#ifdef STANDALONE
/*************************************************************************
 * Test bed, drawn from the draft specification, mostly.
 *
 * Dummies to stop e2net.c pulling in genconv.c
 */
int app_recognise()
{
    return 0;
}
void do_null()
{
    return;
}
static unsigned char test_huff[] = { 0x8c,0xf1,0xe3,0xc2,0xe5,0xf2,0x3a,0x6b,0xa0,0xab,0x90,0xf4,0xff};
static unsigned char test_1337on5[] = { 0xff,0x9a,0xa };
/*
 * Head decode tests
 */
static unsigned char  req_questions_nohuff[][32] = {{
0x40,0x0a,0x63,0x75,0x73,0x74,0x6f,0x6d,0x2d,0x6b,0x65,0x79,0x0d,0x63,0x75,0x73,0x74,0x6f,0x6d,0x2d,0x68,0x65,0x61,0x64,0x65,0x72,0x0},
{0x04,0x0c,0x2f,0x73,0x61,0x6d,0x70,0x6c,0x65,0x2f,0x70,0x61,0x74,0x68,0x0},
{0x10,0x08,0x70,0x61,0x73,0x73,0x77,0x6f,0x72,0x64,0x06,0x73,0x65,0x63,0x72,0x65,0x74,0x0},
{ 0x82 ,0x0},
{0x82,0x86,0x84,0x41,0x0f,0x77,0x77,0x77,0x2e,0x65,0x78,0x61,0x6d,0x70,0x6c,0x65,0x2e,0x63,0x6f,0x6d,0x0},
{0x82,0x86,0x84,0xbe,0x58,0x08,0x6e,0x6f,0x2d,0x63,0x61,0x63,0x68,0x65,0x0},
{0x82,0x87,0x85,0xbf,0x40,0x0a,0x63,0x75,0x73,0x74,0x6f,0x6d,0x2d,0x6b,0x65,0x79,0x0c,0x63,0x75,0x73,0x74,0x6f,0x6d,0x2d,0x76,0x61,0x6c,0x75,0x65,0x0}};

static unsigned char req_questions_huff[][32] = {
{0x82,0x86,0x84,0x41,0x8c,0xf1,0xe3,0xc2,0xe5,0xf2,0x3a,0x6b,0xa0,0xab,0x90,0xf4,0xff,0x0},
{0x82,0x86,0x84,0xbe,0x58,0x86,0xa8,0xeb,0x10,0x64,0x9c,0xbf,0x0},
{0x82,0x87,0x85,0xbf,0x40,0x88,0x25,0xa8,0x49,0xe9,0x5b,0xa9,0x7d,0x7f,0x89,0x25,0xa8,0x49,0xe9,0x5b,0xb8,0xe8,0xb4,0xbf ,0x0}};
static unsigned char * req_answers_nohuff[] = {
   "custom-key: custom-header\r\n",
   ":path: /sample/path\r\n",
   "password: secret\r\n",
   ":method: GET\r\n",
   ":method: GET\r\n\
:scheme: http\r\n\
:path: /\r\n\
:authority: www.example.com\r\n",
   ":method: GET\r\n\
:scheme: http\r\n\
:path: /\r\n\
:authority: www.example.com\r\n\
cache-control: no-cache\r\n",
   ":method: GET\r\n\
:scheme: https\r\n\
:path: /index.html\r\n\
:authority: www.example.com\r\n\
custom-key: custom-value\r\n"};

static unsigned char * req_answers_huff[] = {
   ":method: GET\r\n\
:scheme: http\r\n\
:path: /\r\n\
:authority: www.example.com\r\n",
   ":method: GET\r\n\
:scheme: http\r\n\
:path: /\r\n\
:authority: www.example.com\r\n\
cache-control: no-cache\r\n",
   ":method: GET\r\n\
:scheme: https\r\n\
:path: /index.html\r\n\
:authority: www.example.com\r\n\
custom-key: custom-value\r\n"};

static unsigned char resp_questions_nohuff[][100] = { 
{0x48,0x03,0x33,0x30,0x32,0x58,0x07,0x70,0x72,0x69,0x76,0x61,0x74,0x65,0x61,0x1d,0x4d,0x6f,0x6e,0x2c,0x20,0x32,0x31,0x20,0x4f,0x63,0x74,0x20,0x32,0x30,0x31,0x33,0x20,0x32,0x30,0x3a,0x31,0x33,0x3a,0x32,0x31,0x20,0x47,0x4d,0x54,0x6e,0x17,0x68,0x74,0x74,0x70,0x73,0x3a,0x2f,0x2f,0x77,0x77,0x77,0x2e,0x65,0x78,0x61,0x6d,0x70,0x6c,0x65,0x2e,0x63,0x6f,0x6d,0x0},
{0x48,0x03,0x33,0x30,0x37,0xc1,0xc0,0xbf,0x0},
{0x88,0xc1,0x61,0x1d,0x4d,0x6f,0x6e,0x2c,0x20,0x32,0x31,0x20,0x4f,0x63,0x74,0x20,0x32,0x30,0x31,0x33,0x20,0x32,0x30,0x3a,0x31,0x33,0x3a,0x32,0x32,0x20,0x47,0x4d,0x54,0xc0,0x5a,0x04,0x67,0x7a,0x69,0x70,0x77,0x38,0x66,0x6f,0x6f,0x3d,0x41,0x53,0x44,0x4a,0x4b,0x48,0x51,0x4b,0x42,0x5a,0x58,0x4f,0x51,0x57,0x45,0x4f,0x50,0x49,0x55,0x41,0x58,0x51,0x57,0x45,0x4f,0x49,0x55,0x3b,0x20,0x6d,0x61,0x78,0x2d,0x61,0x67,0x65,0x3d,0x33,0x36,0x30,0x30,0x3b,0x20,0x76,0x65,0x72,0x73,0x69,0x6f,0x6e,0x3d,0x31,0x0}};
static unsigned char resp_questions_huff[][100] = {
{0x48,0x82,0x64,0x02,0x58,0x85,0xae,0xc3,0x77,0x1a,0x4b,0x61,0x96,0xd0,0x7a,0xbe,0x94,0x10,0x54,0xd4,0x44,0xa8,0x20,0x05,0x95,0x04,0x0b,0x81,0x66,0xe0,0x82,0xa6,0x2d,0x1b,0xff,0x6e,0x91,0x9d,0x29,0xad,0x17,0x18,0x63,0xc7,0x8f,0x0b,0x97,0xc8,0xe9,0xae,0x82,0xae,0x43,0xd3,0x0},
{0x48,0x83,0x64,0x0e,0xff,0xc1,0xc0,0xbf,0x0},
{0x88,0xc1,0x61,0x96,0xd0,0x7a,0xbe,0x94,0x10,0x54,0xd4,0x44,0xa8,0x20,0x05,0x95,0x04,0x0b,0x81,0x66,0xe0,0x84,0xa6,0x2d,0x1b,0xff,0xc0,0x5a,0x83,0x9b,0xd9,0xab,0x77,0xad,0x94,0xe7,0x82,0x1d,0xd7,0xf2,0xe6,0xc7,0xb3,0x35,0xdf,0xdf,0xcd,0x5b,0x39,0x60,0xd5,0xaf,0x27,0x08,0x7f,0x36,0x72,0xc1,0xab,0x27,0x0f,0xb5,0x29,0x1f,0x95,0x87,0x31,0x60,0x65,0xc0,0x03,0xed,0x4e,0xe5,0xb1,0x06,0x3d,0x50,0x07,0x0}};
static unsigned char * resp_answers[] = {
   ":status: 302\r\n\
cache-control: private\r\n\
date: Mon, 21 Oct 2013 20:13:21 GMT\r\n\
location: https://www.example.com\r\n",
   ":status: 307\r\n\
cache-control: private\r\n\
date: Mon, 21 Oct 2013 20:13:21 GMT\r\n\
location: https://www.example.com\r\n",
   ":status: 200\r\n\
cache-control: private\r\n\
date: Mon, 21 Oct 2013 20:13:22 GMT\r\n\
location: https://www.example.com\r\n\
content-encoding: gzip\r\n\
set-cookie: foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1\r\n"};

int main(argc, argv)
int argc;
char ** argv;
{
unsigned char buf[128];
struct http2_head_decoding_context * hhdcp;
struct http2_head_coding_context * hhccp;
unsigned char * xp;
unsigned long int len;
union {
    unsigned char ibuf[8];
    unsigned long int li;
} idec;
unsigned char * iptr;
char ibuf[4096];
char mbuf[4096];
char obuf[4096];
int flag;
char * top;
char * xptr;

    ini_huffman_decodes();
    hhdcp = ini_http2_head_decoding_context(256);
    hhccp = ini_http2_head_coding_context(256);
    iptr = decode_hpack_int(5, &test_1337on5[0], &test_1337on5[3],
                            &idec.ibuf[0], &idec.ibuf[8], &len);
    fprintf(stderr, "%u should be 2, %u should be 1337, points to end=%u\n",
                     len, idec.li,(iptr == &test_1337on5[3])?1: 0);
    xp = decode_hpack_string(&test_huff[0], &test_huff[13],
            &buf[0], &buf[128], &len);
    if (xp != &test_huff[13])
        fprintf(stderr, "Failed to scan the whole string; did %u\n",
                (xp - &test_huff[0]));
    fprintf(stderr, "Decode: (%u) %*s\n", len, len, (char *) & buf[0]);
#ifdef TEST_STRINGS
    while (fgets(ibuf, sizeof(ibuf) -1, stdin) != NULL)
    {
/*
 * First no Huffman coding, then Huffman
 */
        top = &ibuf[strlen(ibuf) - 1];
        *top = '\0';
        for (flag = 0; flag < 2; flag++)
        {
            memset(obuf, 0, sizeof(obuf));
            if ((xp = code_hpack_string(&ibuf[0], top, mbuf, &mbuf[4096], &len, flag))
                != top)
                fprintf(stderr, "flag = %u\nFailed to handle all of (%s) got to (%s)\n",
                                  flag,ibuf, xp);
            xptr = &mbuf[len];
            if (( xp = decode_hpack_string(&mbuf[0], xptr,
                &obuf[0], &obuf[4096], &len)) != xptr)
                fprintf(stderr, "Failed to decode the whole string; did %u\n",
                    (xp - ((unsigned char *) &mbuf[0])));
            if (strlen(ibuf) != len)
                fprintf(stderr, "Flag: %d\nFailed to re-generate the original string (%u): %s\n(%u): %*s\n",
                flag, strlen(ibuf),ibuf, len, len, obuf);
            else
                printf("Flag: %d In: %s Out: %.*s\n", flag, ibuf, len, obuf);
        }
    }
    exit(0);
#endif
    for (flag = 0; flag < 7; flag++)
    {
        iptr =  &req_questions_nohuff[flag][0];
        len = decode_head_stream(hhdcp, &iptr, iptr + strlen(iptr), &obuf[0],
                                        &obuf[4096]);
        if (memcmp(req_answers_nohuff[flag],&obuf[0], len))
        {
            fprintf(stderr, "req_nohuf[%u] Failed. Should have got %s\nbut instead got\n",
                   flag, req_answers_nohuff[flag]);
            gen_handle(stderr,&obuf[0],&obuf[len],1);
        }
        else
            fprintf(stderr, "Test req nohuff %d passed\n", flag);
        dump_dynamic(hhdcp);
        if (flag < 4)
        {
            hhdcp->dynamic_cnt = 0;
            purge_dynamic_decomp(hhdcp);
            fputs("Dynamic Table Should Be Empty ...\n", stderr);
            dump_dynamic(hhdcp);
            hhdcp->dynamic_cnt = 256;
        }
    }
    hhdcp->dynamic_cnt = 0;
    purge_dynamic_decomp(hhdcp);
    fputs("Dynamic Table Should Be Empty ...\n", stderr);
    dump_dynamic(hhdcp);
    hhdcp->dynamic_cnt = 256;
    for (flag = 0; flag < 3; flag++)
    {
        iptr =  &req_questions_huff[flag][0];
        len = decode_head_stream(hhdcp, &iptr, iptr + strlen(iptr), &obuf[0],
                                        &obuf[4096]);
        dump_dynamic(hhdcp);
        if (memcmp(req_answers_huff[flag],&obuf[0], len))
        {
            fprintf(stderr, "req_huff[%u] Failed. Should have got %s\nbut instead got\n",
                   flag, req_answers_huff[flag]);
            gen_handle(stderr,&obuf[0],&obuf[len],1);
        }
        else
            fprintf(stderr, "Test req huff %d passed\n", flag);
    }
    hhdcp->dynamic_cnt = 0;
    purge_dynamic_decomp(hhdcp);
    fputs("Dynamic Table Should Be Empty ...\n", stderr);
    dump_dynamic(hhdcp);
    hhdcp->dynamic_cnt = 256;
    for (flag = 0; flag < 3; flag++)
    {
        iptr = &resp_questions_nohuff[flag][0];
        len = decode_head_stream(hhdcp, &iptr, iptr + strlen(iptr), &obuf[0],
                                        &obuf[4096]);
        dump_dynamic(hhdcp);
        if (memcmp(resp_answers[flag],&obuf[0], len))
        {
            fprintf(stderr, "resp_nohuff[%u] Failed. Should have got %s\nbut instead got\n",
                   flag, resp_answers[flag]);
            gen_handle(stderr,&obuf[0],&obuf[len],1);
        }
        else
            fprintf(stderr, "Test resp nohuff %d passed\n", flag);
    }
    hhdcp->dynamic_cnt = 0;
    purge_dynamic_decomp(hhdcp);
    fputs("Dynamic Table Should Be Empty ...\n", stderr);
    dump_dynamic(hhdcp);
    hhdcp->dynamic_cnt = 256;
    for (flag = 0; flag < 3; flag++)
    {
        iptr = &resp_questions_huff[flag][0];
        len = decode_head_stream(hhdcp, &iptr, iptr + strlen(iptr), &obuf[0],
                                        &obuf[4096]);
        dump_dynamic(hhdcp);
        if (memcmp(resp_answers[flag],&obuf[0], len))
        {
            fprintf(stderr, "resp_huff[%u] Failed. Should have got %s\nbut instead got\n",
                   flag, resp_answers[flag]);
            gen_handle(stderr,&obuf[0],&obuf[len],1);
        }
        else
            fprintf(stderr, "Test resp huff %d passed\n", flag);
    }
/*
 * Read in HTTP, either requests or responses,  on stdin, and render it.
 */
    hhdcp->dynamic_cnt = 0;
    purge_dynamic_decomp(hhdcp);
    fputs("Dynamic Table Should Be Empty ...\n", stderr);
    dump_dynamic(hhdcp);
    for (xp = ibuf; fgets(xp, sizeof(ibuf) - (xp -
                 (unsigned char *) &ibuf[0]) -1, stdin) != NULL;)
    {
        if (*xp == '\n') 
        {
            memset(mbuf, 0, sizeof(mbuf));
            memset(obuf, 0, sizeof(obuf));
            *xp++ = '\r';
            *xp++ = '\n';
            *xp = '\0';
            top = xp;
            xp = ibuf;
            len = code_head_stream(hhccp, &xp , top, &mbuf[0], &mbuf[4096]);
            if (xp != top)
                fprintf(stderr, "Coder failed to handle all of (%s) got to (%s)\n",
                                  ibuf, xp);
            xp = &mbuf[0];
            top = &mbuf[len];
            len = decode_head_stream(hhdcp, &xp, top, &obuf[0],
                                        &obuf[4096]);
            if (xp != top)
                fprintf(stderr, "Decoder failed to handle all of (%s) got to (%s)\n",
                                  mbuf, xp);
            obuf[len] = '\0';
            fprintf(stderr, "Input:\n%s\nOutput:\n%s\n", ibuf, obuf);
            xp = &ibuf[0];
        }
        else
        {
            xp += strlen(xp) - 1;
            *xp++ = '\r'; 
            *xp++ = '\n'; 
        }
    }
    exit(0);
}
#endif
