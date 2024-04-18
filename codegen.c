#include "compiler.h"
#include <stdarg.h>
#include <stdio.h>
#include "helpers/vector.h"
#include <assert.h>
static struct compiler_process* current_process = NULL;
static struct node* current_function = NULL;
void codegen_generate_exp_node(struct node* node, struct history*history);
int codegen_label_count();


enum
{
    RESPONSE_FLAG_ACKNOWLEDGED = 0b00000001,
    RESPONSE_FLAG_PUSHED_STRUCT = 0b00000010,
    RESPONSE_FLAG_RESOLVED_ENTITY = 0b00000100,
    RESPONSE_FLAG_UNARY_GET_ADDRESS = 0b00001000
};

#define RESPONSE_SET(x) &(struct response{x};)
#define RESPONSE_EMPTY_RESPONSE_SET()

struct response_data
{
    union
    {
        struct resolver_entity* resolved_entity;
    };
};

struct response
{
    int flags;
    struct response_data* data;
};

void codegen_response_expect()
{
    // Whenever someone expects a response, first we put an empty response to the stack
    struct response* res = calloc(1,sizeof (struct response));
    vector_push(current_process->generator->responses,&res);
}

struct response_data* codegen_response_data(struct response* response)
{
    return &response->data;
}

struct response* codegen_response_pull()
{
    // Get the last response in the vector
    struct response* res = vector_back_ptr_or_null(current_process->generator->responses);
    if (res)
    {
        vector_pop(current_process->generator->responses);
    }
    return res;

}

void codegen_response_acknowledge(struct response* response_in)
{
    struct response* res = vector_back_ptr_or_null(current_process->generator->responses);
    if (res)
    {
        res->flags |= response_in->flags;
        if (response_in->data->resolved_entity)
        {
            res->data->resolved_entity = response_in->data->resolved_entity;
        }
        res->flags |= RESPONSE_FLAG_ACKNOWLEDGED;
    }
}

bool codegen_response_acknowledged(struct response* res)
{
    return res && res->flags & RESPONSE_FLAG_ACKNOWLEDGED;
}

bool codegen_response_has_entity(struct response* res)
{
    return codegen_response_acknowledged(res) && res->flags & RESPONSE_FLAG_RESOLVED_ENTITY && res->data->resolved_entity;
}

struct history
{
    int flags;
};

static struct history* history_begin(int flags)
{
    struct history* history = calloc(1,sizeof(history));
    history->flags = flags;
    return history;
}

static struct history* history_down(struct history* history, int flags)
{
    struct history* new_history = calloc(1,sizeof(struct history));
    memcpy(new_history,history,sizeof(struct history));
    new_history->flags = flags;

    return new_history;
}

struct resolver_default_entity_data* codegen_entity_private(struct resolver_entity* entity)
{
    return resolver_default_entity_private(entity);
}

void codegen_new_scope(int flags)
{
    resolver_default_new_scope(current_process->resolver,flags);
}

void codegen_finish_scope()
{
    resolver_default_finish_scope(current_process->resolver);
}

struct node* codegen_node_next()
{
    return vector_peek_ptr(current_process->node_tree_vec);
}

void asm_push_args(const char* ins, va_list args)
{
    va_list args2;
    va_copy(args2,args);
    vfprintf(stdout,ins,args);
    fprintf(stdout, "\n");
    if (current_process->ofile)
    {
        vfprintf(current_process->ofile,ins,args2);
        fprintf(current_process->ofile,"\n");
    }
}

void asm_push_ins_push_with_data(const char* fmt, int stack_entity_type, const char* stack_entity_name,int flags, struct stack_frame_data*data,...)
{
    char tmp_buff[200];
    sprintf(tmp_buff,"push %s",fmt);
    va_list args;
    va_start(args,data);
    asm_push_args(tmp_buff,args);
    va_end(args);
    flags |= STACK_FRAME_ELEMENT_FLAG_HAS_DATATYPE;
    // Assert that we are in a function, because we work with stack and that's only possible in a  function
    assert(current_function);
    stackframe_push(current_function,&(struct stack_frame_element){.type=stack_entity_type,.name=stack_entity_name,.flags=flags,.data=*data});
}

void asm_push(const char* ins, ...)
{
    va_list args;
    va_start(args,ins);
    asm_push_args(ins,args);
    va_end(args);
}

void asm_push_no_nl(const char* ins,...)
{
    va_list args;
    va_start(args,ins);
    vfprintf(stdout,ins,args);
    va_end(args);
    if (current_process->ofile)
    {
        va_list args;
        va_start(args,ins);
        vfprintf(current_process->ofile,ins,args);
        va_end(args);
    }
}
// Generate push instruction and change stackframe
void asm_push_ins_push(const char* fmt, int stack_entity_type, const char* stack_entity_name,...)
{
    char tmp_buff[200];
    sprintf(tmp_buff,"push %s", fmt);
    va_list args;
    va_start(args,stack_entity_name);
    asm_push_args(tmp_buff,args);
    va_end(args);
    assert(current_function);
    stackframe_push(current_function,&(struct stack_frame_element){.type = stack_entity_type,.name=stack_entity_name});
}

int asm_push_ins_pop(const char* fmt, int expecting_stack_entity_type, const char* expecting_stack_entity_name,...)
{
    char tmp_buff[200];
    sprintf(tmp_buff,"pop %s",fmt);
    va_list args;
    va_start(args,expecting_stack_entity_name);
    asm_push_args(tmp_buff,args);
    va_end(args);
    // Make sure we are in a function because we only use the stack in functions
    assert(current_function);
    struct stack_frame_element* element = stackframe_back(current_function);
    int flags = element->flags;
    stackframe_pop_expecting(current_function,expecting_stack_entity_type,expecting_stack_entity_name);
    return flags;
}

