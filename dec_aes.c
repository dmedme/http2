#include <mcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#ifndef WIN32
#define O_BINARY 0
#define setmode(x,y)
#endif
extern int optind;
extern int opterr;
extern char * optarg;
extern int getopt();
static char * hlp = "dec_aes: Decrypt an AES-encoded stream\n\
Options\n\
-f arguments are files containing binary values\n\
-x arguments are hexadecimal strings (default, but files are fallback)\n\
-h output this string\n\
Arguments: Provide the AES key (32 bytes binary)\n\
and Initialisation Vector (IV; 16 bytes binary)\n\
Cipher text is on stdin, clear goes on stdout\n\
If options conflict, the last to appear prevails.\n";
/**************************************************************************
 * Render hexadecimal characters as a binary stream.
 */
static unsigned char * hexout(out, in, len)
unsigned char * out;
unsigned char *in;
int len;
{
unsigned char * top = out + len;
/*
 * Build up half-byte at a time, subtracting 48 initially, and subtracting
 * another 7 (to get the range from A-F) if > (char) 9. The input must be
 * valid.
 */
register unsigned char * x = out,  * x1 = in;

    while (x < top)
    {
    register unsigned char x2;

        x2 = *x1 - (char) 48;
        if (x2 > (char) 48)
           x2 -= (char) 32;    /* Handle lower case */
        if (x2 > (char) 9)
           x2 -= (char) 7; 
        *x = (unsigned char) (((int ) x2) << 4);
        x1++;
        if (*x1 == '\0')
            break;
        x2 = *x1++ - (char) 48;
        if (x2 > (char) 48)
           x2 -= (char) 32;    /* Handle lower case */
        if (x2 > (char) 9)
           x2 -= (char) 7; 
        *x++ |= x2;
    }
    return x;
}
/******************************************************************************
 * Check for hexadecimality; return the length if valid, 0 if not.
 */
static int hex_check(ip)
unsigned char * ip;
{
unsigned char * insp;

    for ( insp = ip; *insp != 0; insp++)
        if (!isxdigit(*insp))
        {
            fprintf(stderr,
              "Invalid hex digit (%c); %s is not a valid hexadecimal string.\n",
                        *insp, ip);
            return 0;
        }
    return (insp - ip);
}
/*****************************************************************************
 * Main program starts here
 * VVVVVVVVVVVVVVVVVVVVVVVV
 */
int main(argc, argv)
int argc;
char ** argv;
{
MCRYPT td;
int i;
char IV[16]; 
char key[32];
FILE * ifp;
char block_buffer[16];
int keysize=32; /* 256 bits */

int hex_flag = 1;
int n;
/*
 * Examine the input options
 */
    while ((i = getopt(argc, argv, "fhx")) != EOF)
    {
        switch (i)
        {
        case 'f':
            hex_flag = 0;
            break;
        case 'x':
            hex_flag = 1;
            break;
        case 'h':
        default:
            fputs( hlp, stderr);
            exit(1);
        }
    }
    if (argc - optind < 2)
    {
        fputs("Insufficient arguments\n", stderr);
        fputs(hlp, stderr);
        exit(1);
    }
    setmode(fileno(stdin), O_BINARY);
    setmode(fileno(stdout), O_BINARY);
/*
 * Now get keys
 */
    if (hex_flag)
    {
        if (hex_check(argv[optind]) == 2*sizeof(key))
        {
            hexout(key, argv[optind], sizeof(key));
            if (hex_check(argv[optind + 1]) == 2*sizeof(IV))
                hexout(IV, argv[optind+1], sizeof(IV));
            else
            {
                fputs("Hexadecimal IV invalid\n", stderr);
                fputs(hlp, stderr);
                exit(1);
            }
        }
        else
        {
            fputs("Hexadecimal key invalid, perhaps you meant -f?\n", stderr);
            hex_flag = 0;
        }
    }
    if (!hex_flag)
    {            
        if ((ifp = fopen(argv[optind], "rb")) == NULL)
        {
            perror(argv[optind]);
            fputs(hlp, stderr);
            exit(1);
        }
        if (fread(key, sizeof(char), sizeof(key), ifp) != sizeof(key))
        {
            fputs("Could not read correct key length (32)\n", stderr);
            fputs(hlp, stderr);
            exit(1);
        }
        fclose(ifp);
        if ((ifp = fopen(argv[optind + 1], "rb")) == NULL)
        {
            perror(argv[optind + 1]);
            fputs(hlp, stderr);
            exit(1);
        }
        if (fread(IV, sizeof(char), sizeof(IV),  ifp) != sizeof(IV))
        {
            fputs("Could not read correct IV length (16)\n", stderr);
            fputs(hlp, stderr);
            exit(1);
        }
        fclose(ifp);
    }
/*
 * Now mcrypt ...
 */
    td = mcrypt_module_open("rijndael-128", NULL, "cbc", NULL);
    if (td==MCRYPT_FAILED)
    {
        fputs("Failed to load rijndael-128 for CBC\n", stderr);
        exit(1);
    }
    if ((i = mcrypt_generic_init( td, key, keysize, IV)) < 0)
    {
        mcrypt_perror(i);
        exit(1);
    }
    while ( fread (&block_buffer[0], 1, sizeof(block_buffer), stdin) == sizeof(block_buffer) )
    {
/*
 * Comment below and uncomment this to encrypt
 *    mcrypt_generic (td, &block_buffer[0], sizeof(block_buffer));
 */
        mdecrypt_generic (td, &block_buffer[0], sizeof(block_buffer));
        fwrite ( &block_buffer, 1, sizeof(block_buffer), stdout);
    }
/*
 * Deinit the encryption thread, and unload the module
 */
    mcrypt_generic_end(td);
    exit(0);
}
