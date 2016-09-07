/*
 * Turn a stream into tokens preparatory to working out differences,
 * finding things etc.
 *
 * The main calling interface has to look like fgets(); 3 parameters, a return
 * buffer, a return buffer size, and a channel.
 *
 * The return buffer needs to take:
 * - An indication as to what the thing is; all white space might compare equal,
 *   for instance
 * - A length
 * - The fragment.
 * Things like viewstates can be very large. But we need to be able to search
 * for sequences of tokens in lists, so it does no harm for large ones to be
 * broken up into 255 byte pieces.
 * 
 * Probably only need to have string input, so don't parameterise the read
 * routine.
 */
#include <stdio.h>
#include <stdlib.h>
struct tok_chan {
    unsigned char * buf;
    unsigned int buf_len;
    unsigned char * cur_pos;
    unsigned int cont_flag;   /* Flags that what follows is a continuation */
};
/*
 * Return buffer value types. The buffer contains this byte, then a length,
 * then the value. If the length is zero, this is indicates that the flag byte
 * is the character; ', ", =, +  etc.
 * -   Not sure how useful the number recognition is.
 * -   Reduce the number of operators?
 */
#define E2_CONTINUE 0
#define E2_WSP 1
#define E2_NUM 2
#define E2_BIN 3

char * tgets(ret_buf, ret_len, tchan)
char * ret_buf;
int ret_len;
struct tok_chan * tchan;
{
int to_do;
unsigned char * xp;
unsigned char * op;
unsigned char * bound;

    if (ret_buf == NULL
     || tchan == NULL
     || tchan->cur_pos >= (tchan->buf + tchan->buf_len))
        return NULL;
    to_do = (tchan->buf + tchan->buf_len) - tchan->cur_pos;
    if (to_do > ret_len - 2)
        to_do = ret_len - 2;
    if (to_do > 255)
        to_do = 255;
    for (xp = (unsigned char *) (tchan->cur_pos),
         bound = xp + to_do,
         op = (unsigned char *) (ret_buf + 2);
             xp < bound; )
    {
        switch (*xp)
        {                               /* look at the character */
        case 0:
        case '\t':
        case '\r':
        case '\n':
        case '\f':
        case ' ':
/*
 * White space
 */
            if (xp != tchan->cur_pos
             && (( tchan->cont_flag == E2_CONTINUE
             && *ret_buf != E2_WSP)
             || (*ret_buf == E2_CONTINUE
             &&  tchan->cont_flag != E2_WSP)))
                goto operator;
            if (xp == tchan->cur_pos)
            {
                if (tchan->cont_flag == E2_WSP)
                    *ret_buf = E2_CONTINUE;
                else
                {
                    *ret_buf = E2_WSP;
                    tchan->cont_flag = E2_CONTINUE;
                }
            }
            *op++ = *xp++;
            break;
        case ';':
        case ',':
        case '{':
        case '[':
        case '(':
        case '}':
        case ']':
        case ')':
/** Try the following as ordinary characters ...
        case '+':
        case '-':
        case '*':
        case '/':
        case '%':
        case ':':
        case '!':
        case '|':
 */
        case '?':
        case '&':
        case '=':
        case '<':
        case '>':
        case '"':
        case '\\':
        case '`':
        case '\'':
operator:
            tchan->cont_flag = E2_CONTINUE;
tidyup:
            if (xp == tchan->cur_pos)
            {
                *ret_buf = *xp;
                *(ret_buf + 1) = '\0';
                tchan->cur_pos++;
            }
            else
            {
                *(ret_buf + 1) = (unsigned char) (xp - tchan->cur_pos);
                tchan->cur_pos = xp;
            }
            return ret_buf;
/**
        case '.':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':                         /o apparently a number o/
            if (xp == tchan->cur_pos && tchan->cont_flag == 0)
            {
    /o
     * Decide what we have.
     o/
                for ( xp++;
                          xp < bound 
                       && (*xp == '.' || ( *xp >='0' && *xp <= '9'));
                               xp++);
                if (xp == tchan->cur_pos + 1
                 && *(xp - 1) == '.')
                {
                    xp--;
                    goto operator;
                }
                *ret_buf = E2_NUM;
                *(ret_buf + 1) = (unsigned char) (xp - tchan->cur_pos);
                memcpy(op, tchan->cur_pos, *(ret_buf + 1));
                tchan->cur_pos = xp;
                if (xp == bound && xp != tchan->buf + tchan->buf_len)
                    tchan->cont_flag = 1;
                return ret_buf;
            }
*/
         default: /* Everything else is a binary string */
            if (xp != tchan->cur_pos
             && ((tchan->cont_flag == E2_CONTINUE
               && *ret_buf != E2_BIN)
             ||  (*ret_buf == E2_CONTINUE
               && tchan->cont_flag != E2_BIN)))
                goto operator;
            if (xp == tchan->cur_pos)
            {
                if (tchan->cont_flag == E2_BIN)
                    *ret_buf = E2_CONTINUE;
                else
                {
                    *ret_buf = E2_BIN;
                    tchan->cont_flag = E2_CONTINUE;
                }
            }
            *op++ = *xp++;
            break;
        } /* switch is repeated on next character if have not returned */
    }
    if (xp >= bound)
        tchan->cont_flag = *ret_buf;
    goto tidyup;
}
int tlen(buf)
unsigned char * buf;
{
int i;

    if (*buf <= E2_BIN)
        return (2 + *(buf + 1));
    else
        return 2;
}
#ifdef STANDALONE
static char * el_clone(thing, len)
char * thing;
int len;
{
char * buf;

    if ((buf = (char *) malloc(len + 1)) == NULL)
    {
        perror("malloc() failed");
        return NULL;
    }
    memcpy(buf, thing, len);
    buf[len] = '\0';
    return buf;
}
/*
 * Originally a Work-alike for the VM/CMS EXECIO DISKR facility. It read a
 * file into a dynamically allocated array. The zero'th element is a pointer
 * to an integer line count. The other elements were the file lines.
 *
 * Generalised by passing a 'channel' rather than a file name, and
 * parameterising the routines that handle elements from the channel.
 */
