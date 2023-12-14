#include <stdio.h>
#include <stdlib.h>
#include "compiler.h"
struct compiler_process *compile_process_create(const char *filename, const char *file_name_out, int flags)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        return NULL;
    }

    FILE *out_file = NULL;
    if (file_name_out)
    {
        out_file = fopen(file_name_out, "w");
    }
    if (!out_file)
    {
        return NULL;
    }

    struct compiler_process* process = calloc(1,sizeof(struct compiler_process));
    process->flags = flags;
    process->cfile.fp = file;
    process->ofile = out_file;
    return process;

}