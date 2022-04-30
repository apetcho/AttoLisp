(define else #t)
(define factorial (lambda (n)
    (cond ((equal? n 0) a)
          (else (* n (factorial (- n 1)))))))

(print (factorial 3))
(newline)
