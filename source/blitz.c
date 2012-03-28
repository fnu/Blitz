/*
  +----------------------------------------------------------------------+
  | Authors: Alexey Rybak <raa@phpclub.net>,                             |
  |          template analyzing is based on php_templates code by        | 
  |          Maxim Poltarak (http://php-templates.sf.net)                | 
  +----------------------------------------------------------------------+
*/

/* $Id: blitz.c,v 0.7 2006/09/24 16:45:59 fisher Exp $ */

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
static int blitz_exec_template(blitz_tpl *tpl, zval *id, unsigned char **result, unsigned long *result_len TSRMLS_DC);

/* True global resources - no need for thread safety here */
static int le_blitz;
static int le_blitz_pack;

/* internal classes: Blitz, BlitzPack */
static zend_class_entry blitz_class_entry;
static zend_class_entry blitz_pack_class_entry;

/* {{{ blitz_functions[] : Blitz class */
function_entry blitz_functions[] = {
    PHP_FALIAS(blitz,               blitz_init,                 NULL)
    PHP_FALIAS(load,                blitz_load,                 NULL)
    PHP_FALIAS(dump_set,            blitz_dump_set,             NULL)
    PHP_FALIAS(dump_struct,         blitz_dump_struct,          NULL)
    PHP_FALIAS(dump_iterations,     blitz_dump_iterations,      NULL)
    PHP_FALIAS(set_global,          blitz_set_global,           NULL)
    PHP_FALIAS(set,                 blitz_set,                  NULL)
    PHP_FALIAS(set_new,             blitz_set,                  NULL)
    PHP_FALIAS(parse,               blitz_parse,                NULL)
    PHP_FALIAS(include,             blitz_include,              NULL)
    PHP_FALIAS(iterate,             blitz_iterate,              NULL)
    PHP_FALIAS(context,             blitz_context,              NULL)
    PHP_FALIAS(block,               blitz_block,                NULL)
    PHP_FALIAS(fetch,               blitz_fetch,                NULL)
    {NULL, NULL, NULL}
};
/* }}} */

