#include "ast.hpp"
#include "symbol.hpp"
#include <error.h>

void
enter_symbols (ast_t * node)
{
  /* Construct the top-level table. */
  symtab_t *symtab = symtab_get_root (node->symtab);

  /* Insert types. */
  {
    symtab->enter (symbol_make_type ("bool", new named_type_t ("bool", bool_type_t::instance ()), node));

    symtab->enter (symbol_make_type ("int", new named_type_t ("int", int_type_t::instance ()), node));

    symtab->enter (symbol_make_type ("uint", new named_type_t ("uint", uint_type_t::instance ()), node));

    symtab->enter (symbol_make_type ("string", new named_type_t ("string", string_type_t::instance ()), node));
  }

  /* Insert zero constant. */
  symtab->enter (symbol_make_typed_constant ("nil",
                                             typed_value_t::nil (),
                                             node));

  /* Insert untyped boolean constants. */
  symtab->enter (symbol_make_typed_constant ("true",
                                             typed_value_t (true),
                                             node));
  symtab->enter (symbol_make_typed_constant ("false",
                                             typed_value_t (false),
                                             node));

  struct visitor : public ast_visitor_t
  {
    void default_action (ast_t& node)
    {
      AST_FOREACH (child, &node)
        {
          child->accept (*this);
        }
    }

    void visit (ast_instance_t& node)
    {
      enter_undefined_symbol (node.symbol, node.at (INSTANCE_IDENTIFIER),
			      SymbolInstance, node.symtab);
    }

    void visit (ast_type_definition_t& node)
    {
      enter_undefined_symbol (node.symbol, node.at (TYPE_IDENTIFIER),
			      SymbolType, node.symtab);
    }

    void visit (ast_function_t& node)
    {
      enter_undefined_symbol (node.function_symbol, node.at (FUNCTION_IDENTIFIER),
                              SymbolFunction, symtab_parent (node.symtab));
    }

    static void
    enter_undefined_symbol (symbol_holder& node,
                            ast_t * identifier_node, SymbolKind kind,
                            symtab_t* symtab)
    {
      const std::string& identifier = ast_get_identifier (identifier_node);
      symbol_t *symbol = symtab->find_current (identifier);
      if (symbol == NULL)
        {
          symbol = symbol_make_undefined (identifier, kind, identifier_node);
          symtab->enter (symbol);
          node.symbol (symbol);
        }
      else
        {
          error_at_line (-1, 0, identifier_node->file,
                         identifier_node->line,
                         "%s is already defined in this scope", identifier.c_str ());
        }
    }
  };

  visitor v;
  node->accept (v);
}