#include "compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include "helpers/vector.h"
struct compiler_process *compiler_process_create(const char *filename, const char *file_name_out, int flags)
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
    process->node_vec = vector_create(sizeof(struct node*));
    process->node_tree_vec = vector_create(sizeof(struct node*));
    process->flags = flags;
    process->cfile.fp = file;
    process->ofile = out_file;

    symresolver_initialize(process);
    symresolver_new_table(process);
    return process;

}


char compiler_process_next_char(struct lex_process* lex_process){
    struct compiler_process* compiler = lex_process->compiler;
    compiler->pos.col += 1;

    char c = getc(compiler->cfile.fp);

    if (c=='\n')
    {
       compiler->pos.line+=1;
       compiler->pos.col=1;
    }
    
    return c;
}

char compiler_process_peek_char(struct lex_process* lex_process){
    struct compiler_process* compiler = lex_process->compiler;
    char c = getc(compiler->cfile.fp);
    ungetc(c,compiler->cfile.fp);
    return c;
}

void compiler_process_push_char(struct lex_process* lex_process,char c){
    struct compiler_process* compiler = lex_process->compiler;

    ungetc(c,compiler->cfile.fp);
}