/* {{{ blitz_pack_functions[] : BlitzPack class */
function_entry blitz_pack_functions[] = {
    PHP_FALIAS(blitzpack,   blitz_pack_init,    NULL)
    PHP_FALIAS(add,         blitz_pack_add,     NULL)
    PHP_FALIAS(save,        blitz_pack_save,    NULL)
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
    PHP_RINIT(blitz),        /* Replace with NULL if there's nothing to do at request start */
    PHP_RSHUTDOWN(blitz),    /* Replace with NULL if there's nothing to do at request end */
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
    STD_PHP_INI_ENTRY("blitz.node_open", BLITZ_TAG_OPEN_DEFAULT, PHP_INI_ALL, 
        OnUpdateString, node_open, zend_blitz_globals, blitz_globals)
    STD_PHP_INI_ENTRY("blitz.node_close", BLITZ_TAG_CLOSE_DEFAULT, PHP_INI_ALL, 
        OnUpdateString, node_close, zend_blitz_globals, blitz_globals)
PHP_INI_END()
/* }}} */


/* f - filename, b - buf, bl - buf_len, s = stream */
/* use: BLITZ_READ_STREAM(filename,buf,buf_len,stream) */
#define BLITZ_READ_STREAM(f,b,bl,s)                                                 \
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

/* use: BLITZ_READ_FOPEN(filename,buf,buf_len,f,get_len) */
#define BLITZ_READ_FOPEN(f,b,bl,s,gl)                                               \
    if(php_check_open_basedir((f) TSRMLS_CC)){                                      \
        return 0;                                                                   \
    }                                                                               \
                                                                                    \
    if(!((s) = fopen((f), "rb"))) {                                                 \
        php_error_docref(NULL TSRMLS_CC, E_ERROR,                                   \
            "unable to open file %s", (f)                                           \
        );                                                                          \
        return 0;                                                                   \
    }                                                                               \
                                                                                    \
    (b) = (char*)emalloc(BLITZ_INPUT_BUF_SIZE);                                     \
    (bl) = 0;                                                                       \
                                                                                    \
    while(((gl) = fread((b)+(bl), 1, BLITZ_INPUT_BUF_SIZE, (s))) > 0) {             \
        (bl) += (gl);                                                               \
        (b) = (char*)erealloc((b), (bl) + BLITZ_INPUT_BUF_SIZE);                    \
    }                                                                               \
    fclose((s));                                                                    \



/*  {{{ blitz_init_tpl_base */
/**********************************************************************************************************************/
blitz_tpl *blitz_init_tpl_base(HashTable *ht TSRMLS_DC){
/**********************************************************************************************************************/
    blitz_tpl *tpl = (blitz_tpl*)emalloc(sizeof(blitz_tpl));
    if (!tpl) {
        php_error_docref(
            NULL TSRMLS_CC, E_ERROR,
            "INTERNAL:unable to allocate memory for blitz template structure"
        );
        return NULL;
    }

    tpl->name = NULL;
    tpl->body = NULL;

    tpl->flags = 0;
    tpl->n_nodes = 0;
    tpl->config = (tpl_config_struct*)emalloc(sizeof(tpl_config_struct));
    if (!tpl->config) {
        php_error_docref(
            NULL TSRMLS_CC, E_ERROR,
            "INTERNAL:unable to allocate memory for blitz template config structure"
        );
        return NULL;
    }
    tpl->config->open_node = BLITZ_TAG_OPEN_DEFAULT;
    tpl->config->close_node = BLITZ_TAG_CLOSE_DEFAULT;
    tpl->config->l_open_node = strlen(tpl->config->open_node);
    tpl->config->l_close_node = strlen(tpl->config->close_node);

    tpl->nodes = NULL;
    tpl->is_from_pack = 0;
    tpl->pack = NULL;

    //  initialize iterations structire: array(0 => array()) and set current_iteration pointer
    MAKE_STD_ZVAL(tpl->iterations);
    array_init(tpl->iterations);
    tpl->current_iteration = NULL;
    tpl->current_iteration_node = & tpl->iterations;
    tpl->current_path = "/";
    tpl->path_buf = emalloc(BLITZ_CONTEXT_PATH_MAX_LEN);
    tpl->fetch_index = NULL;

    // allocate "personal" hash-table
    if (ht == NULL) { 
        tpl->hash_globals = NULL;
        tpl->hash_globals_is_others = 0;
        ALLOC_HASHTABLE(tpl->hash_globals);

        if (
            !tpl->hash_globals 
            || (FAILURE == zend_hash_init(tpl->hash_globals, 8, NULL, ZVAL_PTR_DTOR, 0))
        ) {
            php_error_docref(
                NULL TSRMLS_CC, E_ERROR,
                "INTERNAL:unable to allocate or fill memory for blitz params"
            );
            return NULL;
        }
    } else {
        /* 
           We "inherit" params from opener: the most correct way to do that
           is to follow a "copy-on-write" strategy. unless "inner" code doesn't
           modify template params - we use just a pointer-copy. otherwise we
           make a full data copy. But to make our life mush easier we just make 
           a copy and mark template with hash_globals_is_others = 1 not to free this object.
        */
        tpl->hash_globals = ht;
        tpl->hash_globals_is_others = 1;
    }

    tpl->ht_tpl_name = NULL;
    ALLOC_HASHTABLE(tpl->ht_tpl_name);
    if (
        !tpl->ht_tpl_name
        || (FAILURE == zend_hash_init(tpl->ht_tpl_name, 8, NULL, ZVAL_PTR_DTOR, 0))
    ) {
        php_error_docref(
            NULL TSRMLS_CC, E_ERROR,
            "INTERNAL:unable to allocate or fill memory for blitz template index"
        );
        return NULL;
    }

    tpl->itpl_list = 
        (blitz_tpl**)emalloc(BLITZ_ITPL_ALLOC_INIT*sizeof(blitz_tpl*));
    if(!tpl->itpl_list) {
        php_error_docref(
            NULL TSRMLS_CC, E_ERROR,
            "INTERNAL:unable to allocate memory for inner-include blitz template list"
        );
        return NULL;
    }
    tpl->itpl_list_len = 0;
    tpl->itpl_list_alloc = BLITZ_ITPL_ALLOC_INIT;

    return tpl;
}

/*  {{{ blitz_init_tpl */
/**********************************************************************************************************************/
blitz_tpl *blitz_init_tpl(
/**********************************************************************************************************************/
    char *filename, 
    HashTable *ht TSRMLS_DC) {
/**********************************************************************************************************************/
    unsigned int add_buffer_len = 0;
    FILE *f = NULL;
    unsigned int get_len = 0;
    unsigned int filename_len = 0;

    blitz_tpl *tpl = blitz_init_tpl_base(ht TSRMLS_CC);

    if(!tpl) return NULL;

    if(!filename) return tpl; // OK, will be loaded after

/* 
it seems to be 10% faster to use just fopen/fread than 
streams on lebowski-bench. however under win32 there are errors 
with relative paths (see php_blitz.h), i didn't checked why yet ;)
*/
#ifdef BLITZ_USE_STREAMS
    php_stream *stream = NULL;
    BLITZ_READ_STREAM(filename,tpl->body,tpl->body_len,stream);
#else
    BLITZ_READ_FOPEN(filename,tpl->body,tpl->body_len,f,get_len);
#endif

    // search algorithm requires lager buffer: body_len + add_buffer
    add_buffer_len = MAX(tpl->config->l_open_node,tpl->config->l_close_node);
    tpl->body = erealloc(tpl->body,tpl->body_len + add_buffer_len);
    memset(tpl->body + tpl->body_len,'\x0',add_buffer_len);

    filename_len = strlen(filename);
    tpl->name = emalloc(filename_len+1);
    if(tpl->name){
        memcpy(tpl->name,filename,filename_len);
        tpl->name[filename_len] = '\x0';
    }

    return tpl;
}
/* }}} */


/*  {{{ blitz_init_tpl_load */
/**********************************************************************************************************************/
int blitz_load_body(
/**********************************************************************************************************************/
    blitz_tpl *tpl,
    zval *body TSRMLS_DC) {
/**********************************************************************************************************************/
    unsigned int add_buffer_len = 0;
    char *name = "noname_loaded_from_zval";
    int name_len = strlen(name);

    if (!tpl || !body) {
        return 0;
    }

    tpl->body_len = Z_STRLEN_P(body);
    add_buffer_len = MAX(tpl->config->l_open_node,tpl->config->l_close_node);
    tpl->body = emalloc(tpl->body_len + add_buffer_len);
    memcpy(tpl->body,Z_STRVAL_P(body),Z_STRLEN_P(body));
    memset(tpl->body + tpl->body_len,'\x0',add_buffer_len);

    tpl->name = emalloc(name_len+1);
    if(tpl->name){
        memcpy(tpl->name,name,name_len);
        tpl->name[name_len] = '\x0';
    }

    return 1;
}
/* }}} */

/* {{{ blitz_free_tpl(blitz_tpl *tpl) */
/**********************************************************************************************************************/
void blitz_free_tpl(blitz_tpl *tpl) {
/**********************************************************************************************************************/
    unsigned char n_nodes=0, n_args=0, i=0, j=0;

    if(!tpl) return;

    if(tpl->config) 
        efree(tpl->config);

// 2DO: if PACK - don't free marked by PACK!!!

    n_nodes = tpl->n_nodes;
    if(n_nodes) {
        for(i=0;i<n_nodes;++i) {
            // these values shold be freed only for non-packed templates
            if(0 == tpl->is_from_pack) { 
                n_args = tpl->nodes[i].n_args;
                for(j=0;j<n_args;++j) {
                    if(tpl->nodes[i].args[j].name) {
                        efree(tpl->nodes[i].args[j].name); 
                    }
                }
                if(tpl->nodes[i].lexem) efree(tpl->nodes[i].lexem);
            }
            if(tpl->nodes[i].args) efree(tpl->nodes[i].args);
            if(tpl->nodes[i].children) efree(tpl->nodes[i].children);
        }
    }

    if(tpl->name && (0 == tpl->is_from_pack))
        efree(tpl->name);
    if(tpl->nodes)
        efree(tpl->nodes);

    if(tpl->body && (0 == tpl->is_from_pack)) 
        efree(tpl->body); 

    if(tpl->hash_globals && !tpl->hash_globals_is_others)
        FREE_HASHTABLE(tpl->hash_globals);

    if(tpl->ht_tpl_name)
        FREE_HASHTABLE(tpl->ht_tpl_name);
    if(tpl->fetch_index)
        FREE_HASHTABLE(tpl->fetch_index);

    if(tpl->path_buf)
        efree(tpl->path_buf);

    if(tpl->iterations)
        FREE_ZVAL(tpl->iterations);

    if(tpl->itpl_list) { // ? PACK
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
/**********************************************************************************************************************/
static void blitz_resource_dtor(zend_rsrc_list_entry *rsrc TSRMLS_DC) {
/**********************************************************************************************************************/
    blitz_tpl *tpl = NULL;
    tpl = (blitz_tpl*)rsrc->ptr;
    blitz_free_tpl(tpl);
}
/* }}} */

/* {{{ blitz_pack(char *filename, HashTable *ht) */
/**********************************************************************************************************************/
blitz_pack *blitz_init_pack(char *filename) {
/**********************************************************************************************************************/
    php_stream *stream = NULL;
    void *i_data = NULL, *i_ptr_head = NULL;
    unsigned long i = 0, n_ht_tpl_shift = BLITZ_PACK_SHIFT_ALLOC_INIT; 
    unsigned long i_tpl_pack_len = 0, i_tpl_name_len = 0;
    char *i_tpl_name = NULL;
    zval *temp = NULL;

    blitz_pack *pack = (blitz_pack*)emalloc(sizeof(blitz_pack));
  
    if (!pack) {
        php_error_docref(
            NULL TSRMLS_CC, E_ERROR,
            "INTERNAL:unable to allocate memory for blitz template structure"
        );
        efree(pack);
        return NULL; 
    }

    pack->tpl_head = NULL;
    pack->tpl_pack = NULL;
    pack->tpl_num = 0;
    pack->tpl_head_len = 0;
    pack->tpl_pack_len = 0;
    pack->data = NULL;
    pack->data_len = 0;
    pack->filename = NULL;
    pack->version = 10004;

    if (filename) {
        // read file
        pack->filename = estrdup(filename);
        if (php_check_open_basedir(filename TSRMLS_CC)){
            efree(pack);
            return NULL; 
        }
        stream = php_stream_open_wrapper(filename, "rb",
            IGNORE_PATH|IGNORE_URL|IGNORE_URL_WIN|ENFORCE_SAFE_MODE|REPORT_ERRORS, NULL
        );
        if (!stream)
        {
            efree(pack);
            return NULL; 
        }
        pack->data_len = php_stream_copy_to_mem(stream, (char**)&pack->data, PHP_STREAM_COPY_ALL, 0);
        if (pack->data && pack->data_len>0) {

            // 2DO: check ALL values!!! everithing from the file!

            // header
            i_data = pack->data;
            pack->version = (unsigned long)(*(unsigned long*)(i_data));
            i_data += sizeof(unsigned long);
            pack->tpl_num = (unsigned long)(*(unsigned long*)(i_data));
            i_data += sizeof(unsigned long);
            // template header
            pack->tpl_head_len = (unsigned long)(*(unsigned long*)(i_data));
            i_data += sizeof(unsigned long);

            if (pack->tpl_head_len + 3*sizeof(unsigned long) > pack->data_len) {
                efree(pack);
                return NULL;
            }
            pack->tpl_head = i_data;
            i_data += pack->tpl_head_len;
            // template data 
            pack->tpl_pack_len = (unsigned long)(*(unsigned long*)(i_data));
            if (pack->tpl_pack_len + pack->tpl_head_len + 4*sizeof(unsigned long) > pack->data_len) {
                efree(pack);
                return NULL;
            }
            pack->tpl_pack = i_data + sizeof(unsigned long);
            pack->filename = estrdup(filename);
        } else {
            efree(pack);
            return NULL;
        }
    } 

    // allocate lookup table
    n_ht_tpl_shift = pack->tpl_num;
    ALLOC_HASHTABLE(pack->ht_tpl_shift);
    if (
        !pack->ht_tpl_shift
        || (FAILURE == zend_hash_init(pack->ht_tpl_shift, n_ht_tpl_shift, NULL, ZVAL_PTR_DTOR, 0))
    ) {
        php_error_docref(
            NULL TSRMLS_CC, E_ERROR,
            "INTERNAL:unable to allocate or fill memory for shift index in plitz pack"
        );
        efree(pack);
        return NULL;
    }

    // lookup table values
    i_ptr_head = pack->tpl_head;
    for (i=0; i<n_ht_tpl_shift; i++) {
        i_tpl_pack_len = (unsigned long)(*(unsigned long*)i_ptr_head);
        i_ptr_head += sizeof(unsigned long);
        i_tpl_name_len = (unsigned long)(*(unsigned long*)i_ptr_head);
        i_ptr_head += sizeof(unsigned long);
        i_tpl_name = (char*)i_ptr_head;
        i_ptr_head += i_tpl_name_len;
        
        MAKE_STD_ZVAL(temp);
        ZVAL_LONG(temp, i_tpl_pack_len);
        zend_hash_update(pack->ht_tpl_shift, i_tpl_name, i_tpl_name_len,
           &temp, sizeof(zval *), NULL);
        // 2DO: check, free, return NULL if errors et cetera
    }

    return pack;
}
/* }}} */

/* {{{ blitz_free_pack(blitz_pack *pack) */
/**********************************************************************************************************************/
void blitz_free_pack(blitz_pack *pack) {
/**********************************************************************************************************************/
    if (!pack)
        return;

    if (pack->filename)
        efree(pack->filename);

    efree(pack);
}

/* {{{ blitz_pack_resource_dtor */
/**********************************************************************************************************************/
static void blitz_pack_resource_dtor(zend_rsrc_list_entry *rsrc TSRMLS_DC) {
/**********************************************************************************************************************/
    blitz_pack *pack = NULL;
    pack = (blitz_pack*)rsrc->ptr;
    blitz_free_pack(pack);
}
/* }}} */

/* {{{ php_blitz_init_globals */
/**********************************************************************************************************************/
static void php_blitz_init_globals(zend_blitz_globals *blitz_globals)
/**********************************************************************************************************************/
{
    blitz_globals->var_prefix = BLITZ_TAG_VAR_PREFIX;
    blitz_globals->node_open = BLITZ_TAG_OPEN_DEFAULT;
    blitz_globals->node_close = BLITZ_TAG_CLOSE_DEFAULT;
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION */
/**********************************************************************************************************************/
PHP_MINIT_FUNCTION(blitz)
/**********************************************************************************************************************/
{
    ZEND_INIT_MODULE_GLOBALS(blitz, php_blitz_init_globals, NULL);
    REGISTER_INI_ENTRIES();
    le_blitz = zend_register_list_destructors_ex(
        blitz_resource_dtor, NULL, "blitz template", module_number);

    le_blitz_pack = zend_register_list_destructors_ex(
        blitz_pack_resource_dtor, NULL, "blitz pack", module_number);

    INIT_CLASS_ENTRY(blitz_class_entry, "blitz", blitz_functions);
    zend_register_internal_class(&blitz_class_entry TSRMLS_CC);

    INIT_CLASS_ENTRY(blitz_pack_class_entry, "blitzpack", blitz_pack_functions);
    zend_register_internal_class(&blitz_pack_class_entry TSRMLS_CC);

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION */
/**********************************************************************************************************************/
PHP_MSHUTDOWN_FUNCTION(blitz)
/**********************************************************************************************************************/
{
    UNREGISTER_INI_ENTRIES();
    return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION */
/**********************************************************************************************************************/
PHP_RINIT_FUNCTION(blitz)
/**********************************************************************************************************************/
{
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_RSHUTDOWN_FUNCTION */
/**********************************************************************************************************************/
PHP_RSHUTDOWN_FUNCTION(blitz)
/**********************************************************************************************************************/
{
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION */
/**********************************************************************************************************************/
PHP_MINFO_FUNCTION(blitz)
/**********************************************************************************************************************/
{
    php_info_print_table_start();
    php_info_print_table_row(2, "Blitz support", "enabled");
    php_info_print_table_row(2, "Version", "0.4.0");
    php_info_print_table_end();
    DISPLAY_INI_ENTRIES();
}
/* }}} */

// debug functions

/**********************************************************************************************************************/
static void php_blitz_dump_struct_plain(blitz_tpl *tpl) {
/**********************************************************************************************************************/
    unsigned long i = 0, j = 0;
    tpl_node_struct *node = NULL;

    php_printf("== PLAIN STRUCT (%ld nodes):",(unsigned long)tpl->n_nodes);
    for (i=0; i<tpl->n_nodes; ++i) {
        node = & tpl->nodes[i];
        php_printf("\n%s[%d] (%ld(%ld), %ld(%ld)); ",
            node->lexem,
            node->type,
            node->pos_begin, node->pos_begin_shift,
            node->pos_end, node->pos_end_shift
        );
        if (BLITZ_IS_METHOD(node->type)) {
            php_printf("ARGS(%d): ",node->n_args);
            for (j=0;j<node->n_args;++j) {
                if(j) php_printf(",");
                php_printf("%s(%d)",node->args[j].name,node->args[j].type);
            }
            if(node->children) {
                php_printf("; CHILDREN(%d):",node->n_children);
            }
        }
    }
    return;
}

/**********************************************************************************************************************/
static void php_blitz_dump_node(tpl_node_struct *node, unsigned int *p_level) {
/**********************************************************************************************************************/
    unsigned long j = 0;
    unsigned int level = 0;
    char shift_str[] = "--------------------------------"; 
    if (!node) return;
    level = p_level ? *p_level : 0;
    if(level>=10) level = 10;
    memset(shift_str,' ',2*level+1);
    shift_str[2*level+1] = '^';
    shift_str[2*level+3] = '\x0';
    php_printf("\n%s%s[%u] (%lu(%lu), %lu(%lu)); ",
        shift_str,
        node->lexem,
        (unsigned int)node->type,
        node->pos_begin, node->pos_begin_shift,
        node->pos_end, node->pos_end_shift
    );
    if (BLITZ_IS_METHOD(node->type)) {
        php_printf("ARGS(%d): ",node->n_args);
        for (j=0;j<node->n_args;++j) {
            if(j) php_printf(",");
            php_printf("%s(%d)",node->args[j].name,node->args[j].type);
        }
        if(node->children) {
            php_printf("; CHILDREN(%d):",node->n_children);
            for (j=0;j<node->n_children;++j) {
                (*p_level)++;
                php_blitz_dump_node(node->children[j],p_level);
                (*p_level)--;
            }
        }
    }
}

/**********************************************************************************************************************/
static void php_blitz_dump_struct(blitz_tpl *tpl) {
/**********************************************************************************************************************/
    unsigned long i = 0;
    unsigned int level = 0; 
    php_printf("== TREE STRUCT (%ld nodes):",(unsigned long)tpl->n_nodes);
    unsigned int last_close = 0;
    for (i=0; i<tpl->n_nodes; ++i) {
        if(tpl->nodes[i].pos_begin>last_close) {
            php_blitz_dump_node(tpl->nodes+i, &level);
            last_close = tpl->nodes[i].pos_end;
        }
    }

    php_printf("\n");
    php_blitz_dump_struct_plain(tpl);
    php_printf("\n");

    return;
}

# define REALLOC_POS_IF_EXCEEDS                                                 \
    if (i >= alloc_size) {                                                      \
        alloc_size = alloc_size << 1;                                           \
        *pos = erealloc(*pos,alloc_size*sizeof(**pos));                         \
    }                                                                           \

/**********************************************************************************************************************/
inline static void blitz_bm_search (                                                        
/**********************************************************************************************************************/
    unsigned char *haystack, 
    unsigned long haystack_length,
    unsigned char *needle,
    unsigned int needle_len,
    unsigned int *n_found,
    unsigned long **pos, 
    unsigned long pos_alloc_size TSRMLS_DC) {
/**********************************************************************************************************************/
    register unsigned long  i=0, j=0, k=0, shift=0, cmp=0;
    register unsigned long  haystack_len=haystack_length;
    register unsigned long alloc_size = pos_alloc_size;
    unsigned long           bmBc[256];

    if(haystack_len < (unsigned long)needle_len) {
        php_error_docref(NULL TSRMLS_CC, E_ERROR, 
            "INTERNAL:length of haystack(%lu) is less than needle length(%u)\n", haystack_len, needle_len
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


#define INIT_CALL_ARGS                                                         \
    node->args = (call_arg*)                                                   \
        emalloc(BLITZ_CALL_ALLOC_ARG_INIT*sizeof(call_arg));                   \
    node->n_args = 0;                                                          \
    n_arg_alloc = BLITZ_CALL_ALLOC_ARG_INIT;

# define REALLOC_ARG_IF_EXCEEDS                                                \
    if (arg_id >= n_arg_alloc) {                                               \
        n_arg_alloc = n_arg_alloc << 1;                                        \
        node->args = (call_arg*)                                               \
            erealloc(node->args,n_arg_alloc*sizeof(call_arg));                 \
    }  

# define ADD_CALL_ARGS                                                         \
    REALLOC_ARG_IF_EXCEEDS;                                                    \
    i_arg = node->args + arg_id;                                               \
    i_arg->name = estrndup(token,i_len);                                       \
    i_arg->len = i_len;                                                        \
    i_arg->type = i_type;                                                      \
    ++arg_id;                                                                  \
    node->n_args = arg_id;                                                     

/**********************************************************************************************************************/
inline static void blitz_parse_call (
/**********************************************************************************************************************/
    unsigned char *text, 
    unsigned int len_text, 
    tpl_node_struct *node, 
    unsigned int *true_lexem_len,
    unsigned char *error TSRMLS_DC) {
/**********************************************************************************************************************/
    register unsigned char *c = text;
    unsigned char *p = NULL;
    register unsigned char symb = 0, i_symb = 0;
    unsigned char state = BLITZ_CALL_STATE_ERROR;
    unsigned char ok = 0;
    register unsigned int pos = 0, i_pos = 0, i_len = 0;
    unsigned char was_escaped;
    unsigned char main_token[1024], token[1024];
    unsigned char n_arg_alloc = 0;
    register unsigned char i_type = 0;
    unsigned char arg_id = 0;
    call_arg *i_arg = NULL;

    // init node
    node->n_args = 0;
    node->args = NULL;
    node->lexem = NULL;
    node->type = BLITZ_TYPE_METHOD;

    *true_lexem_len = 0; // used for parameters only

    BLITZ_SKIP_BLANK(c,i_pos,pos);

//php_printf("B: pos=%ld, c=%c\n", pos, *c);

    p = main_token;
    i_pos = 0;
    // parameter or method?
    if (*c == BLITZ_TAG_VAR_PREFIX) { // scan parameter
        ++c; ++pos; i_pos=0;
        BLITZ_SCAN_ALNUM(c,p,i_pos,i_symb);
        pos+=i_pos;
        if (i_pos!=0) {
            node->lexem = estrndup(main_token,i_pos);
            *true_lexem_len = i_pos;
            node->type = BLITZ_TYPE_VAR;
            state = BLITZ_CALL_STATE_FINISHED;
        }
    } else if(BLITZ_IS_ALPHA(*c)) { // scan function

//php_printf("D1: pos=%ld, c=%c\n", pos, *c);
        
        BLITZ_SCAN_ALNUM(c,p,i_pos,i_symb);
//php_printf("D2: pos=%ld, c=%c\n", pos, *c);
        //i_pos--;
        pos+=i_pos-1;
        c = text + pos;
//php_printf("D3: pos=%ld, c=%c\n", pos, *c);

        if(i_pos>1) {
            zend_str_tolower(main_token,i_pos);
            node->lexem = estrndup(main_token,i_pos);
            node->type = BLITZ_TYPE_METHOD;
            *true_lexem_len = i_pos-1;
            ++pos; ++c;

//php_printf("F: %s, pos=%ld, c=%c\n", node->lexem, pos, *c);
            if(*c == '(') { // has arguments
                ok = 1; ++pos;
                BLITZ_SKIP_BLANK(c,i_pos,pos);
                if(*c == ')') { // move to finished without any middle-state if no args
                    ++pos;
                    state = BLITZ_CALL_STATE_FINISHED;
                } else {
                    INIT_CALL_ARGS;
                    state = BLITZ_CALL_STATE_NEXT_ARG;

                    // predefined method?
                    if (0 == strcmp(main_token,BLITZ_NODE_TYPE_IF_S)) {
                        node->type = BLITZ_NODE_TYPE_IF;
                    } else if (0 == strcmp(main_token,BLITZ_NODE_TYPE_INCLUDE_S)) {
                        node->type = BLITZ_NODE_TYPE_INCLUDE;
                    }
                }
            } else {
                ok = 1;
                if(0 == strcmp(main_token,BLITZ_NODE_TYPE_BEGIN_S)) {
                    INIT_CALL_ARGS; 
                    state = BLITZ_CALL_STATE_BEGIN;
                    node->type = BLITZ_NODE_TYPE_BEGIN;
                } else if(0 == strcmp(main_token,BLITZ_NODE_TYPE_END_S)) {
                    INIT_CALL_ARGS; 
                    state = BLITZ_CALL_STATE_END;
                    node->type = BLITZ_NODE_TYPE_END;
                } else {
                    state = BLITZ_CALL_STATE_FINISHED;
                }
            }
        }

//php_printf("W: pos=%ld, c=%c\n", pos, *c);
        c = text + pos;
        while((symb = *c) && ok) {
//php_printf("SWI: pos=%ld, state=%ld, c=%c\n", pos, state, symb);
            switch(state) {
                case BLITZ_CALL_STATE_BEGIN:
                    symb = *c;
                    BLITZ_SKIP_BLANK(c,i_pos,pos);
                    i_len = i_pos = ok = 0;
                    p = token;
                    BLITZ_SCAN_ALNUM(c,p,i_len,i_symb);

                    if (i_len!=0) {
                        ok = 1; 
                        pos += i_len;
                        ADD_CALL_ARGS;
                        state = BLITZ_CALL_STATE_FINISHED;
                    } else {
                        state = BLITZ_CALL_STATE_ERROR;
                    }
//php_printf("B:E pos=%ld, c=%c\n", pos, *c);
                    break;
                case BLITZ_CALL_STATE_END:
                    state = BLITZ_CALL_STATE_FINISHED;
                    break;
                case BLITZ_CALL_STATE_NEXT_ARG:
                    BLITZ_SKIP_BLANK(c,i_pos,pos);
                    symb = *c;
                    i_len = i_pos = ok = 0;
                    p = token;
                    i_type = 0;

//php_printf("P:B %ld, %ld; c = %c\n", pos, len_text, *c);
                    if (symb == BLITZ_TAG_VAR_PREFIX) {
                        ++c; ++pos;
                        BLITZ_SCAN_ALNUM(c,p,i_pos,i_symb);
                        if (i_pos!=0) ok = 1;
                        i_type = BLITZ_ARG_TYPE_VAR;
                        i_len = i_pos;
                    } else if (symb == '\'') {
                        ++c; ++pos;
                        BLITZ_SCAN_SINGLE_QUOTED(c,p,i_pos,i_len,ok);
                        i_pos++;
                        i_type = BLITZ_ARG_TYPE_STR;
                    } else if (symb == '\"') {
                        ++c; ++pos;
                        BLITZ_SCAN_DOUBLE_QUOTED(c,p,i_pos,i_len,ok);
                        i_pos++;
                        i_type = BLITZ_ARG_TYPE_STR;
                    } else if (BLITZ_IS_NUMBER(symb)) {
                        BLITZ_SCAN_NUMBER(c,p,i_pos,i_symb);
                        i_type = BLITZ_ARG_TYPE_NUM;
                        i_len = i_pos;
                        if (i_pos!=0) ok = 1;
                    }
//php_printf("P:B-2 %ld, %ld; c = \"%c\", i_pos = %ld\n", pos, len_text, *c, i_pos);

                    if (ok) {
                        pos += i_pos;
                        c = text + pos;
//php_printf("P:E %ld, %ld; c = \"%c\", i_pos = %ld\n", pos, len_text, *c, i_pos);
                        ADD_CALL_ARGS;
//php_printf("P:NEXT %ld, %ld; c = %c\n", pos, len_text, *c);
                        state = BLITZ_CALL_STATE_HAS_NEXT;
                    } else {
                        state = BLITZ_CALL_STATE_ERROR;
                    }
                    break;
                case BLITZ_CALL_STATE_HAS_NEXT:
                    BLITZ_SKIP_BLANK(c,i_pos,pos);
                    symb = *c;
//php_printf("P:2-0 %ld, %ld; c = %c\n", pos, len_text, *c);
                    if (symb == ',') {
                        state = BLITZ_CALL_STATE_NEXT_ARG;
                        ++c; ++pos;
                    } else if (symb == ')') {
                        state = BLITZ_CALL_STATE_FINISHED;
                        ++c; ++pos;
//php_printf("P:2-1 %ld, %ld; c = %c\n", pos, len_text, *c);
                    } else {
                        state = BLITZ_CALL_STATE_ERROR;
                    }
                    break;
                case BLITZ_CALL_STATE_FINISHED:
                    BLITZ_SKIP_BLANK(c,i_pos,pos);
//php_printf("P:3 %ld, %ld; c = %c\n", pos, len_text, *c);
                    if(*c == ';') {
                        ++c; ++pos;
                    }
                    BLITZ_SKIP_BLANK(c,i_pos,pos);
//php_printf("P:3 %ld, %ld; c = %c\n", pos, len_text, *c);
                    if(pos != len_text) {
                        state = BLITZ_CALL_STATE_ERROR;
                    }
                    ok = 0;
                    break;
                default:
                    ok = 0;
                    break;
            }
        }
    }

    if(state != BLITZ_CALL_STATE_FINISHED) {
        *error = BLITZ_CALL_ERROR;
    } else if ((node->type == BLITZ_NODE_TYPE_IF) && (node->n_args<2 || node->n_args>3)) {
        *error = BLITZ_CALL_ERROR_IF;
    } else if ((node->type == BLITZ_NODE_TYPE_INCLUDE) && (node->n_args!=1)) {
        *error = BLITZ_CALL_ERROR_INCLUDE;
    }
}

/*************************************************************************************************/
inline unsigned long get_line_number(char *str, unsigned long pos) {
/*************************************************************************************************/
    register char *p = str;
    register unsigned long i = pos;
    register unsigned int n = 0;
    p += i;
    if(i>0) {
        while(i--) {
            if(*p == '\n') {
                n++;
            }
            p--;
        }
    }
    // humans like to start counting with 1, not 0
    n += 1; 
    return n;
}

/*************************************************************************************************/
inline unsigned long get_line_pos(char *str, unsigned long pos) {
/*************************************************************************************************/
    register char *p = str;
    register unsigned long i = pos;
    p += i;
    if(i>0) {
        while(i--) {
            if(*p == '\n') {
                return pos - i + 1;
            }
            p--;
        }
    }
    // humans like to start counting with 1, not 0
    return pos - i + 1; 
}

/**********************************************************************************************************************/
inline static int blitz_analyse (blitz_tpl *tpl TSRMLS_DC) {
/**********************************************************************************************************************/
    unsigned int n_open = 0, n_close = 0, n_nodes = 0;
    unsigned int i = 0, j = 0;
    unsigned long *pos_open = NULL;
    unsigned long *pos_close = NULL;
    unsigned long *i_pos_open = NULL;
    unsigned long *i_pos_close = NULL;
    unsigned long current_open = 0, current_close, next_open = 0; 

    unsigned int l_open_node = tpl->config->l_open_node;
    unsigned int l_close_node = tpl->config->l_close_node;
    unsigned int lexem_len = 0;
    unsigned int true_lexem_len = 0;
    unsigned int shift = 0;
    tpl_node_struct *i_node = NULL, *parent = NULL;
    tpl_node_struct **node_stack = NULL, **pp_node_stack = NULL; 
    unsigned char *body = NULL;
    unsigned char *plex = NULL;
    unsigned long pos_open_size = 0, pos_close_size = 0;
    unsigned char i_error = 0;

    pos_open = (unsigned long*)ecalloc(BLITZ_ALLOC_TAGS_STEP,sizeof(unsigned long));
    pos_open_size = BLITZ_ALLOC_TAGS_STEP;
    pos_close = (unsigned long*)ecalloc(BLITZ_ALLOC_TAGS_STEP,sizeof(unsigned long));
    pos_close_size = BLITZ_ALLOC_TAGS_STEP;

    if((pos_open == NULL) || (pos_close == NULL)) {
        return -1;
    }

    // find open node positions
    body = tpl->body;
    blitz_bm_search (
        body, tpl->body_len,
        tpl->config->open_node, l_open_node, 
        &n_open, &pos_open, pos_open_size TSRMLS_CC
    );

    // find close node positions
    body = tpl->body;
    blitz_bm_search (
        body, tpl->body_len,
        tpl->config->close_node, l_close_node,
        &n_close, &pos_close, pos_close_size TSRMLS_CC
    );

    // allocate memory for nodes
    tpl->nodes = (tpl_node_struct*)emalloc(
        n_open*sizeof(tpl_node_struct)
    );

    if (!tpl->nodes) {
        php_error_docref(NULL TSRMLS_CC, E_ERROR, 
            "INTERNAL: unable to allocate memory for %u nodes", n_open
        );
        efree(pos_open);
        efree(pos_close);
        return 0;
    }

    // set correct pairs even if template has wrong grammar
    i = 0;
    i_pos_open = pos_open;
    i_pos_close = pos_close;
    n_nodes = 0;

    for(i=0; i<n_open; i++) {
        current_open = *i_pos_open;
        // mmm... we won't add check here, but we need +1 in allocated buffer then
        next_open = (i<(n_open-1)) ? *(i_pos_open+1) : 0;
        if (!i_pos_close) {
            ++i_pos_close; // ? 2DO: segfault  
            ++i;
            continue;
        }
        current_close = *i_pos_close;
        if (current_close<current_open) {
            php_error_docref(NULL TSRMLS_CC, E_WARNING,
                "SYNTAX ERROR: close tag without open (%s: line %lu, pos %lu)",
                 tpl->name, get_line_number(body,current_close), get_line_pos(body,current_close)
            );
            ++i_pos_close; 
            ++i;
            continue;
        } else if (next_open && next_open<current_close) {
            php_error_docref(NULL TSRMLS_CC, E_WARNING,
                "SYNTAX ERROR: open tag without close (%s: line %lu, pos %lu)",
                tpl->name, get_line_number(body,current_open), get_line_pos(body,current_open)
            );
            ++i_pos_open; 
            ++i;
        } else { 
            shift = current_open + l_open_node;
            lexem_len = current_close - shift;
            if(lexem_len>BLITZ_MAX_LEXEM_LEN) {
                php_error_docref(NULL TSRMLS_CC, E_WARNING, 
                    "SYNTAX ERROR: lexem is too long (%s: line %lu, pos %lu)", 
                    tpl->name, get_line_number(body,current_open), get_line_pos(body,current_open)
                );
            } else if (lexem_len<=0) {
                php_error_docref(NULL TSRMLS_CC, E_WARNING, 
                    "SYNTAX ERROR: zero length lexem (%s: line %lu, pos %lu)", 
                    tpl->name, get_line_number(body,current_open), get_line_pos(body,current_open)
                );
            } else { // OK: parse 
                i_error = 0;
                plex = tpl->body + shift;
                j = lexem_len;
                true_lexem_len = 0;
                i_node = tpl->nodes + n_nodes;
                i_node->pos_in_list = n_nodes;
                blitz_parse_call(plex,j,i_node,&true_lexem_len,&i_error TSRMLS_CC);
                if (i_error) {
                    i_node->has_error = 1;
                    if(i_error == BLITZ_CALL_ERROR) {
                        php_error_docref(NULL TSRMLS_CC, E_WARNING,
                            "SYNTAX ERROR: invalid method call (%s: line %lu, pos %lu)",
                            tpl->name, get_line_number(body,current_open), get_line_pos(body,current_open)
                        );
                    } else if (i_error == BLITZ_CALL_ERROR_IF) {
                        php_error_docref(NULL TSRMLS_CC, E_WARNING,
                            "SYNTAX ERROR: invalid <if> syntax, only 2 or 3 arguments allowed (%s: line %lu, pos %lu)",
                            tpl->name, get_line_number(body,current_open), get_line_pos(body,current_open)
                        );
                    } else if (i_error == BLITZ_CALL_ERROR_INCLUDE) {
                        php_error_docref(NULL TSRMLS_CC, E_WARNING,
                            "SYNTAX ERROR: invalid <inlcude> syntax, only 1 argument allowed (%s: line %lu, pos %lu)",
                            tpl->name, get_line_number(body,current_open), get_line_pos(body,current_open)
                        );
                    }
                } else { 
                    i_node->has_error = 0;
                } 

                i_node->n_children = 0;
                i_node->children = NULL;
                i_node->pos_begin_shift = 0;
                i_node->pos_end_shift = 0;
                // if it's a context node - build tree 
                if(i_node->type == BLITZ_NODE_TYPE_BEGIN) {
                    // stack: allocate here (no mallocs for simple templates)
                    if(!node_stack) {
                        // allocate memory for node stack to build tree
                        node_stack = (tpl_node_struct**)emalloc(
                            (n_open+1)*sizeof(tpl_node_struct*)
                        );
                        *node_stack = NULL; // first element is NULL to make simple check if stack is empty
                        pp_node_stack = node_stack;
                    }

                    // begin: init node, get parent from stack, 
                    // add child to parent, put child to stack

                    if(*pp_node_stack) {
                        parent = *pp_node_stack;
                    }
                    ++pp_node_stack;
                    *pp_node_stack = i_node;

                    i_node->pos_begin = current_open;
                    i_node->pos_begin_shift = current_close + l_close_node;
                    // next 3 need finalization when END is detected
                    i_node->pos_end = current_close; // fake - needs finalization
                    i_node->pos_end_shift = 0;
                    i_node->lexem_len = true_lexem_len; 
                    ++n_nodes;
                    if(parent) {
                        // 2DO: add dynamical re-allocation
                        if(0 == parent->n_children) {
                            parent->children = (tpl_node_struct**)emalloc(16*sizeof(tpl_node_struct*));
                        }
                        parent->children[parent->n_children] = i_node;
                        ++parent->n_children;
                    }
                    parent = i_node;
                } else if (i_node->type == BLITZ_NODE_TYPE_END) {
                    // end: remove node from stack, finalize node: set true close positions and new type
                    if(pp_node_stack && *pp_node_stack) {
                        //
                        //    {{ begin    a }}                 bla       {{                  end a }}
                        //   ^--pos_open      ^--pos_begin_shift        ^--pos_end_shift          ^--pos_close
                        //
                        parent = *pp_node_stack;
                        parent->pos_end_shift = current_open;
                        parent->pos_end = current_close;
                        parent->type = BLITZ_NODE_TYPE_CONTEXT;
                        --pp_node_stack;
                        parent = *pp_node_stack;
                    } else {
                        // error: end with no begin
                        i_node->has_error = 1;
                        php_error_docref(NULL TSRMLS_CC, E_WARNING,
                            "SYNTAX ERROR: end with no begin (%s: line %lu, pos %lu)", 
                            tpl->name, get_line_number(body,current_open),get_line_pos(body,current_open)
                        );
                    }
                } else { // just a var- or call-node
                    i_node->pos_begin = current_open;
                    i_node->pos_end = current_close;
                    i_node->lexem_len = true_lexem_len;
                    ++n_nodes;
                    // add node to parent 
                    if(parent) {
                        // 2DO: add dynamical re-allocation
                        if(0 == parent->n_children) {
                            parent->children = (tpl_node_struct**)emalloc(16*sizeof(tpl_node_struct*));
                        }
                        parent->children[parent->n_children] = i_node;
                        ++parent->n_children;
                    }
                }
            }
            ++i_pos_open;
            ++i_pos_close;
        }
    }

    tpl->n_nodes = n_nodes;

    efree(pos_open);
    efree(pos_close);
    if(node_stack)
        efree(node_stack);

    return 1;
}

#define BLITZ_PACK_ADD(ptr_src,src_bytes)                                       \
    if((pack_len+src_bytes)>=pack_alloc_len) {                                  \
        *pack = (void*)erealloc((*pack),pack_len+src_bytes);                    \
        if(!(*pack)) {                                                          \
            php_error_docref(NULL TSRMLS_CC, E_ERROR,                           \
                "INTERNAL: unable to reallocate memory for the pack (%ld,%ld)", \
                pack_alloc_len, pack_len+src_bytes                              \
            );                                                                  \
            return 0;                                                           \
        }                                                                       \
        pack_alloc_len=pack_len+src_bytes;                                      \
        i_pack = *pack + pack_len;                                              \
    }                                                                           \
    memcpy(i_pack,(void*)(ptr_src),(src_bytes));                                \
    i_pack += (src_bytes);                                                      \
    pack_len += (src_bytes);                                                    \

#define BLITZ_UNPACK_ULONG(pack,s)        ((unsigned long)(*(unsigned long*) ((pack) + (s)) ))
#define BLITZ_UNPACK_UINT(pack,s)         ((unsigned int)(*(unsigned int*) ((pack) + (s)) ))
#define BLITZ_UNPACK_UCHAR(pack,s)        ((unsigned char)(*(unsigned char*) ((pack) + (s)) ))
#define BLITZ_UNPACK_CHAR_P(pack,s)       (char*)((pack) + (s))

// debug function
/**********************************************************************************************************************/
void blitz_tpl_pack_dump(void *pack) {
/**********************************************************************************************************************/
    unsigned long i_len = 0, name_len = 0, body_len = 0, lex_len = 0;
    unsigned long shift = 0, n_nodes = 0, n_args = 0;
    unsigned long i = 0, j = 0;
    char *i_str = NULL;

    php_printf("started unpack test...\n");
    php_printf("header length: %lu\n",BLITZ_UNPACK_ULONG(pack,0));
    name_len = BLITZ_UNPACK_ULONG(pack,2*sizeof(unsigned long));
    body_len = BLITZ_UNPACK_ULONG(pack,name_len+3*sizeof(unsigned long));
    php_printf("template name: %s\n",BLITZ_UNPACK_CHAR_P(pack,3*sizeof(unsigned long)));
    php_printf("template name length: %lu\n",name_len);
    php_printf("body length:%lu\n",body_len);
    php_printf("full length:%lu\n",BLITZ_UNPACK_ULONG(pack,sizeof(unsigned long)));
    
    shift = 4*sizeof(unsigned long) + name_len;
    n_nodes = BLITZ_UNPACK_UINT(pack,shift);
    php_printf("total nodes: %lu",n_nodes);
    if (n_nodes>0) 
        php_printf(" ...dumping");
    php_printf("\n");

    shift = 4*sizeof(unsigned long) + sizeof(unsigned int) + name_len + body_len; 

    for (i=0; i<n_nodes; i++) {
        i_len = BLITZ_UNPACK_ULONG(pack,shift); 
        lex_len = BLITZ_UNPACK_ULONG(pack,shift+sizeof(unsigned long));
        i_str = BLITZ_UNPACK_CHAR_P(pack,shift+2*sizeof(unsigned long));
        n_args = BLITZ_UNPACK_UCHAR(pack,shift+4*sizeof(unsigned long)+sizeof(unsigned char)+lex_len); 
        shift += i_len;

        php_printf(" * node:%s, pack_len: %lu, lexem_len=%lu, n_args=%lu\n",i_str,i_len,lex_len,n_args);
        if (n_args>0) {
            php_printf("   total args: %lu, dumping...\n",n_args);
            for (j=0; j<n_args; j++) {
                i_len = BLITZ_UNPACK_ULONG(pack,shift);
                lex_len = BLITZ_UNPACK_ULONG(pack,shift+sizeof(unsigned long));
                i_str = BLITZ_UNPACK_CHAR_P(pack,shift+2*sizeof(unsigned long));
                php_printf("   ==== arg: %s, lexem_len=%lu\n",i_str,lex_len);
                shift+=i_len; 
            }
        }
    }
}

// make packed presentation of the template
/**********************************************************************************************************************/
int blitz_pack_tpl(blitz_tpl *tpl,void **pack) {
/**********************************************************************************************************************/
    unsigned long pack_len = 0, pack_alloc_len = 0;
    unsigned long li = 0;
    unsigned int  ii = 0, ik = 0;
    unsigned long name_len = 0, header_len = 0, full_len = 0, i_len = 0;
    unsigned long node_len = 0, arg_len, full_len_shift = 0;
    void *i_pack = NULL;
    char tbyte = '\x0';

    if(!tpl) return 0;

    // header + body + structure: some initial predicted size
    li = 256 + tpl->body_len + 64*tpl->n_nodes;
    *pack = (void*)emalloc(li);

    if(!(*pack)) {
        php_error_docref(NULL TSRMLS_CC, E_ERROR,
            "INTERNAL: unable to allocate memory for the pack (%ld)", li
        );
        return 0;
    }

    i_pack = *pack;
    pack_alloc_len = li;

    // header: header_len, name_len, {name}, full_len, body_len, n_args
    name_len = strlen(tpl->name) + 1; // + 1 cause we save length without additional \x0

    i_len = tpl->body_len + 1;
    header_len = sizeof(unsigned int) + 4*sizeof(unsigned long) + name_len;
    full_len = 0;

    BLITZ_PACK_ADD(&header_len, sizeof(unsigned long)); 
    full_len_shift = pack_len; // will be used in the end to correct full length
    BLITZ_PACK_ADD(&full_len, sizeof(unsigned long)); // will be corrected
    BLITZ_PACK_ADD(&name_len, sizeof(unsigned long));
    BLITZ_PACK_ADD(tpl->name, name_len); 
    BLITZ_PACK_ADD(&i_len, sizeof(unsigned long));
    BLITZ_PACK_ADD(&tpl->n_nodes, sizeof(unsigned int));

    // body
    BLITZ_PACK_ADD(tpl->body, tpl->body_len);
    BLITZ_PACK_ADD(&tbyte,1); // tpl->body has additional buffer and doesn't end up with \x0 in body_len+1 position

    // nodes
    for (ii=0; ii<tpl->n_nodes; ii++) {
        tpl_node_struct *i_node = tpl->nodes + ii;
        // each node: node_pack_len, lexem_len, lexem, pos_begin, pos_end, type, n_args
        i_len = i_node->lexem_len + 1;
        node_len = i_len + 4*sizeof(unsigned long) + 2*sizeof(unsigned char);
        BLITZ_PACK_ADD(&node_len, sizeof(unsigned long));
        BLITZ_PACK_ADD(&i_len, sizeof(unsigned long));
        BLITZ_PACK_ADD(i_node->lexem, i_node->lexem_len);
        BLITZ_PACK_ADD(&tbyte,1);
        BLITZ_PACK_ADD(&i_node->pos_begin, sizeof(unsigned long));
        BLITZ_PACK_ADD(&i_node->pos_end, sizeof(unsigned long));
        BLITZ_PACK_ADD(&i_node->type, sizeof(unsigned char));
        BLITZ_PACK_ADD(&i_node->n_args, sizeof(unsigned char));

        // each arg (if any): arg_pack_len, name_len, name, type
        if (i_node->n_args>0) {
            for (ik=0; ik<i_node->n_args; ik++) {
                call_arg *i_arg = i_node->args + ik;
                i_len = i_arg->len + 1;
                arg_len = i_len + 2*sizeof(unsigned long) + sizeof(unsigned char);
                BLITZ_PACK_ADD(&arg_len, sizeof(unsigned long));
                BLITZ_PACK_ADD(&i_len, sizeof(unsigned long));
                BLITZ_PACK_ADD(i_arg->name, i_len);
                //BLITZ_PACK_ADD(i_arg->name, i_arg->len);
                //BLITZ_PACK_ADD(&tbyte,1);
                BLITZ_PACK_ADD(&i_arg->type, sizeof(unsigned char));
            }
        }
    } 

    // full_len correction in header
    memcpy(*pack + full_len_shift,&pack_len,sizeof(unsigned long)); 

    return pack_len;
}

// add template pack (single) to the whole blitz pack
/**********************************************************************************************************************/
int blitz_add_tpl_to_pack(blitz_tpl *tpl, blitz_pack *pack) {
/**********************************************************************************************************************/
    void *tpl_pack = NULL, *i_ptr = NULL;
    unsigned long tpl_pack_len = 0, tpl_name_len = 0;
    unsigned long old_tpl_pack_len = 0, new_tpl_pack_len = 0;
    unsigned long old_tpl_head_len = 0, new_tpl_head_len = 0;
    zval *temp;

    if(!tpl || !pack) return 0;

    // pack template data
    tpl_pack_len = blitz_pack_tpl(tpl,&tpl_pack); 
    if(!(tpl_pack_len>0)) {
        efree(tpl_pack);
        return 0;
    }

    // add template pack to current pack
    // new template header memory size: name + name_len(unsigned long) + shift(unsigned long)
    old_tpl_head_len = pack->tpl_head_len;
    tpl_name_len = strlen(tpl->name) + 1;
    new_tpl_head_len = old_tpl_head_len + 2*sizeof(unsigned long) + tpl_name_len;

    // new template pack memory size
    old_tpl_pack_len = pack->tpl_pack_len;
    new_tpl_pack_len = old_tpl_pack_len + tpl_pack_len;
    
    // realloc memory
    pack->tpl_head = erealloc(pack->tpl_head,new_tpl_head_len);
    pack->tpl_pack = erealloc(pack->tpl_pack,new_tpl_pack_len);

    if (!(pack->tpl_head && pack->tpl_pack)) {
        php_error_docref(NULL TSRMLS_CC, E_ERROR,
            "INTERNAL:unable to reallocate memory for the pack" 
        );
        return 0;
    }

    // add template to header
    i_ptr = pack->tpl_head + old_tpl_head_len;
    memcpy(i_ptr,(void*)&old_tpl_pack_len,sizeof(long));
    i_ptr += sizeof(long);
    memcpy(i_ptr,(void*)&tpl_name_len,sizeof(long));
    i_ptr += sizeof(long);
    memcpy(i_ptr,(void*)tpl->name,tpl_name_len);

    // add template to pack
    i_ptr = pack->tpl_pack + old_tpl_pack_len;
    memcpy(i_ptr,tpl_pack,tpl_pack_len);

    pack->tpl_pack_len = new_tpl_pack_len;
    pack->tpl_head_len = new_tpl_head_len;

    MAKE_STD_ZVAL(temp);
    ZVAL_LONG(temp, old_tpl_pack_len);
    zend_hash_update(pack->ht_tpl_shift, tpl->name, tpl_name_len,
       &temp, sizeof(zval *), NULL);

    pack->tpl_num++;

    efree(tpl_pack);

    return 1;
}

// debug function
/**********************************************************************************************************************/
int blitz_test_pack_full(blitz_pack *pack) {
/**********************************************************************************************************************/
    unsigned long i = 0, i_shift = 0, i_tpl_name_len = 0;
    char *i_tpl_name = NULL;
    void *i_ptr_head = NULL;
    zval **elem = NULL;
    HashPosition pointer;
    char *key = NULL;
    int key_len = 0;
    long index = 0;

    if(!pack) return 0;

    php_printf("BlitzPack: %lu templates\n",pack->tpl_num);

    i_ptr_head = pack->tpl_head;
    for(i=0; i<pack->tpl_num;i++) {
        i_shift = (unsigned long)(*(unsigned long*)i_ptr_head);
        i_ptr_head += sizeof(unsigned long);
        i_tpl_name_len = (unsigned long)(*(unsigned long*)i_ptr_head);
        i_ptr_head += sizeof(unsigned long);
        i_tpl_name = (char*)i_ptr_head;

        php_printf("template:%s, shift = %lu\n",i_tpl_name,i_shift);
        i_ptr_head += i_tpl_name_len;
    }     
    
    zend_hash_internal_pointer_reset_ex(pack->ht_tpl_shift, &pointer);
    while(zend_hash_get_current_data_ex(pack->ht_tpl_shift, (void**) &elem, &pointer) == SUCCESS) {
        zend_hash_get_current_key_ex(pack->ht_tpl_shift, &key, &key_len, &index, 0, &pointer);
        php_printf("key: %s, len:%d, shift:",key,key_len);
        php_var_dump(elem,1);
        zend_hash_move_forward_ex(pack->ht_tpl_shift, &pointer);
    }

}

// create template from pack using shift from header lookup
/**********************************************************************************************************************/
blitz_tpl *blitz_init_tpl_from_pack(blitz_pack *bp, unsigned long bp_shift TSRMLS_DC) {
/**********************************************************************************************************************/
    unsigned long i_len = 0, body_len = 0, name_len = 0, lex_len = 0;
    unsigned long shift = 0, i_shift = 0, n_nodes = 0, n_args = 0;
    unsigned long i = 0, j = 0;
    tpl_node_struct *i_node = NULL;
    call_arg *i_arg = NULL;
    blitz_tpl *tpl = NULL;
    void *pack = NULL;

    if(!(bp && bp->tpl_pack && bp_shift>=0)) return NULL;
    pack = bp->tpl_pack + bp_shift;

    tpl = blitz_init_tpl_base(NULL TSRMLS_CC);
    if(!tpl) return NULL;

    tpl->is_from_pack = 1;
    shift += 2*sizeof(unsigned long); // skip header_len, full_len
    name_len = BLITZ_UNPACK_ULONG(pack,shift);
    shift += sizeof(unsigned long);
    tpl->name = BLITZ_UNPACK_CHAR_P(pack,shift);
    shift += name_len;

    body_len = BLITZ_UNPACK_ULONG(pack,shift);
    shift += sizeof(unsigned long);
    tpl->body_len = body_len - 1;

    n_nodes = BLITZ_UNPACK_UINT(pack,shift);
    shift += sizeof(unsigned int);
    tpl->body = BLITZ_UNPACK_CHAR_P(pack,shift);

    shift += body_len; 
    tpl->nodes = (tpl_node_struct*)emalloc(n_nodes*sizeof(tpl_node_struct));
    tpl->n_nodes = n_nodes;

    // init nodes
    for (i=0; i<n_nodes; i++) {
        i_shift = shift;
        i_node = tpl->nodes + i;
        i_len = BLITZ_UNPACK_ULONG(pack,i_shift); 
        i_shift += sizeof(unsigned long);
        lex_len = BLITZ_UNPACK_ULONG(pack,i_shift);
        i_shift += sizeof(unsigned long);
        i_node->lexem_len = lex_len;
        i_node->lexem = BLITZ_UNPACK_CHAR_P(pack,i_shift);
        i_shift += lex_len;
        i_node->pos_begin = BLITZ_UNPACK_ULONG(pack,i_shift); 
        i_shift += sizeof(unsigned long);
        i_node->pos_end = BLITZ_UNPACK_ULONG(pack,i_shift);
        i_shift += sizeof(unsigned long);
        i_node->type = BLITZ_UNPACK_UCHAR(pack,i_shift);
        i_shift += sizeof(unsigned char);
        n_args = BLITZ_UNPACK_UCHAR(pack,i_shift); 
        i_shift += sizeof(unsigned char);
        i_node->n_args = n_args;
        i_node->args = NULL;
        i_node->has_error = 0;

        // init args
        if (n_args>0) {
            i_node->args = (call_arg*)emalloc(n_args*sizeof(call_arg));
            for (j=0; j<n_args; j++) {
                i_arg = i_node->args + j;
                i_len = BLITZ_UNPACK_ULONG(pack,i_shift);
                i_shift += sizeof(unsigned long);
                lex_len = BLITZ_UNPACK_ULONG(pack,i_shift);
                i_arg->len = lex_len - 1;
                i_shift += sizeof(unsigned long);
                i_arg->name = BLITZ_UNPACK_CHAR_P(pack,i_shift); 
                i_shift += lex_len;
                i_arg->type = BLITZ_UNPACK_UCHAR(pack,i_shift);
                i_shift += sizeof(unsigned char);
            }
        }

        i_node->children = NULL;
        i_node->n_children = 0;

        shift = i_shift;
    }

    tpl->pack = bp;
    return tpl;
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

/**********************************************************************************************************************/
inline static int blitz_exec_predefined_method (
/**********************************************************************************************************************/
    tpl_node_struct *node,
    HashTable *hash_globals,
    HashTable *iteration_params,
    zval *id,
    unsigned char **result,
    unsigned char **p_result,
    unsigned long *result_len,
    unsigned long *result_alloc_len TSRMLS_DC) {
/**********************************************************************************************************************/
    zval *retval = NULL, **z = NULL;
    unsigned long buf_len = 0, new_len = 0;
    HashTable *params = NULL;

    MAKE_STD_ZVAL(retval);
    ZVAL_FALSE(retval);

//php_blitz_dump_node(node, NULL);

    if (node->type == BLITZ_NODE_TYPE_IF) {
        char not_empty = 0;
        char i_arg = 0;
        BLITZ_ARG_NOT_EMPTY(node->args[0],iteration_params,not_empty);
//php_printf("%ld\n",not_empty);
        if(not_empty == -1) { // not found
            BLITZ_ARG_NOT_EMPTY(node->args[0],hash_globals,not_empty);
//php_printf("%ld\n",not_empty);
            if(not_empty == 1) { // first argument is not empty
                i_arg = 1;
            } else {
                if(node->n_args == 3) {  // empty && if has 3 arguments
                    i_arg = 2;
                } else { // empty && if has only 2 arguments
                    return 1; 
                }
            }
            params = hash_globals;
        } else {
            if(not_empty) { // first argument is not empty
                i_arg = 1;
            } else {
                if(node->n_args == 3) { // empty && if has 3 arguments
                    i_arg = 2;
                } else { // empty && if has only 2 arguments
                    return 1;
                }
            }
            params =  iteration_params;
        }

        if(node->args[i_arg].type & BLITZ_ARG_TYPE_VAR) { // argument is parameter
            if ((SUCCESS == zend_hash_find(iteration_params,node->args[i_arg].name,1+node->args[i_arg].len,(void**)&z))
                || (SUCCESS == zend_hash_find(hash_globals,node->args[i_arg].name,1+node->args[i_arg].len,(void**)&z)))
            {
                convert_to_string_ex(z);
                buf_len = Z_STRLEN_PP(z);
                BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,*p_result);
                *p_result = (char*)memcpy(*p_result,Z_STRVAL_PP(z),buf_len);
                *result_len += buf_len;
                p_result+=*result_len;
            }
        } else { // argument is string or number
            buf_len = (unsigned long)node->args[i_arg].len;
            BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,*p_result);
            *p_result = (char*)memcpy(*p_result,node->args[i_arg].name,buf_len);
            *result_len += buf_len;
            p_result+=*result_len;
        }
    } else if (node->type == BLITZ_NODE_TYPE_INCLUDE) {
        // this strategy is not optimized for those cases when a complex template 
        // is included more than once. all the times it will be opened and analysed, 
        // and it's better to use packs for these cases 
        char *filename = node->args[0].name;
        unsigned char *inner_result = NULL;
        unsigned long inner_result_len = 0;
        blitz_tpl *tpl = NULL;

        // initialize template 
        if(!(tpl = blitz_init_tpl(filename, hash_globals TSRMLS_CC))) {
            blitz_free_tpl(tpl);    
            return 0;
        }

        // analyse template 
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

/**********************************************************************************************************************/
inline static int blitz_exec_user_method (
/**********************************************************************************************************************/
    tpl_node_struct *node,
    HashTable *hash_globals,
    HashTable *iteration_params,
    zval  *obj,
    unsigned char **result,
    unsigned char **p_result,
    unsigned long *result_len,
    unsigned long *result_alloc_len TSRMLS_DC) {
/**********************************************************************************************************************/
    zval *retval = NULL, *z_method = NULL, **z_param = NULL;
    unsigned int method_res = 0;
    unsigned long buf_len = 0, new_len = 0;
    zval ***args = NULL; 
    zval **pargs = NULL;
    unsigned int i = 0;
    call_arg *i_arg = NULL;
    char i_arg_type = 0;
   
    MAKE_STD_ZVAL(retval);
    ZVAL_FALSE(retval);
    MAKE_STD_ZVAL(z_method);
    ZVAL_STRING(z_method,node->lexem,1);

    // prepare arguments if needed
    if(node->n_args>0 && node->args) {
        // dirty trick to pass arguments
        // pargs are zval ** with parameter data
        // args just point to pargs
        args = emalloc(node->n_args*sizeof(zval*));
        pargs = emalloc(node->n_args*sizeof(zval));
        for(i=0; i<node->n_args; i++) {
            args[i] = NULL;
            MAKE_STD_ZVAL(pargs[i]);
            i_arg  = &(node->args[i]);
            i_arg_type = i_arg->type;
            if (i_arg_type == BLITZ_ARG_TYPE_VAR) {
                // fetch variable and bind to the argument
                if ((SUCCESS == zend_hash_find(iteration_params,node->args[i].name,1+node->args[i].len,(void**)&z_param))
                    || (SUCCESS == zend_hash_find(hash_globals,node->args[i].name,1+node->args[i].len,(void**)&z_param)))
                {
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
        z_method, &retval, node->n_args, args, 1, NULL TSRMLS_CC
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
         for(i=0; i<node->n_args; i++) {
             zval_dtor(pargs[i]);
         }
         efree(args);
         efree(pargs);
    }

    return 1;
}

/**********************************************************************************************************************/
static int blitz_exec_nodes (
/**********************************************************************************************************************/
    blitz_tpl *tpl, 
    tpl_node_struct *nodes,
    unsigned int n_nodes,
    zval *id, 
    unsigned char **result,
    unsigned long *result_len, 
    unsigned long *result_alloc_len,
    unsigned long parent_begin,
    unsigned long parent_end,
    zval *parent_ctx_data TSRMLS_DC) {
/**********************************************************************************************************************/
    unsigned int i = 0, i_max = 0, i_processed = 0;;
    unsigned char *p_result = NULL;
    unsigned long new_len = 0;
    unsigned long shift = 0, buf_len = 0, last_close = 0, current_open = 0;
    unsigned long l_open_node = tpl->config->l_open_node, l_close_node = tpl->config->l_close_node;
    zval **zparam;
    tpl_node_struct *node;

    p_result = *result;
    shift = 0;
    //last_close = (parent_begin>0) ? parent_begin : *result_len;
    last_close = parent_begin;

    // walk through nodes, build result for each, 1-level sub-node, skip N-level ones (node->pos_begin<last_close)
    i = 0;
    i_processed = 0;
    i_max = tpl->n_nodes;
    
//php_printf("i_max:%ld, n_nodes = %ld\n",i_max,n_nodes);

//    while((i<i_max) && (i_processed<n_nodes)) {
//    for(i=0; i<n_nodes; ++i) {
    for(; i<i_max && i_processed<n_nodes; ++i) {
        node = nodes + i;
//php_printf("node:%s, %ld/%ld pos = %ld, lc = %ld\n",node->lexem, i, n_nodes, node->pos_begin, last_close);
        if (node->pos_begin<last_close) { // not optimal: just fix for node-tree
//php_printf("SKIPPED(%ld) %ld\n", i, tpl->n_nodes);
            continue;
        }
        current_open = node->pos_begin;

        // between nodes
        if (current_open>last_close) {
            buf_len = current_open - last_close;
            BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,p_result);
            p_result = (char*)memcpy(p_result, tpl->body + last_close, buf_len); 
            *result_len += buf_len;
            p_result+=*result_len;
        }

//php_var_dump(&parent_ctx_data, 0);

        if (node->lexem && !node->has_error) {
            if (BLITZ_IS_VAR(node->type)) {  // search: in current context, then in global params
                if(
                    (parent_ctx_data && (SUCCESS == zend_hash_find(Z_ARRVAL_P(parent_ctx_data),node->lexem, strlen(node->lexem)+1, (void**)&zparam)))
                    ||
                    (tpl->hash_globals && (SUCCESS == zend_hash_find(tpl->hash_globals,node->lexem, strlen(node->lexem)+1, (void**)&zparam)))
                ){

                    if(Z_TYPE_PP(zparam) != IS_STRING) { 
                        zval *p = NULL;
                        MAKE_STD_ZVAL(p);
                        *p = **zparam;
                        zval_copy_ctor(p);
                        convert_to_string_ex(&p);
                        buf_len = Z_STRLEN_P(p);
                        BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,p_result);
                        p_result = (char*)memcpy(p_result,Z_STRVAL_P(p), buf_len);
                    } else {
                       buf_len = Z_STRLEN_PP(zparam);
                       BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,p_result);
                       p_result = (char*)memcpy(p_result,Z_STRVAL_PP(zparam), buf_len);
                    }

                    /* old - with conversion
                       but we cannot just convert zparam here - because this will convert data itself, not a local copy.
                       data can be used many times, and if we convert it we will have bugs: if-operators on int(0) 
                       will not work on string("0"). 2DO: tets it

                    convert_to_string_ex(zparam);
                    buf_len = Z_STRLEN_PP(zparam);
                    BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,p_result);
                    p_result = (char*)memcpy(p_result,Z_STRVAL_PP(zparam), buf_len);

                    */

                    *result_len += buf_len;
                    p_result+=*result_len;

                }
            }
            else if (BLITZ_IS_METHOD(node->type)) {
                if(node->type == BLITZ_NODE_TYPE_CONTEXT) {
//php_printf("context:%s\n",node->args->name);
                    zval **ctx_iterations = NULL;
                    zval **ctx_data = NULL;
                    HashPosition pointer = NULL;  
                    call_arg *arg = node->args;
                    if(parent_ctx_data && (zend_hash_find(Z_ARRVAL_P(parent_ctx_data), arg->name, 1 + arg->len, (void**)&ctx_iterations) != FAILURE)) {
                        if(Z_TYPE_PP(ctx_iterations) == IS_ARRAY) {
                            tpl_node_struct *subnodes = NULL;
                            unsigned int n_subnodes = 0;
                            if(node->children) {
                                subnodes = *node->children;
                                n_subnodes = node->n_children;
                            }
                            for(
                                zend_hash_internal_pointer_reset_ex(Z_ARRVAL_PP(ctx_iterations), &pointer); 
                                zend_hash_get_current_data_ex(Z_ARRVAL_PP(ctx_iterations), (void**) &ctx_data, &pointer) == SUCCESS; 
                                zend_hash_move_forward_ex(Z_ARRVAL_PP(ctx_iterations), &pointer)) 
                            {
// 2DO: проверить что это нормальный массив
//php_printf("GO subnode, params:\n");
//php_var_dump(ctx_data,0);
                                blitz_exec_nodes(tpl,subnodes,n_subnodes,id,
                                    result,result_len,result_alloc_len,
                                    node->pos_begin_shift,
                                    node->pos_end_shift,
                                    *ctx_data
                                );
                            }
                        }
                    }
                } else {
                    HashTable *iteration_params = parent_ctx_data ? Z_ARRVAL_P(parent_ctx_data) : NULL;
                    if(BLITZ_IS_PREDEF_METHOD(node->type)) {
                        blitz_exec_predefined_method(
                            node,tpl->hash_globals,iteration_params,id,
                            result,&p_result,result_len,result_alloc_len TSRMLS_CC
                        );
                    } else {
                        blitz_exec_user_method(
                            node,tpl->hash_globals,iteration_params,id,
                            result,&p_result,result_len,result_alloc_len TSRMLS_CC
                        );
                    }
                }
            }
        } 
        last_close = node->pos_end + l_close_node;
        ++i_processed;
    }

//php_printf("D:b3  %ld,%ld,%ld\n",last_close,parent_begin,parent_end);
    if(parent_end>last_close) {
        buf_len = parent_end - last_close;
        BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,p_result);
        p_result = (char*)memcpy(p_result, tpl->body + last_close, buf_len);
        *result_len += buf_len;
        p_result+=*result_len;
    }

//php_printf("END NODES:\n");

    return 1;
}

/**********************************************************************************************************************/
static int blitz_exec_template (
/**********************************************************************************************************************/
    blitz_tpl *tpl,
    zval *id,
    unsigned char **result,
    unsigned long *result_len TSRMLS_DC) {
/**********************************************************************************************************************/
    unsigned long result_alloc_len = 0;

    // quick return if there was no nodes
    if(0 == tpl->n_nodes) {
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

    if(tpl->iterations) {
        zval **iteration_data = NULL;
        HashPosition pointer = NULL;

        // если это не числовой массив то считать его одной итерацией - и передать в качестве параметра, 
        // если это числовой массив то пройтись итерациями по всем элементам 
        char *key;
        unsigned int key_len;
        unsigned long key_index;
        zend_hash_internal_pointer_reset_ex(Z_ARRVAL_P(tpl->iterations), &pointer);
        if(HASH_KEY_IS_LONG != zend_hash_get_current_key_ex(Z_ARRVAL_P(tpl->iterations), &key, &key_len, &key_index, 0, NULL)) {
           blitz_exec_nodes(tpl,tpl->nodes,tpl->n_nodes,id,result,result_len,&result_alloc_len,0,tpl->body_len,tpl->iterations);
        } else {
           while(zend_hash_get_current_data_ex(Z_ARRVAL_P(tpl->iterations), (void**) &iteration_data, &pointer) == SUCCESS) {
               if(
                   HASH_KEY_IS_LONG != zend_hash_get_current_key_ex(Z_ARRVAL_P(tpl->iterations), &key, &key_len, &key_index, 0, NULL)
                   || IS_ARRAY != Z_TYPE_PP(iteration_data)) 
               {
                   continue;
               }
               blitz_exec_nodes(tpl,tpl->nodes,tpl->n_nodes,id,result,result_len,&result_alloc_len,0,tpl->body_len,*iteration_data);
               zend_hash_move_forward_ex(Z_ARRVAL_P(tpl->iterations), &pointer);
           }
        }
    } else {
        blitz_exec_nodes(tpl,tpl->nodes,tpl->n_nodes,id,result,result_len,&result_alloc_len,0,tpl->body_len,NULL);
    }

    return 1;
}

/**********************************************************************************************************************/
inline static int blitz_normalize_path(char **dest, char *local, int local_len, char *global, int global_len) {
/**********************************************************************************************************************/
    int buf_len = 0;
    char *buf = *dest;
    register char  *p = NULL, *q = NULL;

    if(local_len && local[0] == '/') {
        if(local_len+1>BLITZ_CONTEXT_PATH_MAX_LEN) {
            php_error_docref(NULL TSRMLS_CC, E_ERROR,"context path %s is too long (limit %d)",local,BLITZ_CONTEXT_PATH_MAX_LEN);
            return 0;
        }
        memcpy(buf, local, local_len+1);
        buf_len = local_len;
    } else {
        if(local_len + global_len + 2 > BLITZ_CONTEXT_PATH_MAX_LEN) {
            php_error_docref(NULL TSRMLS_CC, E_ERROR,"context path %s is too long (limit %d)",local,BLITZ_CONTEXT_PATH_MAX_LEN);
            return 0;
        }

        memcpy(buf, global, global_len);
        buf[global_len] = '/';
        buf_len = 1 + global_len;
        if(local) {
            memcpy(buf + 1 + global_len, local, local_len + 1);
            buf_len += local_len;
        }
    }

    buf[buf_len] = '\x0';
    while((p = strstr(buf, "//"))) {
        for(q = p+1; *q; q++) *(q-1) = *q;
        *(q-1) = 0;
        --buf_len;
    }

    /* check for `..` in the path */
    /* first, remove path elements to the left of `..` */
    for(p = buf; p <= (buf+buf_len-3); p++) {
        if(memcmp(p, "/..", 3) != 0 || (*(p+3) != '/' && *(p+3) != 0)) continue;
        for(q = p-1; q >= buf && *q != '/'; q--, buf_len--);
        --buf_len;
        if(*q == '/') {
            p += 3;
            while(*p) *(q++) = *(p++);
            *q = 0;
            buf_len -= 3;
            p = buf;
        }
    }

    /* second, clear all `..` in the begining of the path
       because `/../` = `/` */
    while(buf_len > 2 && memcmp(buf, "/..", 3) == 0) {
        for(p = buf+3; *p; p++) *(p-3) = *p;
        *(p-3) = 0;
        buf_len -= 3;
    }

    /* clear `/` at the end of the path */
    while(buf_len > 1 && buf[buf_len-1] == '/') buf[--buf_len] = 0;
    if(!buf_len) { memcpy(buf, "/", 2); buf_len = 1; }

    for(p=buf; *p; p++) *p = tolower(*p);
    buf[buf_len] = '\x0';

    return 1;
}


/**********************************************************************************************************************/
inline int blitz_iterate_by_path(blitz_tpl *tpl, char *path, int path_len, int is_current_iteration) {
/**********************************************************************************************************************/
    zval **tmp;
    int i = 1, ilast = 1, j = 0, k = 0;
    char *p = path;
    int pmax = path_len;
    char key[BLITZ_CONTEXT_PATH_MAX_LEN];
    int key_len  = 0;
    int found = 1;
    int res = 0;
    k = pmax - 1;
    tmp = &tpl->iterations;

    // check root
    if(*p == '/' && pmax == 1) {
        zval *empty_array;
        MAKE_STD_ZVAL(empty_array);
        array_init(empty_array);
        add_next_index_zval(*tmp, empty_array);
        zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &tpl->current_iteration);
        if(is_current_iteration)
            tpl->current_iteration_node = & tpl->iterations;
        return 1;
    }

    *p++;
    while(i<pmax) {
        if(*p == '/' || i == k) {
            j = i - ilast;
            key_len = j + (i == k ? 1 : 0);
            memcpy(key, p-j, key_len);
            key[key_len] = '\x0';

            zend_hash_internal_pointer_end(Z_ARRVAL_PP(tmp));
            if(SUCCESS != zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &tmp)) {
                zval *empty_array;
                MAKE_STD_ZVAL(empty_array);
                array_init(empty_array);
                add_next_index_zval(*tmp,empty_array);
                if(SUCCESS != zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &tmp)) {
                    // error ???
                }
            }

            if(SUCCESS != zend_hash_find(Z_ARRVAL_PP(tmp),key,key_len+1,(void **)&tmp)) {
                found = 0;
                // create
                zval *empty_array;
                MAKE_STD_ZVAL(empty_array);
                array_init(empty_array);

                zval *init_array;
                MAKE_STD_ZVAL(init_array);
                array_init(init_array);
                // [] = array(0 => array())
                add_assoc_zval_ex(*tmp, key, key_len+1, init_array);
                add_next_index_zval(init_array,empty_array);
                zend_hash_internal_pointer_end(Z_ARRVAL_PP(tmp));
                if(is_current_iteration) {
                    res = zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &tpl->current_iteration_node);
                }

                if(SUCCESS != zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &tmp)) {
                    // error ???
                }
            }
            ilast = i + 1;
        }
        ++p;
        ++i;
    }

    if(found) {
        zval *empty_array;
        MAKE_STD_ZVAL(empty_array);
        array_init(empty_array);

        add_next_index_zval(*tmp, empty_array);
        zend_hash_internal_pointer_end(Z_ARRVAL_PP(tmp));
    }

    zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &tpl->current_iteration);

    return 1;
}

/**********************************************************************************************************************/
int blitz_find_iteration_by_path(blitz_tpl *tpl, char *path, int path_len, zval **iteration) {
/**********************************************************************************************************************/
    zval **tmp, **entry;
    int i = 1, ilast = 1, j = 0, k = 0;
    char *p = path;
    int pmax = path_len;
    char key[BLITZ_CONTEXT_PATH_MAX_LEN];
    int key_len  = 0;
    k = pmax - 1;
    tmp = & tpl->iterations;

    // check root
//php_printf("D:-0 %s(%ld)\n", path, path_len);

    if(*p == '/' && pmax == 1) {
        iteration = &tpl->iterations;
        return 1;
    }

    if(i>=pmax) {
        return 0;
    }

    *p++;
//php_printf("%d/%d\n", i, pmax);
    while(i<pmax) {
//php_printf("%d/%d\n", i, pmax);
        if(*p == '/' || i == k) {
            j = i - ilast;
            key_len = j + (i == k ? 1 : 0);
            memcpy(key, p-j, key_len);
            key[key_len] = '\x0';
//php_printf("key:%s\n", key);

            zend_hash_internal_pointer_end(Z_ARRVAL_PP(tmp));
            
            if(SUCCESS != zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &entry)) {
                return 0;
            }

//php_var_dump(entry, 0);
            if(SUCCESS != zend_hash_find(Z_ARRVAL_PP(entry),key,key_len+1,(void **) &tmp)) {
                return 0;
            }
//php_var_dump(tmp, 0);

            ilast = i + 1;
        }
        ++p;
        ++i;
    }

    zend_hash_internal_pointer_end(Z_ARRVAL_PP(tmp));
    zval **dummy;
    if(SUCCESS == zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &dummy)) {
//php_printf("%p %p %p %p\n", dummy, *dummy, iteration, *iteration);
        *iteration = *dummy;
    } else {
        return 0;
    }

//php_printf("%p %p %p %p\n", dummy, *dummy, iteration, *iteration);
//php_var_dump(iteration,0);

    return 1;
}


/**********************************************************************************************************************/
inline int blitz_iterate_current(blitz_tpl *tpl) {
/**********************************************************************************************************************/
    zval *empty_array = NULL;

    MAKE_STD_ZVAL(empty_array);
    array_init(empty_array);

    if(!tpl->current_iteration_node) {
        blitz_iterate_by_path(tpl, tpl->current_path, strlen(tpl->current_path), 1);
    } else {
        MAKE_STD_ZVAL(empty_array);
        array_init(empty_array);
        add_next_index_zval(*tpl->current_iteration_node, empty_array);
        zend_hash_internal_pointer_end(Z_ARRVAL_PP(tpl->current_iteration_node));
        zend_hash_get_current_data(Z_ARRVAL_PP(tpl->current_iteration_node), (void **) &tpl->current_iteration);
    }

    return 1;
}

/**********************************************************************************************************************/
void blitz_build_fetch_index_node(blitz_tpl *tpl, tpl_node_struct *node, char *path, unsigned int path_len) {
/**********************************************************************************************************************/
    unsigned long j = 0;
    unsigned int current_path_len = 0;
    char current_path[1024] = "";
    char *lexem = NULL;
    unsigned int lexem_len = 0;

    if (!node) return;
    if(path_len>0) memcpy(current_path,path,path_len);
    if(node->type == BLITZ_NODE_TYPE_CONTEXT) {
        lexem = node->args[0].name;
        lexem_len = node->args[0].len;
    } else if(node->type == BLITZ_TYPE_VAR) {
        lexem = node->lexem;
        lexem_len = node->lexem_len;
    } else {
        return;
    }

    memcpy(current_path + path_len,"/",1);
    memcpy(current_path + path_len + 1, lexem, lexem_len);

    current_path_len = strlen(current_path);
    current_path[current_path_len] = '\x0';

    zval *temp;
    MAKE_STD_ZVAL(temp);
    ZVAL_LONG(temp, node->pos_in_list);
    zend_hash_update(tpl->fetch_index, current_path, current_path_len+1, &temp, sizeof(zval *), NULL);

    if(node->children) {
        for (j=0;j<node->n_children;++j) {
           blitz_build_fetch_index_node(tpl, node->children[j], current_path, current_path_len);
        }
    }
}

/*********************************************************************************************************************/
int blitz_build_fetch_index(blitz_tpl *tpl) {
/**********************************************************************************************************************/
    unsigned long i = 0, last_close = 0;
    char path[1024] = "";
    unsigned int path_len = 0;

    ALLOC_HASHTABLE(tpl->fetch_index);
    if(!tpl->fetch_index || (FAILURE == zend_hash_init(tpl->fetch_index, 8, NULL, ZVAL_PTR_DTOR, 0))) {
        php_error_docref(NULL TSRMLS_CC, E_ERROR, "INTERNAL: unable to allocate or fill memory for blitz fetch index");
        return 0;
    }

    for (i=0;i<tpl->n_nodes;i++) {
        if(tpl->nodes[i].pos_begin>=last_close) {
            blitz_build_fetch_index_node(tpl, tpl->nodes + i, path, path_len);
            last_close = tpl->nodes[i].pos_end;
        }
    }

// test
/*
php_printf("fetch_index dump:\n");

    HashTable *ht = tpl->fetch_index;
    HashPosition pointer;
    zend_hash_internal_pointer_reset_ex(ht, &pointer);
    zval *elem = NULL;
    char *key = NULL;
    int key_len = 0;
    long index = 0;

    while(zend_hash_get_current_data_ex(ht, (void**) &elem, &pointer) == SUCCESS) {
        if(zend_hash_get_current_key_ex(ht, &key, &key_len, &index, 0, &pointer) != HASH_KEY_IS_STRING) {
            zend_hash_move_forward_ex(ht, &pointer);
            continue;
        }
php_printf("key: %s\n", key);
php_var_dump(elem,0);
        zend_hash_move_forward_ex(ht, &pointer);
    }
*/
    return 1;
}

/**********************************************************************************************************************/
int blitz_fetch_node_by_path(
/**********************************************************************************************************************/
    blitz_tpl *tpl, 
    zval *id, 
    char *path, 
    unsigned int path_len, 
    zval *input_params,
    unsigned char **result, 
    unsigned long *result_len TSRMLS_DC) {
/**********************************************************************************************************************/
    tpl_node_struct *i_node = NULL;
    unsigned long result_alloc_len = 0;
    zval **z = NULL;

//php_var_dump(&input_params,0);

    if((path[0] == '/') && (path_len == 1)) {
        return blitz_exec_template(tpl,id,result,result_len TSRMLS_CC);
    }

    if(!(tpl->flags & BLITZ_FLAG_FETCH_INDEX_BUILT)) {
        if(blitz_build_fetch_index(tpl)) {
            tpl->flags |= BLITZ_FLAG_FETCH_INDEX_BUILT;
        } else {
            return 0;
        }
    }

    // find node by path  
    if (SUCCESS == zend_hash_find(tpl->fetch_index,path,path_len+1,(void**)&z)) {
        i_node = tpl->nodes + Z_LVAL_PP(z);
    } else {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,"cannot find context %s in template %s", path, tpl->name);
        return 0;
    }

    // fetch result
    if(i_node->children) {
        result_alloc_len = 2*(i_node->pos_end_shift - i_node->pos_begin_shift);
        *result = (char*)ecalloc(result_alloc_len,sizeof(char));
        return blitz_exec_nodes(tpl,*i_node->children,i_node->n_children,id,
            result,result_len,&result_alloc_len,i_node->pos_begin_shift,i_node->pos_end_shift,
            input_params
        );
//php_printf("result(%ld):\n+%s+\n",*result_len,*result);
    } else {
        unsigned long rlen = i_node->pos_end_shift - i_node->pos_begin_shift;
        *result_len = rlen;
        *result = (char*)emalloc(rlen+1);
        if(!*result)
            return 0;
        if(!memcpy(*result, tpl->body + i_node->pos_begin_shift, rlen)) {
            return 0;
        }
        *(*result + rlen) = '\x0';
//php_printf("%ld, %ld, +%s+\n", i_node->pos_begin_shift, i_node->pos_end, *result);
    }

    return 1;
}

/**********************************************************************************************************************
* Blitz CLASS methods
**********************************************************************************************************************/

/* {{{ blitz_init(filename) */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_init) {
/**********************************************************************************************************************/
    blitz_tpl *tpl = NULL;
    blitz_pack *pack = NULL;
    unsigned int filename_len = 0;
    char *filename = NULL;
    zval *rsrc_pack = NULL;
    zval **desc = NULL;

    zval *new_object = NULL;
    int ret = 0;
    unsigned long shift_in_pack = 0;
    char found_in_pack = 0;

    if(FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"|sr",&filename,&filename_len,&rsrc_pack)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    // extract pack & check if parsed file exists in pack already
    if (filename && rsrc_pack) {
        ZEND_FETCH_RESOURCE(pack, blitz_pack *, &rsrc_pack, -1, "blitz pack", le_blitz_pack);
        if (pack 
            && pack->ht_tpl_shift
            && (SUCCESS == zend_hash_find(pack->ht_tpl_shift,filename,filename_len+1,(void **)&desc))) 
        {
            shift_in_pack = Z_LVAL_PP(desc);
            found_in_pack = 1;
        }
    }

    // if found in pack - init with pack data, otherwise read file
    if (found_in_pack == 1) {
        if(!(tpl = blitz_init_tpl_from_pack(pack, shift_in_pack TSRMLS_CC))) {
            blitz_free_tpl(tpl);
            RETURN_FALSE;
        }
    } else {
        // initialize template 
        if(!(tpl = blitz_init_tpl(filename, NULL TSRMLS_CC))) {
            blitz_free_tpl(tpl);    
            RETURN_FALSE;
        }

        if(filename) {
            // analyse template 
            if(!blitz_analyse(tpl TSRMLS_CC)) {
                blitz_free_tpl(tpl);
                RETURN_FALSE;
            }
        }
    }

    MAKE_STD_ZVAL(new_object);

    if(object_init(new_object) != SUCCESS)
    {
        RETURN_FALSE;
    }

    ret = zend_list_insert(tpl, le_blitz);
    add_property_resource(getThis(), "tpl", ret);
    zend_list_addref(ret);

}
/* }}} */

/* {{{ blitz_load */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_load) {
/**********************************************************************************************************************/
    blitz_tpl *tpl = NULL;
    zval *body;
    zval *id = NULL;
    zval **desc = NULL;

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (zend_hash_find(Z_OBJPROP_P(id), "tpl", sizeof("tpl"), (void **)&desc) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "INTERNAL:cannot find template descriptor (object->tpl resource)"
        );
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "blitz template", le_blitz);


    if(FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"z",&body)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if(tpl->body) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,"INTERNAL: template is already loaded");
        RETURN_FALSE;
    }

    // load body
    if(!blitz_load_body(tpl, body TSRMLS_CC)) {
        blitz_free_tpl(tpl);
        RETURN_FALSE;
    }

    // analyse template
    if(!blitz_analyse(tpl TSRMLS_CC)) {
        blitz_free_tpl(tpl);
        RETURN_FALSE;
    }

    RETURN_TRUE;
}
/* }}} */