void asm_push_ebp()
{
    asm_push_ins_push("ebp", STACK_FRAME_ELEMENT_TYPE_SAVED_BP,"function_entry_saved_ebp");
}

void asm_pop_ebp()
{
    // If the name and tag doesn't match then it will throw error, it helps making sure that we pop off the correct things
    asm_push_ins_pop("ebp",STACK_FRAME_ELEMENT_TYPE_SAVED_BP,"function_entry_saved_ebp");
}

void codegen_stack_sub_with_name(size_t stack_size, const char*name)
{
    if (stack_size != 0)
    {
        stackframe_sub(current_function,STACK_FRAME_ELEMENT_TYPE_UNKNOWN,name,stack_size);
        asm_push("sub esp, %lld",stack_size);
    }
}

void codegen_stack_sub(size_t stack_size)
{
    codegen_stack_sub_with_name(stack_size, "literal_stack_change");
}

struct resolver_entity* codegen_new_scope_entity(struct node* var_node, int offset, int flags)
{
    return resolver_default_new_scope_entity(current_process->resolver,var_node,offset,flags);
}

void codegen_stack_add_with_name(size_t stack_size, const char* name)
{
    if (stack_size != 0)
    {
        stackframe_add(current_function,STACK_FRAME_ELEMENT_TYPE_UNKNOWN,name,stack_size);
        asm_push("add esp, %lld",stack_size);
    }
}

void codegen_stack_add(size_t stack_size)
{
    codegen_stack_add_with_name(stack_size,"literal_stack_change");
}


const char* codegen_get_label_for_string(const char* str)
{
    const char* result = NULL;
    struct code_generator* generator = current_process->generator;
    vector_set_peek_pointer(generator->string_table,0);
    struct string_table_element* current = vector_peek_ptr(generator->string_table);
    while (current)
    {
        if (S_EQ(current->str,str))
        {
            result = current->label;
            break;
        }
        current = vector_peek_ptr(generator->string_table);
    }

}

const char* codegen_register_string(const char* str)
{
    const char* label = codegen_get_label_for_string(str);
    if (label)
    {
        // We already registered this string, just return the label pointing to the string memory
        return label;
    }
    struct string_table_element* str_elem = calloc(1, sizeof(struct string_table_element));
    int label_id = codegen_label_count();
    sprintf((char*)str_elem->label, "str_%i", label_id);
    str_elem->str = str;
    vector_push(current_process->generator->string_table,&str_elem);
    return str_elem->label;
}
struct code_generator* codegenerator_new(struct compiler_process* process)
{
    struct code_generator* generator = calloc(1, sizeof(struct code_generator));
    generator->string_table = vector_create(sizeof(struct string_table_element*));
    generator->entry_points = vector_create(sizeof(struct codegen_entry_point*));
    generator->exit_points = vector_create(sizeof(struct codegen_exit_point*));
    generator->responses = vector_create(sizeof(struct response*));
    return generator;
}

void codegen_register_exit_point(int exit_point_id)
{
    struct code_generator* gen = current_process->generator;
    struct codegen_exit_point* exit_point = calloc(1, sizeof(struct codegen_exit_point));
    exit_point->id = exit_point_id;
    vector_push(gen->exit_points,&exit_point);
}

struct codegen_exit_point* codegen_current_exit_point()
{
    struct code_generator* gen = current_process->generator;
    return vector_back_ptr_or_null(gen->exit_points);
}

int codegen_label_count()
{
    static int count = 0;
    count++;
    return count;
}

void codegen_begin_exit_point()
{
    int exit_point_id = codegen_label_count();
    codegen_register_exit_point(exit_point_id);
}

void codegen_end_exit_point()
{
    struct code_generator*gen = current_process->generator;
    struct codegen_exit_point* exit_point = codegen_current_exit_point();
    assert(exit_point);
    asm_push(".exit_point_%i:",exit_point->id);
    free(exit_point);
    // Pops off the most recent exit point so the second latest can be worked with
    vector_pop(gen->exit_points);
}


// Goes to the last created exit point
void codegen_goto_exit_point(struct node* node)
{
    struct code_generator* gen = current_process->generator;
    struct codegen_exit_point* exit_point = codegen_current_exit_point();
    asm_push("jmp .exit_point_%i",exit_point->id);
}

void codegen_register_entry_point(int entry_point_id)
{
    struct code_generator* gen = current_process->generator;
    struct codegen_entry_point* entry_point = calloc(1, sizeof(struct codegen_entry_point));
    entry_point->id = entry_point_id;
    vector_push(gen->entry_points,&entry_point);

}

struct codegen_entry_point* codegen_current_entry_point()
{
    struct code_generator* gen = current_process->generator;
    return vector_back_ptr_or_null(gen->entry_points);
}

void codegen_begin_entry_point()
{
    int entry_point_id = codegen_label_count();
    codegen_register_entry_point(entry_point_id);
    asm_push(".entry_point_%i:",entry_point_id);
}

void codegen_end_entry_point()
{
    struct code_generator*gen = current_process->generator;
    struct codegen_entry_point* entry_point = codegen_current_entry_point();
    assert(entry_point);
    free(entry_point);
    vector_pop(gen->entry_points);
}


void codegen_goto_entry_point(struct node* current_node)
{
    struct code_generator* gen = current_process->generator;
    struct codegen_entry_point* entry_point = codegen_current_entry_point();
    asm_push("jmp .entry_point_%i",entry_point->id);
}

// Usually it's needed to create both entry and exit points so these 2 functions do both for convenience
void codegen_begin_entry_exit_point()
{
    codegen_begin_entry_point();
    codegen_begin_exit_point();
}

void codegen_end_entry_exit_point()
{
    codegen_end_entry_point();
    codegen_end_exit_point();
}



