#ifndef __ATTOLISP_H_
#define __ATTOLISP_H_
enum{
    ATTOLISP_TYPE_INT=1,
    ATTOLISP_TYPE_CELL,
    ATTOLISP_TYPE_SYMBOL,
    ATTOLISP_TYPE_PRIMITIVE,
    ATTOLISP_TYPE_FUNCTION,
    ATTOLISP_TYPE_MACRO,
    ATTOLISP_TYPE_ENV,
    ATTOLISP_TYPE_MOVED,
    ATTOLISP_TYPE_TRUE,
    ATTOLISP_TYPE_NIL,
    ATTOLISP_TYPE_DOT,
    ATTOLISP_TYPE_LPAREN 
};

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
