#ifndef GLOBALS_C
#define GLOBALS_C

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "defs.h"

int file_map_idx = 0;
char *FILE_NAMES[MAX_FILE];
char *FILE_SOURCES[MAX_FILE];

int macros_idx = 0;
macro_t *MACROS;

void cptok(token_t *dst, token_t *src)
{
    if (!dst && !src)
        return;
    dst->typ = src->typ;
    memcpy(&dst->loc, &src->loc, sizeof(location_t));
    strcpy(dst->literal, src->literal);
}

int file_map_add_entry(char *file_path, char **source_ref, int *len_ref)
{
    char buffer[MAX_LINE_LEN], source = calloc(MAX_SOURCE, sizeof(char));
    int length = 0;
    FILE *f = fopen(file_path, "rb");

    if (!f) {
        printf("[%s] Error: Failed to read file\n", file_path);
        exit(1);
    }

    for (;;) {
        if (!fgets(buffer, MAX_LINE_LEN, f))
            break;

        strcpy(source + length, buffer);
        length += strlen(buffer);
    }
    fclose(f);

    FILE_NAMES[file_map_idx] = calloc(strlen(file_path) + 1, sizeof(char));
    strcpy(FILE_NAMES[file_map_idx], file_path);
    FILE_SOURCES[file_map_idx++] = source;

    source_ref[0] = source;
    len_ref[0] = length;

    return file_map_idx - 1;
}

macro_t *add_macro(bool function_like) {
    macro_t *macro = &MACROS[macros_idx++];
    macro->function_like = function_like;
    macro->disabled = false;
    return macro;
}

macro_t *find_macro(char *name) {
    if (!name)
        return NULL;

    for (int i = 0; i < macros_idx; i++) {
        macro_t *macro = &MACROS[i];

        if (!macro->disabled && !strcmp(name, macro->name->literal))
            return macro;
    }

    return NULL;
}

void free_macros() {
    for (int i = 0; i < macros_idx; i++)
        if (MACROS[i].params)
            free(MACROS[i].params);
    free(MACROS);
}

void init_globals() {
    MACROS = malloc(MAX_MACROS * sizeof(macro_t));
}

void free_globals() {
    free_macros();

    for (int i = 0; i < file_map_idx; i++) {
        free(FILE_NAMES[i]);
        free(FILE_SOURCES[i]);
    }
}

/* produces an error message with location information then exits abnormally,
 * location is optional. */
void error(char *str, location_t *location, ...)
{
    /* sprintf logic copied from c.c, no boundary check included */
    char ERR_MSG_BUF[100];
    int *var_args = &str + 8;
    int si = 0, bi = 0, pi = 0;

    while (str[si]) {
        if (str[si] != '%') {
            ERR_MSG_BUF[bi] = str[si];
            bi++;
            si++;
        } else {
            int w = 0, zp = 0, pp = 0;

            si++;
            if (str[si] == '#') {
                pp = 1;
                si++;
            }
            if (str[si] == '0') {
                zp = 1;
                si++;
            }
            if (str[si] >= '1' && str[si] <= '9') {
                w = str[si] - '0';
                si++;
                if (str[si] >= '0' && str[si] <= '9') {
                    w = w * 10;
                    w += str[si] - '0';
                    si++;
                }
            }
            switch (str[si]) {
            case 37: /* % */
                ERR_MSG_BUF[bi++] = '%';
                si++;
                continue;
            case 99: /* c */
                ERR_MSG_BUF[bi++] = var_args[pi];
                break;
            case 115: /* s */
                strcpy(ERR_MSG_BUF + bi, var_args[pi]);
                bi += strlen(var_args[pi]);
                break;
            case 111: /* o */
                bi += __format(ERR_MSG_BUF + bi, var_args[pi], w, zp, 8, pp);
                break;
            case 100: /* d */
                bi += __format(ERR_MSG_BUF + bi, var_args[pi], w, zp, 10, 0);
                break;
            case 120: /* x */
                bi += __format(ERR_MSG_BUF + bi, var_args[pi], w, zp, 16, pp);
                break;
            default:
                abort();
                break;
            }
            pi++;
            si++;
        }
    }
    ERR_MSG_BUF[bi] = '\0';

    if (!location) {
        printf("[ICE] Error: %s\n", ERR_MSG_BUF);
        exit(1);
    }

    printf("[%s:%d:%d] Error: %s\n", FILE_NAMES[location->file_idx],
           location->line, location->col, ERR_MSG_BUF);

    char line[MAX_LINE_LEN], *source = FILE_SOURCES[location->file_idx];
    int line_num = 1, line_start = 0, pos = 0, len;

    while (source[pos] && line_num < location->line) {
        if (source[pos] == '\n') {
            line_num++;
            line_start = pos + 1;
        }
        pos++;
    }
    while (source[pos] && source[pos] != '\n')
        pos++;
    len = pos - line_start;
    strncpy(line, source + line_start, len);
    line[len] = '\0';
    printf("%s\n", line);
    for (len = 0; len < location->col - 1; len++)
        line[len] = ' ';
    line[len] = '^';
    line[len + 1] = '\0';
    printf("%s\n", line);

    exit(1);
}

#endif
