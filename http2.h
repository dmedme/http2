#ifndef HTTP2_H
#define HTTP2_H

enum http2_stream_states {
HTTP2_STREAM_IDLE,
HTTP2_STREAM_LOCAL_RESERVED,
HTTP2_STREAM_REMOTE_RESERVED,
HTTP2_STREAM_OPEN, 
HTTP2_STREAM_LOCAL_HALF_CLOSED, 
HTTP2_STREAM_REMOTED_HALF_CLOSED,
HTTP2_STREAM_HAS_CLOSED 
};
struct http2_frame_header {
unsigned char len[3]; /* Length = len[0]*65536+len[1]*256+len[2], excluding header */
unsigned char typ;    /* Stream type */
unsigned char flags;  /* Applicable flags */
unsigned char sid[4]; /* Stream ID = ((sid[0] & 0x7f) << 24)
                       + (sid[1] << 16)
                       + (sid[2] << 8)
                       + sid[3]) */
};
/*
 * Not all flags are meaningful for all types
 */
#define HTTP2_END_STREAM 0x1
#define HTTP2_ACK 0x1
#define HTTP2_END_HEADERS 0x4
#define HTTP2_PADDED 0x8
#define HTTP2_PRIORITY 0x20

#define HTTP2_DATA_TYPE 0
/*
 * A data frame is:
 * - 1 byte pad length iff PADDED flag set
 * - payload
 * - optional pad length of zero bytes
 */ 
#define HTTP2_HEADERS_TYPE 1
/*
 * A headers frame is:
 * - 1 byte pad length iff PADDED flag set
 * - 1 bit stream dependency exclusive flag iff PRIORITY flag set
 * - 31 bit dependent Stream ID iff PRIORITY flag set
 * - 1 byte priority iff PRIORITY flag set
 * - compressed name/value pair data
 * - optional pad length of zero bytes
 */ 
#define HTTP2_PRIORITY_TYPE 2
/*
 * A priority frame is:
 * - 1 byte pad length
 * - 1 bit stream dependency exclusive
 * - 31 bit dependent Stream ID
 */
#define HTTP2_RST_STREAM_TYPE 3
/*
 * A rst frame is:
 * - 4 byte error code
 * Currently defined error codes are as follows. Unknown errors should be
 * treated as Internal Error
 */
#define HTTP2_NO_ERROR 0x0
  /*  The associated condition is not as a result of an
      error.  For example, a GOAWAY might include this code to indicate
      graceful shutdown of a connection. */
#define HTTP2_PROTOCOL_ERROR 0x1
  /*  The endpoint detected an unspecific protocol
      error.  This error is for use when a more specific error code is
      not available. */
#define HTTP2_INTERNAL_ERROR 0x2
  /*  The endpoint encountered an unexpected
      internal error. */
#define HTTP2_FLOW_CONTROL_ERROR 0x3
  /*  The endpoint detected that its peer
      violated the flow control protocol. */
#define HTTP2_SETTINGS_TIMEOUT 0x4
  /*  The endpoint sent a SETTINGS frame, but did
      not receive a response in a timely manner. */
#define HTTP2_STREAM_CLOSED 0x5
  /*  The endpoint received a frame after a stream
      was half closed. */
#define HTTP2_FRAME_SIZE_ERROR 0x6
  /*  The endpoint received a frame with an
      invalid size. */
#define HTTP2_REFUSED_STREAM 0x7
  /*  The endpoint refuses the stream prior to
      performing any application processing */
#define HTTP2_CANCEL 0x8
  /*  Used by the endpoint to indicate that the stream is no
      longer needed. */
#define HTTP2_COMPRESSION_ERROR 0x9
  /*  The endpoint is unable to maintain the
      header compression context for the connection. */
#define HTTP2_CONNECT_ERROR 0xa
  /*  The connection established in response to a
      CONNECT request was reset or abnormally closed. */
#define HTTP2_ENHANCE_YOUR_CALM 0xb
  /*  The endpoint detected that its peer is
      exhibiting a behavior that might be generating excessive load. */
