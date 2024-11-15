#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defs.h"

typedef struct {
    int capacity;
    int size;
    token_t *data;
} token_arena_t;

token_arena_t *arena_init(int capacity)
{
    token_arena_t *arena = malloc(sizeof(token_arena_t));
    arena->capacity = capacity;
    arena->size = 0;
    arena->data = malloc(capacity * sizeof(token_t));
    return arena;
}

token_t *alloc_token(token_arena_t *arena, int size)
{
    /* TODO: Expand arena if remaining capacity is not enough */
    if (arena->size + size > arena->capacity)
        return NULL;

    token_t *data = &arena->data[arena->size], *cur = data;
    for (int i = 1; i < size; i++) {
        cur->next = &arena->data[arena->size + i];
        cur = cur->next;
    }
    cur->next = NULL;
    arena->size += size;
    return data;
}

/* Copies single token data to a new token */
token_t *arena_copy_token(token_arena_t *arena, token_t *token)
{
    if (!token)
        return NULL;

    token_t *new_token = alloc_token(arena, 1);
    cptok(new_token, token);
    return new_token;
}

/* Copies whole token list to a new token list */
token_t *arena_copy_tokens(token_arena_t *arena, token_t *token_list)
{
    if (!token_list)
        return NULL;

    token_t *new_token_head = alloc_token(arena, 1), *cur = new_token_head;
    cptok(new_token_head, token_list);

    token_list = token_list->next;
    while (token_list) {
        cur->next = alloc_token(arena, 1);
        cur = cur->next;
        cptok(cur, token_list);
        token_list = token_list->next;
    }

    return new_token_head;
}

void arena_free(token_arena_t *arena)
{
    if (!arena)
        return;
    if (arena->data)
        free(arena->data);
    free(arena);
}

typedef enum { LM_source, LM_token } lexer_mode_t;

typedef struct {
    token_arena_t *arena;
    int file_idx;
    lexer_mode_t mode;
    macro_arg_t *macro_args;
    /* LM_Source specific members */
    char *source;
    int source_len;
    int pos;
    int line;
    int col;
    token_t *cur_token;
    bool after_newline;
    bool inside_macro;
    /* LM_Token specific members */
    token_t *tokens_head;
} regional_lexer_t;

void reg_lexer_next_token(regional_lexer_t *lexer);

regional_lexer_t *reg_lexer_source_init(token_arena_t *arena,
                                        char *source,
                                        int source_len,
                                        int file_idx)
{
    regional_lexer_t *lexer = malloc(sizeof(regional_lexer_t));
    lexer->arena = arena;
    lexer->file_idx = file_idx;
    lexer->mode = LM_source;
    lexer->macro_args = NULL;
    lexer->source = source;
    lexer->source_len = source_len;
    lexer->pos = 0;
    lexer->line = 1;
    lexer->col = 1;
    lexer->cur_token = NULL;
    lexer->after_newline = true;
    lexer->inside_macro = false;
    lexer->tokens_head = NULL;
    return lexer;
}

regional_lexer_t *reg_lexer_token_init(token_arena_t *arena,
                                       token_t *tokens_head,
                                       macro_arg_t *macro_args,
                                       int file_idx)
{
    regional_lexer_t *lexer = malloc(sizeof(regional_lexer_t));
    lexer->arena = arena;
    lexer->file_idx = file_idx;
    lexer->mode = LM_token;
    lexer->macro_args = macro_args;
    lexer->tokens_head = tokens_head;
    return lexer;
}

/* Utility Functions for identifying characters */
bool is_white_space(char ch)
{
    return ch == ' ' || ch == '\t';
}

bool is_newline(char ch)
{
    return ch == '\n' || ch == '\r';
}

bool is_alpha(char ch)
{
    return ch >= 'a' && ch <= 'z' || ch >= 'A' && ch <= 'Z';
}

bool is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

bool is_alnum(char ch)
{
    return is_alpha(ch) || is_digit(ch);
}

bool is_identifier_start(char ch)
{
    return is_alpha(ch) || ch == '_';
}

bool is_identifier(char ch)
{
    return is_alnum(ch) || ch == '_';
}

/* Copies location data to loc_ref, allocates location_t on heap if loc_ref is
 * NULL */
location_t *reg_lexer_cur_loc(regional_lexer_t *lexer, location_t *loc_ref)
{
    if (!loc_ref)
        loc_ref = malloc(sizeof(location_t));

    switch (lexer->mode) {
    case LM_source: {
        loc_ref->file_idx = lexer->file_idx;
        loc_ref->col = lexer->col;
        loc_ref->line = lexer->line;
        return loc_ref;
    }
    case LM_token: {
        loc_ref->file_idx = lexer->file_idx;
        loc_ref->col = lexer->tokens_head->loc.col;
        loc_ref->line = lexer->tokens_head->loc.line;
        return loc_ref;
    }
    }

    return NULL;
}

