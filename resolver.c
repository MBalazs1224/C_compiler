#include "compiler.h"
#include <stdlib.h>
#include "helpers/vector.h"
#include <assert.h>
void resolver_follow_part(struct resolver_process* resolver, struct node* node, struct resolver_result* result);
struct resolver_entity* resolver_follow_exp (struct resolver_process* resolver, struct node* node, struct resolver_result* result);
struct resolver_result* resolver_follow(struct resolver_process* resolver, struct node* node);
struct resolver_entity* resolver_follow_array_bracket(struct resolver_process* resolver, struct node* node, struct resolver_result* result);
bool resolver_result_failed(struct resolver_result* result)
{
    return result->flags & RESOLVER_RESULT_FLAG_FAILED;
}

bool resolver_result_ok(struct resolver_result*result)
{
    return !resolver_result_failed(result);
}

bool resolver_result_finished(struct resolver_result*result)
{
    return result->flags & RESOLVER_RESULT_FLAG_RUNTIME_NEEDED_TO_FINISH_PATH;
}

struct resolver_entity* resolver_result_entity_root(struct resolver_result* result)
{
    return result->entity;
}


struct resolver_entity* resolver_result_entity_next(struct resolver_entity* entity)
{
    return entity->next;
}

struct resolver_entity* resolver_entity_clone(struct resolver_entity* entity)
{
    if (!entity)
    {
        return NULL;
    }

    struct resolver_entity* entity_new = calloc(1, sizeof(struct resolver_entity));
    memcpy(entity_new,entity,sizeof(struct resolver_entity));
    return entity_new;
}


struct resolver_entity* resolver_result_entity(struct resolver_result*result)
{
    if (resolver_result_failed((result)))
    {
        return NULL;
    }
    return result->entity;
}


struct resolver_result* resolver_new_result(struct resolver_process* process)
{
    struct resolver_result* result = calloc(1, sizeof(struct resolver_result));
    result->array_data.array_entities = vector_create(sizeof(struct resolver_entity*));
    return result;
}

void resolver_result_free(struct resolver_result* result)
{
    vector_free(result->array_data.array_entities);
    free(result);
}

struct resolver_scope* resolver_process_scope_current(struct resolver_process* process)
{
    return process->scope.current;
}


void resolver_runtime_needed(struct resolver_result* result, struct resolver_entity* last_entity)
{
    result->entity = last_entity;
    result->flags &= ~RESOLVER_RESULT_FLAG_RUNTIME_NEEDED_TO_FINISH_PATH;
}

void resolver_result_entity_push (struct resolver_result* result, struct resolver_entity* entity)
{
    // Set teh correct first and last entities
    if (!result->first_entity_const)
    {
        result->first_entity_const = entity;
    }
    if (!result->last_entity)
    {
        result->entity = entity;
        result->last_entity = entity;
        result->count++;
        return;
    }

    result->last_entity->next = entity;
    entity->prev = result->last_entity;
    result->last_entity = entity;
    result->count++;
}

struct resolver_entity* resolver_result_peek(struct resolver_result* result)
{
    return result->last_entity;
}

// Returns the last entity that's not a rule
struct resolver_entity* resolver_result_peek_ignore_rule_entity(struct resolver_result* result)
{
    struct resolver_entity* entity = resolver_result_peek(result);
    while (entity && entity->type == RESOLVER_ENTITY_TYPE_RULE)
    {
        entity = entity->prev;
    }
    return entity;
}


// Pops the last resolver_entity from the result
struct resolver_entity* resolver_result_pop(struct resolver_result* result)
{
    struct resolver_entity* entity = result->last_entity;
    if (!result->entity)
    {
        return NULL;
    }
    // If the current entity is the last entity we have to set everything back a step
    if (result->entity == result->last_entity)
    {
        result->entity = result->last_entity->prev;
        result->last_entity = result->last_entity->prev;
        result->count--;
        goto out;
    }
    result->last_entity = result->last_entity->prev;
    result->count--;

    out:
    // If the count is 0 then it means that there are no more entities so we reset everything (to NULL)
    if (result->count == 0)
    {
        result->first_entity_const = NULL;
        result->last_entity = NULL;
        result->entity = NULL;
    }
    entity->prev = NULL;
    entity->next = NULL;
    return entity;
}

struct vector* resolver_array_data_vec(struct resolver_result* result)
{
    return result->array_data.array_entities;
}

struct compiler_process* resolver_compiler(struct resolver_process* process)
{
    return process->compiler;
}


struct resolver_scope* resolver_scope_current(struct resolver_process* process)
{
    return process->scope.current;
}

struct resolver_scope* resolver_scope_root(struct resolver_process*process)
{
    return process->scope.root;
}

struct resolver_scope* resolver_new_scope_create()
{
    struct resolver_scope* scope = calloc(1,sizeof(struct resolver_scope));
    scope->entities = vector_create(sizeof(struct resolver_entity*));
    return scope;
}

struct resolver_scope* resolver_new_scope(struct resolver_process* resolver, void* private ,int flags)
{
    struct resolver_scope* scope = resolver_new_scope_create();
    if (!scope)
    {
        return NULL;
    }

    resolver->scope.current->next = scope;
    scope->prev = resolver->scope.current;
    resolver->scope.current = scope;
    scope->private = private;
    scope->flags = flags;
    return scope;
}


void resolver_finish_scope(struct resolver_process* resolver)
{
    struct resolver_scope* scope = resolver->scope.current;
    resolver->scope.current = scope->prev;
    resolver->callbacks.delete_scope(scope);
    free(scope);
}


struct resolver_process* resolver_new_process(struct compiler_process* compiler, struct resolver_callback* callbacks)
{
    struct resolver_process* process = calloc(1, sizeof(struct resolver_process));
    process->compiler = compiler;
    memcpy(&process->callbacks, callbacks, sizeof(process->callbacks));
    process->scope.root = resolver_new_scope_create();
    process->scope.current = process->scope.root;
    return process;
}

