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
push dword 50
pop eax
mov dword [ebp-4], eax
push dword 20
pop eax
mov dword [ebp-4], eax
add esp, 16
pop ebp
ret
section .rodata
str_1: db 'h','e','l','l','o',0
