#include "Ast.hpp"
#include "Symbol.hpp"
#include <error.h>
#include "semantic.hpp"
#include "action.hpp"
#include "field.hpp"
#include "parameter.hpp"
#include "Callable.hpp"
#include "AstVisitor.hpp"

using namespace Type;
using namespace Ast;

typed_value_t
ImplicitlyConvert (Ast::Node*& expr, const Type::Type* target)
{
  typed_value_t tv = expr->typed_value;
  if (!Type::Identitical (target, tv.type) && tv.AssignableTo (target))
    {
      tv = tv.Convert (expr->location, target);
      expr = new ast_implicit_conversion_expr_t (expr->location.Line, expr);
      expr->typed_value = tv;
    }
  return tv;
}

typed_value_t
ImplicitlyConvertToDefault (Ast::Node*& expr)
{
  typed_value_t tv = expr->typed_value;
  const Type::Type* target = tv.type->DefaultType ();
  if (!Type::Identitical (target, tv.type))
    {
      if (!tv.AssignableTo (target))
        {
          error_at_line (-1, 0, expr->location.File.c_str (), expr->location.Line,
                         "cannot convert to real type (E68)");
        }
      tv = tv.Convert (expr->location, target);
      expr = new ast_implicit_conversion_expr_t (expr->location.Line, expr);
      expr->typed_value = tv;
    }
  return tv;
}

void
check_assignment (typed_value_t left_tv,
                  typed_value_t right_tv,
                  const Node& node,
                  const char* conversion_message,
                  const char* leak_message)
{
  assert (left_tv.type != NULL);
  assert (right_tv.type != NULL);

  if (left_tv.kind != typed_value_t::REFERENCE)
    {
      error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                     "target of assignment is not an lvalue (E48)");
    }

  if (left_tv.intrinsic_mutability != MUTABLE)
    {
      error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                     "target of assignment is not mutable (E13)");
    }

  if (right_tv.kind != typed_value_t::VALUE)
    {
      error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                     "source of assignment is not an rvalue (E132)");
    }

  if (!(
        Identitical (left_tv.type, right_tv.type) ||
        (Type::type_cast<Type::Pointer> (type_strip(left_tv.type)) && right_tv.type == Type::Nil::Instance ())
      ))
    {
      error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                     conversion_message, left_tv.type->ToString ().c_str (), right_tv.type->ToString ().c_str ());
    }

  if (type_contains_pointer (left_tv.type))
    {
      if (left_tv.dereference_mutability < right_tv.dereference_mutability)
        {
          error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                         "%s", leak_message);
        }
    }
}

static void checkArgs (Node * node, TypedValueListType& tvlist);

static void checkCall (Node& node,
                       const Type::Signature* signature,
                       typed_value_t return_value,
                       Ast::Node* argsnode,
                       const TypedValueListType& args)
{
  size_t argument_count = args.size ();
  size_t parameter_count = signature->Arity ();
  if (argument_count != parameter_count)
    {
      error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                     "call expects %zd arguments but given %zd (E26)",
                     parameter_count, argument_count);
    }

  bool component_state = false;
  size_t idx = 0;
  for (Type::Signature::const_iterator pos = signature->Begin (),
       limit = signature->End ();
       pos != limit;
       ++pos, ++idx)
    {
      typed_value_t parameter_tv = typed_value_t::make_ref ((*pos)->value);
      typed_value_t argument_tv  = ImplicitlyConvert (argsnode->At (idx), parameter_tv.type);
      if (argument_tv.component_state && type_contains_pointer (parameter_tv.type) && parameter_tv.dereference_mutability == MUTABLE)
        {
          component_state = true;
        }
      check_assignment (parameter_tv, argument_tv, *argsnode->At (idx),
                        "incompatible types (%s) = (%s) (E116)",
                        "argument leaks mutable pointers (E117)");
    }

  // Set the return type.
  node.typed_value = return_value;
  node.typed_value.component_state = component_state && type_contains_pointer (return_value.type);
}

static typed_value_t
insertImplicitDereference (Ast::Node*& expr)
{
  typed_value_t tv = expr->typed_value;
  expr = new ast_implicit_dereference_expr_t (expr->location.Line, expr);
  tv = typed_value_t::implicit_dereference (tv);
  expr->typed_value = tv;
  return tv;
}

typed_value_t
CheckAndImplicitlyDereference (Ast::Node*& expr)
{
  typed_value_t tv = type_check_expr (expr);
  if (tv.isReference ())
    {
      // Insert a dereference node.
      tv = insertImplicitDereference (expr);
    }
  return tv;
}

typed_value_t
CheckAndImplicitlyDereferenceAndConvert (Ast::Node*& expr, const Type::Type* type)
{
  CheckAndImplicitlyDereference (expr);
  ImplicitlyConvert (expr, type);
  return expr->typed_value;
}

typed_value_t
CheckAndImplicitlyDereferenceAndConvertToDefault (Ast::Node*& expr)
{
  CheckAndImplicitlyDereference (expr);
  ImplicitlyConvertToDefault (expr);
  return expr->typed_value;
}