#define HTTP2_INADEQUATE_SECURITY 0xc
  /*  The underlying transport has properties
      that do not meet minimum security requirements */

#define HTTP2_SETTINGS_TYPE 4
/*
 * A Settings frame is:
 * - empty if the ACK flag is set; it simply acknowledges receipt of the peer's settings
 * - otherwise, zero or pairs of:
 *   - 2 byte ID
 *   - 4 byte value
 * Currently known ID's are as follows:
 */
#define HTTP2_SETTINGS_HEADER_TABLE_SIZE 0x1
 /*   Allows the sender to inform the
      remote endpoint of the maximum size of the header compression
      table used to decode header blocks, in octets.  The encoder can
      select any size equal to or less than this value by using
      signaling specific to the header compression format inside a
      header block.  The initial value is 4,096 octets.
*/

#define HTTP2_SETTINGS_ENABLE_PUSH 0x2
 /*   This setting can be use to disable
      server push (Section 8.2).  An endpoint MUST NOT send a
      PUSH_PROMISE frame if it receives this parameter set to a value of
      0.  An endpoint that has both set this parameter to 0 and had it
      acknowledged MUST treat the receipt of a PUSH_PROMISE frame as a
      connection error (Section 5.4.1) of type PROTOCOL_ERROR.

      The initial value is 1, which indicates that server push is
      permitted.  Any value other than 0 or 1 MUST be treated as a
      connection error (Section 5.4.1) of type PROTOCOL_ERROR.
*/

#define HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS 0x3
 /*   Indicates the maximum number
      of concurrent streams that the sender will allow.  This limit is
      directional: it applies to the number of streams that the sender
      permits the receiver to create.  Initially there is no limit to
      this value.  It is recommended that this value be no smaller than
      100, so as to not unnecessarily limit parallelism.

      A value of 0 for SETTINGS_MAX_CONCURRENT_STREAMS SHOULD NOT be
      treated as special by endpoints.  A zero value does prevent the
      creation of new streams, however this can also happen for any
      limit that is exhausted with active streams.  Servers SHOULD only
      set a zero value for short durations; if a server does not wish to
      accept requests, closing the connection could be preferable.
*/

#define HTTP2_SETTINGS_INITIAL_WINDOW_SIZE 0x4
 /*   Indicates the sender's initial
      window size (in octets) for stream level flow control.  The
      initial value is 2^16-1 (65,535) octets.

      This setting affects the window size of all streams, including
      existing streams, see Section 6.9.2.

      Values above the maximum flow control window size of 2^31-1 MUST
      be treated as a connection error (Section 5.4.1) of type
      FLOW_CONTROL_ERROR.
*/

#define HTTP2_SETTINGS_MAX_FRAME_SIZE 0x5
 /*   Indicates the size of the largest
      frame payload that the sender is willing to receive, in octets.

      The initial value is 2^14 (16,384) octets.  The value advertised
      by an endpoint MUST be between this initial value and the maximum
      allowed frame size (2^24-1 or 16,777,215 octets), inclusive.
      Values outside this range MUST be treated as a connection error
      (Section 5.4.1) of type PROTOCOL_ERROR.
*/

#define HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE 0x6
 /*   This advisory setting informs a
      peer of the maximum size of header list that the sender is
      prepared to accept, in octets.  The value is based on the
      uncompressed size of header fields, including the length of the
      name and value in octets plus an overhead of 32 octets for each
      header field.

      For any given request, a lower limit than what is advertised MAY
      be enforced.  The initial value of this setting is unlimited.
 *
 * Non-infinite initial values
 */
#define HTTP2_HEADER_TABLE_SIZE_INI 4096
#define HTTP2_ENABLE_PUSH_INI 1
#define HTTP2_INITIAL_WINDOW_SIZE_INI 65535
#define HTTP2_MAX_FRAME_SIZE_INI 16384