char reg_lexer_peek_char(regional_lexer_t *lexer, int offset)
{
    if (lexer->pos + offset >= lexer->source_len)
        return '\0';

    /* FIXME: Currently requires a temporary variable to store string ptr in
     * order to prevent flaw machine code generation. */
    char *src = lexer->source;

    return src[lexer->pos + offset];
}

void reg_lexer_read_char(regional_lexer_t *lexer, int len)
{
    lexer->pos += len;
    lexer->col += len;
}

void reg_lexer_skip_white_space(regional_lexer_t *lexer)
{
    /* Only true if it's at the start of file or behind a newline character,
     * false otherwise. */
    bool after_backslash = false;
    bool after_new_line =
        lexer->pos == 0 ||
        (lexer->cur_token && lexer->cur_token->typ == T_newline);
    if (lexer->cur_token && lexer->cur_token->typ == T_newline) {
        /* pos already incremented by the current token (T_newline) */
        lexer->line += 1;
        lexer->col = 1;
    }
    char ch;

    while (true) {
        ch = reg_lexer_peek_char(lexer, 0);

        if (lexer->inside_macro && is_newline(ch)) {
            if (!after_backslash) {
                /* End of macro, stops skipping and macro parser
                 * should handle the upcoming newline token
                 */
                break;
            } else {
                /* Continues macro parsing. */
                after_backslash = false;
                lexer->line += 1;
                lexer->col = 1;
                lexer->pos += 1;
                continue;
            }
        }

        if (lexer->inside_macro && ch == '\\') {
            lexer->pos += 1;
            lexer->col += 1;
            after_backslash = true;
            continue;
        }

        if (!lexer->inside_macro && is_newline(ch)) {
            lexer->line += 1;
            lexer->col = 1;
            lexer->pos += 1;
            after_new_line = true;
            continue;
        }

        if (is_white_space(ch)) {
            lexer->pos += 1;
            lexer->col += 1;
            continue;
        }

        break;
    }

    if (lexer->inside_macro && after_backslash)
        error("Expected newline after backslash",
              reg_lexer_cur_loc(lexer, NULL));

    lexer->after_newline = after_new_line;
}

void reg_lexer_make_token(regional_lexer_t *lexer, token_type_t typ, int len);
void reg_lexer_make_identifier_token(regional_lexer_t *lexer, int len);