// Generate DD DW etc. depending on the size of the variable
static const char* asm_keyword_for_size(size_t size, char*tmp_buff)
{
    const char* keyword = NULL;
    switch (size) {
        case DATA_SIZE_BYTE:
            keyword = "db";
            break;
        case DATA_SIZE_WORD:
            keyword = "dw";
            break;
        case DATA_SIZE_DWORD:
            keyword = "dd";
            break;
        case DATA_SIZE_DDWORD:
            keyword = "dq";
            break;
        default:
            // means size * db
            sprintf(tmp_buff, "times %lld db",(unsigned long)size);
            return tmp_buff;
    }

    strcpy(tmp_buff,keyword);
    return tmp_buff;
}

void codegen_generate_global_Variable_for_primitive(struct node* node)
{
    char tmp_buff[256];
    if (node->var.val != NULL)
    {
        // Handle the value
        if (node->var.val->type == NODE_TYPE_STRING)
        {
            const char* label = codegen_register_string(node->var.val->sval);
            // IDENTIFIER: SIZE VALUE
            asm_push("%s: %s %s",node->var.name, asm_keyword_for_size(variable_size(node),tmp_buff),label);
        } else
        {
            // IDENTIFIER: SIZE VALUE
            asm_push("%s: %s %lld",node->var.name, asm_keyword_for_size(variable_size(node),tmp_buff),node->var.val->llnum);
        }
        return;
    }
    asm_push("%s: %s 0", node->var.name, asm_keyword_for_size(variable_size((node)),tmp_buff));
}

void codegen_generate_global_variable(struct node* node )
{
    // ; TYPE_NAME VARIABLE_NAME
    asm_push("; %s %s",node->var.type.type_str, node->var.name);
    switch (node->var.type.type) {
        case DATA_TYPE_VOID:
        case DATA_TYPE_CHAR:
        case DATA_TYPE_SHORT:
        case DATA_TYPE_INTEGER:
        case DATA_TYPE_LONG:
            codegen_generate_global_Variable_for_primitive(node);
            break;
        case DATA_TYPE_DOUBLE:
        case DATA_TYPE_FLOAT:
            compiler_error(current_process,"Doubles and floats are not supported in this compiler!");
            break;
    }
}

void codegen_generate_data_section_part(struct node* node)
{
    // CREATE A SWITCH FOR PROCESSING THE GLOBAL DATA (anything that will be in the global scope)

    switch (node->type) {
        case NODE_TYPE_VARIABLE:
            codegen_generate_global_variable(node);
            break;
    }
}

void codegen_generate_data_section()
{
    asm_push("section .data");
    // This loop only processes the root nodes, but leaves the children alone
    struct node* node = codegen_node_next();
    while (node)
    {
        codegen_generate_data_section_part(node);
        node = codegen_node_next();
    }
}

struct resolver_entity* codegen_register_function(struct node* func_node,int flags)
{
    return resolver_default_register_function(current_process->resolver,func_node,flags);
}

void codegen_generate_function_prototype(struct node* node)
{
    codegen_register_function(node,0);
    // In assembly extern means it is located somewhere else and the linker will link it later
    asm_push("extern %s", node->func.name);
}

void codegen_generate_function_arguments(struct vector* argument_vector)
{
    // Loop through the arguments vector and create a new stack entity for all of them (specifying that they are in a local stack, because we are in a function
    vector_set_peek_pointer(argument_vector,0);
    struct node* current = vector_peek_ptr(argument_vector);
    while (current)
    {
        codegen_new_scope_entity(current,current->var.aoffset,RESOLVER_DEFAULT_ENTITY_FLAG_IS_LOCAL_STACK);
        current = vector_peek_ptr(argument_vector);
    }
}

bool codegen_is_exp_root_for_flags(int flags)
{
    return !(flags & EXPRESSION_IS_NOT_ROOT_NODE);
}

bool codegen_is_exp_root(struct history* history)
{
    return codegen_is_exp_root_for_flags(history->flags);
}

void codegen_generate_number_node(struct node* node, struct history* history)
{
    // We are pushing a dword integer
    // If node's number value was 50 you would see push dword 50
    asm_push_ins_push_with_data("dword %i",STACK_FRAME_ELEMENT_TYPE_PUSHED_VALUE,"result_value",STACK_FRAME_ELEMENT_FLAG_IS_NUMERICAL,&(struct stack_frame_data){.dtype=datatype_for_numeric()},node->llnum);

}

void codegen_generate_expressionable(struct node* node, struct history* history)
{
    // To later on know if we are currently processing the root node
    bool is_root = codegen_is_exp_root(history);
    if (is_root)
    {
        history->flags |= EXPRESSION_IS_NOT_ROOT_NODE;
    }
    switch (node->type) {
        case NODE_TYPE_NUMBER:
            codegen_generate_number_node(node,history);
            break;
        case NODE_TYPE_EXPRESSION:
            codegen_generate_exp_node();
            break;
    }
}

const char*codegen_sub_register(const char* original_register, size_t size)
{
    const char* reg = NULL;
    if (S_EQ(original_register,"eax"))
    {
        if (size == DATA_SIZE_BYTE)
        {
            reg = "al";
        }
        else if (size == DATA_SIZE_WORD)
        {
            reg = "ax";
        }
        else if (size == DATA_SIZE_DWORD)
        {
            reg = "eax";
        }
    }
    else if (S_EQ(original_register,"ebx"))
    {
        if (size == DATA_SIZE_BYTE)
        {
            reg = "bl";
        }
        else if (size == DATA_SIZE_WORD)
        {
            reg = "bx";
        }
        else if (size == DATA_SIZE_DWORD)
        {
            reg = "ebx";
        }
    }
    else if (S_EQ(original_register,"ecx"))
    {
        if (size == DATA_SIZE_BYTE)
        {
            reg = "cl";
        }
        else if (size == DATA_SIZE_WORD)
        {
            reg = "cx";
        }
        else if (size == DATA_SIZE_DWORD)
        {
            reg = "ecx";
        }
    }
    else if (S_EQ(original_register,"edx"))
    {
        if (size == DATA_SIZE_BYTE)
        {
            reg = "dl";
        }
        else if (size == DATA_SIZE_WORD)
        {
            reg = "dx";
        }
        else if (size == DATA_SIZE_DWORD)
        {
            reg = "edx";
        }
    }

    return reg;
}