// debug functions
/* {{{ blitz_dump_set() */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_dump_set) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }
    
    if (zend_hash_find(Z_OBJPROP_P(id), "tpl", sizeof("tpl"), (void **)&desc) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, 
            "INTERNAL:cannot find template descriptor (object->tpl resource)"
        );
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "blitz template", le_blitz);
    php_blitz_dump_data(tpl->hash_globals);

    RETURN_TRUE;
}
/* }}} */

/**********************************************************************************************************************/
PHP_FUNCTION(blitz_dump_struct) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (zend_hash_find(Z_OBJPROP_P(id), "tpl", sizeof("tpl"), (void **)&desc) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "INTERNAL:cannot find template descriptor (object->tpl resource)"
        );
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "blitz template", le_blitz);
    php_blitz_dump_struct(tpl);

    RETURN_TRUE;
}
/* }}} */

/**********************************************************************************************************************/
PHP_FUNCTION(blitz_dump_iterations) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (zend_hash_find(Z_OBJPROP_P(id), "tpl", sizeof("tpl"), (void **)&desc) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "INTERNAL:cannot find template descriptor (object->tpl resource)"
        );
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "blitz template", le_blitz);

    zval **dummy;
    dummy = & tpl->iterations;
    php_var_dump(dummy,0);

    RETURN_TRUE;
}

