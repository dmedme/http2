/*
 *    sublib.c - Routines to handle stream scanning and substitution for
 *    t3drive
 *
 *    Copyright (C) E2 Systems 1993, 2000
 *
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995";
#include "webdrive.h"
#include <assert.h>
static pthread_t zeroth;
/****************************************************************************
 * Dump out pattern spec.
 */
void dump_pattern_spec(ofp, psp)
FILE * ofp;
PATTERN_SPEC * psp;
{
int i;

    fprintf(ofp,":PATTERN_SPEC:%d", psp->cnt);
    for (i = 0; i < psp->cnt;i++)
    {
        fputc(':', ofp);
        gen_handle_nolf(ofp,psp->bmp[i]->match_word,psp->bmp[i]->match_word
                  + psp->bmp[i]->match_len, 1);
    }
    return;
}
/*
 * Reset the current tracker for progressive scans
 */
void reset_progressive(wdbp)
WEBDRIVE_BASE * wdbp;
{
int i;

    for (i = 0; i < wdbp->scan_spec_used; i++)
    {
         wdbp->scan_spec[i]->ebp.curr = 0;
         wdbp->scan_spec[i]->rbp.curr = 0;
    }
    return;
}
/****************************************************************************
 * Dump out a scan spec.
 */
void dump_scan_spec(ofp, ssp)
FILE * ofp;
SCAN_SPEC * ssp;
{
int i;

    fprintf(ofp,"SCAN_SPEC:%c:(%s)", ssp->c_e_r_o_flag[0], ssp->scan_key);
    if ( ssp->c_e_r_o_flag[0] != 'S'
      && ssp->c_e_r_o_flag[0] != 'Y'
      && ssp->c_e_r_o_flag[0] != 'R'
      && ssp->c_e_r_o_flag[0] != 'F'
      && ssp->c_e_r_o_flag[0] != 'T')
    {
        dump_pattern_spec(ofp, &(ssp->ebp));
        if (ssp->c_e_r_o_flag[0] != 'A'
          && ssp->c_e_r_o_flag[0] != 'B'
          && ssp->c_e_r_o_flag[0] != 'D'
          && ssp->c_e_r_o_flag[0] != 'E'
          && ssp->c_e_r_o_flag[0] != 'G'
          && ssp->c_e_r_o_flag[0] != 'W'
          && ssp->c_e_r_o_flag[0] != 'P')
        {
            fprintf(ofp,":%d:%d", ssp->i_offset, ssp->i_len);
            if (ssp->i_cust[0] != 0)
            {
                fprintf(ofp,"(custom:%u:", (unsigned int) ssp->i_cust[0]);
                for (i = 1; i <= ssp->i_cust[0]; i++)
                    fprintf(ofp,"%02x", (unsigned int) ssp->i_cust[i]);
                fputc(')', ofp);
            }
            dump_pattern_spec(ofp, &(ssp->rbp));
            fprintf(ofp,":%d:%d:", ssp->o_offset, ssp->o_len);
            if (ssp->o_cust[0] != 0)
            {
                fprintf(ofp,"(custom:%u:", (unsigned int) ssp->o_cust[0]);
                for (i = 1; i <= ssp->o_cust[0]; i++)
                    fprintf(ofp,"%02x", (unsigned int) ssp->o_cust[i]);
                fputc(')', ofp);
            }
            if (ssp->encrypted_token == (char *) NULL)
                fputs( "(Not Yet Seen)", ofp);
            else
            if (ssp->i_len == 0)
                fputs(ssp->encrypted_token, ofp);
            else
                gen_handle_nolf(ofp, ssp->encrypted_token,
                                     ssp->encrypted_token +
                  ((ssp->c_e_r_o_flag[0] == 'H') ? ssp->o_len : ssp->i_len), 1);
        }
    }
    fputc('\n', ofp);
    return;
}
/*
 * Scan_spec hash function
 */
unsigned hash_func(x, modulo)
unsigned char * x;
int modulo;
{
    return(string_hh( ((SCAN_SPEC *) x)->scan_key,
                             modulo) & (modulo-1));
}
/*
 * Scan_spec hash comparison function
 */
int comp_func(x1, x2)
unsigned char * x1;
unsigned char * x2;
{
    return strcmp( ((SCAN_SPEC *) x1)->scan_key,
              ((SCAN_SPEC *) x2)->scan_key);
}
/*
 * Initialise the scan_spec structure. A model is used.
 * ****************************************************
 * DO NOT CALL THIS IF THE VALUE IS ALREADY HASHED.
 * ****************************************************
 */
SCAN_SPEC * new_scan_spec(wdbp, model )
WEBDRIVE_BASE * wdbp;
SCAN_SPEC * model;
{
SCAN_SPEC * x;
int i;

    if ((x = (SCAN_SPEC *) calloc( 1, sizeof(SCAN_SPEC)))
          == (SCAN_SPEC *) NULL)
        return x;
    x->record_type = SCAN_SPEC_TYPE;
    strcpy(x->scan_key, model->scan_key);
    strcpy(x->c_e_r_o_flag, model->c_e_r_o_flag);
    memcpy(&(x->ebp), &(model->ebp), sizeof(x->ebp));
    if (x->c_e_r_o_flag[0] != 'A'
     && x->c_e_r_o_flag[0] != 'B'
     && x->c_e_r_o_flag[0] != 'G'
     && x->c_e_r_o_flag[0] != 'E'
     && x->c_e_r_o_flag[0] != 'D'
     && x->c_e_r_o_flag[0] != 'W'
     && x->c_e_r_o_flag[0] != 'P')
    {
        x->i_offset = model->i_offset;
        x->i_len = model->i_len;
        memcpy(x->i_cust, model->i_cust, sizeof(model->i_cust));
        x->o_offset = model->o_offset;
        x->o_len = model->o_len;
        memcpy(x->o_cust, model->o_cust, sizeof(model->o_cust));
        x->frozen = model->frozen;
        memcpy(&(x->rbp), &(model->rbp), sizeof(x->rbp));
    }
    insert(wdbp->ht,x,x);
    return x;
} 
/*
 * Routine called to initialise 'internal' scan_specs used for ORACLE WebForms
 * etc. These must be called first, because they don't get deleted by the 
 * reset processing.
 */
void internal_scan_spec(wdbp, c_e_r_o_flag, in_marker, in_offset, out_marker,
             out_offset)
