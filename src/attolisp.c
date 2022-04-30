#include<stdbool.h>
#include<stdint.h>
#include<stdlib.h>
#include<ctype.h>
#include<string.h>
#include<stdarg.h>
#include<assert.h>
#include<stdio.h>

#include "attolisp.h"

#define AL_MAX_TOKEN    256
#define AL_HEAPSIZE     16384
#define AL_MAX_ROOTS    500
#define AL_MAX_FRAMES   50
#define AL_HASHMAP_SIZE 2028
#define AL_ATOM_CHAR(a) (((a) >= '!' && (a) <= '\'') || \
    ((a) >= '*' && (a) <= '~'))
#define AL_TEXT(a) (((a) && (a)->tag == AL_TAG_ATOM) ? \
    ((const char*)((a)->car)) : "")

// ----------------
static al_gc_t al_gc;

// --- constant ---
static const char *AL_QUOTE = NULL;
static const char *AL_LAMBDA = NULL;
static const char *AL_COND = NULL;
static const char *AL_DEFINE = NULL;

// ----- other global variables -------
static char al_token[AL_MAX_TOKEN];
static int al_token_peek = 0;
static al_object_t *al_true =NULL;
static al_object_t *heap;
static al_object_t *tospace;
static al_object_t *fromspace;
static al_object_t *allocptr;
static al_object_t *scanptr;
static al_object_t **roots[AL_MAX_ROOTS];
size_t roottop;
size_t numroots;
static al_object_t al_marker = {
    .car = 0,
    .car = 0,
    .tag = AL_TAG_ATOM
};

// ----------------
/**
 * @brief Compute the hash value of data
 * 
 * @param data 
 * @return size_t 
 */
static size_t _al_hash(const unsigned char *data){
    size_t index = 5381;
    for(int c=*data++; c; c = *data++){
        index = (index << 5) + index + c;
    }

    return index;
}

/**
 * @brief Create a new node for an AttoLisp symbol and return it.
 * 
 * If the symbol exist already, just return it.
 * 
 * @param data 
 * @return const char* 
 */
static const char* _al_intern_string(const char *data){
    typedef struct Node{
        struct Node *next;
        char data[];
    } Node_t;
    static Node_t *nodes[AL_HASHMAP_SIZE] = {0};
    size_t index = _al_hash((const unsigned char*)data) % AL_HASHMAP_SIZE;
    for(Node_t *node = nodes[index]; node != NULL; node = node->next){
        if(strcmp(node->data, data) == 0){ // symbol exist already
            return node->data;
        }
    }
    // data is a new symbol
    size_t size = strlen(data) + 1;
    Node_t *node = malloc(sizeof(*node) + size);
    memcpy(node->data, data, size);
    node->next = nodes[index];
    nodes[index] = node;
    return node->data;
    //! @note Who is managing the heap allocated memory ?
}

/**
 * @brief Test whether a symbol is a number
 * 
 * @param data 
 * @return true 
 * @return false 
 */
static bool _al_is_number(const char *data){
    if(*data == '-' || *data == '+'){ data++;}
    do{
        if(*data < '0' || *data > '9'){ return false; }
    }while(*++data != '\0');

    return true;
}

/**
 * @brief Return a a string representation of a number
 * 
 * @param num 
 * @return const char* 
 */
static const char* _al_num_to_string(long num){
    char buffer[AL_MAX_TOKEN];
    char reversed[AL_MAX_TOKEN];

    char *cursor1 = buffer;
    char *cursor2 = reversed;
    unsigned long value = (unsigned long)num;
    if(num < 0){            // if num is negative
        *cursor1++ = '-';   // make sure the '-' is not forgotten
        value = ~value +1;  // and make sure the last bit of value is toggled.
    }
    // Extract the digits and push them onto stack
    do{
        *cursor2++ = (char)(value % 10) + '0';
        value /= 10;
    }while(value > 0);
    // Get the digits from the stack and place the in the buffer
    do{
        *cursor1 = *--cursor2;
    }while(cursor2 != reversed);
    // a string is properly define if NULL-terminated
    *cursor1 = '\0';
    return _al_intern_string(buffer);
}

/**
 * @brief Allocate a slot on the heap for a function
 * 
 * @param fn 
 * @return al_object_t* 
 */