const char*codegen_byte_word_or_dword_or_ddword(size_t size, const char** reg_to_use)
{
    const char* type = NULL;
    const char* new_register = *reg_to_use;
    if (size == DATA_SIZE_BYTE)
    {
        type = "byte";
        // If we don't need the whole EAX then just use AL for example
        new_register = codegen_sub_register(*reg_to_use,DATA_SIZE_BYTE);
    }
    else if (size == DATA_SIZE_WORD)
    {
        type = "word";
        // If we don't need the whole EAX then just use AL for example
        new_register = codegen_sub_register(*reg_to_use,DATA_SIZE_WORD);
    }
    else if (size == DATA_SIZE_DWORD)
    {
        type = "dword";
        // If we don't need the whole EAX then just use AL for example
        new_register = codegen_sub_register(*reg_to_use,DATA_SIZE_DWORD);
    }
    // Only in 64 bits
    else if (size == DATA_SIZE_DDWORD)
    {
        type = "ddword";
        // If we don't need the whole EAX then just use AL for example
        new_register = codegen_sub_register(*reg_to_use,DATA_SIZE_DDWORD);
    }
    *reg_to_use = new_register;
    return type;
}

void codegen_generate_assignment_instruction_for_operator(const char* mov_type_keyword,const char* address,const char* reg_to_use,const char* op,bool is_signed)
{
    if (S_EQ(op,"="))
    {
        asm_push("mov %s [%s], %s",mov_type_keyword,address,reg_to_use);
    }
    else if (S_EQ(op,"+="))
    {
        asm_push("add %s [%s], %s",mov_type_keyword,address,reg_to_use);
    }
}

void codegen_generate_scope_variable(struct node* node)
{
    struct resolver_entity*entity = codegen_new_scope_entity(node,node->var.aoffset,RESOLVER_DEFAULT_ENTITY_FLAG_IS_LOCAL_STACK);
    // a = 50 -> We need to generate the 50
    if (node->var.val)
    {
        // Pop off the value that was pushed on by the generate_expressionable
        codegen_generate_expressionable(node->var.val, history_begin(EXPRESSION_IS_ASSIGNMENT | IS_RIGHT_OPERAND_OF_ASSIGNMENT));
        // pop eax
        asm_push_ins_pop("eax",STACK_FRAME_ELEMENT_TYPE_PUSHED_VALUE,"result_value");

        const char*reg_to_use = "eax";
        // In asm we use byte, dword, ddword so we need to decide which one to use
        const char* move_type = codegen_byte_word_or_dword_or_ddword(datatype_element_size(&entity->dtype),&reg_to_use);
        // It must be assignment so the operator is "="
        codegen_generate_assignment_instruction_for_operator(move_type,codegen_entity_private(entity)->address,reg_to_use,"=",entity->dtype.flags & DATATYPE_FLAG_IS_SIGNED);
    }


}

void codegen_generate_entity_access_start(struct resolver_result* result, struct resolver_entity* root_assignment_entity, struct history*history)
{
    if (root_assignment_entity->type == RESOLVER_ENTITY_TYPE_UNSUPPORTED)
    {
        // Process unsupported entity
        codegen_generate_expressionable(root_assignment_entity->node,history);
    }
    // If it's a simple push then execute it
    else if(result->flags & RESOLVER_RESULT_FLAG_FIRST_ENTITY_PUSH_VALUE)
    {
        asm_push_ins_push_with_data("dword [%s]",STACK_FRAME_ELEMENT_TYPE_PUSHED_VALUE,"result_value",0,&(struct stack_frame_data){.dtype = root_assignment_entity->dtype},result->base.address);
    }
    // If we need to load the value to ebx
    else if (result->flags &RESOLVER_RESULT_FLAG_FIRST_ENTITY_LOAD_TO_EBX)
    {
        // If it's a pointer than we need to load the value at the address ([] -> indirection, pointer dereference)
        if (root_assignment_entity->next && root_assignment_entity->flags & RESOLVER_ENTITY_FLAG_DO_IS_POINTER_ARRAY_ENTITY)
        {
            asm_push("mov ebx, [%s]",result->base.address);
        }
        else
        {
            // If it's not a pointer, then load the address of the variable to ebx
            asm_push("lea ebx, [%s]",result->base.address);
        }
        asm_push_ins_push_with_data("ebx",STACK_FRAME_ELEMENT_TYPE_PUSHED_VALUE,"result_value",0,&(struct stack_frame_data){.dtype = root_assignment_entity->dtype});
    }

}

