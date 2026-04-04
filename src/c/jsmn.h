// jsmn - minimalist JSON tokenizer
// Based on jsmn by Serge Zaitsev (MIT License)
// https://github.com/zserge/jsmn
//
// All functions are static so this header can be included in multiple .c files.
#pragma once
#include <stddef.h>

typedef enum {
    JSMN_UNDEFINED = 0,
    JSMN_OBJECT    = 1,
    JSMN_ARRAY     = 2,
    JSMN_STRING    = 3,
    JSMN_PRIMITIVE = 4,
} jsmntype_t;

typedef enum {
    JSMN_ERROR_NOMEM = -1,  // not enough tokens
    JSMN_ERROR_INVAL = -2,  // invalid character
    JSMN_ERROR_PART  = -3,  // incomplete JSON
} jsmnerr_t;

typedef struct {
    jsmntype_t type;
    int start;
    int end;
    int size;   // for OBJECT: number of key-value pairs; for ARRAY: elements; for STRING key: 1 (value is child)
} jsmntok_t;

typedef struct {
    unsigned int pos;
    unsigned int toknext;
    int toksuper;
} jsmn_parser;

static void jsmn_init(jsmn_parser *parser);
static int  jsmn_parse(jsmn_parser *parser, const char *js, size_t len,
                        jsmntok_t *tokens, unsigned int num_tokens);

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

static jsmntok_t *jsmn__alloc(jsmn_parser *p, jsmntok_t *tokens, size_t num_tokens) {
    if (p->toknext >= num_tokens) return NULL;
    jsmntok_t *tok = &tokens[p->toknext++];
    tok->start = tok->end = -1;
    tok->size = 0;
    return tok;
}

static int jsmn__primitive(jsmn_parser *p, const char *js, size_t len,
                             jsmntok_t *tokens, size_t num_tokens) {
    int start = (int)p->pos;
    for (; p->pos < len && js[p->pos] != '\0'; p->pos++) {
        switch (js[p->pos]) {
        case '\t': case '\r': case '\n': case ' ':
        case ',':  case ']':  case '}':
            goto found;
        default:
            break;
        }
    }
found:
    if (tokens == NULL) { p->pos--; return 0; }
    jsmntok_t *tok = jsmn__alloc(p, tokens, num_tokens);
    if (!tok) { p->pos = (unsigned int)start; return JSMN_ERROR_NOMEM; }
    tok->type  = JSMN_PRIMITIVE;
    tok->start = start;
    tok->end   = (int)p->pos;
    p->pos--;
    return 0;
}

static int jsmn__string(jsmn_parser *p, const char *js, size_t len,
                          jsmntok_t *tokens, size_t num_tokens) {
    int start = (int)p->pos;
    p->pos++;
    for (; p->pos < len && js[p->pos] != '\0'; p->pos++) {
        char c = js[p->pos];
        if (c == '"') {
            if (tokens == NULL) return 0;
            jsmntok_t *tok = jsmn__alloc(p, tokens, num_tokens);
            if (!tok) { p->pos = (unsigned int)start; return JSMN_ERROR_NOMEM; }
            tok->type  = JSMN_STRING;
            tok->start = start + 1;
            tok->end   = (int)p->pos;
            return 0;
        }
        if (c == '\\' && p->pos + 1 < len) {
            p->pos++;
            switch (js[p->pos]) {
            case '"': case '/': case '\\':
            case 'b': case 'f': case 'r': case 'n': case 't':
                break;
            case 'u':
                p->pos++;
                for (int i = 0; i < 4 && p->pos < len; i++, p->pos++) {
                    char h = js[p->pos];
                    if (!((h >= '0' && h <= '9') || (h >= 'A' && h <= 'F') ||
                          (h >= 'a' && h <= 'f'))) {
                        p->pos = (unsigned int)start;
                        return JSMN_ERROR_INVAL;
                    }
                }
                p->pos--;
                break;
            default:
                p->pos = (unsigned int)start;
                return JSMN_ERROR_INVAL;
            }
        }
    }
    p->pos = (unsigned int)start;
    return JSMN_ERROR_PART;
}

static void jsmn_init(jsmn_parser *parser) {
    parser->pos      = 0;
    parser->toknext  = 0;
    parser->toksuper = -1;
}

static int jsmn_parse(jsmn_parser *parser, const char *js, size_t len,
                       jsmntok_t *tokens, unsigned int num_tokens) {
    int count = (int)parser->toknext;

    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
        char c = js[parser->pos];
        int r;

        switch (c) {
        case '{': case '[': {
            count++;
            if (tokens == NULL) break;
            jsmntok_t *tok = jsmn__alloc(parser, tokens, num_tokens);
            if (!tok) return JSMN_ERROR_NOMEM;
            if (parser->toksuper != -1)
                tokens[parser->toksuper].size++;
            tok->type  = (c == '{') ? JSMN_OBJECT : JSMN_ARRAY;
            tok->start = (int)parser->pos;
            parser->toksuper = (int)parser->toknext - 1;
            break;
        }
        case '}': case ']': {
            if (tokens == NULL) break;
            jsmntype_t type = (c == '}') ? JSMN_OBJECT : JSMN_ARRAY;
            int i;
            for (i = (int)parser->toknext - 1; i >= 0; i--) {
                jsmntok_t *tok = &tokens[i];
                if (tok->start != -1 && tok->end == -1) {
                    if (tok->type != type) return JSMN_ERROR_INVAL;
                    tok->end = (int)parser->pos + 1;
                    parser->toksuper = -1;
                    break;
                }
            }
            if (i == -1) return JSMN_ERROR_INVAL;
            for (; i >= 0; i--) {
                jsmntok_t *tok = &tokens[i];
                if (tok->start != -1 && tok->end == -1) {
                    parser->toksuper = i;
                    break;
                }
            }
            break;
        }
        case '"':
            r = jsmn__string(parser, js, len, tokens, num_tokens);
            if (r < 0) return r;
            count++;
            if (parser->toksuper != -1 && tokens != NULL)
                tokens[parser->toksuper].size++;
            break;

        case '\t': case '\r': case '\n': case ' ':
            break;

        case ':':
            parser->toksuper = (int)parser->toknext - 1;
            break;

        case ',':
            if (tokens != NULL && parser->toksuper != -1 &&
                tokens[parser->toksuper].type != JSMN_ARRAY &&
                tokens[parser->toksuper].type != JSMN_OBJECT) {
                for (int i = (int)parser->toknext - 1; i >= 0; i--) {
                    jsmntok_t *t = &tokens[i];
                    if ((t->type == JSMN_ARRAY || t->type == JSMN_OBJECT) &&
                        t->start != -1 && t->end == -1) {
                        parser->toksuper = i;
                        break;
                    }
                }
            }
            break;

        default:
            r = jsmn__primitive(parser, js, len, tokens, num_tokens);
            if (r < 0) return r;
            count++;
            if (parser->toksuper != -1 && tokens != NULL)
                tokens[parser->toksuper].size++;
            break;
        }
    }

    if (tokens != NULL) {
        for (int i = (int)parser->toknext - 1; i >= 0; i--) {
            if (tokens[i].start != -1 && tokens[i].end == -1)
                return JSMN_ERROR_PART;
        }
    }
    return count;
}