static al_object_t* _al_new_function(al_function_t fn){
    return al_gc_alloc(AL_TAG_FUNCTION, (al_object_t*)fn, NULL);
}

/**
 * @brief Allocate a slot on the heap for a new atom
 * 
 * @param data 
 * @return al_object_t 
 */
static al_object_t* _al_new_atom(const char *data){
    return al_gc_alloc(
        AL_TAG_ATOM, (al_object_t*)_al_intern_string(data), NULL
    );
}

/**
 * @brief Allocate a slot on the heap for a new cons.
 * 
 * @param env 
 * @param object 
 * @return al_object_t* 
 */
static al_object_t* _al_new_cons(al_object_t *car, al_object_t *cdr){
    al_gc_protect(&car, &cdr, NULL);
    al_object_t *obj = al_gc_alloc(AL_TAG_CONS, car, cdr);
    al_gc_pop();
    return obj;
}

/**
 * @brief Read data from stream and tokenize it.
 * 
 * @param stream 
 * @return const char* 
 */
static const char* _al_read_token(FILE *stream){
    int n = 0;
    while(isspace(al_token_peek)){
        al_token_peek = fgetc(stream);
    }
    if(al_token_peek == '(' || al_token_peek == ')'){
        al_token[n++] = al_token_peek;
        al_token_peek = fgetc(stream);
    }else{
        while(AL_ATOM_CHAR(al_token_peek)){
            if(n == AL_MAX_TOKEN){ // token to long
                abort();
            }
            al_token[n++] = al_token_peek;
            al_token_peek = fgetc(stream);
        }
    }
    if(al_token_peek == EOF){ exit(0); }
    al_token[n] = '\0';

    return _al_intern_string(al_token);
}


/**
 * @brief Read and object from token string
 * 
 * @param stream 
 * @param text 
 * @return al_object_t* 
 */
static al_object_t* _al_read_object(FILE *stream, const char *text){
    if(text[0] != '('){
        // (void)stream;
        return _al_new_atom(text);
    }else{
        return _al_read_list(stream, _al_read_token(stream));
    }
}

/**
 * @brief Read an AttoLisp from a string.
 * 
 * @param stream 
 * @param text 
 * @return al_object_t* 
 */
static al_object_t* _al_read_list(FILE *stream, const char *text){
    if(text[0] == ')'){ // end of atom list.
        return NULL;
    }
    al_object_t *obj1 = NULL;
    al_object_t *obj2 = NULL;
    al_object_t *tmp = NULL;
    al_gc_protect(&obj1, &obj2, &tmp, NULL);
    obj1 = _al_read_object(stream, text);
    text = _al_read_token(stream);
    if(text[0] == '.' && text[1] == '\0'){
        text = _al_read_token(stream);
        tmp = _al_read_object(stream, text);
        obj2 = _al_new_cons(obj1, tmp);
        al_gc_pop();
        if(text[0] == ')'){ return obj2; }
        fputs("ERROR: Malformed dotted cons\n", stderr);
        return NULL;
    }
    tmp = _al_read_list(stream, text);
    obj2 = _al_new_cons(obj1, tmp);
    al_gc_pop();
    return obj2;
}

/**
 * @brief Read an object from stream
 * 
 * @param stream 
 * @return al_object_t* 
 */
static al_object_t* _al_read(FILE *stream){
    const char *token = _al_read_token(stream);
    if(token == NULL){ return NULL; }
    if(token[0] != ")"){
        return _al_read_object(stream, token);
    }
    fputs("ERROR: Unexpected ')'\n", stderr);
    return NULL;
}

/**
 * @brief Test whether to object are the same or members are equal.
 * 
 * @param a 
 * @param b 
 * @return true 
 * @return false 
 */
static bool _al_equal(const al_object_t *a, const al_object_t *b){
    if(a == b){ return true; }
    if(a == NULL || b == NULL || a->tag != b->tag){
        return false;
    }
    if(a->tag != AL_TAG_CONS){
        return a->car == b->car;
    }

    return _al_equal(a->car, b->car) && _al_equal(a->cdr, b->cdr);
}

/**
 * @brief Test a whether an object is in a list.
 * 
 * @param object 
 * @param list 
 * @return al_object_t* 
 */
