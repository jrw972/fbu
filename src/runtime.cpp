#include "runtime.hpp"

#include <error.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <netdb.h>
#include <unistd.h>

#include "node.hpp"
#include "node_visitor.hpp"
#include "callable.hpp"
#include "symbol_visitor.hpp"
#include "types.hpp"
#include "reaction.hpp"
#include "field.hpp"
#include "composition.hpp"
#include "heap.hpp"
#include "executor_base.hpp"
#include "semantic.hpp"
#include "check_types.hpp"
#include "error_reporter.hpp"
#include "parameter_list.hpp"

namespace runtime
{
using namespace util;
using namespace type;
using namespace ast;
using namespace decl;
using namespace semantic;

struct port_t
{
  component_t* instance;
  const decl::Reaction* reaction;
  type::Int::ValueType parameter;
  port_t* next;

  port_t (component_t* i, const decl::Reaction* r, type::Int::ValueType p) : instance (i), reaction (r), parameter (p), next (NULL) { }
};

struct heap_link_t
{
  Heap* heap;
  pthread_mutex_t mutex;
  size_t change_count;
};

void
allocate_instances (composition::Composer& instance_table)
{
  for (composition::Composer::InstancesType::const_iterator pos = instance_table.instances_begin (),
       limit = instance_table.instances_end ();
       pos != limit;
       ++pos)
    {
      composition::Instance* instance = pos->second;
      component_t* ptr;
      if (instance->is_top_level ())
        {
          const type::Type* type = instance->type;
          size_t size = type->Size ();
          ptr = static_cast<component_t*> (malloc (size));
          memset (ptr, 0, size);
        }
      else
        {
          ptr = reinterpret_cast<component_t*> (reinterpret_cast<char*> (instance->parent->component) + instance->offset ());
        }
      instance->component = ptr;
    }
}

static void
bind (port_t** output_port, component_t* input_instance, const decl::Reaction* reaction, type::Int::ValueType parameter)
{
  port_t* port = new port_t (input_instance, reaction, parameter);
  port->next = *output_port;
  *output_port = port;
}

static heap_link_t*
make_heap_link (Heap* heap,
                Heap* allocator)
{
  // Allocate a new heap link in the parent.
  heap_link_t* hl = static_cast<heap_link_t*> (allocator->allocate (sizeof (heap_link_t)));
  // Set up the link.
  hl->heap = heap;
  pthread_mutex_init (&hl->mutex, NULL);
  return hl;
}

void
create_bindings (composition::Composer& instance_table)
{
  for (composition::Composer::PushPortsType::const_iterator pp_pos = instance_table.push_ports_begin (),
       pp_limit = instance_table.push_ports_end ();
       pp_pos != pp_limit;
       ++pp_pos)
    {
      composition::PushPort* pp = pp_pos->second;
      composition::Instance* output_instance = pp->instance;
      size_t output_port = pp->address - output_instance->address;

      for (composition::ReactionsType::const_iterator reaction_pos = pp->reactions.begin (),
           reaction_limit = pp->reactions.end ();
           reaction_pos != reaction_limit;
           ++reaction_pos)
        {
          composition::Reaction* r = *reaction_pos;
          bind (reinterpret_cast<port_t**> (reinterpret_cast<char*> (output_instance->component) + output_port),
                r->instance->component,
                r->reaction,
                r->iota);
        }
    }

  for (composition::Composer::PullPortsType::const_iterator pp_pos = instance_table.pull_ports_begin (),
       pp_limit = instance_table.pull_ports_end ();
       pp_pos != pp_limit;
       ++pp_pos)
    {
      composition::Instance* pull_port_instance = pp_pos->second->instance;
      size_t pull_port_address = pp_pos->first - pull_port_instance->address;
      composition::Getter* getter = *pp_pos->second->getters.begin ();
      pull_port_t* pull_port = reinterpret_cast<pull_port_t*> (reinterpret_cast<char*> (pull_port_instance->component) + pull_port_address);
      assert (getter->instance != NULL);
      pull_port->instance = getter->instance->component;
      pull_port->getter = getter->getter;
    }
}

void
initialize (ExecutorBase& exec, ComponentInfoBase* info)
{
  if (info->instance ()->is_top_level ())
    {
      // Set up the heap.
      exec.current_info (info);
      // Call the initializer.
      info->instance ()->operation->execute (exec);
      // Clean up any return value.
      exec.stack ().popn (info->instance ()->initializer->return_size ());
    }
}

template <typename T>
static void
evaluate (ExecutorBase& exec, const MemoryModel& memoryModel, const ast::Binary& node, const T& op)
{
  struct visitor : public type::DefaultVisitor
  {
    ExecutorBase& exec;
    const MemoryModel& memoryModel;
    const ast::Binary& node;
    const T& op;

    visitor (ExecutorBase& e,
             const MemoryModel& mm,
             const ast::Binary& n,
             const T& o) : exec (e), memoryModel (mm), node (n), op (o) { }

    void default_action (const type::Type& t)
    {
      TYPE_NOT_REACHED (t);
    }

    void visit (const NamedType& t)
    {
      t.UnderlyingType ()->Accept (*this);
    }

    void visit (const Bool& t)
    {
      op (exec, memoryModel, node, t);
    }

    void visit (const Boolean& t)
    {
      op (exec, memoryModel, node, t);
    }

    void visit (const type::Int& t)
    {
      op (exec, memoryModel, node, t);
    }

    void visit (const Int8& t)
    {
      op (exec, memoryModel, node, t);
    }

    void visit (const Uint& t)
    {
      op (exec, memoryModel, node, t);
    }

    void visit (const Uint8& t)
    {
      op (exec, memoryModel, node, t);
    }

    void visit (const Uint64& t)
    {
      op (exec, memoryModel, node, t);
    }

    void visit (const Float64& t)
    {
      op (exec, memoryModel, node, t);
    }

    void visit (const Pointer& t)
    {
      op (exec, memoryModel, node, t);
    }
  };

  visitor v (exec, memoryModel, node, op);
  op.dispatch_type (node)->Accept (v);
}

struct RetvalDispatch
{
  const type::Type*
  dispatch_type (const ast::Binary& node) const
  {
    UNIMPLEMENTED;
    //return node.typed_value.type;
  }
};

struct LeftDispatch
{
  const type::Type*
  dispatch_type (const ast::Binary& node) const
  {
    UNIMPLEMENTED;
    //return node.left ()->typed_value.type;
  }
};

template <typename T>
struct LeftShiftVisitor : public type::DefaultVisitor
{
  ExecutorBase& exec;
  const MemoryModel& memoryModel;
  const ast::Binary& node;

  LeftShiftVisitor (ExecutorBase& e,
                    const MemoryModel& mm,
                    const ast::Binary& n)
    : exec (e)
    , memoryModel (mm)
    , node (n)
  { }

  void default_action (const type::Type& t)
  {
    TYPE_NOT_REACHED (t);
  }

  void visit (const NamedType& t)
  {
    t.UnderlyingType ()->Accept (*this);
  }

  void visit (const type::Int& t)
  {
    doit (t);
  }

  void visit (const Uint& t)
  {
    doit (t);
  }

  void visit (const Uint8& t)
  {
    doit (t);
  }

