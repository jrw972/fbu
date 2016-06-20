#ifndef RC_SRC_STACK_HPP
#define RC_SRC_STACK_HPP

#include <cstring>

#include "types.hpp"
#include "arch.hpp"
#include "util.hpp"

namespace runtime
{
// An aligned stack of bytes.
// The alignment is given by arch::stack_alignment ().
// The stack grows up (instead of down like a hardware stack).
// The stack contains a base pointer to set up function call frames.
struct Stack
{
  Stack (size_t capacity);
  ~Stack ();

  void push_pointer (void* pointer);
  void* pop_pointer ();
  void* peek_pointer () const;

  template <typename T>
  void
  push (T b)
  {
    size_t s = util::align_up (sizeof (T), arch::stack_alignment ());
    assert (top_ + s <= limit_);
    std::memcpy (top_, &b, sizeof (T));
    top_ += s;
  }

  template <typename T>
  void
  pop (T& retval)
  {
    size_t s = util::align_up (sizeof (T), arch::stack_alignment ());
    assert (top_ - s >= data_);
    top_ -= s;
    std::memcpy (&retval, top_, sizeof (T));
  }

  // Pop size bytes from the top of the stack.
  void popn (size_t size);

  // Push base_pointer + offset.
  // Used to get the address of an argument or local variable.
  void push_address (ptrdiff_t offset);
  // Return base_pointer + offset.
  void* get_address (ptrdiff_t offset);

  // Reserve size bytes on the top of the stack.
  void reserve (size_t size);

  // Copy size bytes from ptr to the top of the stack.
  void load (const void* ptr,
             size_t size);

  // Copy size bytes from the top of the stack to ptr
  // and remove that many bytes from the stack.
  void store (void* ptr,
              size_t size);

  // Copy size bytes from ptr to base_pointer + offset.
  void write (ptrdiff_t offset,
              const void* ptr,
              size_t size);

  void read (ptrdiff_t offset,
             void* ptr,
             size_t size) const;

  // Read a pointer at base_pointer + offset.
  void* read_pointer (ptrdiff_t offset);

  // Copy size bytes from the top of the stack to base_pointer + offset
  // and remove that many bytes from the stack.
  void move (ptrdiff_t offset,
             size_t size);

  // Clear size bytes at base_pointer + offset.
  void clear (ptrdiff_t offset,
              size_t size);

  // Setup a new frame by
  // - pushing the old base pointer
  // - setting a new base pointer
  // - reserving and clearing size bytes and set them to zero.
  void setup (size_t size);

  // Tear down a frame by resetting the base pointer.
  void teardown ();

  // Get/set the base pointer.
  char* base_pointer () const;
  void base_pointer (char* base_pointer);

  // Get/set the top of the stack.
  char* top () const;

  // True if the stack is empty.
  bool empty () const;

  // Size of the stack in bytes.
  size_t size () const;

  // Return a pointer to the return instruction pointer (which is below the base pointer).
  void* pointer_to_instruction_pointer () const;

  // Debugging dump.
  void print (std::ostream& out = std::cout) const;

private:
  char* data_;
  char* base_pointer_;
  char* top_;
  char* limit_;
};

}

#endif // RC_SRC_STACK_HPP
