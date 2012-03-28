/*
  +----------------------------------------------------------------------+
  | Authors: Alexey Rybak <raa@phpclub.net>                              |
  +----------------------------------------------------------------------+
*/

/* $Id: php_blitz.h,v 0.6 2006/09/24 16:45:59 fisher Exp $ */

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

// class Blitz
PHP_FUNCTION(blitz_init);
PHP_FUNCTION(blitz_load);
PHP_FUNCTION(blitz_set);
PHP_FUNCTION(blitz_set_global);
PHP_FUNCTION(blitz_dump_set);
PHP_FUNCTION(blitz_dump_struct);
PHP_FUNCTION(blitz_dump_iterations);
PHP_FUNCTION(blitz_parse);
PHP_FUNCTION(blitz_parse_new);
PHP_FUNCTION(blitz_include);
PHP_FUNCTION(blitz_iterate);
PHP_FUNCTION(blitz_context);
PHP_FUNCTION(blitz_block);
PHP_FUNCTION(blitz_fetch);

// class BlitzPack
PHP_FUNCTION(blitz_pack_init);
PHP_FUNCTION(blitz_pack_add);
PHP_FUNCTION(blitz_pack_save);

ZEND_BEGIN_MODULE_GLOBALS(blitz)
	long  var_prefix;
	char *node_open;
	char *node_close;
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

#define BLITZ_TYPE_VAR      	1	
#define BLITZ_TYPE_METHOD       2	
#define BLITZ_IS_VAR(type)      (type & BLITZ_TYPE_VAR)
#define BLITZ_IS_METHOD(type)   (type & BLITZ_TYPE_METHOD)

#define BLITZ_ARG_TYPE_VAR		1
#define BLITZ_ARG_TYPE_STR      2
#define BLITZ_ARG_TYPE_NUM      4

#define BLITZ_TAG_VAR_PREFIX    		'$'
#define BLITZ_TAG_VAR_PREFIX_S  		"$"
#define BLITZ_TAG_OPEN_DEFAULT 			"{{"
#define BLITZ_TAG_CLOSE_DEFAULT	 		"}}"

#define BLITZ_MAX_LEXEM_LEN    			1024
#define BLITZ_INPUT_BUF_SIZE 	   		512 
#define BLITZ_ALLOC_TAGS_STEP   		16
#define BLITZ_CALL_ALLOC_ARG_INIT		2


#define BLITZ_NODE_TYPE_PREDEF_MASK		28
#define BLITZ_IS_PREDEF_METHOD(type)	(type & BLITZ_NODE_TYPE_PREDEF_MASK)
#define BLITZ_NODE_TYPE_IF_S			"if"
#define BLITZ_NODE_TYPE_INCLUDE_S		"include"
#define BLITZ_NODE_TYPE_BEGIN_S			"begin"
#define BLITZ_NODE_TYPE_END_S  			"end"
#define BLITZ_NODE_TYPE_IF         	    (1<<2 | BLITZ_TYPE_METHOD)
#define BLITZ_NODE_TYPE_INCLUDE    		(2<<2 | BLITZ_TYPE_METHOD)
#define BLITZ_NODE_TYPE_BEGIN           (3<<2 | BLITZ_TYPE_METHOD) // non-finalized node
#define BLITZ_NODE_TYPE_END             (4<<2 | BLITZ_TYPE_METHOD) // non-finalized node
#define BLITZ_NODE_TYPE_CONTEXT         (5<<2 | BLITZ_TYPE_METHOD)

#define BLITZ_ITPL_ALLOC_INIT			4	
#define BLITZ_PACK_SHIFT_ALLOC_INIT   	8
#define BLITZ_CONTEXT_PATH_MAX_LEN      1024

#define BLITZ_FLAG_FETCH_INDEX_BUILT    1
#define BLITZ_FLAG_GLOBALS_IS_OTHER     2
#define BLITZ_FLAG_IS_FROM_PACK         4

// call argument
typedef struct {
    char *name;
    unsigned long len;
    char type;
} call_arg;

// node
typedef struct _tpl_node_struct {
    unsigned long pos_begin;
    unsigned long pos_end;
    unsigned long pos_begin_shift;
    unsigned long pos_end_shift;
    char type;
    char has_error;
    char *lexem; 
    unsigned long lexem_len;
    call_arg *args;
    unsigned char n_args;
    struct _tpl_node_struct **children; 
    unsigned int n_children;
    unsigned int pos_in_list;
} tpl_node_struct;

// config
typedef struct {
    char *open_node;
    char *close_node;
    unsigned int l_open_node;
    unsigned int l_close_node;
} tpl_config_struct;

// template pack (parsed cache)
typedef struct _blitz_pack {
    void *tpl_head;
    void *tpl_pack;
    unsigned long tpl_head_len;
    unsigned long tpl_pack_len;
    unsigned long tpl_num;
    void *data;
    unsigned long data_len;
    HashTable *ht_tpl_shift;
    char *filename;
    unsigned long version;
} blitz_pack;