struct resolver_entity* resolver_create_new_entity(struct resolver_result* result, int type, void* private)
{
    struct resolver_entity*entity = calloc(1, sizeof(struct resolver_entity));
    if (!entity)
    {
        return NULL;
    }
    entity->type = type;
    entity->private = private;
    return entity;
}

struct resolver_entity* resolver_create_new_entity_for_unsupported_node(struct resolver_result* result, struct node* node)
{
    struct resolver_entity* entity = resolver_create_new_entity(result,RESOLVER_ENTITY_TYPE_UNSUPPORTED,NULL);
    if (!entity)
    {
        return NULL;
    }
    entity->node = node;
    entity->flags = RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_LEFT_ENTITY | RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_NEXT_ENTITY;
    return entity;
}


struct resolver_entity* resolver_create_new_entity_for_array_brackets(struct resolver_result* result, struct resolver_process*process, struct node* node, struct node* array_index_node, int index, struct datatype* dtype, void* private, struct resolver_scope*scope)
{
    struct resolver_entity* entity = resolver_create_new_entity(result, RESOLVER_ENTITY_TYPE_ARRAY_BRACKET,private);
    if (!entity)
    {
        return NULL;
    }

    entity->scope = scope;
    assert(entity->scope);
    entity->name = NULL;
    entity->dtype = *dtype;
    entity->node = node;
    entity->array.index = index;
    entity->array.dtype = *dtype;
    entity->array.array_index_node = array_index_node;
    int array_index_val = 1;
    if (array_index_node->type == NODE_TYPE_NUMBER)
    {
        array_index_val = array_index_node->llnum;
    }
    entity->offset = array_offset(dtype,index,array_index_val);
    return entity;
}

struct resolver_entity* resolver_create_new_entity_for_merged_array_bracket(struct resolver_result* result, struct resolver_process* process, struct node* node, struct node* array_index_node, int index, struct datatype* dtype, void* private, struct resolver_scope* scope)
{
    struct resolver_entity*entity = resolver_create_new_entity(result,RESOLVER_ENTITY_TYPE_ARRAY_BRACKET,private);
    if (!entity)
    {
        return NULL;
    }

    entity->scope = scope;
    assert(scope);
    entity->name = NULL;
    entity->dtype = *dtype;
    entity->node = node;
    entity->array.index = index;
    entity->array.dtype = *dtype;
    entity->array.array_index_node = array_index_node;
    return entity;
}


struct resolver_entity* resolver_create_new_unknown_entity( struct resolver_process* process,struct resolver_result* result, struct datatype* dtype,struct node* node,struct resolver_scope* scope, int offset)
{
    struct resolver_entity* entity = resolver_create_new_entity(result,RESOLVER_ENTITY_TYPE_GENERAL,NULL);
    if (!entity)
    {
        return NULL;
    }

    entity->flags = RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_NEXT_ENTITY | RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_LEFT_ENTITY;
    entity->scope = scope;
    entity->dtype = *dtype;
    entity->node = node;
    entity->offset = offset;
    return entity;
}

struct resolver_entity* resolver_create_new_unary_indirection_entity( struct resolver_process* process,struct resolver_result* result,struct node* node,int indirection_depth)
{
    struct resolver_entity* entity = resolver_create_new_entity(result,RESOLVER_ENTITY_TYPE_UNARY_INDIRECTION,NULL);
    if (!entity)
    {
        return NULL;
    }

    entity->flags = RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_NEXT_ENTITY | RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_LEFT_ENTITY;
    entity->node = node;
    entity->indirection.depth = indirection_depth;
    return entity;
}

struct resolver_entity* resolver_create_new_unary_get_address_entity( struct resolver_process* process,struct resolver_result* result,struct datatype* dtype,struct node* node,struct resolver_scope* scope,int offset)
{
    struct resolver_entity* entity = resolver_create_new_entity(result,RESOLVER_ENTITY_TYPE_UNARY_GET_ADDRESS,NULL);
    if (!entity)
    {
        return NULL;
    }

    entity->flags = RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_NEXT_ENTITY | RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_LEFT_ENTITY;
    entity->node = node;
    entity->dtype = *dtype;
    entity->dtype.flags |= DATATYPE_FLAG_IS_POINTER;
    entity->scope = scope;
    entity->dtype.pointer_depth++;
    return entity;
}

struct resolver_entity* resolver_create_new_cast_entity( struct resolver_process* process,struct resolver_scope* scope,struct datatype* cast_dtype)
{
    struct resolver_entity* entity = resolver_create_new_entity(NULL,RESOLVER_ENTITY_TYPE_CAST,NULL);
    if (!entity)
    {
        return NULL;
    }

    entity->flags = RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_NEXT_ENTITY | RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_LEFT_ENTITY;
    entity->scope = scope;
    entity->dtype = *cast_dtype;
    return entity;
}

struct resolver_entity* resolver_create_new_entity_for_var_node_custom_scope( struct resolver_process* process,struct node* var_node, void* private,struct resolver_scope* scope,int offset)
{
    assert(var_node->type == NODE_TYPE_VARIABLE);
    struct resolver_entity* entity = resolver_create_new_entity(NULL,RESOLVER_ENTITY_TYPE_VARIABLE,private);
    if (!entity)
    {
        return NULL;
    }
	
    entity->scope = scope;
    assert(entity->scope);
    entity->dtype = var_node->var.type;
    entity->var_data.dtype = var_node->var.type;
    entity->node = var_node;
    entity->name = var_node->var.name;
    entity->offset = offset;
    return entity;
}

struct resolver_entity* resolver_create_new_entity_for_var_node( struct resolver_process* process,struct node* var_node, void* private,int offset)
{
    return resolver_create_new_entity_for_var_node_custom_scope(process,var_node,private, resolver_scope_current(process),offset);
}

struct resolver_entity* resolver_create_new_entity_for_var_node_no_push( struct resolver_process* process,struct node* var_node, void* private,int offset,struct resolver_scope* scope)
{
    struct resolver_entity* entity = resolver_create_new_entity_for_var_node_custom_scope(process, var_node, private, scope, offset);
    if (!entity)
    {
        return NULL;
    }
    if (scope->flags & RESOLVER_SCOPE_FLAG_IS_STACK)
    {
        entity->flags |= RESOLVER_ENTITY_FLAG_IS_STACK;
    }
    return entity;
}