WEBDRIVE_BASE * wdbp;
unsigned char * c_e_r_o_flag;
unsigned char * in_marker;
int in_offset;
unsigned char * out_marker;
int out_offset;
{
SCAN_SPEC setup_spec;
SCAN_SPEC *sp;

    setup_spec.ebp.cnt = 1;
    setup_spec.ebp.curr = 0;
    setup_spec.ebp.bmp[0] = bm_compile(in_marker);
    setup_spec.ebp.bfp[0] = bm_frag_init(setup_spec.ebp.bmp[0], bm_match_new);
    strcpy(setup_spec.c_e_r_o_flag, c_e_r_o_flag);
    strcpy(setup_spec.scan_key, in_marker);
    if (*c_e_r_o_flag != 'A'
      && *c_e_r_o_flag != 'B'
      && *c_e_r_o_flag != 'E'
      && *c_e_r_o_flag != 'P'
      && *c_e_r_o_flag != 'G'
      && *c_e_r_o_flag != 'W'
      && *c_e_r_o_flag != 'D')
    {
        setup_spec.i_offset = in_offset;
        setup_spec.i_len = 0;
        memset(&setup_spec.i_cust[0], 0, sizeof(setup_spec.i_cust));
        setup_spec.rbp.cnt = 1;
        setup_spec.rbp.curr = 0;
        setup_spec.rbp.bmp[0] = bm_compile(out_marker);
        setup_spec.rbp.bfp[0] = bm_frag_init(setup_spec.rbp.bmp[0],
                                             bm_match_new);
        setup_spec.o_offset = out_offset;
        setup_spec.o_len = 0;
        memset(&setup_spec.o_cust[0], 0, sizeof(setup_spec.o_cust));
        setup_spec.frozen = 0;
    }
    sp = new_scan_spec(wdbp, &setup_spec);
    activate_scan_spec(wdbp, sp);
    wdbp->scan_spec_internal++;
    return;
}
/*
 * Free a pattern spec.
 */
static void zap_pattern_spec(psp)
PATTERN_SPEC * psp;
{
int i;

    for (i = 0; i < psp->cnt; i++)
    {
        bm_zap(psp->bmp[i]);
        bm_frag_zap(psp->bfp[i]);
    }
    return;
}
/*
 * Update the scan_spec structure
 *
 * This routine does nothing with the encrypted_token value. This
 * is by design. It makes it possible to substitute the search string
 * whilst in mid-substitution.
 */
void update_scan_spec( to,  from)
SCAN_SPEC * to;
SCAN_SPEC * from;
{
    to->record_type = from->record_type;
    if (to->c_e_r_o_flag[0] != 'A'
     && to->c_e_r_o_flag[0] != 'B'
     && to->c_e_r_o_flag[0] != 'G'
     && to->c_e_r_o_flag[0] != 'E'
     && to->c_e_r_o_flag[0] != 'D'
     && to->c_e_r_o_flag[0] != 'W'
     && to->c_e_r_o_flag[0] != 'P')
        zap_pattern_spec(&(to->rbp));
    memcpy( to->c_e_r_o_flag, from->c_e_r_o_flag, sizeof(to->c_e_r_o_flag));
    to->i_offset = from->i_offset ;
    to->i_len = from->i_len;
    memcpy(to->i_cust, from->i_cust, sizeof(from->i_cust));
    zap_pattern_spec(&(to->ebp));
    memcpy(&(to->ebp), &(from->ebp), sizeof(to->ebp));
    if (to->c_e_r_o_flag[0] != 'A'
     && to->c_e_r_o_flag[0] != 'B'
     && to->c_e_r_o_flag[0] != 'E'
     && to->c_e_r_o_flag[0] != 'G'
     && to->c_e_r_o_flag[0] != 'D'
     && to->c_e_r_o_flag[0] != 'W'
     && to->c_e_r_o_flag[0] != 'P')
    {
        to->o_offset = from->o_offset ;
        to->o_len = from->o_len;
        memcpy(to->o_cust, from->o_cust, sizeof(from->o_cust));
        to->frozen = from->frozen;
        memcpy(&(to->rbp), &(from->rbp), sizeof(to->rbp));
    }
    return;
}
/*
 * Find a matching scan_spec structure, if possible
 */
SCAN_SPEC * find_scan_spec(wdbp,sp)
WEBDRIVE_BASE * wdbp;
SCAN_SPEC * sp;
{
HIPT *h;

    if ((h = lookup(wdbp->ht, (char *) sp)) != (HIPT *) NULL)
        return (SCAN_SPEC *) (h->body);
    else
        return (SCAN_SPEC *) NULL;
} 
/*
 * Hunt for a sequence of strings
 */
static unsigned char * hunt_the_pattern(psp, base, bound)
PATTERN_SPEC * psp;
unsigned char * base;
unsigned char * bound;
{
int i;
unsigned char * p1;

    for (p1 = base, i = 0; i < psp->cnt && p1 != NULL; i++)  
    {
        p1 = bm_match(psp->bmp[i], p1, bound);
        if (p1 != NULL && i < (psp->cnt - 1))
            p1 += psp->bmp[i]->match_len;
    }
    return p1;
}
/*
 * Hunt for a sequence of strings
 */
static unsigned char * hunt_progressive(psp, base, bound)
PATTERN_SPEC * psp;
unsigned char * base;
unsigned char * bound;
{
int i;
unsigned char * p1;

    for (p1 = base, i = psp->curr; i < psp->cnt && p1 != NULL; i++)  
    {
        p1 = bm_match_frag(psp->bfp[i], p1, bound);
        if (p1 != NULL && i < (psp->cnt - 1))
            p1 += psp->bmp[i]->match_len;
    }
    if (i < psp->cnt && p1 == NULL)
        psp->curr = i;
    else
        psp->curr = 0;
    return p1;
}
/****************************************************************************
 * Routine to look for values to be substituted in cookies or whatever.
 */
void scan_incoming_cookie(wdbp, base, bound)
WEBDRIVE_BASE * wdbp;
unsigned char * base;
unsigned char * bound;
{
int i;
int j;
unsigned char * p1;
int len;

    strcpy(wdbp->narrative, "scan_incoming_cookie");
    for (j = 0; j < wdbp->scan_spec_used; j++)
    {
        if ((wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'C')
          && !(wdbp->scan_spec[j]->frozen)
          && ((p1 = hunt_the_pattern(&(wdbp->scan_spec[j]->ebp), base, bound))
                != (unsigned char *) NULL))
        {
            p1 +=  wdbp->scan_spec[j]->i_offset;
            len =  (bound - p1);

            if (wdbp->scan_spec[j]->encrypted_token == (char *) NULL)
                wdbp->scan_spec[j]->encrypted_token = (char *) malloc(len + 1);
            else
                wdbp->scan_spec[j]->encrypted_token = (char *) realloc(
                                 wdbp->scan_spec[j]->encrypted_token, len + 1);
            memcpy( wdbp->scan_spec[j]->encrypted_token, p1, len);
            *(wdbp->scan_spec[j]->encrypted_token + len) = '\0';
            if (wdbp->verbosity > 1)
            {
                fprintf(stderr, "(Client:%s) Seen Cookie Substitution %d:",
                                wdbp->parser_con.pg.rope_seq,
                                j);
                dump_scan_spec(stderr, wdbp->scan_spec[j]);
            }
        }
    }
    return;
}
/****************************************************************************
 * Routine to look for error indications 
 */
void scan_incoming_error(wdbp, base, bound)
WEBDRIVE_BASE * wdbp;
unsigned char * base;
unsigned char * bound;
{
int j;
unsigned char * p1;

    strcpy(wdbp->narrative, "scan_incoming_error");
    for (j = 0; j < wdbp->scan_spec_used; j++)
    {
        if ((wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'E'
         || wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'D'
         || wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'G')
          && ((p1 = hunt_progressive(&(wdbp->scan_spec[j]->ebp), base, bound))
                != (unsigned char *) NULL))
        {
            fprintf(stderr, "(Client:%s) Seen %s:",
                                wdbp->parser_con.pg.rope_seq,
            (wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'E' ) ? "Error" : (
            (wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'D' ) ? "Disaster" :
                            "Good"));
            dump_scan_spec(stderr, wdbp->scan_spec[j]);
            if (wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'E')
                wdbp->except_flag |= E2_ERROR_FOUND;
            else
            if (wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'D')
                wdbp->except_flag |= E2_DISASTER_FOUND;
            else
                wdbp->except_flag |= E2_GOOD_FOUND;
            return;
        }
    }
    return;
}
/****************************************************************************
 * Routine to look for indications the request can be retried. 
 */