void reg_lexer_next_token(regional_lexer_t *lexer)
{
    if (lexer->mode == LM_token) {
        if (lexer->tokens_head) {
            lexer->cur_token = lexer->tokens_head;
            lexer->tokens_head = lexer->tokens_head->next;
        } else if (lexer->cur_token->typ != T_eof) {
            /* Allocates dummy token with type T_eof */
            token_t *eof_token = alloc_token(lexer->arena, 1);
            memcpy(&eof_token->loc, &lexer->cur_token->loc, sizeof(location_t));
            eof_token->typ = T_eof;
            lexer->cur_token = eof_token;
        }
        return;
    }

    char ch, token_str[MAX_TOKEN_LEN];
    int len;

    reg_lexer_skip_white_space(lexer);
    ch = reg_lexer_peek_char(lexer, 0);

    if (ch == '/') {
        ch = reg_lexer_peek_char(lexer, 1);

        if (ch == '*') {
            /* in a comment, skip until end */
            len = 2;

            do {
                ch = reg_lexer_peek_char(lexer, len);

                if (ch == '*' && reg_lexer_peek_char(lexer, len + 1) == '/') {
                    reg_lexer_read_char(lexer, len + 2);
                    reg_lexer_next_token(lexer);
                    return;
                }

                len++;
            } while (ch);

            if (!ch)
                error("Unenclosed comment block",
                      reg_lexer_cur_loc(lexer, NULL));
        } else if (ch == '/') {
            /* C99 style comment */
            len = 2;

            do {
                ch = reg_lexer_peek_char(lexer, len);
                len++;
            } while (!is_newline(ch) && ch);

            reg_lexer_read_char(lexer, len);
            reg_lexer_next_token(lexer);
            return;
        } else if (ch == ' ') {
            /* single '/', predict divide */
            reg_lexer_make_token(lexer, T_divide, 1);
            return;
        }

        /* TODO: check invalid cases */
        error("TODO: Unimplemented '/' compound operator",
              reg_lexer_cur_loc(lexer, NULL));
    }

    if (ch == '#') {
        if (reg_lexer_peek_char(lexer, 1) == '#') {
            reg_lexer_make_token(lexer, T_cppd_hashhash, 2);
            return;
        }

        reg_lexer_make_token(lexer, T_cppd_hash, 1);
        return;
    }

    if (ch == '(') {
        reg_lexer_make_token(lexer, T_open_bracket, 1);
        return;
    }

    if (ch == ')') {
        reg_lexer_make_token(lexer, T_close_bracket, 1);
        return;
    }

    if (ch == '{') {
        reg_lexer_make_token(lexer, T_open_curly, 1);
        return;
    }

    if (ch == '}') {
        reg_lexer_make_token(lexer, T_close_curly, 1);
        return;
    }

    if (ch == '[') {
        reg_lexer_make_token(lexer, T_open_square, 1);
        return;
    }

    if (ch == ']') {
        reg_lexer_make_token(lexer, T_close_square, 1);
        return;
    }

    if (ch == ',') {
        reg_lexer_make_token(lexer, T_comma, 1);
        return;
    }

    if (ch == '^') {
        reg_lexer_make_token(lexer, T_bit_xor, 1);
        return;
    }

    if (ch == '~') {
        reg_lexer_make_token(lexer, T_bit_not, 1);
        return;
    }

    if (ch == '*') {
        reg_lexer_make_token(lexer, T_asterisk, 1);
        return;
    }

    if (ch == '&') {
        ch = reg_lexer_peek_char(lexer, 1);

        if (ch == '&') {
            reg_lexer_make_token(lexer, T_log_and, 2);
            return;
        }

        if (ch == '=') {
            reg_lexer_make_token(lexer, T_andeq, 2);
            return;
        }

        reg_lexer_make_token(lexer, T_ampersand, 1);
        return;
    }

    if (ch == '|') {
        ch = reg_lexer_peek_char(lexer, 1);

        if (ch == '|') {
            reg_lexer_make_token(lexer, T_log_or, 2);
            return;
        }

        if (ch == '=') {
            reg_lexer_make_token(lexer, T_oreq, 2);
            return;
        }

        reg_lexer_make_token(lexer, T_bit_or, 1);
        return;
    }

    if (ch == '<') {
        ch = reg_lexer_peek_char(lexer, 1);

        if (ch == '<') {
            reg_lexer_make_token(lexer, T_lshift, 2);
            return;
        }

        if (ch == '=') {
            reg_lexer_make_token(lexer, T_le, 2);
            return;
        }

        reg_lexer_make_token(lexer, T_lt, 1);
        return;
    }

    if (ch == '>') {
        ch = reg_lexer_peek_char(lexer, 1);

        if (ch == '>') {
            reg_lexer_make_token(lexer, T_rshift, 2);
            return;
        }

        if (ch == '=') {
            reg_lexer_make_token(lexer, T_ge, 2);
            return;
        }

        reg_lexer_make_token(lexer, T_gt, 1);
        return;
    }

    if (ch == '%') {
        reg_lexer_make_token(lexer, T_mod, 1);
        return;
    }

    if (ch == '!') {
        if (reg_lexer_peek_char(lexer, 1) == '=') {
            reg_lexer_make_token(lexer, T_noteq, 2);
            return;
        }

        reg_lexer_make_token(lexer, T_log_not, 1);
        return;
    }

    if (ch == '.') {
        if (reg_lexer_peek_char(lexer, 1) == '.' &&
            reg_lexer_peek_char(lexer, 2) == '.') {
            reg_lexer_make_token(lexer, T_elipsis, 3);
            return;
        }

        reg_lexer_make_token(lexer, T_dot, 1);
        return;
    }

    if (ch == '-') {
        ch = reg_lexer_peek_char(lexer, 1);

        if (ch == '>') {
            reg_lexer_make_token(lexer, T_arrow, 2);
            return;
        }

        if (ch == '-') {
            reg_lexer_make_token(lexer, T_decrement, 2);
            return;
        }

        if (ch == '=') {
            reg_lexer_make_token(lexer, T_minuseq, 2);
            return;
        }

        reg_lexer_make_token(lexer, T_minus, 1);
        return;
    }

    if (ch == '+') {
        ch = reg_lexer_peek_char(lexer, 1);

        if (ch == '+') {
            reg_lexer_make_token(lexer, T_increment, 2);
            return;
        }

        if (ch == '=') {
            reg_lexer_make_token(lexer, T_pluseq, 2);
            return;
        }

        reg_lexer_make_token(lexer, T_plus, 1);
        return;
    }

    if (ch == ';') {
        reg_lexer_make_token(lexer, T_semicolon, 1);
        return;
    }

    if (ch == '=') {
        if (reg_lexer_peek_char(lexer, 1) == '=') {
            reg_lexer_make_token(lexer, T_eq, 1);
            return;
        }

        reg_lexer_make_token(lexer, T_assign, 1);
        return;
    }

    if (ch == '\0') {
        reg_lexer_make_token(lexer, T_eof, 0);
        return;
    }

    if (ch == '"') {
        int output_len = 0;
        bool special = false;

        do {
            len++;
            ch = reg_lexer_peek_char(lexer, len);

            if (special) {
                if (ch == 'n')
                    token_str[output_len++] = '\n';
                else if (ch == '"')
                    token_str[output_len++] = '"';
                else if (ch == 'r')
                    token_str[output_len++] = '\r';
                else if (ch == '\'')
                    token_str[output_len++] = '\'';
                else if (ch == 't')
                    token_str[output_len++] = '\t';
                else if (ch == '\\')
                    token_str[output_len++] = '\\';
                else if (ch == '0')
                    token_str[output_len++] = '\0';
                else {
                    reg_lexer_read_char(lexer, len);
                    error("Unexpected escaped character: %c",
                          reg_lexer_cur_loc(lexer, NULL), ch);
                }

                continue;
            } else if (ch != '\\') {
                token_str[output_len++] = ch;
            }

            special = ch == '\\';
        } while (ch && ch != '"');

        if (special) {
            reg_lexer_read_char(lexer, len - 1);
            error("Incomplete escaped character",
                  reg_lexer_cur_loc(lexer, NULL));
        }

        if (!ch)
            error("Unenclosed string literal", reg_lexer_cur_loc(lexer, NULL));

        token_t *token = alloc_token(lexer->arena, 1);
        token->loc.line = lexer->line;
        token->loc.col = lexer->col;
        token->typ = T_string;
        strncpy(token->literal, token_str, output_len);
        token->literal[output_len] = '\0';
        lexer->cur_token = token;
        reg_lexer_read_char(lexer, len + 1);
        return;
    }

    if (ch == '\'') {
        len = 1;
        ch = reg_lexer_peek_char(lexer, len);

        if (ch == '\\') {
            len++;
            ch = reg_lexer_peek_char(lexer, len);

            if (ch == 'n')
                token_str[0] = '\n';
            else if (ch == '"')
                token_str[0] = '"';
            else if (ch == 'r')
                token_str[0] = '\r';
            else if (ch == '\'')
                token_str[0] = '\'';
            else if (ch == 't')
                token_str[0] = '\t';
            else if (ch == '\\')
                token_str[0] = '\\';
            else if (ch == '0')
                token_str[0] = '\0';
            else {
                reg_lexer_read_char(lexer, len);
                error("Unexpected escaped character: %c",
                      reg_lexer_cur_loc(lexer, NULL), ch);
            }
        } else
            token_str[0] = ch;

        len++;
        ch = reg_lexer_peek_char(lexer, len);

        if (ch != '\'')
            error("Unenclosed character literal",
                  reg_lexer_cur_loc(lexer, NULL));

        token_t *token = alloc_token(lexer->arena, 1);
        token->loc.line = lexer->line;
        token->loc.col = lexer->col;
        token->typ = T_char;
        strncpy(token->literal, token_str, 1);
        token->literal[1] = '\0';
        lexer->cur_token = token;
        reg_lexer_read_char(lexer, len + 1);
        return;
    }

    if (is_newline(ch)) {
        reg_lexer_make_token(lexer, T_newline, 1);
        return;
    }

    if (is_identifier_start(ch)) {
        len = 0;

        do {
            len++;
            ch = reg_lexer_peek_char(lexer, len);
        } while (is_identifier(ch));

        reg_lexer_make_identifier_token(lexer, len);
        return;
    }

    if (is_digit(ch)) {
        len = 0;

        /* TODO: Handle hexadecimal numeric */
        do {
            len++;
            ch = reg_lexer_peek_char(lexer, len);
        } while (is_digit(ch));

        reg_lexer_make_token(lexer, T_numeric, len);
        return;
    }

    if (is_newline(ch)) {
        reg_lexer_make_token(lexer, T_newline, 1);
        return;
    }

    error("Unexpected character: %c", reg_lexer_cur_loc(lexer, NULL), ch);
}

