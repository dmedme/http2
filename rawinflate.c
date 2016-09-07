#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#ifdef WIN32
#include <fcntl.h>
#include <io.h>
#endif
struct element_tracker {
    int len;
    unsigned char * element;
};
/*
 * Decompress input.
 */
int inf_open(hp, decoded, b)
z_stream * hp;
struct element_tracker * decoded; 
unsigned char b;
{
/*
 * Allocate inflate state
 */ 
    hp->zalloc = Z_NULL;
    hp->zfree = Z_NULL;
    hp->opaque = Z_NULL;
    hp->avail_in = 0;
    hp->next_in = Z_NULL;
    if ((decoded->element = (unsigned char *) malloc(65536)) == NULL)
    {
        fputs("Out of Memory; bad things are about to happen\n", stderr);
        fflush(stderr);
    }
    decoded->len = 0;
    if ((b & 0xf) == Z_DEFLATED)
        return inflateInit(hp);
    else
        return inflateInit2(hp, -MAX_WBITS);
}
int inf_block(hp, from_wire, decoded)
z_stream * hp;
struct element_tracker * from_wire; 
struct element_tracker * decoded; 
{
int ret;
int beg = 0;
int to_decompress = from_wire->len;
int declen = 65536;

    if (decoded->len == -1)
    {
/*
 * If we have a gzip header, skip over it. Note that we are only testing the
 * first byte of the two ID bytes; we are not checking for the presence of
 * optional gzip header elements, nor are we determining that the data when
 * we encounter it is going to be deflate data.
 */ 
        if ( from_wire->element[0] == 0x1f)
        {
            beg = 10;
            to_decompress -= 10;
        }
        if (to_decompress < 1)
        {
            decoded->element = NULL; /* Not yet allocated */
            decoded->len = 0;
            return -1;
        }
        inf_open(hp, decoded, from_wire->element[beg]);
    }
    hp->next_in = &from_wire->element[beg];
    hp->avail_in = to_decompress;
    hp->avail_out = declen;
    hp->next_out = decoded->element;
restart:
    ret = inflate(hp, Z_NO_FLUSH);
    switch (ret)
    {
    case Z_NEED_DICT:
        ret = Z_DATA_ERROR;     /* and fall through */
    case Z_DATA_ERROR:
    case Z_MEM_ERROR:
        (void)inflateEnd(hp);
        free(decoded->element);
        decoded->element = NULL;
        decoded->len = 0;
        return ret;
    }
    if (hp->avail_out == 0 && hp->avail_in > 0)
    {
        fprintf(stderr, "Ran out of space for decompression, %d unprocessed\n",
                hp->avail_in);
        hp->avail_out = 20 * hp->avail_in;
        decoded->element = realloc(decoded->element, declen +
                   hp->avail_out);
        hp->next_out = decoded->element + declen;
        declen += hp->avail_out;
        goto restart;
    }
    decoded->len = declen - hp->avail_out;
    return ret;
}
int inf_close(hp, decoded)
z_stream * hp;
struct element_tracker * decoded; 
{
    if (decoded->element != NULL)
        free(decoded->element);
    (void)inflateEnd(hp);
    return 0;
}
int main(argc, argv)
int argc;
char ** argv;
{
struct element_tracker decoded; 
struct element_tracker from_wire; 
z_stream inflate;

#ifdef WIN32
    setmode(fileno(stdin), O_BINARY);
    setmode(fileno(stdout), O_BINARY);
#endif
    decoded.len = -1;
    decoded.element = NULL;
    from_wire.element = (char *) malloc(65536);

    while (( from_wire.len = fread(&from_wire.element[0], sizeof(char), 65536, stdin)) > 0)
    {
        if (inf_block(&inflate, &from_wire, &decoded) < 0)
            break;
        fwrite(&decoded.element[0], sizeof(char), decoded.len, stdout);
        decoded.len = 0; 
    }
    free(from_wire.element);
    if (decoded.len != -1)
        inf_close(&inflate, &decoded);
    exit(0);
}
