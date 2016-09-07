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
 * Compress input.
 */
int def_open(hp, encoded)
z_stream * hp;
struct element_tracker * encoded; 
{
/*
 * Allocate deflate state
 */ 
    hp->zalloc = Z_NULL;
    hp->zfree = Z_NULL;
    hp->opaque = Z_NULL;
    hp->avail_in = 0;
    hp->next_in = Z_NULL;
    if ((encoded->element = (unsigned char *) malloc(65536)) == NULL)
    {
        fputs("Out of Memory; bad things are about to happen\n", stderr);
        fflush(stderr);
    }
    encoded->len = 0;
    return deflateInit2(hp, -1, Z_DEFLATED, 15, 9, Z_DEFAULT_STRATEGY);
}
int def_block(hp, from_wire, encoded, flush)
z_stream * hp;
struct element_tracker * from_wire; 
struct element_tracker * encoded; 
int flush;
{
int ret;
int enclen = 65536;

    if (encoded->len == -1)
        def_open(hp, encoded);
    hp->next_in = &from_wire->element[0];
    hp->avail_in = from_wire->len;
    hp->avail_out = enclen;
    hp->next_out = encoded->element;
restart:
    ret = deflate(hp, flush);
    switch (ret)
    {
    case Z_STREAM_ERROR:
    case Z_MEM_ERROR:
        (void)deflateEnd(hp);
        free(encoded->element);
        encoded->element = NULL;
        encoded->len = 0;
        return ret;
    }
    if (hp->avail_out == 0 && hp->avail_in > 0)
    {
        fprintf(stderr, "Ran out of space for compression, %d unprocessed\n",
                hp->avail_in);
        hp->avail_out = 20 * hp->avail_in;
        encoded->element = realloc(encoded->element, enclen +
                   hp->avail_out);
        hp->next_out = encoded->element + enclen;
        enclen += hp->avail_out;
        goto restart;
    }
    encoded->len = enclen - hp->avail_out;
    return ret;
}
int def_close(hp, encoded)
z_stream * hp;
struct element_tracker * encoded; 
{
    if (encoded->element != NULL)
        free(encoded->element);
    (void)deflateEnd(hp);
    return 0;
}
int main(argc, argv)
int argc;
char ** argv;
{
struct element_tracker encoded; 
struct element_tracker from_wire; 
z_stream deflate;

#ifdef WIN32
    setmode(fileno(stdin), O_BINARY);
    setmode(fileno(stdout), O_BINARY);
#endif
    encoded.len = -1;
    encoded.element = NULL;
    from_wire.element = (char *) malloc(65536);

    while (( from_wire.len = fread(&from_wire.element[0], sizeof(char), 65536, stdin)) > 0)
    {
        if (def_block(&deflate, &from_wire, &encoded, feof(stdin) ? Z_FINISH : Z_NO_FLUSH) < 0)
            break;
        fwrite(&encoded.element[0], sizeof(char), encoded.len, stdout);
        encoded.len = 0; 
    }
    free(from_wire.element);
    if (encoded.len != -1)
        def_close(&deflate, &encoded);
    exit(0);
}