#define HTTP2_PUSH_PROMISE_TYPE 0x5
/*
 * A PUSH_PROMISE frame contains:
 * - An optional 1 byte pad length (iff pad flag is set)
 * - A 1 bit reserved flag
 * - A promised stream ID, 31 bits
 * - An optional header block fragment
 * - optional padding
 */
#define HTTP2_PING_TYPE 0x6
/*
 * A PING frame contains:
 * - A compulsory 8 byte payload, that can be anything
 *
 * The response ('pong?') is the same, but with the ACK flag set.
 * There must be no stream ID.
 */
#define HTTP2_GOAWAY_TYPE 0x7
/*
 * A GOAWAY frame contains:
 * - 1 bit reserved
 * - 31 bits Last Stream ID; all transactions on Stream ID's higher than this
 *   can be re-tried on a fresh connection.
 * - A 4 byte error code, as above
 * - Optionally, as much debug data as we care to include
 */
#define HTTP2_WINDOW_UPDATE_TYPE 0x8
/*
 * A WINDOW_UPDATE frame contains:
 * - 1 bit reserved
 * - 31 bits window increment. This can apply to the connection (Stream ID == 0)
 *   or an individual stream. Both need to be tracked. The SETTINGS frame is
 *   used to reduce the window size.
 * Only DATA frames are subject to flow control.
 */
#define HTTP2_CONTINUATION_TYPE 0x9
/*
 * A CONTINUATION frame contains:
 * - Header information
 * You can have as many as you like; the sequence is terminated by a
 * CONTINUATION frame with the END_HEADERS flag set.
 ****************************************************************************
 * Data requirements for HTTP/2 housekeeping.
 ****************************************************************************
 * We need to:
 * -  Track requests, so that responses can be matched to them
 * -  Update outstanding requests, in the event of NTLM gubbins, Basic
 *    Authentication, ORACLE Forms Pragma stuff, etc. etc.
 * -  Track mapping between notional HTTP/1.1 connections and HTTP/2 streams.
 * -  Match requests to PUSH PROMISE headers, both incoming and outgoing.
 * -  Track lowest and highest stream ID's
 * -  Track stream states, for both open and closed streams.
 * -  Find streams, both from the incoming side and the outgoing side
 * -  Maintain the stream priority tree
 * -  Be able to re-establish connections in the event of connection loss, and
 *    resend.
 * -  Pass additional requests from the read side to the write side
 * -  Recognise when requests are complete, and 'retire' them.
 * At the top level, we have:
 * -  Access via the Endpoint
 * -  Bootstrap state (to support the switch from HTTP/1.1 to HTTP/2; possibly
 *    just embedded in the program logic)
 * -  A timer (to generate SETTINGS_TIMEOUT errors)?
 * -  Lowest valid stream ID incoming
 * -  Next Stream ID outgoing
 * -  Window Size incoming
 * -  Window Size outgoing
 * -  Header compression table
 * -  Header decompression table
 * -  Connection Settings:
 *    - HTTP2_SETTINGS_HEADER_TABLE_SIZE
 *    - HTTP2_SETTINGS_ENABLE_PUSH
 *    - HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS
 *    - HTTP2_SETTINGS_INITIAL_WINDOW_SIZE
 *    - HTTP2_SETTINGS_MAX_FRAME_SIZE
 *    - HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE
 * -  Lowest stream that can be re-tried (used for re-opening after failure)
 * So:
 * -  Streams need to be hashed by stream ID and HTTP/1.1 link, and an
 *    indefinite number tracked. Keep:
 *    -    Stream ID
 *    -    LINK (a la WebdriveBase)
 *    -    Window Size
 *    -    HTTP/1.1 link
 *    -    State (Idle, Open, Half-closed local, Half-closed remote, Closed)
 *    -    Dependency chain; link to the dependent stream.
 *    A chain of requests? Or just the current one? Has to be a chain, even if
 *    they are necessarily done in order. The requests for multiple streams are
 *    ideally chained in the order that they should be sent, which is governed
 *    by the priority, as well as being chained for the link. Requests have to
 *    be hashed to be able to link PUSH PROMISE stuff with the corresponding
 *    actual request. Carrying out replacement processing on the things after
 *    they are added to the chain is then a possibility; cookies, for instance.
 *    A complication is that, like the GOTO logic, Cookie Processing is in
 *    webread(). So anything with a possible Set-Cookie response has to be a
 *    quiescent point. Hmmm.
 * -  Requests and responses have to have HTTP/1.1 and HTTP/2 manifestations.
 *    Only HTTP/1.1 is seen outside the HTTP/2 sub-system, if for no other
 *    reason than that the HTTP/2 versions are unintelligible.
 * Note that the 'Quiescent points' are envisaged outside, that is, quiescence
 * suspends input, rather than impacting the behaviour of the HTTP/2 sub-system.
 *
 * Note also that this gives every appearance of being an HTTP proxy.
 */
