#include <stdio.h>
#include "helpers/vector.h"
#include "compiler.h"
int main(){

    int res = compile_file("./test.c","test.c",0);

    if (res == COMPILER_FILE_COMPILED_OK)
    {
        printf("FINE\n");
    }
    else if (res == COMPILER_FAILED_WITH_ERRORS){
        printf("ERRORS\n");
    }
    else{
        printf("Unkonw errors\n");
    }
    

    return 0;
}