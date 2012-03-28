/*
  +----------------------------------------------------------------------+
  | Authors: Alexey Rybak <raa@phpclub.net>,                             |
  |          template analyzing is based on php_templates code by        | 
  |          Maxim Poltarak (http://php-templates.sf.net)                | 
  +----------------------------------------------------------------------+
*/

/* $Id: blitz.c,v 0.2 2005/09/18 19:45:35 fisher Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "php_blitz.h"

/*
// shm-related functions
#ifndef PHP_WIN32
#include <sys/ipc.h>
#include <sys/shm.h>
#else
#include "tsrm_win32.h"
#endif
// for debugs
#include <sys/time.h>
*/

ZEND_DECLARE_MODULE_GLOBALS(blitz)

// some declaration - for recursive code
static int blitz_exec_template(blitz_tpl *tpl, zval *id, uchar **result, ulong *result_len);

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
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"blitz",
	blitz_functions,
	PHP_MINIT(blitz),
	PHP_MSHUTDOWN(blitz),
	PHP_RINIT(blitz),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(blitz),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(blitz),
#if ZEND_MODULE_API_NO >= 20010901
	"$Revision: 0.2 $", 
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_BLITZ
ZEND_GET_MODULE(blitz)
#endif

/* {{{ PHP_INI */
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("blitz.var_prefix", BLITZ_TAG_VAR_PREFIX_S, PHP_INI_ALL, 
#ifdef BLITZ_FOR_PHP_4
		OnUpdateInt, var_prefix, zend_blitz_globals, blitz_globals)
#else
        OnUpdateLong, var_prefix, zend_blitz_globals, blitz_globals)
#endif
    STD_PHP_INI_ENTRY("blitz.tag_open", BLITZ_TAG_OPEN_DEFAULT, PHP_INI_ALL, 
		OnUpdateString, tag_open, zend_blitz_globals, blitz_globals)
    STD_PHP_INI_ENTRY("blitz.tag_close", BLITZ_TAG_CLOSE_DEFAULT, PHP_INI_ALL, 
		OnUpdateString, tag_close, zend_blitz_globals, blitz_globals)
PHP_INI_END()
/* }}} */

