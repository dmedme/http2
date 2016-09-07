/*
 * Handle the decryption of the AES-128 key and IV, and the encryption of
 * the login block.
 */
#include <openssl/opensslconf.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <mcrypt.h>
#include "webdrive.h"
/*
 * Login arguments to be encrypted. It would be better to put this in the
 * script, and have the driver encrypt what it finds, a la racdrive, but
 * it would be very tedious to have to set up loads of users, so we will
 * take this approach to begin with.
 **************************************************************************
 * Note that the below includes the PKCS#7 padding (the 12 0x0c at the end).
 */
static unsigned char clear_login_args[] = {
  0x00, 0x01, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x02, 0x00, 0x00, 0x00, 'S' , 'C' ,
  'a' , 's' , 'M' , 'i' , 'd' , 'T' , 'i' , 'e' , 'r' , 'T' , 'y' , 'p' ,
  'e' , 's' , ',' , ' ' , 'V' , 'e' , 'r' , 's' , 'i' , 'o' , 'n' , '=' ,
  '1' , '2' , '.' , '1' , '.' , '4' , '.' , '0' , ',' , ' ' , 'C' , 'u' ,
  'l' , 't' , 'u' , 'r' , 'e' , '=' , 'n' , 'e' , 'u' , 't' , 'r' , 'a' ,
  'l' , ',' , ' ' , 'P' , 'u' , 'b' , 'l' , 'i' , 'c' , 'K' , 'e' , 'y' ,
  'T' , 'o' , 'k' , 'e' , 'n' , '=' , 'd' , '7' , '8' , 'd' , '2' , '7' ,
  '1' , 'c' , '3' , '2' , '1' , 'e' , '7' , '8' , 'e' , 'a' , 0x05, 0x01,
  0x00, 0x00, 0x00, '(' , 'C' , 'a' , 's' , '.' , 'C' , 'a' , 's' , 'F' ,
  'r' , 'a' , 'm' , 'e' , 'w' , 'o' , 'r' , 'k' , '.' , 'M' , 'i' , 'd' ,
  'T' , 'i' , 'e' , 'r' , '.' , 'T' , 'y' , 'p' , 'e' , 's' , '.' , 'L' ,
  'o' , 'g' , 'o' , 'n' , 'A' , 'r' , 'g' , 's' , 0x04, 0x00, 0x00, 0x00,
  0x09, 'l' , 'o' , 'g' , 'o' , 'n' , 'N' , 'a' , 'm' , 'e' , 0x08, 'p' ,
  'a' , 's' , 's' , 'w' , 'o' , 'r' , 'd' , 0x0a, 'c' , 'l' , 'i' , 'e' ,
  'n' , 't' , 'T' , 'y' , 'p' , 'e' , 0x0e, 'e' , 'x' , 't' , 'e' , 'r' ,
  'n' , 'a' , 'l' , 'U' , 's' , 'e' , 'r' , 'I' , 'd' , 0x01, 0x01, 0x04,
  0x01, ')' , 'C' , 'a' , 's' , '.' , 'C' , 'a' , 's' , 'F' , 'r' , 'a' ,
  'm' , 'e' , 'w' , 'o' , 'r' , 'k' , '.' , 'M' , 'i' , 'd' , 'T' , 'i' ,
  'e' , 'r' , '.' , 'T' , 'y' , 'p' , 'e' , 's' , '.' , 'C' , 'l' , 'i' ,
  'e' , 'n' , 't' , 'T' , 'y' , 'p' , 'e' , 0x02, 0x00, 0x00, 0x00, 0x02,
  0x00, 0x00, 0x00, 0x06, 0x03, 0x00, 0x00, 0x00, 0x0d, 'a' , 'd' , 'm' ,
  'i' , 'n' , 'i' , 's' , 't' , 'r' , 'a' , 't' , 'o' , 'r' , 0x06, 0x04,
  0x00, 0x00, 0x00, 0x07, 's' , 'y' , 'd' , 'n' , 'e' , 'y' , '1' , 0x05,
  0xfb, 0xff, 0xff, 0xff, ')' , 'C' , 'a' , 's' , '.' , 'C' , 'a' , 's' ,
  'F' , 'r' , 'a' , 'm' , 'e' , 'w' , 'o' , 'r' , 'k' , '.' , 'M' , 'i' ,
  'd' , 'T' , 'i' , 'e' , 'r' , '.' , 'T' , 'y' , 'p' , 'e' , 's' , '.' ,
  'C' , 'l' , 'i' , 'e' , 'n' , 't' , 'T' , 'y' , 'p' , 'e' , 0x01, 0x00,
  0x00, 0x00, 0x07, 'v' , 'a' , 'l' , 'u' , 'e' , '_' , '_' , 0x00, 0x08,
  0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x06, 0x00, 0x00,
  0x00, 0x09, 'd' , 'a' , 'n' , '.' , 'w' , 'h' , 'i' , 't' , 'e' , 0x0b,
  0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c
};
static unsigned int clear_login_args_len = 384;
#ifdef STANDALONE
unsigned char encrypted_login_block[] = {
0x80,0x3D,0x17,0xDD,0xC7,0x27,0x3D,0x82,0x60,
0x80,0xCA,0xA4,0xD7,0xF4,0x87,0x1D,0xCD,0x5D,0xB9,0xE6,0xCB,0x1E,0xA0,0x21,0xEC,0xC8,0x27,0xF5,0x9C,0x91,0x1C,0xF4,0xFC,0x68,0xC3,0xD4,0x3C,0x06,0x34,
0xE7,0x94,0x86,0x7F,0x7A,0x74,0xC8,0x49,0x8B,0xBC,0x3C,0x75,0x96,0x22,0x3C,0x8E,0x33,0x9D,0xD3,0x64,0xFD,0x07,0x37,0xE4,0x6D,0x17,0x3D,0xFD,0x5A,0xB7,
0x4E,0x24,0xD6,0x11,0x3E,0xEB,0xEA,0x6B,0xCE,0xC6,0x30,0x50,0x1F,0xC9,0xD7,
'h','k','G','w',
0xC2,0xD3,0xD6,0x6F,0x23,0x74,0xB0,0xDF,0x4D,0x72,0xC5,0x52,0xF7,0x15,0xC7,0x61,0x9B,0x17,0x4F,0x7F,0x81,0xB8,0x70,0xD8,0x51,0x99,0x87,0xEA,0xED,0xCC,
0xC5,0xC5,0x83,0x56,0x51,0x1E,0x5A,0x7F,0x3B,0x53,0xD5,0x53,0x06,0x9E,0x5E,0x07,0x67,0x67,0x98,0x13,0x8C,0xF2,0x37,0xF9,0x8A,0xE2,0xAF,0xE9,0xD5,0xF8,
']','a','x',
0x0D,
'o',
0x0B,0x32,0xD1,0xE4,0x67,0x1F,0x8C,0xB6,0x8A,0xA5,0x24,0x00,0x3D,0xCA,0x92,0x94,0x7F,0x7E,0x67,0x10,0xD3,0x5B,0xA6,0x79,0x08,0x71,0xD5,0xDA,0xA5,0x68,
0xAF,0x90,0x90,0x9D,0x32,0xBC,0x2D,0x5B,0x3D,0xDD,0x16,0x41,0xB7,0xB4,0xC7,
'=','{','t',')',
0xA5,0x13,0x8D,0xA0,0x36,0x27,0x21,0xA3,0x97,0xED,0xB4,0xDA,0x47,0xBD,0xCC,0xFD,0xC7,0xB9,0x78,0x45,0xAB,0x5A,0x7E,0x15,0x3A,0x10,0xA6,0x64,0x96,0x04,
0x94,0xF9,0x1B,0x68,0x48,0xF6,0x2F,0xB5,0x15,0xA4,0x89,0xA5,0x72,0xA7,0xBF,0x22,0x15,0x09,0x3D,0x62,0x84,0xAC,0xCB,0x49,0x8E,0xC8,0x81,0xD4,0x20,0x63,
0x97,0x01,0x56,0xB4,0xCE,0xC1,0x20,0xB5,0x80,0x87,0xFC,0xA2,0x2F,0xE8,0x8B,0x43,0xE3,0x07,0x1B,0xF0,0xF0,0xD7,0x39,0x12,0xED,0x9A,0x03,0x8C,0x15,0x59,
0x71,0x44,0x16,0x31,0x47,0x73,0xD3,0xE3,0xDF,0x01,0xED,0xD3,0x13,0xE7,0x26,0xFD,0xF0,0xCA,0xFE,0x36,0x48,0x63,0xFB,0x87,0x0F,0xBB,0x6A,0x59,0xC7,0xD7,
0x17,0x90,0xC5,0xF6,0x83,0x35,0x22,0xF5,0xC1,0x9D,0xA5,0xCF,0xAD,0xBA,0xB9,0xD7,0x7E,0xF5,0x5E,0xDD,0x0F,0x46,0xE5,0x0A,0x2F,0x7C,0xEA,0x8D,0x49,0x47,
0x83,0xB5,0x2E,0x0F,0x19,0x22,0xD6,0x7C,0xDD,0x80,0x11,0x95,0x74,0x3A,0x60,0xC9}
;
unsigned char session_key[] ={
0x0C,0xBE,0xEB,0xB2,0xD8,0xAF,0xA8,0x18,0x05,0x81,0xDC,0x5A,0xC5,0x08,0x1C,0x05,0x6F,0x0F,0x88,0x6C,0x59,0xF2,0x4C,0x9B,0x1B,0x22,0xEF,0xEB,0xB8,0xBF,0x85,0xD6};
unsigned char session_iv[] = {0x10,0x28,0x33,0x49,0x96,0x83,0xD0,0xAA,0xDA,0x60,0x37,0x3A,0x10,0xCE,0x9C,0x96};
/*
 * Generic aes_decryption with mcrypt
 */
