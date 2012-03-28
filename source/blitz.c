/*
  +----------------------------------------------------------------------+
  | Authors: Alexey Rybak <raa@phpclub.net>,                             |
  |          template analyzing is based on php_templates code by        | 
  |          Maxim Poltarak (http://php-templates.sf.net)                | 
  +----------------------------------------------------------------------+
*/

/* $Id: blitz.c,v 0.3 2005/10/02 18:38:04 fisher Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "safe_mode.h"
#include "php_globals.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_blitz.h"

ZEND_DECLARE_MODULE_GLOBALS(blitz)

// some declaration - for recursive code
static int blitz_exec_template(blitz_tpl *tpl, zval *id, uchar **result, ulong *result_len TSRMLS_DC);

/* True global resources - no need for thread safety here */
static int le_blitz;

static zend_class_entry blitz_class_entry;

/* {{{ blitz_functions[] */
function_entry blitz_functions[] = {
    PHP_FALIAS(blitz,		blitz_init,			NULL)
    PHP_FALIAS(dump_set,	blitz_dump_set,		NULL)
    PHP_FALIAS(set,			blitz_set,			NULL)
	PHP_FALIAS(parse,		blitz_parse,		NULL)
	PHP_FALIAS(include,		blitz_include,	NULL)
	{NULL, NULL, NULL}
};
/* }}} */

