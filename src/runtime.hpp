#ifndef runtime_hpp
#define runtime_hpp

#include "types.hpp"
#include "Ast.hpp"
#include "heap.hpp"
#include "field.hpp"
#include <error.h>
#include <string.h>
#include "Composition.hpp"
#include "executor_base.hpp"
#include "Template.hpp"

namespace runtime
{
  void
  allocate_instances (Composition::Composer& instance_table);

  void
  create_bindings (Composition::Composer& instance_table);

  void
  initialize (executor_base_t& exec, Composition::Instance* instance);

// Returns true if the action is enabled.
  bool enabled (executor_base_t& exec, component_t* instance, const action_t* action, size_t iota);

// Returns true if the action was executed.
  bool exec (executor_base_t& exec, component_t* instance, const action_t* action, size_t iota);

// Execute the action without checking the precondition.
// Returns true.
  bool exec_no_check (executor_base_t& exec, component_t* instance, const action_t* action, size_t iota);

  enum ControlAction
  {
    RETURN,
    CONTINUE,
  };

  ControlAction
  evaluate_statement (executor_base_t& exec,
                      const MemoryModel& memoryModel,
                      Ast::Node* node);

  void
  evaluate_expr (executor_base_t& exec,
                 const MemoryModel& memoryModel,
                 const Ast::Node* node);

  struct New : public Template
  {
    New (Ast::Node* dn);
    virtual typed_value_t instantiate (TypedValueListType& tvlist);
  };

  struct Move : public Template
  {
    Move (Ast::Node* dn);
    virtual typed_value_t instantiate (TypedValueListType& tvlist);
  };

  struct Merge : public Template
  {
    Merge (Ast::Node* dn);
    virtual typed_value_t instantiate (TypedValueListType& tvlist);
  };

  struct Copy : public Template
  {
    Copy (Ast::Node* dn);
    virtual typed_value_t instantiate (TypedValueListType& tvlist);
  };

  struct Println : public Template
  {
    Println (Ast::Node* dn);
    virtual typed_value_t instantiate (TypedValueListType& tvlist);
  };

}

#endif /* runtime_hpp */