struct resolver_entity*     resolver_new_entity_for_var_node( struct resolver_process* process,struct node* var_node, void* private,int offset)
{
    struct resolver_entity* entity = resolver_create_new_entity_for_var_node_no_push(process,var_node,private,offset,
                                                                                     resolver_process_scope_current(process));
    if (!entity)
    {
        return NULL;
    }
    vector_push(process->scope.current->entities,&entity);
    return entity;
}

void resolver_new_entity_for_rule(struct resolver_process*process, struct resolver_result* result, struct resolver_entity_rule* rule)
{
    struct resolver_entity* entity_rule = resolver_create_new_entity(result, RESOLVER_ENTITY_TYPE_RULE,NULL);
    entity_rule->rule = *rule;
    resolver_result_entity_push(result,entity_rule);
}

struct resolver_entity* resolver_make_entity(struct resolver_process* process, struct resolver_result* result, struct datatype*custom_dtype, struct node*node , struct resolver_entity* guided_entity, struct resolver_scope*scope)
{
    struct resolver_entity* entity = NULL;
    int offset = guided_entity->offset;
    int flags = guided_entity->flags;
    switch (node->type) {
        case NODE_TYPE_VARIABLE:
            entity = resolver_create_new_entity_for_var_node_no_push(process,node,NULL,offset,scope);
            break;
        default:
            entity = resolver_create_new_unknown_entity(process,result,custom_dtype,node,scope,offset);
    }
    if (entity)
    {
        entity->flags |= flags;
        if (custom_dtype)
        {
            entity->dtype = *custom_dtype;
        }
        entity->private = process->callbacks.make_private(entity,node,offset,scope);
    }

    return entity;
}

struct resolver_entity* resolver_create_new_entity_for_function_call(struct resolver_result* result, struct resolver_process* process,struct resolver_entity* left_operand_entity, void* private)
{
    struct resolver_entity*entity = resolver_create_new_entity(result,RESOLVER_ENTITY_TYPE_FUNCTION_CALL,private);
    if (!entity)
    {
        return NULL;
    }
    entity->dtype = left_operand_entity->dtype;
    entity->func_call_data.arguments = vector_create(sizeof(struct node*));
    return entity;
}

// --------------------------------------------------------------------------------
// The variables have to be aligned to the  CPU's word size (on 32 bit -> 4 bytes) because on every CPU cycle it can only load the word size (4 bytes for us) -> if there is an int after a char then there has to be a 3 byte gap between them so the int won't be cut into 2 parts. For example -> char c (offset 0-1), int i (offset 4-8), if it was char c (offset 0-1) and int (offset 1-5) then the CPU could only load the bytes from offset 0-4 and the integer would be cut into 2 parts (from byte 1-4 and 4-5). This is why we need to align the memory. Union offsets are always 0 until a sub struct is discovered.
// --------------------------------------------------------------------------------




struct resolver_entity* resolver_register_function(struct resolver_process*process, struct node* func_node, void*private) {
    struct resolver_entity *entity = resolver_create_new_entity(NULL, RESOLVER_ENTITY_TYPE_FUNCTION, private);
    if (!entity)
    {
        return NULL;
    }
    entity->name = func_node->func.name;
    entity->node = func_node;
    entity->dtype = func_node->func.rtype;
    entity->scope = resolver_process_scope_current(process);
    vector_push(process->scope.root->entities,&entity);
    return entity;
}

// Gets the entity within the scope with the entity_type type
struct resolver_entity* resolver_get_entity_in_scope_with_entity_type(struct resolver_result*result, struct resolver_process* resolver, struct resolver_scope* scope, const char* entity_name, int entity_type)
{
    if (result && result->last_struct_union_entity)
    {
        // This means we are accessing a structure or union

        struct resolver_scope* scope = result->last_struct_union_entity->scope;
        struct node* out_node = NULL;
        struct datatype* node_var_datatype = &result->last_struct_union_entity->dtype;
        int offset = struct_offset(resolver_compiler(resolver),node_var_datatype->type_str,entity_name,&out_node,0,0);
        if (node_var_datatype->type == DATA_TYPE_UNION)
        {
            // Unions have 0 offsets
            offset = 0;
        }
        return resolver_make_entity(resolver,result,NULL,out_node,&(struct resolver_entity){.type = RESOLVER_ENTITY_TYPE_VARIABLE,.offset = offset},scope);
    }

    // Must be primitive type

    // Loop through all the variables inside the scope backwards
    /*
     * int abc()
     * {
     *      int a,;
     *      int b;
     *      int d = a; -> We work up the stack to find a, that's why we need to iterate backwards, because everythind needs to be declared before it can be accessed -> a will be above d
     * }
     */
    vector_set_peek_pointer_end(scope->entities);
    vector_set_flag(scope->entities,VECTOR_FLAG_PEEK_DECREMENT);
    struct resolver_entity* current = vector_peek_ptr(scope->entities);

    while (current)
    {
        // Ignores all entities that are not the same type as the given entity_type
        // entity_type == -1 -> deal with every entity type, don't ignore anything
        if (entity_type != -1 && current->type != entity_type)
        {
            current = vector_peek_ptr(scope->entities);
            continue;
        }
        if (S_EQ(current->name,entity_name))
        {
            // We found the entity we are looking for
            break;
        }
        current = vector_peek_ptr(scope->entities);
    }
    return current;
}

struct resolver_entity* resolver_get_entity_for_type(struct resolver_result* result, struct resolver_process* resolver, const char* entity_name, int entity_type)
{
    struct resolver_scope* scope = resolver->scope.current;
    struct resolver_entity* entity = NULL;

    // Tries to find the entity in a scope, if it can't, it tries the next scope