/* {{{ blitz_module_entry */
zend_module_entry blitz_module_entry = {
	STANDARD_MODULE_HEADER,
	"blitz",
	blitz_functions,
	PHP_MINIT(blitz),
	PHP_MSHUTDOWN(blitz),
	PHP_RINIT(blitz),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(blitz),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(blitz),
	NO_VERSION_YET,
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_BLITZ
ZEND_GET_MODULE(blitz)
#endif

/* {{{ PHP_INI */
PHP_INI_BEGIN()
#if PHP_API_VERSION < 20031224
    STD_PHP_INI_ENTRY("blitz.var_prefix", BLITZ_TAG_VAR_PREFIX_S, PHP_INI_ALL, 
        OnUpdateInt, var_prefix, zend_blitz_globals, blitz_globals)
#else
    STD_PHP_INI_ENTRY("blitz.var_prefix", BLITZ_TAG_VAR_PREFIX_S, PHP_INI_ALL, 
        OnUpdateLong, var_prefix, zend_blitz_globals, blitz_globals)
#endif
    STD_PHP_INI_ENTRY("blitz.tag_open", BLITZ_TAG_OPEN_DEFAULT, PHP_INI_ALL, 
		OnUpdateString, tag_open, zend_blitz_globals, blitz_globals)
    STD_PHP_INI_ENTRY("blitz.tag_close", BLITZ_TAG_CLOSE_DEFAULT, PHP_INI_ALL, 
		OnUpdateString, tag_close, zend_blitz_globals, blitz_globals)
PHP_INI_END()
/* }}} */


/* f - filename, b - buf, bl - buf_len, s = stream */
/* use: BLITZ_READ_TPL_STREAM(filename,buf,buf_len,stream) */
#define BLITZ_READ_TPL_STREAM(f,b,bl,s)                                             \
    if(php_check_open_basedir((f) TSRMLS_CC)){                                      \
        return 0;                                                                   \
    }                                                                               \
    (s) = php_stream_open_wrapper((f), "rb",                                        \
        IGNORE_PATH|IGNORE_URL|IGNORE_URL_WIN|ENFORCE_SAFE_MODE|REPORT_ERRORS, NULL \
    );                                                                              \
    if(!(s)) {                                                                      \
        return 0;                                                                   \
    }                                                                               \
    (bl) = php_stream_copy_to_mem((s), &(b), PHP_STREAM_COPY_ALL, 0);               \
    php_stream_close((s));                                                          \

/* use: BLITZ_READ_TPL_FOPEN(filename,buf,buf_len,f,get_len) */
#define BLITZ_READ_TPL_FOPEN(f,b,bl,s,gl)											\
    if(php_check_open_basedir((f) TSRMLS_CC)){                                      \
        return 0;                                                                   \
    }																				\
																					\
    if(!((s) = fopen((f), "rb"))) {													\
        php_error_docref(NULL TSRMLS_CC, E_ERROR,									\
            "unable to open file %s", (f)											\
        );																			\
        return 0;																	\
    }																				\
																					\
    (b) = (char*)emalloc(BLITZ_INPUT_BUF_SIZE);										\
    (bl) = 0;																		\
																					\
    while(((gl) = fread((b)+(bl), 1, BLITZ_INPUT_BUF_SIZE, (s))) > 0) {				\
        (bl) += (gl);																\
        (b) = (char*)erealloc((b), (bl) + BLITZ_INPUT_BUF_SIZE);					\
    }																				\
    fclose((s));																	\

/*  {{{ blitz_init_tpl(char *body, ulong body_len) */
/******************************************************************************/
inline blitz_tpl *blitz_init_tpl(
/******************************************************************************/
    char *filename, 
    HashTable *ht TSRMLS_DC) {
/******************************************************************************/
    uint add_buffer = 0;
    php_stream *stream = NULL;
    FILE *f = NULL;
    uint get_len = 0;
    uint filename_len = 0;

    blitz_tpl *tpl = (blitz_tpl*)emalloc(sizeof(blitz_tpl));

    if (!tpl) {
        php_error_docref(
			NULL TSRMLS_CC, E_ERROR,
			"unable to allocate memory for blitz template structure"
		);
        return NULL;
    }

    tpl->n_tags = 0;
    tpl->config = (tpl_config_struct*)emalloc(sizeof(tpl_config_struct));
    if (!tpl->config) {
        php_error_docref(
            NULL TSRMLS_CC, E_ERROR,
            "unable to allocate memory for blitz template config structure"
		);
        return NULL;
    }
    tpl->config->open_tag = BLITZ_TAG_OPEN_DEFAULT;
    tpl->config->close_tag = BLITZ_TAG_CLOSE_DEFAULT;
    tpl->config->l_open_tag = strlen(tpl->config->open_tag);
    tpl->config->l_close_tag = strlen(tpl->config->close_tag);

    tpl->tags = NULL;

/* 
it seems to be 10% faster to use just fopen/fread than 
streams on lebowski-bench. however under win32 there are errors 
with relative paths (see php_blitz.h), i didn't checked why yet ;)
*/
#ifdef BLITZ_USE_STREAMS
    BLITZ_READ_TPL_STREAM(filename,tpl->body,tpl->body_len,stream);
#else
    BLITZ_READ_TPL_FOPEN(filename,tpl->body,tpl->body_len,f,get_len);
#endif

    // search algorithm requires lager buffer: body_len + add_buffer
    add_buffer = MAX(tpl->config->l_open_tag,tpl->config->l_close_tag);
    tpl->body = erealloc(tpl->body,tpl->body_len + add_buffer);
    memset(tpl->body + tpl->body_len,'\0',add_buffer);

    filename_len = strlen(filename);
    tpl->name = emalloc((filename_len+1)*sizeof(char));
    if(tpl->name){
        memcpy(tpl->name,filename,filename_len);
        tpl->name[filename_len] = '\0';
    }

    if (ht == NULL) { // allocate personal hash-table
        tpl->ht_data = NULL;
        tpl->ht_data_is_others = 0;
        ALLOC_HASHTABLE(tpl->ht_data);

        if (
    		!tpl->ht_data 
		    || (FAILURE == zend_hash_init(tpl->ht_data, 8, NULL, ZVAL_PTR_DTOR, 0))
	    ) {
		    php_error_docref(
                NULL TSRMLS_CC, E_ERROR,
                "unable to allocate or fill memory for blitz params"
            );
            return NULL;
        }
    } else {
        /* 
           We "inherit" params from opener: the most correct way to do that
           is to follow a "copy-on-write" strategy. unless "inner" code doesn't
           modify template params - we use just a pointer-copy. otherwise we
           make a full data copy. But to make our life mush easier we just make 
           a copy and mark template with ht_data_is_others = 1 not to free this object.
        */
        tpl->ht_data = ht;
        tpl->ht_data_is_others = 1;
    }

    tpl->ht_tpl_name = NULL;
    ALLOC_HASHTABLE(tpl->ht_tpl_name);
	if (
		!tpl->ht_tpl_name
		|| (FAILURE == zend_hash_init(tpl->ht_tpl_name, 8, NULL, ZVAL_PTR_DTOR, 0))
	) {
		php_error_docref(
			NULL TSRMLS_CC, E_ERROR,
			"unable to allocate or fill memory for blitz template index"
		);
		return NULL;
	}

    tpl->itpl_list = 
        (blitz_tpl**)emalloc(BLITZ_ITPL_ALLOC_INIT*sizeof(blitz_tpl*));
    if(!tpl->itpl_list) {
        php_error_docref(
            NULL TSRMLS_CC, E_ERROR,
            "unable to allocate memory for inner-include blitz template list"
        );
        return NULL;
    }
    tpl->itpl_list_len = 0;
    tpl->itpl_list_alloc = BLITZ_ITPL_ALLOC_INIT;

    return tpl;
}
/* }}} */

/* {{{ blitz_free_tpl(blitz_tpl *tpl) */
/******************************************************************************/
inline void blitz_free_tpl(blitz_tpl *tpl) {
/******************************************************************************/
    uchar n_tags=0, n_args=0, i=0, j=0;

    if(!tpl) return;

    if(tpl->config) 
		efree(tpl->config);

    n_tags = tpl->n_tags;
    if(n_tags) {
        for(i=0;i<n_tags;++i) {
            n_args = tpl->tags[i].n_args;
            for(j=0;j<n_args;++j) {
                if(tpl->tags[i].args[j].name) {
	                efree(tpl->tags[i].args[j].name);
                }
            }
            if(tpl->tags[i].args) efree(tpl->tags[i].args);
            if(tpl->tags[i].lexem) efree(tpl->tags[i].lexem);
        }
    }

    if(tpl->name)
        efree(tpl->name);
    if(tpl->tags)
		efree(tpl->tags);
    if(tpl->body) 
		efree(tpl->body);
    if(tpl->ht_data && !tpl->ht_data_is_others)
	    FREE_HASHTABLE(tpl->ht_data);
    if(tpl->ht_tpl_name)
		FREE_HASHTABLE(tpl->ht_tpl_name);
    if(tpl->itpl_list) {
        for(i=0; i<tpl->itpl_list_len; i++) {
            if(tpl->itpl_list[i])
                blitz_free_tpl(tpl->itpl_list[i]);
        }
        efree(tpl->itpl_list);
    }

    efree(tpl);
}
/* }}} */


/* {{{ blitz_resource_dtor */
/******************************************************************************/
static void blitz_resource_dtor(zend_rsrc_list_entry *rsrc TSRMLS_DC) {
/******************************************************************************/
    blitz_tpl *tpl = NULL;
    tpl = (blitz_tpl*)rsrc->ptr;
    blitz_free_tpl(tpl);
}
/* }}} */

/* {{{ php_blitz_init_globals */
/******************************************************************************/
static void php_blitz_init_globals(zend_blitz_globals *blitz_globals)
/******************************************************************************/
{
	blitz_globals->var_prefix = BLITZ_TAG_VAR_PREFIX;
	blitz_globals->tag_open = BLITZ_TAG_OPEN_DEFAULT;
	blitz_globals->tag_close = BLITZ_TAG_CLOSE_DEFAULT;
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION */
/******************************************************************************/
PHP_MINIT_FUNCTION(blitz)
/******************************************************************************/
{
	ZEND_INIT_MODULE_GLOBALS(blitz, php_blitz_init_globals, NULL);
	REGISTER_INI_ENTRIES();
    le_blitz = zend_register_list_destructors_ex(
		blitz_resource_dtor, NULL, "Blitz template handle", module_number);

    INIT_CLASS_ENTRY(blitz_class_entry, "blitz", blitz_functions);
    zend_register_internal_class(&blitz_class_entry TSRMLS_CC);

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION */
/******************************************************************************/
PHP_MSHUTDOWN_FUNCTION(blitz)
/******************************************************************************/
{
	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION */
/******************************************************************************/
PHP_RINIT_FUNCTION(blitz)
/******************************************************************************/
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_RSHUTDOWN_FUNCTION */
/******************************************************************************/
PHP_RSHUTDOWN_FUNCTION(blitz)
/******************************************************************************/
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION */
/******************************************************************************/
PHP_MINFO_FUNCTION(blitz)
/******************************************************************************/
{
	php_info_print_table_start();
	php_info_print_table_row(2, "Blitz support", "enabled");
	php_info_print_table_row(2, "Revision", "$Revision: 0.3 $");
	php_info_print_table_end();
	DISPLAY_INI_ENTRIES();
}
/* }}} */

/******************************************************************************/
static void php_blitz_dump_struct(blitz_tpl *tpl) {
/******************************************************************************/
    ulong i = 0, j = 0;
    php_printf("php_blitz_dump_struct, tags: %ld\n",(ulong)tpl->n_tags);
    for (i=0; i<tpl->n_tags; ++i) {
        php_printf("tag (%ld,%ld):%s",
            tpl->tags[i].pos_begin,
            tpl->tags[i].pos_end,
            tpl->tags[i].lexem
        );
        if (BLITZ_IS_METHOD(tpl->tags[i].type)) {
            php_printf(", ARGS(%d) ",tpl->tags[i].n_args);
            for (j=0;j<tpl->tags[i].n_args;++j) {
                php_printf(", %s (%d)",tpl->tags[i].args[j].name,tpl->tags[i].args[j].type);
            }       
        }
        php_printf("\n");
    }

    return;
}

# define REALLOC_POS_IF_EXCEEDS                                                 \
    if (i >= alloc_size) {                                              		\
        alloc_size = alloc_size << 1;                                           \
        *pos = erealloc(*pos,alloc_size*sizeof(**pos));							\
    }                                                                           \

/*******************************************************************************/
inline static void blitz_bm_search (														
/*******************************************************************************/
    uchar *haystack, 
    ulong haystack_length,
	uchar *needle,
	uint needle_len,
    uint *n_found,
    ulong **pos, 
    ulong pos_alloc_size TSRMLS_DC) {
/*******************************************************************************/
    register ulong  i=0, j=0, k=0, shift=0, cmp=0;
    register ulong  haystack_len=haystack_length;
    register ulong alloc_size = pos_alloc_size;
    ulong           bmBc[256];

    if(haystack_len < (ulong)needle_len) {
        php_error_docref(NULL TSRMLS_CC, E_ERROR, 
            "length of haystack(%d) is less than needle length(%d)\n", haystack_len, needle_len
        );
        return;
    }

    for(i=0; i < 256; ++i) bmBc[i] = needle_len;
    for(i=0; i < needle_len - 1; ++i) bmBc[needle[i]] = needle_len - 1 - i;

    shift = bmBc[needle[needle_len - 1]];
    bmBc[needle[needle_len - 1]] = 0;
    memset(haystack + haystack_len, needle[needle_len - 1], needle_len);

    j = 0;
    i = 0;
    while(j < haystack_len) {
        k = bmBc[haystack[j + needle_len - 1]];
        while(k != 0) {
            j += k; k = bmBc[haystack[j + needle_len - 1]];
            j += k; k = bmBc[haystack[j + needle_len - 1]];
            j += k; k = bmBc[haystack[j + needle_len - 1]];
        }
        if(j < haystack_len) {
            for(cmp = 0; cmp<needle_len; ++cmp)
                if(needle[cmp] != haystack[j+cmp]) break;
            if(cmp == needle_len) {
                REALLOC_POS_IF_EXCEEDS;
                *(*pos+i) = j;
                ++i;
            }
        }
        j += shift;
    }
    *n_found = i;
}

# define REALLOC_ARG_IF_EXCEEDS                                                \
    if (arg_id >= n_arg_alloc) {                                               \
        n_arg_alloc = n_arg_alloc << 1;                                        \
        tag->args = (call_arg*)                                                \
            erealloc(tag->args,n_arg_alloc*sizeof(call_arg));                  \
    }  

/******************************************************************************/
inline static void blitz_parse_call (
/******************************************************************************/
    uchar *text, 
    uint len_text, 
    tpl_parts_struct *tag, 
    uint *true_lexem_len,
    uchar *error TSRMLS_DC) {
/******************************************************************************/
    uchar *c = text;
    uchar *p = NULL;
    uchar symb = 0, i_symb = 0;
    uchar state = BLITZ_CALL_STATE_ERROR;
    uchar ok = 0;
    register uint pos = 0, i_pos = 0, i_len = 0;
    uchar was_escaped;
    uchar main_token[1024], token[1024];
    uchar n_arg_alloc = 0;
    register uchar i_type = 0;
    uchar arg_id = 0;
    call_arg *i_arg = NULL;

    // init tag
    tag->n_args = 0;
    tag->args = NULL;
    tag->lexem = NULL;

    *true_lexem_len = 0; // used for parameters only


    BLITZ_SKIP_BLANK(c,i_pos,pos);
    p = main_token;
    // parameter or method?
    if (*c == BLITZ_TAG_VAR_PREFIX) { // scan parameter
        ++c; ++pos; i_pos=0;
        BLITZ_SCAN_ALNUM(c,p,i_pos,i_symb);
        pos+=i_pos;
        if (i_pos!=0) {
            tag->lexem = estrndup(main_token,i_pos);
            *true_lexem_len = i_pos;
            tag->type = BLITZ_TYPE_VAR;
            state = BLITZ_CALL_STATE_FINISHED;
        }
    } else if(BLITZ_IS_ALPHA(*c)) { // scan function
        BLITZ_SCAN_ALNUM(c,p,i_pos,i_symb);
        if(i_pos!=0) {
            tag->lexem = estrndup(main_token,i_pos);
            tag->type = BLITZ_TYPE_METHOD;

            if(*c == '(') { // has arguments
                ++c; ++pos;
                ok = 1;
                state = BLITZ_CALL_STATE_NEXT_ARG;
                // alocate memory for arguments
                tag->args = (call_arg*)
					emalloc(BLITZ_CALL_ALLOC_ARG_INIT*sizeof(call_arg));
                tag->n_args = 0;
                n_arg_alloc = BLITZ_CALL_ALLOC_ARG_INIT;

				// predefined method?
				if (0 == strcmp(main_token,BLITZ_METHOD_IF_S)) {
                    tag->type = tag->type | BLITZ_METHOD_IF;
				} else if (0 == strcmp(main_token,BLITZ_METHOD_INCLUDE_S)) {
					tag->type = tag->type | BLITZ_METHOD_INCLUDE;
				}

            } else {
                state = BLITZ_CALL_STATE_FINISHED;
            }
        }
        while((symb = *c) && ok) {
            switch(state) {
                case BLITZ_CALL_STATE_NEXT_ARG:
                    BLITZ_SKIP_BLANK(c,i_pos,pos);
                    symb = *c;
                    i_len = i_pos = 0;
                    p = token;
                    ok = 0;
                    i_type = 0;
                    if (symb == BLITZ_TAG_VAR_PREFIX) {
                        ++c; ++pos;
                        BLITZ_SCAN_ALNUM(c,p,i_pos,i_symb);
                        if (i_pos!=0) ok = 1;
                        i_type = BLITZ_ARG_TYPE_VAR;
                        i_len = i_pos;
                    } else if (symb == '\'') {
                        ++c; ++pos;
                        BLITZ_SCAN_SINGLE_QUOTED(c,p,i_pos,i_len,ok);
                        i_type = BLITZ_ARG_TYPE_STR;
                    } else if (symb == '\"') {
                        ++c; ++pos;
                        BLITZ_SCAN_DOUBLE_QUOTED(c,p,i_pos,i_len,ok);
                        i_type = BLITZ_ARG_TYPE_STR;
                    } else if (BLITZ_IS_NUMBER(symb)) {
                        BLITZ_SCAN_NUMBER(c,p,i_pos,i_symb);
                        i_type = BLITZ_ARG_TYPE_NUM;
                        i_len = i_pos;
                        if (i_pos!=0) ok = 1;
                    }
                    if (ok) {
                        state = BLITZ_CALL_STATE_HAS_NEXT;
                        pos+=i_pos;
                        REALLOC_ARG_IF_EXCEEDS;
                        i_arg = tag->args + arg_id;
                        i_arg->name = estrndup(token,i_len);
                        i_arg->len = i_len;
                        i_arg->type = i_type;
                        ++arg_id;
                        tag->n_args = arg_id;
                    } else {
                        state = BLITZ_CALL_STATE_ERROR;
                    }
                    break;
                case BLITZ_CALL_STATE_HAS_NEXT:
                    BLITZ_SKIP_BLANK(c,i_pos,pos);
                    symb = *c;
                    if (symb == ',') {
                        state = BLITZ_CALL_STATE_NEXT_ARG;
                        ++c; ++pos;
                    } else if (symb == ')') {
                        state = BLITZ_CALL_STATE_FINISHED;
                        ++c; ++pos;
                    } else {
                        state = BLITZ_CALL_STATE_ERROR;
                    }
                    break;
                case BLITZ_CALL_STATE_FINISHED:
                    ok = 0;
                    break;
                default:
                    ok = 0;
                    break;
            }
        }
    }


    if(state != BLITZ_CALL_STATE_FINISHED) {
        *error = 1;
    } else if ((tag->type & BLITZ_METHOD_IF) && (tag->n_args<2 || tag->n_args>3)) {
        *error = 1;
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "wrong <if> syntax, only 2 or 3 arguments allowed"
        );
    } else if ((tag->type & BLITZ_METHOD_INCLUDE) && (tag->n_args!=1)) {
       *error = 1;
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "wrong <inlcude> syntax, only 1 argument allowed"
        );

    }
}

/******************************************************************************/
inline static int blitz_analyse (blitz_tpl *tpl TSRMLS_DC) {
/******************************************************************************/
    uint n_open = 0, n_close = 0, n_parts = 0;
    uint i = 0, j = 0, k = 0;
    ulong *pos_open = NULL;
    ulong *pos_close = NULL;
    ulong *i_pos_open = NULL;
    ulong *i_pos_close = NULL;
    ulong current_open = 0, current_close, next_open = 0; 

    uint l_open_tag = tpl->config->l_open_tag;
    uint l_close_tag = tpl->config->l_close_tag;
	uint lexem_len = 0;
    uint true_lexem_len = 0;
    uint tag_type = 0;
    uint shift = 0;
    tpl_parts_struct *i_parts = NULL;
    uchar *body = NULL;
    uchar *plex = NULL;
    ulong pos_open_size = 0, pos_close_size = 0;
    uchar i_error = 0;

    pos_open = (ulong*)ecalloc(BLITZ_ALLOC_TAGS_STEP,sizeof(ulong));
    pos_open_size = BLITZ_ALLOC_TAGS_STEP;
    pos_close = (ulong*)ecalloc(BLITZ_ALLOC_TAGS_STEP,sizeof(ulong));
    pos_close_size = BLITZ_ALLOC_TAGS_STEP;

    if((pos_open == NULL) || (pos_close == NULL)) {
        return -1;
    }

    // find open tag positions
    body = tpl->body;
    blitz_bm_search (
		body, tpl->body_len,
		tpl->config->open_tag, l_open_tag, 
		&n_open, &pos_open, pos_open_size TSRMLS_CC
	);

    // find close tag positions
    body = tpl->body;
    blitz_bm_search (
		body, tpl->body_len,
		tpl->config->close_tag, l_close_tag,
        &n_close, &pos_close, pos_close_size TSRMLS_CC
	);

    // allocate memory for tags
    tpl->tags = (tpl_parts_struct*)emalloc(
        n_open*sizeof(tpl_parts_struct)
    );

    if (!tpl->tags) {
        php_error_docref(NULL TSRMLS_CC, E_ERROR, 
            "unable to allocate memory for %ld tags", n_open
        );
        efree(pos_open);
        efree(pos_close);
        return 0;
    }

    // set correct pairs even if template has wrong grammar
    i = 0;
    i_pos_open = pos_open;
    i_pos_close = pos_close;
    n_parts = 0;

    while (i<n_open) {
        current_open = *i_pos_open;
        // mmm... we won't add check here, but we need +1 in allocated buffer then
        next_open = (i<(n_open-1)) ? *(i_pos_open+1) : 0;
        current_close = *i_pos_close;
        if (current_close<current_open) {
            ++i_pos_close; // current close tag goes before open !ADD ERROR_LOG!
            continue;
        } else if (next_open && next_open<current_close) {
            ++i_pos_open;  // current close tag goes before next open  !ADD ERROR_LOG!
        } else { 
            shift = current_open + l_open_tag;
            lexem_len = current_close - shift;
            if(lexem_len>BLITZ_MAX_LEXEM_LEN) {
                php_error_docref(NULL TSRMLS_CC, E_WARNING, 
                    "lexem is too long in tag started at pos: %ld", current_open
                );
            } else if (lexem_len<=0) {
                php_error_docref(NULL TSRMLS_CC, E_WARNING, 
                    "zero length lexem in tag started at pos: %ld", current_open
                );
            } else { // OK: parse 
                i_error = 0;
                plex = tpl->body + shift;
                j = lexem_len;
                true_lexem_len = 0;
                blitz_parse_call(plex,j,&tpl->tags[n_parts],&true_lexem_len,&i_error TSRMLS_CC);
                if (i_error) {
                    tpl->tags[n_parts].has_error = 1;
                    php_error_docref(NULL TSRMLS_CC, E_WARNING, 
                        "syntax error in tag started at pos: %ld", current_open
                    );
                } else { 
                    tpl->tags[n_parts].has_error = 0;
                } 

                tpl->tags[n_parts].pos_begin = current_open;
                tpl->tags[n_parts].pos_end = current_close;
                ++n_parts;
            }
            ++i_pos_open;
            ++i_pos_close;
        }
        ++i;
    }

    tpl->n_tags = n_parts;
    efree(pos_open);
    efree(pos_close);

    return 1;
}

#define TIMER(ts,tz,tdata,tnum)                         \
    gettimeofday(&(ts),&(tz));                          \
    tdata[tnum] = 1000000*ts.tv_sec + ts.tv_usec;       \
    tnum++;

#define TIMER_ADD(ts,tz,tdata,tnum)                     \
    gettimeofday(&(ts),&(tz));                          \
    tdata[tnum] += 1000000*ts.tv_sec + ts.tv_usec;      \

//#define TIMER_DEBUG_INIT
//#define TIMER_DEBUG_PARSE
//#define TIMER_DEBUG_SET
//#define TIMER_DEBUG_EXEC

/*****************************************************************************/
inline static int blitz_exec_predefined_method (
/*****************************************************************************/
    tpl_parts_struct *tag,
    HashTable *ht_data,
    zval *id,
    uchar **result,
    uchar **p_result,
    ulong *result_len,
    ulong *result_alloc_len TSRMLS_DC) {
/*****************************************************************************/
    zval *retval = NULL, *z_method = NULL, **z = NULL;
    uint method_res = 0;
    ulong buf_len = 0, new_len = 0;
    MAKE_STD_ZVAL(retval);
    ZVAL_FALSE(retval);

	if (tag->type & BLITZ_METHOD_IF) {
		char not_empty = 0;
		char i_arg = 0;
		BLITZ_ARG_NOT_EMPTY(tag->args[0],ht_data,not_empty);

		if(not_empty) { // first argument is not empty
			i_arg = 1;
		} else if(tag->n_args == 3) { // empty && if has 3 arguments
			i_arg = 2;
		} else { // empty && if has only 2 arguments
			return 1;
		}

		if(tag->args[i_arg].type & BLITZ_ARG_TYPE_VAR) { // argument is parameter
			if (SUCCESS == zend_hash_find(ht_data,tag->args[i_arg].name,1+tag->args[i_arg].len,(void**)&z)) {
                convert_to_string_ex(z);
				buf_len = Z_STRLEN_PP(z);
				BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,*p_result);
				*p_result = (char*)memcpy(*p_result,Z_STRVAL_PP(z),buf_len);
				*result_len += buf_len;
				p_result+=*result_len;
			}
		} else { // argument is string or number
			buf_len = tag->args[i_arg].len;
			BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,*p_result);
			*p_result = (char*)memcpy(*p_result,tag->args[i_arg].name,buf_len);
			*result_len += buf_len;
			p_result+=*result_len;
		}
	} else if (tag->type & BLITZ_METHOD_INCLUDE) {
        /* this strategy is not optimized for those cases when a complex template 
        is included more than once. all the times it will be opened and analyzed */
        char *filename = tag->args[0].name;
        uint filename_len = tag->args[0].len;
        char *buf = NULL;
        uchar *inner_result = NULL;
        ulong inner_result_len = 0;
        ulong buf_len = 0;
        php_stream *stream = NULL;
        FILE *f = NULL;
        int get_len = 0;
        blitz_tpl *tpl = NULL;

		/* initialize template */
		if(!(tpl = blitz_init_tpl(filename, ht_data TSRMLS_CC))) {
			blitz_free_tpl(tpl);    
			return 0;
		}

		/* analyze template */
		if(!blitz_analyse(tpl TSRMLS_CC)) {
			blitz_free_tpl(tpl);
			return 0;
		}

        // parse
        if(blitz_exec_template(tpl,id,&inner_result,&inner_result_len TSRMLS_CC)) {
            BLITZ_REALLOC_RESULT(inner_result_len,new_len,*result_len,*result_alloc_len,*result,*p_result);
            *p_result = (char*)memcpy(*p_result,inner_result,inner_result_len);
            *result_len += inner_result_len;
			p_result+=*result_len;
        }
        blitz_free_tpl(tpl);
	} 

    return 1;
}

