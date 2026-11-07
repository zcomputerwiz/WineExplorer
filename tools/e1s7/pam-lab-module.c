/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Test-only PAM conversation fault module. No install/production integration.
 */
#include <security/pam_modules.h>
#include <security/pam_appl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

static int descriptors_cloexec(void)
{
    DIR *dir=opendir("/proc/self/fd");
    struct dirent *entry;
    int ok=1;
    if (!dir) return 0;
    while ((entry=readdir(dir)))
    {
        char *end;
        long fd=strtol(entry->d_name,&end,10);
        int flags;
        if (*end || fd<3 || fd==dirfd(dir)) continue;
        flags=fcntl((int)fd,F_GETFD);
        if (flags<0 || !(flags&FD_CLOEXEC)) { ok=0; break; }
    }
    closedir(dir); return ok;
}

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    const void *item;
    const struct pam_conv *conv;
    unsigned int rounds=2, i;
    int j, result, oversize=0, unsupported=0;
    char long_text[4098];
    (void)flags;
    /* Includes the executable pin: it must be closed before PAM is entered.
     * Any remaining worker descriptor must disappear on a helper exec. */
    if (!descriptors_cloexec()) return PAM_SYSTEM_ERR;
    for (j=0; j<argc; j++)
    {
        if (!strcmp(argv[j],"rounds=9")) rounds=9;
        else if (!strcmp(argv[j],"oversize")) oversize=1;
        else if (!strcmp(argv[j],"unsupported")) unsupported=1;
        else if (!strcmp(argv[j],"block")) { sleep(30); return PAM_AUTH_ERR; }
        else if (!strcmp(argv[j],"redirect")) return pam_set_item(pamh,PAM_USER,"root");
        else return PAM_SERVICE_ERR;
    }
    if (pam_get_item(pamh,PAM_CONV,&item)!=PAM_SUCCESS || !item) return PAM_SYSTEM_ERR;
    conv=item;
    memset(long_text,'x',sizeof(long_text)-1); long_text[sizeof(long_text)-1]=0;
    for (i=0; i<rounds; i++)
    {
        struct pam_message info={PAM_TEXT_INFO,"fixture information"};
        struct pam_message echo={PAM_PROMPT_ECHO_ON,"fixture label"};
        struct pam_message secret={PAM_PROMPT_ECHO_OFF,"fixture challenge"};
        const struct pam_message *messages[]={&info,&echo,&secret};
        struct pam_response *responses=NULL;
        if (oversize) secret.msg=long_text;
        if (unsupported) secret.msg_style=999;
        result=conv->conv(3,messages,&responses,conv->appdata_ptr);
        if (result!=PAM_SUCCESS) return result;
        result=(responses && !responses[0].resp && responses[1].resp && responses[2].resp &&
                !strcmp(responses[1].resp,"label") && !strcmp(responses[2].resp,"E1S7-SECRET-CANARY"))
               ? PAM_SUCCESS : PAM_AUTH_ERR;
        if (responses)
        {
            for (j=0; j<3; j++) if (responses[j].resp)
            {
                volatile char *p=responses[j].resp;
                size_t n=strlen(responses[j].resp);
                while (n--) *p++=0;
                free(responses[j].resp);
            }
            free(responses);
        }
        if (result!=PAM_SUCCESS) return result;
    }
    return PAM_SUCCESS;
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags, int argc, const char **argv)
{ (void)pamh; (void)flags; (void)argc; (void)argv; return PAM_SERVICE_ERR; }
PAM_EXTERN int pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc, const char **argv)
{ (void)pamh; (void)flags; (void)argc; (void)argv; return PAM_SUCCESS; }