/*  {{{ blitz_init_tpl(char *body, ulong body_len) */
/******************************************************************************/
inline blitz_tpl *blitz_init_tpl(
/******************************************************************************/
    char *body, 
    ulong body_len, 
    HashTable *ht TSRMLS_DC) {
/******************************************************************************/
    int add_buffer = 0;

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

    // search algorithm requires lager buffer: body_len + add_buffer
    add_buffer = MAX(tpl->config->l_open_tag,tpl->config->l_close_tag);
    tpl->body = (char*)ecalloc(body_len+add_buffer,sizeof(char));

    // 2DO: copying here is not good idea, reading directly 
    // from file to tpl->body could be much better
    if (!tpl->body || NULL == memcpy(tpl->body,body,body_len)) {
        php_error_docref(
            NULL TSRMLS_CC, E_ERROR,
            "unable to allocate or fill memory for blitz template body"
        );
        return NULL;
    }
    tpl->body_len = body_len;

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
        // here we "inherit" params from opener: the most correct way to do that
        // is to follow a "copy-on-write" strategy. unless "inner" code doesn't
        // modify template params - we use just a pointer-copy. otherwise we
        // make a full data copy. but here we just make a copy and mark template with 
        // ht_data_is_others = 1 not to free this object.
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
	php_info_print_table_row(2, "Revision", "$Revision: 0.2 $");
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

/*****************************************************************************/
static void php_blitz_dump_data(HashTable *data) {
/*****************************************************************************/
    HashPosition pointer;
    int array_count;
    zval **elem;
    char *key;
    int key_len = 0;
    long index = 0;    

    zend_hash_internal_pointer_reset_ex(data, &pointer);
    php_printf("php_blitz_dump_data:\n");
    while(zend_hash_get_current_data_ex(data, (void**) &elem, &pointer) == SUCCESS) {
        if(zend_hash_get_current_key_ex(data, &key, &key_len, &index, 0, &pointer) == HASH_KEY_IS_STRING) {
            PHPWRITE(key,key_len);
        } else {
            php_printf("%ld",key);
        }
        php_printf("=><");
        convert_to_string_ex(elem); 
        PHPWRITE(Z_STRVAL_PP(elem), Z_STRLEN_PP(elem));
        php_printf(">\n");
        zend_hash_move_forward_ex(data, &pointer);
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
    ulong pos_alloc_size) {
/*******************************************************************************/
    register ulong  i=0, j=0, k=0, shift=0, cmp=0;
    register ulong  haystack_len=haystack_length;
    register ulong alloc_size = pos_alloc_size;
    ulong           bmBc[256];

    if(haystack_len < (long)needle_len) {
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
    uchar *error) {
/******************************************************************************/
    uchar *c = text;
    uchar *p = NULL;
    uchar symb = 0, i_symb = 0;
    uchar state = BLITZ_CALL_STATE_ERROR;
    uchar ok = 0;
    register uint pos, i_pos, i_len;
    register uchar was_escaped;
    uchar main_token[1024], token[1024];
    uchar n_arg_alloc = 0;
    uchar i_type = 0;
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
inline static int blitz_analyse (blitz_tpl *tpl) {
/******************************************************************************/
    uint n_open = 0, n_close = 0, n_parts = 0;
    register uint i = 0, j = 0, k = 0;
    ulong *pos_open = NULL;
    ulong *pos_close = NULL;
    ulong *i_pos_open = NULL;
    ulong *i_pos_close = NULL;
    register ulong current_open = 0, current_close, next_open = 0; 

    uint l_open_tag = tpl->config->l_open_tag;
    uint l_close_tag = tpl->config->l_close_tag;
    uchar lexem_buf[BLITZ_MAX_LEXEM_LEN];
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
		&n_open, &pos_open, pos_open_size
	);

    // find close tag positions
    body = tpl->body;
    blitz_bm_search (
		body, tpl->body_len,
		tpl->config->close_tag, l_close_tag,
        &n_close, &pos_close, pos_close_size
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
                blitz_parse_call(plex,j,&tpl->tags[n_parts],&true_lexem_len,&i_error);
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
    ulong *result_alloc_len) {
/*****************************************************************************/
    zval *retval = NULL, *z_method = NULL;
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
			zval **z;
			if (SUCCESS == zend_hash_find(ht_data,tag->args[i_arg].name,1+tag->args[i_arg].len,(void**)&z)) {
                convert_to_string_ex(z);
				buf_len = Z_STRLEN_PP(z);
				BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,*p_result);
				*p_result = (char*)mempcpy(*p_result,Z_STRVAL_PP(z),buf_len);
				*result_len += buf_len;
			}
		} else { // argument is string or number
			buf_len = tag->args[i_arg].len;
			BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,*p_result);
			*p_result = (char*)mempcpy(*p_result,tag->args[i_arg].name,buf_len);
			*result_len += buf_len;
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

		if((PG(safe_mode)
			&& !php_checkuid(filename, NULL, CHECKUID_CHECK_FILE_AND_DIR))
			|| php_check_open_basedir(filename TSRMLS_CC))
		{
			return 0;
		}
/*
		stream = php_stream_open_wrapper(
            filename, "rb", IGNORE_PATH|ENFORCE_SAFE_MODE|REPORT_ERRORS, NULL
        );
		if(!stream) { 
			return 0;
		}

		buf_len = php_stream_copy_to_mem(stream, &buf, PHP_STREAM_COPY_ALL, 0);
		php_stream_close(stream);
*/

        if(!(filename && (f = fopen(filename, "rb")))) {
            php_error_docref(NULL TSRMLS_CC, E_ERROR,
                "unable to open file %s", filename
            );
            return 0;
        }

		// read template: probably the fastest way for many small files
		// 2DO: try "manual" fstat+mmap here (not through php_streams)
		buf = (char*)emalloc((BLITZ_INPUT_BUF_SIZE)*sizeof(char));
		buf_len = 0;
		while((get_len = fread(buf+buf_len, sizeof(char), BLITZ_INPUT_BUF_SIZE, f)) > 0) {
			buf_len += get_len;
			buf = (char*)erealloc(buf, (buf_len + BLITZ_INPUT_BUF_SIZE)*sizeof(char));
		}
		fclose(f);
        if(buf_len) buf[buf_len-1] = '\0';

		/* initialize template */
		if(!(tpl = blitz_init_tpl(buf, buf_len, ht_data TSRMLS_CC))) {
			blitz_free_tpl(tpl);    
			efree(buf);
			return 0;
		}

        tpl->name = emalloc((filename_len+1)*sizeof(char));
        if(tpl->name){
            memcpy(tpl->name,filename,filename_len);
            tpl->name[filename_len] = '\0';
        }

		/* analyze template */
		if(!blitz_analyse(tpl)) {
			blitz_free_tpl(tpl);
			efree(buf);
			return 0;
		}
		efree(buf);

        // parse
        if(blitz_exec_template(tpl,id,&inner_result,&inner_result_len)) {
            BLITZ_REALLOC_RESULT(inner_result_len,new_len,*result_len,*result_alloc_len,*result,*p_result);
            *p_result = (char*)mempcpy(*p_result,inner_result,inner_result_len);
            *result_len += inner_result_len;
        }
        blitz_free_tpl(tpl);
	} 

    return 1;
}

/*****************************************************************************/
inline static int blitz_exec_user_method (
/*****************************************************************************/
    tpl_parts_struct *tag,
    zval  *obj,
    uchar **result,
    uchar **p_result,
    ulong *result_len,
    ulong *result_alloc_len) {
/*****************************************************************************/
    zval *retval = NULL, *z_method = NULL;
    uint method_res = 0;
    ulong buf_len = 0, new_len = 0;
    MAKE_STD_ZVAL(retval);
    ZVAL_FALSE(retval);
    MAKE_STD_ZVAL(z_method);
    ZVAL_STRING(z_method,tag->lexem,1);

    method_res = call_user_function_ex(
        CG(function_table), &obj,
        z_method, &retval, 0, 0, 1, NULL TSRMLS_CC
    );


    if (method_res != FAILURE) {
        buf_len = Z_STRLEN_P(retval);
        BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,*p_result);
        *p_result = (char*)mempcpy(*p_result,Z_STRVAL_P(retval), buf_len);
        *result_len += buf_len;
    }

    zval_dtor(z_method);
    if (retval)
        zval_dtor(retval);

}

