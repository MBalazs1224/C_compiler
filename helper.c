#include "compiler.h"
#include "helpers/vector.h"
#include <assert.h>
size_t variable_size(struct node* var_node)
{
    assert(var_node->type == NODE_TYPE_VARIABLE);
    return datatype_size(&var_node->var.type);
}

size_t variable_size_for_list(struct node*var_list_node)
{
    assert(var_list_node->type == NODE_TYPE_VARIABLE_LIST);
    size_t size = 0;
    vector_set_peek_pointer(var_list_node->var_list.list,0);
    struct node* var_node = vector_peek_ptr(var_list_node->var_list.list);
    while(var_node)
    {
        size += variable_size(var_node);
        var_node = vector_peek_ptr(var_list_node->var_list.list);
    }
    return size;
}

/**
 *
 * @param val Base number of bytes
 * @param to  Needed amount of bytes
 * @return The amount of bytes needed to pad the size to the correct size.
 */
int padding(int val, int to)
{
    if (to <= 0)
    {
        return 0;
    }

    if ((val % 2) == 0)
    {
        return 0;
    }
    return to - (val % to) % to;
}

int align_value(int val, int to)
{
    // If not aligned (5 % 4 = 1 -> will enter the if statement)
    if (val % to)
    {
        val += padding(val,to);
    }
    return val;
}
// For negative needed bytes (used mainly at stack)
int align_value_treat_positive(int val, int to)
{
    assert(to >= 0);
    if (val < 0)
    {
        to = -to;
    }
    return align_value(val, to);
}

int compute_sum_padding(struct vector*vec)
{
    int padding = 0;
    int last_type = 1;
    bool mixed_types = false;
    vector_set_peek_pointer(vec,0);
    struct node* cur_node = vector_peek_ptr(vec);
    struct node* last_node = NULL;
    while (cur_node)
    {
        if (cur_node->type != NODE_TYPE_VARIABLE)
        {
            cur_node = vector_peek_ptr(vec);
            continue;
        }
        padding += cur_node->var.padding;
        last_type = cur_node->var.type.type;
        cur_node = vector_peek_ptr(vec);
    }
    return padding;
}


struct node*variable_struct_or_union_body_node(struct node* node)
{
    if (!node_is_struct_or_union_variable(node))
    {
        return NULL;
    }
    if (node->var.type.type == DATA_TYPE_STRUCT)
    {
        return node->var.type.struct_node->_struct.body_n;
    }

    //return union body
    if (node->var.type.type == DATA_TYPE_UNION)
    {
        return node->var.type.union_node->_union.body_n;
    }
    return NULL;
}
// char abc[50][20]
int array_multiplier(struct datatype*dtype, int index, int index_value)
{
    if (!(dtype->flags & DATATYPE_FLAG_IS_ARRAY))
    {
        return index_value;
    }

    vector_set_peek_pointer(dtype->array.brackets->n_brackets, index + 1);
    int size_sum = index_value;
    struct node* bracket_node = vector_peek_ptr(dtype->array.brackets->n_brackets);
    while (bracket_node)
    {
        assert(bracket_node->bracket.inner->type == NODE_TYPE_NUMBER);
        int declared_index = bracket_node->bracket.inner->llnum;
        int size_value = declared_index;
        size_sum *= size_value;
        bracket_node = vector_peek_ptr(dtype->array.brackets->n_brackets);
    }

    return size_sum;
}

int array_offset(struct datatype*dtype, int index, int index_value)
{
    if (!(dtype->flags & DATATYPE_FLAG_IS_ARRAY) || (index == vector_count(dtype->array.brackets->n_brackets) - 1))
    {
        return index_value * datatype_element_size(dtype);
    }

    return array_multiplier(dtype,index,index_value) * datatype_element_size(dtype);
}


// --------------------------------------------------------------------------------
// The variables have to be aligned to the  CPU's word size (on 32 bit -> 4 bytes) because on every CPU cycle it can only load the word size (4 bytes for us) -> if there is an int after a char then there has to be a 3 byte gap between them so the int won't be cut into 2 parts. For example -> char c (offset 0-1), int i (offset 4-8), if it was char c (offset 0-1) and int (offset 1-5) then the CPU could only load the bytes from offset 0-4 and the integer would be cut into 2 parts (from byte 1-4 and 4-5). This is why we need to align the memory. Union offsets are always 0 until a sub struct is discovered.
// --------------------------------------------------------------------------------