/* {{{ blitz_set_global(zend_array(node_key=>node_val)) */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_set_global) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;
    zval *input_arr = NULL, **elem;
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
            "INTERNAL:cannot find template descriptor (object->tpl resource)"
        );
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "blitz template", le_blitz);

    if(FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"a",&input_arr)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    input_ht = Z_ARRVAL_P(input_arr);
    zend_hash_internal_pointer_reset_ex(tpl->hash_globals, &pointer);
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
                    tpl->hash_globals,
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
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_parse) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl;
    unsigned char *result = NULL;
    unsigned long result_len = 0;
    zval *input_arr = NULL;
    zval      **src_entry = NULL;
    char      *string_key = NULL;
    unsigned int      string_key_len = 0;
    unsigned long     num_key = 0;
    HashPosition pos;
    HashTable *input_ht = NULL;
    char exec_status = 0;

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (zend_hash_find(Z_OBJPROP_P(id), "tpl", sizeof("tpl"), (void **)&desc) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "INTERNAL:cannot find template descriptor (object::tpl resource)"
        );
        RETURN_FALSE;
    }

    if(FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"|z",&input_arr)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    } 

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "blitz template", le_blitz);
    if(!tpl) {
        RETURN_FALSE;
    }

    if(input_arr && (IS_ARRAY != Z_TYPE_P(input_arr))) {
        input_arr = NULL;
    }

    // empty template (data was not loaded): return empty string
    if(!tpl->body) {
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
                zend_hash_update(tpl->hash_globals, string_key, string_key_len,
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
        RETURN_FALSE;
    }

    return;
}



