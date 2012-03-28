/*
  +----------------------------------------------------------------------+
  | Authors: Alexey Rybak <raa@phpclub.net>                              |
  +----------------------------------------------------------------------+
*/

/* $Id: php_blitz.h,v 0.3 2005/10/02 18:38:04 fisher Exp $ */

#ifndef PHP_BLITZ_H
#define PHP_BLITZ_H

extern zend_module_entry blitz_module_entry;
#define phpext_blitz_ptr &blitz_module_entry

#ifdef PHP_WIN32
#define PHP_BLITZ_API __declspec(dllexport)
#define BLITZ_USE_STREAMS
#else
#define PHP_BLITZ_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

// php interface
PHP_MINIT_FUNCTION(blitz);
PHP_MSHUTDOWN_FUNCTION(blitz);
PHP_RINIT_FUNCTION(blitz);
PHP_RSHUTDOWN_FUNCTION(blitz);
PHP_MINFO_FUNCTION(blitz);

PHP_FUNCTION(blitz_init);
PHP_FUNCTION(blitz_set);
PHP_FUNCTION(blitz_dump_set);
PHP_FUNCTION(blitz_parse);
PHP_FUNCTION(blitz_include);

ZEND_BEGIN_MODULE_GLOBALS(blitz)
	long  var_prefix;
	char *tag_open;
	char *tag_close;
ZEND_END_MODULE_GLOBALS(blitz)

#ifdef ZTS
#define BLITZ_G(v) TSRMG(blitz_globals_id, zend_blitz_globals *, v)
#else
#define BLITZ_G(v) (blitz_globals.v)
#endif

/*****************************************************************************/
/* additional headings                                                       */
/*****************************************************************************/
#include <stdio.h>

#ifndef uchar
#define uchar unsigned char
#endif
#ifndef ulong
#define ulong unsigned long
#endif
#ifndef uint
#define uint unsigned int
#endif

#define BLITZ_TYPE_VAR      	1	
#define BLITZ_TYPE_METHOD       2	
#define BLITZ_IS_VAR(type)      (type & BLITZ_TYPE_VAR)
#define BLITZ_IS_METHOD(type)   (type & BLITZ_TYPE_METHOD)

#define BLITZ_ARG_TYPE_VAR		1
#define BLITZ_ARG_TYPE_STR      2
#define BLITZ_ARG_TYPE_NUM      4

#define BLITZ_TAG_VAR_PREFIX    '$'
#define BLITZ_TAG_VAR_PREFIX_S  "$"
#define BLITZ_TAG_OPEN_DEFAULT 	"{{"
#define BLITZ_TAG_CLOSE_DEFAULT	 "}}"

#define BLITZ_MAX_LEXEM_LEN    		1024
#define BLITZ_INPUT_BUF_SIZE 	   	512 
#define BLITZ_ALLOC_TAGS_STEP   	16
#define BLITZ_CALL_ALLOC_ARG_INIT	2

#define BLITZ_METHOD_PREDEF_MASK		28
#define BLITZ_IS_PREDEF_METHOD(type)	(type & BLITZ_METHOD_PREDEF_MASK)
#define BLITZ_METHOD_IF_S				"if"
#define BLITZ_METHOD_INCLUDE_S			"include"
#define BLITZ_METHOD_IF         		4
#define BLITZ_METHOD_INCLUDE    		8

#define BLITZ_ITPL_ALLOC_INIT	4	

// call arguments
typedef struct {
    char *name;
    ulong len;
    char type;
} call_arg;

// tags
typedef struct {
    ulong pos_begin;
    ulong pos_end;
    char type;
    char has_error;
    char *lexem; 
    call_arg *args;
    uchar n_args;
} tpl_parts_struct;

// config
typedef struct {
    char *open_tag;
    char *close_tag;
    uint l_open_tag;
    uint l_close_tag;
} tpl_config_struct;

// template
typedef struct _blitz_tpl {
    char *name;
    tpl_config_struct *config;
    tpl_parts_struct *tags;
    uint n_tags;
    char *body;
    ulong body_len;
    HashTable *ht_data;
    char ht_data_is_others;
    HashTable *ht_tpl_name;
    struct _blitz_tpl **itpl_list;
    uint itpl_list_alloc;
    uint itpl_list_len;
} blitz_tpl;

/* call scanner */

#define BLITZ_ISNUM_MIN                48  // '0'
#define BLITZ_ISNUM_MAX                57  // '9'
#define BLITZ_ISALPHA_SMALL_MIN        97  // 'a'
#define BLITZ_ISALPHA_SMALL_MAX        122 // 'z'
#define BLITZ_ISALPHA_BIG_MIN          65  // 'A'
#define BLITZ_ISALPHA_BIG_MAX          90  // 'Z'

#define BLITZ_SKIP_BLANK(c,len,pos)                                            \
    len = 0;                                                                   \
    while(isspace(c[len])) len++;                                              \
    c+=len; pos+=len;                                                          \

#define BLITZ_IS_NUMBER(c)                                                     \
    (                                                                          \
        (c)>=BLITZ_ISNUM_MIN                                                   \
        && (c)<=BLITZ_ISNUM_MAX                                                \
    )