void codegen_generate_entity_access_for_variable_or_general(struct resolver_result*result, struct resolver_entity* entity)
{
    /*
     * struct abc
     * {
     *      int a;
     *      int e;
     * }
     * struct dog
     * {
     *      struct abc* b;
     * }
     * struct dog a;
     */

    /*
     * a.b->e -> the resolver would give us 2 entities, first would be a.b combined (for example offset of 8) that would be lea ebx, [a+8] and then we need to indirect it (because of ->) that would generate move ebx, [ebx] and then we need to add the offset of e in the struct (for example 4) -> add ebx,4 and runtime now in ebx we would be pointing to e (entity_access_start would process the first entity then this function processes the rest)
     *
     * lea ebx, [a+8]
     * move ebx, [ebx] Before this instruction ebx points to int a in the abc struct because the struct points to the first variable
     * add ebx,4
     */


    // Restore EBX (that's where the start address is stored, because in the entity_access_start function we pushed it regardless (if runtime computation is needed to calculate it i.e a.b->c then it will contain it to the closest that we could resolve (in this case address of b and we are responsible for finding e)))

    asm_push_ins_pop("ebx",STACK_FRAME_ELEMENT_TYPE_PUSHED_VALUE,"result_value");
    if(entity->flags & RESOLVER_ENTITY_FLAG_DO_INDIRECTION)
    {
        // *a -> pointer access
        asm_push("move ebx, [ebx]"); // Move to ebx whatever is stored at the address that is currently in ebx
    }
    asm_push("add ebx, %i",entity->offset);
    asm_push_ins_push_with_data("ebx",STACK_FRAME_ELEMENT_TYPE_PUSHED_VALUE,"result_value",0,&(struct stack_frame_data){.dtype = entity->dtype});
}

void codegen_generate_entity_access_for_entity_assignment_left_operand(struct resolver_result*result, struct resolver_entity*entity, struct history* history)
{
    switch (entity->type) {
        case  RESOLVER_ENTITY_TYPE_ARRAY_BRACKET:
#warning "Implement array brackets"
        break;
        case RESOLVER_ENTITY_TYPE_VARIABLE:
            case RESOLVER_ENTITY_TYPE_GENERAL:
                codegen_generate_entity_access_for_variable_or_general(result,entity);
            break;
            case RESOLVER_ENTITY_TYPE_FUNCTION_CALL:
#warning "Implement function call"
                break;
                case RESOLVER_ENTITY_TYPE_UNARY_INDIRECTION:
#warning "Implement unary indirection"
                    break;
                    case RESOLVER_ENTITY_TYPE_UNARY_GET_ADDRESS:
#warning "Implement unary get address"
            break;
        case RESOLVER_ENTITY_TYPE_UNSUPPORTED:
#warning "Implement unsupported"
            break;
        case RESOLVER_ENTITY_TYPE_CAST:
#warning "Implement cast"
            break;
        default:
            compiler_error(current_process,"COMPILER BUG");
    }
}

void codegen_generate_entity_access_for_assignment_left_operand(struct resolver_result* result, struct resolver_entity* root_assignment_entity, struct node *top_most_node, struct history*history)
{
    codegen_generate_entity_access_start(result,root_assignment_entity,history);
    struct resolver_entity* current = resolver_result_entity_next(root_assignment_entity);
    while (current)
    {
        codegen_generate_entity_access_for_entity_assignment_left_operand(result,current,history);
        current = resolver_result_entity_next(current);
    }
}

void codegen_generate_assignment_part(struct node* node, const char* op, struct history* history)
{

    struct datatype right_operand_type;
    // x = 50 -> x will be followed, node will contain the identifier
    struct resolver_result* result = resolver_follow(current_process->resolver,node);
    assert(resolver_result_ok(result));
    struct resolver_entity* root_assignment_entity = resolver_result_entity_root(result);
    const char* reg_to_use = "eax";
    //a.b.c -> we only care about the datatype of c, that's why we get the last_entity's datatype
    const char* mov_type = codegen_byte_word_or_dword_or_ddword(datatype_element_size(&result->last_entity->dtype),&reg_to_use);
    struct resolver_entity* next_entity = resolver_result_entity_next(root_assignment_entity);

    // If there is no next_entity it means that we managed to resolve the offset in one cycle so we already have the needed address
    if (!next_entity)
    {
        if (datatype_is_struct_or_union_non_pointer(&result->last_entity->dtype))
        {
#warning "Generate a move struct"
        }
        else
        {
            // This is a pointer or a primitive variable

            // result_value is the used tag for everything that has a result i.e. functions returning a value, assignment operations, arithmetics etc.
            asm_push_ins_pop("eax",STACK_FRAME_ELEMENT_TYPE_PUSHED_VALUE,"result_value");

            // Generates operators that have assignments i.e. +=,-=,= etc.
            codegen_generate_assignment_instruction_for_operator(mov_type,result->base.address,reg_to_use,op,result->last_entity->dtype.flags & DATATYPE_FLAG_IS_SIGNED);
        }
    }
    else
    {
        codegen_generate_entity_access_for_assignment_left_operand(result,root_assignment_entity,node,history);
        // Some result value must be on the stack at this point

        // a = 50
        // Pops of the value i.e. 50
        asm_push_ins_pop("edx",STACK_FRAME_ELEMENT_TYPE_PUSHED_VALUE,"result_value");
        // Pops of the address i.e offset or address of a
        asm_push_ins_pop("eax",STACK_FRAME_ELEMENT_TYPE_PUSHED_VALUE,"result_value");
        codegen_generate_assignment_instruction_for_operator(mov_type,"edx",reg_to_use,op,result->last_entity->flags & DATATYPE_FLAG_IS_SIGNED);
    }

}

void codegen_generate_assignment_expression(struct node* node, struct history*history)
{
    // This is assignment not decleration -> int x = 50 is not valid here only x = 50
    // THis generates the right operand of the expression for example: x = 50 -> this would generate 50
    codegen_generate_expressionable(node->exp.right, history_down(history,EXPRESSION_IS_ASSIGNMENT | IS_RIGHT_OPERAND_OF_ASSIGNMENT));
    // Generate the x part of x = 50
    codegen_generate_assignment_part(node->exp.left, node->exp.op,history);
}