bool reg_lexer_accept_token(regional_lexer_t *lexer, token_type_t typ)
{
    if (!lexer->cur_token)
        error("Lexer is not initialized", NULL);

    if (lexer->cur_token->typ == typ) {
        reg_lexer_next_token(lexer);
        return true;
    }

    return false;
}

bool reg_lexer_peek_token(regional_lexer_t *lexer, token_type_t typ, char *buf)
{
    if (!lexer->cur_token)
        error("Lexer is not initialized", NULL);

    if (lexer->cur_token->typ == typ) {
        if (!buf)
            return true;

        strcpy(buf, lexer->cur_token->literal);
        return true;
    }

    return false;
}

void reg_lexer_ident_token(regional_lexer_t *lexer, token_type_t typ, char *buf)
{
    if (!lexer->cur_token)
        error("Lexer is not initialized", NULL);

    if (lexer->cur_token->typ == typ) {
        if (!buf)
            return;

        strcpy(buf, lexer->cur_token->literal);
        reg_lexer_next_token(lexer);
        return;
    }

    error("Unexpected token `%s`", &lexer->cur_token->loc,
          lexer->cur_token->literal);
}

void reg_lexer_expect_token(regional_lexer_t *lexer, token_type_t typ)
{
    if (!lexer->cur_token)
        error("Lexer is not initialized", NULL);

    if (lexer->cur_token->typ == typ) {
        reg_lexer_next_token(lexer);
        return;
    }

    error("Unexpected token `%s`, expected `tk_typ: %d`",
          &lexer->cur_token->loc, lexer->cur_token->literal, typ);
}