#define BLITZ_IS_ALPHA(c)                                                      \
  ( ((c)>=BLITZ_ISALPHA_SMALL_MIN && (c)<=BLITZ_ISALPHA_SMALL_MAX)             \
    || ((c)>=BLITZ_ISALPHA_BIG_MIN && (c)<=BLITZ_ISALPHA_BIG_MAX)              \
    || (c) == '-' || (c) == '_'                                                \
  )


#define BLITZ_SCAN_SINGLE_QUOTED(c,p,pos,len,ok)                               \
    was_escaped = 0;                                                           \
    ok = 0;                                                                    \
    while(*(c)) {                                                              \
        if (*(c) == '\'' && !was_escaped) {                                    \
            ok = 1;                                                            \
            break;                                                             \
        } else if (*(c) == '\\' && !was_escaped) {                             \
            was_escaped = 1;                                                   \
        } else {                                                               \
            *(p) = *(c);                                                       \
            (p)++;                                                             \
            (len)++;							                               \
            if (was_escaped) was_escaped = 0;                                  \
        }                                                                      \
        (c)++;                                                                 \
        (pos)++;                                                               \
    }                                                                          \
    if((c)) (c)++;                                                             \
    *(p) = '\x0';

#define BLITZ_SCAN_DOUBLE_QUOTED(c,p,pos,len,ok)                               \
    was_escaped = 0;                                                           \
    ok = 0;                                                                    \
    while(*(c)) {                                                              \
        if (*(c) == '\"' && !was_escaped) {                                    \
            ok = 1;                                                            \
            break;                                                             \
        } else if (*(c) == '\\' && !was_escaped) {                             \
            was_escaped = 1;                                                   \
        } else {                                                               \
            *(p) = *(c);                                                       \
            (p)++;                                                             \
            (len)++;							                               \
            if (was_escaped) was_escaped = 0;                                  \
        }                                                                      \
        (c)++;                                                                 \
        (pos)++;                                                               \
    }                                                                          \
    if((c)) (c)++;                                                             \
    *(p) = '\x0';

#define BLITZ_SCAN_NUMBER(c,p,pos,symb)                                        \
    while(((symb) = *(c)) && BLITZ_IS_NUMBER(symb)) {                          \
        *(p) = symb;                                                           \
        (p)++;                                                                 \
        (c)++;                                                                 \
        (pos)++;                                                               \
    }                                                                          \
    *(p) = '\x0';

#define BLITZ_SCAN_ALNUM(c,p,pos,symb)                                                   \
    while(((symb) = *(c)) && (BLITZ_IS_NUMBER(symb) || BLITZ_IS_ALPHA(symb))) {          \
        *(p) = symb;                                                                     \
        (p)++;                                                                           \
        (c)++;                                                                           \
        (pos)++;                                                                         \
    }                                                                                    \
    *(p) = '\x0';

#define BLITZ_CALL_STATE_NEXT_ARG    1
#define BLITZ_CALL_STATE_PARAM       2
#define BLITZ_CALL_STATE_SQS         4
#define BLITZ_CALL_STATE_DQS         8
#define BLITZ_CALL_STATE_NUMBER      16
#define BLITZ_CALL_STATE_FINISHED    32
#define BLITZ_CALL_STATE_HAS_NEXT    64
#define BLITZ_CALL_STATE_ERROR       0

#define BLITZ_ARG_NOT_EMPTY(a,ht,res)                                                             \
    if((a).type & BLITZ_ARG_TYPE_STR) {                                                           \
        (res) = ((a).len>0) ? 1 : 0;                                                              \
    } else if ((a).type & BLITZ_ARG_TYPE_NUM) {												      \
        (res) = (0 == strncmp((a).name,"0",1)) ? 0 : 1;                                           \
    } else if ((a).type & BLITZ_ARG_TYPE_VAR) {                                                   \
        zval **z;                                                                                 \
        if((a).name && (a).len>0 &&                                                               \
            (SUCCESS == zend_hash_find(ht,(a).name,1+(a).len,(void**)&z)))                        \
        {                                                                                         \
            switch (Z_TYPE_PP(z)) {                                                               \
            	case IS_NULL: res = 0; break;                                                     \
                case IS_STRING: res = (0 == Z_STRLEN_PP(z)) ? 0 : 1; break;                       \
                case IS_LONG: res = (0 == Z_LVAL_PP(z)) ? 0 : 1; break;                           \
                case IS_DOUBLE: res = (.0 == Z_LVAL_PP(z)) ? 0 : 1; break;                        \
                default: res = 0; break;                                                          \
            }                                                                                     \
        } else {                                                                                  \
            res = 0;                                                                              \
        }                                                                                         \
    } else {                                                                                      \
        res = 0;                                                                                  \
    }                                                                                             

// well, we cannot set non-scalar template value, but if ever...
// case IS_ARRAY: res = (0 == zend_hash_num_elements(Z_ARRVAL_PP(z))) ? 0 : 1;

#define BLITZ_REALLOC_RESULT(blen,nlen,rlen,alen,res,pres)										  \
    (nlen) = (rlen) + (blen);												                      \
    if ((rlen) < (nlen)) {                                                                        \
        while ((alen) < (nlen)) {                                                                 \
            (alen) <<= 1;                                                                         \
        }                                                                                         \
        (res) = (uchar*)erealloc((res),(alen)*sizeof(uchar));                                     \
        (pres) = (res) + (rlen);                                                                  \
    } 																		                      \

#endif	/* PHP_BLITZ_H */