/*****************************************************************************/
inline static int blitz_exec_user_method (
/*****************************************************************************/
    tpl_parts_struct *tag,
    HashTable *ht_data,
    zval  *obj,
    uchar **result,
    uchar **p_result,
    ulong *result_len,
    ulong *result_alloc_len TSRMLS_DC) {
/*****************************************************************************/
    zval *retval = NULL, *z_method = NULL, **z_param = NULL;
    uint method_res = 0;
    ulong buf_len = 0, new_len = 0;
    zval ***args = NULL;
    zval **pargs = NULL;
    uint i = 0;
    call_arg *i_arg = NULL;
    char i_arg_type = 0;
   
    MAKE_STD_ZVAL(retval);
    ZVAL_FALSE(retval);
    MAKE_STD_ZVAL(z_method);
    ZVAL_STRING(z_method,tag->lexem,1);

    // prepare arguments if needed
    if(tag->n_args>0 && tag->args) {
        // dirty trick to pass arguments
        // pargs are zval ** with parameter data
        // args just point to pargs
        args = emalloc(tag->n_args*sizeof(zval));
        pargs = emalloc(tag->n_args*sizeof(zval));
        for(i=0; i<tag->n_args; i++) {
            args[i] = NULL;
            MAKE_STD_ZVAL(pargs[i]);
            i_arg  = &(tag->args[i]);
            i_arg_type = i_arg->type;
            if (i_arg_type == BLITZ_ARG_TYPE_VAR) {
                // fetch variable and bind to the argument
				if (SUCCESS == zend_hash_find(ht_data,tag->args[i].name,1+tag->args[i].len,(void**)&z_param)) {
                    args[i] = z_param;
				} else {
                    ZVAL_NULL(pargs[i]);
                }
            } else if (i_arg_type == BLITZ_ARG_TYPE_NUM) {
                ZVAL_LONG(pargs[i],atol(i_arg->name));
            } else {
				ZVAL_STRING(pargs[i],i_arg->name,1);
            }
            if(!args[i]) args[i] = & pargs[i];
        }
    }

	// call object method
    method_res = call_user_function_ex(
        CG(function_table), &obj,
        z_method, &retval, tag->n_args, args, 1, NULL TSRMLS_CC
    );

    if (method_res != FAILURE) {
        buf_len = Z_STRLEN_P(retval);
        BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,*p_result);
        *p_result = (char*)memcpy(*p_result,Z_STRVAL_P(retval), buf_len);
        *result_len += buf_len;
		p_result+=*result_len;
    }

    zval_dtor(z_method);
    if (retval) {
        zval_dtor(retval);
    }
	if(pargs) {
         for(i=0; i<tag->n_args; i++) {
             zval_dtor(pargs[i]);
         }
         efree(args);
         efree(pargs);
    }

    return 1;
}