struct ln_con {
    char * ln;
    struct ln_con *nxt;
};
static void execio_diskr(fp, buf_len, el_read, el_len, ln)
char * fp;
int buf_len;
char * (el_read)();
int (el_len)();
char *** ln;
{
int n;
struct ln_con * anchor, * tail;
char * buf;
char **lp;

    if ((buf = (char *) malloc(buf_len)) == NULL)
    {
        perror("malloc() failed");
        *ln = (char **) NULL;
        return;
    }
    n = 0;
    anchor = (struct ln_con *) NULL;
    while ((el_read(buf, buf_len, fp)) != NULL)
    {
        n++;
        if (anchor == (struct ln_con *) NULL)
        {
            anchor = (struct ln_con *) malloc(sizeof(struct ln_con));
            tail = anchor;
        }
        else
        {
            tail->nxt = (struct ln_con *) malloc(sizeof(struct ln_con));
            tail = tail->nxt;
        }
        tail->nxt = (struct ln_con *) NULL;
        tail->ln = el_clone(buf, el_len(buf));
    }
    *ln = (char **) malloc(sizeof(char *)*(n+1));
    **ln = (char *) malloc( sizeof(long int));
    *((long int *) (**ln)) = n;
    for (tail = anchor, lp = *ln + 1;
             tail != (struct ln_con *) NULL;
                 lp++)
    {
        *lp = tail->ln;
        anchor = tail;
        tail = tail->nxt;
        free((char *) anchor);
    }
    free(buf);
    return;
}
static void test_routine(label, tchan)
char * label;
struct tok_chan * tchan;
{
unsigned char **anchor;
int ln_cnt, i;

