#include "textsetup.h"

int main(int argc, char *argv[])
{
    SetupState state;
    memset(&state, 0, sizeof(state));

    /* Check unattended mode */
    if (access("/setup/unattend.sif", F_OK) == 0) {
        state.unattended = true;
        snprintf(state.unattend_path, sizeof(state.unattend_path), "/setup/unattend.sif");
    } else {
        FILE *fcmd = fopen("/proc/cmdline", "r");
        if (fcmd) {
            char cmdline[1024];
            if (fgets(cmdline, sizeof(cmdline), fcmd)) {
                char *p = strstr(cmdline, "wexunattend=");
                if (p) {
                    state.unattended = true;
                    sscanf(p + 12, "%255s", state.unattend_path);
                }
            }
            fclose(fcmd);
        }
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