    /*
     * int test()
     * {
     *      int b;
     *      if(1)
     *      {
     *          int a = b;
     *          It won't find be in the if scope, so because the vector is being iterated backwards,          it will look for b in the previous scope (the scope of test()), it will do it until           it finds it or there are no more scopes
     *      }
     *
     * }
     *
     */
    while (scope)
    {
        entity = resolver_get_entity_in_scope_with_entity_type(result,resolver,scope,entity_name,entity_type);
        if (entity)
        {
            break;
        }
        scope = scope->prev;
    }

    if (entity)
    {
        memset(&entity->last_resolve,0, sizeof(entity->last_resolve));
    }

    return entity;
}

struct resolver_entity* resolver_get_entity(struct resolver_result* result, struct resolver_process* resolver, const char* entity_name)
{
    // -1 means it won't ignore any type and will return the first entity with the given entity_name (doesn't matter if it is a function or variable etc.)
    return resolver_get_entity_for_type(result,resolver,entity_name,-1);
}

struct resolver_entity* resolver_get_entity_in_scope(struct resolver_result* result,struct resolver_process* resolver, struct resolver_scope* scope, const char* entity_name)
{
    // -1 means it won't ignore any type and will return the first entity with the given entity_name (doesn't matter if it is a function or variable etc.)
    return resolver_get_entity_in_scope_with_entity_type(result,resolver,scope,entity_name,-1);
}

struct resolver_entity* resolver_get_variable(struct resolver_result* result,struct resolver_process* resolver, const char* entity_name)
{
    // We only want variables
    return resolver_get_entity_for_type(result,resolver,entity_name,RESOLVER_ENTITY_TYPE_VARIABLE);
}

struct resolver_entity* resolver_get_function_in_scope(struct resolver_result* result,struct resolver_process* resolver,const char* func_name, struct resolver_scope* scope)
{
    // We only want functions
    return resolver_get_entity_for_type(result,resolver,func_name,RESOLVER_ENTITY_TYPE_FUNCTION);
}

// Gets the function entity in the root scope
struct resolver_entity* resolver_get_function(struct resolver_result* result,struct resolver_process* resolver,const char* func_name)
{
    struct resolver_entity* entity = NULL;
    struct resolver_scope* scope = resolver->scope.root;
    entity = resolver_get_function_in_scope(result,resolver,func_name,scope);
    return entity;
}

// Looks for the entity with the given name
struct resolver_entity* resolver_follow_for_name (struct resolver_process* resolver, const char* name, struct resolver_result* result)
{
    struct resolver_entity* entity = resolver_entity_clone(resolver_get_entity(result,resolver,name));
    if (!entity)
    {
        return NULL;
    }
    resolver_result_entity_push(result,entity);

    // The first found identifier
    if (!result->identifier)
    {
        result->identifier = entity;
    }
    // If the entity is a variable or function and the datatype is struct or union then we have to set the last struct or union entity to the found entity
    if (entity->type == RESOLVER_ENTITY_TYPE_VARIABLE && datatype_is_struct_or_union(&entity->var_data.dtype) || (entity->type == RESOLVER_ENTITY_TYPE_FUNCTION && datatype_is_struct_or_union(&entity->dtype)))
    {
        result->last_struct_union_entity = entity;
    }
    return entity;
}

struct resolver_entity* resolver_follow_identifier(struct resolver_process* resolver, struct node* node, struct resolver_result* result)
{
    struct resolver_entity* entity = resolver_follow_for_name(resolver,node->sval,result);
    if (entity)
    {
        entity->last_resolve.referencing_node = node;
    }
    return entity;
}

struct resolver_entity*resolver_follow_variable (struct resolver_process* resolver, struct node* var_node, struct resolver_result* result)
{
    struct resolver_entity* entity = resolver_follow_for_name(resolver,var_node->var.name,result);
    return entity;
}

struct resolver_entity*  resolver_follow_struct_exp(struct resolver_process* resolver, struct node* node, struct resolver_result* result)
{
    struct resolver_entity* entity = NULL;

    // a.b -> resolver_follow_part will create the "a" entity and then we access that with the resolver_result_peek
    resolver_follow_part(resolver,node->exp.left,result);
    struct resolver_entity* left_entity = resolver_result_peek(result);
    struct resolver_entity_rule rule = {};
    // This is a pointer, and we don't know the offset of it at compile time that's why we mustn't merge it with the left entity
    if (is_access_node_with_op(node,"->"))
    {
        rule.left.flags = RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_NEXT_ENTITY;
        // Indicate to "dereference the pointer" -> int* a; *a = 50;
        if (left_entity->type != RESOLVER_ENTITY_TYPE_FUNCTION_CALL)
        {
            rule.right.flags = RESOLVER_ENTITY_FLAG_DO_INDIRECTION;
        }
    }

    resolver_new_entity_for_rule(resolver,result,&rule);
    resolver_follow_part(resolver,node->exp.right,result);

    return NULL;

}

struct resolver_entity* resolver_follow_array (struct resolver_process* resolver, struct node* node, struct resolver_result* result)
{
    // Follow the path of the left and right part of the array
    resolver_follow_part(resolver,node->exp.left,result);
    struct resolver_entity* left_entity = resolver_result_peek(result);
    resolver_follow_part(resolver,node->exp.right,result);

    return left_entity;
}

