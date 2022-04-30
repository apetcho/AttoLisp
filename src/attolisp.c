#include<stddef.h>
#include<stdlib.h>
#include<stdbool.h>
#include<stdint.h>
#include<stdarg.h>
#include<string.h>
#include<assert.h>
#include<ctype.h>
#include<stdio.h>
#include<sys/mman.h>

#include "attolisp.h"

#define ATTOLISP_MAXLEN     200
#define ATTOLISP_MEMSIZE    65536
#define AL_ROOT_END     ((void*)-1)

#define AL_ADD_ROOT(size)                   \
    void *_RootBucket[(size)+2];            \
    _RootBucket[0] = root;                  \
    for(int i=1; i <= (size); i++){         \
        _RootBucket[i] = NULL;              \
    }                                       \
    _RootBucket[(size)+1] = AL_ROOT_END;    \
    root = _RootBucket

#define AL_DEFINE1(var1)                                        \
    AL_ADD_ROOT(1);                                             \
    al_object_t **var1 = (al_object_t**)(_RootBucket + 1)

#define AL_DEFINE2(var1, var2)                                  \
    AL_ADD_ROOT(2);                                             \
    al_object_t **var1 = (al_object_t**)(_RootBucket + 1);      \
    al_object_t **var2 = (al_object_t**)(_RootBucket + 2)


#define AL_DEFINE2(var1, var2, var3)                            \
    AL_ADD_ROOT(2);                                             \
    al_object_t **var1 = (al_object_t**)(_RootBucket + 1);      \
    al_object_t **var2 = (al_object_t**)(_RootBucket + 2);      \
    al_object_t **var3 = (al_object_t**)(_RootBucket + 3)

#define AL_DEFINE2(var1, var2, var3)                            \
    AL_ADD_ROOT(2);                                             \
    al_object_t **var1 = (al_object_t**)(_RootBucket + 1);      \
    al_object_t **var2 = (al_object_t**)(_RootBucket + 2);      \
    al_object_t **var3 = (al_object_t**)(_RootBucket + 3);      \
    al_object_t **var4 = (al_object_t**)(_RootBucket + 4)


// constants
static al_object_t *al_true = &(al_object_t){ ATTOLISP_TYPE_TRUE };
static al_object_t *al_nil = &(al_object_t){ ATTOLISP_TYPE_NIL };
static al_object_t *al_dot = &(al_object_t){ ATTOLISP_TYPE_DOT };
static al_object_t *al_lparen = &(al_object_t){ ATTOLISP_TYPE_LPAREN };

// symbol list
static al_object_t *al_symbols;

// ---
static void *al_memory;
static void *al_from;
static size_t al_mem_used = 0;
// GC flags
static bool al_gc_running = false;
static bool al_gc_debug = false;
static bool al_gc_always = false;

// ---
static void al_error(const char *fmt, ...){
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(EXIT_FAILURE);
}

static void attolisp_gc(void *root);

// *****
static inline size_t al_round_up(size_t var, size_t size){
    return (var + size - 1) & ~(size - 1);
}


// ******
static al_object_t* al_alloc(void *root, int type, size_t size){
    size = al_round_up(size, sizeof(void*));
    size += offsetof(al_object_t, value);
    size = al_round_up(size, sizeof(void*));
    if(al_gc_always && !al_gc_running){
        attolisp_gc(root);
    }

    if(!al_gc_always && ATTOLISP_MEMSIZE < al_mem_used + size){
        attolisp_gc(root);
    }

    if(ATTOLISP_MEMSIZE < al_mem_used + size){
        al_error("Memory exhausted");
    }

    al_object_t *object = al_memory + al_mem_used;
    object->type = type;
    object->size = size;
    al_mem_used += size;

    return object;
}

// -------------------------------
// ----- GARBAGE COLLECTOR -------
// -------------------------------
static al_object_t *scan1;
static al_object_t *scan2;

static inline al_object_t* al_forward(al_object_t *object);
static void* al_alloc_semispace();
static void al_forward_root_objects(void *root);

// ---- implemenation of al_gc
static void attolisp_gc(void *root){}

