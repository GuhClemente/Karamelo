#include "getopt.h"
#include <string.h>
#include <stdio.h>

char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = '?';

static char *nextchar = NULL;

int getopt(int argc, char *const argv[], const char *optstring) {
    if (optind >= argc || argv[optind] == NULL || argv[optind][0] != '-' || argv[optind][1] == '\0') {
        return -1;
    }
    if (strcmp(argv[optind], "--") == 0) {
        optind++;
        return -1;
    }

    if (!nextchar || !*nextchar) {
        nextchar = argv[optind] + 1;
    }

    char c = *nextchar++;
    const char *oli = strchr(optstring, c);

    if (c == ':' || oli == NULL) {
        optopt = c;
        if (!*nextchar) optind++;
        return '?';
    }

    if (oli[1] == ':') {
        if (*nextchar != '\0') {
            optarg = nextchar;
            optind++;
            nextchar = NULL;
        } else if (optind + 1 < argc) {
            optind++;
            optarg = argv[optind];
            optind++;
            nextchar = NULL;
        } else {
            optopt = c;
            optind++;
            nextchar = NULL;
            return (optstring[0] == ':') ? ':' : '?';
        }
    } else {
        if (!*nextchar) {
            optind++;
            nextchar = NULL;
        }
    }
    return c;
}

int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex) {
    if (optind >= argc || argv[optind] == NULL) {
        return -1;
    }

    char *arg = argv[optind];
    if (arg[0] == '-' && arg[1] == '-' && arg[2] != '\0') {
        const char *name = arg + 2;
        const char *eq = strchr(name, '=');
        size_t namelen = eq ? (size_t)(eq - name) : strlen(name);

        for (int i = 0; longopts && longopts[i].name; i++) {
            if (strncmp(longopts[i].name, name, namelen) == 0 && strlen(longopts[i].name) == namelen) {
                if (longindex) *longindex = i;
                if (longopts[i].has_arg == required_argument) {
                    if (eq) {
                        optarg = (char *)(eq + 1);
                        optind++;
                    } else if (optind + 1 < argc) {
                        optind++;
                        optarg = argv[optind];
                        optind++;
                    } else {
                        optopt = longopts[i].val;
                        optind++;
                        return ':';
                    }
                } else if (longopts[i].has_arg == optional_argument) {
                    if (eq) {
                        optarg = (char *)(eq + 1);
                    } else {
                        optarg = NULL;
                    }
                    optind++;
                } else {
                    if (eq) {
                        optind++;
                        return '?';
                    }
                    optarg = NULL;
                    optind++;
                }
                if (longopts[i].flag) {
                    *longopts[i].flag = longopts[i].val;
                    return 0;
                }
                return longopts[i].val;
            }
        }
        optopt = 0;
        optind++;
        return '?';
    }

    return getopt(argc, argv, optstring);
}