    fputs(label, stdout);
    fputs("--------------------\n", stdout);
    fputs("Input\n", stdout);
    fputs("--------------------\n", stdout);
    fputs(tchan->buf, stdout);
    fputs("\n--------------------\n", stdout);
    execio_diskr((char *) tchan, 258, tgets, tlen, &anchor);
    for (ln_cnt = (*((long int *) (anchor[0]))), i = 1; i <= ln_cnt; i++)
        fprintf(stdout,"%02x(%c) %u (%*s)\n",
             *(anchor[i]),
             (*(anchor[i]) > E2_BIN)?  *(anchor[i]): *(anchor[i]) + 48,
             *(anchor[i]+1),
             *(anchor[i]+1),
             (anchor[i]+2));
    fputs("--------------------\n", stdout);
    fputs("Reconstitute\n", stdout);
    fputs("--------------------\n", stdout);
    for (ln_cnt = (*((long int *) (anchor[0]))), i = 1; i <= ln_cnt; i++)
    {
         if ( (*(anchor[i]) > E2_BIN))
             fwrite( anchor[i],1,1, stdout);
         else
             fwrite(anchor[i]+2,1,*(anchor[i]+1), stdout);
    }
    fputs("\n====================\n", stdout);
    return;
}
main()
{
struct tok_chan test1;
struct tok_chan test2;

    test1.buf = "POST /AGRUKIsolator/PunchoutListenerFormField.ashx?sessionid=cd444e6f-dfb7-4c8d-8d72-79270de776d4 HTTP/1.1\r\n\
Host: dc1-abw-fe-dev\r\n\
User-Agent: Mozilla/5.0 (Windows NT 6.1; rv:29.0) Gecko/20100101 Firefox/29.0\r\n\
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n\
Accept-Language: en-US,en;q=0.5\r\n\
Accept-Encoding: gzip, deflate\r\n\
Referer: http://scot-ccm-preprod.eurodyn.com/ccm-preprod/punchout/sendPunchOutOrderMessage.do?requisitionId=4589\r\n\
Cookie: ASP.NET_SessionId=x0q4wqrdikb4bb3fepkv4cf0; .ASPXAUTH=\
66FB8D4B9A491B09A4373A3D74C80FF4E240DD7F56C14CDD4786D1A8759E5F5E0B370F0B62C9ECE14B3243842914916D1497B2845D18EF24C8080B58F7669D47\
88D2B1D4246EC0DD73BADAFEFE5F0C25B635A91DCB1868FFA19F68254C77FBD3E3901D5F3D12B4431B8275C4CEEFE991798AFE0ED85F172726B39A9C91C3693A\
0CBB1FCC8EAF861A70FF639E2EA56021; Template_146_STRG01=order_id=True&article=True&xwf_state=True&art_descr=True&unit_code=True&\
rev_val=True&amount=True&xapar_id=True&order_id2=True&deliv_date=True&contract_id=True\r\n\
Connection: keep-alive\r\n\
Content-Type: application/x-www-form-urlencoded\r\n\
Content-Length: 2988\r\n\
VIA: 1.0 localhost:6501 (E2 Systems Script Capture)\r\n\
\r\n\
cxml-urlencoded=%3C%3Fxml+version%3D%221.0%22+encoding%3D%22UTF-8%22%3F%3E%0D%0A%3C%21DOCTYPE+cXML+SYSTEM+%22http%3A%2F%2Fxml.\
cXML.org%2Fschemas%2FcXML%2F1.1.010%2FcXML.dtd%22%3E%0D%0A%3CcXML+xml%3Alang%3D%22en%22+timestamp%3D%222014-05-17T16%3A48%3A42%\
2B03%3A00%22+payloadID%3D%221400334522434.http-0.0.0.0-8180-9%4010.250.56.10%22%3E%0D%0A++++%3CHeader%3E%0D%0A++++++++%3CFrom%\
3E%0D%0A++++++++++++%3CCredential+domain%3D%22DUNS%22%3E%0D%0A++++++++++++++++%3CIdentity%3E728423943%3C%2FIdentity%3E%0D%0A++++\
++++++++%3C%2FCredential%3E%0D%0A++++++++%3C%2FFrom%3E%0D%0A++++++++%3CTo%3E%0D%0A++++++++++++%3CCredential+domain%3D%\
22NetworkId%22%3E%0D%0A++++++++++++++++%3CIdentity%3Eagresso%3C%2FIdentity%3E%0D%0A++++++++++++%3C%2FCredential%3E%0D%0A++++++++\
%3C%2FTo%3E%0D%0A++++++++%3CSender%3E%0D%0A++++++++++++%3CCredential+domain%3D%22DUNS%22%3E%0D%0A++++++++++++++++%3CIdentity%\
3E728423943%3C%2FIdentity%3E%0D%0A++++++++++++%3C%2FCredential%3E%0D%0A++++++++++++%3CUserAgent%3ECCM%3C%2FUserAgent%3E%0D%0A+++\
+++++%3C%2FSender%3E%0D%0A++++%3C%2FHeader%3E%0D%0A++++%3CMessage+deploymentMode%3D%22production%22%3E%0D%0A++++++++%\
3CPunchOutOrderMessage%3E%0D%0A++++++++++++%3CBuyerCookie%3Ecd444e6f-dfb7-4c8d-8d72-79270de776d4%3C%2FBuyerCookie%3E%0D%0A++++++\
++++++%3CPunchOutOrderMessageHeader+quoteStatus%3D%22final%22+operationAllowed%3D%22edit%22%3E%0D%0A++++++++++++++++%3CTotal%3E%\
0D%0A++++++++++++++++++++%3CMoney+currency%3D%22GBP%22%3E68.16%3C%2FMoney%3E%0D%0A++++++++++++++++%3C%2FTotal%3E%0D%0A++++++++++\
++%3C%2FPunchOutOrderMessageHeader%3E%0D%0A++++++++++++%3CItemIn+quantity%3D%222%22%3E%0D%0A++++++++++++++++%3CItemID%3E%0D%0A++\
++++++++++++++++++%3CSupplierPartID%3E548035%3C%2FSupplierPartID%3E%0D%0A++++++++++++++++++++%3CSupplierPartAuxiliaryID%3E4589%\
3C%2FSupplierPartAuxiliaryID%3E%0D%0A++++++++++++++++%3C%2FItemID%3E%0D%0A++++++++++++++++%3CItemDetail%3E%0D%0A++++++++++++++++\
++++%3CUnitPrice%3E%0D%0A++++++++++++++++++++++++%3CMoney+currency%3D%22GBP%22%3E34.08%3C%2FMoney%3E%0D%0A++++++++++++++++++++%\
3C%2FUnitPrice%3E%0D%0A++++++++++++++++++++%3CDescription+xml%3Alang%3D%22en%22%3ESoap+Foam+Everyday+Use+Kimcare+General+Case+\
Of+6+X+1l+6340%3C%2FDescription%3E%0D%0A++++++++++++++++++++%3CUnitOfMeasure%3ECA%3C%2FUnitOfMeasure%3E%0D%0A+++++++++++++++++++\
+%3CClassification+domain%3D%22UNSPSC%22%3E53131608%3C%2FClassification%3E%0D%0A++++++++++++++++++++%3CManufacturerPartID%3E%3C%\
2FManufacturerPartID%3E%0D%0A++++++++++++++++++++%3CManufacturerName%3E%3C%2FManufacturerName%3E%0D%0A++++++++++++++++++++%\
3CLeadTime%3E%3C%2FLeadTime%3E%0D%0A++++++++++++++++++++%3CExtrinsic+name%3D%22fixedAssetCapitalItem%22%3E%3C%2FExtrinsic%3E%0D%\
0A++++++++++++++++++++%3CExtrinsic+name%3D%22valuableAndAttractive%22%3E%3C%2FExtrinsic%3E%0D%0A++++++++++++++++%3C%\
2FItemDetail%3E%0D%0A++++++++++++++++%3CSupplierID+domain%3D%22DUNS%22%3E578706251%3C%2FSupplierID%3E%0D%0A++++++++++++%3C%\
2FItemIn%3E%0D%0A++++++++%3C%2FPunchOutOrderMessage%3E%0D%0A++++%3C%2FMessage%3E%0D%0A%3C%2FcXML%3E";
    test1.buf_len = strlen(test1.buf);
    test1.cur_pos = test1.buf;
    test1.cont_flag = 0;
    test2.buf = "HTTP/1.1 200 OK\r\n\
Cache-Control: no-cache, no-store\r\n\
Pragma: no-cache\r\n\
Content-Type: text/html; charset=utf-8\r\n\
Expires: -1\r\n\
Server: Microsoft-IIS/8.0\r\n\
X-AspNet-Version: 4.0.30319\r\n\
X-Powered-By: ASP.NET\r\n\
Date: Sat, 17 May 2014 13:46:54 GMT\r\n\
Content-Length: 3632\r\n\
\r\n\
<html><head><script type=\"text/javascript\">function onLoad(){ window.document.getElementById(\"listenform\").submit(); }</script><\
/head><body onload=\"onLoad()\"><P>Your basket should automatically be passed back to Agresso Self Service in a second.</P><BR/><\
P>If it does not get passed back automatically, press the button below.</P><BR/><form method=\"post\" id=\"listenform\" action=\"\
http://dc1-abw-fe-dev/Agresso/Custom/ReqPunchBack.aspx?SESSIONID=CD444E6F-DFB7-4C8D-8D72-79270DE776D4\"><input type=\"hidden\" \
name=\"ispostback\" value=\"\
PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiPz4NCjwhRE9DVFlQRSBjWE1MIFNZU1RFTSAiaHR0cDovL3htbC5jWE1MLm9yZy9zY2hlbWFzL2NYTUwv\
MS4xLjAxMC9jWE1MLmR0ZCI+\
DQo8Y1hNTCB4bWw6bGFuZz0iZW4iIHRpbWVzdGFtcD0iMjAxNC0wNS0xN1QxNjo0ODo0MiswMzowMCIgcGF5bG9hZElEPSIxNDAwMzM0NTIyNDM0Lmh0dHAtMC4wLjAu\
MC04MTgwLTlAMTAuMjUwLjU2LjEwIj4NCiAgICA8SGVhZGVyPg0KICAgICAgICA8RnJvbT4NCiAgICAgICAgICAgIDxDcmVkZW50aWFsIGRvbWFpbj0iRFVOUyI+\
DQogICAgICAgICAgICAgICAgPElkZW50aXR5PjcyODQyMzk0MzwvSWRlbnRpdHk+DQogICAgICAgICAgICA8L0NyZWRlbnRpYWw+\
DQogICAgICAgIDwvRnJvbT4NCiAgICAgICAgPFRvPg0KICAgICAgICAgICAgPENyZWRlbnRpYWwgZG9tYWluPSJOZXR3b3JrSWQiPg0KICAgICAgICAgICAgICAgIDxJ\
ZGVudGl0eT5hZ3Jlc3NvPC9JZGVudGl0eT4NCiAgICAgICAgICAgIDwvQ3JlZGVudGlhbD4NCiAgICAgICAgPC9Ubz4NCiAgICAgICAgPFNlbmRlcj4NCiAgICAgICAg\
ICAgIDxDcmVkZW50aWFsIGRvbWFpbj0iRFVOUyI+DQogICAgICAgICAgICAgICAgPElkZW50aXR5PjcyODQyMzk0MzwvSWRlbnRpdHk+\
DQogICAgICAgICAgICA8L0NyZWRlbnRpYWw+\
DQogICAgICAgICAgICA8VXNlckFnZW50PkNDTTwvVXNlckFnZW50Pg0KICAgICAgICA8L1NlbmRlcj4NCiAgICA8L0hlYWRlcj4NCiAgICA8TWVzc2FnZSBkZXBsb3lt\
ZW50TW9kZT0icHJvZHVjdGlvbiI+\
DQogICAgICAgIDxQdW5jaE91dE9yZGVyTWVzc2FnZT4NCiAgICAgICAgICAgIDxCdXllckNvb2tpZT5jZDQ0NGU2Zi1kZmI3LTRjOGQtOGQ3Mi03OTI3MGRlNzc2ZDQ8\
L0J1eWVyQ29va2llPg0KICAgICAgICAgICAgPFB1bmNoT3V0T3JkZXJNZXNzYWdlSGVhZGVyIHF1b3RlU3RhdHVzPSJmaW5hbCIgb3BlcmF0aW9uQWxsb3dlZD0iZWRp\
dCI+DQogICAgICAgICAgICAgICAgPFRvdGFsPg0KICAgICAgICAgICAgICAgICAgICA8TW9uZXkgY3VycmVuY3k9IkdCUCI+\
NjguMTY8L01vbmV5Pg0KICAgICAgICAgICAgICAgIDwvVG90YWw+\
DQogICAgICAgICAgICA8L1B1bmNoT3V0T3JkZXJNZXNzYWdlSGVhZGVyPg0KICAgICAgICAgICAgPEl0ZW1JbiBxdWFudGl0eT0iMiI+\
DQogICAgICAgICAgICAgICAgPEl0ZW1JRD4NCiAgICAgICAgICAgICAgICAgICAgPFN1cHBsaWVyUGFydElEPjU0ODAzNTwvU3VwcGxpZXJQYXJ0SUQ+\
DQogICAgICAgICAgICAgICAgICAgIDxTdXBwbGllclBhcnRBdXhpbGlhcnlJRD40NTg5PC9TdXBwbGllclBhcnRBdXhpbGlhcnlJRD4NCiAgICAgICAgICAgICAgICA8\
L0l0ZW1JRD4NCiAgICAgICAgICAgICAgICA8SXRlbURldGFpbD4NCiAgICAgICAgICAgICAgICAgICAgPFVuaXRQcmljZT4NCiAgICAgICAgICAgICAgICAgICAgICAg\
IDxNb25leSBjdXJyZW5jeT0iR0JQIj4zNC4wODwvTW9uZXk+\
DQogICAgICAgICAgICAgICAgICAgIDwvVW5pdFByaWNlPg0KICAgICAgICAgICAgICAgICAgICA8RGVzY3JpcHRpb24geG1sOmxhbmc9ImVuIj5Tb2FwIEZvYW0gRXZl\
cnlkYXkgVXNlIEtpbWNhcmUgR2VuZXJhbCBDYXNlIE9mIDYgWCAxbCA2MzQwPC9EZXNjcmlwdGlvbj4NCiAgICAgICAgICAgICAgICAgICAgPFVuaXRPZk1lYXN1cmU+\
Q0E8L1VuaXRPZk1lYXN1cmU+DQogICAgICAgICAgICAgICAgICAgIDxDbGFzc2lmaWNhdGlvbiBkb21haW49IlVOU1BTQyI+\
NTMxMzE2MDg8L0NsYXNzaWZpY2F0aW9uPg0KICAgICAgICAgICAgICAgICAgICA8TWFudWZhY3R1cmVyUGFydElEPjwvTWFudWZhY3R1cmVyUGFydElEPg0KICAgICAg\
ICAgICAgICAgICAgICA8TWFudWZhY3R1cmVyTmFtZT48L01hbnVmYWN0dXJlck5hbWU+\
DQogICAgICAgICAgICAgICAgICAgIDxMZWFkVGltZT48L0xlYWRUaW1lPg0KICAgICAgICAgICAgICAgICAgICA8RXh0cmluc2ljIG5hbWU9ImZpeGVkQXNzZXRDYXBp\
dGFsSXRlbSI+PC9FeHRyaW5zaWM+\
DQogICAgICAgICAgICAgICAgICAgIDxFeHRyaW5zaWMgbmFtZT0idmFsdWFibGVBbmRBdHRyYWN0aXZlIj48L0V4dHJpbnNpYz4NCiAgICAgICAgICAgICAgICA8L0l0\
ZW1EZXRhaWw+DQogICAgICAgICAgICAgICAgPFN1cHBsaWVySUQgZG9tYWluPSJEVU5TIj41Nzg3MDYyNTE8L1N1cHBsaWVySUQ+\
DQogICAgICAgICAgICA8L0l0ZW1Jbj4NCiAgICAgICAgPC9QdW5jaE91dE9yZGVyTWVzc2FnZT4NCiAgICA8L01lc3NhZ2U+DQo8L2NYTUw+\"><input type=\"\
submit\" value=\"Pass basket back to Agresso\"></form></body></html>";
    test2.buf_len = strlen(test2.buf);
    test2.cur_pos = test2.buf;
    test2.cont_flag = 0;
    test_routine("Sample HTTP Request\n", &test1);
    test_routine("Sample HTTP Response\n", &test2);
    exit(0);
}
#endif