void codegen_generate_entity_access_for_entity(struct resolver_result* result, struct resolver_entity* entity, struct history* history)
{
    switch (entity->type) {
        case RESOLVER_ENTITY_TYPE_ARRAY_BRACKET:
#warning "Implement array brackets"
            break;
        case RESOLVER_ENTITY_TYPE_VARIABLE:
        case RESOLVER_ENTITY_TYPE_GENERAL:
            codegen_generate_entity_access_for_variable_or_general(result, entity);
            break;
        case RESOLVER_ENTITY_TYPE_FUNCTION_CALL:
#warning "Implement function call"
            break;
        case RESOLVER_ENTITY_TYPE_UNARY_INDIRECTION:
#warning "Implement unary indirection"
            break;
        case RESOLVER_ENTITY_TYPE_UNARY_GET_ADDRESS:
#warning "Implement unary get address"
            break;
        case RESOLVER_ENTITY_TYPE_UNSUPPORTED:
#warning "Implement unsupported"
            break;
        case RESOLVER_ENTITY_TYPE_CAST:
#warning "Implement cast"
            break;
        default:
            compiler_error(current_process, "COMPILER BUG");
    }
}

void codegen_generate_entity_access(struct resolver_result*result, struct resolver_entity* root_assignment_entity, struct  node* top_most_node, struct history*history)
{
    codegen_generate_entity_access_start(result,root_assignment_entity,history);
    struct resolver_entity*current = resolver_result_entity_next(root_assignment_entity);
    while (current)
    {
        codegen_generate_entity_access_for_entity(result,current,history);
        current = resolver_result_entity_next(current);
    }
}

bool codegen_resolve_node_return_result(struct node* node, struct history* history, struct resolver_result** result_out)
{
    struct resolver_result* result = resolver_follow(current_process->resolver,node);
    if (resolver_result_ok(result))
    {
        struct resolver_entity* root_assignment_entity = resolver_result_entity_root(result);
        codegen_generate_entity_access(result,root_assignment_entity,node,history);
        if (result_out)
        {
            *result_out = result;
        }
        codegen_response_acknowledge(&(struct response){.flags = RESPONSE_FLAG_RESOLVED_ENTITY,.data.resolved_entity = result->last_entity});
        return true;
    }
    return false;
}

void codegen_resolve_node_for_value(struct node* node, struct history* history)
{
    struct resolver_result* result = NULL;
    if (!codegen_resolve_node_return_result(node,history,&result))
    {

    }
}

int get_additional_flags(int current_flags, struct node* node)
{
    if (node->type != NODE_TYPE_EXPRESSION)
    {
        return 0;
    }

    int additional_flags = 0;
    bool maintain__function_call_argument_flag = (current_flags & EXPRESSION_IN_FUNCTION_CALL_ARGUMENTS) && S_EQ(node->exp.op,",");
    if (maintain__function_call_argument_flag)
    {
        additional_flags |= EXPRESSION_IN_FUNCTION_CALL_ARGUMENTS;
    }
    return additional_flags;
}

int codegen_set_flag_for_operator(const char* op)
{
    int flag = 0;
    if (S_EQ(op,"+"))
    {
        flag |= EXPRESSION_IS_ADDITION;
    }
    else if (S_EQ(op,"-"))
    {
        flag |= EXPRESSION_IS_SUBTRACTION;
    }
    else if (S_EQ(op,"*"))
    {
        flag |= EXPRESSION_IS_MULTIPLICATION;
    }
    else if (S_EQ(op,"/"))
    {
        flag |= EXPRESSION_IS_DIVISION;
    }
    else if (S_EQ(op,"%"))
    {
        flag |= EXPRESSION_IS_MODULUS;
    }
    else if (S_EQ(op,">"))
    {
        flag |= EXPRESSION_IS_ABOVE;
    }
    else if (S_EQ(op,"<"))
    {
        flag |= EXPRESSION_IS_BELOW;
    }
    else if (S_EQ(op,">="))
    {
        flag |= EXPRESSION_IS_ABOVE_OR_EQUAL;
    }
    else if (S_EQ(op,"<="))
    {
        flag |= EXPRESSION_IS_BELOW_OR_EQUAL;
    }
    else if (S_EQ(op,"!="))
    {
        flag |= EXPRESSION_IS_NOT_EQUAL;
    }
    else if (S_EQ(op,"=="))
    {
        flag |= EXPRESSION_IS_EQUAL;
    }
    else if (S_EQ(op,"&&"))
    {
        flag |= EXPRESSION_IS_LOGICAL_AND;
    }
    else if (S_EQ(op,"<<"))
    {
        flag |= EXPRESSION_IS_BITSHIFT_LEFT;
    }
    else if (S_EQ(op,">>"))
    {
        flag |= EXPRESSION_IS_BITSHIFT_RIGHT;
    }
    else if (S_EQ(op,"&"))
    {
        flag |= EXPRESSION_IS_BITWISE_AND;
    }
    else if (S_EQ(op,"|"))
    {
        flag |= EXPRESSION_IS_BITWISE_OR;
    }
    else if (S_EQ(op,"^"))
    {
        flag |= EXPRESSION_IS_BITWISE_XOR;
    }
    return flag;
}

struct stack_frame_element* asm_stack_back()
{
    return stackframe_back(current_function);
}

struct stack_frame_element* asm_stack_peek()
{
    return stackframe_peek(current_function);
}

void asm_stack_peek_start()
{
    stackframe_peek_start(current_function);
}

bool asm_datatype_back(struct datatype* dtype_out)
{
    // If it can find the passed in datatype it returns true, otherwise false
    struct stack_frame_element* last_stack_frame_element = asm_stack_back();

    if (!last_stack_frame_element)
    {
        return false;
    }

    if (!(last_stack_frame_element->flags & STACK_FRAME_ELEMENT_FLAG_HAS_DATATYPE))
    {
        return false;
    }
    *dtype_out = last_stack_frame_element->data.dtype;
    return true;
}

