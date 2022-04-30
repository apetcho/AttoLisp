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

static al_object_t* _al_read_list(FILE *stream, const char *text);
static al_object_t* _al_read_object(FILE *stream, const char *text);
static al_object_t* _al_read(FILE *stream);
static void _al_print(al_object_t *object);
static al_object_t* _al_eval(al_object_t *env, al_object_t *object);

static bool _al_match_number(const char *data);

static al_object_t* _al_new_function(al_function_t fn);
static al_object_t _al_new_atom(const char *data);
static al_object_t* _al_new_cons(al_object_t *env, al_object_t *object);
static const char* _al_read_token(FILE *stream);
static bool _al_equal(const al_object_t *a, const al_object_t *b);
static al_object_t* _al_find_pair(al_object_t object, al_object_t *list);
static al_object_t* _al_lookup_env(al_object_t *object, al_object_t *list);
static al_object_t* _al_set_env(
    al_object_t *env, al_object_t *key, al_object_t *val);
static al_object_t* _al_new_env(al_object_t*env);
static al_object_t* _al_reverse_list(al_object_t *list);