/*****************************************************************************/
static int blitz_exec_template (
/*****************************************************************************/
    blitz_tpl *tpl,
    zval *id,
    uchar **result,
    ulong *result_len TSRMLS_DC) {
/*****************************************************************************/
    zval **desc = NULL;
    uint i = 0, j = 0;
    uchar *p_body = NULL, *p_result = NULL;
    ulong result_alloc_len = 0, new_len = 0;
    ulong shift = 0, buf_len = 0, last_close = 0, current_open = 0;
    ulong l_open_tag = 0, l_close_tag = 0;
    zval **zparam;
    tpl_parts_struct *tag;

    // quick return if there's no tags
    if(0 == tpl->n_tags) {
        *result = tpl->body; // won't copy
        *result_len += tpl->body_len;
        return 2; // will not call efree on result
    }

    // build result, initial alloc of twice bigger than body
    *result_len = 0;
    result_alloc_len = 2*tpl->body_len; 
    *result = (char*)ecalloc(result_alloc_len,sizeof(char));
    if(!*result) {
        return 0;
    }

    p_result = *result;
    shift = 0;
    l_open_tag = tpl->config->l_open_tag;
    l_close_tag = tpl->config->l_close_tag;
    last_close = *result_len;
    p_body = tpl->body;

    for(i=0; i<tpl->n_tags; ++i) {
        tag = tpl->tags + i;
        current_open = tag->pos_begin;
        buf_len = current_open - last_close;
        // between tags
        if (buf_len>0) {
            BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,result_alloc_len,*result,p_result);
            p_result = (char*)memcpy(p_result, p_body + last_close, buf_len); 
			*result_len += buf_len;
			p_result+=*result_len;
        }

        if (tag->lexem && !tag->has_error) {
        	if (BLITZ_IS_VAR(tag->type)) { 
				if(SUCCESS == zend_hash_find(
						tpl->ht_data,tag->lexem,
						strlen(tag->lexem)+1,
						(void**)&zparam
					)
				)
			    {
                    convert_to_string_ex(zparam);
			        buf_len = Z_STRLEN_PP(zparam);
                    BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,result_alloc_len,*result,p_result);
	                p_result = (char*)memcpy(p_result,Z_STRVAL_PP(zparam), buf_len);
	                *result_len += buf_len;
					p_result+=*result_len;
	            }
            }
			else if (BLITZ_IS_METHOD(tag->type)) {
                if(BLITZ_IS_PREDEF_METHOD(tag->type)) {
                    blitz_exec_predefined_method(
                        tag,tpl->ht_data,id,result,&p_result,result_len,&result_alloc_len TSRMLS_CC
                    );
                } else {
                    blitz_exec_user_method(
                        tag,tpl->ht_data,id,result,&p_result,result_len,&result_alloc_len TSRMLS_CC
                    );
                }
			}
        } 
        last_close = tag->pos_end + l_close_tag;    
    }

    buf_len = tpl->body_len - last_close;
    if(buf_len>0) {
        BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,result_alloc_len,*result,p_result);
        p_result = (char*)memcpy(p_result, tpl->body + last_close, buf_len);
        *result_len += buf_len;
		p_result+=*result_len;
    }

    return 1;
}