int check_recoverable(wdbp, base, bound)
WEBDRIVE_BASE * wdbp;
unsigned char * base;
unsigned char * bound;
{
int j;

    strcpy(wdbp->narrative, "check_recoverable");
    for (j = 0; j < wdbp->scan_spec_used; j++)
    {
        if ((wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'W')
          && (hunt_progressive(&(wdbp->scan_spec[j]->ebp), base, bound)
                != (unsigned char *) NULL))
        {
            fprintf(stderr, "(Client:%s) Seen Retry Possibility:",
                                wdbp->parser_con.pg.rope_seq);
            dump_scan_spec(stderr, wdbp->scan_spec[j]);
            return 1;
        }
    }
    return 0;
}
/*
 * Activate a scan spec by adding it to the list if
 * it isn't there already
 */
void activate_scan_spec(wdbp, sp)
WEBDRIVE_BASE * wdbp;
SCAN_SPEC * sp;
{
int i;

    for (i = 0; i < wdbp->scan_spec_used; i++)
    {
        if (wdbp->scan_spec[i] == sp)
            break;
    }
    if (i >= wdbp->scan_spec_used)
    {
        wdbp->scan_spec[wdbp->scan_spec_used] = sp;
        wdbp->scan_spec_used++;
        if (wdbp->verbosity > 1)
        {
            fprintf(stderr,"(Client:%s) Activated:",
                             wdbp->parser_con.pg.rope_seq);
            dump_scan_spec(stderr, sp);
        }
    }
    return;
}
/*
 * Suspend a scan spec by removing it from the
 * list if it is there.
 */
void suspend_scan_spec(wdbp, sp)
WEBDRIVE_BASE * wdbp;
SCAN_SPEC * sp;
{
int i;

    for (i = 0; i < wdbp->scan_spec_used; i++)
    {
        if (wdbp->scan_spec[i] == sp)
        {
            if (wdbp->verbosity > 1)
            {
                fprintf(stderr,"(Client:%s) Suspended:",
                             wdbp->parser_con.pg.rope_seq);
                dump_scan_spec(stderr, sp);
            }
            break;
        }
    }
    while (i < (wdbp->scan_spec_used - 1))
    {
        wdbp->scan_spec[i] = wdbp->scan_spec[i + 1];
        i++;
    }
    wdbp->scan_spec_used = i;
    return;
}
/*
 * Remove a scan_spec althogether.
 */
void remove_scan_spec(wdbp, sp)
WEBDRIVE_BASE * wdbp;
SCAN_SPEC * sp;
{
    if (wdbp->verbosity > 1)
    {
        fprintf(stderr,"(Client:%s) Removing:", wdbp->parser_con.pg.rope_seq);
        dump_scan_spec(stderr, sp);
    }
    suspend_scan_spec(wdbp, sp);
    hremove(wdbp->ht, sp);
    zap_pattern_spec(&sp->ebp);
    if (sp->c_e_r_o_flag[0] != 'A'
     && sp->c_e_r_o_flag[0] != 'B'
     && sp->c_e_r_o_flag[0] != 'G'
     && sp->c_e_r_o_flag[0] != 'E'
     && sp->c_e_r_o_flag[0] != 'D'
     && sp->c_e_r_o_flag[0] != 'P')
        zap_pattern_spec(&sp->rbp);
    if (sp->encrypted_token != (char *) NULL)
        free(sp->encrypted_token);
    free(sp);
    return;
}
/****************************************************************************
 * Routine to look for values to be substituted in HTML or similar, where the
 * values may be wrapped in quote marks.
 */
void scan_incoming_body(wdbp, base, bound)
WEBDRIVE_BASE * wdbp;
unsigned char * base;
unsigned char * bound;
{
int j;
unsigned char * p1;

    strcpy(wdbp->narrative, "scan_incoming_body");
    for (j = 0; j < wdbp->scan_spec_used; j++)
    {
        if ((wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'O'
         || wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'H'
         || wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'U')
          && !(wdbp->scan_spec[j]->frozen))
        for (p1 = base;
              ((p1 = hunt_progressive(&(wdbp->scan_spec[j]->ebp), p1, bound))
                != (unsigned char *) NULL);)
        {
        int len, olen;

            p1 += wdbp->scan_spec[j]->i_offset;
            if (p1 < base || p1 > bound)
            {
                fprintf(stderr, "Logic Error: What we want (%x) isn't in our buffer (%x, %x)\n",
                     p1, base, bound);  
                dump_scan_spec(stderr, wdbp->scan_spec[j]);
                break;
            }
            if ( wdbp->scan_spec[j]->i_len > 0)
                len =  wdbp->scan_spec[j]->i_len;
            else
            if (wdbp->scan_spec[j]->i_cust[0] != 0)
            {
                len = memcspn(p1, bound, wdbp->scan_spec[j]->i_cust[0],
                         &wdbp->scan_spec[j]->i_cust[1]);
            }
            else
            if (*p1 == '\'')
            {
                p1++;
                len =  memcspn(p1, bound, 4, "'\r\n");
                if (p1 +len >= bound || *(p1 + len) != '\'')
                    continue; /* We have seen our variable in a string.
                               * This happens in Javascript with DHTML
                               */
            }
            else
            if (*p1 == '"')
            {
                p1++;
                len =  memcspn(p1, bound, 4, "\"\r\n");
                if (p1 +len >= bound || *(p1 + len) != '"')
                    continue; /* Our variable was in a string */
            }
            else
            {
                len = memcspn(p1, bound, wdbp->sep_exp_len, wdbp->sep_exp);
/*
 *              len = memcspn(p1, bound, 9, "'\"?&; \r\n");
 *                if (p1 + len >= bound
 *                 ||*(p1 + len) == '"'
 *                 || *(p1 + len) == '\'')
                    continue;   /o Our variable was in a string - but we still need it */
            }
            olen = len;
            if (wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'U')
                olen += len + len;
            if (wdbp->scan_spec[j]->encrypted_token == (char *) NULL)
                wdbp->scan_spec[j]->encrypted_token = (char *) malloc(olen + 1);
            else
                wdbp->scan_spec[j]->encrypted_token = (char *) realloc(
                                 wdbp->scan_spec[j]->encrypted_token, olen + 1);
            if (wdbp->scan_spec[j]->c_e_r_o_flag[0] != 'U')
                memcpy( wdbp->scan_spec[j]->encrypted_token, p1,  olen);
            else
            {
                olen = url_escape(wdbp->scan_spec[j]->encrypted_token, p1,  len, 1);
                wdbp->scan_spec[j]->encrypted_token = realloc( wdbp->scan_spec[j]->encrypted_token,
                                            olen + 1);
            }
            *(wdbp->scan_spec[j]->encrypted_token + olen) = '\0';
            if (wdbp->verbosity > 1)
            {
                fprintf(stderr, "(Client:%s) Seen Body Substitution %d:",
                                wdbp->parser_con.pg.rope_seq,
                                j);
                dump_scan_spec(stderr, wdbp->scan_spec[j]);
            }
            break;
        }
    }
    return;
}
/*********************************************************************
 * Adjust content length
 *
 * Patch the content length header in the light of reality
 */
