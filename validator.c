#include "compiler.h"

void validate_initialize(struct compiler_process* process)
{
	//TODO: Create initialize function
}

void validate_destruct(struct compiler_process* process)
{
	//TODO: Create destruct function
}

int validate_tree()
{
	return VALIDATION_ALL_OK;
}


int validate(struct compiler_process* process)
{
	int res = 0;
	
	validate_initialize(process);
	res = validate_tree();
	validate_destruct(process);
	
	return res;
}