/*****************************************************************************/
static int blitz_exec_template (
/*****************************************************************************/
    blitz_tpl *tpl,
    zval *id,
    uchar **result,
    ulong *result_len) {
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
        return 1;
    }

    // build result, initial alloc of twice bigger than body
    *result_len = 0;
    result_alloc_len = 2*tpl->body_len; 
    //result_alloc_len = 512; 
    *result = (char*)ecalloc(result_alloc_len,sizeof(char));
    if(!*result) {
        return 0;
    }

    TSRMLS_FETCH();

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
            p_result = (char*)mempcpy(p_result, p_body + last_close, buf_len); 
			*result_len += buf_len;
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
	                p_result = (char*)mempcpy(p_result,Z_STRVAL_PP(zparam), buf_len);
	                *result_len += buf_len;
	            }
            }
			else if (BLITZ_IS_METHOD(tag->type)) {
                if(BLITZ_IS_PREDEF_METHOD(tag->type)) {
                    blitz_exec_predefined_method(
                        tag,tpl->ht_data,id,result,&p_result,result_len,&result_alloc_len
                    );
                } else {
                    blitz_exec_user_method(
                        tag,id,result,&p_result,result_len,&result_alloc_len
                    );
                }
			}
        } 
        last_close = tag->pos_end + l_close_tag;    
    }

    buf_len = tpl->body_len - last_close;
    if(buf_len>0) {
        BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,result_alloc_len,*result,p_result);
        p_result = (char*)mempcpy(p_result, tpl->body + last_close, buf_len);
        *result_len += buf_len;
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
    char *buf = NULL;
    ulong buf_len = 0;
    FILE *f = NULL;
    uint filename_len;
    char *filename;

    php_stream *stream;
    struct blitz_shmop *shmop;
    key_t k;
    int shmid;
    char *shmaddr;
    int get_len = 0;

    if(FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"s",&filename,&filename_len)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    // check security restrictions
    if((PG(safe_mode)
        && !php_checkuid(filename, NULL, CHECKUID_CHECK_FILE_AND_DIR))
        || php_check_open_basedir(filename TSRMLS_CC))
    {
        RETURN_FALSE
    }

