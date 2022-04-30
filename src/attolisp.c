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
static al_object_t *al_atom =NULL;
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

static void _al_print(al_object_t *object);
static al_object_t* _al_eval(al_object_t *env, al_object_t *object);


static al_object_t* _al_find_pair(al_object_t object, al_object_t *list);
static al_object_t* _al_lookup_env(al_object_t *object, al_object_t *list);
static al_object_t* _al_set_env(
    al_object_t *env, al_object_t *key, al_object_t *val);
static al_object_t* _al_new_env(al_object_t*env);
static al_object_t* _al_reverse_list(al_object_t *list);