void reg_lexer_make_token(regional_lexer_t *lexer, token_type_t typ, int len)
{
    token_t *token = alloc_token(lexer->arena, 1);
    token->loc.line = lexer->line;
    token->loc.col = lexer->col;
    token->typ = typ;
    strncpy(token->literal, lexer->source + lexer->pos, len);
    token->literal[len] = '\0';
    lexer->cur_token = token;
    reg_lexer_read_char(lexer, len);
}

void reg_lexer_make_identifier_token(regional_lexer_t *lexer, int len)
{
    char a[100];
    token_t *token = alloc_token(lexer->arena, 1);
    token->loc.line = lexer->line;
    token->loc.col = lexer->col;
    strncpy(token->literal, lexer->source + lexer->pos, len);
    token->literal[len] = '\0';

    if (!strcmp(token->literal, "if"))
        token->typ = T_if;
    else if (!strcmp(token->literal, "while"))
        token->typ = T_while;
    else if (!strcmp(token->literal, "for"))
        token->typ = T_for;
    else if (!strcmp(token->literal, "do"))
        token->typ = T_do;
    else if (!strcmp(token->literal, "else"))
        token->typ = T_else;
    else if (!strcmp(token->literal, "return"))
        token->typ = T_return;
    else if (!strcmp(token->literal, "typedef"))
        token->typ = T_typedef;
    else if (!strcmp(token->literal, "enum"))
        token->typ = T_enum;
    else if (!strcmp(token->literal, "struct"))
        token->typ = T_struct;
    else if (!strcmp(token->literal, "sizeof"))
        token->typ = T_sizeof;
    else if (!strcmp(token->literal, "switch"))
        token->typ = T_switch;
    else if (!strcmp(token->literal, "case"))
        token->typ = T_case;
    else if (!strcmp(token->literal, "break"))
        token->typ = T_break;
    else if (!strcmp(token->literal, "default"))
        token->typ = T_default;
    else if (!strcmp(token->literal, "continue"))
        token->typ = T_continue;
    else
        token->typ = T_identifier;

    lexer->cur_token = token;
    reg_lexer_read_char(lexer, len);
}

void reg_lexer_free(regional_lexer_t *lexer)
{
    free(lexer);
}

typedef struct {
    int len;
    regional_lexer_t *lexers[MAX_REGIONAL_LEXERS_SIZE];
} regional_lexer_stack_t;

regional_lexer_stack_t *lexer_stack_init()
{
    regional_lexer_stack_t *stack = malloc(sizeof(regional_lexer_stack_t));
    stack->len = 0;
    return stack;
}

void lexer_stack_push(regional_lexer_stack_t *stack, regional_lexer_t *lexer)
{
    if (stack->len + 1 >= MAX_REGIONAL_LEXERS_SIZE)
        return;

    stack->lexers[stack->len++] = lexer;
}

void lexer_stack_pop(regional_lexer_stack_t *stack)
{
    if (stack->len == 0)
        return;

    stack->len--;
    regional_lexer_t *lexer = stack->lexers[stack->len];
    reg_lexer_free(lexer);
}