struct datatype* resolver_get_datatype(struct resolver_process* resolver, struct node* node)
{
    struct resolver_result* result = resolver_follow(resolver,node);
    if (!resolver_result_ok(result))
    {
        return NULL;
    }
    return &result->last_entity->dtype;
}
// It computes the stack size for the function call arguments (how many bytes will it take up aligned to the word size to fully push the arguments to the stack)
void resolver_build_function_call_arguments(struct resolver_process* resolver,struct node* argument_node, struct resolver_entity* root_func_call_entity, size_t* total_size_out)
{
    // If there are arguments we need to push them to the stack
    // abc(50,20,30) -> 50,20,30 would be in the expression node
    if (is_argument_node(argument_node))
    {
        // 50,30 would be in the exp node, so we need to build it for both parts
        resolver_build_function_call_arguments(resolver,argument_node->exp.left,root_func_call_entity,total_size_out);
        resolver_build_function_call_arguments(resolver,argument_node->exp.right,root_func_call_entity,total_size_out);
    }
    // If there is a parenthesis (abc((50))) we need to do it differently a bit
    else if (argument_node->type == NODE_TYPE_EXPRESSION_PARENTHESIS)
    {
        resolver_build_function_call_arguments(resolver,argument_node->parenthesis.exp,root_func_call_entity,total_size_out);
    }
    // If the node is valdi then we push
    else if (node_valid(argument_node))
    {
        vector_push(root_func_call_entity->func_call_data.arguments, &argument_node);
        // We assume it's numeric, so we set it to the word size
        size_t stack_change = DATA_SIZE_DWORD;
        struct datatype* dtype = resolver_get_datatype(resolver,argument_node);
        // The change will be the word size (4 bytes) unless it's a structure

        // Numeric values don't return a datatype,so they will stay the word size, but for example variable names (abc(var_name)) will and we need to process them accordingly

        if (dtype)
        {
            stack_change = datatype_element_size(dtype);
            if (stack_change < DATA_SIZE_DWORD)
            {
                stack_change = DATA_SIZE_DWORD;
            }
            // Align the value to the word size
            stack_change = align_value(stack_change,DATA_SIZE_DWORD);
        }
        // Add this argument's size to the total stack size
        *total_size_out += stack_change;
    }

}

struct resolver_entity* resolver_follow_function_call (struct resolver_process* resolver,  struct resolver_result* result ,struct node* node)
{
    resolver_follow_part(resolver,node->exp.left,result);
    struct resolver_entity* left_entity = resolver_result_peek(result);
    struct resolver_entity* func_call_entity = resolver_create_new_entity_for_function_call(result,resolver,left_entity,NULL);
    assert(func_call_entity);
    func_call_entity->flags |= RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_LEFT_ENTITY | RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_NEXT_ENTITY;
    // Right node is the func arguments, left one is the func name
    resolver_build_function_call_arguments(resolver,node->exp.right,func_call_entity,&func_call_entity->func_call_data.stack_size);

    // Push the function call entity to the stack
    resolver_result_entity_push(result,func_call_entity);

    return func_call_entity;
}
struct resolver_entity* resolver_follow_parentheses (struct resolver_process* resolver, struct node* node, struct resolver_result* result)
{
    //abc() -> exp.left -> abc, exp.right ->(), if the left part of the expression is an identifier then it means that it must be a function call
    if (node->exp.left->type == NODE_TYPE_IDENTIFIER)
    {
        return resolver_follow_function_call(resolver,result,node);
    }
    // It must be a normal expression
    return resolver_follow_struct_exp(resolver,node->parenthesis.exp,result);
}

struct resolver_entity* resolver_follow_exp (struct resolver_process* resolver, struct node* node, struct resolver_result* result)
{
    struct resolver_entity* entity = NULL;
    if (is_access_node(node))
    {
        entity = resolver_follow_struct_exp(resolver,node,result);
    }
    else if (is_array_node(node))
    {
        entity = resolver_follow_array(resolver,node,result);
    }
    else if (is_parentheses_node(node))
    {
        entity = resolver_follow_parentheses(resolver,node,result);
    }
    return entity;
}

void resolver_array_bracket_set_flags(struct resolver_entity* bracket_entity,struct datatype* dtype,struct node* bracket_node, int index)
{
    /*
     * char* abc; not an array
     * abc[4]; we are accessing it like an array
     */
    // We can do abc[1][2] that's why we need array_brackets_count(dtype) <= index
    if (!(dtype->flags & DATATYPE_FLAG_IS_ARRAY) || array_brackets_count(dtype) <= index)
    {
        // We don't merge and set teh flag to know it's a pointer array
        bracket_entity->flags = RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_LEFT_ENTITY | RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_NEXT_ENTITY | RESOLVER_ENTITY_FLAG_DO_IS_POINTER_ARRAY_ENTITY;
    }
    else if (bracket_node->bracket.inner->type != NODE_TYPE_NUMBER)
    {
        bracket_entity->flags = RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_LEFT_ENTITY | RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_NEXT_ENTITY;

    }
    else
    {
        // If it's a number then we just use it's offset
        bracket_entity->flags = RESOLVER_ENTITY_FLAG_JUST_USE_OFFSET;
    }
}

struct resolver_entity* resolver_follow_array_bracket(struct resolver_process* resolver, struct node* node, struct resolver_result* result)
{
    // Make sure it's a bracket node
    assert(node->type == NODE_TYPE_BRACKET);
    int index = 0;
    struct datatype dtype;
    struct resolver_scope* scope = NULL;
    // Get the last entity but ignore the rules
    struct resolver_entity* last_entity = resolver_result_peek_ignore_rule_entity(result);
    scope = last_entity->scope;
    dtype = last_entity->dtype;
    // This function can be recursive so we always need to get the index+1 because if we call this function again on this node, we are no longer accessing the 0th
    /*
     * int abc[50][2];
     * abc[3][4] -> when we call it on [3] the index is 0 but when we call it on [4] the index is 1 so we get the last index and add 1 to it to get teh correct index
     */
    if (last_entity->type == RESOLVER_ENTITY_TYPE_ARRAY_BRACKET)
    {
        index = last_entity->array.index + 1;
    }
    // We need to calculate the array size differently (and we calculate it on index+1 just like above)
    if (dtype.flags & DATATYPE_FLAG_IS_ARRAY)
    {
        dtype.array.size = array_brackets_calculate_size_from_index(&dtype,dtype.array.brackets,index + 1);

    }
    // We need to reduce the datatype

    void* private = resolver->callbacks.new_array_entity(result,node);
    struct resolver_entity* array_bracket_entity = resolver_create_new_entity_for_array_brackets(result,resolver,node,node->bracket.inner,index,&dtype,private,scope);
    struct resolver_entity_rule rule = {};
    // Set the correct array bracket flags (it's a pointer, use the offset etc.)
    resolver_array_bracket_set_flags(array_bracket_entity,&dtype,node,index);