int aes_decryption(outp, inp, len, keyp, ivp)
unsigned char * outp;
unsigned char * inp;
int len;
unsigned char * keyp;
unsigned char * ivp;
{
MCRYPT td;
int i;
int block_size = 16; /* 128 bits */
int keysize=32;      /* 256 bits */
/*
 * Now mcrypt ...
 */
#ifndef STANDALONE
    pthread_mutex_lock(&(webdrive_base.encrypt_mutex));
#endif
    td = mcrypt_module_open("rijndael-128", NULL, "cbc", NULL);
    if (td == MCRYPT_FAILED)
    {
        fputs("Failed to load rijndael-128 for CBC\n", stderr);
#ifndef STANDALONE
        pthread_mutex_unlock(&(webdrive_base.encrypt_mutex));
#endif
        return 0;
    }
    if ((i = mcrypt_generic_init( td, keyp, keysize, ivp)) < 0)
    {
        mcrypt_perror(i);
#ifndef STANDALONE
        pthread_mutex_unlock(&(webdrive_base.encrypt_mutex));
#endif
        return 0;
    }
    while (len > block_size)
    {
        memcpy(outp, inp, block_size);
        len -= block_size;
        inp += block_size;
        mdecrypt_generic (td, outp, block_size);
        outp += block_size;
    }
/*
 * Deinit the encryption thread, and unload the module
 */
    mcrypt_generic_deinit(td);
    mcrypt_module_close(td);
#ifndef STANDALONE
    pthread_mutex_unlock(&(webdrive_base.encrypt_mutex));
#endif
    return 1;
}
main()
{
int len;
char buf[4096];

    puts("Finding Original Logon Args");
    aes_decryption(buf, encrypted_login_block, sizeof(encrypted_login_block),
              session_key, session_iv);
    gen_handle(stdout, buf, buf + sizeof(encrypted_login_block), 1);
    putchar('\n');
    exit(0);
}
#endif
/*
 * Generic aes_encryption with mcrypt
 */
