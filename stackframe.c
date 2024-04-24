#include "compiler.h"
#include <assert.h>
#include "helpers/vector.h"

// because it's a 32 bit compiler it's 4, in 64 bit it's 8


void stackframe_pop(struct node* func_node)
{
    struct stack_frame* frame = &func_node->func.frame;
    vector_pop(frame->elements);
}

struct stack_frame_element* stackframe_back(struct node* func_node)
{
    return vector_back_or_null(func_node->func.frame.elements);
}

// Allows user to check if the value on stack is the one they are expecting (that's why it doesn't throw error)
struct stack_frame_element* stackframe_back_expect(struct node*func_node, int expecting_type, const char*expected_name)
{
    struct stack_frame_element* element = stackframe_back(func_node);
    if (element &&element->type != expecting_type || !S_EQ(element->name,expected_name))
    {
        return NULL;
    }
    return element;
}

// Returns the last element in the stack frame but throws an error if it doesn't have a specific type and name
void stackframe_pop_expecting(struct node*func_node, int expecting_type, const char* expecting_name)
{
    struct stack_frame*frame = &func_node->func.frame;
    struct stack_frame_element* last_element = stackframe_back(func_node);
    assert(last_element);
    assert(last_element->type == expecting_type && S_EQ(last_element->name,expecting_name));
    stackframe_pop(func_node);
}

// Makes peeking available on the stackframe
void stackframe_peek_start(struct node* func_node)
{
    struct stack_frame* frame = &func_node->func.frame;
    vector_set_peek_pointer_end(frame->elements);
    vector_set_flag(frame->elements,VECTOR_FLAG_PEEK_DECREMENT);
}
// Peeks the stack frame and returns the element
struct stack_frame_element* stackframe_peek(struct node* func_node)
{
    struct stack_frame* frame = &func_node->func.frame;
    return vector_peek(frame->elements);
}
// Pushes and element to the stackframe
void stackframe_push(struct node* func_node, struct stack_frame_element* element)
{
    struct stack_frame* frame = &func_node->func.frame;
    // The stack grows downwards, so we need to calculate it in a specific way
    // The offset is -> the number of elements in the frame (i.e. variables etc.) multiplied by the stack size (we need to make "one room" for all variables) and negative because it grows downwards
    element->offset_from_bp = -(vector_count(frame->elements) * STACK_PUSH_SIZE);
    vector_push(frame->elements,element);
}

// Pushes {amount} of bytes to the stack (can be used to create room for variables)
void stackframe_sub(struct node* func_node,int type, const char* name, size_t amount)
{
    // Make sure that the push amount is aligned to the stack push size to avoid pushing wrong values (like 3 or 15 etc.)
     assert((amount % STACK_PUSH_SIZE) == 0);
     // How many pushes we need to achieve {amount} subtraction from stack
     size_t total_pushes = amount / STACK_PUSH_SIZE;
    for (int i = 0; i < total_pushes; ++i) {
        stackframe_push(func_node,&(struct stack_frame_element){.type=type,.name = name});
    }

}
// Pops {amount} number of bytes from the stack (it can be used to reset, add to the stack etc.)
void stackframe_add(struct node* func_node,int type, const char* name, size_t amount)
{
    // Make sure that the push amount is aligned to the stack push size to avoid pushing wrong values (like 3 or 15 etc.)
    assert((amount % STACK_PUSH_SIZE) == 0);
    // How many pops we need to achieve {amount} addition from stack
    size_t total_pushes = amount / STACK_PUSH_SIZE;
    for (int i = 0; i < total_pushes; ++i) {
        stackframe_pop(func_node);
    }
}

// If its empty nothing will happen, but if empty it will abort
void stackframe_assert_empty(struct node*func_node)
{
    struct stack_frame* frame = &func_node->func.frame;
    assert(vector_count(frame->elements) == 0);
}