/* {{ blitz_context() */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_context) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;
    char *path = NULL;
    int current_len = 0, norm_len = 0, path_len = 0, res = 0;

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (zend_hash_find(Z_OBJPROP_P(id), "tpl", sizeof("tpl"), (void **)&desc) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "INTERNAL:cannot find template descriptor (object::tpl resource)"
        );
        RETURN_FALSE;
    }

    if(FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"s",&path,&path_len)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "blitz template", le_blitz);
    if(!tpl) {
        RETURN_FALSE;
    }

    current_len = strlen(tpl->current_path);
    if(path && path_len == current_len && 0 == strncmp(path,tpl->current_path,path_len)) {
        RETURN_TRUE;
    }

    norm_len = 0;
    res = blitz_normalize_path(&tpl->path_buf, path, path_len, tpl->current_path, current_len);
    if(res) {
        norm_len = strlen(tpl->path_buf);
    }

    if((current_len != norm_len) || (0 != strncmp(tpl->path_buf,tpl->current_path,norm_len))) {
        tpl->current_iteration_node = NULL;
        tpl->current_iteration = NULL;
    }

    tpl->current_path = estrndup(tpl->path_buf,norm_len);
//php_printf("current_path = %s\n",tpl->current_path);

    RETURN_TRUE;
}