struct node* body_largest_variable_node(struct node* body_node)
{
    if (!body_node)
    {
        return NULL;
    }

    if (body_node->type != NODE_TYPE_BODY)
    {
        return NULL;
    }

    return body_node->body.largest_var_node;
}

struct node* variable_struct_or_union_largest_variable_node(struct node* var_node)
{
    return body_largest_variable_node(variable_struct_or_union_body_node(var_node));
}

// Find an offset for the variable with the given var_name name
int struct_offset(struct compiler_process*compile_proc, const char*struct_name,  const char*var_name ,struct node**var_node_out, int last_pos, int flags)
{
    struct symbol* struct_sym = symresolver_get_symbol(compile_proc,struct_name);
    assert(struct_sym && struct_sym->type == SYMBOL_TYPE_NODE);
    struct node* node = struct_sym->data;
    assert(node_is_struct_or_union(node));

    // We are getting the variables inside the struct body struct abc {VARIABLES}.
    struct vector* struct_vars_vec = node->_struct.body_n->body.statements;
    vector_set_peek_pointer(struct_vars_vec,0);

    // If we are accessing the struct backwards then we need to set the peek pointer to the end and iterate through it backward
    if (flags & STRUCT_ACCESS_BACKWARDS)
    {
        vector_set_peek_pointer_end(struct_vars_vec);
        vector_set_flag(struct_vars_vec,VECTOR_FLAG_PEEK_DECREMENT);

    }

    struct node*var_node_current = variable_node(vector_peek_ptr(struct_vars_vec));
    struct node*var_node_last = NULL;
    int position = last_pos;
    *var_node_out = NULL;
    while (var_node_current)
    {
        *var_node_out = var_node_current;
        if (var_node_last)
        {
            position += variable_size(var_node_last);
            if (variable_node_is_primitive(var_node_current))
            {
                position = align_value_treat_positive(position,var_node_current->var.type.size);
            } else{
                position = align_value_treat_positive(position,variable_struct_or_union_largest_variable_node(var_node_current)->var.type.size);
            }
        }

        if (S_EQ(var_node_current->var.name, var_name))
        {
            // We need to stop because we have found the variable because we have computed it's offset
            break;
        }
        var_node_last = var_node_current;
        var_node_current = variable_node(vector_peek_ptr(struct_vars_vec));
    }
    vector_unset_flag(struct_vars_vec,VECTOR_FLAG_PEEK_DECREMENT);
    return position;

}
// Variable access operators
bool is_access_operator(const char* op)
{
    return S_EQ(op,"->") || S_EQ(op,".");
}
bool is_array_operator(const char* op)
{
    return S_EQ(op,"[]");
}

bool is_parentheses_operator(const char* op)
{
    return S_EQ(op,"()");
}

bool is_argument_operator(const char* op)
{
    return S_EQ(op,",");
}

bool is_access_node(struct node* node)
{
    return node->type == NODE_TYPE_EXPRESSION && is_access_operator(node->exp.op);
}
bool is_access_node_with_op(struct node* node, const char* op)
{
    return is_access_node(node) && S_EQ(node->exp.op,op);
}

bool is_array_node(struct node* node)
{
    return node->type == NODE_TYPE_EXPRESSION && is_array_operator(node->exp.op);
}
bool is_parentheses_node(struct node* node)
{
    return node->type == NODE_TYPE_EXPRESSION && is_parentheses_operator(node->exp.op);
}

bool is_argument_node(struct node* node)
{
    return node->type == NODE_TYPE_EXPRESSION && is_argument_operator(node->exp.op);
}

void datatype_decrement_pointer(struct datatype* dtype)
{
    dtype->pointer_depth--;
    if (dtype->pointer_depth <= 0)
    {
        // Reset the flag that it's a pointer
        dtype->flags &= ~DATATYPE_FLAG_IS_POINTER;
    }
}

bool op_is_indirection(const char* op)
{
    return S_EQ(op,"*");
}

bool is_unary_operator(const char* op)
{
    return S_EQ(op,"-") || S_EQ(op,"!")  || S_EQ(op,"~") || S_EQ(op,"*") || S_EQ(op,"&");
}
