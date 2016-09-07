/*
 * NTLM Validation sequence for a secured page ...
 ****************************************************************
 * (After http://www.innovation.ch/personal/ronald/ntlm.html)
 ****************************************************************
 * 1: C  --> S   GET ...
 *     
 * 2: C <--  S   401 Unauthorized
 *    WWW-Authenticate: NTLM
 *     
 * 3: C  --> S   GET ...
 *    Authorization: NTLM <base64-encoded type-1-message>
 *     
 * 4: C <--  S   401 Unauthorized
 *    WWW-Authenticate: NTLM <base64-encoded type-2-message>
 *     
 * 5: C  --> S   GET ...
 *    Authorization: NTLM <base64-encoded type-3-message>
 *     
 * 6: C <--  S   200 Ok, 304 or whatever
 ****************************************************************
 * NTLM Validation sequence for a secured proxy as observed ...
 ****************************************************************
 * 1: C  --> S   GET ...
 *     
 * 2: C <--  S   407 Unauthorized
 *    Proxy-Authenticate: NTLM
 *     
 * 3: C  --> S   GET ...
 *    Proxy-Authorization: NTLM <base64-encoded type-1-message>
 *     
 * 4: C <--  S   407 Unauthorized
 *    Proxy-Authenticate: NTLM <base64-encoded type-2-message>
 *     
 * 5: C  --> S   GET ...
 *    Proxy-Authorization: NTLM <base64-encoded type-3-message>
 *     
 * 6: C <--  S   200 Ok, 304 or whatever
 ****************************************************************
 * This code currently assumes it is running on a little-endian machine
 ****************************************************************
 * Also includes the Basic authentication routine, basic_construct()
 */
