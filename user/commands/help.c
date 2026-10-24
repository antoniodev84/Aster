#include "common.h"
int main(void) {
    puts("Commands (each is a separate ELF64):\n  cat chmod chown clear cp echo edit env false "
         "grep head hexdump\n  id kill ln ls mapwrite mkdir printf ps pwd readlink rm rmdir seq "
         "sigwatch sleep sort spin stat su time\n  "
         "sync tail tee test touch true uname wc whoami yes\n  ping ifconfig udhcp httpd wget\nShell builtins:\n  cd export unset "
         "exec exit read set shift source . umask wait\n  jobs fg bg trap break continue :\nShell "
         "syntax:\n  scripts, quotes, $variables, $? $$ $! $#, $(command), globs,\n  pipes, < > >> "
         "2> 2>&1, && ||, background &, if/elif/else,\n  for, while, until, subshells and "
         "positional arguments.\nEditor: edit FILE; Ctrl-S saves, Ctrl-Q quits, arrow keys move.");
    return 0;
}