int aes_encryption(outp, inp, len, keyp, ivp)
unsigned char * outp;
unsigned char * inp;
int len;
unsigned char * keyp;
unsigned char * ivp;
{
MCRYPT td;
int i;
int block_size = 16; /* 128 bits */
int keysize=32;      /* 256 bits */
/*
 * Now mcrypt ...
 */
#ifndef STANDALONE
    pthread_mutex_lock(&(webdrive_base.encrypt_mutex));
#endif
    td = mcrypt_module_open("rijndael-128", NULL, "cbc", NULL);
    if (td == MCRYPT_FAILED)
    {
        fputs("Failed to load rijndael-128 for CBC\n", stderr);
#ifndef STANDALONE
        pthread_mutex_unlock(&(webdrive_base.encrypt_mutex));
#endif
        return 0;
    }
    if ((i = mcrypt_generic_init( td, keyp, keysize, ivp)) < 0)
    {
        mcrypt_perror(i);
#ifndef STANDALONE
        pthread_mutex_unlock(&(webdrive_base.encrypt_mutex));
#endif
        return 0;
    }
    while (len > block_size)
    {
        memcpy(outp, inp, block_size);
        len -= block_size;
        inp += block_size;
/*
 * Uncomment this and comment below to decrypt
 *      mdecrypt_generic (td, outp, block_size);
 */
        mcrypt_generic (td, outp, block_size);
        outp += block_size;
    }
/*
 * Pad the last block, according to PKCS#7
 */
    if (len > 0)
    {
        memcpy(outp, inp, len);
        for (inp = outp + len, len = block_size - len;
                  inp < outp + block_size; inp++)
             *inp = (unsigned char) len;
        mcrypt_generic (td, outp, block_size);
    }
/*
 * Deinit the encryption thread, and unload the module
 */
    mcrypt_generic_deinit(td);
    mcrypt_module_close(td);
#ifndef STANDALONE
    pthread_mutex_unlock(&(webdrive_base.encrypt_mutex));
#endif
    return 1;
}
/*
 * Encrypt the logon arguments
 */