regional_lexer_t *lexer_stack_top(regional_lexer_stack_t *stack)
{
    if (stack->len == 0)
        return NULL;

    return stack->lexers[stack->len - 1];
}

typedef struct {
    token_arena_t *arena;
    regional_lexer_t *global_lexer;
    regional_lexer_stack_t *regional_lexers;
} lexer_t;

lexer_t *lexer_init(char *entry_file_path)
{
    char *source;
    int length;
    int file_idx = file_map_add_entry(entry_file_path, &source, &length);

    lexer_t *lexer = malloc(sizeof(lexer_t));
    lexer->arena = arena_init(1024);
    lexer->global_lexer =
        reg_lexer_source_init(lexer->arena, source, length, file_idx);
    lexer->regional_lexers = lexer_stack_init();
    return lexer;
}

regional_lexer_t *lexer_top_reg_lexer(lexer_t *lexer)
{
    regional_lexer_t *reg_lexer = lexer_stack_top(lexer->regional_lexers);

    return reg_lexer ? reg_lexer : lexer->global_lexer;
}

token_t *lexer_cur_token(lexer_t *lexer)
{
    regional_lexer_t *reg_lexer = lexer_top_reg_lexer(lexer);

    return reg_lexer->cur_token;
}

token_type_t lexer_cur_token_type(lexer_t *lexer)
{
    regional_lexer_t *reg_lexer = lexer_top_reg_lexer(lexer);

    return reg_lexer->cur_token ? reg_lexer->cur_token->typ : T_eof;
}

char *lexer_cur_token_literal(lexer_t *lexer)
{
    regional_lexer_t *reg_lexer = lexer_top_reg_lexer(lexer);

    return reg_lexer->cur_token ? reg_lexer->cur_token->literal : NULL;
}

token_type_t lexer_next_token(lexer_t *lexer);

bool lexer_accept_token(lexer_t *lexer, token_type_t typ)
{
    if (lexer_cur_token_type(lexer) == typ) {
        lexer_next_token(lexer);
        return true;
    }

    return false;
}

bool lexer_peek_token(lexer_t *lexer, token_type_t typ, char *buf)
{
    regional_lexer_t *reg_lexer = lexer_top_reg_lexer(lexer);

    return reg_lexer_peek_token(reg_lexer, typ, buf);
}

void lexer_expect_token(lexer_t *lexer, token_type_t typ)
{
    regional_lexer_t *reg_lexer = lexer_top_reg_lexer(lexer);

    if (lexer_cur_token_type(lexer) == typ) {
        lexer_next_token(lexer);
        return true;
    }

    return false;
}

token_t *lexer_read_macro_replacement(lexer_t *lexer,
                                      regional_lexer_t *reg_lexer)
{
    token_t *token = reg_lexer->cur_token, *cur = token;

    while (!reg_lexer_accept_token(reg_lexer, T_eof)) {
        if (reg_lexer_peek_token(reg_lexer, T_newline, NULL))
            break;

        cur->next = reg_lexer->cur_token;
        cur = cur->next;
        reg_lexer_next_token(reg_lexer);
    }

    cur->next = NULL;
    cur = token;
    reg_lexer->inside_macro = false;

    return token;
}

void read_macro_params(lexer_t *lexer,
                       regional_lexer_t *reg_lexer,
                       macro_t *macro)
{
    macro_param_t *params_head = malloc(sizeof(macro_param_t)),
                  *cur = params_head;
    bool is_variadic = false;

    if (!reg_lexer_accept_token(reg_lexer, T_close_bracket)) {
        for (;;) {
            if (reg_lexer_peek_token(reg_lexer, T_elipsis, NULL)) {
                is_variadic = true;
                cur->param_token = reg_lexer->cur_token;
                cur->is_variadic = true;
                reg_lexer_expect_token(reg_lexer, T_elipsis);
                reg_lexer_expect_token(reg_lexer, T_close_bracket);
                break;
            }

            cur->param_token = reg_lexer->cur_token;
            cur->is_variadic = false;
            reg_lexer_expect_token(reg_lexer, T_identifier);

            if (!reg_lexer_accept_token(reg_lexer, T_comma)) {
                reg_lexer_expect_token(reg_lexer, T_close_bracket);
                break;
            } else {
                cur->next = malloc(sizeof(macro_param_t));
                cur = cur->next;
            }
        }
    }

    cur->next = NULL;
    macro->params = params_head;
    macro->is_variadic = is_variadic;
}