    // Tell the code generator that the entity is using array brackets
    last_entity->flags |= RESOLVER_ENTITY_FLAG_USES_ARRAY_BRACKETS;

    /*
     * char abc[3];
     * abc[3][2]; -> after processing [3] it becomes abc[2] so the pointer depth actually decreased
     */
    if (array_bracket_entity->flags & RESOLVER_ENTITY_FLAG_DO_IS_POINTER_ARRAY_ENTITY)
    {
        datatype_decrement_pointer(&array_bracket_entity->dtype);
    }
    resolver_result_entity_push(result,array_bracket_entity);
    return array_bracket_entity;
}
struct resolver_entity* resolver_follow_part_return_entity(struct resolver_process* resolver, struct node* node, struct resolver_result* result);

struct resolver_entity* resolver_follow_expression_parenthesis(struct resolver_process* resolver, struct node* node, struct resolver_result* result)
        {
    return resolver_follow_part_return_entity(resolver,node->parenthesis.exp,result);
}

struct resolver_entity* resolver_follow_unsupported_unary_node(struct resolver_process* resolver,struct node* node, struct resolver_result* result)
{
    return resolver_follow_part_return_entity(resolver,node->unary.operand,result);
}

struct resolver_entity* resolver_follow_unsupported_node(struct resolver_process* resolver,struct node* node, struct resolver_result* result)
{
    bool followed = false;
    switch (node->type) {
        case NODE_TYPE_UNARY:
            resolver_follow_unsupported_unary_node(resolver,node,result);
            followed = true;
            break;
        default:
            followed = false;
            break;
    }

    struct resolver_entity* unsupported_entity = resolver_create_new_entity_for_unsupported_node(result,node);
    assert(unsupported_entity);
    resolver_result_entity_push(result,unsupported_entity);
    return unsupported_entity;
}

struct resolver_entity* resolver_follow_cast(struct resolver_process* resolver, struct node* node, struct resolver_result* result)
{
    struct resolver_entity* operand_entity = NULL;
    resolver_follow_unsupported_node(resolver,node->cast.operand,result);
    operand_entity = resolver_result_peek(result);
    operand_entity->flags |= RESOLVER_ENTITY_FLAG_WAS_CASTED;

    struct resolver_entity* cast_entity = resolver_create_new_cast_entity(resolver,operand_entity->scope,&node->cast.dtype);
    resolver_result_entity_push(result,cast_entity);

    return cast_entity;

}

struct resolver_entity* resolver_follow_indirection(struct resolver_process* resolver, struct node* node, struct resolver_result* result)
{
    // Indirection **a.b, *a.b, ******k etc.
    resolver_follow_part(resolver,node->unary.operand,result);

    struct resolver_entity* last_entity = resolver_result_peek(result);
    // If the last_entity doesn't exist it must be unsupported
    if (!last_entity)
    {
        last_entity = resolver_follow_unsupported_node(resolver,node->unary.operand,result);
    }
    struct resolver_entity* entity = resolver_create_new_unary_indirection_entity(resolver,result,node,node->unary.indirection.depth);
    resolver_result_entity_push(result,entity);
    return entity;

}

struct resolver_entity* resolver_follow_unary_address(struct resolver_process* resolver, struct node* node, struct resolver_result* result)
{
    //&a.b.c -> trying to find the address of c
    resolver_follow_part(resolver,node->unary.operand,result);

    // Create the entity and push it to the stack
    struct resolver_entity* last_entity = resolver_result_peek(result);
    struct resolver_entity* unary_address_entity = resolver_create_new_unary_get_address_entity(resolver,result,&last_entity->dtype,node,last_entity->scope,last_entity->offset);
    resolver_result_entity_push(result,unary_address_entity);

    return unary_address_entity;
}

struct resolver_entity* resolver_follow_unary(struct resolver_process* resolver, struct node* node, struct resolver_result* result)
{
    struct resolver_entity* result_entity = NULL;
    if (op_is_indirection(node->unary.op))
    {
        result_entity = resolver_follow_indirection(resolver,node,result);
    }
    // If we need something's address (& operator)
    else if (op_is_address(node->unary.op))
    {
        result_entity = resolver_follow_unary_address(resolver,node,result);
    }
    return result_entity;

}

struct resolver_entity* resolver_follow_part_return_entity(struct resolver_process* resolver, struct node* node, struct resolver_result* result)
{
    struct resolver_entity* entity = NULL;
    switch (node->type) {
        case NODE_TYPE_IDENTIFIER:
                entity = resolver_follow_identifier(resolver,node,result);
            break;
        case NODE_TYPE_VARIABLE:
            entity = resolver_follow_variable(resolver,node,result);
            break;
        case NODE_TYPE_EXPRESSION:
            entity = resolver_follow_exp(resolver,node,result);
            break;
        case NODE_TYPE_BRACKET:
            entity = resolver_follow_array_bracket(resolver,node,result);
            break;
        case NODE_TYPE_EXPRESSION_PARENTHESIS:
            entity = resolver_follow_expression_parenthesis(resolver,node,result);
            break;
        case NODE_TYPE_CAST:
            entity = resolver_follow_cast(resolver,node,result);
            break;
        case NODE_TYPE_UNARY:
            entity = resolver_follow_unary(resolver,node,result);
            break;
        default:
            {
                // Can't do anything, create a special entity that needs more computation at runtime
                entity = resolver_follow_unsupported_node(resolver,node,result);
            }
            break;
    }
    if (entity)
    {
        entity->result = result;
        entity->resolver =resolver;
    }
    return entity;
}

void resolver_follow_part(struct resolver_process* resolver, struct node* node, struct resolver_result* result)
{
    resolver_follow_part_return_entity(resolver,node,result);

}

void resolver_rule_apply_rules(struct resolver_entity* rule_entity,struct resolver_entity* left_entity,struct resolver_entity* right_entity)
{
    assert(rule_entity->type == RESOLVER_ENTITY_TYPE_RULE);

    // Apply the rules for both side entities
    if (left_entity)
    {
        left_entity->flags |= rule_entity->rule.left.flags;
    }
    if (right_entity)
    {
        right_entity->flags |= rule_entity->rule.right.flags;
    }
}