/*****************************************************************************/
/* PHP_INTEFACE                                                              */
/*****************************************************************************/

/* {{{ blitz_init(filename) */
/*****************************************************************************/
PHP_FUNCTION(blitz_init) {
/*****************************************************************************/
    blitz_tpl *tpl = NULL;
    uint filename_len;
    char *filename;

    int get_len = 0;
    zval *new_object = NULL;
	int ret = 0;

    if(FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"s",&filename,&filename_len)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    /* initialize template */
    if(!(tpl = blitz_init_tpl(filename, NULL TSRMLS_CC))) {
        blitz_free_tpl(tpl);    
        RETURN_FALSE;
    }

    /* analyze template */
    if(!blitz_analyse(tpl TSRMLS_CC)) {
        blitz_free_tpl(tpl);
        RETURN_FALSE;
    }

    MAKE_STD_ZVAL(new_object);

    if(object_init(new_object) != SUCCESS)
    {
        // do error handling here
        RETURN_FALSE;
    }

    ret = zend_list_insert(tpl, le_blitz);
//    object_init_ex(getThis(), &blitz_class_entry);
    add_property_resource(getThis(), "tpl", ret);
    zend_list_addref(ret);

}
/* }}} */

/* {{{ blitz_dump_set() */
/*****************************************************************************/
PHP_FUNCTION(blitz_dump_set) {
/*****************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }
    
    if (zend_hash_find(Z_OBJPROP_P(id), "tpl", sizeof("tpl"), (void **)&desc) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, 
			"cannot find template descriptor (object->tpl resource)"
		);
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "Blitz template handle", le_blitz);
//    php_blitz_dump_data(tpl->ht_data);

    RETURN_TRUE;
}
/* }}} */


/* {{{ blitz_set(zend_array(tag_key=>tag_val)) */
/*****************************************************************************/
PHP_FUNCTION(blitz_set) {
/*****************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;
    zval *input_arr, **elem;
    HashTable *input_ht = NULL;
    HashPosition pointer;
    char *key = NULL;
    int key_len = 0;
    long index = 0;
    int r = 0;

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (zend_hash_find(Z_OBJPROP_P(id), "tpl", sizeof("tpl"), (void **)&desc) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "cannot find template descriptor (object->tpl resource)"
        );
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "Blitz template handle", le_blitz);

    if(FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"a",&input_arr)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    input_ht = Z_ARRVAL_P(input_arr);
    zend_hash_internal_pointer_reset_ex(tpl->ht_data, &pointer);
    zend_hash_internal_pointer_reset_ex(input_ht, &pointer);

    while(zend_hash_get_current_data_ex(input_ht, (void**) &elem, &pointer) == SUCCESS) {
        if(zend_hash_get_current_key_ex(input_ht, &key, &key_len, &index, 0, &pointer) != HASH_KEY_IS_STRING) {
            // 2do: log warning or long-to-char convert from index
            zend_hash_move_forward_ex(input_ht, &pointer);
            continue;
        } 

        if(key) {
            if (
				Z_TYPE_PP(elem) == IS_STRING 
				|| Z_TYPE_PP(elem) == IS_LONG 
				|| Z_TYPE_PP(elem) == IS_DOUBLE
				|| Z_TYPE_PP(elem) == IS_NULL) 
            {
                zval *temp;
                ALLOC_INIT_ZVAL(temp);
                *temp = **elem;
                //convert_to_string_ex(&temp);
                zval_copy_ctor(temp);

                r = zend_hash_update(
                    tpl->ht_data,
                    key,
                    key_len,
                    (void*)&temp, sizeof(zval *), NULL 
                );
            } else {
                php_error_docref(NULL TSRMLS_CC, E_WARNING,
                    "array element in blitz_set is not scalar"
                );            
            }
        }
        zend_hash_move_forward_ex(input_ht, &pointer);
    }

    RETURN_TRUE;
}
/* }}} */

/* {{{ blitz_parse() */
/*****************************************************************************/
PHP_FUNCTION(blitz_parse) {
/*****************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl;
    uchar *result = NULL;
    ulong result_len = 0, result_alloc_len = 0;
    zval **zparam = NULL;
    zval *input_arr = NULL;
    tpl_parts_struct *tag = NULL;
    zval      **src_entry = NULL;
    char      *string_key = NULL;
    uint      string_key_len = 0;
    ulong     num_key =0;
    HashPosition pos;
    HashTable *input_ht = NULL;
	char exec_status = 0;

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (zend_hash_find(Z_OBJPROP_P(id), "tpl", sizeof("tpl"), (void **)&desc) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "cannot find template descriptor (object::tpl resource)"
        );
        RETURN_FALSE;
    }

    if(FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"|a",&input_arr)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    } 

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "Blitz template handle", le_blitz);
    if(!tpl) {
        RETURN_FALSE;
    }

    // merge data: input array => template parameters

    if(input_arr) {
		input_ht = Z_ARRVAL_P(input_arr);
		zend_hash_internal_pointer_reset_ex(input_ht, &pos);
		while (zend_hash_get_current_data_ex(input_ht, (void **)&src_entry, &pos) == SUCCESS) {
			if(HASH_KEY_IS_STRING == zend_hash_get_current_key_ex(
				input_ht, &string_key, &string_key_len, &num_key, 0, &pos)) 
			{
				++(*src_entry)->refcount;
				zend_hash_update(tpl->ht_data, string_key, string_key_len,
				    src_entry, sizeof(zval *), NULL);
			}
			zend_hash_move_forward_ex(input_ht, &pos);
		}
    }

    // execute call
    exec_status = blitz_exec_template(tpl,id,&result,&result_len TSRMLS_CC);

    if(exec_status) {
        ZVAL_STRINGL(return_value,result,result_len,1);
        if(exec_status == 1) efree(result);
    } else {
        ZVAL_STRINGL(return_value,"",0,1);
    }

    return;
}

/* {{{ blitz_incude(filename,params) */
/*****************************************************************************/
PHP_FUNCTION(blitz_include) {
/*****************************************************************************/
    blitz_tpl *tpl = NULL, *itpl = NULL;
    FILE *f = NULL;
    uint filename_len = 0, get_len = 0;
    char *filename = NULL;
    zval **desc = NULL;
    zval *id = NULL;
    uchar *result = NULL;
    ulong result_len = 0;
    zval *input_arr = NULL;
    zval      **src_entry = NULL,
              **dest_entry = NULL;
    char      *string_key = NULL;
    uint      string_key_len = 0;
    ulong     num_key = 0;
    HashPosition pos;
    HashTable *input_ht = NULL;
	ulong itpl_idx = 0;
    zval *temp = NULL;
    php_stream *stream = NULL;

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"s|a",&filename,&filename_len,&input_arr)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (!filename) RETURN_FALSE;

    if (zend_hash_find(Z_OBJPROP_P(id), "tpl", sizeof("tpl"), (void **)&desc) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "cannot find template descriptor (object::tpl resource)"
        );
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "Blitz template handle", le_blitz);
    if (!tpl) {
        RETURN_FALSE;
    }

    // try to find already parsed tpl index
    if(SUCCESS == zend_hash_find(tpl->ht_tpl_name,filename,filename_len,(void **)&desc)) {
        itpl = tpl->itpl_list[Z_LVAL_PP(desc)];
    } else { // read and init template
        /* initialize template */
        if(!(itpl = blitz_init_tpl(filename, tpl->ht_data TSRMLS_CC))) {
            blitz_free_tpl(itpl);    
            RETURN_FALSE;
        }

        /* analyze template */
        if(!blitz_analyse(itpl TSRMLS_CC)) {
            blitz_free_tpl(itpl);
            RETURN_FALSE;
        }

        // realloc list if needed
        if (tpl->itpl_list_len >= tpl->itpl_list_alloc-1) {
            tpl->itpl_list = (blitz_tpl**)erealloc(
                tpl->itpl_list,(tpl->itpl_list_alloc<<1)*sizeof(blitz_tpl*)
            ); 
            if(tpl->itpl_list) {
                tpl->itpl_list_alloc <<= 1;
            } else {
                php_error_docref(NULL TSRMLS_CC, E_WARNING,
                    "cannot  realloc memory for inner-template list"
                );
                blitz_free_tpl(itpl);
                RETURN_FALSE;
            }
        }

        // save template index values
        itpl_idx = tpl->itpl_list_len;
        tpl->itpl_list[itpl_idx] = itpl;
        MAKE_STD_ZVAL(temp);
        ZVAL_LONG(temp, itpl_idx);
        zend_hash_update(tpl->ht_tpl_name, filename, filename_len,
            &temp, sizeof(zval *), NULL);
        tpl->itpl_list_len++;
    }

    // merge data: input array => template parameters
    if(input_arr) {
        input_ht = Z_ARRVAL_P(input_arr);
        zend_hash_internal_pointer_reset_ex(input_ht, &pos);
        while (zend_hash_get_current_data_ex(input_ht, (void **)&src_entry, &pos) == SUCCESS) {
            if(HASH_KEY_IS_STRING == zend_hash_get_current_key_ex(
                input_ht, &string_key, &string_key_len, &num_key, 0, &pos))
            {
                (*src_entry)->refcount++;
                zend_hash_update(itpl->ht_data, string_key, string_key_len,
                    src_entry, sizeof(zval *), NULL);
            }
            zend_hash_move_forward_ex(input_ht, &pos);
        }
    }

    // execute call
    if(blitz_exec_template(itpl,id,&result,&result_len TSRMLS_CC)) {
        // return
        ZVAL_STRINGL(return_value,result,result_len,1);
        efree(result);
    } else {
        ZVAL_STRINGL(return_value,"",0,1);
    }
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