/* Reads preprocessor directive, this action is location-sensitive. */
void lexer_read_directive(lexer_t *lexer, regional_lexer_t *reg_lexer)
{
    if (!reg_lexer->after_newline)
        error("Stray # in non-macro context or non-line-start position",
              &reg_lexer->cur_token->loc);

    reg_lexer_expect_token(reg_lexer, T_cppd_hash);
    reg_lexer->inside_macro = true;

    if (!strcmp(reg_lexer->cur_token->literal, "define")) {
        macro_t *macro;
        reg_lexer_expect_token(reg_lexer, T_identifier);
        macro = add_macro(false);
        macro->name = reg_lexer->cur_token;
        reg_lexer_expect_token(reg_lexer, T_identifier);

        if (reg_lexer_accept_token(reg_lexer, T_open_bracket)) {
            read_macro_params(lexer, reg_lexer, macro);
            token_t *replacement =
                lexer_read_macro_replacement(lexer, reg_lexer);
            macro->replacement = replacement;
            macro->function_like = true;
        } else {
            token_t *replacement =
                lexer_read_macro_replacement(lexer, reg_lexer);
            macro->replacement = replacement;
            macro->function_like = false;
        }

        return;
    }

    error("Unexpected preprocessor directive `%s`", &reg_lexer->cur_token->loc,
          reg_lexer->cur_token->literal);
}

macro_arg_t *lexer_read_macro_arg(lexer_t *lexer,
                                  regional_lexer_t *reg_lexer,
                                  token_t *name_token_ref,
                                  bool is_variadic)
{
    macro_arg_t *arg = malloc(sizeof(macro_arg_t));
    int bracket_level = 0;
    token_t *replacement = reg_lexer->cur_token, *cur = replacement;

    for (;;) {
        if (bracket_level == 0 &&
            reg_lexer_peek_token(reg_lexer, T_close_bracket, NULL))
            break;
        if (bracket_level == 0 && !is_variadic &&
            reg_lexer_peek_token(reg_lexer, T_comma, NULL))
            break;

        if (reg_lexer_accept_token(reg_lexer, T_eof))
            error("Unterminated macro argument list",
                  reg_lexer_cur_loc(reg_lexer, NULL));

        if (reg_lexer_accept_token(reg_lexer, T_open_bracket))
            bracket_level++;
        else if (reg_lexer_accept_token(reg_lexer, T_close_bracket))
            bracket_level--;

        cur->next = reg_lexer->cur_token;
        cur = cur->next;
        reg_lexer_next_token(reg_lexer);
    }

    cur->next = NULL;
    arg->replacement = replacement;
    arg->name = name_token_ref;
    arg->is_variadic = is_variadic;
    return arg;
}

macro_arg_t *lexer_read_macro_args(lexer_t *lexer,
                                   regional_lexer_t *reg_lexer,
                                   macro_t *macro)
{
    macro_arg_t *macro_arg_head = NULL, *cur = NULL, *arg;

    for (macro_param_t *macro_param = macro->params; macro_param;
         macro_param = macro_param->next) {
        if (macro_arg_head)
            reg_lexer_expect_token(reg_lexer, T_comma);
        if (!macro_arg_head) {
            macro_arg_head =
                lexer_read_macro_arg(lexer, reg_lexer, macro_param->param_token,
                                     macro_param->is_variadic);
            cur = macro_arg_head;
        } else {
            arg =
                lexer_read_macro_arg(lexer, reg_lexer, macro_param->param_token,
                                     macro_param->is_variadic);
            cur->next = arg;
            cur = arg;
        }
    }

    reg_lexer_expect_token(reg_lexer, T_close_bracket);
    return macro_arg_head;
}

bool lexer_expand_macro_direct(lexer_t *lexer, regional_lexer_t *reg_lexer)
{
    macro_t *macro = find_macro(reg_lexer->cur_token->literal);

    if (macro) {
        if (macro->function_like) {
            reg_lexer_expect_token(reg_lexer, T_identifier);
            reg_lexer_expect_token(reg_lexer, T_open_bracket);
            macro_arg_t *args = lexer_read_macro_args(lexer, reg_lexer, macro);
            regional_lexer_t *macro_lexer = reg_lexer_token_init(
                lexer->arena, macro->replacement, args, reg_lexer->file_idx);
            lexer_stack_push(lexer->regional_lexers, macro_lexer);
            return true;
        } else {
            reg_lexer_expect_token(reg_lexer, T_identifier);
            regional_lexer_t *macro_lexer = reg_lexer_token_init(
                lexer->arena, macro->replacement, NULL, reg_lexer->file_idx);
            lexer_stack_push(lexer->regional_lexers, macro_lexer);
            return true;
        }
    }

    return false;
}

