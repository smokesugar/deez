#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "json.h"

struct Scanner {
    char* ptr;
    int line;
};

enum TokenType {
    TOKEN_ERROR,
    TOKEN_EOF,
    TOKEN_STRING,
    TOKEN_NUMBER,
    TOKEN_COLON,
    TOKEN_COMMA,
    TOKEN_LSQUARE,
    TOKEN_RSQUARE,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_NULL,
    TOKEN_TRUE,
    TOKEN_FALSE
};

struct Token {
    TokenType type;
    char* ptr;
    size_t len;
    int line;
};

static char scanner_advance(Scanner* s) {
    char c = *s->ptr;

    if (c != '\0') {
        ++s->ptr;
    }

    if (c == '\n') {
        ++s->line;
    }

    return c;
}

static Token scan(Scanner* s) {
    while (isspace(*s->ptr)) {
        scanner_advance(s);
    }

    char* start = s->ptr;
    int line = s->line;

    char c = scanner_advance(s);
    TokenType type = TOKEN_ERROR;

    switch (c) {
        case '\0': 
            type = TOKEN_EOF;
            break;

        case '"': {
            do {
                c = scanner_advance(s);
            } while (c != '"' && c != '\0');

            if (c != '\0') {
                type = TOKEN_STRING;
            }
        } break;

        case ':':
            type = TOKEN_COLON;
            break;

        case ',':
            type = TOKEN_COMMA;
            break;

        case '[':
            type = TOKEN_LSQUARE;
            break;
        case ']':
            type = TOKEN_RSQUARE;
            break;

        case '{':
            type = TOKEN_LBRACE;
            break;
        case '}':
            type = TOKEN_RBRACE;
            break;

        case 'n':
            if (strncmp("null", start, strlen("null")) == 0) {
                type = TOKEN_NULL;
                s->ptr = start + strlen("null");
            }
            break;

        case 't':
            if (strncmp("true", start, strlen("true")) == 0) {
                type = TOKEN_TRUE;
                s->ptr = start + strlen("true");
            }
            break;

        case 'f':
            if (strncmp("false", start, strlen("false")) == 0) {
                type = TOKEN_FALSE;
                s->ptr = start + strlen("false");
            }
            break;

        default: {
            if (isdigit(c) || c == '-') {
                (void)strtof(start, &s->ptr);
                type = TOKEN_NUMBER;
            };
        } break;
    }
    
    Token tok;
    tok.type = type;
    tok.len = s->ptr - start;
    tok.ptr = start;
    tok.line = line;

    return tok;
};

static Token scan_peek(Scanner* s) {
    Scanner temp = *s;
    return scan(&temp);
}

static Token match_token(Scanner* s, TokenType type) {
    UNUSED(type);
    Token tok = scan(s);
    assert(tok.type == type && "bad json");
    return tok;
}

static Json* make_json(JsonType type) {
    Json* j = (Json*)calloc(1, sizeof(Json));
    j->type = type;
    return j;
}

static char* extract_string(Token tok) {
    assert(tok.type == TOKEN_STRING);
    char* str = (char*)malloc(tok.len - 1);
    memcpy(str, tok.ptr + 1, tok.len - 2);
    str[tok.len - 2] = '\0';
    return str;
}

static Json* parse_unknown(Scanner* s) {
    Token tok = scan(s);
    switch (tok.type) {
        case TOKEN_NULL: return make_json(JSON_NULL);
        case TOKEN_NUMBER: {
            Json* j = make_json(JSON_NUMBER);
            j->number = strtof(tok.ptr, NULL);
            return j;
        };
        case TOKEN_STRING: {
            Json* j = make_json(JSON_STRING);
            j->string = extract_string(tok);
            return j;
        };
        case TOKEN_TRUE: {
        case TOKEN_FALSE:
            Json* j = make_json(JSON_BOOLEAN);
            j->boolean = tok.type == TOKEN_TRUE;
            return j;
        };

        case TOKEN_LSQUARE: {
            Json head = {};
            Json* cur = &head;

            while (scan_peek(s).type != TOKEN_RSQUARE) {
                if (head.next) {
                    match_token(s, TOKEN_COMMA);
                }

                cur = cur->next = parse_unknown(s);
            }

            match_token(s, TOKEN_RSQUARE);

            Json* arr = make_json(JSON_ARRAY);
            arr->arr_first = head.next;
            
            return arr;
        };

        case TOKEN_LBRACE: {
            JsonPair head = {};
            JsonPair* cur = &head;

            while (scan_peek(s).type != TOKEN_RBRACE) {
                if (head.next) {
                    match_token(s, TOKEN_COMMA);
                }

                Token name_tok = match_token(s, TOKEN_STRING);
                match_token(s, TOKEN_COLON);

                JsonPair* pair = (JsonPair*)calloc(1, sizeof(JsonPair));
                pair->name = extract_string(name_tok);
                pair->json = parse_unknown(s);

                cur = cur->next = pair;
            }

            match_token(s, TOKEN_RBRACE);

            Json* obj = make_json(JSON_OBJECT);
            obj->obj_first = head.next;

            return obj;
        };
    }
    assert(false && "bad json");
    return NULL;
}

Json* json_parse(char* str) {
    Scanner s;
    s.ptr = str;
    s.line = 1;

    Json* j= parse_unknown(&s);
    match_token(&s, TOKEN_EOF);

    return j;
}

void json_free(Json* j) {
    switch (j->type) {
        case JSON_STRING:
            free(j->string);
            break;
        case JSON_ARRAY:
            for (Json* e = j->arr_first; e;) {
                Json* next = e->next;
                json_free(e);
                e = next;
            }
            break;
        case JSON_OBJECT: {
            for (JsonPair* p = j->obj_first; p;) {
                JsonPair* next = p->next;
                free(p->name);
                json_free(p->json);
                free(p);
                p = next;
            }
            break;
        };
    }

    free(j);
}

static Json* search_entry(Json* j, char* name) {
    assert(j->type == JSON_OBJECT);

    for (JsonPair* pair = j->obj_first; pair; pair = pair->next) {
        if (strcmp(pair->name, name) == 0) {
            return pair->json;
        }
    }

    return NULL;
}

Json* json_lookup(Json* j, char* name) {
    Json* e = search_entry(j, name);
    assert(e);
    return e;
}

bool json_has(Json* j, char* name) {
    return search_entry(j, name) != NULL;
}

float json_number(Json* j) {
    assert(j->type == JSON_NUMBER);
    return j->number;
}

char* json_string(Json* j) {
    assert(j->type == JSON_STRING);
    return j->string;
}

bool json_boolean(Json* j) {
    assert(j->type == JSON_BOOLEAN);
    return j->boolean;
}

int json_array_len(Json* j) {
    int l = 0;
    for (Json* e = j->arr_first; e; e = e->next) {
        ++l;
    }
    return l;
}