bool codegen_can_gen_math(int flags)
{
    return flags & EXPRESSION_GEN_MATHABLE;
}

void codegen_gen_cmp(const char* value, const char* set_ins)
{
    // Compare the value to the eax reg and store the result back into eax (0 extended)


    asm_push("cmp eax, %s",value);
    asm_push("% al",set_ins);
    asm_push("movzx eax,al");
}

void codegen_gen_math_for_value(const char* reg, const char*value, int flags, bool is_signed)
{
    if (flags & EXPRESSION_IS_ADDITION)
    {
        asm_push("add %s, %s",reg,value);
    }
    else if (flags & EXPRESSION_IS_SUBTRACTION)
    {
        asm_push("sub %s, %s",reg,value);
    }
    else if (flags & EXPRESSION_IS_MULTIPLICATION)
    {
        asm_push("mov ecx, %s",value);
        if (is_signed)
        {
            asm_push("imul ecx");
        }
        else
        {
            asm_push("mul ecx");
        }
    }
    // In division the value will be stored in eax and the remainder will be stored in edx
    else if (flags & EXPRESSION_IS_DIVISION)
    {
        asm_push("mov ecx, %s",value);
        asm_push("cdq");
        if (is_signed)
        {
            asm_push("idiv ecx");
        }
        else
        {
            asm_push("div ecx");
        }
    }
    else if (flags & EXPRESSION_IS_MODULUS)
    {
        asm_push("mov ecx, %s",value);
        asm_push("cdq");
        if (is_signed)
        {
            asm_push("idiv ecx");
        }
        else
        {
            asm_push("div ecx");
        }
        // Move the remainder to eax
        asm_push("mov eax,edx");
    }
    else if (flags & EXPRESSION_IS_ABOVE)
    {
        // setg -> set if greater, so the result will only be moved to the al (then to eax) if the value is greater, setl -> set if lower etc.
        codegen_gen_cmp(value,"setg");
    }
    else if (flags & EXPRESSION_IS_BELOW)
    {
        codegen_gen_cmp(value,"setl");
    }
    else if (flags & EXPRESSION_IS_EQUAL)
    {
        codegen_gen_cmp(value,"sete");
    }
    else if (flags & EXPRESSION_IS_ABOVE_OR_EQUAL)
    {
        codegen_gen_cmp(value,"setge");
    }
    else if (flags & EXPRESSION_IS_BELOW_OR_EQUAL)
    {
        codegen_gen_cmp(value,"setle");
    }
    else if (flags & EXPRESSION_IS_NOT_EQUAL)
    {
        codegen_gen_cmp(value,"setne");
    }
    else if(flags & EXPRESSION_IS_BITSHIFT_LEFT)
    {
        value = codegen_sub_register(value, DATA_SIZE_BYTE);
        // sal eax, 5 -> shift left eax reg by 5 and store the result back in eax
        asm_push("sal %s, %s",reg,value);
    }
    else if(flags & EXPRESSION_IS_BITSHIFT_RIGHT)
    {
        value = codegen_sub_register(value, DATA_SIZE_BYTE);
        // sar eax, 5 -> shift right eax reg by 5 and store the result back in eax
        asm_push("sar %s, %s",reg,value);
    }
    else if (flags & EXPRESSION_IS_BITWISE_AND)
    {
        asm_push("and %s,%s", reg,value);
    }
    else if (flags & EXPRESSION_IS_BITWISE_OR)
    {
        asm_push("or %s,%s", reg,value);
    }
    else if (flags & EXPRESSION_IS_BITWISE_XOR)
    {
        asm_push("xor %s,%s", reg,value);
    }

}

void codegen_generate_exp_node_for_arithmetic(struct node* node, struct history* history)
{
    assert(node->type == NODE_TYPE_EXPRESSION);
    int flags = history->flags;

    //if (is_logical_operator(node->exp.op))
    //{
    //  codegen_generate_exp_node_for_logical_arithmetic();
    //}

    struct node*left_node = node->exp.left;
    struct node* right_node = node->exp.right;
    int op_flags = codegen_set_flag_for_operator(node->exp.op);

    codegen_generate_expressionable(left_node, history_down(flags));
    codegen_generate_expressionable(right_node, history_down(flags));

    struct datatype last_dtype = datatype_for_numeric();

    asm_datatype_back(&last_dtype);

    if (codegen_can_gen_math(op_flags))
    {
        struct datatype right_dtype = datatype_for_numeric();
        asm_datatype_back(&right_dtype);
        asm_push_ins_pop("ecx",STACK_FRAME_ELEMENT_TYPE_PUSHED_VALUE,"result_value");
        if (last_dtype.flags & DATATYPE_FLAG_IS_LITERAL)
        {
            asm_datatype_back(&last_dtype);
        }

        struct datatype left_dtype = datatype_for_numeric();
        asm_datatype_back(&left_dtype);
        asm_push_ins_pop("eax",STACK_FRAME_ELEMENT_TYPE_PUSHED_VALUE,"result_value");
#warning "Generate pointer stuff"
        codegen_gen_math_for_value("eax","ecx",op_flags,last_dtype.flags & DATATYPE_FLAG_IS_SIGNED);
    }
    asm_push_ins_push_with_data("eax",STACK_FRAME_ELEMENT_TYPE_PUSHED_VALUE,"result_value",0,&(struct stack_frame_data){.dtype = last_dtype});
}

