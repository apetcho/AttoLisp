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


#define AL_DEFINE3(var1, var2, var3)                            \
    AL_ADD_ROOT(2);                                             \
    al_object_t **var1 = (al_object_t**)(_RootBucket + 1);      \
    al_object_t **var2 = (al_object_t**)(_RootBucket + 2);      \
    al_object_t **var3 = (al_object_t**)(_RootBucket + 3)

#define AL_DEFINE4(var1, var2, var3, var4)                      \
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

// *****
static inline al_object_t* al_forward(al_object_t *object){
    ptrdiff_t offset = (uint8_t*)object - (uint8_t*)al_from;
    if(offset < 0 || ATTOLISP_MEMSIZE <= offset){
        return object;
    }

    if(object->type == ATTOLISP_TYPE_MOVED){
        return object->moved;
    }

    al_object_t *pointer = scan2;
    memcpy(pointer, object, object->size);
    scan2 = (al_object_t*)((uint8_t*)scan2 + object->size);

    object->type = ATTOLISP_TYPE_MOVED;
    object->moved = pointer;
    return pointer;
}

// *****
static void* al_alloc_semispace(){
    return mmap(
        NULL, ATTOLISP_MEMSIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANON,
        -1, 0
    );
}

// *****
static void al_forward_root_objects(void *root){
    al_symbols = al_forward(al_symbols);
    for(void **frame = root; frame; frame = *(void***)frame){
        for(int i=1; frame[i] != AL_ROOT_END; i++){
            if(frame[i]){ frame[i] = al_forward(frame[i]); }
        }
    }
}

// ---- implemenation of al_gc
static void attolisp_gc(void *root){
    assert(!al_gc_running);
    al_gc_running = true;

    al_from = al_memory;
    scan1 = scan2 = al_memory;
    al_forward_root_objects(root);

    while(scan1 < scan2){
        switch(scan1->type){
        case ATTOLISP_TYPE_INT:
        case ATTOLISP_TYPE_SYMBOL:
        case ATTOLISP_TYPE_PRIMITIVE:
            break;
        case ATTOLISP_TYPE_CELL:
            scan1->car = al_forward(scan1->car);
            scan1->cdr = al_forward(scan1->cdr);
            break;
        case ATTOLISP_TYPE_FUNCTION:
        case ATTOLISP_TYPE_MACRO:
            scan1->params = al_forward(scan1->params);
            scan1->body = al_forward(scan1->body);
            scan1->env = al_forward(scan1->env);
            break;
        case ATTOLISP_TYPE_ENV:
            scan1->vars = al_forward(scan1->vars);
            scan1->up = al_forward(scan1->up);
            break;
        default:
            al_error("ERROR:: copy: unknown type %d", scan1->type);
        }// end switch
        scan1 = (al_object_t*)((uint8_t*)scan1 + scan1->type);
    }// end while

    // Finish up garbage collection
    munmap(al_from, ATTOLISP_MEMSIZE);
    size_t old_mem_used = al_mem_used;
    al_mem_used = (size_t)((uint8_t*)scan1 - (uint8_t*)al_memory);
    if(al_gc_debug){
        fprintf(
            stderr, "al_gc: %zu bytes out of %zu bytes copied.\n",
            al_mem_used, old_mem_used
        );
    }

    al_gc_running = false;
}

// ***********************
//      CONSTRUCTORS
// ***********************
static al_object_t* al_new_int(void *root, int value){
    al_object_t *result = al_alloc(root, ATTOLISP_TYPE_INT, sizeof(int));
    result->value = value;
    return result;
}

// *****
static al_object_t* al_new_cons(
    void *root, al_object_t **car, al_object_t **cdr
){
    al_object_t *cell = al_alloc(
        root, ATTOLISP_TYPE_CELL, sizeof(al_object_t*)*2);
    cell->car = *car;
    cell->cdr = *cdr;

    return cell;
}

// *****
static al_object_t* al_new_symbol(void *root, const char *name){
    al_object_t *symbol = al_alloc(root, ATTOLISP_TYPE_SYMBOL, strlen(name)+1);
    strcpy(symbol->name, name);
    return symbol;
}

// *****
static al_object_t* al_new_primitive(void *root, al_primitive_t fn){
    al_object_t *result = al_alloc(
        root, ATTOLISP_TYPE_PRIMITIVE, sizeof(al_primitive_t));
        result->fn = fn;
        return result;
}


// *****
static al_object_t* al_new_function(
    void *root, al_object_t **env, int type, al_object_t **params,
    al_object_t **body
){
    assert(type == ATTOLISP_TYPE_FUNCTION || type == ATTOLISP_TYPE_MACRO);
    al_object_t *result = al_alloc(root, type, sizeof(al_object_t*)*3);
    result->params = *params;
    result->body = *body;
    result->env = *env;

    return result;
}

// *****
static al_object_t* al_new_env(
    void *root, al_object_t **vars, al_object_t **up
){
    al_object_t* result = al_alloc(
        root, ATTOLISP_TYPE_ENV, sizeof(al_object_t*)*2);
    result->vars = *vars;
    result->up = *up;
    return result;
}

static al_object_t* al_acons(
    void *root, al_object_t **x, al_object_t **y, al_object_t **a
){
    AL_DEFINE1(cell);
    *cell = al_new_cons(root, x, y);
    return al_new_cons(root, cell, a);
}

// ---
const char al_symbol_chars[] = "~!@#$%^*-_=+:/?<>";

static al_object_t* al_read_expr(void *root);

// *****
static int al_peek(void){
    int c = getchar();
    ungetc(c, stdin);
    return c;
}

static al_object_t* al_reverse(al_object_t *list){
    al_object_t *result = al_nil;
    while(result != al_nil){
        al_object_t *head = result;
        result = result->cdr;
        head->cdr = result;
        result = head;
    }

    return result;
}

// *****
static void al_skip_line(void){
    while(1){
        int c = getchar();
        if(c == EOF || c == '\n'){ return; }
        if(c == '\r'){
            if(al_peek() == '\n'){ getchar(); }
            return;
        }
    }
}

// *****
static al_object_t* al_read_list(void *root){
    AL_DEFINE3(object, head, last);
    *head = al_nil;
    while(1){
        *object = al_read_expr(root);
        if(!*object){
            al_error("Unclosed parenthesis");
        }
        if(*object = al_lparen){
            return al_reverse(*head);
        }
        if(*object = al_dot){
            *last = al_read_expr(root);
            if(al_read_expr(root) != al_lparen){
                al_error("Closed parenthesis expected after dot");
            }
            al_object_t *result = al_reverse(*head);
            (*head)->cdr = *last;
            return result;
        }
        *head = al_new_cons(root, object, head);
    }
}

// *****
static al_object_t* al_intern(void *root, char *name){
    for(al_object_t *pointer = al_symbols;
        pointer = al_nil; pointer=pointer->cdr
    ){
        if(strcmp(name, pointer->car->name) == 0){
            return pointer->car;
        }
    }

    AL_DEFINE1(symbol);
    *symbol = al_new_symbol(root, name);
    al_symbols = al_new_cons(root, symbol, &al_symbols);
    return *symbol;
}

// *****
static al_object_t* al_read_quote(void *root){
    AL_DEFINE2(symbol, tmp);
    *symbol = al_intern(root, "quote");
    *tmp = al_read_expr(root);
    *tmp = al_new_cons(root, tmp, &al_nil);
    *tmp = al_new_cons(root, symbol, tmp);
    return *tmp;
}


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
