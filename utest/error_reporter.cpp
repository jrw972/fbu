#include "error_reporter.hpp"

#include <sstream>

#include "location.hpp"
#include "type.hpp"
#include "tap.hpp"

using namespace util;

static void check (Tap& tap,
                   const char* name,
                   const Location& loc,
                   const std::stringstream& ss,
                   const ErrorReporter& er,
                   int code)
{
  const std::string& msg = ss.str ();

  std::string loc_str;
  {
    std::stringstream x;
    x << loc.file << ':' << loc.line;
    loc_str = x.str ();
  }
  std::string code_str;
  {
    std::stringstream x;
    x << '(' << 'E' << code << ')';
    code_str = x.str ();
  }

  bool flag = er.count () == 1 && msg.find (loc_str) != std::string::npos && msg.find (code_str) != std::string::npos;
  tap.tassert (name, flag, msg);
}

int
main (int argc, char** argv)
{
  Tap tap;

  {
    Location loc;
    std::stringstream ss;
    ErrorReporter er (0, ss);
    int code = er.func_expects_count (loc, "myfunc", 3, 4);
    check (tap, "ErrorReporter::func_expects_count", loc, ss, er, code);
  }

  {
    Location loc;
    std::stringstream ss;
    ErrorReporter er (0, ss);
    int code = er.func_expects_arg (loc, "myfunc", 1, &type::named_int, &type::named_uint);
    check (tap, "ErrorReporter::func_expects_arg", loc, ss, er, code);
  }

  {
    Location loc;
    std::stringstream ss;
    ErrorReporter er (0, ss);
    int code = er.cannot_be_applied (loc, "somefunc", &type::named_int);
    check (tap, "ErrorReporter::cannot_be_applied", loc, ss, er, code);
  }

  {
    Location loc;
    std::stringstream ss;
    ErrorReporter er (0, ss);
    int code = er.cannot_be_applied (loc, "somefunc", &type::named_int, &type::named_int);
    check (tap, "ErrorReporter::cannot_be_applied", loc, ss, er, code);
  }

  {
    Location loc;
    std::stringstream ss;
    ErrorReporter er (0, ss);
    int code = er.undefined (loc, "somefunc");
    check (tap, "ErrorReporter::undefined", loc, ss, er, code);
  }

  {
    Location loc;
    std::stringstream ss;
    ErrorReporter er (0, ss);
    int code = er.hidden (loc, "somefunc");
    check (tap, "ErrorReporter::hidden", loc, ss, er, code);
  }

  {
    Location loc;
    std::stringstream ss;
    ErrorReporter er (0, ss);
    int code = er.requires_value_or_variable (loc);
    check (tap, "ErrorReporter::requires_value_or_variable", loc, ss, er, code);
  }

  {
    Location loc;
    std::stringstream ss;
    ErrorReporter er (0, ss);
    int code = er.requires_type (loc);
    check (tap, "ErrorReporter::requires_type", loc, ss, er, code);
  }

  {
    Location loc;
    std::stringstream ss;
    ErrorReporter er (0, ss);
    int code = er.leaks_pointers (loc);
    check (tap, "ErrorReporter::leaks_pointers", loc, ss, er, code);
  }

  {
    Location loc;
    std::stringstream ss;
    ErrorReporter er (0, ss);
    int code = er.signature_is_not_foreign_safe (loc);
    check (tap, "ErrorReporter::signature_is_not_foreign_safe", loc, ss, er, code);
  }

  tap.print_plan ();

  return 0;
}
