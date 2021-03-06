#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <pwd.h>

#include "proto.h"

static int expand_env(char **oldp, char **newp, int space_left, int brace_flag)
{
    int i, n = 0;
    char env_buffer[MAX_ENV], *env_val;
    char *old = *oldp;
    char *new = *newp;

    i = 0;

    if (!brace_flag)
        while ((*old != ' ') && (*old != '\0'))	
            env_buffer[i++] = *old++;
    else {
        old++;
        while ((*old != '}')) {
            if (*old == '\0')
                return ENOBRACE;
            env_buffer[i++] = *old++;
        }
        old++;
    }

    env_buffer[i] = '\0';

    env_val = getenv(env_buffer);

    if (env_val != NULL) {
        if ((n = strlen(env_val)) > space_left)
            return ENOROOM;

        while (*env_val != '\0')
            *new++ = *env_val++;
    }

    *oldp = old - 1;
    *newp = new;
    return n;
}

static int expand_pid(char **newp, int space_left)
{
    int i, n;
    pid_t pid;
    char pid_buffer[10];
    char *new = *newp;

    pid = getpid();
    snprintf(pid_buffer, 10, "%d", pid);

    if ((n = strlen(pid_buffer)) > space_left) 
        return ENOROOM;
    i = 0;
    while (pid_buffer[i] != '\0')
        *new++ = pid_buffer[i++];

    *newp = new;
    return n;
}

static int expand_argv(char **oldp, char **newp, int space_left)
{
    char arg_idx[4], *arg;
    char *old = *oldp;
    char *new = *newp;
    int argn, n, i = 0;

    while (isdigit(*old))
        arg_idx[i++] = *old++;
    arg_idx[i] = '\0';

    argn = atoi(arg_idx);

    /* replace $0 with the name of the shell in interactive mode */	
    if ((cmdline_argc < 2) && (argn == 0)) {
        n = strlen(cmdline_argv[0]);
        if (n > space_left)
            return ENOROOM;
        arg = cmdline_argv[0];
        while (*arg != '\0')
            *new++ = *arg++;
    }

    else if ((argn + cmdline_shift) < (cmdline_argc)) {
        n = strlen(cmdline_argv[argn + cmdline_shift]);
        if (n > space_left)
            return ENOROOM;
        arg = cmdline_argv[argn + cmdline_shift];
        while (*arg != '\0')
            *new++ = *arg++;	
    }

    *oldp = (old - 1);
    *newp = new;

    return n;
}

static int expand_argc(char **newp, int space_left)
{
    char argc_buffer[10], *new = *newp;
    int n = snprintf(argc_buffer, 10, "%d", cmdline_argc - cmdline_shift);
    int i = 0;

    if (n > space_left)
        return ENOROOM;

    while (argc_buffer[i] != '\0')
        *new++ = argc_buffer[i++];

    *newp = new;
    return n;
}

static int expand_status(char **newp, int space_left)
{
    char status_buffer[10], *new = *newp;
    int n = snprintf(status_buffer, 10, "%d", prev_status);
    int i = 0;

    if (n > space_left)
        return ENOROOM;

    while (status_buffer[i] != '\0')
        *new++ = status_buffer[i++];

    *newp = new;

    return n;
}

static int expand_wildcard(char **oldp, char **newp, int space_left)
{
    DIR *dp;
    struct dirent *d_ent;

    char pattern_buffer[100], dir_buffer[100];
    char *old = *oldp;
    char *new = *newp;
    int i = 0, n = 0;

    /* open directory for checking all it's files*/
    getcwd(dir_buffer, 100);
    dp = opendir(dir_buffer);	

    /* set up the pattern buffer */
    while (!isspace(*old) && (*old != '\"') && (*old != '\0'))
        pattern_buffer[i++] = *old++;
 
    pattern_buffer[i++] = '\0';

    while ((d_ent = readdir(dp)) != NULL) {
        if (d_ent->d_name[0] == '.')
            continue;
        if (is_match(d_ent->d_name, pattern_buffer)) {
            n += strlen(d_ent->d_name) + 1;
            if (n >= space_left)
                return ENOROOM;
            i = 0;
            while (d_ent->d_name[i] != '\0') {
                *new++ = d_ent->d_name[i++];
                n++;
            }

            *new++ = ' ';
        }
    }

    *oldp = old; 
    *newp = new;

    closedir(dp);

    return n;
}

static int expand_home(char **oldp, char **newp, int space_left)
{
    char username[20];
    char *old = *oldp;
    char *new = *newp;
    struct passwd *p;
    int i = 0, n = 0; 

    if (++*old == '/')
        p = getpwuid(getuid());
    else {
        while ((*old != '/') || (*old != '\0') || (!isspace(*old)))
            username[i++] = *old++;
        username[i] = '\0';
        
        p = getpwnam(username);
    }

    *new++ = '~';
    if (p != NULL) {
        n = strlen(p->pw_dir) + 1;
        if (n > space_left)
            return ENOROOM;
        while (*(p->pw_dir) != '\0')
            *new++ = *(p->pw_dir)++;
    }

    *oldp = old;
    *newp = new;
    return n;
}

int expand(char *old, char *new, int newsize)
{
    int dollar_flag = 0;
    int i = 0, rv, brace_flag;

    for ( ; *old != '\0'; old++) {
        if (dollar_flag) {
            if ((*old) == '$') {
                if ((rv = expand_pid(&new, newsize - i)) > 0)
                    i += rv;
            } else if ((*old) == ' ') {
                *new++ = '$';
                i++;
            } else if (isdigit(*old)) { 
                if ((rv = expand_argv(&old, &new, newsize - i)) > 0)
                    i += rv;
            } else if ((*old) == '#') {
                if ((rv = expand_argc(&new, newsize - i)) > 0)
                    i += rv;
            } else if ((*old) == '?') {
                if ((rv = expand_status(&new, newsize - i)) > 0)
                    i += rv;
            } else {
                brace_flag = ((*old) == '{');
                if ((rv = expand_env(&old, &new, newsize - i, brace_flag)) > 0) 
                    i += rv;
            }
            dollar_flag = 0;
        }

        else {
            if (*old == '$')
                dollar_flag = 1;
            else if (*old == '*') {
                while (!isspace(*old) && *old != '\"')
                    old--;
                old++;

                while (!isspace(*new) && *new != '\"') {
                    new--;
                    i--;
                }
                new++;
                i++;

                if ((rv = expand_wildcard(&old, &new, newsize - i)) > 0)
                    i += rv;
            } else if (*old == '~') {
                if ((rv = expand_home(&old, &new, newsize - i)) > 0)
                    i += rv;
            
            } else if (*old == '#') /* if we reach a comment we can ignore the rest of the line */
                break;

            else {
                *new++ = *old;
                i++;
            }
        }

        if (i >= newsize)
            return ENOROOM;
    }

    *new = '\0';
    return 0;
}
