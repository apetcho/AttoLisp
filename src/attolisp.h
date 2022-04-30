#ifndef __ATTOLISP_H_
#define __ATTOLISP_H_
#include<stddef.h>
#include<stdbool.h>
#include<stdio.h>

typedef enum {
    AL_TAG_CONS,
    AL_TAG_ATOM,
    AL_TAG_FUNCTION,
    AL_TAG_LAMBDA
} al_tag_t;

typedef struct al_object al_object_t;
struct al_object{
    al_object_t *car;
    al_object_t *cdr;
    al_tag_t tag;
};

typedef al_object_t* (*al_function_t)(al_object_t *object);

typedef struct {
    al_object_t *heap;
    al_object_t *from;
    al_object_t *to;
    al_object_t *allocptr;
    al_object_t *scanptr;
    size_t nroots;
    size_t roottop;
} al_gc_t;

void al_gc_init(void);
al_object_t* al_gc_alloc(al_tag_t tag, al_object_t *car, al_object_t *cdr);
void al_gc_protect(al_object_t **root, ...);
void al_gc_pop(void);
void al_gc_copy(al_object **root);
void al_gc_collect(void);

// ---

al_object_t* al_car(al_object_t *object);
al_object_t* al_cdr(al_object_t *object);
al_object_t* al_cons(al_object_t *object);
al_object_t* al_equalp(al_object_t *object);
al_object_t* al_pairp(al_object_t *object);
al_object_t* al_nullp(al_object_t *object);
al_object_t* al_sum(al_object_t *object);
al_object_t* al_sub(al_object_t *object);
al_object_t* al_mul(al_object_t *object);
al_object_t* al_print(al_object_t *object); // display
al_object_t* al_newline(al_object_t *object);
al_object_t* al_read(al_object_t *object);

#endif