static al_object_t* _al_find_pair(al_object_t *object, al_object_t *list){
    for(; list != NULL; list = list->cdr){
        if(list->car != NULL && _al_equal(object, list->car->car)){
            return list->car;
        }
    }

    return NULL;
}

/**
 * @brief Search for an object in a given environment
 * 
 * @param object 
 * @param list 
 * @return al_object_t* 
 */
static al_object_t* _al_lookup_env(al_object_t *object, al_object_t *list){
    for(al_object_t *pair; list != NULL; list = list->cdr){
        if((pair = _al_find_pair(object, list->car)) != NULL){
            return pair->cdr;
        }
    }

    return NULL;
}

/**
 * @brief Add a pair of key/value data to a given environment.
 * 
 * @param env 
 * @param key 
 * @param value
 * @return al_object_t* 
 */
static al_object_t* _al_set_env(
    al_object_t *env, al_object_t *key, al_object_t *value 
){
    // ---
    al_object_t *pair = NULL;
    al_object_t *frame = NULL;
    al_gc_protect(&env, &key, &value, &pair, &frame, NULL);
    pair = _al_new_cons(key, value);
    frame = _al_new_cons(pair, env->car);
    env->car = frame;
    al_gc_pop();
    return env;
}

/**
 * @brief Create a new environment.
 * 
 * @param env 
 * @return al_object_t* 
 */
static al_object_t* _al_new_env(al_object_t*env){
    return _al_new_cons(NULL, env);
}

/**
 * @brief Reverse a list
 * 
 * @param list 
 * @return al_object_t* 
 */
static al_object_t* _al_reverse_list(al_object_t *list){
    if(list == NULL){ return NULL; }
    al_object_t *prev = NULL;
    al_object_t *current = list;
    al_object_t *next = list->cdr;
    while(current){
        current->cdr = prev;
        prev = current;
        current = next;
        if(next != NULL){ next = next->cdr; }
    }

    return prev;
}

/**
 * @brief Evaluate AttoLisp expression and return the result.
 * 
 * @param env 
 * @param object 
 * @return al_object_t* 
 */
static al_object_t* _al_eval(al_object_t *expr, al_object_t *env){
AGAIN:
    if(expr == NULL){ return NULL; }
    if(expr->tag == AL_TAG_ATOM){
        return _al_is_number(AL_TEXT(expr)) ? expr : _al_lookup_env(expr, env);
    }
    if(expr->tag != AL_TAG_CONS){ return expr; }

    al_object_t *head = expr->car;
    if(AL_TEXT(head) == AL_QUOTE){
        return expr->cdr->car;
    }else if(AL_TEXT(head) == AL_COND){
        al_object_t *item = NULL;
        al_object_t *cond = NULL;
        al_gc_protect(&expr, &env, &item, &cond, NULL);
        for(item = expr->cdr; item != NULL; item = item->cdr){
            cond = item->car;
            if(_al_eval(cond->car, env) != NULL){
                expr = cond->cdr->car;
                al_gc_pop();
                goto AGAIN;
            }
        }
        return NULL; 
    }else if(AL_TEXT(head) == AL_DEFINE){
        al_object_t *name = NULL;
        al_object_t *value = NULL;
        al_gc_protect(&env, &name, &value, NULL);
        name = expr->cdr->car;
        value = _al_eval(expr->cdr->cdr->car, env);
        _al_set_env(env, name, value);
        al_gc_pop();
        return value;
    }else if(AL_TEXT(head) == AL_LAMBDA){
        expr->cdr->tag = AL_TAG_LAMBDA;
        return expr->cdr;
    }

    al_object_t *fn = NULL;
    al_object_t *args = NULL;
    al_object_t *params = NULL;
    al_object_t *param = NULL;
    al_gc_protect(&expr, &env, &fn, &args, &params, &param, NULL);
    fn = _al_eval(head, env);
    if(fn->tag == AL_TAG_FUNCTION){
        for(params = expr->cdr; params != NULL; params = params->cdr){
            param = _al_eval(params->car, env);
            args = _al_new_cons(param, args);
        }
        al_object_t *result = ((al_function_t)fn->car)(_al_reverse_list(args));
        al_gc_pop();
        return result;
    }else if(fn->tag == AL_TAG_LAMBDA){
        al_object_t *callenv = _al_new_env(env);
        args = fn->car;
        al_object_t *item = NULL;
        al_gc_protect(&callenv, &item, NULL);
        for(params=expr->cdr; params!=NULL; params=params->cdr, args=args->cdr){
            param = _al_eval(params->car, env);
            _al_set_env(callenv, args->car, param);
        }

        for(item = fn->cdr; item != NULL; item = item->cdr){
            if(item->cdr == NULL){
                expr = item->car;
                env = callenv;
                al_gc_pop();
                al_gc_pop();
                goto AGAIN;
            }
            _al_eval(item->car, callenv);
        }
        al_gc_pop();
        al_gc_pop();
    }

    return NULL;
}