/* {{ blitz_iterate() */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_iterate) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;
    char *path = NULL;
    int path_len = 0;
    int res = 0;

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (zend_hash_find(Z_OBJPROP_P(id), "tpl", sizeof("tpl"), (void **)&desc) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "INTERNAL:cannot find template descriptor (object::tpl resource)"
        );
        RETURN_FALSE;
    }

    if(FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"|s",&path,&path_len)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "blitz template", le_blitz);
    if(!tpl) {
        RETURN_FALSE;
    }


    if((path_len == 0) && tpl->current_iteration_node) {
        res = blitz_iterate_current(tpl);
    } else {
        int current_len = strlen(tpl->current_path);
        int norm_len = 0;
        res = blitz_normalize_path(&tpl->path_buf, path, path_len, tpl->current_path, current_len);
        norm_len = strlen(tpl->path_buf);

        if(tpl->current_iteration_node
            && (current_len == norm_len)
            && (0 == strncmp(tpl->path_buf,tpl->current_path,norm_len)))
        {
            res = blitz_iterate_current(tpl);
        } else {
            tpl->current_iteration_node = NULL;
            res = blitz_iterate_by_path(tpl, tpl->path_buf, norm_len, 0);
        }
    }

    if(res) {
       RETURN_TRUE;
    }

    RETURN_FALSE;
}

