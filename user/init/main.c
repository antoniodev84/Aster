#include <os.h>
static volatile int changed;
static void child_changed(int signal) {
    (void)signal;
    changed = 1;
}
int main(void) {
    signal(SIGCHLD, child_changed);
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    puts("[init] PID 1 started");
    for (;;) {
        pid_t child = fork();
        if (child < 0) {
            perror("init: fork");
            sleep(1);
            continue;
        }
        if (!child) {
            signal(SIGCHLD, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            setpgid(0, 0);
            tcsetpgrp(0, getpid());
            execl("/bin/sh", "sh", (char *)NULL);
            perror("init: exec sh");
            _exit(127);
        }
        setpgid(child, child);
        tcsetpgrp(0, child);
        int status;
        pid_t pid;
        do {
            pid = waitpid(-1, &status, 0);
            if (pid < 0 && errno != EINTR)
                break;
        } while (pid != child);
        changed = 0;
        puts("[init] shell exited; restarting");
    }
}
