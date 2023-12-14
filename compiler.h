#ifndef COMPILER_H
#define COMPILER_H
#include <stdio.h>


enum
{
    COMPILER_FILE_COMPILED_OK,
    COMPILER_FAILED_WITH_ERRORS
};

struct compiler_process
{
    // The flags in regards to how the file should be compiler
    int flags;

    struct compiler_process_input_file {
        FILE* fp;
        const char* abs_path;
    } cfile;
    FILE* ofile;
};


int compile_file(const char* filename, const char* out_filename, int flags);

struct compiler_process *compile_process_create(const char *filename, const char *file_name_out, int flags);


#endif