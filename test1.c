section .data
; char hello
hello: db str_1
section .text
global test
; test function
test:
push ebp
mov ebp, esp
sub esp, 16
add esp, 16
pop ebp
ret
section .rodata
str_1: db 'h','e','l','l','o',0