int adjust_content_length(wdbp,b,xp)
WEBDRIVE_BASE * wdbp;
unsigned char * b;
unsigned char ** xp;
{
unsigned char *lenp, * contp;
unsigned char *x = *xp;
int old_len;
int new_len;
int old_len_len;
int new_len_len;
char len_buf[10];

    if (*b == 'G')
        return 0;    /* GET may not have a content length */
    if ((contp = bm_match(wdbp->ehp, b, x)) == (unsigned char *) NULL)
        return 0;    /* No End of Header, do nothing */
/*
 * Is there a content length?
 */
    if ((lenp = bm_match(wdbp->sbp, b, contp)) == (unsigned char *) NULL)
        return 0;    /* No content length, do nothing */
    lenp += wdbp->sbp->match_len;
    sscanf(lenp,"%d%n",&old_len, &old_len_len);
/*
 * Is it greater than zero?
 */
    if (old_len <= 0)
        return 0;   /* The edit scripts cannot create content if there is none */
    contp += wdbp->ehp->match_len;
/*
 * There shouldn't be any carriage return/line feeds on the end, but this is
 * the calling routine's job to sort out.
 */
    new_len = x - contp;
/*
 * Has the length changed?
 */
    if (new_len == old_len)
        return 0;    /* No change, do nothing */
/*
 * Patch the length.
 */
    new_len_len = sprintf(len_buf, "%d", new_len);
    if (new_len_len != old_len_len)
        memmove(lenp +  new_len_len,
                            lenp +  old_len_len,
                                x - lenp -old_len_len);
    x += (new_len_len - old_len_len);
    memcpy(lenp, len_buf, new_len_len);
    if (wdbp->debug_level > 2)
        fprintf(stderr,
             "(Client:%s) Length patched (%x,%x,%x,%x) - before %d after %d\n",
                          wdbp->parser_con.pg.rope_seq, 
                          (long) b, (long) lenp, (long) contp, (long) x,
                          old_len, new_len);
    *xp = x;
    return 1;
}
/*********************************************************************
 * Edit Header
 *********************************************************************
 * Pass in a bm_table for the header, and a replacement value.
 *
 * Return the new length.
 *
 * The message is assumed to be in a buffer that is big enough.
 */
int edit_header(wdbp, bp, sp, slen, b, len)
WEBDRIVE_BASE * wdbp;
struct bm_table * bp;               /* The header              */
unsigned char * sp;                 /* The substitution        */
int slen;                           /* The substitution length */
unsigned char * b;
int len;
{
unsigned char *hp;
unsigned char *headp;
unsigned char *x;
char len_buf[10];

    if ((hp = bm_match(wdbp->ehp, b, b + len)) == (unsigned char *) NULL)
        return -1;    /* No End of Header, do nothing */
/*
 * Is the header there already?
 */
    if ((headp = bm_match(bp, b, hp)) == (unsigned char *) NULL)
    {
/*
 * No header, we just make space, copy stuff in, and we are done
 */
        memmove(hp + bp->match_len + 2 + slen, hp, (len - (hp - b)));
        *hp++ = '\r';
        *hp++ = '\n';
        memcpy(hp, bp->match_word, bp->match_len);
        hp += bp->match_len;
        memcpy(hp, sp, slen);
        return (len + 2 + bp->match_len + slen);
    }
/*
 * We already have the header, we just adjust the space, and copy in
 */
    headp += bp->match_len;
    x = strchr(headp, '\r');
    if (x != headp + slen)
        memmove(headp + slen, x, (len - (x - b)));
    memcpy(headp, sp, slen);
    return len + slen - (x - headp);
}
/****************************************************************************
 * Routine to apply edits to a region of memory.
 * Returns the total change in the length
 * The edits are as defined in the SCAN_SPECS.
 * rbp has the markers for the substitution. 
 * encrypted_token has the values to be plugged in.
 */
int apply_edits(wdbp, x, mess_len)
WEBDRIVE_BASE * wdbp;
unsigned char * x;
int mess_len;
{
int j;
int diff_len = 0;
int cnt;

    strcpy(wdbp->narrative, "apply_edits");
    for (j = 0; j < wdbp->scan_spec_used; j++)
    {
        if (wdbp->scan_spec[j]->encrypted_token != (char *) NULL
         && wdbp->scan_spec[j]->rbp.cnt > 0)
        {
        unsigned char * p1;
        unsigned char * p2;

            for (p1 = x, cnt = 0;
                  ((p1 = hunt_the_pattern(&(wdbp->scan_spec[j]->rbp),
                         p1, x + mess_len)) != (unsigned char *) NULL);)
            {
            int old_len;
            int new_len;
/*
 * Copy in the new state or page or jsessionid or whatever
 */
                if (wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'H')
                    new_len = wdbp->scan_spec[j]->o_len;
                else
                    new_len = strlen(wdbp->scan_spec[j]->encrypted_token);
                if (wdbp->scan_spec[j]->o_len > 0)
                    old_len = wdbp->scan_spec[j]->o_len;
                else
                if (wdbp->scan_spec[j]->o_cust[0] != 0)
                    old_len = memcspn(p1 + wdbp->scan_spec[j]->o_offset,
                                   x + mess_len,
                          wdbp->scan_spec[j]->o_cust[0],
                         &wdbp->scan_spec[j]->o_cust[1]);
                else
                    old_len = memcspn(p1 + wdbp->scan_spec[j]->o_offset,
                                   x + mess_len,
                                   wdbp->sep_exp_len, wdbp->sep_exp);
                if (p1 + wdbp->scan_spec[j]->o_offset + old_len > x + mess_len)
                    break;
                if (wdbp->verbosity > 1)
                {
                    fprintf(stderr, "(Client:%s) Made Substitution (",
                                wdbp->parser_con.pg.rope_seq);
                    gen_handle_nolf(stderr, 
                                p1 + wdbp->scan_spec[j]->o_offset,
                                p1 + wdbp->scan_spec[j]->o_offset +
                                old_len, 1);
                    fprintf(stderr, ") %d:", j);
                    dump_scan_spec(stderr, wdbp->scan_spec[j]);
                }
                if (old_len != new_len)
                {
                    if ((mess_len + (new_len - old_len)) > WORKSPACE
                     && mess_len <= WORKSPACE)
                    {
                        fprintf(stderr, "(Client:%s) Substitution (mess_len=%d old_len=%d new_len=%d) would corrupt buffer - ignored\n",
                                wdbp->parser_con.pg.rope_seq, mess_len, old_len, new_len);
                        return 0;
                    }
/*
 * Shuffle what else is there up or down to make room for it
 */
                    memmove(p1 + wdbp->scan_spec[j]->o_offset + new_len,
                            p1 + wdbp->scan_spec[j]->o_offset + old_len,
                                mess_len - ((p1 - x) + 
                                  wdbp->scan_spec[j]->o_offset + old_len));
                    mess_len += (new_len - old_len);
                    diff_len += (new_len - old_len);
                }
                memcpy(p1 + wdbp->scan_spec[j]->o_offset,
                              wdbp->scan_spec[j]->encrypted_token, new_len);
                p1 = p1 + wdbp->scan_spec[j]->o_offset + new_len;
                cnt++;
            }
        }
        if (wdbp->debug_level > 4 && cnt == 0)
        {
            fprintf(stderr, "(Client:%s) Did not see (%d):",
                                wdbp->parser_con.pg.rope_seq, j);
            dump_scan_spec(stderr, wdbp->scan_spec[j]);
        }
    }
    return diff_len;
}
/****************************************************************************
 * Routines to compress a region of memory.
 * The compressed value ends up 'in place'
 * Returns the total change in the length
 */