#include <stdio.h>
#include <openssl/opensslconf.h>
#include <openssl/err.h>
#include <openssl/md4.h>
#include <openssl/md5.h>
#include <openssl/hmac.h>
#include <openssl/des.h>
#include "e2conv.h"
static struct ntlm_mess {
    int mess_id;
    char *mess_name;
    char *mess_form;
    struct iocon * mess_io;
    int mess_len;
}
ntlm_mess[] = {
{ 0, "HEADER", "1X8 1I4"},
{ 1, "TYPE 1", "1I4 2I2 1I4 2I2 1I4 1I1 1I1 1I2 1Q4" },
/*
 **** type-1-message *** First step from the client
 * byte    label[8];     // 'N', 'T', 'L', 'M', 'S', 'S', 'P', '\0'
 * long    type;            // 0x01
 * long    flags;
 * 
 * Flags are a208b207
 * ==================
 * 0x00000001	Negotiate Unicode
 * 0x00000002	Negotiate OEM
 * 0x00000004	Request Target 	Requests that the server's authentication realm be included in the Type 2 message.
 * NOT   0x00000010	Negotiate Sign 	Specifies that authenticated communication between the client and server should carry a digital signature (message integrity).
 * NOT   0x00000020	Negotiate Seal 	Specifies that authenticated communication between the client and server should be encrypted (message confidentiality).
 * NOT   0x00000040	Negotiate Datagram Style 	Indicates that datagram authentication is being used.
 * NOT   0x00000080	Negotiate Lan Manager Key 	Indicates that the Lan Manager Session Key should be used for signing and sealing authenticated communications.
 * NOT   0x00000100	Negotiate Netware 	This flag's usage has not been identified.
 * 0x00000200	Negotiate NTLM 	Indicates that NTLM authentication is being used.
 * NOT   0x00000800	Negotiate Anonymous 	Sent by the client in the Type 3 message to indicate that an anonymous context has been established. This also affects the response fields (as detailed in the "Anonymous Response" section).
 * 0x00001000	Negotiate Domain Supplied 	Sent by the client in the Type 1 message to indicate that the name of the domain in which the client workstation has membership is included in the message. This is used by the server to determine whether the client is eligible for local authentication.
 * 0x00002000	Negotiate Workstation Supplied 	Sent by the client in the Type 1 message to indicate that the client workstation's name is included in the message. This is used by the server to determine whether the client is eligible for local authentication.
 * NOT   0x00004000	Negotiate Local Call 	Sent by the server to indicate that the server and client are on the same machine. Implies that the client may use the established local credentials for authentication instead of calculating a response to the challenge.
 * 0x00008000	Negotiate Always Sign 	Indicates that authenticated communication between the client and server should be signed with a "dummy" signature.
 * NOT   0x00010000	Target Type Domain 	Sent by the server in the Type 2 message to indicate that the target authentication realm is a domain.
 * NOT   0x00020000	Target Type Server 	Sent by the server in the Type 2 message to indicate that the target authentication realm is a server.
 * NOT   0x00040000	Target Type Share 	Sent by the server in the Type 2 message to indicate that the target authentication realm is a share. Presumably, this is for share-level authentication. Usage is unclear.
 * 0x00080000	Negotiate NTLM2 Key 	Indicates that the NTLM2 signing and sealing scheme should be used for protecting authenticated communications. Note that this refers to a particular session security scheme, and is not related to the use of NTLMv2 authentication. This flag can, however, have an effect on the response calculations (as detailed in the "NTLM2 Session Response" section).
 * NOT   0x00100000	Request Init Response 	This flag's usage has not been identified.
 * NOT   0x00200000	Request Accept Response 	This flag's usage has not been identified.
 * NOT   0x00400000	Request Non-NT Session Key 	This flag's usage has not been identified.
 * NOT   0x00800000	Negotiate Target Info 	Sent by the server in the Type 2 message to indicate that it is including a Target Information block in the message. The Target Information block is used in the calculation of the NTLMv2 response.
 * 0x02000000	unknown 	This flag's usage has not been identified.
 * 0x20000000	Negotiate 128 	Indicates that 128-bit encryption is supported.
 * NOT   0x40000000	Negotiate Key Exchange 	Indicates that the client will provide an encrypted master key in the "Session Key" field of the Type 3 message.
 * 0x80000000	Negotiate 56 	Indicates that 56-bit encryption is support
 *
 * short   dom_len;         // domain string length
 * short   dom_alloc;         // domain string length
 * long    dom_off;         // domain string offset
 * short   host_len;        // host string length
 * short   host_alloc;        // host string length
 * long    host_off;        // host string offset (always 0x28)
 * byte    os_major;
 * byte    os_minor;
 * short   build;
 * long    something;       // 0x0f000000
 * byte    host[*];         // host string (ASCII)
 * byte    dom[*];          // domain string (ASCII)
 */
{ 2, "TYPE 2", "2I2 2I4 1X8 2I4 2I2 1I4 2I1 1I2 1Q4" },
/*
 **** type-2-message *** Server challenge
 * byte    protocol[8];     // 'N', 'T', 'L', 'M', 'S', 'S', 'P', '\0'
 * long    type;            // 0x02
 * short   target_name_len;
 * short   target_name_alloc;
 * long    target_name_off;
 * long    flags;
 * byte    nonce[8];        // nonce
 * long    context[2];
 * short   target_len;
 * short   target_alloc;
 * long    target_off;
 * byte    os_major;
 * byte    os_minor;
 * short   build;
 * long    something;       // 0x0f000000
 * context[*]
 */
{ 3, "TYPE 3", "2I2 1I4 2I2 1I4 2I2 1I4 2I2 1I4 2I2 1I4 2I2 2I4 2I1 1I2 1Q4"  },
/*
 **** type-3-message *** Response incorporating challenge and password hashes
 * byte    protocol[8];     // 'N', 'T', 'L', 'M', 'S', 'S', 'P', '\0'
 * long    type;            // 0x03
 * short   lm_resp_len;     // LanManager response length (always 0x18)
 * short   lm_resp_alloc;     // LanManager response buffer length (always 0x18)
 * long    lm_resp_off;     // LanManager response offset
 * short   nt_resp_len;     // NT response length (always 0x18)
 * short   nt_resp_alloc;     // NT response length buffer (always 0x18)
 * long    nt_resp_off;     // NT response offset
 * short   dom_len;         // domain string length
 * short   dom_alloc;         // domain string buffer length
 * long    dom_off;         // domain string offset (always 0x40)
 * short   user_len;        // username string length
 * short   user_alloc;        // username string buffer length
 * long    user_off;        // username string offset
 * short   host_len;        // host string length
 * short   host_alloc;        // host string buffer length
 * long    host_off;        // host string offset
 * long    msg_len;         // message length
 * long    flags;           // 0x8201
 * byte    dom[*];          // domain string (unicode UTF-16LE)
 * byte    user[*];         // username string (unicode UTF-16LE)
 * byte    host[*];         // host string (unicode UTF-16LE)
 * byte    lm_resp[*];      // LanManager response
 * byte    nt_resp[*];      // NT response
 */
{ 0, 0 }
};
/*
 * Structure to hold all the NTLM-related data
 */
struct ntlm_data {
    int rec_type;
    unsigned char nonce[8];
    int target_len;      /* target length                                   */
    int session_len;     /* session key length                              */
    int target_name_len; /* target name length                              */
    int flags;
    int lm_resp_len;     /* LanManager response length (always 0x18)        */
    int lm_resp_off;     /* LanManager response offset                      */
    int nt_resp_len;     /* NT response length (always 0x18)                */
    int nt_resp_off;     /* NT response offset                              */
    int dom_len;         /* domain string length                            */
    int dom_off;         /* domain string offset                            */
    int user_len;        /* username string length                          */
    int user_off;        /* username string offset                          */
    int host_len;        /* host string length                              */
    int host_off;        /* host string offset                              */
    int session_off;     /* session key offset                              */
    int target_off;      /* target offset                                   */
    int target_name_off; /* target name offset                              */
    int os_major;
    int os_minor;
    int context[2];
    int build;
    unsigned char *    dom;          /* domain string (unicode UTF-16LE)    */
    unsigned char *    user;         /* username string (unicode UTF-16LE)  */
    unsigned char *    host;         /* host string (unicode UTF-16LE)      */
    unsigned char *    lm_resp;      /* LanManager response                 */
    unsigned char *    nt_resp;      /* NT response                         */
    unsigned char * target;          /* Target                              */
    unsigned char * target_name;     /* Target Name                         */
    unsigned char * session_key;     /* Should not be there                 */
};
/*
 * Clear out malloc()ed stuff
 */