static typed_value_t
insertExplicitDereference (Ast::Node*& expr, typed_value_t tv)
{
  expr = new ast_dereference_expr_t (expr->location.Line, expr);
  tv = typed_value_t::dereference (tv);
  expr->typed_value = tv;
  return tv;
}

typed_value_t
CheckExpectReference (Ast::Node* expr)
{
  typed_value_t tv = type_check_expr (expr);
  if (!tv.isReference ())
    {
      error_at_line (EXIT_FAILURE, 0, expr->location.File.c_str (), expr->location.Line, "expected reference (E14)");
    }
  return tv;
}

struct check_visitor : public Ast::Visitor
{
  Ast::Node* ptr;

  check_visitor (Ast::Node* p) : ptr (p) { }

  void default_action (Node& node)
  {
    ast_not_reached(node);
  }

  void visit (TypeExpression& node)
  {
    const Type::Type* type = process_type_spec (node.type_spec (), true);
    node.typed_value = typed_value_t (type);
  }

  void visit (ast_indexed_port_call_expr_t& node)
  {
    const std::string& port_identifier = ast_get_identifier (node.identifier ());
    const Type::Type *this_type = node.GetReceiverType ();
    const Type::Type *type = type_select (this_type, port_identifier);

    if (type == NULL)
      {
        error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                       "no port named %s (E15)", port_identifier.c_str ());
      }

