/*
 * This rubbish is required on Linux because the PAM library includes fork()
 * and calls malloc() before exec(). Morons.
 *
 * But it is useful to have it, because it allows the Windows authentication
 * code to be cleanly incorporated.
 */
static char * sccs_id="@(#) $Name$ $Id$\n\
Copyright (C) E2 Systems Limited 1995, 2012";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef MINGW32
#include <windows.h>
static int validate(username, passwd)
char * username;
char * passwd;
{
HANDLE hToken;

    if ( LogonUser(
            username,
            NULL,
            passwd,
            LOGON32_LOGON_NETWORK,
            LOGON32_PROVIDER_DEFAULT,
            &hToken))
    {
        CloseHandle(hToken);
        return 1;
    }
    return 0;
}
#else
#include <unistd.h>
#ifdef FRIG
static int validate(username, passwd)
char * username;
char * passwd;
{
    if (!strcmp(username,"hacktest") &&  !strcmp(passwd,"B4dguy5s!0"))
        return 1;
    else
        return 0;
}
#else
#include <security/pam_appl.h>
/*
 * The only way to pass the password in ...
 */
static int noprompt_conv(num_msg, msg, resp, appdata_ptr)
int num_msg;
const struct pam_message **msg;
struct pam_response **resp;
void *appdata_ptr;
{
    *resp = (struct pam_response *) appdata_ptr;
    return PAM_SUCCESS;
}
#define SHOW_PAM_ERROR(pamh, contxt, ret) { fprintf(stderr, "%s error : %s\n", contxt, pam_strerror(pamh,ret)); }
/*
 * Validate user and password
 */
static int validate(username, passwd)
char * username;
char * passwd;
{
pam_handle_t *pamh;
int ret;
struct pam_conv pc;
struct pam_response *resp;

    pc.conv = noprompt_conv;
    resp = (struct pam_response *) malloc(sizeof(struct pam_response));
    resp->resp = strdup(passwd); /* PAM will ordinarily free() this */
    resp->resp_retcode = 0;
    pc.appdata_ptr = resp;       /* PAM will ordinarily free() this */
    if ((ret = pam_start("passwd", username, &pc, &pamh)) != PAM_SUCCESS)
    {
        SHOW_PAM_ERROR( pamh, "pam_start()", ret);
        free(resp->resp);
        free(resp);
        return 0;
    }
    else
    if ((ret = pam_authenticate(pamh, PAM_DISALLOW_NULL_AUTHTOK)) != PAM_SUCCESS)
    {
        SHOW_PAM_ERROR( pamh, "pam_authenticate()", ret);
    }
    else
    {
        if ((ret = pam_end(pamh, PAM_DATA_SILENT)) != PAM_SUCCESS)
            SHOW_PAM_ERROR( pamh, "Authenticated but pam_end() problem",ret);
        return 1;
    }
    if ((ret = pam_end(pamh, PAM_DATA_SILENT)) != PAM_SUCCESS)
        SHOW_PAM_ERROR( pamh, "pam_end()",ret);
    return 0;
}
#endif
#endif
/*
 * Accept the user and password on stdin, and output OK or not on stdout
 */
int main(argc, argv)
int argc;
char ** argv;
{
char user_pass[128];
int i;
int r;
char * pwd;

    r = read(0, user_pass, sizeof(user_pass) - 1);
    if (r > 0)
    {
        user_pass[r] = '\0';
        for (pwd = user_pass; *pwd != '\0' && pwd < &user_pass[r]; pwd++);
        pwd++;
        if (pwd <  &user_pass[r])
        {
             if (validate(user_pass, pwd))
             {
                 write(1, "OK", 2);
                 exit(0);
             }
        }
    }
    write(1, "Failed", 6);
    exit(1);
}
