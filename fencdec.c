/*************************************************************************
 * Encode/Decode Forms 9 Encrypted Traffic
 *
 * Two public routines, one to set up the encrypt/decrypt control block
 * from the ORACLE-available key components, and the other to do the encryption/
 * decryption.
 *
 * I believe this is actually RC4.
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 2005";
#include <stdio.h>
#include <malloc.h>
struct enc_dec_con {
unsigned char seed[256];
int el1;
int el2;
};
/*
 * Allocate a new encryption control structure.
 */
static struct enc_dec_con * set_key(int len, unsigned char * key)
{
int i, j, k, l;
struct enc_dec_con * edc = (struct enc_dec_con *)
                              malloc(sizeof(struct enc_dec_con));

    for (i = 0; i < sizeof(edc->seed); i++)
        edc->seed[i] = i;
    
/*
 * Scramble the seed buffer order, using the key
 */
    for (j = k = l = 0; j < 256; j++)
    {
        l += (key[k] + edc->seed[j]);
        if (l >= 256)
            l -= 256;
        if (l >= 256)
            l -= 256;
        i =  edc->seed[j];
        edc->seed[j] = edc->seed[l];
        edc->seed[l] = i;
        k++;
        if (k >= len)
            k = 0;
    }
/*
 * Reset the initial element values
 */
    edc->el1 = 0;
    edc->el2 = 0;
    return edc;
}
/*
 * Encrypt or Decrypt a block (it's the same operation, because
 * (a ^ b) ^ b = a). Make ip and op the same to update in place.
 */
void block_enc_dec( char * opaque, char * op, char * ip, int len)
{
int i;
struct enc_dec_con * edc = ( struct enc_dec_con * ) opaque;
int el1 = edc->el1;
int el2 = edc->el2;
#ifdef DEBUG
int j = len;

    fprintf(stderr, "INPUT Control: %x coding %d\n",
             (unsigned int) opaque, len);
    gen_handle(stderr, ip, ip + len, 1);
#endif
    for (; len; len--, ip++, op++)
    {
        el1++;
        if (el1 >= sizeof(edc->seed))
            el1 = 0;
        el2 += edc->seed[el1];
        if (el2 >= 256)
            el2 -= 256;
        i = edc->seed[el1];
        edc->seed[el1] =  edc->seed[el2];
        edc->seed[el2] = i;

        i += edc->seed[el1];
        if (i >= 256)
            i -= 256;
        *op = (*ip) ^ edc->seed[i];
    }
    edc->el1 = el1;
    edc->el2 = el2;
#ifdef DEBUG
    fprintf(stderr, "OUTPUT Control: %x coding %d\n",
             (unsigned int) opaque, j);
    gen_handle(stderr, op - j, op, 1);
#endif
    return;
}
/****************************************************************************
 * The input parameters are pointers to the byte beyond GDay, and the byte
 * beyond Mate.
 */
unsigned char * ready_code( unsigned char * gday, unsigned char * mate)
{
unsigned char key[5];

    key[0] = gday[2];
    key[1] =  (mate[2] << 4) | (mate[3] >> 4);
    key[2] = 0xae;
    key[3] = gday[1];
    key[4] = (mate[1] << 4) | (mate[2] >> 4);
    return (unsigned char *) set_key(sizeof(key), key);
} 