/* {{ blitz_set() */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_set) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl;
    zval *input_arr = NULL, **elem;
    HashTable *input_ht = NULL;
    HashPosition pointer;
    char *key = NULL;
    int key_len = 0;
    long index = 0;
    int res = 0;

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (zend_hash_find(Z_OBJPROP_P(id), "tpl", sizeof("tpl"), (void **)&desc) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "INTERNAL:cannot find template descriptor (object::tpl resource)"
        );
        RETURN_FALSE;
    }

    if(FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"a",&input_arr)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "blitz template", le_blitz);
    if(!tpl) {
        RETURN_FALSE;
    }

    if(!tpl->current_iteration) {
        blitz_iterate_by_path(tpl, tpl->current_path, strlen(tpl->current_path), 1);
    }

    input_ht = Z_ARRVAL_P(input_arr);
    zend_hash_internal_pointer_reset_ex(Z_ARRVAL_PP(tpl->current_iteration), &pointer);
    zend_hash_internal_pointer_reset_ex(input_ht, &pointer);

    while(zend_hash_get_current_data_ex(input_ht, (void**) &elem, &pointer) == SUCCESS) {
        if(zend_hash_get_current_key_ex(input_ht, &key, &key_len, &index, 0, &pointer) != HASH_KEY_IS_STRING) {
            zend_hash_move_forward_ex(input_ht, &pointer);
            continue;
        }

        if(key) {
            zval *temp;
            ALLOC_INIT_ZVAL(temp);
            *temp = **elem;
            zval_copy_ctor(temp);

            res = zend_hash_update(
                Z_ARRVAL_PP(tpl->current_iteration),
                key,
                key_len,
                (void*)&temp, sizeof(zval *), NULL
            );
        }
        zend_hash_move_forward_ex(input_ht, &pointer);
    }

    RETURN_TRUE;
}