/*
    stream = php_stream_open_wrapper(filename, "rb", IGNORE_PATH|ENFORCE_SAFE_MODE|REPORT_ERRORS, NULL);
    if(!stream) { // 2do: log
        RETURN_FALSE;
    }

    buf_len = php_stream_copy_to_mem(stream, &buf, PHP_STREAM_COPY_ALL, 0);
    php_stream_close(stream);
*/

    if(!(filename && (f = fopen(filename, "rb")))) {
        php_error_docref(NULL TSRMLS_CC, E_ERROR,
            "unable to open file %s", filename
        );        
		RETURN_FALSE;
    }

    // read template: probably the fastest way for many small files
    // 2DO: try "manual" fstat+mmap here (not through php_streams)
    buf = (char*)emalloc((BLITZ_INPUT_BUF_SIZE)*sizeof(char));
    buf_len = 0;
    while((get_len = fread(buf+buf_len, sizeof(char), BLITZ_INPUT_BUF_SIZE, f)) > 0) {
        buf_len += get_len;
        buf = (char*)erealloc(buf, (buf_len + BLITZ_INPUT_BUF_SIZE)*sizeof(char));
    }
    fclose(f);
    if(buf_len) buf[buf_len-1] = '\0';

    /* initialize template */
    if(!(tpl = blitz_init_tpl(buf, buf_len, NULL TSRMLS_CC))) {
        blitz_free_tpl(tpl);    
        efree(buf);
        RETURN_FALSE;
    }

    tpl->name = emalloc((filename_len+1)*sizeof(char));
    if(tpl->name){
        memcpy(tpl->name,filename,filename_len);
        tpl->name[filename_len] = '\0';
    }

    /* analyze template */
    if(!blitz_analyse(tpl)) {
        blitz_free_tpl(tpl);
        efree(buf);
        RETURN_FALSE;
    }

    efree(buf);

    /* create new object */
    zval *new_object = NULL;
    MAKE_STD_ZVAL(new_object);

    if(object_init(new_object) != SUCCESS)
    {
        // do error handling here
        RETURN_FALSE;
    }

    int ret = zend_list_insert(tpl, le_blitz);
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
    php_blitz_dump_data(tpl->ht_data);

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
    zval *input_arr, **elem, **zdummy;
    HashTable *input_ht = NULL;
    HashPosition pointer;
    char *key = NULL;
    int key_len = 0;
    long index = 0;

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

                int r = zend_hash_update(
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
    zval      **src_entry,
              **dest_entry;
    char      *string_key;
    uint      string_key_len;
    ulong     num_key;
    HashPosition pos;
    HashTable *input_ht = NULL;

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
    char exec_status = blitz_exec_template(tpl,id,&result,&result_len);

    if(exec_status) {
        ZVAL_STRINGL(return_value,result,result_len,1);
        efree(result);
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
    char *buf = NULL;
    ulong buf_len = 0;
    FILE *f = NULL;
    uint filename_len = 0, get_len = 0;
    char *filename = NULL;
    zval **desc = NULL;
    zval *id = NULL;
    uchar *result = NULL;
    ulong result_len = 0;
    zval *input_arr = NULL;
    zval      **src_entry,
              **dest_entry;
    char      *string_key;
    uint      string_key_len;
    ulong     num_key;
    HashPosition pos;
    HashTable *input_ht = NULL;

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
		// check security restrictions
		if((PG(safe_mode)
			&& !php_checkuid(filename, NULL, CHECKUID_CHECK_FILE_AND_DIR))
			|| php_check_open_basedir(filename TSRMLS_CC))
		{
			RETURN_FALSE
		}

        if(!(f = fopen(filename, "rb"))) {
	    	RETURN_FALSE;
        }
        buf = (char*)emalloc((BLITZ_INPUT_BUF_SIZE)*sizeof(char)); 
        buf_len = 0;
        while((get_len = fread(buf+buf_len, sizeof(char), BLITZ_INPUT_BUF_SIZE, f)) > 0) {
            buf_len += get_len;
            buf = (char*)erealloc(buf, (buf_len + BLITZ_INPUT_BUF_SIZE)*sizeof(char));
        }
        fclose(f);
        if(buf_len) buf[buf_len-1] = '\0';

        /* initialize template */
        if(!(itpl = blitz_init_tpl(buf, buf_len, tpl->ht_data TSRMLS_CC))) {
            blitz_free_tpl(itpl);    
            efree(buf);
            RETURN_FALSE;
        }

        itpl->name = emalloc((filename_len+1)*sizeof(char));
        if(itpl->name){
            memcpy(itpl->name,filename,filename_len);
            itpl->name[filename_len] = '\0';
        }

        /* analyze template */
        if(!blitz_analyse(itpl)) {
            blitz_free_tpl(itpl);
            efree(buf);
            RETURN_FALSE;
        }
        efree(buf);

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
                efree(buf);
                RETURN_FALSE;
            }
        }

        // save template index values
        ulong itpl_idx = tpl->itpl_list_len;
        tpl->itpl_list[itpl_idx] = itpl;
        zval *temp;
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
    if(blitz_exec_template(itpl,id,&result,&result_len)) {
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
