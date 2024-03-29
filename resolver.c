#include "compiler.h"
#include <stdlib.h>
#include "helpers/vector.h"
#include <assert.h>
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

struct resolver_entity* resolver_reslt_entity_root(struct resolver_result* result)
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
    while (entity && entity->type == RESOLVER_ENTITY_TYPE_RULE);
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
    struct resolver_entity* entity = resolver_create_new_entity(NULL,NODE_TYPE_VARIABLE,NULL);
    if (!entity)
    {
        return NULL;
    }

    entity->flags = RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_NEXT_ENTITY | RESOLVER_ENTITY_FLAG_NO_MERGE_WITH_LEFT_ENTITY;
    entity->scope = scope;
    assert(entity->scope);
    entity->dtype = var_node->var.type;
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
    struct resolver_entity* entity = resolver_create_new_entity(NULL,NODE_TYPE_VARIABLE,NULL);
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

struct resolver_entity* resolver_new_entity_for_var_node( struct resolver_process* process,struct node* var_node, void* private,int offset)
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

        struct resolver_scope* scope = result->last_struct_union_entity;
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

struct resolver_entity* resolver_get_function(struct resolver_result* result,struct resolver_process* resolver,const char* func_name)
{
    struct resolver_entity* entity = NULL;
    // Gets the function entity in the root scope
    struct resolver_scope* scope = resolver->scope.root;
    entity = resolver_get_function_in_scope(result,resolver,func_name,scope);
    return entity;
}
