void clean_ntlm_data (pnd)
struct ntlm_data * pnd;
{
    if (pnd->dom != NULL)
    {
        free(pnd->dom);
        pnd->dom = NULL;
    }
    if (pnd->host != NULL)
    {
        free(pnd->host);
        pnd->host = NULL;
    }
    if (pnd->user != NULL)
    {
        free(pnd->user);
        pnd->user = NULL;
    }
    if (pnd->target != NULL)
    {
        free(pnd->target);
        pnd->target = NULL;
    }
    if (pnd->target_name != NULL)
    {
        free(pnd->target_name);
        pnd->target_name = NULL;
    }
    if (pnd->session_key != NULL)
    {
        free(pnd->session_key);
        pnd->session_key = NULL;
    }
    if (pnd->lm_resp != NULL)
    {
        free(pnd->lm_resp);
        pnd->lm_resp = NULL;
    }
    if (pnd->nt_resp != NULL)
    {
        free(pnd->nt_resp);
        pnd->nt_resp = NULL;
    }
    return;
}
/*
 * Set up the scan logic
 */
void ntlm_init()
{
struct ntlm_mess *dmp;
int i;

    for (dmp = &ntlm_mess[0]; dmp->mess_name != (char *) NULL; dmp++)
    {
        if (dmp->mess_form != (char *) NULL)
            dmp->mess_len = e2rec_comp(&(dmp->mess_io), dmp->mess_form);
        else
            dmp->mess_len = 0;
    }
    return;
}
/*
 * Recognise any of the three possible NTLM records
 */
int ntlm_recognise(buf, len, pnd)
unsigned char * buf;
int len;
struct ntlm_data * pnd; 
{
int ret_len;
unsigned int xi;
unsigned char * tbuf = (unsigned char *) malloc(len);
int fld_cnt;
struct fld_descrip * desc_arr;
unsigned short int xsi;
int rec_type;
int i;

    if (tbuf == NULL)
        return 0;
    ret_len = b64dec(len, buf, tbuf);