  template <typename U>
  void doit (const U&)
  {
    UNIMPLEMENTED;
    // evaluate_expression (exec, memoryModel, node.left ());
    // typename T::ValueType left;
    // exec.stack ().pop (left);
    // evaluate_expression (exec, memoryModel, node.right ());
    // typename U::ValueType right;
    // exec.stack ().pop (right);
    // exec.stack ().push (left << right);
  }
};

struct LeftShift : public RetvalDispatch
{
  template <typename T>
  void
  doit (ExecutorBase& exec,
        const MemoryModel& memoryModel,
        const ast::Binary& node,
        const T&) const
  {
    LeftShiftVisitor<T> v (exec, memoryModel, node);
    UNIMPLEMENTED;
    //node.right ()->typed_value.type->Accept (v);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Int& t) const
  {
    doit (exec, memoryModel, node, t);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const Uint64& t) const
  {
    doit (exec, memoryModel, node, t);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Type& t) const
  {
    TYPE_NOT_REACHED (t);
  }
};

struct RightShift : public RetvalDispatch
{
  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Int&) const
  {
    struct visitor : public type::DefaultVisitor
    {
      ExecutorBase& exec;
      const MemoryModel& memoryModel;
      const ast::Binary& node;

      visitor (ExecutorBase& e,
               const MemoryModel& mm,
               const ast::Binary& n)
        : exec (e)
        , memoryModel (mm)
        , node (n)
      { }

      void default_action (const type::Type& t)
      {
        TYPE_NOT_REACHED (t);
      }

      void visit (const NamedType& t)
      {
        t.UnderlyingType ()->Accept (*this);
      }

      void visit (const type::Int&)
      {
        UNIMPLEMENTED;
        // evaluate_expression (exec, memoryModel, node.left ());
        // type::Int::ValueType left;
        // exec.stack ().pop (left);
        // evaluate_expression (exec, memoryModel, node.right ());
        // Uint::ValueType right;
        // exec.stack ().pop (right);
        // exec.stack ().push (left >> right);
      }

      void visit (const Uint&)
      {
        UNIMPLEMENTED;
        // evaluate_expression (exec, memoryModel, node.left ());
        // Uint::ValueType left;
        // exec.stack ().pop (left);
        // evaluate_expression (exec, memoryModel, node.right ());
        // Uint::ValueType right;
        // exec.stack ().pop (right);
        // exec.stack ().push (left >> right);
      }
    };

    visitor v (exec, memoryModel, node);
    UNIMPLEMENTED;
    //node.right ()->typed_value.type->Accept (v);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Type& t) const
  {
    TYPE_NOT_REACHED (t);
  }
};

struct BitAnd : public RetvalDispatch
{
  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Int&) const
  {
    UNIMPLEMENTED;
    // evaluate_expression (exec, memoryModel, node.left ());
    // type::Int::ValueType left;
    // exec.stack ().pop (left);
    // evaluate_expression (exec, memoryModel, node.right ());
    // type::Int::ValueType right;
    // exec.stack ().pop (right);
    // exec.stack ().push (left & right);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Type& t) const
  {
    TYPE_NOT_REACHED (t);
  }
};

struct BitAndNot : public RetvalDispatch
{
  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Int&) const
  {
    UNIMPLEMENTED;
    // evaluate_expression (exec, memoryModel, node.left ());
    // type::Int::ValueType left;
    // exec.stack ().pop (left);
    // evaluate_expression (exec, memoryModel, node.right ());
    // type::Int::ValueType right;
    // exec.stack ().pop (right);
    // exec.stack ().push (left & (~right));
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Type& t) const
  {
    TYPE_NOT_REACHED (t);
  }
};

struct Add : public RetvalDispatch
{
  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Int& type) const
  {
    doit (exec, memoryModel, node, type);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const Uint& type) const
  {
    doit (exec, memoryModel, node, type);
  }

  template <typename T>
  void
  doit (ExecutorBase& exec,
        const MemoryModel& memoryModel,
        const ast::Binary& node,
        const T&) const
  {
    UNIMPLEMENTED;
    // evaluate_expression (exec, memoryModel, node.left ());
    // typename T::ValueType left;
    // exec.stack ().pop (left);
    // evaluate_expression (exec, memoryModel, node.right ());
    // typename T::ValueType right;
    // exec.stack ().pop (right);
    // exec.stack ().push (left + right);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Type& t) const
  {
    TYPE_NOT_REACHED (t);
  }
};

struct Subtract : public RetvalDispatch
{
  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Int&) const
  {
    UNIMPLEMENTED;
    // evaluate_expression (exec, memoryModel, node.left ());
    // type::Int::ValueType left;
    // exec.stack ().pop (left);
    // evaluate_expression (exec, memoryModel, node.right ());
    // type::Int::ValueType right;
    // exec.stack ().pop (right);
    // exec.stack ().push (left - right);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Type& t) const
  {
    TYPE_NOT_REACHED (t);
  }
};

struct BitOr : public RetvalDispatch
{
  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Int&) const
  {
    UNIMPLEMENTED;
    // evaluate_expression (exec, memoryModel, node.left ());
    // type::Int::ValueType left;
    // exec.stack ().pop (left);
    // evaluate_expression (exec, memoryModel, node.right ());
    // type::Int::ValueType right;
    // exec.stack ().pop (right);
    // exec.stack ().push (left | right);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Type& t) const
  {
    TYPE_NOT_REACHED (t);
  }
};

struct BitXor : public RetvalDispatch
{
  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Int&) const
  {
    UNIMPLEMENTED;
    // evaluate_expression (exec, memoryModel, node.left ());
    // type::Int::ValueType left;
    // exec.stack ().pop (left);
    // evaluate_expression (exec, memoryModel, node.right ());
    // type::Int::ValueType right;
    // exec.stack ().pop (right);
    // exec.stack ().push (left ^ right);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Type& t) const
  {
    TYPE_NOT_REACHED (t);
  }
};

struct Equal : public LeftDispatch
{
  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const Bool& type) const
  {
    doit (exec, memoryModel, node, type);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const Boolean& type) const
  {
    doit (exec, memoryModel, node, type);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Int& type) const
  {
    doit (exec, memoryModel, node, type);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const Uint& type) const
  {
    doit (exec, memoryModel, node, type);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const Uint8& type) const
  {
    doit (exec, memoryModel, node, type);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const Pointer& type) const
  {
    doit (exec, memoryModel, node, type);
  }

  template <typename T>
  void doit (ExecutorBase& exec,
             const MemoryModel& memoryModel,
             const ast::Binary& node,
             const T&) const
  {
    UNIMPLEMENTED;
    // evaluate_expression (exec, memoryModel, node.left ());
    // typename T::ValueType left;
    // exec.stack ().pop (left);
    // evaluate_expression (exec, memoryModel, node.right ());
    // typename T::ValueType right;
    // exec.stack ().pop (right);
    // exec.stack ().push (left == right);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Type& t) const
  {
    TYPE_NOT_REACHED (t);
  }
};

struct NotEqual : public LeftDispatch
{
  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const Bool&) const
  {
    doit<Bool> (exec, memoryModel, node);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Int&) const
  {
    doit<type::Int> (exec, memoryModel, node);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const Uint&) const
  {
    doit<Uint> (exec, memoryModel, node);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const Uint64&) const
  {
    doit<Uint64> (exec, memoryModel, node);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const Pointer&) const
  {
    doit<Pointer> (exec, memoryModel, node);
  }