    const Type::Array* array_type = Type::type_cast<Type::Array> (type);
    if (!array_type)
      {
        error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                       "%s is not an array of ports (E16)", port_identifier.c_str ());
      }

    const Type::Function* push_port_type = Type::type_cast<Type::Function> (array_type->Base ());

    if (push_port_type == NULL || push_port_type->kind != Type::Function::PUSH_PORT)
      {
        error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                       "%s is not an array of ports (E17)", port_identifier.c_str ());
      }

    typed_value_t index_tv = CheckAndImplicitlyDereference (node.index_ref ());

    typed_value_t::index (node.index ()->location, typed_value_t::make_ref (array_type, IMMUTABLE, IMMUTABLE, false), index_tv);

    Node *args = node.args ();
    TypedValueListType tvlist;
    checkArgs (args, tvlist);
    checkCall (node, push_port_type->GetSignature (), push_port_type->GetReturnParameter ()->value, args, tvlist);
    node.field = type_select_field (this_type, port_identifier);
    node.array_type = array_type;
  }

  void visit (ast_identifier_expr_t& node)
  {
    Node *identifier_node = node.child ();
    const std::string& identifier = ast_get_identifier (identifier_node);
    Symbol *symbol = node.FindGlobalSymbol (identifier);
    if (symbol == NULL)
      {
        error_at_line (-1, 0, identifier_node->location.File.c_str (),
                       identifier_node->location.Line, "%s is not defined (E18)",
                       identifier.c_str ());
      }

    struct visitor : public ConstSymbolVisitor
    {
      ast_identifier_expr_t& node;

      visitor (ast_identifier_expr_t& n)
        : node (n)
      { }

      void defaultAction (const Symbol& symbol)
      {
        not_reached;
      }

      void visit (const BuiltinFunction& symbol)
      {
        node.typed_value = symbol.value ();
      }

      void visit (const ::Template& symbol)
      {
        node.typed_value = symbol.value ();
      }

      void visit (const ::Function& symbol)
      {
        node.typed_value = symbol.value ();
      }

      void visit (const ParameterSymbol& symbol)
      {
        node.typed_value = symbol.value;
      }

      void visit (const TypeSymbol& symbol)
      {
        node.typed_value = typed_value_t (symbol.type);
      }

      void visit (const TypedConstantSymbol& symbol)
      {
        node.typed_value = symbol.value;
      }

      void visit (const VariableSymbol& symbol)
      {
        node.typed_value = symbol.value;
      }

      void visit (const HiddenSymbol& symbol)
      {
        error_at_line (-1, 0, node.location.File.c_str (),
                       node.location.Line, "%s is not accessible in this scope (E19)",
                       symbol.identifier.c_str ());
      }
    };
    visitor v (node);
    symbol->accept (v);

    node.symbol = symbol;
  }

  void visit (ast_select_expr_t& node)
  {
    const std::string& identifier = ast_get_identifier (node.identifier ());
    typed_value_t in = type_check_expr (node.base ());
    assert (in.type != NULL);

    if (in.isReference () && type_dereference (in.type))
      {
        // Pointer reference.
        // Insert an implicit dereference.
        in = insertImplicitDereference (node.base_ref ());
      }

    if (in.IsValue () && type_dereference (in.type))
      {
        // Pointer value.
        // Insert an explicit dereference.
        in = insertExplicitDereference (node.base_ref (), in);
      }

    if (in.isReference ())
      {
        typed_value_t out = typed_value_t::select (in, identifier);
        if (out.IsError ())
          {
            error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                           "cannot select %s from expression of type %s (E20)",
                           identifier.c_str (), in.type->ToString ().c_str ());
          }
        node.typed_value = out;
      }
    else if (in.IsValue ())
      {
        unimplemented;
      }
  }

  void visit (ast_dereference_expr_t& node)
  {
    typed_value_t in = CheckAndImplicitlyDereference (node.child_ref ());
    typed_value_t out = typed_value_t::dereference (in);
    if (out.IsError ())
      {
        error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                       "incompatible types: %s (E21)", in.type->ToString ().c_str ());
      }
    node.typed_value = out;
  }

  void visit (ast_literal_expr_t& node)
  {
    // Do nothing.
  }

  void check_address_of (ast_address_of_expr_t& node)
  {
    Ast::Node* expr = node.child ();
    typed_value_t in = expr->typed_value;
    typed_value_t out = typed_value_t::address_of (in);
    if (out.IsError ())
      {
        error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                       "incompatible types: %s (E22)", in.type->ToString ().c_str ());
      }
    node.typed_value = out;
    node.address_of_dereference = ast_cast<ast_dereference_expr_t> (expr) != NULL;
  }

  void visit (ast_address_of_expr_t& node)
  {
    typed_value_t in = CheckExpectReference (node.child ());
    typed_value_t out = typed_value_t::address_of (in);
    if (out.IsError ())
      {
        error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                       "E45: incompatible types: %s (E23)", in.type->ToString ().c_str ());
      }
    node.typed_value = out;
  }

  void visit (ast_unary_arithmetic_expr_t& node)
  {
    typed_value_t in = CheckAndImplicitlyDereference (node.child_ref ());
    switch (node.arithmetic)
      {
      case LogicNot:
        node.typed_value = in.LogicNot (node.location);
        return;
      case Negate:
        node.typed_value = in.Negate (node.location);
        return;
      }
    not_reached;
  }

  void visit (ast_binary_arithmetic_expr_t& node)
  {
    typed_value_t left = CheckAndImplicitlyDereference (node.left_ref ());
    typed_value_t right = CheckAndImplicitlyDereference (node.right_ref ());
    switch (node.arithmetic)
      {
      case Multiply:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::Multiply (node.location, left, right);
        return;
      case Divide:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::Divide (node.location, left, right);
        return;
      case Modulus:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::Modulus (node.location, left, right);
        return;
      case LeftShift:
        if (left.value.present && !right.value.present)
          {
            left = ImplicitlyConvertToDefault (node.left_ref ());
          }
        if (right.value.present && !left.value.present)
          {
            right = ImplicitlyConvertToDefault (node.right_ref ());
          }
        node.typed_value = typed_value_t::LeftShift (node.location, left, right);
        return;
      case RightShift:
        if (left.value.present && !right.value.present)
          {
            left = ImplicitlyConvertToDefault (node.left_ref ());
          }
        if (right.value.present && !left.value.present)
          {
            right = ImplicitlyConvertToDefault (node.right_ref ());
          }
        node.typed_value = typed_value_t::RightShift (node.location, left, right);
        return;
      case BitAnd:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::BitAnd (node.location, left, right);
        return;
      case BitAndNot:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::BitAndNot (node.location, left, right);
        return;
      case Add:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::Add (node.location, left, right);
        return;
      case Subtract:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::Subtract (node.location, left, right);
        return;
      case BitOr:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::BitOr (node.location, left, right);
        return;
      case BitXor:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::BitXor (node.location, left, right);
        return;
      case Equal:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::Equal (node.location, left, right);
        return;
      case NotEqual:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::NotEqual (node.location, left, right);
        return;
      case LessThan:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::LessThan (node.location, left, right);
        return;
      case LessEqual:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::LessEqual (node.location, left, right);
        return;
      case MoreThan:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::MoreThan (node.location, left, right);
        return;
      case MoreEqual:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::MoreEqual (node.location, left, right);
        return;
      case LogicOr:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::LogicOr (node.location, left, right);
        return;
      case LogicAnd:
        left = ImplicitlyConvert (node.left_ref (), right.type);
        right = ImplicitlyConvert (node.right_ref (), left.type);
        node.typed_value = typed_value_t::LogicAnd (node.location, left, right);
        return;
      }
    not_reached;
  }

  void visit (ast_call_expr_t& node)
  {
    // Analyze the args.
    TypedValueListType tvlist;
    checkArgs (node.args (), tvlist);

    // Analyze the callee.
    // Expecting a value.
    typed_value_t expr_tv = CheckAndImplicitlyDereference (node.expr_ref ());

    node.original_expr_tv = expr_tv;

    if (expr_tv.kind == typed_value_t::TYPE)
      {
        // Conversion.
        if (tvlist.size () != 1)
          {
            error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                           "conversion requires exactly one argument (E69)");
          }

        node.typed_value = tvlist[0].Convert (node.location, expr_tv.type);
        node.IsCall = false;
        return;
      }

    node.IsCall = true;

    const Type::Template* tt = Type::type_strip_cast <Type::Template> (expr_tv.type);
    if (tt != NULL)
      {
        ::Template* t = expr_tv.value.template_value ();
        expr_tv = t->instantiate (tvlist);
        node.expr ()->typed_value = expr_tv;
      }

    struct visitor : public Type::DefaultVisitor
    {
      check_visitor& rvalue_visitor;
      ast_call_expr_t& node;
      TypedValueListType& tvlist;

      visitor (check_visitor& rv,
               ast_call_expr_t& n,
               TypedValueListType& tvl)
        : rvalue_visitor (rv)
        , node (n)
        , tvlist (tvl)
      { }

      void default_action (const Type::Type& type)
      {
        error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                       "cannot call %s (E27)", type.ToString ().c_str ());
      }

      void visit (const Type::Function& type)
      {
        Node::Context context = node.GetContext ();

        switch (type.kind)
          {
          case Type::Function::FUNCTION:
            // No restrictions on caller.
            checkCall (node, type.GetSignature (), type.GetReturnParameter ()->value, node.args (), tvlist);
            break;

          case Type::Function::PUSH_PORT:
            error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                           "push ports cannot be called (E28)");

            break;

          case Type::Function::PULL_PORT:
            // Must be called from either a getter, an action, or reaction.
            if (!(context == Node::Getter ||
                  context == Node::Action ||
                  context == Node::Reaction))
              {
                error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                               "pull ports may only be called from a getter, an action, or a reaction (E29)");
              }

            checkCall (node, type.GetSignature (), type.GetReturnParameter ()->value, node.args (), tvlist);
            if (node.GetInMutableSection ())
              {
                error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                               "cannot call pull port in mutable section (E30)");
              }
            break;
          }
      }

      void visit (const Type::Method& type)
      {
        Node::Context context = node.GetContext ();

        // Convert to a function call.
        // Move the receiver to the args.
        Ast::Node* receiver = node.expr ()->At (0)->At (0);
        if (type_dereference (type.receiver_type) != NULL)
          {
            // Method expects a pointer.  Insert address of.
            ast_address_of_expr_t* e = new ast_address_of_expr_t (node.location.Line, receiver);
            rvalue_visitor.check_address_of (*e);
            receiver = e;
          }
        else
          {
            insertImplicitDereference (receiver);
          }
        node.args ()->Prepend (receiver);
        tvlist.insert (tvlist.begin (), receiver->typed_value);
        // Reset the expression to a literal.
        typed_value_t method_tv = node.expr ()->typed_value;
        method_tv.type = type.function_type;
        node.expr_ref () = new ast_literal_expr_t (node.location.Line, method_tv);

        checkCall (node, type.function_type->GetSignature (), type.return_parameter->value, node.args (), tvlist);

        switch (type.kind)
          {
          case Type::Method::METHOD:
            // No restrictions on caller.
            break;
          case Type::Method::INITIALIZER:
            // Caller must be an initializer.
            if (context != Node::Initializer)
              {
                error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                               "initializers may only be called from initializers (E31)");
              }
            break;
          case Type::Method::GETTER:
          {
            // Must be called from either a getter, action, reaction, or initializer.
            if (!(context == Node::Getter ||
                  context == Node::Action ||
                  context == Node::Reaction ||
                  context == Node::Initializer))
              {
                error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                               "getters may only be called from a getter, an action, a reaction, or an initializer (E32)");
              }

            if (node.GetInMutableSection ())
              {
                error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                               "cannot call getter in mutable section (E33)");
              }
          }
          break;
          case Type::Method::REACTION:
          {
            unimplemented;
          }
          break;
          }
      }
    };

    visitor v (*this, node, tvlist);
    expr_tv.type->Accept (v);
  }

  void visit (ast_push_port_call_expr_t& node)
  {
    Node *expr = node.identifier ();
    Node *args = node.args ();
    const std::string& port_identifier = ast_get_identifier (expr);
    const Type::Type *this_type = node.GetReceiverType ();
    const Type::Function *push_port_type = Type::type_cast<Type::Function> (type_select (this_type, port_identifier));
    if (push_port_type == NULL || push_port_type->kind != Type::Function::PUSH_PORT)
      {
        error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                       "no port named %s (E34)", port_identifier.c_str ());
      }
    TypedValueListType tvlist;
    checkArgs (args, tvlist);
    checkCall (node, push_port_type->GetSignature (), push_port_type->GetReturnParameter ()->value, args, tvlist);
    node.field = type_select_field (this_type, port_identifier);
  }

  void visit (ast_index_expr_t& node)
  {
    typed_value_t base_tv = CheckExpectReference (node.base_ref ());
    typed_value_t idx_tv = CheckAndImplicitlyDereference (node.index_ref ());
    typed_value_t result = typed_value_t::index (node.location, base_tv, idx_tv);
    if (result.IsError ())
      {
        error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                       "incompatible types (%s)[%s] (E35)",
                       base_tv.type->ToString ().c_str (),
                       idx_tv.type->ToString ().c_str ());
      }
    node.typed_value = result;
  }

  void visit (ast_slice_expr_t& node)
  {
    typed_value_t base_tv = CheckExpectReference (node.base_ref ());
    typed_value_t low_tv = CheckAndImplicitlyDereference (node.low_ref ());
    typed_value_t high_tv = CheckAndImplicitlyDereference (node.high_ref ());
    typed_value_t result = typed_value_t::slice (node.location, base_tv, low_tv, high_tv);
    if (result.IsError ())
      {
        error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                       "incompatible types (%s)[%s : %s] (E36)",
                       base_tv.type->ToString ().c_str (),
                       low_tv.type->ToString ().c_str (),
                       high_tv.type->ToString ().c_str ());
      }
    node.typed_value = result;
  }
};