static int def_open(hp, encoded)
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
    encoded->len = 0;
    return deflateInit2(hp, -1, Z_DEFLATED, 15, 9, Z_DEFAULT_STRATEGY);
}
static int def_block(hp, from_wire, encoded, flush)
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
        encoded->len = 0;
        return ret;
    }
/*
 * Beware: assumes WORKSPACE handed in!
 */
    if (hp->avail_out == 0 && hp->avail_in > 0)
    {
        fprintf(stderr, "Ran out of space for compression, %d unprocessed\n",
                hp->avail_in);
        hp->avail_out = 20 * hp->avail_in;
        hp->next_out = encoded->element + enclen;
        enclen += hp->avail_out;
        if (enclen > WORKSPACE)
        {
            (void)deflateEnd(hp);
            encoded->len = 0;
            return ret;
        }
        goto restart;
    }
    encoded->len = enclen - hp->avail_out;
    return ret;
}
int apply_compression(wdbp, x, mess_len, work_buffer)
WEBDRIVE_BASE * wdbp;
unsigned char * x;
int mess_len;
char * work_buffer; /* Assumed to be WORKSPACE */
{
static unsigned char gzip_header[] = {0x1f, 0x8b, 0x8, 0, 0, 0, 0, 0, 0, 0xff};
int j;
int diff_len = 0;
struct element_tracker encoded; 
struct element_tracker from_wire; 
z_stream deflate;

    encoded.len = -1;
    encoded.element = work_buffer;
    from_wire.element = x;
    from_wire.len = mess_len;
    def_block(&deflate, &from_wire, &encoded,  Z_FINISH);
    if (encoded.len != -1)
    {
        memcpy(x, &gzip_header[0], sizeof(gzip_header));
        memcpy(x + sizeof(gzip_header), encoded.element, encoded.len);
        diff_len = encoded.len + sizeof(gzip_header) - mess_len;
        (void) deflateEnd(&deflate);
    }
    else
        diff_len = 0;
    return diff_len;
}
int is_internal(wdbp, sp)
WEBDRIVE_BASE * wdbp;
SCAN_SPEC * sp;
{
int i;

    for (i = 0; i < wdbp->scan_spec_internal; i++)
        if (!strcmp(wdbp->scan_spec[i]->scan_key,sp->scan_key))
            return 1;
    return 0;
}
/*
 * Clear the cookie cache
 */
void clear_cookie_cache(wdbp)
WEBDRIVE_BASE * wdbp;
{
int i;
int ref;
HIPT * p;
END_POINT * cur_ep;

    strcpy(wdbp->narrative, "clear_cookie_cache");

    for (ref = wdbp->cookie_cnt, wdbp->cookie_cnt = 0, i = 0;
             i < ref; i++)
        free( wdbp->cookies[i]);
    wdbp->pragma_seq = 0;
/*
 * Also reset the directives
 * -   Clear the substitutions for the internal specifications
 */
    for (i = 0; i < wdbp->scan_spec_internal; i++)
        if (wdbp->scan_spec[i]->encrypted_token != (char *) NULL)
        {
            free(wdbp->scan_spec[i]->encrypted_token);
            wdbp->scan_spec[i]->encrypted_token = (char *) NULL;
        }
/*
 * -   Remove the others entirely.
 */
    while (i < wdbp->scan_spec_used)
        remove_scan_spec(wdbp, wdbp->scan_spec[i]);
/*
 * Now get rid of any suspended ones
 */
    for (i = 0; i < wdbp->ht->tabsize; i++)
    {
        p = &wdbp->ht->table[i];
        do
        {
            if (p->in_use)
            {
               if (!is_internal(wdbp,(SCAN_SPEC *) (p->body)))
                   remove_scan_spec(wdbp, (SCAN_SPEC *)(p->body));
            }
            p = p->next;
        }
        while(p);
    }
/*
 * Zap any ORACLE Web Forms encryption state.
 */
    if (wdbp->gday != (unsigned char *) NULL)
    {
        free(wdbp->gday);
        wdbp->gday = (unsigned char *) NULL;
    }
    if ( wdbp->f_enc_dec[0] != (unsigned char *) NULL)
    {
        free(wdbp->f_enc_dec[0]);
        wdbp->f_enc_dec[0] = (unsigned char *) NULL;
    }
    if ( wdbp->f_enc_dec[1] != (unsigned char *) NULL)
    {
        free(wdbp->f_enc_dec[1]);
        wdbp->f_enc_dec[1] = (unsigned char *) NULL;
    }
/*
 * Kill off any lingering HTTP2 connections.
 */
    for (cur_ep = wdbp->end_point_det;
            cur_ep->record_type == END_POINT_TYPE; cur_ep++)
    {
        if (cur_ep->iwdbp != NULL)
        {   /* HTTP2 state */
            cur_ep->iwdbp->go_away = 1;
            cur_ep->owdbp->go_away = 1;
            pipe_buf_add(cur_ep->owdbp->pbp, 1, 0, NULL, NULL, 0);
                                  /* Notify the output thread */
            if ( !pthread_equal(cur_ep->iwdbp->own_thread_id, zeroth))
                pthread_kill(cur_ep->iwdbp->own_thread_id, SIGIO);
                                  /* Notify the input thread, which can hang */
        }
    }
/*
 * Close any connections that are still open
 */
    pthread_mutex_lock(&wdbp->script_mutex);
    for (wdbp->cur_link = wdbp->link_det;
            wdbp->cur_link < &wdbp->link_det[MAXLINKS]
         && wdbp->cur_link->link_id != 0;
                wdbp->cur_link++)
    {
        if (wdbp->cur_link->connect_fd != -1)
            socket_cleanup(wdbp);
#ifndef TUNDRIVE
        if (wdbp->cur_link->to_ep->iwdbp != NULL)
        {   /* HTTP2 state */
            if (wdbp->cur_link->to_ep->h2cp != NULL)
            {
                zap_http2_con(wdbp->cur_link->to_ep->h2cp);
                wdbp->cur_link->to_ep->h2cp = NULL;
            }
        }
#endif
    }
#ifdef USE_SSL
/*
 * Zap any SSL_SESSIONS but not SSL_CTX structures, since they only impact
 * the load on the driver machine ...
 */
    for (cur_ep = wdbp->end_point_det;
            cur_ep->record_type == END_POINT_TYPE; cur_ep++)
    {
        if ( cur_ep->ssl_sess != NULL
          && cur_ep->ssl_sess->references > 0)
        {
            for (;;)
            {
                ref = cur_ep->ssl_sess->references;
                SSL_SESSION_free(cur_ep->ssl_sess);
                if (ref <= 1)
                    break;
            }
            cur_ep->ssl_sess = NULL;
        }
    }
#endif
    pthread_mutex_unlock(&wdbp->script_mutex);
    return;
}
/*
 * Save the cookie in the cookie list, inserting or updating as appropriate
 */
