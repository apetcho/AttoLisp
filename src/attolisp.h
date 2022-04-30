#ifndef __ATTOLISP_H_
#define __ATTOLISP_H_

#define ATTOLISP_TYPE_INT       1
#define ATTOLISP_TYPE_CELL      2
#define ATTOLISP_TYPE_SYMBOL    3
#define ATTOLISP_TYPE_PRIMITIVE 4
#define ATTOLISP_TYPE_FUNCTION  5
#define ATTOLISP_TYPE_MACRO     6
#define ATTOLISP_TYPE_ENV       7
#define ATTOLISP_TYPE_MOVED     8
#define ATTOLISP_TYPE_TRUE      9
#define ATTOLISP_TYPE_NIL       10
#define ATTOLISP_TYPE_DOT       11
#define ATTOLISP_TYPE_PAREN     12

struct al_object_t;
typedef al_object_t* (*al_primitive_t)(
    void *root, struct al_object_t **env, struct al_object_t **args
);

typedef struct al_object_t {
    int type;   /* object type */
    int size;   /* total size  */
    union{
        // integer
        int value;
        // cell
        struct{
            struct al_object_t *car;
            struct al_object_t *cdr;
        };
        // symbol
        char name[1];
        // primive function
        al_primitive_t fn;
        // macro
        struct {
            struct al_object_t *params;
            struct al_object_t *body;
            struct al_object_t *env;
        };
        // environment frame
        struct {
            struct al_object_t *vars;
            struct al_object_t *up;
        };
        // forwarding pointer
        void *moved;
    };
} al_object_t;

#endif