typed_value_t
type_check_expr (Ast::Node* ptr)
{
  check_visitor check_lvalue_visitor (ptr);
  ptr->Accept (check_lvalue_visitor);
  return ptr->typed_value;
}

static void
checkArgs (Node * node, TypedValueListType& tvlist)
{
  for (Node::Iterator child = node->Begin (), limit = node->End ();
       child != limit;
       ++child)
    {
      tvlist.push_back (CheckAndImplicitlyDereference (*child));
    }
}

static typed_value_t
check_condition (Ast::Node*& condition_node)
{
  typed_value_t tv = CheckAndImplicitlyDereferenceAndConvertToDefault (condition_node);
  if (!type_is_boolean (tv.type))
    {
      error_at_line (-1, 0, condition_node->location.File.c_str (),
                     condition_node->location.Line,
                     "cannot convert (%s) to boolean expression in condition (E37)", tv.type->ToString ().c_str ());
    }
  return tv;
}

static void
type_check_statement (Node * node)
{
  struct visitor : public Ast::Visitor
  {
    void default_action (Node& node)
    {
      ast_not_reached (node);
    }

    void visit (ast_const_t& node)
    {
      ProcessDeclarations (&node);
    }

    void visit (ast_empty_statement_t& node)
    { }

    typed_value_t bind (Node& node, Ast::Node* port_node, Ast::Node*& reaction_node)
    {
      CheckExpectReference (port_node);
      CheckAndImplicitlyDereference (reaction_node);

      typed_value_t port_tv = port_node->typed_value;
      typed_value_t reaction_tv = reaction_node->typed_value;

      const Type::Function *push_port_type = Type::type_cast<Type::Function> (port_tv.type);

      if (push_port_type == NULL || push_port_type->kind != Type::Function::PUSH_PORT)
        {
          error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                         "source of bind is not a port (E38)");
        }

      const Type::Method* reaction_type = Type::type_cast<Type::Method> (reaction_tv.type);
      if (reaction_type == NULL || reaction_type->kind != Type::Method::REACTION)
        {
          error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                         "target of bind is not a reaction (E39)");
        }

      if (!type_is_equal (push_port_type->GetSignature (), reaction_type->signature))
        {
          error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                         "cannot bind %s to %s (E40)", push_port_type->ToString ().c_str (), reaction_type->ToString ().c_str ());
        }

      return reaction_tv;
    }

    void visit (ast_bind_push_port_statement_t& node)
    {
      bind (node, node.left (), node.right_ref ());
    }

    void visit (ast_bind_push_port_param_statement_t& node)
    {
      typed_value_t reaction_tv = bind (node, node.left (), node.right_ref ());
      typed_value_t param_tv = CheckAndImplicitlyDereference (node.param_ref ());
      assert (reaction_tv.value.present);
      reaction_t* reaction = reaction_tv.value.reaction_value ();
      if (!reaction->has_dimension ())
        {
          error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                         "parameter specified for non-parameterized reaction (E41)");
        }
      Type::Uint::ValueType dimension = reaction->dimension ();
      typed_value_t::index (node.location, typed_value_t::make_ref (reaction->reaction_type->GetArray (dimension), IMMUTABLE, IMMUTABLE, false), param_tv);
    }

    void visit (ast_bind_pull_port_statement_t& node)
    {
      typed_value_t pull_port_tv = CheckExpectReference (node.left ());
      typed_value_t getter_tv = CheckAndImplicitlyDereference (node.right_ref ());

      const Type::Function* pull_port_type = type_cast<Type::Function> (pull_port_tv.type);

      if (pull_port_type == NULL || pull_port_type->kind != Type::Function::PULL_PORT)
        {
          error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                         "target of bind is not a pull port (E42)");
        }

      const Type::Method* getter_type = type_cast<Type::Method> (getter_tv.type);

      if (getter_type == NULL)
        {
          error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                         "source of bind is not a getter (E43)");
        }

      Type::Function g (Type::Function::FUNCTION, getter_type->signature, getter_type->return_parameter);
      if (!type_is_equal (pull_port_type, &g))
        {
          error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                         "cannot bind %s to %s (E44)", pull_port_type->ToString ().c_str (), getter_type->ToString ().c_str ());
        }
    }

    void visit (ast_for_iota_statement_t& node)
    {
      const std::string& identifier = ast_get_identifier (node.identifier ());
      typed_value_t limit = process_array_dimension (node.limit_node_ref ());
      typed_value_t zero = limit;
      zero.zero ();
      Symbol* symbol = new VariableSymbol (identifier, node.identifier (), typed_value_t::make_ref (typed_value_t::make_range (zero, limit, IMMUTABLE, IMMUTABLE)));
      node.symbol = enter_symbol (node, symbol);
      type_check_statement (node.body ());
      node.limit = limit;
    }

    static typed_value_t
    check_assignment_target (Ast::Node* left)
    {
      typed_value_t tv = CheckExpectReference (left);
      if (tv.intrinsic_mutability != MUTABLE)
        {
          error_at_line (-1, 0, left->location.File.c_str (), left->location.Line,
                         "cannot assign to read-only location of type %s (E45)", tv.type->ToString ().c_str ());
        }

      return tv;
    }

    static void arithmetic_assign (ast_binary_t* node, const char* symbol)
    {
      typed_value_t left_tv = check_assignment_target (node->left ());
      typed_value_t right_tv = CheckAndImplicitlyDereference (node->right_ref ());
      if (!type_is_equal (left_tv.type, right_tv.type))
        {
          error_at_line (-1, 0, node->location.File.c_str (), node->location.Line,
                         "incompatible types (%s) %s (%s) (E46)", left_tv.type->ToString ().c_str (), symbol, right_tv.type->ToString ().c_str ());
        }

      struct visitor : public DefaultVisitor
      {
        Ast::Node* node;
        const char* symbol;

        visitor (Ast::Node* n, const char* s) : node (n), symbol (s) { }

        void visit (const NamedType& type)
        {
          type.UnderlyingType ()->Accept (*this);
        }

        void visit (const Int& type)
        {
          // Okay.
        }

        void visit (const Uint& type)
        {
          // Okay.
        }

        void default_action (const Type::Type& type)
        {
          error_at_line (-1, 0, node->location.File.c_str (), node->location.Line,
                         "incompatible types (%s) %s (%s) (E47)", type.ToString ().c_str (), symbol, type.ToString ().c_str ());
        }
      };
      visitor v (node, symbol);
      left_tv.type->Accept (v);
    }

    void visit (ast_assign_statement_t& node)
    {
      typed_value_t left_tv = check_assignment_target (node.left ());
      typed_value_t right_tv = CheckAndImplicitlyDereferenceAndConvert (node.right_ref (), left_tv.type);
      check_assignment (left_tv, right_tv, node,
                        "incompatible types (%s) = (%s) (E122)",
                        "assignment leaks mutable pointers (E123)");
    }

    void visit (ast_change_statement_t& node)
    {
      // Process the expression.
      typed_value_t tv = CheckAndImplicitlyDereference (node.expr_ref ());
      tv = typed_value_t::change (node.location, tv);

      // Enter the new heap root.
      const std::string& identifier = ast_get_identifier (node.identifier ());
      Symbol* symbol = new VariableSymbol (identifier, &node, typed_value_t::make_ref (tv));
      node.root_symbol = enter_symbol (node, symbol);

      // Enter all parameters and variables in scope that are pointers as pointers to foreign.
      node.Change ();

      // Check the body.
      type_check_statement (node.body ());
    }

    void visit (ast_expression_statement_t& node)
    {
      CheckAndImplicitlyDereference (node.child_ref ());
    }

    void visit (ast_if_statement_t& node)
    {
      check_condition (node.condition_ref ());
      type_check_statement (node.true_branch ());
      type_check_statement (node.false_branch ());
    }

    void visit (ast_while_statement_t& node)
    {
      check_condition (node.condition_ref ());
      type_check_statement (node.body ());
    }

    void visit (ast_add_assign_statement_t& node)
    {
      arithmetic_assign (&node, "+=");
    }

    void visit (ast_subtract_assign_statement_t& node)
    {
      arithmetic_assign (&node, "-=");
    }

    void visit (ast_list_statement_t& node)
    {
      for (Node::ConstIterator pos = node.Begin (), limit = node.End ();
           pos != limit;
           ++pos)
        {
          type_check_statement (*pos);
        }
    }

    void visit (ast_return_statement_t& node)
    {
      // Get the return symbol.
      node.return_symbol = SymbolCast<ParameterSymbol> (node.FindGlobalSymbol (ReturnSymbol));
      assert (node.return_symbol != NULL);

      // Check the expression.
      typed_value_t expr_tv = CheckAndImplicitlyDereferenceAndConvert (node.child_ref (), node.return_symbol->value.type);

      // Check that it matches with the return type.
      check_assignment (node.return_symbol->value, expr_tv, node,
                        "cannot convert to (%s) from (%s) in return (E124)",
                        "return leaks mutable pointers (E125)");
    }

    void visit (ast_increment_statement_t& node)
    {
      Ast::Node* expr = node.child ();
      check_assignment_target (expr);
      struct visitor : public DefaultVisitor
      {
        Node& node;

        visitor (Node& n) : node (n) { }

        void default_action (const Type::Type& type)
        {
          error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                         "cannot increment location of type %s (E50)", type.ToString ().c_str ());
        }

        void visit (const NamedType& type)
        {
          type.UnderlyingType ()->Accept (*this);
        }

        void visit (const Int& type)
        {
          // Okay.
        }

        void visit (const Uint& type)
        {
          // Okay.
        }
      };
      visitor v (node);
      expr->typed_value.type->Accept (v);
    }

    void visit (ast_decrement_statement_t& node)
    {
      unimplemented;
    }

    void visit (ast_activate_statement_t& node)
    {
      Node *expression_list_node = node.expr_list ();
      Node *body_node = node.body ();

      /* Check the activations. */
      TypedValueListType tvlist;
      checkArgs (expression_list_node, tvlist);

      /* Re-insert this as a pointer to mutable. */
      node.Activate ();

      /* Check the body. */
      type_check_statement (body_node);
      node.mutable_phase_access = ComputeReceiverAccess (body_node);
    }

    void visit (ast_var_statement_t& node)
    {
      Ast::Node* identifier_list = node.identifier_list ();
      Ast::Node* type_spec = node.type_spec ();
      Ast::Node* expression_list = node.expression_list ();

      if (expression_list->Size () != 0 &&
          identifier_list->Size () != expression_list->Size ())
        {
          error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                         "wrong number of initializers (E51)");
        }

      // Process the type spec.
      const Type::Type* type = process_type_spec (type_spec, true);

      if (expression_list->Size () == 0)
        {
          // Type, no expressions.

          if (type_cast<Void> (type) != NULL)
            {
              error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                             "missing type (E52)");

            }

          // Enter each symbol.
          typed_value_t left_tv = typed_value_t::make_ref (type, node.mutability, node.dereferenceMutability, false);
          for (Node::Iterator id_pos = identifier_list->Begin (),
               id_limit = identifier_list->End ();
               id_pos != id_limit;
               ++id_pos)
            {
              const std::string& name = ast_get_identifier (*id_pos);
              Symbol* symbol = new VariableSymbol (name, *id_pos, left_tv);
              node.symbols.push_back (enter_symbol (*node.GetParent (), symbol));
            }

          return;
        }

      if (type_cast<Void> (type) == NULL)
        {
          // Type, expressions.

          // Enter each symbol.
          for (Node::Iterator id_pos = identifier_list->Begin (),
               id_limit = identifier_list->End (),
               init_pos = expression_list->Begin ();
               id_pos != id_limit;
               ++id_pos, ++init_pos)
            {
              // Assume left is mutable.
              typed_value_t left_tv = typed_value_t::make_ref (type, MUTABLE, node.dereferenceMutability, false);
              typed_value_t right_tv = CheckAndImplicitlyDereferenceAndConvert (*init_pos, left_tv.type);
              check_assignment (left_tv, right_tv, node,
                                "incompatible types (%s) = (%s) (E126)",
                                "assignment leaks mutable pointers (E127)");
              // Convert to specified mutability.
              left_tv.intrinsic_mutability = node.mutability;
              const std::string& name = ast_get_identifier (*id_pos);
              Symbol* symbol = new VariableSymbol (name, *id_pos, left_tv);
              node.symbols.push_back (enter_symbol (*node.GetParent (), symbol));
            }

          return;
        }

      // No type, expressions.

      // Enter each symbol.
      for (Node::Iterator id_pos = identifier_list->Begin (),
           id_limit = identifier_list->End (),
           init_pos = expression_list->Begin ();
           id_pos != id_limit;
           ++id_pos, ++init_pos)
        {
          // Process the initializer.
          typed_value_t right_tv = CheckAndImplicitlyDereferenceAndConvertToDefault (*init_pos);
          typed_value_t left_tv = typed_value_t::make_ref (right_tv);
          left_tv.intrinsic_mutability = MUTABLE;
          left_tv.dereference_mutability = node.dereferenceMutability;
          check_assignment (left_tv, right_tv, node,
                            "incompatible types (%s) = (%s) (E128)",
                            "assignment leaks mutable pointers (E129)");
          // Convert to specified mutability.
          left_tv.intrinsic_mutability = node.mutability;
          const std::string& name = ast_get_identifier (*id_pos);
          Symbol* symbol = new VariableSymbol (name, *id_pos, left_tv);
          node.symbols.push_back (enter_symbol (*node.GetParent (), symbol));
        }
    }
  };

  visitor v;
  node->Accept (v);
}