    if ((fld_cnt = e2rec_map_bin(&desc_arr, tbuf, NULL, ntlm_mess[0].mess_io,
                   0,0)) == 2
      &&  desc_arr[0].len == 8
      && ! strcmp("NTLMSSP", desc_arr[0].fld))
    {
        memcpy((char *) &rec_type, desc_arr[1].fld, 4);
        free(desc_arr);
        memset((char *) pnd, 0, sizeof(struct ntlm_data));
        pnd->rec_type = rec_type;
        switch(rec_type)
        {
        case 1:
            if ((fld_cnt = e2rec_map_bin(&desc_arr, tbuf + 12,
                        NULL, ntlm_mess[1].mess_io, 0,0)) == 10)
            {
                memcpy((char *) &xi, desc_arr[0].fld, 4);
                pnd->flags = xi;
                memcpy((char *) &xsi, desc_arr[1].fld, 2);
                pnd->dom_len = xsi;
                memcpy((char *) &xsi, desc_arr[2].fld, 2);
                if (pnd->dom_len == xsi)
                {
                    memcpy((char *) &xi, desc_arr[3].fld, 4);
                    pnd->dom_off = xi;
                    memcpy((char *) &xsi, desc_arr[4].fld, 2);
                    pnd->host_len = xsi;
                    memcpy((char *) &xsi, desc_arr[5].fld, 2);
                    if (pnd->host_len == xsi)
                    {
                        memcpy((char *) &xi, desc_arr[6].fld, 4);
                        pnd->host_off = xi;
                        if (pnd->host_off == 0x28)
                        {
                            pnd->os_major = *(desc_arr[7].fld);
                            pnd->os_minor = *(desc_arr[8].fld);
                            memcpy((char *) &xsi, desc_arr[9].fld, 2);
                            pnd->build = xsi;
                            pnd->host = (unsigned char *)
                                  malloc(pnd->host_len + 1);
                            memcpy(pnd->host, tbuf + pnd->host_off,
                                      pnd->host_len);
                            *(pnd->host + pnd->host_len) = '\0';
                            pnd->dom = (unsigned char *)
                                       malloc(pnd->dom_len + 1);
                            memcpy(pnd->dom, tbuf + pnd->dom_off,
                                                 pnd->dom_len);
                            *(pnd->dom + pnd->dom_len) = '\0';
                            free(tbuf);
                            free(desc_arr);
                            return 1;
                        }
                    }
                }
            }
            break;
        case 2:
            if ((fld_cnt = e2rec_map_bin(&desc_arr, tbuf + 12, NULL,
                       ntlm_mess[2].mess_io, 0,0)) == 13)
            {
                memcpy((char *) &xsi, desc_arr[0].fld, 2);
                pnd->target_name_len = xsi;
                memcpy((char *) &xsi, desc_arr[1].fld, 2);
                if (xsi == pnd->target_name_len)
                {
                    memcpy((char *) &xi, desc_arr[2].fld, 4);
                    pnd->target_name_off = xi;
                    memcpy((char *) &xi, desc_arr[3].fld, 4);
                    pnd->flags = xi;
                    memcpy(pnd->nonce, desc_arr[4].fld, 8);
                    memcpy((char *) &xi, desc_arr[5].fld, 4);
                    pnd->context[0] = xi;
                    memcpy((char *) &xi, desc_arr[6].fld, 4);
                    pnd->context[1] = xi;
                    memcpy((char *) &xsi, desc_arr[7].fld, 2);
                    pnd->target_len = xsi;
                    memcpy((char *) &xsi, desc_arr[8].fld, 2);
                    if (xsi == pnd->target_len)
                    {
                        memcpy((char *) &xi, desc_arr[9].fld, 4);
                        pnd->target_off = xi;
                        pnd->os_major = *(desc_arr[10].fld);
                        pnd->os_minor = *(desc_arr[11].fld);
                        memcpy((char *) &xsi, desc_arr[12].fld, 2);
                        pnd->build = xsi;
                        pnd->target_name = (unsigned char *) malloc(
                                              pnd->target_name_len);
                        memcpy(pnd->target_name, tbuf + pnd->target_name_off,
                                     pnd->target_name_len);
                        pnd->target = (unsigned char *) malloc(
                                              pnd->target_len);
                        memcpy(pnd->target, tbuf + pnd->target_off,
                                     pnd->target_len);
                        free(tbuf);
                        free(desc_arr);
                        return 1;
                    }
                }
            }
            break;
        case 3:
            if ((fld_cnt = e2rec_map_bin(&desc_arr, tbuf + 12, NULL,
                            ntlm_mess[3].mess_io, 0,0)) == 22)
            {
                for (i = 0; i < 5; i = (i == 1) ? (i + 2) : (i + 1))
                {
                    memcpy((char *) &xsi, desc_arr[i].fld, 2);
                    if (xsi != 0x18)
                        break;
                }
/*
 * This probably won't be true for NTLMv2
 */
                if (i == 5)
                {
                    pnd->lm_resp_len = 0x18;
                    pnd->nt_resp_len = 0x18;
                    memcpy((char *) &xi, desc_arr[2].fld, 4);
                    pnd->lm_resp_off = xi;
                    memcpy((char *) &xi, desc_arr[5].fld, 4);
                    pnd->nt_resp_off = xi;
                    memcpy((char *) &xsi, desc_arr[6].fld, 2);
                    pnd->dom_len = xsi;
                    memcpy((char *) &xsi, desc_arr[7].fld, 2);
                    if (pnd->dom_len == xsi)
                    {
                        memcpy((char *) &xi, desc_arr[8].fld, 4);
                        if (xi == 0x48)
                        {
                            pnd->dom_off = xi;
                            memcpy((char *) &xsi, desc_arr[9].fld, 2);
                            pnd->user_len = xsi;
                            memcpy((char *) &xsi, desc_arr[10].fld, 2);
                            if (pnd->user_len == xsi)
                            {
                                memcpy((char *) &xi, desc_arr[11].fld, 4);
                                pnd->user_off = xi;
                                memcpy((char *) &xsi, desc_arr[12].fld, 2);
                                pnd->host_len = xsi;
                                memcpy((char *) &xsi, desc_arr[13].fld, 2);
                                if (pnd->host_len == xsi)
                                {
                                    memcpy((char *) &xi, desc_arr[14].fld, 4);
                                    pnd->host_off = xi;
                                    memcpy((char *) &xsi, desc_arr[15].fld, 2);
                                    if (xsi == 0)
                                    {
                                        pnd->session_len = 0;
                                        memcpy((char *) &xsi, desc_arr[16].fld, 2);
                                        if (xsi == 0)
                                        {
                                            memcpy((char *) &xi,
                                                   desc_arr[17].fld, 4);
                                            pnd->session_off = xi;
                                            memcpy((char *) &xi,
                                                   desc_arr[18].fld, 4);
                                            pnd->flags = xi;
                                            pnd->os_major = *(desc_arr[19].fld);
                                            pnd->os_minor = *(desc_arr[20].fld);
                                            memcpy((char *) &xsi,
                                                  desc_arr[21].fld, 2);
                                            pnd->build = xsi;
                                            pnd->dom = (unsigned char *)
                                                   malloc(pnd->dom_len);
                                            memcpy(pnd->dom, tbuf+pnd->dom_off,
                                               pnd->dom_len);
                                            pnd->user = (unsigned char *)
                                                   malloc(pnd->user_len);
                                            memcpy(pnd->user,
                                               tbuf+pnd->user_off,
                                               pnd->user_len);
                                            pnd->host = (unsigned char *)
                                                   malloc(pnd->host_len);
                                            memcpy(pnd->host,
                                               tbuf+pnd->host_off,
                                               pnd->host_len);
                                            pnd->lm_resp = (unsigned char *)
                                                   malloc(pnd->lm_resp_len);
                                            memcpy(pnd->lm_resp,
                                               tbuf+pnd->lm_resp_off,
                                               pnd->lm_resp_len);
                                            pnd->nt_resp = (unsigned char *)
                                                   malloc(pnd->nt_resp_len);
                                            memcpy(pnd->nt_resp,
                                               tbuf+pnd->nt_resp_off,
                                               pnd->nt_resp_len);
                                           free(tbuf);
                                           free(desc_arr);
                                           return 1;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            break;
        default:
            break;
        }
    }
    free(tbuf);
    free(desc_arr);
    return 0;
}
#ifdef USE_BITWISE_56
void gen64from56(in, result)
unsigned char * in;
unsigned char * result;
{
int i;
int bit_pos = 1; /* Keeps track of the result bit position */
int bit_cnt = 0; /* Bits in byte                           */
int bit;

    memset(result, 0, 8);
/*
 * for each of the 56 input bits
 */    
    for (i=0; i<56; i++)
    {
        bit = (in[6-i/8]&(1<<(i%8))) > 0; /* Bit at bit position i */
        if (bit)
        {
            result[7-bit_pos/8] |= (1<<(bit_pos%8))&0xFF;
            bit_cnt++;
        }
/*
 * Every 7 bits, set the parity bit
 */
        if ((i+1) % 7 == 0)
        {
/*
 * If bit count is even, set the low bit to ensure parity
 */
            if (bit_cnt % 2 == 0)
                result[7-bit_pos/8] |= 1;
            bit_pos++;
            bit_cnt = 0;
        }
        bit_pos++;
    }
    return;
}
#endif
/*
 * turns a 56 bit key into the 64 bit, odd parity key and sets the key.
 * The key schedule ks is also set.
 */
void setup_des_key(unsigned char key_56[], DES_key_schedule * ks)
{
DES_cblock key;

#ifdef USE_BITWISE56
    gen64from56(key_56, &key);
#else
    key[0] = key_56[0];
    key[1] = ((key_56[0] << 7) & 0xFF) | (key_56[1] >> 1);
    key[2] = ((key_56[1] << 6) & 0xFF) | (key_56[2] >> 2);
    key[3] = ((key_56[2] << 5) & 0xFF) | (key_56[3] >> 3);
    key[4] = ((key_56[3] << 4) & 0xFF) | (key_56[4] >> 4);
    key[5] = ((key_56[4] << 3) & 0xFF) | (key_56[5] >> 5);
    key[6] = ((key_56[5] << 2) & 0xFF) | (key_56[6] >> 6);
    key[7] =  (key_56[6] << 1) & 0xFF;
    DES_set_odd_parity(&key);
#endif
    DES_set_key(&key, ks);
    return;
}
/*
 * takes a 21 byte array and treats it as 3 56-bit DES keys. The
 * 8 byte plaintext is encrypted with each key and the resulting 24
 * bytes are stored in the results array.
 */
void ntlm_calc_resp(keys, plaintext, results)
unsigned char *keys;
unsigned char *plaintext;
unsigned char *results;
{
DES_key_schedule ks;

    setup_des_key(keys, &ks);
    DES_ecb_encrypt((const_DES_cblock*) plaintext, (DES_cblock*) results,
                     &ks, DES_ENCRYPT);
    setup_des_key(keys+7, &ks);
    DES_ecb_encrypt((const_DES_cblock*) plaintext, (DES_cblock*) (results+8),
                     &ks, DES_ENCRYPT);
    setup_des_key(keys+14, &ks);
    DES_ecb_encrypt((const_DES_cblock*) plaintext, (DES_cblock*) (results+16),
                     &ks, DES_ENCRYPT);
    return;
}
/*
 * Create the MD4 password hash
 */
void ntlm_pass_hash(passw, passh) 
unsigned char * passw;
unsigned char * passh;
{
int i;
int len;
unsigned char  nt_pw[64];
unsigned char * ip;
unsigned char * op;
MD4_CTX context;

    len = strlen(passw);
    for (ip = passw, op = nt_pw, i = 0; i < len; i++)
    {
        *op++ = *ip++;
        *op++ = '\0';
    }
    MD4_Init(&context);
    MD4_Update(&context, nt_pw, 2*len);
    MD4_Final(passh, &context);
    memset(passh+16, 0, 5);
    return;
}
/*
 * Do NTLM 1 substitution
 *
 * Inputs are passw and nonce, the results are in lm_resp and nt_resp.
 * Response buffers must be unsigned char lm_resp[24], nt_resp[24];
 */
void ntlm_1_responses(passw, nonce, lm_resp, nt_resp)
unsigned char * passw;
unsigned char * nonce;
unsigned char * lm_resp;
unsigned char * nt_resp;
{
/*
 * setup LanManager password
 */
int i;
unsigned char magic[] = { 0x4B, 0x47, 0x53, 0x21, 0x40, 0x23, 0x24, 0x25 };
unsigned char lm_hpw[21];
char  lm_pw[14];
int   len = strlen(passw);
unsigned char nt_hpw[21];
DES_key_schedule ks;

    if (len > 14)
        len = 14;
    for (i = 0; i < len; i++)
        lm_pw[i] = toupper(passw[i]);
    for (; i < 14; i++)
        lm_pw[i] = 0;
/*
 * create LanManager hashed password
 */
    setup_des_key(lm_pw, &ks);
    DES_ecb_encrypt((const_DES_cblock *) magic,
                    (const_DES_cblock *) lm_hpw, &ks, DES_ENCRYPT);
    setup_des_key(lm_pw+7, &ks);
    DES_ecb_encrypt((const_DES_cblock *) magic,
                    (const_DES_cblock *) lm_hpw+8, &ks, DES_ENCRYPT);
    memset(lm_hpw+16, 0, 5);
/*
 * Create NT hashed password
 */
    ntlm_pass_hash(passw, nt_hpw); 
/*
 * Create responses
 */ 
    ntlm_calc_resp(lm_hpw, nonce, lm_resp);
    ntlm_calc_resp(nt_hpw, nonce, nt_resp);
    return;
}
/*
 * Allocate a unicode string
 */
unsigned char * ascdupuni(ip, cnt)
unsigned char * ip;
unsigned int cnt;         /* Count of UNICODE characters */
{
unsigned char * ret = (unsigned char *) malloc(cnt*2 + 2);
unsigned char * op = ret;

    while (cnt > 0)
    {
        *op++ = *ip++;
        *op++ = '\0';
        cnt--;
    }
    *op++ = '\0';
    *op++ = '\0';
    return ret;
}
#ifdef NTLM_V2
/***********************************************************************
 * This is not all here. We do not use this at the moment
 ***********************************************************************
 * Do MD5 hash
 *
 * Inputs are passw and nonce, the results are in lm_resp and nt_resp.
 * Response buffers must be unsigned char lm_resp[24], nt_resp[24];
 */
void ntlm_v2_hash(target, target_len, in_resp, out_resp)
unsigned char * target;   /* User concatenated with target from record */
int target_len;
unsigned char * in_resp;  /* MD4 password hash                         */
unsigned char * out_resp; /* MD5 hash                                  */
{
int md5_len = 16;

    HMAC(evp_md5, in_resp, 16, target, target_len, out_resp, &md5_len); 
    return;
}
/*
 * Don't assume 64 bit integer arithmetic available
 */
void get_blob_time(outp)
char * outp;
{
unsigned int lowt;
unsigned int hight;
double td = (11644473600000.0 +(((double) time(0)) * 1000.0))*10000.0;
                              /* 1/10 Microsecond since 1601 */
    hight = (long) (floor(td/4294967296.0));
    lowt = (long) (fmod(td, 4294967296.0));
    memcpy(outp, (unsigned char *) &lowt, 4);
    memcpy(outp + 4, (unsigned char *) &higt, 4);
    return;
}
/*
 * blob used in NTLMv2
 */
void init_blob(target, target_len, nonce, blob)
unsigned char * target;
int target_len;
unsigned char * nonce;
unsigned char * blob;
{
    memset(blob, 0, 32 + target_len);
    blob[0] = 1;
    blob[1] = 1;
    get_blob_time(&blob[8]);
    memcpy(&blob[16], nonce, 8);
    memcpy(&blob[24], target, target_len);
    return;
}
void asc2ucuni(op, ip, cnt)
unsigned char * op;
unsigned char * ip;
unsigned int cnt;         /* Count of UNICODE characters */
{
    while (cnt > 0)
    {
        if (islower(*ip))
            *op++ = toupper(*ip++);
        else
            *op++ = *ip++;
        *op++ = '\0';
        cnt--;
    }
    return;
}
/*
 * Concatenate elements needed for NTLM v2
 */
void init_target(target, target_len, user, outp)
{
unsigned char * ret_target;
int user_len;

    user_len = strlen(user);
    asc2ucuni(ret_target, user, user_len);
    memcpy(ret_target + 2 * user_len, target, target_len);
    return ret_target;
}
#endif
/*
 * Actually do it.
 *
 * We need to be able to:
 * -   Construct a type 1 record
 * -   Read a type 2 record, and make sense of it
 * -   Construct a type 3 record
 * -   Add the extra header for the first send on the socket, but not
 *     thereafter
 * We use the ability to validate all three record types to check that we have
 * constructed them accurately.
 */
int ntlm_construct_type1(host, dom, pbuf )
unsigned char * host;
unsigned char * dom;
unsigned char * pbuf;
{
struct ntlm_data test;
unsigned char buf[4096];
int host_len;
int dom_len;
int dom_off;
int len;

    memset(buf, 0, 40); 
    buf[0] = 'N';
    buf[1] = 'T';
    buf[2] = 'L';
    buf[3] = 'M';
    buf[4] = 'S';
    buf[5] = 'S';
    buf[6] = 'P';
    buf[7] = '\0';       /* Label */
    buf[8] = 1;          /* Type  */
    buf[12] = 0x7;
    buf[13] = 0xb2;
    buf[14] = 0x8;
    buf[15] = 0xa2;      /* Flags */
    host_len = strlen(host);
    dom_len = strlen(dom);
    buf[16] = (unsigned char) (dom_len & 0xff); 
    buf[17] = (unsigned char) ((dom_len >> 8) & 0xff);  /* dom_len */
    buf[18] = buf[16];
    buf[19] = buf[17];                                 /* dom_alloc */
    dom_off = 40 + host_len;
    buf[20] = (unsigned char) (dom_off & 0xff); 
    buf[21] = (unsigned char) ((dom_off >> 8) & 0xff);
    buf[22] = (unsigned char) ((dom_off >> 16) & 0xff);
    buf[23] = (unsigned char) ((dom_off >> 24) & 0xff);  /* dom_off */
    buf[24] = (unsigned char) (host_len & 0xff); 
    buf[25] = (unsigned char) ((host_len >> 8) & 0xff);  /* host_len */
    buf[26] = buf[24];
    buf[27] = buf[25];                                 /* host_alloc */
    buf[28] = 40;                                      /* host_off */
    buf[32] = 5;
    buf[33] = 2;
    buf[34] = 0xce;
    buf[35] = 0xe;
    buf[39] = 0xf;
    memcpy(&buf[40], host, host_len);
    memcpy(&buf[dom_off], dom, dom_len);
    len = b64enc(40 + host_len + dom_len, &buf[0], pbuf);
    pbuf[len] = '\0';
#ifdef DEBUG
    if (!ntlm_recognise(pbuf, len, &test)
     || 1 != test.rec_type
     || dom_off != test.dom_off
     || host_len != test.host_len
     || dom_len != test.dom_len)
        fprintf(stderr, "%s:%d Failed to decipher our record\n",
                __FILE__, __LINE__);
    clean_ntlm_data(&test);
#endif
    return len;
}
/*
 * Contruct the basic authentication record. It is actually just a base64
 * encoding of the user and password, separated by a colon.
 */
int basic_construct(user, password, pbuf )
unsigned char * user;
unsigned char * password;
unsigned char * pbuf;
{
int len;
unsigned char buf[150];

    len = sprintf(buf, "%-.64s:%-.64s", user, password);
    len = b64enc(len,  &buf[0], pbuf);
    pbuf[len] = '\0';
    return len;
}
/*
 * Recognise the challenge and produce the response
 */
int ntlm_construct_type3(host, dom, user, passw, client_nonce,
                          ibuf, ilen, pbuf )
unsigned char * host;
unsigned char * dom;
unsigned char * user;
unsigned char * passw;
unsigned char * client_nonce;
unsigned char * ibuf;    /* The Type 2 record, in Base 64 */ 
int ilen;                /* The Type 2 record length      */ 
unsigned char * pbuf;    /* Buffer where the output Base 64 ends up */
{
struct ntlm_data test;
unsigned char buf[4096];
int host_len;
int dom_len;
int user_len;
int user_off;
int host_off;
int lm_resp_off;
int nt_resp_off;
int session_off;
int len;
MD5_CTX context;
unsigned char lm_resp[24];
unsigned char nt_resp[24];
unsigned char lmv2_resp[24];
unsigned char ntv2_resp[24];
unsigned char ntarget[256];
unsigned char blob[256];
unsigned char * uhost;
unsigned char * udom;
unsigned char * uuser;

    if (!ntlm_recognise(ibuf, ilen, &test) || 2 != test.rec_type)
    {
        fprintf(stderr, "%s:%d Failed to decipher type 2 record\n",
                __FILE__, __LINE__);
        return 0;
    }
    if (client_nonce == NULL)
        client_nonce = test.nonce;
    host_len = strlen(host);
    uhost = ascdupuni(host, host_len);
    host_len += host_len;
    dom_len = strlen(dom);
    udom = ascdupuni(dom, dom_len);
    dom_len += dom_len;
    user_len = strlen(user);
    uuser = ascdupuni(user, user_len);
    user_len += user_len;
/*
 * NTLM2 Logic
 */
    if (test.flags & 0x80000)
    {
        memset(lmv2_resp, 0 , 24);
        memset(nt_resp, 0, 24);
        ntlm_pass_hash(passw, nt_resp);
/*
 * Concatenate server nonce and client nonce
 */
        memcpy(lmv2_resp, test.nonce, 8); 
        memcpy(lmv2_resp+ 8, client_nonce, 8); 
/*
 * Compute an MD5 hash of the concatenation, in ntv2_resp
 */
        MD5_Init(&context);
        MD5_Update(&context, lmv2_resp, 16);
        memset(ntv2_resp, 0 , 24);
        MD5_Final(ntv2_resp, &context);
/*
 * Now encrypt the first 8 bytes of this hash using successive 7 byte pieces of
 * the NT Password Hash
 */
        ntlm_calc_resp(nt_resp, ntv2_resp, lm_resp);
/*
 * What we have in lm_resp is actually what we want in nt_resp, so copy it over
 */
        memcpy(nt_resp, lm_resp, 24);
/*
 * Copy the client nonce into lm_resp
 */
        memset(lm_resp, 0 , 24);
        memcpy(lm_resp, client_nonce, 8); 
    }
    else
        ntlm_1_responses(passw, test.nonce, lm_resp, nt_resp);
    user_off = 72 + dom_len;
    host_off = user_off + user_len;
    lm_resp_off = host_off + host_len;
    nt_resp_off = lm_resp_off + 24;
    session_off = 120 + dom_len + user_len + host_len;
/*
 * Now set up the binary record
 */
    memset(buf, 0, 72); 
    buf[0] = 'N';
    buf[1] = 'T';
    buf[2] = 'L';
    buf[3] = 'M';
    buf[4] = 'S';
    buf[5] = 'S';
    buf[6] = 'P';
    buf[7] = '\0';       /* Label */
    buf[8] = 3;          /* Type  */
    buf[12] = 24;
    buf[14] = 24;        /* LM resp len */
    buf[16] = (unsigned char) (lm_resp_off & 0xff); 
    buf[17] = (unsigned char) ((lm_resp_off >> 8) & 0xff);  /* lm_resp_off */
    buf[20] = 24;
    buf[22] = 24;        /* NT resp len */
    buf[24] = (unsigned char) (nt_resp_off & 0xff); 
    buf[25] = (unsigned char) ((nt_resp_off >> 8) & 0xff);  /* nt_resp_off */

    buf[28] = (unsigned char) (dom_len & 0xff); 
    buf[29] = (unsigned char) ((dom_len >> 8) & 0xff);  /* dom_len */
    buf[30] = buf[28];
    buf[31] = buf[29];                                 /* dom_alloc */
    buf[32] = 72;                                      /* dom_off */
    buf[36] = (unsigned char) (user_len & 0xff); 
    buf[37] = (unsigned char) ((user_len >> 8) & 0xff);  /* user_len */
    buf[38] = buf[36];
    buf[39] = buf[37];                                 /* user_alloc */
    buf[40] = (unsigned char) (user_off & 0xff); 
    buf[41] = (unsigned char) ((user_off >> 8) & 0xff);     /* user_off */
    buf[44] = (unsigned char) (host_len & 0xff); 
    buf[45] = (unsigned char) ((host_len >> 8) & 0xff);     /* host_len */
    buf[46] = buf[44];
    buf[47] = buf[45];                                      /* host_alloc */
    buf[48] = (unsigned char) (host_off & 0xff); 
    buf[49] = (unsigned char) ((host_off >> 8) & 0xff);     /* host offset */
/*
 * No session allocation
 */
    buf[56] = (unsigned char) (session_off & 0xff); 
    buf[57] = (unsigned char) ((session_off >> 8) & 0xff);  /* session_off */
    buf[60] = 0x05;
    buf[61] = 0x82;
    buf[62] = 0x88;
    buf[63] = 0xa2;      /* Flags */
    buf[64] = 5;
    buf[65] = 2;
    buf[66] = 0xce;
    buf[67] = 0xe;
    buf[71] = 0xf;
    memcpy(&buf[72], udom, dom_len);
    memcpy(&buf[user_off], uuser, user_len);
    memcpy(&buf[host_off], uhost, host_len);
    memcpy(&buf[lm_resp_off], lm_resp, 24);
    memcpy(&buf[nt_resp_off], nt_resp, 24);
/*
 * Generate Base 64
 */
    len = b64enc(120 + dom_len + user_len + host_len, &buf[0], pbuf);
    pbuf[len] = '\0';
    clean_ntlm_data(&test);
#ifdef DEBUG
/*
 * Check the validity of the generated record
 */
    if (!ntlm_recognise(pbuf, len, &test)
     || 3 != test.rec_type
     || user_off != test.user_off
     || lm_resp_off != test.lm_resp_off
     || nt_resp_off != test.nt_resp_off
     || session_off != test.session_off
     || user_len != test.user_len
     || 72 != test.dom_off
     || host_len != test.host_len
     || dom_len != test.dom_len)
        fprintf(stderr, "%s:%d Failed to decipher our record\n",
                __FILE__, __LINE__);
    clean_ntlm_data(&test);
#endif
    free(uuser);
    free(udom);
    free(uhost);
    return len;
}
#ifdef STANDALONE
char *test_type1="TlRMTVNTUAABAAAAB7IIogQABAAwAAAACAAIACgAAAAFAs4OAAAAD0VEQy1OUzE1Uk1KTQ";
char *test_type2="TlRMTVNTUAACAAAACAAIADgAAAAFgomi4Ssnan5Sfl0AAAAAAAAAAHIAcgBAAAAABQLODgAAAA9SAE0ASgBNAAIACABSAE0ASgBNAAEAEABFAEQAQwAtAEQASwAwADMABAAQAFIATQBKAE0ALgBOAEUAVAADACIAZQBkAGMALQBkAGsAMAAzAC4AUgBNAEoATQAuAE4ARQBUAAUAEABSAE0ASgBNAC4ATgBFAFQAAAAAAA";
char *test_type3="TlRMTVNTUAADAAAAGAAYAHAAAAAYABgAiAAAAAgACABIAAAAEAAQAFAAAAAQABAAYAAAAAAAAACgAAAABYKIogUCzg4AAAAPUgBNAEoATQBwAGUAcgBmAHQAZQBzAHQARQBEAEMALQBOAFMAMQA1AJuenqDKL+8JAAAAAAAAAAAAAAAAAAAAAEj9bg4ORe07eVQDn71BSigo/cosHBIjVw"; 
/*
 * Main program
 */
int main(argc, argv) 
{
char buf[16384];
struct ntlm_data test;
char client_nonce[] = { 0x9b, 0x9e, 0x9e, 0xa0, 0xca, 0x2f, 0xef, 0x09 };

    ntlm_init();
    (void) ntlm_construct_type1("EDC-NS15", "RMJM", &buf[0] );
    if (strcmp(test_type1, buf))
    {
        fputs("Failed to duplicate type 1\n", stderr);
        fprintf(stderr, "Captured:\n%s\n", test_type1);
        fprintf(stderr, "Generated:\n%s\n", buf);
    }
    ntlm_construct_type3("EDC-NS15","RMJM","perftest","passw0rd1",
               client_nonce,
               test_type2, strlen(test_type2), &buf[0] );
    if (strcmp(test_type3, buf))
    {
        fputs("Failed to duplicate type 3\n", stderr);
        fprintf(stderr, "Captured:\n%s\n", test_type3);
        fprintf(stderr, "Generated:\n%s\n", buf);
    }
    exit(0);
}
#endif