void provide_logon_args(outp, keyp, ivp)
unsigned char * outp;
unsigned char * keyp;
unsigned char * ivp;
{
    aes_encryption(outp, clear_login_args, clear_login_args_len, keyp, ivp);
    return;
}
/*
 * Decrypt the Session Key or the Session IV
 */
int rsa_decryption(outp, inp, in_len)
unsigned char * outp;
unsigned char * inp;
int in_len;
{
char *keyfile = "rsa_private.txt";
BIO *keyb;
BIO *berr;
EVP_PKEY *pkey;
RSA *rsa;
int out_len;

#ifndef STANDALONE
    pthread_mutex_lock(&(webdrive_base.encrypt_mutex));
#endif
    berr = BIO_new_fp(stderr, BIO_NOCLOSE);
    OPENSSL_load_builtin_modules();
    if (CONF_modules_load(NULL, NULL, 0) <= 0)
    {
        ERR_print_errors(berr);
        fputs("Error configuring OpenSSL\n", stderr);
#ifndef STANDALONE
        pthread_mutex_unlock(&(webdrive_base.encrypt_mutex));
#endif
        return -1;
    }
    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();
    keyb = BIO_new(BIO_s_file());
    if (BIO_read_filename(keyb, keyfile) <= 0)
    {
        ERR_print_errors(berr);
        fprintf(stderr, "Failed to read keyfile %s\n", keyfile);
#ifndef STANDALONE
        pthread_mutex_unlock(&(webdrive_base.encrypt_mutex));
#endif
        return -1;
    }
    pkey = PEM_read_bio_PrivateKey(keyb, NULL, NULL, NULL);
    BIO_set_close(keyb, BIO_CLOSE);
    BIO_free(keyb);
    rsa = EVP_PKEY_get1_RSA(pkey);
    EVP_PKEY_free(pkey);
    if (!rsa)
    {
        fputs("Failed to get RSA private key\n", stderr);
        ERR_print_errors(berr);
#ifndef STANDALONE
        pthread_mutex_unlock(&(webdrive_base.encrypt_mutex));
#endif
        return -1;
    }
    if ((out_len = RSA_private_decrypt(in_len, inp, outp, rsa,
                    RSA_PKCS1_PADDING)) <= 0)
    {
        fputs("RSA operation error\n", stderr);
        ERR_print_errors(berr);
    }
    RSA_free(rsa);
#ifndef STANDALONE
    pthread_mutex_unlock(&(webdrive_base.encrypt_mutex));
#endif
    return out_len;
}
