/*
	cparse

	This file is public domain. No warranty implied, use at your own risk.

	AUTHORS
		2016 - Canio Massimo Tristano
*/

#ifndef CPARSE_H_
#define CPARSE_H_

#define CPARSE_API

#ifdef _WIN64
typedef unsigned long long cparse_size_t;
#else
typedef unsigned long cparse_size_t;
#endif

enum cparse_result {
	CPARSE_RESULT_OK,
	CPARSE_RESULT_OUT_OF_MEMORY,
	CPARSE_RESULT_INVALID_INPUT_FILE,
	CPARSE_RESULT_SYNTAX_ERROR
};

enum cparse_type_kind {
	CPARSE_TYPE_PRIMITIVE
};

enum cparse_primitive_type_kind {
	CPARSE_PRIMITIVE_TYPE_SIGNED_CHAR,
	CPARSE_PRIMITIVE_TYPE_UNSIGNED_CHAR,
	CPARSE_PRIMITIVE_TYPE_SIGNED_SHORT,
	CPARSE_PRIMITIVE_TYPE_UNSIGNED_SHORT,
	CPARSE_PRIMITIVE_TYPE_SIGNED_INT,
	CPARSE_PRIMITIVE_TYPE_UNSIGNED_INT,
	CPARSE_PRIMITIVE_TYPE_SIGNED_LONG,
	CPARSE_PRIMITIVE_TYPE_UNSIGNED_LONG,
	CPARSE_PRIMITIVE_TYPE_SIGNED_LONG_LONG,
	CPARSE_PRIMITIVE_TYPE_UNSIGNED_LONG_LONG,
	CPARSE_PRIMITIVE_TYPE_FLOAT,
	CPARSE_PRIMITIVE_TYPE_DOUBLE,
	CPARSE_PRIMITIVE_TYPE_LONG_DOUBLE,
	CPARSE_PRIMITIVE_TYPE_COUNT_,
};

struct cparse_type {
	enum cparse_type_kind kind;
	const char* spelling;
};

struct cparse_primitive_type {
	struct cparse_type type;
	enum cparse_primitive_type_kind primitive_kind;
};

enum cparse_node_type {
	CPARSE_NODE_TYPE_REF,
	CPARSE_NODE_DECL_STRUCT,
};

struct cparse_node {
	enum cparse_node_type type;
};

struct cparse_node_type_ref {
	struct cparse_node node;
	struct cparse_node* type;
};

struct cparse_node_decl {
	struct cparse_node node;
	struct cparse_node_decl* next;
	const char* spelling;
};

struct cparse_node_decl_struct {
	struct cparse_node_decl decl;
};

struct cparse_unit {
	struct cparse_node_decl* decls;
};

struct cparse_info {
	char* buffer;
	cparse_size_t buffer_size;
	const char** include_dirs; /* null or null terminated */
	const char** defines; /* null or null terminated */
};

CPARSE_API enum cparse_result cparse_file(const char* filename, struct cparse_info const*, struct cparse_unit** out);

#endif CPARSE_H_


/**************************************************************************************************/
/*                                         IMPLEMENTATION                                         */
/**************************************************************************************************/
#ifdef CPARSE_IMPLEMENTATION
#undef CPARSE_IMPLEMENTATION

#ifndef uint
#define uint unsigned int
#endif

#define _CRT_SECURE_NO_WARNINGS 0

#include <setjmp.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>

static int cparse_min(int a, int b) { return a < b ? a : b;}

#define CPARSE_TOKENS(_)\
	_(TOK_FLOAT, "floating point literal")\
	_(TOK_INTEGER, "integer literal")\
	_(TOK_IDENTIFIER, "identifier")\
	_(KW_CHAR, "char")\
	_(KW_DO, "do")\
	_(KW_DOUBLE, "double")\
	_(KW_ELSE, "else")\
	_(KW_ENUM, "enum")\
	_(KW_FLOAT, "float")\
	_(KW_FOR, "for")\
	_(KW_IF, "if")\
	_(KW_INT, "int")\
	_(KW_LONG, "long")\
	_(KW_SHORT, "short")\
	_(KW_SIGNED, "signed")\
	_(KW_STATIC, "static")\
	_(KW_STRUCT, "struct")\
	_(KW_TYPEDEF, "typedef")\
	_(KW_UNSIGNED, "unsigned")\
	_(KW_VOLATILE, "volatile")\
	_(KW_WHILE, "while")\

#define CPARSE_MAKE_TOKEN_ENUM(id, str) CPARSE_##id,

enum
{
	CPARSE_TOK_EOF = -9999,
	CPARSE_TOKENS(CPARSE_MAKE_TOKEN_ENUM)
};

