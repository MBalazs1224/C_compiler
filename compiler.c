#include "compiler.h"
#include <stdlib.h>
#include <stdarg.h>

struct lex_process_functions compiler_lex_functions = {
    .next_char = compiler_process_next_char,
    .peek_char = compiler_process_peek_char,
    .push_char = compiler_process_push_char};

void compiler_node_error(struct node* node, const char* message, ...)
{
	va_list args;
	va_start(args,message);
	vfprintf(stderr,message,args);
	va_end(args);
	
	fprintf(stderr, " on line %i, col %i in file %s\n",node->pos.line,node->pos.col,node->pos.filename);
	exit(-1);
}

void compiler_error(struct compiler_process *compiler, const char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
    fprintf(stderr, " on line %i, col %i in file %s\n", compiler->pos.line, compiler->pos.col, compiler->pos.filename);
    exit(-1);
}

void compiler_warning(struct compiler_process *compiler, const char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
    fprintf(stderr, " on line %i, col %i in file %s\n", compiler->pos.line, compiler->pos.col, compiler->pos.filename);
}

int compile_file(const char *filename, const char *out_filename, int flags)
{
    struct compiler_process *process = compiler_process_create(filename, out_filename, flags);

    if (!process)
    {
        return COMPILER_FAILED_WITH_ERRORS;
    }
    // perfrom lexical analysis
    struct lex_process *lex_process = lex_process_create(process, &compiler_lex_functions, NULL);
    if (!lex_process)
    {
        return COMPILER_FAILED_WITH_ERRORS;
    }

    if (lex(lex_process) != LEXICAL_ANALYSIS_ALL_OK)
    {
        return COMPILER_FAILED_WITH_ERRORS;
    }

    process->token_vec = lex_process->token_vec;
    // perform parsing

    if (parse(process) != PARSE_ALL_OK)
    {
        return COMPILER_FAILED_WITH_ERRORS;
    }
	
	if (validate(process) != VALIDATION_ALL_OK)
	{
		return COMPILER_FAILED_WITH_ERRORS;
	}

    if (codegen(process) != CODEGEN_ALL_OK)
    {
        return COMPILER_FAILED_WITH_ERRORS;
    }

    fclose(process->ofile);
    return COMPILER_FILE_COMPILED_OK;
}