// template
typedef struct _blitz_tpl {
    char *name;
    tpl_config_struct *config;
    tpl_node_struct *nodes;
    unsigned int n_nodes;
    char *body;
    unsigned long body_len;
    HashTable *hash_globals;
    zval *iterations;
    zval **current_iteration;  // current iteraion values
    zval **current_iteration_node; // list of current context iterations (current_iteration is last element in this list)
    char *current_path;
    char *path_buf;
    HashTable *fetch_index; // path -> node number, used for fetch operations only, is not built by default
    char flags;
    char hash_globals_is_others; // 2DO: use BLITZ_FLAG_GLOBALS_IS_OTHER
    HashTable *ht_tpl_name; // index template_name -> itpl_list number
    struct _blitz_tpl **itpl_list; // list of included templates
    unsigned int itpl_list_alloc;
    unsigned int itpl_list_len;
    unsigned int is_from_pack; // 2DO: use BLITZ_FLAG_IS_FROM_PACK
    blitz_pack *pack;
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


#define BLITZ_SKIP_BLANK_END(c,len,pos)                                        \
    len = 0;                                                                   \
    while(isspace(c[len]) len++;                                               \
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
            ++(p);                                                             \
            ++(len);							                               \
            if (was_escaped) was_escaped = 0;                                  \
        }                                                                      \
        (c)++;                                                                 \
        (pos)++;                                                               \
    }                                                                          \
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
            ++(p);                                                             \
            ++(len);							                               \
            if (was_escaped) was_escaped = 0;                                  \
        }                                                                      \
        ++(c);                                                                 \
        ++(pos);                                                               \
    }                                                                          \
    *(p) = '\x0';

#define BLITZ_SCAN_NUMBER(c,p,pos,symb)                                        \
    while(((symb) = *(c)) && BLITZ_IS_NUMBER(symb)) {                          \
        *(p) = symb;                                                           \
        ++(p);                                                                 \
        ++(c);                                                                 \
        ++(pos);                                                               \
    }                                                                          \
    *(p) = '\x0';

#define BLITZ_SCAN_ALNUM(c,p,pos,symb)                                                   \
    while(((symb) = *(c)) && (BLITZ_IS_NUMBER(symb) || BLITZ_IS_ALPHA(symb))) {          \
        *(p) = symb;                                                                     \
        ++(p);                                                                           \
        ++(c);                                                                           \
        ++(pos);                                                                         \
    }                                                                                    \
    *(p) = '\x0';

#define BLITZ_CALL_STATE_NEXT_ARG    1
/*
#define BLITZ_CALL_STATE_PARAM       2
#define BLITZ_CALL_STATE_SQS         4
#define BLITZ_CALL_STATE_DQS         8
#define BLITZ_CALL_STATE_NUMBER      16
*/
#define BLITZ_CALL_STATE_FINISHED    2
#define BLITZ_CALL_STATE_HAS_NEXT    3 
#define BLITZ_CALL_STATE_BEGIN       4
#define BLITZ_CALL_STATE_END         5
#define BLITZ_CALL_STATE_ERROR       0

#define BLITZ_CALL_ERROR             1
#define BLITZ_CALL_ERROR_IF          2
#define BLITZ_CALL_ERROR_INCLUDE     3


#define BLITZ_ARG_NOT_EMPTY(a,ht,res)                                                             \
    if((a).type & BLITZ_ARG_TYPE_STR) {                                                           \
        (res) = ((a).len>0) ? 1 : 0;                                                              \
    } else if ((a).type & BLITZ_ARG_TYPE_NUM) {												      \
        (res) = (0 == strncmp((a).name,"0",1)) ? 0 : 1;                                           \
    } else if (((a).type & BLITZ_ARG_TYPE_VAR) && ht) {                                           \
        zval **z;                                                                                 \
        if((a).name && (a).len>0) {                                                               \
            if (SUCCESS != zend_hash_find(ht,(a).name,1+(a).len,(void**)&z)) {                    \
                res = -1;                                                                         \
            } else {                                                                              \
                switch (Z_TYPE_PP(z)) {                                                           \
                    case IS_BOOL: res = (0 == Z_LVAL_PP(z)) ? 0 : 1; break;                       \
                    case IS_NULL: res = 0; break;                                                 \
                    case IS_STRING: res = (0 == Z_STRLEN_PP(z)) ? 0 : 1; break;                   \
                    case IS_LONG: res = (0 == Z_LVAL_PP(z)) ? 0 : 1; break;                       \
                    case IS_DOUBLE: res = (.0 == Z_LVAL_PP(z)) ? 0 : 1; break;                    \
                    default: res = 0; break;                                                      \
                }                                                                                 \
            }                                                                                     \
        } else {                                                                                  \
            res = 0;                                                                              \
        }                                                                                         \
    } else {                                                                                      \
        res = 0;                                                                                  \
    }

// switch (Z_TYPE_PP(z)) : see 10 lines upper
// well, we cannot set non-scalar template value, but if ever...
// case IS_ARRAY: res = (0 == zend_hash_num_elements(Z_ARRVAL_PP(z))) ? 0 : 1;

#define BLITZ_REALLOC_RESULT(blen,nlen,rlen,alen,res,pres)										  \
    (nlen) = (rlen) + (blen);												                      \
    if ((rlen) < (nlen)) {                                                                        \
        while ((alen) < (nlen)) {                                                                 \
            (alen) <<= 1;                                                                         \
        }                                                                                         \
        (res) = (unsigned char*)erealloc((res),(alen)*sizeof(unsigned char));                                     \
        (pres) = (res) + (rlen);                                                                  \
    } 																		                      \

#endif	/* PHP_BLITZ_H */
