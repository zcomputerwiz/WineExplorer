#include "textsetup.h"
#include <ctype.h>

static void parse_unattend_sif(SetupState *state)
{
    FILE *f = fopen(state->unattend_path, "r");
    if (!f) return;

    char line[512];
    bool in_unattended_sec = false;

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == ';' || *p == '#' || *p == '\0') continue;

        if (*p == '[') {
            char sec[64];
            if (sscanf(p, "[%63[^]]", sec) == 1) {
                in_unattended_sec = (strcasecmp(sec, "Unattended") == 0);
            }
            continue;
        }

        if (in_unattended_sec) {
            char key[64], val[128];
            if (sscanf(p, "%63[^=]=%127[^\r\n]", key, val) == 2) {
                char *k = key;
                while (*k && isspace((unsigned char)*k)) k++;
                char *kend = k + strlen(k) - 1;
                while (kend > k && isspace((unsigned char)*kend)) *kend-- = '\0';

                char *v = val;
                while (*v && isspace((unsigned char)*v)) v++;
                char *vend = v + strlen(v) - 1;
                while (vend > v && isspace((unsigned char)*vend)) *vend-- = '\0';
                if (*v == '"' && *vend == '"' && vend > v) {
                    *vend = '\0';
                    v++;
                }

                if (strcasecmp(k, "TargetDisk") == 0) {
                    snprintf(state->target_disk, sizeof(state->target_disk), "%s", v);
                } else if (strcasecmp(k, "Repartition") == 0) {
                    state->repartition = (strcasecmp(v, "Yes") == 0);
                }
            }
        }
    }
    fclose(f);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);

    SetupState state;
    memset(&state, 0, sizeof(state));

    /* Check unattended mode ONLY from kernel command line: wexunattend=<path> */
    FILE *fcmd = fopen("/proc/cmdline", "r");
    if (fcmd) {
        char cmdline[1024];
        if (fgets(cmdline, sizeof(cmdline), fcmd)) {
            char *p = strstr(cmdline, "wexunattend=");
            if (p) {
                sscanf(p + 12, "%255s", state.unattend_path);
                /* a CD built without an answer file still has the menu entry: install interactively then */
                state.unattended = access(state.unattend_path, R_OK) == 0;
            }
        }
        fclose(fcmd);
    }

    /* Parse answer file if unattended */
    if (state.unattended && state.unattend_path[0]) {
        parse_unattend_sif(&state);
    }

    /* Check UEFI firmware */
    if (access("/sys/firmware/efi", F_OK) == 0) {
        state.is_uefi = true;
    }

    ui_init();

    SetupStage current_stage = STAGE_WELCOME;
    while (current_stage != STAGE_QUIT) {
        switch (current_stage) {
            case STAGE_WELCOME:
                current_stage = screen_welcome(&state);
                break;
            case STAGE_LICENSE:
                current_stage = screen_license(&state);
                break;
            case STAGE_PARTITION:
                current_stage = screen_partition(&state);
                break;
            case STAGE_FORMAT:
                current_stage = screen_format(&state);
                break;
            case STAGE_COPY:
                current_stage = screen_copy(&state);
                break;
            case STAGE_INIT:
                current_stage = screen_init(&state);
                break;
            case STAGE_REBOOT:
                current_stage = screen_reboot(&state);
                break;
            default:
                current_stage = STAGE_QUIT;
                break;
        }
    }

    ui_shutdown();
    return 0;
}
