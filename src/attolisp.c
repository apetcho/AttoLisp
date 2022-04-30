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
static al_object_t *al_cparen = &(al_object_t){ ATTOLISP_TYPE_CPAREN };

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
        if(*object = al_cparen){
            return al_reverse(*head);
        }
        if(*object = al_dot){
            *last = al_read_expr(root);
            if(al_read_expr(root) != al_cparen){
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


// *****
static int al_read_number(int value){
    while(isdigit(al_peek())){
        value = value * 10 + (getchar() - '0');
    }
    return value;
}

// *****
static al_object_t* al_read_symbol(void *root, char c){
    char buffer[ATTOLISP_MAXLEN+1];
    buffer[0] = c;
    int len = 1;
    while(isalnum(al_peek())|| strchr(al_symbol_chars, al_peek())){
        if(ATTOLISP_MAXLEN <= len){
            al_error("ERROR: Symbol name too long");
        }
        buffer[len++] = getchar();
    }
    buffer[len] = '\0';
    return al_intern(root, buffer);
}

// *****
static al_object_t* al_read_expr(void *root){
    while(1){
        int c = getchar();
        if(c == ' ' || c == '\n' || c == '\r' || c == '\t'){ continue; }
        if(c == EOF){ return NULL; }
        if(c == ';'){
            al_skip_line();
            continue;
        }
        if(c == '('){ return al_read_list(root); }
        if(c == ')'){ return al_cparen; }
        if(c == '.'){ return al_dot; }
        if(c == '\''){ return al_read_quote(root); }
        if(isdigit(c)){
            return al_new_int(root, al_read_number(c-'c'));
        }
        if(c == '-' && isdigit(al_peek())){
            return al_new_int(root, -al_read_number(0));
        }
        if(isalpha(c) || strchr(al_symbol_chars, c)){
            return al_read_symbol(root, c);
        }
        al_error("ERROR:: Don't know how to handle %c", c);
    }
}

// *****
static void al_print(al_object_t *object){
    switch(object->type){
    case ATTOLISP_TYPE_CELL:
        printf("(");
        while(1){
            printf(object->car);
            if(object->cdr == al_nil){ break; }
            if(object->cdr->type != ATTOLISP_TYPE_CELL){
                printf(" . ");
                printf(object->cdr);
                break;
            }
            printf(" ");
            object = object->cdr;
        }
        printf(")");
        return;
#define AL_CASE(type, ...)      \
    case type:                  \
        printf(__VA_ARGS__);    \
        return

    AL_CASE(ATTOLISP_TYPE_INT, "%d", object->value);
    AL_CASE(ATTOLISP_TYPE_SYMBOL, "%s", object->name);
    AL_CASE(ATTOLISP_TYPE_PRIMITIVE, "<primitive>");
    AL_CASE(ATTOLISP_TYPE_FUNCTION, "<function>");
    AL_CASE(ATTOLISP_TYPE_MACRO, "<macro>");
    AL_CASE(ATTOLISP_TYPE_MOVED, "<moved>");
    AL_CASE(ATTOLISP_TYPE_TRUE, "t");
    AL_CASE(ATTOLISP_TYPE_NIL, "()");
#undef AL_CASE
        default:
            al_error("ERROR:: print: Unknown tag type: %d", object->type);
    } // end switch
}

// *****
static int al_length(al_object_t *list){
    int len = 0;
    for(; list->type == ATTOLISP_TYPE_CELL; list = list->cdr){ len++; }
    return list == al_nil ? len : -1;
}

// -------------
//  Evaluator
// -------------
static al_object_t* al_eval(
    void *root, al_object_t **env, al_object_t **object
);

// *****
static void al_add_variable(
    void *root,
    al_object_t **env,
    al_object_t **sym,
    al_object_t **values
){
    AL_DEFINE2(vars, tmp);
    *vars = (*env)->vars;
    *tmp = al_acons(root, sym, values, vars);
    (*env)->vars = *tmp;
}

// *****
static al_object_t* al_push_env(
    void *root,
    al_object_t **env,
    al_object_t **vars,
    al_object_t **values
){
    AL_DEFINE3(map, symbol, value);
    *map = al_nil;
    for(; (*vars)->type == ATTOLISP_TYPE_CELL;
        *vars = (*vars)->cdr, *values = (*values)->cdr
    ){
        if((*values)->type != ATTOLISP_TYPE_CELL){
            al_error(
                "ERROR: Cannot apply function: number of argument does "
                "match"
            );
        }
        *symbol = (*vars)->car;
        *value = (*values)->car;
        *map = al_acons(root, symbol, value, map);
    }
    if(*vars != al_nil){
        *map = al_acons(root, vars, values, map);
    }

    return al_new_env(root, map, env);
}

// *****
static al_object_t* al_progn(
    void *root, al_object_t **env, al_object_t **list
){
    AL_DEFINE2(pointer, result);
    for(*pointer = *list; *pointer != al_nil; *pointer = (*pointer)->cdr){
        *result = (*pointer)->car;
        *result = al_eval(root, env, result);
    }

    return *result;
}

// *****
static al_object_t* al_eval_list(
    void *root, al_object_t **env, al_object_t **list
){
    AL_DEFINE4(head, pointer, expr, result);
    *head = al_nil;
    for(pointer = list; *pointer != al_nil; *pointer = (*pointer)->cdr){
        *expr = (*pointer)->car;
        *result = al_eval(root, env, expr);
        *head = al_new_cons(root, result, head);
    }

    return al_reverse(*head);
}

// *****
static bool al_is_list(al_object_t *object){
    return object == al_nil || object->type == ATTOLISP_TYPE_CELL;
}

// *****
static al_object_t* al_apply_callback(
    void *root,
    al_object_t **env,
    al_object_t **callback,
    al_object_t **args
){
    AL_DEFINE3(params, newEnv, body);
    *params = (*callback)->params;
    *newEnv = (*callback)->env;
    *newEnv = al_push_env(root, newEnv, params, args);
    *body = (*callback)->body;

    return al_progn(root, newEnv, body);
}

// *****
static al_object_t* al_apply(
    void *root,
    al_object_t **env,
    al_object_t **callback,
    al_object_t **args
){
    if(!al_is_list(*args)){
        al_error("ERROR: argument must be a list");
    }
    if((*callback)->type == ATTOLISP_TYPE_PRIMITIVE){
        return (*callback)->fn(root, env, args);
    }
    if((*callback)->type == ATTOLISP_TYPE_FUNCTION){
        AL_DEFINE1(xargs);
        *xargs = al_eval_list(root, env, args);
        return al_apply_callback(root, env, callback, xargs);
    }
    al_error("ERROR:: not supported");
}

// *****
static al_object_t* al_find(al_object_t **env, al_object_t *sym){
    for(al_object_t *pointer = *env; pointer != al_nil; pointer = pointer->up){
        for(al_object_t *cell=pointer->vars; cell!=al_nil; cell=cell->cdr){
            al_object_t *bind = cell->car;
            if(sym == bind->car){ return bind; }
        }
    }

    return NULL;
}

// *****
static al_object_t* al_macroexpand(
    void *root,
    al_object_t **env,
    al_object_t **object
){
    if((*object)->type != ATTOLISP_TYPE_CELL ||
        (*object)->car->type != ATTOLISP_TYPE_SYMBOL
    ){ return *object; }
    AL_DEFINE3(bind, macro, args);
    *bind = al_find(env, (*object)->car);
    if(!*bind || (*bind)->cdr->type != ATTOLISP_TYPE_MACRO){
        return *object;
    }
    *macro = (*bind)->cdr;
    *args = (*object)->cdr;
    return al_apply_callback(root, env, macro, args);
}

// *****
static al_object_t* al_eval(
    void *root,
    al_object_t **env, al_object_t **object
){
    switch((*object)->type){
    case ATTOLISP_TYPE_INT:
    case ATTOLISP_TYPE_PRIMITIVE:
    case ATTOLISP_TYPE_FUNCTION:
    case ATTOLISP_TYPE_TRUE:
    case ATTOLISP_TYPE_NIL:
        return *object;
    case ATTOLISP_TYPE_SYMBOL:{
        al_object_t *bind = al_find(env, *object);
        if(!bind){
            al_error("ERROR: Undefined symbol: %s", (*object)->name);
        }
        return bind->cdr;
    }
    case ATTOLISP_TYPE_CELL:{
        AL_DEFINE3(fn, expanded, args);
        *expanded = al_macroexpand(root, env, object);
        if(*expanded != *object){
            return al_eval(root, env, expanded);
        }
        *fn = (*object)->car;
        *fn = al_eval(root, env, fn);
        *args = (*object)->cdr;
        if((*fn)->type != ATTOLISP_TYPE_PRIMITIVE &&
            (*fn)->type != ATTOLISP_TYPE_FUNCTION
        ){
            return al_apply(root, env, fn, args);
        }
    }
    default:
        al_error("ERROR:: eval: Unknown tag type: %d\n", (*object)->type);
    }// end switch
}


// ------------------------------------------------------------------
//              PRIMITIVE FUNCTIONS AND SPECIAL FORMS
// ------------------------------------------------------------------
static al_object_t* al_primitive_quote(
    void *root, al_object_t **env, al_object_t **list
){
    if(al_length(*list) != 1){ al_error("Malformed quote");}
    return (*list)->car;
}

// *****
static al_object_t* al_primitive_cons(
    void *root, al_object_t **env, al_object_t **list
){
    if(al_length(*list) != 2){ al_error("Malformed cons"); }
    al_object_t *cell = al_eval_list(root, env, list);
    cell->cdr = cell->cdr->car;

    return cell;
}

// *****
static al_object_t* al_primitive_car(
    void *root, al_object_t **env, al_object_t **list
){
    al_object_t *args = al_eval_list(root, env, list);
    if(args->car->type != ATTOLISP_TYPE_CELL || args->cdr != al_nil){
        al_error("Malformed car");
    }
    return args->car->car;
}

static al_object_t* al_primitive_cdr(
    void *root, al_object_t **env, al_object_t **list
){
    al_object_t *args = al_eval_list(root, env, list);
    if(args->car->type != ATTOLISP_TYPE_CELL || args->cdr != al_nil){
        al_error("Malformed car");
    }
    return args->car->cdr;
}

// *****
static al_object_t* al_primitive_setq(
    void *root, al_object_t **env, al_object_t **list
){
    if(al_length(*list) != 2 || (*list)->car->type != ATTOLISP_TYPE_SYMBOL){
        al_error("Malformed setq");
    }

    AL_DEFINE2(bind, value);
    *bind = al_find(env, (*list)->car);
    if(!*bind){
        al_error("ERROR: Unbound variable %s", (*list)->car->name);
    }
    *value = (*list)->cdr->car;
    *value = al_eval(root, env, value);
    (*bind)->cdr = *value;

    return *value;
}

// *****
static al_object_t* al_primitive_setcar(
    void *root, al_object_t **env, al_object_t **list
){
    AL_DEFINE1(args);
    *args = al_eval_list(root, env, list);
    if(al_length(*args) != 2 || (*args)->car->type != ATTOLISP_TYPE_CELL){
        al_error("Malformed setcar");
    }
    (*args)->car->car = (*args)->cdr->car;

    return (*args)->car;
}

// *****
static al_object_t* al_primitive_while(
    void *root, al_object_t **env, al_object_t **list
){
    if(al_length(*list) < 2){
        al_error("ERROR: Malformed while");
    }
    AL_DEFINE2(cond, exprs);
    *cond = (*list)->car;
    while(al_eval(root, env, cond) != al_nil){
        *exprs = (*list)->cdr;
        al_eval_list(root, env, exprs);
    }

    return al_nil;
}

// *****
static al_object_t* al_primitive_gensym(
    void *root, al_object_t **env, al_object_t **list
){
    static int count = 0;
    char buffer[10];
    snprintf(buffer, sizeof(buffer), "G__%d", count++);
    return al_new_symbol(root, buffer);
}

// *****
static al_object_t* al_primitive_plus(
    void *root, al_object_t **env, al_object_t **list
){
    int result = 0;
    for(al_object_t *args = al_eval_list(root, env, list);
        args != al_nil; args = args->cdr
    ){
        if(args->car->type != ATTOLISP_TYPE_INT){
            al_error("+ takes only numbers");
        }
        result += args->car->value;
    }

    return al_new_int(root, result);
}

static al_object_t* al_primitive_minus(
    void *root, al_object_t **env, al_object_t **list
){
    al_object_t *args = al_eval_list(root, env, list);
    for(al_object_t *pointer = args; pointer != al_nil; pointer = pointer->cdr){
        if(pointer->car->type != ATTOLISP_TYPE_INT){
            al_error("- takes only numbers");
        }
    }
    if(args->cdr == al_nil){
        return al_new_int(root, -args->car->value);
    }
    int result = args->car->value;
    for(al_object_t *pointer=args->cdr; pointer!=al_nil; pointer=pointer->cdr){
        result -= pointer->car->value;
    }

    return al_new_int(root, result);
}


// *****
static al_object_t* al_primitive_lt(
    void *root, al_object_t **env, al_object_t **list
){
    al_object_t *args = al_eval_list(root, env, list);
    if(al_length(args) != 2){al_error("Malformed <"); }
    al_object_t *x = args->car;
    al_object_t *y = args->cdr->car;
    if(x->type != ATTOLISP_TYPE_INT || y->type != ATTOLISP_TYPE_INT){
        al_error("< takes only numbers");
    }

    return x->value < y->value ? al_true : al_nil;
}

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