token_t *lexer_expand_macro_indirect(lexer_t *lexer,
                                     regional_lexer_t *reg_lexer,
                                     token_t *tok)
{
    macro_t *macro = find_macro(tok->literal);

    if (macro) {
        if (macro->function_like) {
            /* TODO: Implement here */
        } else {
            token_t *replacement =
                arena_copy_tokens(lexer->arena, macro->replacement);
            return replacement;
        }
    }

    return NULL;
}

macro_arg_t *lexer_find_macro_arg(lexer_t *lexer, regional_lexer_t *reg_lexer)
{
    if (!reg_lexer_peek_token(reg_lexer, T_identifier, NULL))
        return NULL;

    macro_arg_t *cur = reg_lexer->macro_args;

    while (cur) {
        if (!strcmp(cur->name->literal, reg_lexer->cur_token->literal))
            return cur;

        cur = cur->next;
    }

    return NULL;
}

token_t *lexer_expand_arg(lexer_t *lexer,
                          regional_lexer_t *reg_lexer,
                          macro_arg_t *arg)
{
    if (arg->expanded)
        return arg->expanded;

    if (!arg->replacement)
        return NULL;
    token_t *cur_replacement = arg->replacement;
    token_t *expanded = NULL, *cur = NULL;

    while (cur_replacement) {
        token_t *expanded_macro =
            lexer_expand_macro_indirect(lexer, reg_lexer, cur_replacement);

        if (expanded_macro) {
            /* gets expanded macro's replacement end then concatentate to
             * cur_replacement's front
             */
            token_t *cur_expanded_macro = expanded_macro;

            while (cur_expanded_macro->next)
                cur_expanded_macro = cur_replacement->next;

            cur_expanded_macro->next = cur_replacement;
            cur_replacement = expanded_macro;
            continue;
        }

        if (expanded) {
            cur->next = arena_copy_token(lexer->arena, cur_replacement);
            cur = cur->next;
        } else {
            expanded = arena_copy_token(lexer->arena, cur_replacement);
            cur = expanded;
        }

        cur_replacement = cur_replacement->next;
    }

    arg->expanded = expanded;
    return expanded;
}

bool lexer_substitute_token(lexer_t *lexer, regional_lexer_t *reg_lexer)
{
    macro_arg_t *arg = lexer_find_macro_arg(lexer, reg_lexer);

    if (arg) {
        token_t *replacement = lexer_expand_arg(lexer, reg_lexer, arg);
        reg_lexer_expect_token(reg_lexer, T_identifier);
        regional_lexer_t *arg_lexer = reg_lexer_token_init(
            lexer->arena, replacement, NULL, reg_lexer->file_idx);
        lexer_stack_push(lexer->regional_lexers, arg_lexer);
        return true;
    }

    return false;
}

token_type_t lexer_next_token(lexer_t *lexer)
{
    regional_lexer_t *reg_lexer = lexer_top_reg_lexer(lexer);
    reg_lexer_next_token(reg_lexer);
    token_type_t typ = reg_lexer->cur_token->typ;

    if (reg_lexer->mode == LM_token) {
        switch (typ) {
        case T_eof: {
            if (lexer->regional_lexers->len) {
                lexer_stack_pop(lexer->regional_lexers);
                regional_lexer_t *cur_reg_lexer = lexer_top_reg_lexer(lexer);
                return cur_reg_lexer->cur_token->typ;
            }
            break;
        }
        default: {
            if (lexer_substitute_token(lexer, reg_lexer))
                return lexer_next_token(lexer);
        }
        }
    } else {
        switch (typ) {
        case T_eof: {
            if (lexer->regional_lexers->len) {
                lexer_stack_pop(lexer->regional_lexers);
                regional_lexer_t *cur_reg_lexer = lexer_top_reg_lexer(lexer);
                return cur_reg_lexer->cur_token->typ;
            }
            break;
        }
        case T_cppd_hash: {
            if (reg_lexer->mode == LM_source && !reg_lexer->inside_macro) {
                /* only suppose to be directive */
                lexer_read_directive(lexer, reg_lexer);
                return lexer_next_token(lexer);
            }
        }
        case T_identifier: {
            if (lexer_expand_macro_direct(lexer, reg_lexer))
                return lexer_next_token(lexer);
        }
        default:
            break;
        }
    }

    return typ;
}

void lexer_free(lexer_t *lexer)
{
    arena_free(lexer->arena);
    reg_lexer_free(lexer->global_lexer);
    free(lexer);
}