// ***********************
//      CONSTRUCTORS
// ***********************
static al_object_t* al_new_int(void *root, int value){}
static al_object_t* al_new_cons(
    void *root, al_object_t *car, al_object_t *cdr
){}

static al_object_t* al_new_symbol(void *root, const char *name){}
static al_object_t* al_new_primitive(void *root, al_primitive_t fn){}
static al_object_t* al_new_function(
    void *root, al_object_t **env, int type, al_object_t **params,
    al_object_t **body
){}

static al_object_t* al_new_env(
    void *root, al_object_t **vars, al_object_t **up
){
}

static al_object_t* al_acons(
    void *root, al_object_t **x, al_object_t **y, al_object_t **a
){}

// ---
const char al_symbol_chars[] = "~!@#$%^*-_=+:/?<>";

static al_object_t* al_read_expr(void *root);

static int al_peek(void){}

static al_object_t* al_reverse(al_object_t *list){}

static void al_skip_line(void){}

static al_object_t* al_read_list(void *root){}

static al_object_t* al_intern(void *root, char *name);

static al_object_t* al_read_quote(void *root){}

static int al_read_number(int value){}

static al_object_t* al_read_symbol(void *root, char c){}

static al_object_t* al_read_expr(void *root){}

static void al_print(al_object_t *object){}

static int al_length(al_object_t *list){}

// -------------
//  Evaluator
// -------------
static al_object_t* al_eval(
    void *root, al_object_t **env, al_object_t **object
);

static void al_add_variable(
    void *root,
    al_object_t **env,
    al_object_t **sym,
    al_object_t **values
){}

static al_object_t* al_push_env(
    void *root,
    al_object_t **env,
    al_object_t **vars,
    al_object_t **values
){}

static al_object_t* al_progn(
    void *root,
    al_object_t **env, al_object_t **list
){}

static al_object_t* al_eval_list(
    void *root,
    al_object_t **env, al_object_t **list
){}

static bool al_is_list(al_object_t *object){}

static al_object_t* al_apply_callback(
    void *root,
    al_object_t **env,
    al_object_t **callback,
    al_object_t **args
){}

static al_object_t* al_apply(
    void *root,
    al_object_t **env,
    al_object_t **callback,
    al_object_t **args
){}

static al_object_t* al_find(al_object_t **env, al_object_t *sym){}

static al_object_t* al_macroexpand(
    void *root,
    al_object_t **env,
    al_object_t **object
){}

static al_object_t* al_eval(
    void *root,
    al_object_t **env, al_object_t **object
){}


// ------------------------------------------------------------------
//              PRIMITIVE FUNCTIONS AND SPECIAL FORMS
// ------------------------------------------------------------------
static al_object_t* al_primitive_quote(
    void *root, al_object_t **env, al_object_t **list
){}


static al_object_t* al_primitive_cons(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_primitive_car(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_primitive_cdr(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_primitive_setq(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_primitive_setcar(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_primitive_while(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_primitive_gensym(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_primitive_plus(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_primitive_minus(
    void *root, al_object_t **env, al_object_t **list
){}


static al_object_t* al_primitive_lt(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_handle_function(
    void *root, al_object_t **env, al_object_t **list, int type
){}

static al_object_t* al_primitive_lambda(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_handle_defun(
    void *root, al_object_t **env, al_object_t **list, int type
){}

static al_object_t* al_primitive_defun(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_primitive_define(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_primitive_defmacro(
    void *root, al_object_t **env, al_object_t **list
){}


static al_object_t* al_primitive_macroexpand(
    void *root, al_object_t **env, al_object_t **list
){}


static al_object_t* al_primitive_println(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_primitive_if(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_primitive_number_eq(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_primitive_eq(
    void *root, al_object_t **env, al_object_t **list
){}

static al_object_t* al_add_primitive(
    void *root, al_object_t **env, char *name, al_primitive_t fn
){}

static al_object_t* al_define_constants(void *root, al_object_t **env){}

static al_object_t* al_define_primitives(void *root, al_object_t **env){}


// --------------------------
//          ENTRY POINT
// --------------------------
static bool al_getenv_flag(char *name){}

// *********************************
// ---- M A I N    D R I V E R -----
// *********************************
int main(int argc, char **argv){

    return EXIT_SUCCESS;
}