void cache_cookie(wdbp, name, bound)
WEBDRIVE_BASE * wdbp;
unsigned char * name;
unsigned char * bound;
{
unsigned char * p1;
unsigned char * p2;
unsigned char * p3;
int i;
double expiry;

    for (p1 = name; p1 < bound && *p1 != '='; p1++);
                     /* De-lineate the cookie name */
    for (p2 = p1 + 1; p2 < bound && *p2 != ';' && *p2 != '\r'; p2++);
                     /* De-lineate the cookie */
    for (i = 0; i < wdbp->cookie_cnt; i++)
         if (!memcmp(wdbp->cookies[i], name, p1 - name + 1))
             break;  /* See if we already have this cookie */
    if (i < wdbp->cookie_cnt)
        free(wdbp->cookies[i]);   /* Free the old value */
/*
 * Check for pre-expiry
 */
    if (*p2 == ';'
     && (bound - p2) > 35
     && !strncmp(p2,"; expires=",10)
     && date_val(p2 + 15,"dd-mon-yyyy hh24:mi:ss",&p3,&expiry)
     && ((long) expiry) <= time(0))
    {
        if (i < wdbp->cookie_cnt)
        {
            for( ; i < wdbp->cookie_cnt - 1; i++)
                wdbp->cookies[i] = wdbp->cookies[i+1];
            wdbp->cookie_cnt = i;
        }
        return;
    }
    wdbp->cookies[i] = (char *) malloc(p2 - name  + 1);
    memcpy( wdbp->cookies[i], name, p2 - name );
    *(wdbp->cookies[i] + (p2 - name)) = '\0';
/*
 * Find the state= value or similar for substitution purposes
 */
    scan_incoming_cookie(wdbp, name, p2);
    if (i >= wdbp->cookie_cnt)
        wdbp->cookie_cnt++;
    return;
}
#ifdef CAN_HAVE_MULTI_PER_LINE
/*
 * Save the cookie in the cookie list, inserting or updating as appropriate
 */
void edit_cookies(wdbp, sep)
WEBDRIVE_BASE * wdbp;
struct script_element * sep;
{
int cookie_len;
int new_len;
unsigned char * ehp;
unsigned char * cookp;
unsigned char * basep;
int i;
/*
 *  Add up the length of the cookies
 */
    for (cookie_len = 0, i = 0 ; i < wdbp->cookie_cnt; i++)
        cookie_len += strlen(wdbp->cookies[i]) + 1;
    cookie_len--;
/*
 * Find the end of the header (ehp)
 */
    if ((ehp = bm_match(wdbp->ehp,sep->body, sep->body + sep->body_len))
             == NULL)
        return;
/*
 * Find the Cookie header
 * -  If there is one, the move-up point is the next carriage return
 * -  Otherwise, it is the middle of the end header sequence.
 */
    cookp =  bm_match(wdbp->cookp,sep->body, sep->body + sep->body_len);
    if (cookp == NULL)
    {
        new_len = sep->body_len + 10 + cookie_len;
        cookp = ehp;
    }
    else
    {
        ehp = strchr(cookp, '\r');
        cookp -= 2;
        new_len = sep->body_len + cookie_len + 10 - (ehp - cookp); 
    }
/*
 * Re-size the body for new size
 */
    basep = sep->body;
    sep->body = realloc(sep->body, new_len);
    cookp = sep->body + (cookp - basep);
    ehp = sep->body + (ehp - basep);
/*
 *  Move the top bit up or down
 */
    memmove(cookp + 10 + cookie_len, ehp, (sep->body + sep->body_len) - ehp);
    memcpy(cookp, "\r\nCookie: ", 10);
/*
 * Copy the cookies in to position
 */
    for (cookp += 10, i = 0; i < wdbp->cookie_cnt - 1; i++)
    {
        cookie_len = strlen(wdbp->cookies[i]);
        memcpy(cookp, wdbp->cookies[i], cookie_len);
        cookp += cookie_len;
        *cookp++ = ';';
    } 
    cookie_len = strlen(wdbp->cookies[i]);
    memcpy(cookp, wdbp->cookies[i], cookie_len);
    sep->body_len = new_len;
    assert(sep->body_len > 0);
    return;
}
#else
/*
 * Save the cookie in the cookie list, inserting or updating as appropriate
 */
void edit_cookies(wdbp, sep)
WEBDRIVE_BASE * wdbp;
struct script_element * sep;
{
int cookie_len;
int new_len;
unsigned char * ehp;
unsigned char * cookp;
unsigned char * basep;
int i;
/*
 *  Add up the length of the cookies
 */
    for (cookie_len = 0, i = 0 ; i < wdbp->cookie_cnt; i++)
        cookie_len += strlen(wdbp->cookies[i]) + 10;
/*
 * Find the end of the header (ehp)
 */
    if ((ehp = bm_match(wdbp->ehp,sep->body, sep->body + sep->body_len))
             == NULL)
        return;
/*
 * Find the Cookie header
 * -  If there is one, the move-up point is the next carriage return
 * -  Otherwise, it is the middle of the end header sequence.
 */
    cookp =  bm_match(wdbp->cookp,sep->body, sep->body + sep->body_len);
    if (cookp == NULL)
    {
        new_len = sep->body_len + cookie_len;
        cookp = ehp;
    }
    else
    {
        ehp = strchr(cookp, '\r');
        cookp -= 2;
        new_len = sep->body_len + cookie_len  - (ehp - cookp); 
    }
/*
 * Re-size the body for new size
 */
    basep = sep->body;
    sep->body = realloc(sep->body, new_len);
    cookp = sep->body + (cookp - basep);
    ehp = sep->body + (ehp - basep);
/*
 *  Move the top bit up or down
 */
    memmove(cookp + cookie_len, ehp, (sep->body + sep->body_len) - ehp);
/*
 * Copy the cookies in to position
 */
    for ( i = 0; i < wdbp->cookie_cnt; i++)
    {
        memcpy(cookp, "\r\nCookie: ", 10);
        cookp += 10;
        cookie_len = strlen(wdbp->cookies[i]);
        memcpy(cookp, wdbp->cookies[i], cookie_len);
        cookp += cookie_len;
    } 
    sep->body_len = new_len;
    assert(sep->body_len > 0);
    return;
}
#endif
/****************************************************************************
 * Routine to look for values to be substituted in cookies or whatever.
 */
void preserve_script_cookies(wdbp, base, bound)
WEBDRIVE_BASE * wdbp;
unsigned char * base;
unsigned char * bound;
{
int j;
unsigned char * p1;

    for (j = 0; j < wdbp->scan_spec_used; j++)
    {
        if (wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'P'
          && ((p1 = hunt_the_pattern(&(wdbp->scan_spec[j]->ebp), base, bound))
                != (unsigned char *) NULL))
            cache_cookie(wdbp, p1, bound);
    }
    return;
}
/****************************************************************************
 * Routine to see if this input is to be suppressed or not.
 */