typedef int cparse_token_t;

#undef CPARSE_MAKE_TOKEN_ENUM
#define CPARSE_MAKE_TOKEN_STR(id, str) case CPARSE_##id: return str;

static const char* cparse_strtok(cparse_token_t tok)
{
	switch (tok)
	{
		case CPARSE_TOK_EOF: return "end-of-file";
		CPARSE_TOKENS(CPARSE_MAKE_TOKEN_STR)
		
		default:
		{
			static union {
				int tok;
				char chars[8];
			} helper;
			helper.tok = tok;
			return helper.chars;
		}
	}
}

#undef CPARSE_MAKE_TOKEN_STR
#undef CPARSE_TOKENS

struct cparse_lexer
{
	FILE* file;
	const char* filename;
	uint line;
	uint column;
	char* token_buffer;
	uint  token_buffer_capacity;
	uint  token_size;
	enum cparse_token lookahead;
	int curr;
};

struct cparse_state
{
	jmp_buf error_handler;
	char* alloc_begin;
	char* alloc_end;
	char* alloc_cursor;
	char const* error;
	struct cparse_lexer lex;

	/* primitive types */
	struct cparse_primitive_type types[CPARSE_PRIMITIVE_TYPE_COUNT_];
};

static void cparse_state_init_types(struct cparse_state* s)
{
	#define CPARSE_SET_PRIMITIVE_TYPE(id, spelling_)\
		s->types[CPARSE_PRIMITIVE_TYPE_##id].type.kind = CPARSE_TYPE_PRIMITIVE;\
		s->types[CPARSE_PRIMITIVE_TYPE_##id].type.spelling = spelling_;\
		s->types[CPARSE_PRIMITIVE_TYPE_##id].primitive_kind = CPARSE_PRIMITIVE_TYPE_##id

	CPARSE_SET_PRIMITIVE_TYPE(SIGNED_CHAR, "signed char");
	CPARSE_SET_PRIMITIVE_TYPE(UNSIGNED_CHAR, "unsigned char");
	CPARSE_SET_PRIMITIVE_TYPE(SIGNED_SHORT, "signed short");
	CPARSE_SET_PRIMITIVE_TYPE(UNSIGNED_SHORT, "unsigned short");
	CPARSE_SET_PRIMITIVE_TYPE(SIGNED_INT, "signed int");
	CPARSE_SET_PRIMITIVE_TYPE(UNSIGNED_INT, "unsigned int");
	CPARSE_SET_PRIMITIVE_TYPE(SIGNED_LONG, "signed long");
	CPARSE_SET_PRIMITIVE_TYPE(UNSIGNED_LONG, "unsigned long");
	CPARSE_SET_PRIMITIVE_TYPE(SIGNED_LONG_LONG, "signed long long");
	CPARSE_SET_PRIMITIVE_TYPE(UNSIGNED_LONG_LONG, "unsigned long long");
	CPARSE_SET_PRIMITIVE_TYPE(FLOAT, "float");
	CPARSE_SET_PRIMITIVE_TYPE(DOUBLE, "double");

	#undef CPARSE_SET_PRIMITIVE_TYPE
}

static size_t cparse_formatv(char* buffer, size_t buffer_size, const char* format, va_list args)
{
	--buffer_size;
	#define increment_cur() { ++length; cur = cur + 1; cur = cur > buffer_size ? buffer_size : cur; }
	size_t cur = 0;
	size_t length = 0;

	while (*format)	{
		if (*format == '%') {
			switch (*++format) {
				case '%':
					buffer[cur] = '%';
					increment_cur();
					break;

				case 'c':
					buffer[cur] = va_arg(args, char);
					increment_cur();
					break;

				case 's': {
					const char* str = va_arg(args, const char*);
					for (const char* ch = str; *ch; ++ch) {
						buffer[cur] = *ch;
						increment_cur();
					}
					break;
				}

				case 'u': {
					int oldoffset = cur;
					uint value = va_arg(args, uint);
					do {
						buffer[cur] = (char)"0123456789"[value % 10];
						increment_cur();
					} while (value /= 10);

					for (int i = 0, n = (cur - oldoffset) / 2; i <n; ++i) {
						char tmp = buffer[cur - i - 1];
						buffer[cur - i - 1] = buffer[oldoffset + i];
						buffer[oldoffset + i] = tmp;
					}
				}

				default:
					break;
			}
		}
		else {
			buffer[cur] = *format;
			increment_cur();
		}
		++format;
	}
	buffer[cur] = 0; 
	return length;
}

static size_t cparse_format(char* buffer, size_t buffer_size, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	size_t result = cparse_formatv(buffer, buffer_size, format, args);
	va_end(args);
	return result;
}

static void cparse_error(struct cparse_state* s, enum cparse_result result, const char* format, ...)
{
	va_list args;
	
	int buffer_size = 1;
	char* buffer = alloca(buffer_size);

retry:
	va_start(args, format);
	int written = cparse_format(buffer, buffer_size, "at %s:%u:%u: error: ", s->lex.filename, s->lex.line, s->lex.column);
	written += cparse_formatv(buffer + written, buffer_size - written, format, args);
	va_end(args);

	if (written >= buffer_size)
	{
		buffer = alloca(written + 1);
		buffer_size = written + 1;
		goto retry;
	}

	memcpy(s->alloc_begin, buffer, cparse_min(s->alloc_end - s->alloc_begin, written + 1));

	longjmp(s->error_handler, result);
}

static void cparse_error_out_of_memory(struct cparse_state* s)
{
	cparse_error(s, CPARSE_RESULT_OUT_OF_MEMORY, "Out of memory.");
}

static void* cparse_alloc(struct cparse_state* s, cparse_size_t size, uint alignment)
{
	const uint alignment_minus_one = alignment - 1;
	char* ptr = (char*)(((uintptr_t)s->alloc_cursor + alignment_minus_one) & ~alignment_minus_one);
	char* end = ptr + size;
	if (end > s->alloc_end) {
		cparse_error_out_of_memory(s);
	}
	s->alloc_cursor = end;
	return ptr;
}

#define cparse_alloc_type(s, type) ((type*)cparse_alloc(s, sizeof(type), __alignof(type)))

/* lexer */
static int cparse_lex_skip(struct cparse_state* s)
{
	struct cparse_lexer* l = &s->lex;
	l->curr = fgetc(l->file);
	++l->column;
	if (l->curr == '\n') {
		l->column = 1;
		++l->line;
	}
	return l->curr;
}

static int cparse_lex_push(struct cparse_state* s)
{
	struct cparse_lexer* l = &s->lex;
	if (l->token_buffer + l->token_size + 1 == s->alloc_end) {
		char* newbuf = s->alloc_end - l->token_size * 2 + 1;
		if (newbuf <= s->alloc_cursor) {
			cparse_error_out_of_memory(s);
		}
		memmove(newbuf, l->token_buffer, l->token_size + 1);
	}
	l->token_buffer[l->token_size++] = (char)l->curr;
	l->token_buffer[l->token_size] = 0;
	return cparse_lex_skip(s);
}

static bool cparse_lex_accept(struct cparse_state* s, const char* keyword)
{
	struct cparse_lexer* l = &s->lex;
	while (*keyword && l->curr == *keyword) {
		cparse_lex_push(s);
		++keyword;
	}
	return *keyword == 0;
}

static bool cparse_lex_accept_c(struct cparse_state* s, char ch)
{
	if (s->lex.curr == ch)
	{
		cparse_lex_push(s);
		return true;
	}
	return false;
}

static bool cparse_lex_is_identifier_char(int ch, bool first)
{
	return	(ch >= 'a' && ch <= 'z') ||
			(ch >= 'A' && ch <= 'Z') ||
			(ch == '_') ||
			(!first && ch >= '0' && ch <= '9');
}

static cparse_token_t cparse_lex(struct cparse_state* s)
{
	struct cparse_lexer* l = &s->lex;
	l->lookahead = CPARSE_TOK_EOF;
	l->token_size = 0;

	for (;;) {
		switch (l->curr)
		{
			case -1:
				return CPARSE_TOK_EOF;

			case '\n': case '\r': case ' ': case '\t':
				cparse_lex_skip(s);
				continue;

			case '.': case ',': case ';': case '(': case ')': case '[': case ']': case '{': case '}': case ':':
			{
				int ch = l->curr;
				cparse_lex_push(s);
				return l->lookahead = ch;
			}

			case 'd':
				cparse_lex_push(s);
				l->lookahead = CPARSE_TOK_IDENTIFIER;
				if (cparse_lex_accept_c(s, 'o')) {
					l->lookahead = CPARSE_KW_DO;
					if (cparse_lex_accept_c(s, 'o'))
					{
						l->lookahead = CPARSE_KW_DOUBLE;
					}
				}
				goto parse_identifier;

			case 'e':
				cparse_lex_push(s);
				l->lookahead = CPARSE_TOK_IDENTIFIER;
				if (cparse_lex_accept_c(s, 'l'))
				{
					if (cparse_lex_accept(s, "se"))
					{
						l->lookahead = CPARSE_KW_ELSE;
					}
				}
				else if (cparse_lex_accept_c(s, 'n'))
				{
					if (cparse_lex_accept(s, "um"))
					{
						l->lookahead = CPARSE_KW_ENUM;
					}
				}
				goto parse_identifier;

			case 'f':
				cparse_lex_push(s);
				l->lookahead = CPARSE_TOK_IDENTIFIER;
				if (cparse_lex_accept_c(s, 'o'))
				{
					if (cparse_lex_accept_c(s, 'r'))
					{
						l->lookahead = CPARSE_KW_FOR;
					}
				}
				else if (cparse_lex_accept_c(s, 'l'))
				{
					if (cparse_lex_accept(s, "oat"))
					{
						l->lookahead = CPARSE_KW_FLOAT;
					}
				}
				goto parse_identifier;

			case 'i':
				cparse_lex_push(s);
				l->lookahead = CPARSE_TOK_IDENTIFIER;
				if (cparse_lex_accept_c(s, 'f'))
				{
					l->lookahead = CPARSE_KW_IF;
				}
				else if (cparse_lex_accept_c(s, 'n'))
				{
					if (cparse_lex_accept_c(s, 't'))
					{
						l->lookahead = CPARSE_KW_INT;
					}
				}
				goto parse_identifier;

			case 'l':
				cparse_lex_push(s);
				l->lookahead = CPARSE_TOK_IDENTIFIER;
				if (cparse_lex_accept(s, "ong"))
				{
					l->lookahead = CPARSE_KW_LONG;
				}
				goto parse_identifier;

			case 's':
				cparse_lex_push(s);
				l->lookahead = CPARSE_TOK_IDENTIFIER;
				if (cparse_lex_accept_c(s, 't')) {
					if (cparse_lex_accept_c(s, 'r')) {
						if (cparse_lex_accept(s, "uct"))
						{
							l->lookahead = CPARSE_KW_STRUCT;
						}
					}
					else if (cparse_lex_accept(s, "atic"))
					{
						l->lookahead = CPARSE_KW_STATIC;
					}
				}
				else if (cparse_lex_accept_c(s, 'h'))
				{
					if (cparse_lex_accept(s, "ort"))
					{
						l->lookahead = CPARSE_KW_SHORT;
					}
				}
				else if (cparse_lex_accept(s, "igned"))
				{
					l->lookahead = CPARSE_KW_SIGNED;
				}
				goto parse_identifier;

			case 't':
				cparse_lex_push(s);
				l->lookahead = CPARSE_TOK_IDENTIFIER;
				if (cparse_lex_accept(s, "ypedef"))
				{
					l->lookahead = CPARSE_KW_TYPEDEF;
				}
				goto parse_identifier;

			case 'u':
				cparse_lex_push(s);
				l->lookahead = CPARSE_TOK_IDENTIFIER;
				if (cparse_lex_accept(s, "nsigned"))
				{
					l->lookahead = CPARSE_KW_UNSIGNED;
				}
				goto parse_identifier;

			case 'v':
				cparse_lex_push(s);
				l->lookahead = CPARSE_TOK_IDENTIFIER;
				if (cparse_lex_accept(s, "olatile"))
				{
					l->lookahead = CPARSE_KW_VOLATILE;
				}
				goto parse_identifier;

			case 'w':
				cparse_lex_push(s);
				l->lookahead = CPARSE_TOK_IDENTIFIER;
				if (cparse_lex_accept(s, "hile"))
				{
					l->lookahead = CPARSE_KW_WHILE;
				}
				goto parse_identifier;

			default:
				if (!cparse_lex_is_identifier_char(l->curr, true))
					goto lex_error;
				cparse_lex_push(s);
				l->lookahead = CPARSE_TOK_IDENTIFIER;

			parse_identifier:
				while (cparse_lex_is_identifier_char(l->curr, false)) {
					cparse_lex_push(s);
					l->lookahead = CPARSE_TOK_IDENTIFIER;
				}
				return l->lookahead;

			lex_error:
				cparse_error(s, CPARSE_RESULT_SYNTAX_ERROR, "unexpected '%c'.", s->lex.curr);
				break;
		}
	}
}

/* parsing helpers */

static void cparse_error_syntax(struct cparse_state* s)
{
	cparse_error(s, CPARSE_RESULT_SYNTAX_ERROR, "unexpected '%s'.", s->lex.token_buffer);
}

static void cparse_error_syntax_expected(struct cparse_state* s, cparse_token_t tok)
{
	cparse_error(s, CPARSE_RESULT_SYNTAX_ERROR, "missing '%s' before '%s'.", cparse_strtok(tok), s->lex.token_buffer);
}

static bool cparse_peek(struct cparse_state* s, cparse_token_t tok)
{
	return s->lex.lookahead == tok;
}

static bool cparse_accept(struct cparse_state* s, cparse_token_t tok)
{
	if (cparse_peek(s, tok))
	{
		cparse_lex(s);
		return true;
	}
	return false;
}

static void cparse_check(struct cparse_state* s, cparse_token_t tok)
{
	if (!cparse_peek(s, tok))
		cparse_error_syntax_expected(s, tok);
}

static void cparse_expect(struct cparse_state* s, cparse_token_t tok)
{
	cparse_check(s, tok);
	cparse_lex(s);
}

static const char* cparse_scan_token_string(struct cparse_state* s)
{
	char* tok = cparse_alloc(s, s->lex.token_size + 1, 1);
	memcpy(tok, s->lex.token_buffer, s->lex.token_size + 1);
	cparse_lex(s);
	return tok;
}

/* initialize functions */
static void cparse_node_init(struct cparse_node* node, enum cparse_node_type type)
{
	node->type = type;
}

static void cparse_node_decl_init(struct cparse_node_decl* decl, enum cparse_node_type type, const char* spelling)
{
	cparse_node_init(&decl->node, type);
	decl->spelling = spelling;
	decl->next = NULL;
}

/* parsing functions */

static struct cparse_type* cparse_parse_type(struct cparse_state* s)
{
	bool primitive_type = false;
	bool primitive_signed = true;
	
	struct cparse_type_ref* ref = 

	if (cparse_accept(s, CPARSE_KW_SIGNED)) {
		primitive_type = true;
	}
	else if (cparse_accept(s, CPARSE_KW_UNSIGNED)) {
		primitive_type = true;
		primitive_signed = false;
	}

	struct cparse_type* 

	switch (s->lex.lookahead)
	{
		case CPARSE_KW_INT:
			ret
			break;

		default:
			break;
	}

}

static struct cparse_node_decl_struct* cparse_parse_struct(struct cparse_state* s)
{
	cparse_expect(s, CPARSE_KW_STRUCT);
	cparse_check(s, CPARSE_TOK_IDENTIFIER);
	
	struct cparse_node_decl_struct* struct_decl = cparse_alloc_type(s, struct cparse_node_decl_struct);
	cparse_node_decl_init(&struct_decl->decl, CPARSE_NODE_TYPE_DECL_STRUCT, cparse_scan_token_string(s));

	cparse_expect(s, '{');
	cparse_expect(s, '}');
	cparse_expect(s, ';');

	return struct_decl;
}

static struct cparse_unit* cparse_parse_unit(struct cparse_state* s)
{
	struct cparse_unit* unit = cparse_alloc_type(s, struct cparse_unit);
	struct cparse_node_decl** last_next = &unit->decls;

	while (!cparse_peek(s, CPARSE_TOK_EOF))
	{
		struct cparse_node_decl* decl = NULL;
		switch (s->lex.lookahead)
		{
			case CPARSE_KW_STRUCT:
				decl = (struct cparse_node_decl*)cparse_parse_struct(s);
				break;

			default:
				cparse_error_syntax(s);
		}
		*last_next = decl;
		last_next = &decl->next;
	}

	return unit;
}

CPARSE_API enum cparse_result cparse_file(const char* filename, struct cparse_info const* info, struct cparse_unit** out)
{
	struct cparse_state state;

	/* set the error handler and handle any error */
	int result = setjmp(state.error_handler);
	if (result) goto cleanup;

	state.alloc_begin = info->buffer;
	state.alloc_end = info->buffer + info->buffer_size;
	state.alloc_cursor = info->buffer;

	/* init lexer */
	{
		struct cparse_lexer* lex = &state.lex;
		lex->file = NULL;
		fopen_s(&lex->file, filename, "r");
		lex->filename = filename;
		lex->column = 0;
		lex->line = 1;
		lex->token_buffer_capacity = 511;
		lex->token_buffer = cparse_alloc(&state, lex->token_buffer_capacity + 1, 1);
		lex->token_buffer[0] = 0;
		lex->token_size = 0;

		if (!lex->file) {
			cparse_error(&state, CPARSE_RESULT_INVALID_INPUT_FILE, "cannot open file.");
		}

		cparse_lex_skip(&state);
		cparse_lex(&state);
	}

	*out = cparse_parse_unit(&state);

cleanup:
	fclose(state.lex.file);
	return result;
}

#undef cparse_alloc_type
#undef uint
#undef true
#undef false
#undef bool
#endif