void codegen_generate_exp_node(struct node* node, struct history*history)
{
    if (is_node_assignment(node))
    {
        codegen_generate_assignment_expression(node,history);
        return;
    }
    // Can we locate a variable for the given expression
    if (codegen_resolve_node_for_value(node,history))
    {
        return;
    }

    // If it's not assignment and we cannot resolve the node through the resolver then it must be arithmetic i.e. 5 + 10, a + b

    int additional_flags = get_additional_flags(history->flags,node);
    codegen_generate_exp_node_for_arithmetic();
}

void codegen_generate_statement(struct node* node, struct history* history)
{
    switch (node->type) {
        case NODE_TYPE_EXPRESSION:
        codegen_generate_exp_node(node, history_begin(history->flags));
            break;
        case NODE_TYPE_VARIABLE:
            codegen_generate_scope_variable(node);
            break;
    }
}

void codegen_generate_scope_no_new_scope(struct vector* statements, struct history*history)
{
    vector_set_peek_pointer(statements,0);
    struct node* statement_node = vector_peek_ptr(statements);
    while (statement_node)
    {
        codegen_generate_statement(statement_node,history);
        statement_node = vector_peek_ptr(statements);
    }
}

void codegen_generate_stack_scope(struct vector* statements, size_t scope_size, struct history* history)
{
    codegen_new_scope(RESOLVER_SCOPE_FLAG_IS_STACK);
    codegen_generate_scope_no_new_scope(statements,history);
    codegen_finish_scope();
}

void codegen_generate_body(struct node* node, struct history* history)
{
    codegen_generate_stack_scope(node->body.statements,node->body.size,history);


}

void codegen_generate_function_with_body(struct node* node)
{
    // Generate assembly for function
    /*
     * global test
     * ; test function
     * test:
     */
    // Global means it can be accessed outside this file
    codegen_register_function(node,0);
    asm_push("global %s",node->func.name);
    asm_push("; %s function", node->func.name);
    asm_push("%s:",node->func.name);

    // Push the ebp to the stack
    asm_push_ebp();

    // Move the stack pointer to the base pointer, so we have the stackframe in the actual runtime
    asm_push("mov ebp, esp");

    // Subtract from the stack the total size of the function's body size
    codegen_stack_sub(C_ALIGN(function_node_stack_size(node)));

    // Create new scope
    codegen_new_scope(RESOLVER_DEFAULT_ENTITY_FLAG_IS_LOCAL_STACK);

    // Generate the arguments of the function
    codegen_generate_function_arguments(function_node_argument_vec(node));

    codegen_generate_body(node->func.body_n, history_begin(IS_ALONE_STATEMENT));

    codegen_finish_scope();

    // Add back to the stack
    codegen_stack_add(C_ALIGN(function_node_stack_size(node)));

    // Pop back the base pointer
    asm_pop_ebp();

    //Make sure the stackframe of the function is empty, if it isn't then we forgot to pop something at the end
    stackframe_assert_empty(current_function);

    // Generate return instruction
    asm_push("ret");
}

void codegen_generate_function(struct node* node)
{
    current_function = node;
    if (function_node_is_prototype(node))
    {
        codegen_generate_function_prototype(node);
        return;
    }
    codegen_generate_function_with_body(node);
}

void codegen_generate_root_node(struct node* node)
{
    // For global scope
    switch (node->type)
    {
        case NODE_TYPE_VARIABLE:
            // We processed it earlier in .data section
            break;
        case NODE_TYPE_FUNCTION:
            codegen_generate_function(node);
            break;
    }
}

void codegen_generate_root()
{
    asm_push("section .text");
    struct node* node = NULL;
    while ((node = codegen_node_next()) != NULL)
    {
        codegen_generate_root_node(node);
    }
}


// in assembly str_1: db 'hello world\n',0 -> \n will be interpreted as a string literal and will not create a new line
// It will be replaced to str_1: db 'hello world', 10, 0 (10 = '\n', 0 = '\0')
bool codegen_write_string_char_escaped(char c)
{
    const char* c_out = NULL;
    switch (c) {
        case '\n':
            c_out = "10";
            break;
        case '\t':
            c_out = "9";
            break;
    }
    if (c_out)
    {
        asm_push_no_nl("%s, ",c_out);
    }
    return c_out != NULL;
}

void codegen_write_string(struct string_table_element* element)
{
    // str_1: db
    asm_push_no_nl("%s: db ", element->label);
    size_t len = strlen(element->str);
    for (int i = 0; i < len; ++i) {
        char c = element->str[i];
        // String can contain special chars (\n,\t etc.) so we need to handle them
        bool handled = codegen_write_string_char_escaped(c);
        // If the char was handled by the function then will go to the next line otherwise write that char into the variable
        if (handled)
        {
            continue;
        }
        asm_push_no_nl("'%c',",c);
    }
    // Write the ending NULL ('\0') to the string
    asm_push_no_nl("0");
    // Start a new line
    asm_push("");
}

void codegen_write_strings()
{
#warning "Loop through the string table and write all the strings"
    struct code_generator* generator = current_process->generator;
    vector_set_peek_pointer(generator->string_table,0);
    struct string_table_element* element = vector_peek_ptr(generator->string_table);
    while (element)
    {
        codegen_write_string(element);
        element = vector_peek_ptr(generator->string_table);
    }
}

void codegen_generate_rod()
{
    asm_push("section .rodata");
    // Read only data are mostly strings
    codegen_write_strings();
}

int codegen(struct compiler_process* process)
{
    current_process = process;
    scope_create_root(process);
    vector_set_peek_pointer(process->node_tree_vec,0);
    codegen_new_scope(0);

    // Generate the data section of the assembly
    codegen_generate_data_section();

    vector_set_peek_pointer(process->node_tree_vec,0);
    // Generate the code section of the assembly
    codegen_generate_root();
    codegen_finish_scope(0);

    // Generate read only data (strings etc.)

    codegen_generate_rod();

    return 0;
}