int check_allowed_blocked(wdbp, base, bound)
WEBDRIVE_BASE * wdbp;
unsigned char * base;
unsigned char * bound;
{
int j;
int ret;

    for (j = 0, ret = 0; j < wdbp->scan_spec_used; j++)
    {
        if ((wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'A'
         || wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'B')
          && hunt_the_pattern(&(wdbp->scan_spec[j]->ebp), base, bound)
                != (unsigned char *) NULL)
            if (wdbp->scan_spec[j]->c_e_r_o_flag[0] == 'A')
                ret += 1;
            else
                ret -= 1;
    }
    return ret;
}
/*
 * Recognise a pattern spec (a counted list of search strings).
 */
static int recognise_pattern_spec(wdbp, got_to, psp, hex_flag)
WEBDRIVE_BASE * wdbp;
unsigned char ** got_to;
PATTERN_SPEC * psp;
int hex_flag;
{
char val_buf[2048];
int i;
int len;

    if (nextasc_r((char *) NULL, ':', '\\', got_to,
        &val_buf[0], &val_buf[sizeof(val_buf) - 1]) == NULL)
    {
        fprintf(stderr,"(Client:%s) %s:%d PATTERN_SPEC Too Short\n",
                        wdbp->parser_con.pg.rope_seq, __FILE__, __LINE__);
        return 0;
    }
    if ((psp->cnt = atoi(val_buf)) < 0 || psp->cnt > MAX_PATTERN_SPECS)
    { /* Use zero for programmatically-maintained items */
        fprintf(stderr,"(Client:%s) Invalid Number of PATTERN_SPECS:%d\n",
                        wdbp->parser_con.pg.rope_seq, psp->cnt);
        return 0;
    }
    for (i = 0; i < psp->cnt; i++)
    {
        if (nextasc_r((char *) NULL, ':', '\\', got_to,
            &val_buf[0], &val_buf[sizeof(val_buf) - 1]) == NULL)
        {
            fprintf(stderr,"(Client:%s) %s:%d PATTERN_SPEC Too Short\n",
                                        wdbp->parser_con.pg.rope_seq, __FILE__, __LINE__);
            return 0;
        }
        len = strlen(val_buf);
        if (val_buf[len -1 ] == '\n')
        {
            len--;
            val_buf[len] = '\0';
        }
        if (hex_flag)
        {
            hexout(val_buf, val_buf, len);
            len >>= 1;
        }
        psp->bmp[i] = bm_compile_bin(val_buf, len);
        psp->bfp[i] = bm_frag_init(psp->bmp[i], bm_match_new);
    }
    return 1;
}
/**************************************************************************
 * Recognise the scan specifications tacked on the end of the DB directives
 **************************************************************************
 * The scan specifications are:
 * - C; something to be seen in a cookie
 * - O; a ('find something, substitute somewhere' pair)
 * - U; as O, but the substitution needs to be URL-escaped
 * - H; as O, but the values are in Hexadecimal
 * - S; suspend a substitution
 * - Y; yes, activate a substitution
 * - R; remove a substitution
 * - E; marker for an error
 * - D; marker for a disastrous error, requiring a restart
 * - G; marker for a good item
 * - P; cookie to be preserved in the script (one not set by the server)
 *
 * All scan specifications start with:
 * - The flag
 * - A name, used to identify them.
 *
 * For S, Y, and R operations, this is all that is necessary.
 *
 * The other options additionally have associated with them one or two
 * pattern blocks and offsets.
 *
 * A pattern block consists of:
 * -  A pattern count
 * -  The pattern-count number of strings
 *
 * An offset is:
 * -  The offset to the target from the start of the last string
 *
 * E, D, G and P operations have a single pattern block.
 *
 * C, O and U operations have a pair of pattern blocks, each followed
 * by an offset.
 *
 * The scan specifications live in hash table, hashed by name, one table
 * per virtual user.
 *
 * The active specifications are in lists, one list per user.
 *
 * For a given user, there is a maximum of MAX_SCAN_SPECS active at any
 * one time.
 *
 * Scan specifications are created active, but can be de-activated
 * immediately.
 ********************************************************************** 
 */