/* {{{ blitz_block() */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_block) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl;
    zval *input_arr = NULL;
    char *path = NULL;
    int path_len = 0;
    int res = 0;

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (zend_hash_find(Z_OBJPROP_P(id), "tpl", sizeof("tpl"), (void **)&desc) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "INTERNAL:cannot find template descriptor (object::tpl resource)"
        );
        RETURN_FALSE;
    }

    if(FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"s|z",&path,&path_len,&input_arr)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "blitz template", le_blitz);
    if(!tpl) {
        RETURN_FALSE;
    }

    if(input_arr && (IS_ARRAY != Z_TYPE_P(input_arr))) {
        input_arr = NULL;
    }

    if((path_len == 0) && tpl->current_iteration_node) {
        res = blitz_iterate_current(tpl);
    } else {
        int current_len = strlen(tpl->current_path);
        int norm_len = 0;
        res = blitz_normalize_path(&tpl->path_buf, path, path_len, tpl->current_path, current_len);
        norm_len = strlen(tpl->path_buf);

        if(tpl->current_iteration_node
            && (current_len == norm_len)
            && (0 == strncmp(tpl->path_buf,tpl->current_path,norm_len)))
        {
            res = blitz_iterate_current(tpl);
        } else {
            tpl->current_iteration_node = NULL;
            res = blitz_iterate_by_path(tpl, tpl->path_buf, norm_len, 0);
        }
    }

    if(!res) 
        RETURN_FALSE;

    // copy params array to current iteration
    if(input_arr) {
       **(tpl->current_iteration) = *input_arr;
       zval_copy_ctor(*tpl->current_iteration);
    }

    RETURN_TRUE;
}

/* {{{ blitz_incude(filename,params) */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_include) {
/**********************************************************************************************************************/
    blitz_tpl *tpl = NULL, *itpl = NULL;
    char *filename = NULL;
    int filename_len = 0;
    zval **desc = NULL;
    zval *id = NULL;
    unsigned char *result = NULL;
    unsigned long result_len = 0;
    zval *input_arr = NULL;
    zval **src_entry = NULL;
    char *string_key = NULL;
    unsigned int string_key_len = 0;
    unsigned long num_key = 0;
    HashPosition pos;
    HashTable *input_ht = NULL;
    unsigned long itpl_idx = 0;
    zval *temp = NULL;
    unsigned long shift_in_pack = 0;
    unsigned char found_in_pack = 0;
    int exec_status = 0;

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"s|z",&filename,&filename_len,&input_arr)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (!filename) RETURN_FALSE;

    if (zend_hash_find(Z_OBJPROP_P(id), "tpl", sizeof("tpl"), (void **)&desc) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "INTERNAL:cannot find template descriptor (object::tpl resource)"
        );
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "blitz template", le_blitz);
    if (!tpl) {
        RETURN_FALSE;
    }

    if(input_arr && (IS_ARRAY != Z_TYPE_P(input_arr))) {
        input_arr = NULL;
    }

    // try to find already parsed tpl index
    if(SUCCESS == zend_hash_find(tpl->ht_tpl_name,filename,filename_len,(void **)&desc)) {
        itpl = tpl->itpl_list[Z_LVAL_PP(desc)];
    } else { // read and init template
        // try to find in pack
        if (tpl->pack
            && tpl->pack->ht_tpl_shift
            && (SUCCESS == zend_hash_find(tpl->pack->ht_tpl_shift,filename,filename_len+1,(void **)&desc)))
        {
            found_in_pack = 1;
            shift_in_pack = Z_LVAL_PP(desc);
        }

        // if found - init with pack data, otherwise read file
        if ((found_in_pack == 1) && (shift_in_pack>=0)) {
            if(!(itpl = blitz_init_tpl_from_pack(tpl->pack, shift_in_pack TSRMLS_CC))) {
                blitz_free_tpl(itpl);
                RETURN_FALSE;
            }
        } else {
            // initialize template
            if(!(itpl = blitz_init_tpl(filename, tpl->hash_globals TSRMLS_CC))) {
                blitz_free_tpl(itpl);
                RETURN_FALSE;
            }

            // analyse template
            if(!blitz_analyse(itpl TSRMLS_CC)) {
                blitz_free_tpl(itpl);
                RETURN_FALSE;
            }
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
                    "INTERNAL:cannot  realloc memory for inner-template list"
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
                zend_hash_update(itpl->hash_globals, string_key, string_key_len,
                    src_entry, sizeof(zval *), NULL);
            }
            zend_hash_move_forward_ex(input_ht, &pos);
        }
    }

    // execute call
    exec_status = blitz_exec_template(itpl,id,&result,&result_len TSRMLS_CC);
    if(exec_status) {
        ZVAL_STRINGL(return_value,result,result_len,1);
        if(exec_status == 1) efree(result);
    } else {
        RETURN_FALSE;
    }
}
/* }}} */

/**********************************************************************************************************************/
PHP_FUNCTION(blitz_fetch) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl;
    unsigned char *result = NULL;
    unsigned long result_len = 0;
    char exec_status = 0;
    char *path = NULL;
    int path_len = 0, norm_len = 0, res = 0, current_len = 0;
    zval *input_arr = NULL, **elem;
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
            "INTERNAL:cannot find template descriptor (object::tpl resource)"
        );
        RETURN_FALSE;
    }

    if(FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"s|z",&path,&path_len,&input_arr)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(tpl, blitz_tpl *, desc, -1, "blitz template", le_blitz);
    if(!tpl) {
        RETURN_FALSE;
    }

    if(input_arr && (IS_ARRAY != Z_TYPE_P(input_arr))) {
        input_arr = NULL;
    }

    // find corresponding iteration data
    res = blitz_normalize_path(&tpl->path_buf, path, path_len, tpl->current_path, current_len);
    norm_len = strlen(tpl->path_buf);
    zval *dummy = NULL;
    zval **path_iteration = & dummy;
    zval *final_params = NULL;

    if((current_len == norm_len)
        && (0 == strncmp(tpl->path_buf, tpl->current_path, norm_len)))
    {
//php_printf("D:1\n");
        path_iteration = tpl->current_iteration;
        path = tpl->current_path;
        path_len = current_len;
    } else {
//php_printf("D:2\n");
//php_printf("p = %p\n", *path_iteration);
        res = blitz_find_iteration_by_path(tpl, tpl->path_buf, norm_len, path_iteration);
//php_printf("p = %p, res = %d\n", *path_iteration, res);
        if(!res) {
            *path_iteration = NULL;
            final_params = input_arr;
        } else {
            final_params = *path_iteration;
        }
    }

    // merge data with input params
    if(input_arr && *path_iteration) {
        input_ht = Z_ARRVAL_P(input_arr);
        zend_hash_internal_pointer_reset_ex(Z_ARRVAL_PP(path_iteration), &pointer);
        zend_hash_internal_pointer_reset_ex(input_ht, &pointer);

        while(zend_hash_get_current_data_ex(input_ht, (void**) &elem, &pointer) == SUCCESS) {
            if(zend_hash_get_current_key_ex(input_ht, &key, &key_len, &index, 0, &pointer) != HASH_KEY_IS_STRING) {
                zend_hash_move_forward_ex(input_ht, &pointer);
                continue;
            }

            if(key) {
                zval *temp;
                ALLOC_INIT_ZVAL(temp);
                *temp = **elem;
                zval_copy_ctor(temp);

                res = zend_hash_update(
                    Z_ARRVAL_PP(path_iteration),
                    key,
                    key_len,
                    (void*)&temp, sizeof(zval *), NULL
                );
            }
            zend_hash_move_forward_ex(input_ht, &pointer);
        }
    }

//*path_iteration ? php_var_dump(path_iteration, 0) : php_printf("path_iteration uis empty\n");

    exec_status = blitz_fetch_node_by_path(tpl, id, 
        tpl->path_buf, norm_len, final_params, &result, &result_len TSRMLS_CC);
    
    if(exec_status) {
        ZVAL_STRINGL(return_value,result,result_len,1);
        if(exec_status == 1) efree(result);
    } else {
        RETURN_FALSE;
    }


    return;
}
/* }}} */

/*******************************************************************************
* BlitzPack CLASS methods
*******************************************************************************/

/* {{{ blitz_pack_init(filename) */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_pack_init) {
/**********************************************************************************************************************/
    blitz_pack *pack = NULL;
    unsigned int filename_len;
    char *filename = NULL;

    zval *new_object = NULL;
    int ret = 0;

    if(FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"|s",&filename,&filename_len)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    pack = blitz_init_pack(filename);

    MAKE_STD_ZVAL(new_object);

    if(object_init(new_object) != SUCCESS)
    {
        // do error handling here
        RETURN_FALSE;
    }

    ret = zend_list_insert(pack, le_blitz_pack);
    add_property_resource(getThis(), "pack", ret);
    zend_list_addref(ret);
}
/* }}} */

/* {{{ blitz_pack_add(filename) */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_pack_add) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_pack *pack = NULL;
    blitz_tpl *tpl = NULL;
    unsigned int filename_len;
    char *filename;

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }
    
    if (zend_hash_find(Z_OBJPROP_P(id), "pack", sizeof("pack"), (void **)&desc) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, 
            "INTERNAL:cannot find pack descriptor (object->pack resource)"
        );
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(pack, blitz_pack*, desc, -1, "blitz pack", le_blitz_pack);

    if (!pack) {
        RETURN_FALSE;
    }

    if(FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"s",&filename,&filename_len)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    // initialize template 
    if(!(tpl = blitz_init_tpl(filename, NULL TSRMLS_CC))) {
        blitz_free_tpl(tpl);    
        RETURN_FALSE;
    }

    // analyse template 
    if(!blitz_analyse(tpl TSRMLS_CC)) {
        blitz_free_tpl(tpl);
        RETURN_FALSE;
    }

    blitz_add_tpl_to_pack(tpl,pack);

    blitz_free_tpl(tpl);

    RETURN_TRUE;
}
/* }}} */

/* {{{ blitz_pack_add(filename) */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_pack_save) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_pack *pack = NULL;
    unsigned int filename_len;
    char *filename = NULL;

    if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"|s",&filename,&filename_len)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if ((id = getThis()) == 0) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (zend_hash_find(Z_OBJPROP_P(id), "pack", sizeof("pack"), (void **)&desc) == FAILURE) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "INTERNAL:cannot find pack descriptor (object->pack resource)"
        );
        RETURN_FALSE;
    }

    ZEND_FETCH_RESOURCE(pack, blitz_pack*, desc, -1, "blitz pack", le_blitz_pack);

    if (!pack) {
        RETURN_FALSE;
    }

    if (!filename) {
        if (pack->filename) {
            filename = pack->filename;
        } else {
            php_error_docref(NULL TSRMLS_CC, E_WARNING,
                "empty filename, should be passed as parameter or set in pack constructor"
            );
            RETURN_FALSE;
        }
    }

    php_stream *stream = NULL;
    if (php_check_open_basedir(filename TSRMLS_CC)){                                      
        RETURN_FALSE;
    }                                                                               
    stream = php_stream_open_wrapper(filename, "wb",                                        
        IGNORE_PATH|IGNORE_URL|IGNORE_URL_WIN|ENFORCE_SAFE_MODE|REPORT_ERRORS, NULL 
    ); 

    if (!stream)
    {
        RETURN_FALSE;
    }

    // save header
    if ((sizeof(unsigned long) != php_stream_write(stream,(void*)&pack->version,sizeof(unsigned long)))
        || (sizeof(unsigned long) != php_stream_write(stream,(void*)&pack->tpl_num,sizeof(unsigned long))))
    {
        RETURN_FALSE;
    }

    // save template headers
    if ((sizeof(unsigned long) != php_stream_write(stream,(void*)&pack->tpl_head_len,sizeof(unsigned long)))
        || (pack->tpl_head_len != php_stream_write(stream,pack->tpl_head,pack->tpl_head_len)))
    { 
        RETURN_FALSE;                                                                
    }                                                                               

    // save template data
    if ((sizeof(unsigned long) != php_stream_write(stream,(void*)&pack->tpl_pack_len,sizeof(unsigned long))) 
        || (pack->tpl_pack_len != php_stream_write(stream,pack->tpl_pack,pack->tpl_pack_len)))
    {
       RETURN_FALSE; 
    }

    php_stream_close(stream);
    RETURN_TRUE;
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