void resolver_push_vector_of_entities(struct resolver_result*result, struct vector* vec)
{
    // Set the flags to iterate the vector backwards
    vector_set_peek_pointer_end(vec);
    vector_set_flag(vec,VECTOR_FLAG_PEEK_DECREMENT);

    // Push the entire vector into the result->entity stack
    struct resolver_entity* entity = vector_peek_ptr(vec);
    while (entity)
    {
        resolver_result_entity_push(result,entity);
        entity = vector_peek_ptr(vec);
    }
}

void resolver_execute_rules(struct resolver_process* resolver, struct resolver_result* result)
{
    // Iterate through all entities in the result stack, pop them and if they are a rule then apply them to the correct entity nad at the end push everything back to the stack (saved_entities is a temporary vector to store the popped entities)
    struct vector* saved_entities = vector_create(sizeof(struct resolver_entity*));
    struct resolver_entity* entity = resolver_result_pop(result);
    struct resolver_entity* last_processed_entity = NULL;

    // Iterate the vector
    while (entity)
    {
        if (entity->type == RESOLVER_ENTITY_TYPE_RULE)
        {
            // Execute the rules so they get applied
            struct resolver_entity* left_entity = resolver_result_pop(result);
            resolver_rule_apply_rules(entity,left_entity,last_processed_entity);
            entity = left_entity;
        }
        vector_push(saved_entities,&entity);
        last_processed_entity = entity;
        entity = resolver_result_pop(result);
    }
    // Push everything back to the stack
    resolver_push_vector_of_entities(result,saved_entities);
}

struct resolver_entity* resolver_merge_compile_time_result(struct resolver_process* resolver, struct resolver_result* result, struct resolver_entity*left_entity, struct resolver_entity* right_entity)
        {
            if (left_entity && right_entity)
            {
                // If one of the no merge flag is present than that means we cannot merge the entities, so we return null (and possibly perform memory cleanup, that's for later implementation)
                if (left_entity->flags & RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_NEXT_ENTITY || right_entity->flags & RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_LEFT_ENTITY)
                {
                    goto no_merge_possible;
                }
                struct resolver_entity* result_entity = resolver->callbacks.merge_entities(resolver,result,left_entity,right_entity);
                if (!result_entity)
                {
                    goto no_merge_possible;
                }
                return result_entity;
            }



            no_merge_possible:
            return NULL;
}


void _resolver_merge_compile_times(struct resolver_process* resolver, struct resolver_result* result)
{
    struct vector* saved_entities = vector_create(sizeof(struct resolver_entity*));
    while (true)
    {
        struct resolver_entity* right_entity = resolver_result_pop(result);
        struct resolver_entity* left_entity = resolver_result_pop(result);
        // If we don't have 2 entities than we cannot merge (if we don't have the right entity it means we had none)
        if (!right_entity)
        {
            break;
        }
        if (!left_entity)
        {
            // We only have on entity
            resolver_result_entity_push(result,right_entity);
            break;
        }
        struct resolver_entity* merged_entity = resolver_merge_compile_time_result(resolver,result,left_entity,right_entity);

        // If the merge was successful push it back to the stack so the merging can continue using this now merged entity
        if (merged_entity)
        {
            resolver_result_entity_push(result,merged_entity);
            continue;
        }

        // If the merged_entity returned null it means that the merge wasn't possible (maybe incompatible types etc.), so we need to set the no merge flag to prevent it trying to merge again
        right_entity->flags |= RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_LEFT_ENTITY;

        // We push the right entity to the saved_entities and the left_entity back to teh stack, because the left_entity might be able to merge with the next entity, so we need to try again
        vector_push(saved_entities,&right_entity);
        resolver_result_entity_push(result,left_entity);

    }
    resolver_push_vector_of_entities(result,saved_entities);
    vector_free(saved_entities);
}

void resolver_merge_compile_times(struct resolver_process* resolver, struct resolver_result* result)
{
    // Keep looping until we sure there is nothing more to merge (we are constantly pushing and popping from the stack, so false loop stops can happen)
    size_t total_entities = 0;
    do {
        total_entities = result->count;
        _resolver_merge_compile_times(resolver,result);
    }while(total_entities != 1 && total_entities != result->count);
}

