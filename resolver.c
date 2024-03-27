#include "compiler.h"
#include <stdlib.h>
#include "helpers/vector.h"
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

struct resolver_scope* resovler_process_scope_current(struct resolver_process* process)
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