static void
control_check_statement (Node * node)
{
  struct visitor : public Ast::Visitor
  {
    bool in_activation_statement;

    visitor () : in_activation_statement (false) { }

    void visit (ast_change_statement_t& node)
    {
      node.body ()->Accept (*this);
    }

    void visit (ast_if_statement_t& node)
    {
      node.true_branch ()->Accept (*this);
      node.false_branch ()->Accept (*this);
    }

    void visit (ast_while_statement_t& node)
    {
      node.body ()->Accept (*this);
    }

    void visit (ast_list_statement_t& node)
    {
      for (Node::ConstIterator pos = node.Begin (), limit = node.End ();
           pos != limit;
           ++pos)
        {
          (*pos)->Accept (*this);
        }
    }

    void visit (ast_return_statement_t& node)
    {
      // TODO: Maybe.
    }

    void visit (ast_activate_statement_t& node)
    {
      Node::Context context = node.GetContext ();
      Node *body_node = node.body ();

      if (!(context == Node::Action ||
            context == Node::Reaction))
        {
          error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                         "activation outside of action or reaction (E53)");
        }

      if (in_activation_statement)
        {
          error_at_line (-1, 0, node.location.File.c_str (), node.location.Line,
                         "activations within activations are not allowed (E54)");
        }

      in_activation_statement = true;
      body_node->Accept (*this);
      in_activation_statement = false;
    }
  };

  visitor v;
  node->Accept (v);
}

