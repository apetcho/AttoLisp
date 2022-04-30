(define cadr (lambda (x) (car (cdr x))))
(define cdar (lambda (x) (cdr (car x))))
(define caar (lambda (x) (car (car x))))
(define cddr (lambda (x) (cdr (cdr x))))
(define caadr (lambda (x) (car (car (cdr x)))))
(define cadar (lambda (x) (car (cdr (car x)))))
(define caaar (lambda (x) (car (car (car x)))))
(define caddr (lambda (x) (car (cdr (cdr x)))))
(define cdadr (lambda (x) (cdr (car (cdr x)))))
(define cddar (lambda (x) (cdr (cdr (car x)))))
(define cdaar (lambda (x) (cdr (car (car x)))))
(define cdddr (lambda (x) (cdr (cdr (cdr x)))))
(define not (lambda (x) (cond ((null? x) #t) (#t #f))))
(define atom? (lambda (x)
    (cond ((null? x) #f)
          ((pair? x) #f)
          (#t #t))))
(define else #t)
(define println (lambda (x) (print x) (newline)))
(define assert (lambda (expr expect)
    (cond ((equal? expr expect)
        ((lambda () (print (quote pass:_)) (println expr))))
          (else
            ((lambda () (print (quote fail:_)) (println expr)))))))

(define square (lambda (x) (* x x)))
(assert (square 3) 9)

(define length (lambda (x)
    (cond ((null? x) 0)
          (else (+ 1 (length (cdr x)))))))

(assert (length (quote (1 2 3))) 3)

(print (quote len=))
(println (length (quote (1 2 3))))
(println (quote fact15))

(define fac (lambda (n)
    (cond ((equal? n 0) 1)
          (else (* n (fac (- n 1)))))))
(fac 2)