  template <typename T>
  void
  doit (ExecutorBase& exec, const MemoryModel& memoryModel, const ast::Binary& node) const
  {
    UNIMPLEMENTED;
    // evaluate_expression (exec, memoryModel, node.left ());
    // typename T::ValueType left;
    // exec.stack ().pop (left);
    // evaluate_expression (exec, memoryModel, node.right ());
    // typename T::ValueType right;
    // exec.stack ().pop (right);
    // exec.stack ().push (left != right);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Type& t) const
  {
    TYPE_NOT_REACHED (t);
  }
};

struct LessThan : public LeftDispatch
{
  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Int&) const
  {
    UNIMPLEMENTED;
    // evaluate_expression (exec, memoryModel, node.left ());
    // type::Int::ValueType left;
    // exec.stack ().pop (left);
    // evaluate_expression (exec, memoryModel, node.right ());
    // type::Int::ValueType right;
    // exec.stack ().pop (right);
    // exec.stack ().push (left < right);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const Int8&) const
  {
    UNIMPLEMENTED;
    // evaluate_expression (exec, memoryModel, node.left ());
    // Int8::ValueType left;
    // exec.stack ().pop (left);
    // evaluate_expression (exec, memoryModel, node.right ());
    // Int8::ValueType right;
    // exec.stack ().pop (right);
    // exec.stack ().push (left < right);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Type& t) const
  {
    TYPE_NOT_REACHED (t);
  }
};

struct LessEqual : public LeftDispatch
{
  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Int&) const
  {
    UNIMPLEMENTED;
    // evaluate_expression (exec, memoryModel, node.left ());
    // type::Int::ValueType left;
    // exec.stack ().pop (left);
    // evaluate_expression (exec, memoryModel, node.right ());
    // type::Int::ValueType right;
    // exec.stack ().pop (right);
    // exec.stack ().push (left <= right);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Type& t) const
  {
    TYPE_NOT_REACHED (t);
  }
};

struct MoreThan : public LeftDispatch
{
  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Int&) const
  {
    UNIMPLEMENTED;
    // evaluate_expression (exec, memoryModel, node.left ());
    // type::Int::ValueType left;
    // exec.stack ().pop (left);
    // evaluate_expression (exec, memoryModel, node.right ());
    // type::Int::ValueType right;
    // exec.stack ().pop (right);
    // exec.stack ().push (left > right);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Type& t) const
  {
    TYPE_NOT_REACHED (t);
  }
};

struct MoreEqual : public LeftDispatch
{
  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Int&) const
  {
    UNIMPLEMENTED;
    // evaluate_expression (exec, memoryModel, node.left ());
    // type::Int::ValueType left;
    // exec.stack ().pop (left);
    // evaluate_expression (exec, memoryModel, node.right ());
    // type::Int::ValueType right;
    // exec.stack ().pop (right);
    // exec.stack ().push (left >= right);
  }

  void
  operator() (ExecutorBase& exec,
              const MemoryModel& memoryModel,
              const ast::Binary& node,
              const type::Type& t) const
  {
    TYPE_NOT_REACHED (t);
  }
};

bool
enabled (ExecutorBase& exec,
         component_t* instance,
         const decl::Action* action,
         type::Int::ValueType iota)
{
  assert (exec.stack ().empty ());

  // Push receiver.
  exec.stack ().push_pointer (instance);
  // Push iota.
  if (action->has_dimension ())
    {
      exec.stack ().push<Uint::ValueType> (iota);
    }
  // Push an instruction pointer.
  exec.stack ().push_pointer (NULL);
  exec.stack ().setup (0 /* No locals. */);
  action->precondition->operation->execute (exec);
  Bool::ValueType retval;
  exec.stack ().pop (retval);
  exec.stack ().teardown ();
  // Pop the instruction pointer.
  exec.stack ().pop_pointer ();
  // Pop iota.
  if (action->has_dimension ())
    {
      Uint::ValueType x;
      exec.stack ().pop (x);
    }
  // Pop this.
  exec.stack ().pop_pointer ();
  assert (exec.stack ().empty ());
  return retval;
}

static void
execute (ExecutorBase& exec,
         const decl::Action* action,
         component_t* instance)
{
  // Reset the mutable phase base pointer.
  exec.mutable_phase_base_pointer (0);

  exec.stack ().setup (action->memory_model.locals_size ());

  action->body->operation->execute (exec);

  if (exec.mutable_phase_base_pointer () == 0)
    {
      // No activations.  Pop the base pointer and finish.
      exec.stack ().teardown ();
      return;
    }

  // Process all of the deferred activation bodies.
  // First, go to the last frame.
  exec.stack ().base_pointer (exec.mutable_phase_base_pointer ());

  while (exec.stack ().base_pointer () != NULL)
    {
      // Get the deferred body.
      Operation* o = *static_cast<Operation**> (exec.stack ().pointer_to_instruction_pointer ());

      // Execute it.
      o->execute (exec);

      // Pop the base pointer.
      exec.stack ().teardown ();
    }
}

void execute_no_check (ExecutorBase& exec, component_t* instance, const decl::Action* action, type::Int::ValueType iota)
{
  assert (exec.stack ().empty ());

  // Push the instance.
  exec.stack ().push_pointer (instance);
  // Push iota.
  if (action->has_dimension ())
    {
      exec.stack ().push<Uint::ValueType> (iota);
    }
  // Push the instruction pointer.
  exec.stack ().push_pointer (NULL);
  // Execute.
  runtime::execute (exec, action, instance);
  // Pop the instruction pointer.
  exec.stack ().pop_pointer ();
  // Pop iota.
  if (action->has_dimension ())
    {
      Uint::ValueType x;
      exec.stack ().pop (x);
    }
  // Pop the instance.
  exec.stack ().pop_pointer ();

  assert (exec.stack ().empty ());
}

// TODO:  Single instance of Template?

Readable::Readable (const util::Location& loc)
  : BuiltinFunction ("readable",
                     loc,
                     new type::Function (type::Function::FUNCTION, (new ParameterList (loc))
                                         ->append (ParameterSymbol::make (loc, "fd", &type::named_file_descriptor, Immutable, Foreign)),
                                         (new ParameterList (loc))->append (ParameterSymbol::makeReturn (loc, ReturnSymbol, &type::named_bool, Immutable))))
{ }

void
Readable::call (runtime::ExecutorBase& exec) const
{
  runtime::FileDescriptor** fd = static_cast< runtime::FileDescriptor**> (exec.stack ().get_address (type->parameter_list->at (0)->offset ()));
  Bool::ValueType* r = static_cast<Bool::ValueType*> (exec.stack ().get_address (type->GetReturnParameter ()->offset ()));

  struct pollfd pfd;
  pfd.fd = (*fd)->fd ();
  pfd.events = POLLIN;

  int s = poll (&pfd, 1, 0);

  if (s < 0)
    {
      error (EXIT_FAILURE, errno, "poll");
    }

  exec.checkedForReadability (*fd);

  *r = (pfd.revents & POLLIN) != 0;
}

Read::Read (const util::Location& loc)
  : BuiltinFunction ("read",
                     loc,
                     new type::Function (type::Function::FUNCTION, (new ParameterList (loc))
                                         ->append (ParameterSymbol::make (loc, "fd", &type::named_file_descriptor, Immutable, Mutable))
                                         ->append (ParameterSymbol::make (loc, "buf", type::named_byte.get_slice (), Immutable, Mutable)),
                                         (new ParameterList (loc))->append (ParameterSymbol::makeReturn (loc, ReturnSymbol, Int::Instance (), Immutable))))
{ }

void
Read::call (runtime::ExecutorBase& exec) const
{
  runtime::FileDescriptor** fd = static_cast< runtime::FileDescriptor**> (exec.stack ().get_address (type->parameter_list->at (0)->offset ()));
  Slice::ValueType* buf = static_cast<Slice::ValueType*> (exec.stack ().get_address (type->parameter_list->at (1)->offset ()));
  Int::ValueType* r = static_cast<Int::ValueType*> (exec.stack ().get_address (type->GetReturnParameter ()->offset ()));
  *r = read ((*fd)->fd (), buf->ptr, buf->length);
}

Writable::Writable (const util::Location& loc)
  : BuiltinFunction ("writable",
                     loc,
                     new type::Function (type::Function::FUNCTION, (new ParameterList (loc))
                                         ->append (ParameterSymbol::make (loc, "fd", &type::named_file_descriptor, Immutable, Foreign)),
                                         (new ParameterList (loc))->append (ParameterSymbol::makeReturn (loc, ReturnSymbol, &type::named_bool, Immutable))))
{ }

void
Writable::call (runtime::ExecutorBase& exec) const
{
  runtime::FileDescriptor** fd = static_cast< runtime::FileDescriptor**> (exec.stack ().get_address (type->parameter_list->at (0)->offset ()));
  Bool::ValueType* r = static_cast<Bool::ValueType*> (exec.stack ().get_address (type->GetReturnParameter ()->offset ()));

  struct pollfd pfd;
  pfd.fd = (*fd)->fd ();
  pfd.events = POLLOUT;

  int s = poll (&pfd, 1, 0);

  if (s < 0)
    {
      error (EXIT_FAILURE, errno, "poll");
    }

  exec.checkedForWritability (*fd);

  *r = (pfd.revents & POLLOUT) != 0;
}

ClockGettime::ClockGettime (const util::Location& loc)
  : BuiltinFunction ("clock_gettime",
                     loc,
                     new type::Function (type::Function::FUNCTION, (new ParameterList (loc))
                                         ->append (ParameterSymbol::make (loc, "tp", type::named_timespec.get_pointer (), Immutable, Foreign)),
                                         (new ParameterList (loc))->append (ParameterSymbol::makeReturn (loc, ReturnSymbol, &type::named_int, Immutable))))
{ }

void
ClockGettime::call (runtime::ExecutorBase& exec) const
{
  struct timespec* ts = *static_cast< struct timespec**> (exec.stack ().get_address (type->parameter_list->at (0)->offset ()));
  Int::ValueType* r = static_cast<Int::ValueType*> (exec.stack ().get_address (type->GetReturnParameter ()->offset ()));
  *r = clock_gettime (CLOCK_REALTIME, ts);
}

TimerfdCreate::TimerfdCreate (const util::Location& loc)
  : BuiltinFunction ("timerfd_create",
                     loc,
                     new type::Function (type::Function::FUNCTION, new ParameterList (loc),
                                         (new ParameterList (loc))->append (ParameterSymbol::makeReturn (loc, ReturnSymbol, &type::named_file_descriptor, Mutable))))
{ }

void
TimerfdCreate::call (runtime::ExecutorBase& exec) const
{
  runtime::FileDescriptor** ret = static_cast< runtime::FileDescriptor**> (exec.stack ().get_address (type->GetReturnParameter ()->offset ()));
  int fd = timerfd_create (CLOCK_MONOTONIC, TFD_NONBLOCK);
  if (fd != -1)
    {
      *ret = exec.allocateFileDescriptor (fd);
    }
  else
    {
      *ret = NULL;
    }
}

TimerfdSettime::TimerfdSettime (const util::Location& loc)
  : BuiltinFunction ("timerfd_settime",
                     loc,
                     new type::Function (type::Function::FUNCTION, (new ParameterList (loc))
                                         ->append (ParameterSymbol::make (loc, "fd", &type::named_file_descriptor, Immutable, Mutable))
                                         ->append (ParameterSymbol::make (loc, "s", &type::named_uint64, Immutable, Immutable)),
                                         (new ParameterList (loc))->append (ParameterSymbol::makeReturn (loc, ReturnSymbol, &type::named_int, Immutable))))
{ }

void
TimerfdSettime::call (runtime::ExecutorBase& exec) const
{
  runtime::FileDescriptor** fd = static_cast< runtime::FileDescriptor**> (exec.stack ().get_address (type->parameter_list->at (0)->offset ()));
  Uint64::ValueType* v = static_cast<Uint64::ValueType*> (exec.stack ().get_address (type->parameter_list->at (1)->offset ()));
  Int::ValueType* r = static_cast<Int::ValueType*> (exec.stack ().get_address (type->GetReturnParameter ()->offset ()));

  struct itimerspec spec;
  spec.it_interval.tv_sec = *v;
  spec.it_interval.tv_nsec = 0;
  spec.it_value.tv_sec = *v;
  spec.it_value.tv_nsec = 0;
  *r = timerfd_settime ((*fd)->fd (), 0, &spec, NULL);
}

UdpSocket::UdpSocket (const util::Location& loc)
  : BuiltinFunction ("udp_socket",
                     loc,
                     new type::Function (type::Function::FUNCTION, new ParameterList (loc),
                                         (new ParameterList (loc))->append (ParameterSymbol::makeReturn (loc, ReturnSymbol, &type::named_file_descriptor, Mutable))))
{ }

void
UdpSocket::call (runtime::ExecutorBase& exec) const
{
  runtime::FileDescriptor** ret = static_cast< runtime::FileDescriptor**> (exec.stack ().get_address (type->GetReturnParameter ()->offset ()));

  int fd = socket (AF_INET, SOCK_DGRAM, 0);
  if (fd == -1)
    {
      *ret = NULL;
      return;
    }

  int s = fcntl (fd, F_SETFL, O_NONBLOCK);
  if (s == -1)
    {
      *ret = NULL;
      return;
    }

  *ret = exec.allocateFileDescriptor (fd);
}

Sendto::Sendto (const util::Location& loc)
  : BuiltinFunction ("sendto",
                     loc,
                     new type::Function (type::Function::FUNCTION, (new ParameterList (loc))
                                         ->append (ParameterSymbol::make (loc, "fd", &type::named_file_descriptor, Immutable, Mutable))
                                         ->append (ParameterSymbol::make (loc, "host", &type::named_string, Immutable, Foreign))
                                         ->append (ParameterSymbol::make (loc, "port", &type::named_uint16, Immutable, Immutable))
                                         ->append (ParameterSymbol::make (loc, "buf", type::named_byte.get_slice (), Immutable, Foreign)),
                                         (new ParameterList (loc))->append (ParameterSymbol::makeReturn (loc, ReturnSymbol, Int::Instance (), Immutable))))
{ }

void
Sendto::call (runtime::ExecutorBase& exec) const
{
  runtime::FileDescriptor** fd = static_cast< runtime::FileDescriptor**> (exec.stack ().get_address (type->parameter_list->at (0)->offset ()));
  StringU::ValueType* host = static_cast<StringU::ValueType*> (exec.stack ().get_address (type->parameter_list->at (1)->offset ()));
  Uint16::ValueType* port = static_cast<Uint16::ValueType*> (exec.stack ().get_address (type->parameter_list->at (2)->offset ()));
  Slice::ValueType* buf = static_cast<Slice::ValueType*> (exec.stack ().get_address (type->parameter_list->at (3)->offset ()));
  Int::ValueType* ret = static_cast<Int::ValueType*> (exec.stack ().get_address (type->GetReturnParameter ()->offset ()));

  std::string host2 (static_cast<const char*> (host->ptr), host->length);
  std::stringstream port2;
  port2 << *port;

  struct addrinfo* info;
  struct addrinfo hints;
  memset (&hints, 0, sizeof (struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_NUMERICSERV;
  int r = getaddrinfo (host2.c_str (), port2.str ().c_str (), &hints, &info);
  if (r != 0)
    {
      UNIMPLEMENTED;
    }

  ssize_t s = sendto ((*fd)->fd (), buf->ptr, buf->length, 0, info->ai_addr, info->ai_addrlen);
  if (s != static_cast<ssize_t> (buf->length))
    {
      UNIMPLEMENTED;
    }

  freeaddrinfo (info);
  *ret = 0;
}

OpReturn
Load::execute (ExecutorBase& exec) const
{
  child->execute (exec);
  void* ptr = exec.stack ().pop_pointer ();
  exec.stack ().load (ptr, type->Size ());
  return make_continue ();
}


OpReturn
IndexSlice::execute (ExecutorBase& exec) const
{
  base->execute (exec);
  Slice::ValueType s;
  exec.stack ().pop (s);
  index->execute (exec);
  type::Int::ValueType i;
  exec.stack ().pop (i);

  if (i < 0 || static_cast<type::Uint::ValueType> (i) >= s.length)
    {
      error_at_line (-1, 0, location.file.c_str (), location.line,
                     "slice index is out of bounds (E35)");

    }

  exec.stack ().push_pointer (static_cast<char*> (s.ptr) + i * type->UnitSize ());

  return make_continue ();
}

template<typename T>
struct ConvertToInt : public Operation
{
  ConvertToInt (const Operation* c) : child (c) { }
  OpReturn execute (ExecutorBase& exec) const
  {
    child->execute (exec);
    typename T::ValueType in;
    exec.stack ().pop (in);
    type::Int::ValueType out = in;
    exec.stack ().push (out);
    return make_continue ();
  }
  virtual void dump () const
  {
    std::cout << "ConvertToInt(";
    child->dump ();
    std::cout << ")\n";
  }
  const Operation* const child;
};

// template<typename T>
// static Operation*
// makeConvertToInt (const T& t, const Operation* child)
// {
//   return new ConvertToInt<T> (child);
// }

// struct ConvertToIntListener
// {
//   const Operation* const child;
//   Operation* operation;
//   ConvertToIntListener (const Operation* c) : child (c), operation (NULL) { }

//   void NotIntegral (const type::Type& type)
//   {
//     NOT_REACHED;
//   }

//   void operator() (const Uint8& type)
//   {
//     operation = makeConvertToInt (type, child);
//   }
//   void operator() (const Uint16& type)
//   {
//     operation = makeConvertToInt (type, child);
//   }
//   void operator() (const Uint32& type)
//   {
//     operation = makeConvertToInt (type, child);
//   }
//   void operator() (const Uint64& type)
//   {
//     operation = makeConvertToInt (type, child);
//   }

//   void operator() (const Int8& type)
//   {
//     operation = makeConvertToInt (type, child);
//   }
//   void operator() (const Int16& type)
//   {
//     operation = makeConvertToInt (type, child);
//   }
//   void operator() (const Int32& type)
//   {
//     operation = makeConvertToInt (type, child);
//   }
//   void operator() (const Int64& type)
//   {
//     operation = makeConvertToInt (type, child);
//   }

//   void operator() (const Uint& type)
//   {
//     operation = makeConvertToInt (type, child);
//   }
//   void operator() (const type::Int& type)
//   {
//     operation = makeConvertToInt (type, child);
//   }
//   void operator() (const Uintptr& type)
//   {
//     operation = makeConvertToInt (type, child);
//   }
//   void operator() (const Rune& type)
//   {
//     NotIntegral (type);
//   }
//   void operator() (const Integer& type)
//   {
//     NotIntegral (type);
//   }
// };

Operation*
MakeConvertToInt (const Operation* c, const type::Type* type)
{
  switch (type->underlying_kind ())
    {
    case kUint8:
      return new ConvertToInt<Uint8> (c);
    case kUint16:
      return new ConvertToInt<Uint16> (c);
    case kUint32:
      return new ConvertToInt<Uint32> (c);
    case kUint64:
      return new ConvertToInt<Uint64> (c);
    case kInt8:
      return new ConvertToInt<Int8> (c);
    case kInt16:
      return new ConvertToInt<Int16> (c);
    case kInt32:
      return new ConvertToInt<Int32> (c);
    case kInt64:
      return new ConvertToInt<Int64> (c);
    case kUint:
      return new ConvertToInt<Uint> (c);
    case kInt:
      return new ConvertToInt<Int> (c);
    case kUintptr:
      return new ConvertToInt<Uintptr> (c);

    default:
      TYPE_NOT_REACHED (*type);
    }
}

template<typename T>
struct ConvertToUint : public Operation
{
  ConvertToUint (const Operation* c) : child (c) { }
  OpReturn execute (ExecutorBase& exec) const
  {
    child->execute (exec);
    typename T::ValueType in;
    exec.stack ().pop (in);
    type::Uint::ValueType out = in;
    exec.stack ().push (out);
    return make_continue ();
  }
  virtual void dump () const
  {
    std::cout << "ConvertToUint(";
    child->dump ();
    std::cout << ")";
  }
  const Operation* const child;
};

Operation*
MakeConvertToUint (const Operation* c, const type::Type* type)
{
  switch (type->underlying_kind ())
    {
    case kUint8:
      return new ConvertToUint<Uint8> (c);
    case kUint16:
      return new ConvertToUint<Uint16> (c);
    case kUint32:
      return new ConvertToUint<Uint32> (c);
    case kUint64:
      return new ConvertToUint<Uint64> (c);
    case kInt8:
      return new ConvertToUint<Int8> (c);
    case kInt16:
      return new ConvertToUint<Int16> (c);
    case kInt32:
      return new ConvertToUint<Int32> (c);
    case kInt64:
      return new ConvertToUint<Int64> (c);
    case kUint:
      return new ConvertToUint<Uint> (c);
    case kInt:
      return new ConvertToUint<Int> (c);
    case kUintptr:
      return new ConvertToUint<Uintptr> (c);

    default:
      TYPE_NOT_REACHED (*type);
    }
}

OpReturn
LogicOr::execute (ExecutorBase& exec) const
{
  left->execute (exec);
  Bool::ValueType b;
  exec.stack ().pop (b);
  if (b)
    {
      exec.stack ().push (b);
    }
  else
    {
      right->execute (exec);
    }
  return make_continue ();
}

OpReturn
LogicAnd::execute (ExecutorBase& exec) const
{
  left->execute (exec);
  Bool::ValueType b;
  exec.stack ().pop (b);
  if (!b)
    {
      exec.stack ().push (b);
    }
  else
    {
      right->execute (exec);
    }
  return make_continue ();
}

struct MakeLiteralVisitor : public type::DefaultVisitor
{
  const Value& value;
  Operation* op;

  MakeLiteralVisitor (const Value& v) : value (v), op (NULL) { }

  void default_action (const type::Type& type)
  {
    TYPE_NOT_REACHED (type);
  }

  void visit (const Bool& type)
  {
    op = make_literal (value.bool_value);
  }

  void visit (const Uint8& type)
  {
    op = make_literal (value.uint8_value);
  }

  void visit (const Uint16& type)
  {
    op = make_literal (value.uint16_value);
  }

  void visit (const Uint32& type)
  {
    op = make_literal (value.uint32_value);
  }

  void visit (const Uint64& type)
  {
    op = make_literal (value.uint64_value);
  }

  void visit (const Int8& type)
  {
    op = make_literal (value.int8_value);
  }

  void visit (const Int16& type)
  {
    op = make_literal (value.int16_value);
  }

  void visit (const Int32& type)
  {
    op = make_literal (value.int32_value);
  }

  void visit (const Int64& type)
  {
    op = make_literal (value.int64_value);
  }

  void visit (const Uint& type)
  {
    op = make_literal (value.uint_value);
  }

  void visit (const Int& type)
  {
    op = make_literal (value.int_value);
  }

  void visit (const Float64& type)
  {
    op = make_literal (value.float64_value);
  }

  void visit (const StringU& type)
  {
    op = make_literal (value.stringu_value);
  }

  void visit (const Pointer& type)
  {
    op = make_literal (value.pointer_value);
  }

  void visit (const Slice& type)
  {
    op = make_literal (value.slice_value);
  }
};

Operation* make_literal (const type::Type* type, const Value& value)
{
  assert (value.present);
  MakeLiteralVisitor visitor (value);
  type->UnderlyingType ()->Accept (visitor);
  return visitor.op;
}

OpReturn
ListOperation::execute (ExecutorBase& exec) const
{
  for (ListType::const_iterator pos = list.begin (), limit = list.end ();
       pos != limit;
       ++pos)
    {
      OpReturn r = (*pos)->execute (exec);
      if (r == kReturn)
        {
          return r;
        }
    }
  return make_continue ();
}

OpReturn
FunctionCall::execute (ExecutorBase& exec) const
{
  // Create space for the return.
  exec.stack ().reserve (callable->return_size ());

  // Push the arguments.
  arguments->execute (exec);

  // Push a fake instruction pointer.
  exec.stack ().push_pointer (NULL);

  // Setup the frame.
  exec.stack ().setup (callable->locals_size ());

  // Do the call.
  callable->call (exec);

  // Tear down the frame.
  exec.stack ().teardown ();

  // Pop the fake instruction pointer.
  exec.stack ().pop_pointer ();

  // Pop the arguments.
  exec.stack ().popn (callable->arguments_size ());

  return make_continue ();
}

OpReturn
MethodCall::execute (ExecutorBase& exec) const
{
  // Create space for the return.
  exec.stack ().reserve (callable->return_size ());

  // Push the receiver.
  receiver->execute (exec);

  // Push the arguments.
  arguments->execute (exec);

  // Push a fake instruction pointer.
  exec.stack ().push_pointer (NULL);

  // Setup the frame.
  exec.stack ().setup (callable->locals_size ());

  // Do the call.
  callable->call (exec);

  // Tear down the frame.
  exec.stack ().teardown ();

  // Pop the fake instruction pointer.
  exec.stack ().pop_pointer ();

  // Pop the arguments.
  exec.stack ().popn (callable->arguments_size ());

  exec.stack ().popn (callable->receiver_size ());

  return make_continue ();
}

OpReturn
DynamicFunctionCall::execute (ExecutorBase& exec) const
{
  switch (type->function_kind)
    {
    case type::Function::FUNCTION:
      UNIMPLEMENTED;
    case type::Function::PUSH_PORT:
      UNIMPLEMENTED;
    case type::Function::PULL_PORT:
    {
      func->execute (exec);
      pull_port_t pp;
      exec.stack ().pop (pp);

      // Create space for the return.
      exec.stack ().reserve (pp.getter->return_size ());

      // Push the arguments.
      exec.stack ().push_pointer (pp.instance);
      arguments->execute (exec);

      // Push a fake instruction pointer.
      exec.stack ().push_pointer (NULL);

      // Setup the frame.
      exec.stack ().setup (pp.getter->locals_size ());

      // Do the call.
      pp.getter->call (exec);

      // Tear down the frame.
      exec.stack ().teardown ();

      // Pop the fake instruction pointer.
      exec.stack ().pop_pointer ();

      // Pop the arguments.
      exec.stack ().popn (pp.getter->arguments_size ());
      exec.stack ().pop_pointer ();

      return make_continue ();
    }
    break;
    }

  NOT_REACHED;
}

OpReturn
Instance::execute (ExecutorBase& exec) const
{
  exec.stack ().push_pointer (instance->instance->component);
  return make_continue ();
}

OpReturn
SetRestoreCurrentInstance::execute (ExecutorBase& exec) const
{
  ComponentInfoBase* new_info = component_to_info (static_cast<component_t*> (exec.stack ().read_pointer (receiver_offset)));
  ComponentInfoBase* old_info = exec.current_info (new_info);
  OpReturn ca = child->execute (exec);
  exec.current_info (old_info);
  return ca;
}

OpReturn
Clear::execute (ExecutorBase& exec) const
{
  exec.stack ().clear (offset, size);
  return make_continue ();
}

OpReturn
Assign::execute (ExecutorBase& exec) const
{
  left->execute (exec);
  void* ptr = exec.stack ().pop_pointer ();
  right->execute (exec);
  exec.stack ().store (ptr, size);
  return make_continue ();
}

template <typename T>
struct AddAssign : public Operation
{
  AddAssign (Operation* l, Operation* r) : left (l), right (r)
  {
    assert (left != NULL);
    assert (right != NULL);
  }
  OpReturn
  execute (ExecutorBase& exec) const
  {
    left->execute (exec);
    T* ptr = static_cast<T*> (exec.stack ().pop_pointer ());
    right->execute (exec);
    T v;
    exec.stack ().pop (v);
    *ptr += v;
    return make_continue ();
  }
  virtual void dump () const
  {
    UNIMPLEMENTED;
  }
  Operation* const left;
  Operation* const right;
};

Operation* make_add_assign (Operation* l, Operation* r, const type::Type* t)
{
  switch (t->underlying_kind ())
    {
    case kUint8:
      return new AddAssign<Uint8::ValueType> (l, r);
    case kUint16:
      return new AddAssign<Uint16::ValueType> (l, r);
    case kUint32:
      return new AddAssign<Uint32::ValueType> (l, r);
    case kUint64:
      return new AddAssign<Uint64::ValueType> (l, r);
    case kInt8:
      return new AddAssign<Int8::ValueType> (l, r);
    case kInt16:
      return new AddAssign<Int16::ValueType> (l, r);
    case kInt32:
      return new AddAssign<Int32::ValueType> (l, r);
    case kInt64:
      return new AddAssign<Int64::ValueType> (l, r);
    case kFloat32:
      return new AddAssign<Float32::ValueType> (l, r);
    case kFloat64:
      return new AddAssign<Float64::ValueType> (l, r);
    case kComplex64:
      return new AddAssign<Complex64::ValueType> (l, r);
    case kComplex128:
      return new AddAssign<Complex128::ValueType> (l, r);
    case kUint:
      return new AddAssign<Uint::ValueType> (l, r);
    case kInt:
      return new AddAssign<Int::ValueType> (l, r);
    case kUintptr:
      return new AddAssign<Uintptr::ValueType> (l, r);
    default:
      NOT_REACHED;
    }
}

OpReturn
Reference::execute (ExecutorBase& exec) const
{
  exec.stack ().push_address (offset);
  return make_continue ();
}

OpReturn
Select::execute (ExecutorBase& exec) const
{
  base->execute (exec);
  char* ptr = static_cast<char*> (exec.stack ().pop_pointer ());
  ptr += offset;
  exec.stack ().push_pointer (ptr);
  return make_continue ();
}

OpReturn
IndexArray::execute (ExecutorBase& exec) const
{
  base->execute (exec);
  void* ptr = exec.stack ().pop_pointer ();
  index->execute (exec);
  type::Int::ValueType i;
  exec.stack ().pop (i);
  if (i < 0 || i >= type->dimension)
    {
      error_at_line (-1, 0, location.file.c_str (), location.line,
                     "array index is out of bounds (E148)");
    }
  exec.stack ().push_pointer (static_cast<char*> (ptr) + i * type->UnitSize ());
  return make_continue ();
}

OpReturn
SliceArray::execute (ExecutorBase& exec) const
{
  base->execute (exec);
  char* ptr = static_cast<char*> (exec.stack ().pop_pointer ());
  Int::ValueType low_val = 0;
  if (low)
    {
      low->execute (exec);
      exec.stack ().pop (low_val);
    }
  Int::ValueType high_val = type->dimension;
  if (high)
    {
      high->execute (exec);
      exec.stack ().pop (high_val);
    }
  Int::ValueType max_val = type->dimension;
  if (max)
    {
      max->execute (exec);
      exec.stack ().pop (max_val);
    }

  // Bounds check.
  if (!(0 <= low_val &&
        low_val <= high_val &&
        high_val <= max_val &&
        max_val <= type->dimension))
    {
      error_at_line (-1, 0, location.file.c_str (), location.line,
                     "slice index is out of range (E223)");
    }

  Slice::ValueType slice_val;
  slice_val.length = high_val - low_val;
  slice_val.capacity = max_val - low_val;
  slice_val.ptr = slice_val.length ? ptr + low_val * type->UnitSize () : NULL;
  exec.stack ().push (slice_val);

  return make_continue ();
}

OpReturn
SliceSlice::execute (ExecutorBase& exec) const
{
  base->execute (exec);
  Slice::ValueType s;
  exec.stack ().pop (s);
  Int::ValueType low_val = 0;
  if (low)
    {
      low->execute (exec);
      exec.stack ().pop (low_val);
    }
  Int::ValueType high_val = s.length;
  if (high)
    {
      high->execute (exec);
      exec.stack ().pop (high_val);
    }
  Int::ValueType max_val = s.capacity;
  if (max)
    {
      max->execute (exec);
      exec.stack ().pop (max_val);
    }

  // Bounds check.
  if (!(0 <= low_val &&
        low_val <= high_val &&
        high_val <= max_val &&
        max_val <= static_cast<Int::ValueType> (s.capacity)))
    {
      error_at_line (-1, 0, location.file.c_str (), location.line,
                     "slice index is out of range (E22)");
    }

  s.length = high_val - low_val;
  s.capacity = max_val - low_val;
  s.ptr = s.length ? static_cast<char*> (s.ptr) + low_val * type->UnitSize () : NULL;
  exec.stack ().push (s);

  return make_continue ();
}

OpReturn
Return::execute (ExecutorBase& exec) const
{
  child->execute (exec);
  exec.stack ().move (return_offset, return_size);
  return make_return ();
}

OpReturn
If::execute (ExecutorBase& exec) const
{
  condition->execute (exec);
  Bool::ValueType c;
  exec.stack ().pop (c);
  if (c)
    {
      return true_branch->execute (exec);
    }
  else
    {
      return false_branch->execute (exec);
    }
}

OpReturn
While::execute (ExecutorBase& exec) const
{
  for (;;)
    {
      condition->execute (exec);
      Bool::ValueType c;
      exec.stack ().pop (c);
      if (c)
        {
          OpReturn r = body->execute (exec);
          if (r == kReturn)
            {
              return r;
            }
        }
      else
        {
          return make_continue ();
        }
    }
}

OpReturn
Change::execute (ExecutorBase& exec) const
{
  root->execute (exec);
  heap_link_t* hl = static_cast<heap_link_t*> (exec.stack ().pop_pointer ());
  if (hl == NULL)
    {
      // Heap link is null.
      UNIMPLEMENTED;
    }
  pthread_mutex_lock (&hl->mutex);
  ++hl->change_count;
  pthread_mutex_unlock (&hl->mutex);

  // Save the old heap.
  Heap* old_heap = exec.heap ();
  // Set the the new heap.
  exec.heap (hl->heap);

  char** root_value = static_cast<char**> (exec.stack ().get_address (root_offset));

  // Push a pointer to the root object.
  *root_value = static_cast<char*> (hl->heap->root ());

  OpReturn ca = body->execute (exec);

  // Restore the old heap.
  exec.heap (old_heap);

  pthread_mutex_lock (&hl->mutex);
  --hl->change_count;
  pthread_mutex_unlock (&hl->mutex);

  return ca;
}

template <typename T>
struct Increment : public Operation
{
  Increment (Operation* c) : child (c) { }
  virtual OpReturn execute (ExecutorBase& exec) const
  {
    child->execute (exec);
    T* ptr = static_cast<T*> (exec.stack ().pop_pointer ());
    ++*ptr;
    return make_continue ();
  }
  virtual void dump () const
  {
    UNIMPLEMENTED;
  }
  Operation* const child;
};

template <typename T>
struct Decrement : public Operation
{
  Decrement (Operation* c) : child (c) { }
  virtual OpReturn execute (ExecutorBase& exec) const
  {
    child->execute (exec);
    T* ptr = static_cast<T*> (exec.stack ().pop_pointer ());
    --*ptr;
    return make_continue ();
  }
  virtual void dump () const
  {
    UNIMPLEMENTED;
  }
  Operation* const child;
};

Operation* make_increment (Operation* child, const type::Type* type)
{
  switch (type->underlying_kind ())
    {
    case kUint8:
      return new Increment<Uint8::ValueType> (child);
    case kUint16:
      return new Increment<Uint16::ValueType> (child);
    case kUint32:
      return new Increment<Uint32::ValueType> (child);
    case kUint64:
      return new Increment<Uint64::ValueType> (child);
    case kInt8:
      return new Increment<Int8::ValueType> (child);
    case kInt16:
      return new Increment<Int16::ValueType> (child);
    case kInt32:
      return new Increment<Int32::ValueType> (child);
    case kInt64:
      return new Increment<Int64::ValueType> (child);
    case kFloat32:
      return new Increment<Float32::ValueType> (child);
    case kFloat64:
      return new Increment<Float64::ValueType> (child);
    case kComplex64:
      return new Increment<Complex64::ValueType> (child);
    case kComplex128:
      return new Increment<Complex128::ValueType> (child);
    case kUint:
      return new Increment<Uint::ValueType> (child);
    case kInt:
      return new Increment<Int::ValueType> (child);
    case kUintptr:
      return new Increment<Uintptr::ValueType> (child);
    default:
      TYPE_NOT_REACHED (*type);
    }
}

Operation* make_decrement (Operation* child, const type::Type* type)
{
  switch (type->underlying_kind ())
    {
    case kUint8:
      return new Decrement<Uint8::ValueType> (child);
    case kUint16:
      return new Decrement<Uint16::ValueType> (child);
    case kUint32:
      return new Decrement<Uint32::ValueType> (child);
    case kUint64:
      return new Decrement<Uint64::ValueType> (child);
    case kInt8:
      return new Decrement<Int8::ValueType> (child);
    case kInt16:
      return new Decrement<Int16::ValueType> (child);
    case kInt32:
      return new Decrement<Int32::ValueType> (child);
    case kInt64:
      return new Decrement<Int64::ValueType> (child);
    case kFloat32:
      return new Decrement<Float32::ValueType> (child);
    case kFloat64:
      return new Decrement<Float64::ValueType> (child);
    case kComplex64:
      return new Decrement<Complex64::ValueType> (child);
    case kComplex128:
      return new Decrement<Complex128::ValueType> (child);
    case kUint:
      return new Decrement<Uint::ValueType> (child);
    case kInt:
      return new Decrement<Int::ValueType> (child);
    case kUintptr:
      return new Decrement<Uintptr::ValueType> (child);
    default:
      TYPE_NOT_REACHED (*type);
    }
}

OpReturn
Activate::execute (ExecutorBase& exec) const
{
  // Save the base pointer to return to.
  // Currently, this is done in the port call.
  // char* return_bp = *reinterpret_cast<char**> (exec.stack ().base_pointer ());
  // Insert into the mutable phase list.
  *reinterpret_cast<char**> (exec.stack ().base_pointer ()) = exec.mutable_phase_base_pointer ();
  exec.mutable_phase_base_pointer (exec.stack ().base_pointer ());

  // The caller pushed a (fake) instruction pointer which is just
  // before the base pointer.  Overwrite it with the body.
  Operation** ip = static_cast<Operation**> (exec.stack ().pointer_to_instruction_pointer ());
  *ip = body;
  // Execute the expression list.
  port_calls->execute (exec);

  return make_return ();
}

static void push_port_call (ExecutorBase& exec, Operation* args, ptrdiff_t receiver_offset, ptrdiff_t port_offset, ptrdiff_t array_offset)
{
  // TODO:  The port knows the size of the arguments.  No need to measure.
  // Push all of the arguments first and measure their size.
  char* top_before = exec.stack ().top ();
  args->execute (exec);
  char* top_after = exec.stack ().top ();
  ptrdiff_t arguments_size = top_after - top_before; // Assumes stack grows up.

  // Find the port to activate.
  port_t* port = *reinterpret_cast<port_t**> (static_cast<char*> (exec.stack ().read_pointer (receiver_offset)) + port_offset + array_offset);

  char* base_pointer = exec.stack ().base_pointer ();

  // Activate all the reactions bound to the port.
  while (port != NULL)
    {
      // Set up a frame.
      // Push the instance.
      exec.stack ().push_pointer (port->instance);
      // Push the parameter.
      if (port->reaction->has_dimension ())
        {
          exec.stack ().push<type::Int::ValueType> (port->parameter);
        }
      // Push the arguments.
      exec.stack ().load (top_before, arguments_size);
      // Push an instruction pointer.
      exec.stack ().push_pointer (NULL);

      // Jump to the last frame.
      exec.stack ().base_pointer (exec.mutable_phase_base_pointer ());

      exec.stack ().setup (port->reaction->memory_model.locals_size ());

      port->reaction->call (exec);

      // Move back to our frame.
      exec.stack ().base_pointer (base_pointer);

      port = port->next;
    }
}

OpReturn
PushPortCall::execute (ExecutorBase& exec) const
{
  push_port_call (exec, args, receiver_offset, port_offset, 0);
  return make_continue ();
}

OpReturn
IndexedPushPortCall::execute (ExecutorBase& exec) const
{
  index->execute (exec);
  Int::ValueType idx;
  exec.stack ().pop (idx);
  push_port_call (exec, args, receiver_offset, port_offset, idx * array_type->UnitSize ());
  return make_continue ();
}

OpReturn
Push::execute (ExecutorBase& exec) const
{
  OpReturn ca = body->execute (exec);
  exec.push ();
  return ca;
}

OpReturn
ForIota::execute (ExecutorBase& exec) const
{
  for (Int::ValueType idx = 0; idx != limit; ++idx)
    {
      Int::ValueType* i = static_cast<Int::ValueType*> (exec.stack ().get_address (offset));
      *i = idx;
      OpReturn r = body->execute (exec);
      if (r == kReturn)
        {
          return r;
        }
    }
  return make_continue ();
}

struct ConvertStringToSliceOfBytes : public Operation
{
  ConvertStringToSliceOfBytes (Operation* c) : child (c) { }
  OpReturn
  execute (ExecutorBase& exec) const
  {
    child->execute (exec);
    StringU::ValueType in;
    exec.stack ().pop (in);
    Slice::ValueType out;
    out.ptr = exec.heap ()->allocate (in.length);
    memcpy (out.ptr, in.ptr, in.length);
    out.length = in.length;
    out.capacity = in.length;
    exec.stack ().push (out);
    return make_continue ();
  }

  virtual void dump () const
  {
    UNIMPLEMENTED;
  }
  Operation* const child;
};

struct ConvertSliceOfBytesToString : public Operation
{
  ConvertSliceOfBytesToString (Operation* c) : child (c) { }
  OpReturn
  execute (ExecutorBase& exec) const
  {
    child->execute (exec);
    Slice::ValueType in;
    exec.stack ().pop (in);
    StringU::ValueType out;
    out.ptr = exec.heap ()->allocate (in.length);
    memcpy (out.ptr, in.ptr, in.length);
    out.length = in.length;
    exec.stack ().push (out);
    return make_continue ();
  }

  virtual void dump () const
  {
    std::cout << "ConvertSliceOfBytesToString(";
    child->dump ();
    std::cout << ")";
  }
  Operation* const child;
};

template<typename FromType, typename ToType>
struct Conversion : public Operation
{
  Conversion (Operation* c) : child (c) { }
  virtual OpReturn execute (ExecutorBase& exec) const
  {
    child->execute (exec);
    FromType x;
    exec.stack ().pop (x);
    ToType y = x;
    exec.stack ().push (y);
    return kContinue;
  }
  virtual void dump () const
  {
    std::cout << "Conversion(";
    child->dump ();
    std::cout << ")";
  }
  Operation* const child;
};


template <typename T1>
static Operation* make_conversion1 (Operation* c, const type::Type* to)
{
  switch (to->underlying_kind ())
    {
    case kUint8:
      return new Conversion<T1, Uint8::ValueType> (c);
    case kUint16:
      return new Conversion<T1, Uint16::ValueType> (c);
    case kUint32:
      return new Conversion<T1, Uint32::ValueType> (c);
    case kUint64:
      return new Conversion<T1, Uint64::ValueType> (c);
    case kInt8:
      return new Conversion<T1, Int8::ValueType> (c);
    case kInt16:
      return new Conversion<T1, Int16::ValueType> (c);
    case kInt32:
      return new Conversion<T1, Int32::ValueType> (c);
    case kInt64:
      return new Conversion<T1, Int64::ValueType> (c);
    case kFloat32:
      return new Conversion<T1, Float32::ValueType> (c);
    case kFloat64:
      return new Conversion<T1, Float64::ValueType> (c);
    case kUint:
      return new Conversion<T1, Uint::ValueType> (c);
    case kInt:
      return new Conversion<T1, Int::ValueType> (c);
    default:
      break;
    }
  NOT_REACHED;
}

Operation* make_conversion (Operation* c, const type::Type* from, const type::Type* to)
{
  if (are_identical (from->UnderlyingType (), to->UnderlyingType ()))
    {
      return c;
    }
  else if (from->to_pointer () &&
           to->to_pointer () &&
           are_identical (from->to_pointer (), to->to_pointer ()))
    {
      UNIMPLEMENTED;
    }
  else if ((is_typed_integer (from) || is_typed_float (from)) &&
           (is_typed_integer (to) || is_typed_float (to)))
    {
      switch (from->underlying_kind ())
        {
        case kUint8:
          return make_conversion1<Uint8::ValueType> (c, to);
        case kUint16:
          return make_conversion1<Uint16::ValueType> (c, to);
        case kUint32:
          return make_conversion1<Uint32::ValueType> (c, to);
        case kUint64:
          return make_conversion1<Uint64::ValueType> (c, to);
        case kInt8:
          return make_conversion1<Int8::ValueType> (c, to);
        case kInt16:
          return make_conversion1<Int16::ValueType> (c, to);
        case kInt32:
          return make_conversion1<Int32::ValueType> (c, to);
        case kInt64:
          return make_conversion1<Int64::ValueType> (c, to);
        case kFloat32:
          return make_conversion1<Float32::ValueType> (c, to);
        case kFloat64:
          return make_conversion1<Float64::ValueType> (c, to);
        case kUint:
          return make_conversion1<Uint::ValueType> (c, to);
        case kInt:
          return make_conversion1<Int::ValueType> (c, to);
        default:
          break;
        }
      NOT_REACHED;
    }
  else if (is_typed_complex (from) && is_typed_complex (to))
    {
      UNIMPLEMENTED;
    }
  else if (is_typed_integer (from) && is_typed_string (to))
    {
      UNIMPLEMENTED;
    }
  else if (is_slice_of_bytes (from) && is_typed_string (to))
    {
      return new ConvertSliceOfBytesToString (c);
    }
  else if (is_slice_of_runes (from) && is_typed_string (to))
    {
      UNIMPLEMENTED;
    }
  else if (is_typed_string (from) && is_slice_of_bytes (to))
    {
      return new ConvertStringToSliceOfBytes (c);
    }
  else if (is_typed_string (from) && is_slice_of_runes (to))
    {
      UNIMPLEMENTED;
    }
  else
    {
      // Assignable.
      UNIMPLEMENTED;
    }
}

OpReturn
Popn::execute (ExecutorBase& exec) const
{
  OpReturn r = child->execute (exec);
  exec.stack ().popn (size);
  return r;
}

OpReturn PrintlnOp::execute (ExecutorBase& exec) const
{
  ListOperation* lop = static_cast<ListOperation*> (args);

  exec.lock_stdout ();
  for (size_t idx = 0; idx != lop->list.size (); ++idx)
    {
      lop->list[idx]->execute (exec);

      switch (evals[idx].type->underlying_kind ())
        {
        case kBool:
        {
          Bool::ValueType x;
          exec.stack ().pop (x);
          if (x)
            {
              printf ("true");
            }
          else
            {
              printf ("false");
            }
        }
        break;

        case kUint8:
        {
          Uint8::ValueType x;
          exec.stack ().pop (x);
          printf ("%u", x);
        }
        break;

        case kUint16:
        {
          Uint16::ValueType x;
          exec.stack ().pop (x);
          printf ("%u", x);
        }
        break;

        case kUint32:
        {
          Uint32::ValueType x;
          exec.stack ().pop (x);
          printf ("%u", x);
        }
        break;

        case kUint64:
        {
          Uint64::ValueType x;
          exec.stack ().pop (x);
          printf ("%lu", x);
        }
        break;

        case kInt8:
        {
          Int8::ValueType x;
          exec.stack ().pop (x);
          printf ("%d", x);
        }
        break;

        case kInt16:
        {
          Int16::ValueType x;
          exec.stack ().pop (x);
          printf ("%d", x);
        }
        break;

        case kInt32:
        {
          Int32::ValueType x;
          exec.stack ().pop (x);
          printf ("%d", x);
        }
        break;

        case kInt64:
        {
          Int64::ValueType x;
          exec.stack ().pop (x);
          printf ("%ld", x);
        }
        break;

        case kFloat32:
        {
          Float32::ValueType x;
          exec.stack ().pop (x);
          printf ("%g", x);
        }
        break;

        case kFloat64:
        {
          Float64::ValueType x;
          exec.stack ().pop (x);
          printf ("%g", x);
        }
        break;

        case kComplex64:
        case kComplex128:
          TYPE_NOT_REACHED (*evals[idx].type);

        case kUint:
        {
          Uint::ValueType x;
          exec.stack ().pop (x);
          printf ("%lu", x);
        }
        break;

        case kInt:
        {
          Int::ValueType x;
          exec.stack ().pop (x);
          printf ("%ld", x);
        }
        break;

        case kUintptr:
        {
          Uintptr::ValueType x;
          exec.stack ().pop (x);
          printf ("%lu", x);
        }
        break;

        case kStringU:
        {
          StringU::ValueType x;
          exec.stack ().pop (x);
          fwrite (x.ptr, 1, x.length, stdout);
        }
        break;

        case kPointer:
        {
          Pointer::ValueType x;
          exec.stack ().pop (x);
          printf ("%p", x);
        }
        break;

        default:
          TYPE_NOT_REACHED (*evals[idx].type);
        }

    }
  printf ("\n");
  exec.unlock_stdout ();

  return kContinue;
}

OpReturn NewOp::execute (ExecutorBase& exec) const
{
  // Allocate a new instance of the type.
  const type::Heap* heap_type = type_cast<type::Heap> (type);
  if (heap_type == NULL)
    {
      exec.stack ().push (exec.heap ()->allocate (type->Size ()));
    }
  else
    {
      const type::Type* t = heap_type->base_type;
      // Allocate a new heap and root object.
      Heap* h = new Heap (t->Size ());
      // Insert it into its parent.
      Heap* h2 = exec.heap ();
      h2->insert_child (h);
      // Allocate a new heap link in the parent.
      heap_link_t* hl = make_heap_link (h, h2);
      exec.stack ().push (hl);
    }

  return kContinue;
}

OpReturn MoveOp::execute (ExecutorBase& exec) const
{
  arg->execute (exec);

  heap_link_t* hl;
  exec.stack ().pop (hl);
  if (hl != NULL)
    {
      pthread_mutex_lock (&hl->mutex);
      if (hl->heap != NULL && hl->change_count == 0)
        {
          // Break the link.
          Heap* h = hl->heap;
          hl->heap = NULL;
          pthread_mutex_unlock (&hl->mutex);

          // Remove from parent.
          h->remove_from_parent ();
          // Insert into the new parent.
          Heap* h2 = exec.heap ();
          h2->insert_child (h);

          // Allocate a new heap link in the parent.
          heap_link_t* new_hl = make_heap_link (h, h2);

          // Return the heap link.
          exec.stack ().push (new_hl);
        }
      else
        {
          pthread_mutex_unlock (&hl->mutex);
          exec.stack ().push (NULL);
        }
    }
  else
    {
      exec.stack ().push (NULL);
    }

  return kContinue;
}


OpReturn MergeOp::execute (ExecutorBase& exec) const
{
  arg->execute (exec);

  heap_link_t* hl;
  exec.stack ().pop (hl);
  if (hl != NULL)
    {
      pthread_mutex_lock (&hl->mutex);
      if (hl->heap != NULL && hl->change_count == 0)
        {
          // Break the link.
          Heap* h = hl->heap;
          hl->heap = NULL;
          pthread_mutex_unlock (&hl->mutex);

          // Get the heap root.
          char* root = static_cast<char*> (h->root ());

          // Remove from parent.
          h->remove_from_parent ();

          // Merge into the new parent.
          exec.heap ()->merge (h);

          // Return the root.
          exec.stack ().push (root);
        }
      else
        {
          pthread_mutex_unlock (&hl->mutex);
          exec.stack ().push (NULL);
        }
    }
  else
    {
      exec.stack ().push (NULL);
    }

  return kContinue;
}

OpReturn LenOp::execute (ExecutorBase& exec) const
{
  arg->execute (exec);
  Slice::ValueType slice;
  exec.stack ().pop (slice);
  Int::ValueType retval = slice.length;
  exec.stack ().push (retval);
  return make_continue ();
}

template <typename T>
struct AppendOp : public Operation
{
  AppendOp (const type::Slice* a_slice_type,
            Operation* a_arg)
    : slice_type (a_slice_type)
    , arg (a_arg)
  { }

  virtual OpReturn execute (ExecutorBase& exec) const
  {
    arg->execute (exec);

    // Pop the element.
    T element;
    exec.stack ().pop (element);

    // Pop the slice.
    Slice::ValueType slice;
    exec.stack ().pop (slice);

    const Uint::ValueType new_length = slice.length + 1;
    if (new_length > slice.capacity)
      {
        const Uint::ValueType new_capacity = 2 * new_length;
        void* ptr = exec.heap ()->allocate (new_capacity * slice_type->UnitSize ());
        memcpy (ptr, slice.ptr, slice.length * slice_type->UnitSize ());
        slice.ptr = ptr;
        slice.capacity = new_capacity;
      }
    memcpy (static_cast<char*> (slice.ptr) + slice.length * slice_type->UnitSize (), &element, slice_type->UnitSize ());
    slice.length = new_length;
    exec.stack ().push (slice);
    return make_continue ();
  }

  virtual void dump () const
  {
    UNIMPLEMENTED;
  }
  const type::Slice* const slice_type;
  Operation* const arg;
};

Operation* make_append (const type::Slice* slice_type, Operation* args)
{
  switch (slice_type->base_type->underlying_kind ())
    {
    case kUint8:
      return new AppendOp<Uint8::ValueType> (slice_type, args);
    default:
      TYPE_NOT_REACHED (*slice_type->base_type);
    }
}

OpReturn CopyOp::execute (ExecutorBase& exec) const
{
  arg->execute (exec);

  const Slice* slice_type = type_strip_cast<Slice>(type);
  if (slice_type != NULL)
    {
      Slice::ValueType in;
      exec.stack ().pop (in);
      Slice::ValueType out;
      size_t sz = slice_type->UnitSize () * in.length;
      out.ptr = exec.heap ()->allocate (sz);
      memcpy (out.ptr, in.ptr, sz);
      out.length = in.length;
      out.capacity = in.length;
      exec.stack ().push (out);
      return make_continue ();
    }

  const StringU* string_type = type_strip_cast<StringU>(type);
  if (string_type != NULL)
    {
      StringU::ValueType in;
      exec.stack ().pop (in);
      StringU::ValueType out;
      out.ptr = exec.heap ()->allocate (in.length);
      memcpy (out.ptr, in.ptr, in.length);
      out.length = in.length;
      exec.stack ().push (out);
      return make_continue ();
    }

  NOT_REACHED;
}
}

// void
// dump_instances (const runtime_t* runtime)
// {
//   for (Composer::Instancestype::const_iterator pos = runtime->instance_table.instances.begin (),
//          limit = runtime->instance_table.instances.end ();
//        pos != limit;
//        ++pos)
//     {
//       Instance* instance = pos->second;
//       if (instance->is_top_level ())
//         {
//           UNIMPLEMENTED;
//           //void* ptr = instance_record_get_ptr (instance_get_record (instance));
//           //type_print_value (instance_type (instance), ptr);
//           printf ("\n");
//         }
//     }
// }