/**
 * @brief Print the string representation of object to the stdout.
 * 
 * @param object 
 */
static void _al_print(al_object_t *object){
    if(object == NULL){ fputs("()", stdout); }
    else if(object->tag == AL_TAG_ATOM){
        fputs(AL_TEXT(object), stdout);
    }else if(object->tag == AL_TAG_FUNCTION){
        printf("<C@%p>", (void*)object);
    }else if(object->tag == AL_TAG_LAMBDA){
        fputs("<lambda ", stdout);
        _al_print(object->car);
        fputs(">", stdout);
    }else if(object->tag == AL_TAG_CONS){
        fputs("(", stdout);
        while(1){
            _al_print(object->car);
            if(object->cdr == NULL){ break; }
            fputs(" ", stdout);
            if(object->cdr->tag != AL_TAG_CONS){
                fputs(". ", stdout);
                _al_print(object->cdr);
                break;
            }
            object = object->cdr;
        }
        fputs(")", stdout);
    }
}

// -------------------------------------------------------------------------
//                  B U I L T I N    A P I
// -------------------------------------------------------------------------
al_object_t* al_car(al_object_t *object){
    return object->car->car;
}

al_object_t* al_cdr(al_object_t *object){
    return object->car->cdr;
}

al_object_t* al_cons(al_object_t *object){
    return _al_new_cons(object->car, object->cdr->car);
}

al_object_t* al_equalp(al_object_t *object){
    al_object_t *cmp = object->car;
    for(object = object->cdr; object != NULL; object = object->cdr){
        if(!_al_equal(cmp, object->car)){ return NULL; }
    }
    return al_true;
}

al_object_t* al_pairp(al_object_t *object){
    return (object->car!=NULL && object->car->tag==AL_TAG_CONS)? al_true : NULL;
}


al_object_t* al_nullp(al_object_t *object){
    return (object->car == NULL) ? al_true : NULL;
}

al_object_t* al_sum(al_object_t *object){
    long sum = 0;
    for(; object != NULL; object = object->cdr){
        sum += atol(AL_TEXT(object->car));
    }
    return _al_new_atom(_al_num_to_string(sum));
}

al_object_t* al_sub(al_object_t *object){
    long sub;
    if(object->cdr == NULL){
        sub = -atol(AL_TEXT(object->car));
    }else{
        sub = atol(AL_TEXT(object->car));
        for(object = object->cdr; object != NULL; object = object->cdr){
            sub -= atol(AL_TEXT(object->car));
        }
    }

    return _al_new_atom(_al_num_to_string(sub));
}

al_object_t* al_mul(al_object_t *object){
    long mul = 1;
    for(; object != NULL; object = object->cdr){
        mul *= atol(AL_TEXT(object->car));
    }
    return _al_new_atom(_al_num_to_string(mul));
}

al_object_t* al_print(al_object_t *object){
    _al_print(object->car);
    return NULL;
}

al_object_t* al_newline(al_object_t *object){
    puts("");
    return NULL;
}


al_object_t* al_read(al_object_t *object){
    return _al_read(stdin);
}

void al_gc_init(void);
al_object_t* al_gc_alloc(al_tag_t tag, al_object_t *car, al_object_t *cdr){}
void al_gc_protect(al_object_t **root, ...){}
void al_gc_pop(void){}
void al_gc_copy(al_object_t **root){}
void al_gc_collect(void){}

// ---

