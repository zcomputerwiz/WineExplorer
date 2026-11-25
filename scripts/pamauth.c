/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * pamauth SERVICE USER < password-line
 *
 * A test tool (scripts/test_blank_password.sh): runs pam_authenticate for USER on the PAM service SERVICE, answering every
 * password prompt with the first line of standard input (an empty line is an empty password). Exit 0: authenticated,
 * 1: refused, 2: usage or PAM could not start. Build: cc -o pamauth pamauth.c -lpam
 */
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char answer[256];

static int conv(int n, const struct pam_message **msg, struct pam_response **resp, void *data)
{
    struct pam_response *r = calloc(n, sizeof(*r));
    int i;

    (void)data;
    if (!r) return PAM_BUF_ERR;
    for (i = 0; i < n; i++)
    {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF || msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
            r[i].resp = strdup(answer);
    }
    *resp = r;
    return PAM_SUCCESS;
}

int main(int argc, char **argv)
{
    struct pam_conv c = { conv, NULL };
    pam_handle_t *h = NULL;
    int rc;

    if (argc != 3) { fprintf(stderr, "usage: pamauth SERVICE USER < password-line\n"); return 2; }
    if (!fgets(answer, sizeof(answer), stdin)) answer[0] = 0;
    answer[strcspn(answer, "\n")] = 0;
    if (pam_start(argv[1], argv[2], &c, &h) != PAM_SUCCESS) return 2;
    rc = pam_authenticate(h, 0);
    pam_end(h, rc);
    return rc == PAM_SUCCESS ? 0 : 1;
}