enum tok_id recognise_scan_spec(a, wdbp, got_to)
union all_records *a;
WEBDRIVE_BASE * wdbp;
unsigned char ** got_to;
{
int i;
int j;
SCAN_SPEC validate_spec;
SCAN_SPEC *sp;
char num_buf[32];

    strcpy(wdbp->narrative, "recognise_scan_spec");
    if (wdbp->scan_spec_used >= MAX_SCAN_SPECS)
    {
        fprintf(stderr,"(Client:%s) Too Many Active SCAN_SPECS:%d\n",
                        wdbp->parser_con.pg.rope_seq, wdbp->scan_spec_used);
        return E2EOF;
    }
    if (nextasc_r((char *) NULL, ':', '\\', got_to,
        &validate_spec.c_e_r_o_flag[0],
        &validate_spec.c_e_r_o_flag[sizeof(validate_spec.c_e_r_o_flag) - 1])
                     == NULL)
        return E2EOF;
/*
 * A is allow this to be executed
 * B is block this from execution
 * C for a cookie with an offset
 * D for a disastrous exception
 * E for an exception marker
 * F means freeze the match
 * G for a success marker
 * O match and replace at the offset
 * Q quiescent point. Wait for this response before continuing.
 * U match and replace at the offset and should be URL-encoded
 * P is a cookie to preserve
 * R means remove the last-added element that matches
 * T means thaw the match
 * S means suspend the last-added element
 * W for a warning marker, indicating that an error can be retried
 * Y means yes, activate a named element.
 */  
    if (validate_spec.c_e_r_o_flag[0] != 'A'
     && validate_spec.c_e_r_o_flag[0] != 'B'
     && validate_spec.c_e_r_o_flag[0] != 'C'
     && validate_spec.c_e_r_o_flag[0] != 'D'
     && validate_spec.c_e_r_o_flag[0] != 'E'
     && validate_spec.c_e_r_o_flag[0] != 'F'
     && validate_spec.c_e_r_o_flag[0] != 'G'
     && validate_spec.c_e_r_o_flag[0] != 'H'
     && validate_spec.c_e_r_o_flag[0] != 'O'
     && validate_spec.c_e_r_o_flag[0] != 'P'
     && validate_spec.c_e_r_o_flag[0] != 'Q'
     && validate_spec.c_e_r_o_flag[0] != 'R'
     && validate_spec.c_e_r_o_flag[0] != 'S'
     && validate_spec.c_e_r_o_flag[0] != 'T'
     && validate_spec.c_e_r_o_flag[0] != 'U'
     && validate_spec.c_e_r_o_flag[0] != 'W'
     && validate_spec.c_e_r_o_flag[0] != 'Y')
    {
        fprintf(stderr,"(Client:%s) SCAN_SPEC Error :%s:\n", wdbp->parser_con.pg.rope_seq,
                        validate_spec.c_e_r_o_flag);
        syntax_err(__FILE__,__LINE__,
"Flag not (A)llow, (B)lock, (C)ookie (Offset), (D)isaster, (E)rror, (F)reeze, (G)ood, (H)exadecimal, (O)ffset,\n(P)reserve, (Q)uiescent, (R)emove, (S)ave, (T)haw, (U)rl-encoded (Offset), (W)arning marker for retry or (Y)ank",
             &(wdbp->parser_con));
        return E2EOF;
    }
    if (validate_spec.c_e_r_o_flag[0] == 'Q')
    {
        wdbp->sync_flag = 1;
        return SCAN_SPEC_TYPE;
    }
/*
 * Recognise the name
 */
    if (nextasc_r((char *) NULL, ':', '\\', got_to,
        &validate_spec.scan_key[0],
        &validate_spec.scan_key[sizeof(validate_spec.scan_key) - 1])
                     == NULL)
    {
        fprintf(stderr,"(Client:%s) %s:%d Short SCAN_SPEC\n", wdbp->parser_con.pg.rope_seq,
                   __FILE__, __LINE__);
        return E2EOF;
    }
    if (validate_spec.scan_key[ strlen(validate_spec.scan_key) - 1]
                          == '\n')
        validate_spec.scan_key[strlen(validate_spec.scan_key )-1] = '\0';
/*
 * The SCAN_SPEC must already exist for these
 */
    if (validate_spec.c_e_r_o_flag[0] == 'R'
     || validate_spec.c_e_r_o_flag[0] == 'S'
     || validate_spec.c_e_r_o_flag[0] == 'F'
     || validate_spec.c_e_r_o_flag[0] == 'T'
     || validate_spec.c_e_r_o_flag[0] == 'Y')
    {
        if ((sp = find_scan_spec(wdbp,&validate_spec))
             ==  (SCAN_SPEC *) NULL)
        {
            fprintf(stderr,"(Client:%s) SCAN_SPEC:%c: but %s does not exist\n",
                       wdbp->parser_con.pg.rope_seq, validate_spec.c_e_r_o_flag[0],
                       validate_spec.scan_key);
            return E2EOF;
        }
        switch (validate_spec.c_e_r_o_flag[0])
        {
        case 'R':
            remove_scan_spec(wdbp, sp);
            break;
        case 'S':
            suspend_scan_spec(wdbp, sp);
            break;
        case 'Y':
            activate_scan_spec(wdbp, sp);
            break;
        case 'F':
            sp->frozen = 1;
            break;
        case 'T':
            sp->frozen = 0;
            break;
        }
        return SCAN_SPEC_TYPE;
    }
/*
 * All others must have at least one PATTERN_SPEC. Note that the Hexadecimal
 * only relates to the replacement pattern (which will be a session ID
 * or similar being replaced, rather than navigating to a position).
 */
    if (!recognise_pattern_spec(wdbp, got_to, &(validate_spec.ebp), 0))
        return E2EOF;
/*
 * If it isn't an A, B, E, G, W or P, there is an offset and a further SCAN_SPEC
 * and offset pair to find
 */
    if (validate_spec.c_e_r_o_flag[0] != 'A'
     && validate_spec.c_e_r_o_flag[0] != 'B'
     && validate_spec.c_e_r_o_flag[0] != 'D'
     && validate_spec.c_e_r_o_flag[0] != 'E'
     && validate_spec.c_e_r_o_flag[0] != 'G'
     && validate_spec.c_e_r_o_flag[0] != 'W'
     && validate_spec.c_e_r_o_flag[0] != 'P')
    {
        if ( nextasc_r((char *) NULL, ':', '\\', got_to, &num_buf[0],
                                     &num_buf[sizeof(num_buf) - 1]) == NULL)
        {
            fprintf(stderr,"(Client:%s) %s:%d Short SCAN_SPEC\n",
                            wdbp->parser_con.pg.rope_seq, __FILE__, __LINE__);
            return E2EOF;
        }
        validate_spec.i_offset = atoi(num_buf);
        if ( nextasc_r((char *) NULL, ':', '\\', got_to, &num_buf[0],
                                     &num_buf[sizeof(num_buf) - 1]) == NULL)
        {
            fprintf(stderr,"(Client:%s) %s:%d Short SCAN_SPEC\n",
                            wdbp->parser_con.pg.rope_seq, __FILE__, __LINE__);
            return E2EOF;
        }
        memset(&validate_spec.i_cust[0], 0, sizeof(validate_spec.i_cust));
        if (num_buf[0] == 'x')
        {
            validate_spec.i_len = 0;
            hexout(&validate_spec.i_cust[0], num_buf+1, strlen(num_buf+1));
            if (validate_spec.i_cust[0] > 8)
                validate_spec.i_cust[0] = 8;
        }
        else
        {
            validate_spec.i_len = atoi(num_buf);
        }
        if (!recognise_pattern_spec(wdbp, got_to, &(validate_spec.rbp),
               (validate_spec.c_e_r_o_flag[0] == 'H')?1:0))
            return E2EOF;
        if ( nextasc_r((char *) NULL, ':', '\\', got_to, &num_buf[0],
                                     &num_buf[sizeof(num_buf) - 1]) == NULL)
        {
            fprintf(stderr,"(Client:%s) %s:%d Short SCAN_SPEC\n",
                            wdbp->parser_con.pg.rope_seq, __FILE__, __LINE__);
            return E2EOF;
        }
        validate_spec.o_offset = atoi(num_buf);
        if ( nextasc_r((char *) NULL, ':', '\\', got_to, &num_buf[0],
                                     &num_buf[sizeof(num_buf) - 1]) == NULL)
        {
            fprintf(stderr,"(Client:%s) %s:%d Short SCAN_SPEC\n",
                            wdbp->parser_con.pg.rope_seq, __FILE__, __LINE__);
            return E2EOF;
        }
        memset(&validate_spec.o_cust[0], 0, sizeof(validate_spec.o_cust));
        if (num_buf[0] == 'x')
        {
            validate_spec.o_len = 0;
            hexout(&validate_spec.o_cust[0], num_buf+1, strlen(num_buf+1));
            if (validate_spec.o_cust[0] > 8)
                validate_spec.o_cust[0] = 8;
        }
        else
        {
            validate_spec.o_len = atoi(num_buf);
        }
        validate_spec.encrypted_token = (char *) NULL;
        validate_spec.frozen = 0;
    }
/*
 * Check to see if the scan_spec is already known. If it is, we update
 * it, otherwise we create a new one
 */
    if ((sp = find_scan_spec(wdbp,&validate_spec))
         !=  (SCAN_SPEC *) NULL)
        update_scan_spec(sp, &validate_spec);
    else
        sp = new_scan_spec(wdbp, &validate_spec);
/*
 * Activate the new SCAN_SPEC
 */
    activate_scan_spec(wdbp, sp);
    if (validate_spec.c_e_r_o_flag[0] != 'A'
     && validate_spec.c_e_r_o_flag[0] != 'B'
     && validate_spec.c_e_r_o_flag[0] != 'D'
     && validate_spec.c_e_r_o_flag[0] != 'E'
     && validate_spec.c_e_r_o_flag[0] != 'G'
     && validate_spec.c_e_r_o_flag[0] != 'W'
     && validate_spec.c_e_r_o_flag[0] != 'P'
     && validate_spec.ebp.cnt == 0)
        sp->frozen = 1; /* Programmatically maintained; no search applicable */
    return SCAN_SPEC_TYPE;
}
