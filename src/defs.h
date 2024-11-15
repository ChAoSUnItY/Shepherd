#ifndef DEFS_H
#define DEFS_H

#include <stdbool.h>

#define MAX_REGIONAL_LEXERS_SIZE 32
#define MAX_LINE_LEN 256
#define MAX_SOURCE 200000
#define MAX_TOKEN_LEN 256
#define MAX_FILE 32
#define MAX_PATH_LEN 32
#define MAX_MACROS 1024

typedef enum {
    T_numeric,
    T_identifier,
    T_comma,  /* , */
    T_string, /* null-terminated string */
    T_char,
    T_open_bracket,  /* ( */
    T_close_bracket, /* ) */
    T_open_curly,    /* { */
    T_close_curly,   /* } */
    T_open_square,   /* [ */
    T_close_square,  /* ] */
    T_asterisk,      /* '*' */
    T_divide,        /* / */
    T_mod,           /* % */
    T_bit_or,        /* | */
    T_bit_xor,       /* ^ */
    T_bit_not,       /* ~ */
    T_log_and,       /* && */
    T_log_or,        /* || */
    T_log_not,       /* ! */
    T_lt,            /* < */
    T_gt,            /* > */
    T_le,            /* <= */
    T_ge,            /* >= */
    T_lshift,        /* << */
    T_rshift,        /* >> */
    T_dot,           /* . */
    T_arrow,         /* -> */
    T_plus,          /* + */
    T_minus,         /* - */
    T_minuseq,       /* -= */
    T_pluseq,        /* += */
    T_oreq,          /* |= */
    T_andeq,         /* &= */
    T_eq,            /* == */
    T_noteq,         /* != */
    T_assign,        /* = */
    T_increment,     /* ++ */
    T_decrement,     /* -- */
    T_question,      /* ? */
    T_colon,         /* : */
    T_semicolon,     /* ; */
    T_eof,           /* end-of-file (EOF) */
    T_ampersand,     /* & */
    T_return,
    T_if,
    T_else,
    T_while,
    T_for,
    T_do,
    T_typedef,
    T_enum,
    T_struct,
    T_sizeof,
    T_elipsis, /* ... */
    T_switch,
    T_case,
    T_break,
    T_default,
    T_continue,
    /* C pre-processor directives */
    T_cppd_hash,
    T_cppd_hashhash,
    T_cppd_include,
    T_cppd_define,
    T_cppd_undef,
    T_cppd_error,
    T_cppd_if,
    T_cppd_elif,
    T_cppd_else,
    T_cppd_endif,
    T_cppd_ifdef,
    T_cppd_ifndef,
    /* Hint tokens */
    T_newline
} token_type_t;

typedef struct {
    int file_idx;
    int line;
    int col;
} location_t;

typedef struct token_t token_t;

struct token_t {
    int dummy;
    location_t loc;
    token_type_t typ;
    /* TODO: Considering using span to minimize memory usage */
    char literal[MAX_TOKEN_LEN];
    token_t *next;
};

typedef struct macro_param_t macro_param_t;

struct macro_param_t {
    token_t *param_token;
    bool is_variadic;
    macro_param_t *next;
};

typedef struct {
    token_t *name;
    token_t *replacement;
    macro_param_t *params;
    bool function_like;
    bool is_variadic;
    bool disabled;
} macro_t;

typedef struct macro_arg_t macro_arg_t;

struct macro_arg_t {
    token_t *name;
    bool is_variadic;
    token_t *replacement;
    token_t *expanded;
    macro_arg_t *next;
};

#endif