void resolver_finalize_result_flags(struct resolver_process* resolver, struct resolver_result* result)
{
    int flags = RESOLVER_RESULT_FLAG_FIRST_ENTITY_PUSH_VALUE;
    // We need to iterate through all of the results
    struct resolver_entity* entity = result->entity;
    struct resolver_entity* first_entity = entity;
    struct resolver_entity* last_entity = result->last_entity;
    bool does_get_address = false;
    if (entity == last_entity)
    {
        // We only have one entity
        // Check if it is a struct/union but not a poitner
        if (last_entity->type == RESOLVER_ENTITY_TYPE_VARIABLE && datatype_is_struct_or_union_non_pointer(&last_entity->dtype))
        {
            flags |= RESOLVER_RESULT_FLAG_FIRST_ENTITY_LOAD_TO_EBX;
            flags &= ~RESOLVER_RESULT_FLAG_FIRST_ENTITY_PUSH_VALUE;
        }
        result->flags = flags;
        return;
    }
    while (entity)
    {
        if (entity->flags & RESOLVER_ENTITY_FLAG_DO_INDIRECTION)
        {
            // Load the address of the first entity since we have indirection (pointer dereference)
            flags |= RESOLVER_RESULT_FLAG_FIRST_ENTITY_LOAD_TO_EBX | RESOLVER_RESULT_FLAG_FINAL_INDIRECTION_REQUIRED_FOR_VALUE;
            flags &= ~RESOLVER_RESULT_FLAG_FIRST_ENTITY_PUSH_VALUE;
        }
        if (entity->type == RESOLVER_ENTITY_TYPE_UNARY_GET_ADDRESS)
        {
            flags |= RESOLVER_RESULT_FLAG_FIRST_ENTITY_LOAD_TO_EBX | RESOLVER_RESULT_FLAG_DOES_GET_ADDRESS;
            flags &= ~RESOLVER_RESULT_FLAG_FIRST_ENTITY_PUSH_VALUE | RESOLVER_RESULT_FLAG_FINAL_INDIRECTION_REQUIRED_FOR_VALUE;
            does_get_address = true;
        }
        if (entity->type == RESOLVER_ENTITY_TYPE_FUNCTION_CALL)
        {
            flags |= RESOLVER_RESULT_FLAG_FIRST_ENTITY_LOAD_TO_EBX;
            flags &= ~RESOLVER_RESULT_FLAG_FIRST_ENTITY_PUSH_VALUE;
        }
        if (entity->type == RESOLVER_ENTITY_TYPE_ARRAY_BRACKET)
        {
            if (entity->dtype.flags == DATATYPE_FLAG_IS_POINTER)
            {
                flags |= RESOLVER_RESULT_FLAG_FIRST_ENTITY_PUSH_VALUE;
                flags &= ~RESOLVER_RESULT_FLAG_FIRST_ENTITY_LOAD_TO_EBX;
            }
            else
            {
                flags |= RESOLVER_RESULT_FLAG_FIRST_ENTITY_LOAD_TO_EBX;
                flags &= ~RESOLVER_RESULT_FLAG_FIRST_ENTITY_PUSH_VALUE;
            }
            // const char* abc; abc[5];
            if (entity->flags & RESOLVER_ENTITY_FLAG_DO_IS_POINTER_ARRAY_ENTITY)
            {
                flags |= RESOLVER_RESULT_FLAG_FINAL_INDIRECTION_REQUIRED_FOR_VALUE;

            }
        }
        if (entity->type == RESOLVER_ENTITY_TYPE_GENERAL)
        {
            flags |= RESOLVER_RESULT_FLAG_FIRST_ENTITY_LOAD_TO_EBX | RESOLVER_RESULT_FLAG_FINAL_INDIRECTION_REQUIRED_FOR_VALUE;
            flags &= ~ RESOLVER_RESULT_FLAG_FIRST_ENTITY_PUSH_VALUE;
        }
        entity = entity->next;
    }
    if (last_entity->dtype.flags & DATATYPE_FLAG_IS_ARRAY && (!does_get_address && last_entity->type == RESOLVER_ENTITY_TYPE_VARIABLE &&!(last_entity->flags & RESOLVER_ENTITY_FLAG_USES_ARRAY_BRACKETS)))
    {
        // char abc[50]; char* p = abc; -> the generator would set it to char*p = abc[0] which is not true
        flags &= ~RESOLVER_RESULT_FLAG_FINAL_INDIRECTION_REQUIRED_FOR_VALUE;
    }
    else if (last_entity->type == RESOLVER_ENTITY_TYPE_VARIABLE)
    {
        flags |= RESOLVER_RESULT_FLAG_FINAL_INDIRECTION_REQUIRED_FOR_VALUE;
    }
    if (does_get_address)
    {
        flags |= ~RESOLVER_RESULT_FLAG_FINAL_INDIRECTION_REQUIRED_FOR_VALUE;
    }
    result->flags |= flags;
}

void resolver_finalize_unary(struct resolver_process* resolver, struct resolver_result* result, struct resolver_entity* entity)
{
    struct resolver_entity* previous_entity = entity->prev;
    if (!previous_entity)
    {
        // There is nothing to do
        return;
    }
    entity->scope = previous_entity->scope;
    entity->dtype = previous_entity->dtype;
    entity->offset = previous_entity->offset;
    if (entity->type == RESOLVER_ENTITY_TYPE_UNARY_INDIRECTION)
    {
        // char* abc;
        // *abc = 50; -> it can become this abc = 50; The datatype of abc now is char not char*
        int indirection_depth = entity->indirection.depth;
        entity->dtype.pointer_depth -= indirection_depth;
        // It's not a pointer anymore
        if (entity->dtype.pointer_depth <= 0)
        {
            entity->dtype.flags &= ~DATATYPE_FLAG_IS_POINTER;
        }
    }
    else if(entity->type == RESOLVER_ENTITY_TYPE_UNARY_GET_ADDRESS)
    {
        /*
         * int a = 50;
         * &a -> will return int*, it becomes a pointer
         */
        entity->dtype.flags |= DATATYPE_FLAG_IS_POINTER;
        entity->dtype.pointer_depth++;
    }
}

void resolver_finalize_last_entity(struct resolver_process* resolver, struct resolver_result* result)
{
    struct resolver_entity*last_entity = resolver_result_peek(result);
    switch (last_entity->type)
    {
        case RESOLVER_ENTITY_TYPE_UNARY_INDIRECTION:
        case RESOLVER_ENTITY_TYPE_UNARY_GET_ADDRESS:
            resolver_finalize_unary(resolver,result,last_entity);
            break;
    }
}

void resolver_finalize_result(struct resolver_process* resolver, struct resolver_result* result)
{
    struct resolver_entity* first_entity = resolver_result_entity_root(result);
    if (!first_entity)
    {
        // There is nothing on the stack, so we can't do anything
        return;
    }
    resolver->callbacks.set_result_base(result,first_entity);
    resolver_finalize_result_flags(resolver,result);
    resolver_finalize_last_entity(resolver,result);
}

// Node will be the expression node (for example a.b.c -> it will be a)
struct resolver_result* resolver_follow(struct resolver_process* resolver, struct node* node)
{
    assert(resolver);
    assert(node);
    struct resolver_result* result = resolver_new_result(resolver);
    resolver_follow_part(resolver,node,result);
    // Make sure we have a root entity
    if (!resolver_result_entity_root(result))
    {
        result->flags |= RESOLVER_RESULT_FLAG_FAILED;
    }
    resolver_execute_rules(resolver,result);
    resolver_merge_compile_times(resolver,result);
    resolver_finalize_result(resolver,result);
    return result;
}