struct http2_con {
    END_POINT * ep;                /* The End Point we are tied to; host+port */
    LINK * io_link;                /* The Link used for actual I/O            */
    int http_level;                /* 0 - HTTP/1 ; 1 - HTTP/2                 */
    time_t settings_timeout;       /* Wait for settings acknowledgement       */
    int min_new_strm_in;
    int next_new_strm_out;
    int win_size_in;
    int win_size_out;
    struct http2_head_coding_context * hhccp;
    struct http2_head_decoding_context * hhdcp;
/*
 * Settings
 */
    struct http2_settings {
        int header_table_size;
        int enable_push;
        int max_conc_strms;
        int initial_win_size;
        int max_frame_size;
        int max_header_list_size;
    } set_out;
    struct http2_settings set_in;
    int min_strm_retriable; /*  Lowest retriable stream (used for re-opening after failure) */
    HASH_CON * stream_hash;                 /* Hash of multiplexed streams */
    struct http2_stream * priority_anchor; /* root of priority-ordered chain */
};
/*
 * The HTTP/2 request/response structure.
 *
 * Initially a script element is submitted.
 *
 * We need to search for a PUSH stream. If we find one, its stream is linked,
 * and we look to see if it has already had a response. If it has, then we
 * take its http1 response.
 *
 * The HTTP/1.1 stream linkage could be problematic, since there is no reason
 * why PUSH PROMISE streams should simply map to HTTP/1 streams.
 * 
 * The final response is returned as a child script element of the initial
 * submission
 */
struct http2_req_response {
    struct script_element * subm_req;  /* Initial HTTP/1.1 submitted request */
    unsigned char * http1_req;         /* Will point to script element body */
    struct http2_stream * hsp;
    struct http2_stream * ppsp;        /* PUSH PROMISE stream if there is one */
    struct element_tracker http2_req_headers;
    int http2_req_body_len;
    struct element_tracker http2_req_body;
    struct element_tracker http2_resp_headers;
    int http2_resp_body_len;
    int gzip_flag;
    struct http_req_response hr;       /* HTTP/1.1 response (?) */
};
/*
 * Do not need hash by LINK, because the links have stream pointers
 */
#define HTTP2_NORMAL_STREAM 0
#define HTTP2_PUSH_STREAM 1

struct http2_stream {
    LINK * http1_link;
    struct http2_con * own_h2cp;
    int stream_typ;
    int stream_id;
    int weight;
    int win_size_in;
    int win_size_out;
    enum http2_stream_states stream_state;
    struct http2_stream * depend_stream;    /* Dependent stream, if there is one */
    struct http2_stream * peer;       /* Links all the streams at the same level together */
    struct http2_stream * priority_chain;   /* Links all the streams together */
    struct http2_req_response in_flight;
};
void link_pipe_buf_forward();
void pipe_buf_link_forward();
struct http2_con * new_http2_con();
struct http2_stream * new_http2_stream();
void http2_data_append();
void http2_header_append();
void q_end_stream();
void refresh_http2_con();
void clear_in_flight();
void zap_http2_con();
void notify_master();
#endif