// TODO: Replace node with its symbol table.
void
enter_signature (Node& node, const Signature * type)
{
  for (Signature::ParametersType::const_iterator pos = type->Begin (), limit = type->End ();
       pos != limit; ++pos)
    {
      const parameter_t* parameter = *pos;
      // Check if the symbol is defined locally.
      const std::string& identifier = parameter->name;
      Symbol *s = node.FindLocalSymbol (identifier);
      if (s == NULL)
        {
          if (parameter->is_receiver)
            {
              s = ParameterSymbol::makeReceiver (parameter);
            }
          else
            {
              s = ParameterSymbol::make (parameter);
            }
          node.EnterSymbol (s);
        }
      else
        {
          error_at_line (-1, 0, parameter->defining_node->location.File.c_str (), parameter->defining_node->location.Line,
                         "%s is already defined in this scope (E55)",
                         identifier.c_str ());
        }
    }
}

/* Check the semantics of all executable code. */
void
process_definitions (Node * node)
{
  struct visitor : public Ast::Visitor
  {
    void default_action (Node& node)
    {
      ast_not_reached (node);
    }

    void visit (Ast::Type& node)
    { }

    void visit (ast_const_t& node)
    { }

    void visit (ast_action_t& node)
    {
      typed_value_t tv = check_condition (node.precondition_ref ());
      node.action->precondition = node.precondition ();
      Node *body_node = node.body ();
      type_check_statement (body_node);
      control_check_statement (body_node);
      node.action->precondition_access = ComputeReceiverAccess (node.precondition ());
      node.action->immutable_phase_access = ComputeReceiverAccess (node.body ());

      if (tv.value.present)
        {
          if (tv.value.ref (*Bool::Instance ()))
            {
              node.action->precondition_kind = action_t::STATIC_TRUE;
            }
          else
            {
              node.action->precondition_kind = action_t::STATIC_FALSE;
            }
        }
    }

    void visit (ast_dimensioned_action_t& node)
    {
      typed_value_t tv = check_condition (node.precondition_ref ());
      node.action->precondition = node.precondition ();
      Node *body_node = node.body ();
      type_check_statement (body_node);
      control_check_statement (body_node);
      node.action->precondition_access = ComputeReceiverAccess (node.precondition ());
      node.action->immutable_phase_access = ComputeReceiverAccess (node.body ());

      if (tv.value.present)
        {
          if (tv.value.ref (*Bool::Instance ()))
            {
              node.action->precondition_kind = action_t::STATIC_TRUE;
            }
          else
            {
              node.action->precondition_kind = action_t::STATIC_FALSE;
            }
        }
    }

    void visit (ast_bind_t& node)
    {
      Node *body_node = node.body ();
      type_check_statement (body_node);
      control_check_statement (body_node);
    }

    void visit (ast_function_t& node)
    {
      Node *body_node = node.body ();
      type_check_statement (body_node);
      control_check_statement (body_node);
    }

    void visit (ast_method_t& node)
    {
      Node *body_node = node.body ();
      type_check_statement (body_node);
      control_check_statement (body_node);
    }

    void visit (ast_initializer_t& node)
    {
      Node *body_node = node.body ();
      type_check_statement (body_node);
      control_check_statement (body_node);
    }

    void visit (ast_getter_t& node)
    {
      Node *body_node = node.body ();
      type_check_statement (body_node);
      control_check_statement (body_node);
      node.getter->immutable_phase_access = ComputeReceiverAccess (body_node);
    }

    void visit (ast_instance_t& node)
    {
      // Lookup the initialization function.
      InstanceSymbol* symbol = node.symbol;
      const NamedType* type = symbol->type;
      Ast::Node* initializer_node = node.initializer ();
      Initializer* initializer = type->GetInitializer (ast_get_identifier (initializer_node));
      if (initializer == NULL)
        {
          error_at_line (-1, 0, initializer_node->location.File.c_str (),
                         initializer_node->location.Line,
                         "no initializer named %s (E56)",
                         ast_get_identifier (initializer_node).c_str ());
        }

      // Check the call.
      TypedValueListType tvlist;
      checkArgs (node.expression_list (), tvlist);
      checkCall (node, initializer->initializerType->signature, initializer->initializerType->return_parameter->value, node.expression_list (), tvlist);
      symbol->initializer = initializer;
    }

    void visit (ast_reaction_t& node)
    {
      Node *body_node = node.body ();
      type_check_statement (body_node);
      control_check_statement (body_node);
      node.reaction->immutable_phase_access = ComputeReceiverAccess (body_node);
    }

    void visit (ast_dimensioned_reaction_t& node)
    {
      Node *body_node = node.body ();
      type_check_statement (body_node);
      control_check_statement (body_node);
      node.reaction->immutable_phase_access = ComputeReceiverAccess (body_node);
    }

    void visit (SourceFile& node)
    {
      node.VisitChildren (*this);
    }
  };

  visitor v;
  node->Accept (v);
}
