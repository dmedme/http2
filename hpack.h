/*
 * Header file for the hpack implementation.
 * @(#) $Name$ $Id$ - Copyright (C) E2 Systems Limited 2014
 */
#ifndef HPACK_H
#define HPACK_H
#ifdef STANDALONE
#include "hashlib.h"
#include "e2net.h"
#endif
#include "ansi.h"
/*
 * The decoding context is just the static and dynamic tables
 */
struct http2_head_decoding_context {
    int static_size;
    struct circbuf * static_table;
    int dynamic_size;
    int dynamic_cnt;
    int dynamic_used;
    struct circbuf * dynamic_table;
};
/*
 * The coding context has to add a hash table that allows the reverse
 * lookups.
 */
struct http2_head_coding_context {
    struct http2_head_decoding_context *hhdcp;
    struct http2_head_decoding_context *debug;
    HASH_CON * sym_table;
};
struct http2_head_decoding_context * ini_http2_head_decoding_context ANSIARGS((int));
struct http2_head_coding_context * ini_http2_head_coding_context ANSIARGS((int));
int decode_head_stream ANSIARGS(( struct http2_head_decoding_context * hhdcp, unsigned char ** ibasep, unsigned char * itop, unsigned char * obase, unsigned char * otop));
int code_head_stream ANSIARGS(( struct http2_head_coding_context * hhccp, unsigned char ** ibasep, unsigned char * itop, unsigned char * obase, unsigned char * otop));
unsigned char * code_header ANSIARGS(( struct http2_head_coding_context * hhccp, unsigned char * obase, unsigned char * otop, unsigned char * label, unsigned int hlen, unsigned char * value, unsigned int vlen));
void zap_http2_head_coding_context();
void zap_http2_head_decoding_context();
void dump_dynamic();
void dump_symbols();
#endif
