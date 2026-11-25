#include <fcntl.h>
#include <string.h>
#include <sys/reboot.h>
#include <unistd.h>

static void console_message(const char *msg)
{
    int fd = open("/dev/console", O_WRONLY);

    if (fd >= 0) {
        ssize_t ignored = write(fd, msg, strlen(msg));
        (void)ignored;
        close(fd);
    }
}

int main(int argc, char **argv)
{
    static const char failed[] = "SYSTEMD_POWEROFF_FAILED\n";
    const char *marker = "SYSTEMD_OK";
    int poweroff = 1;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-poweroff") == 0)
            poweroff = 0;
        else
            marker = argv[i];
    }

    console_message(marker);
    console_message("\n");
    if (!poweroff)
        return 0;
    sync();
    if (reboot(RB_POWER_OFF) != 0) {
        console_message(failed);
        return 1;
    }
    return 0;
}
