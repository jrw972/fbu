#ifndef RC_SRC_ERROR_REPORTER_HPP
#define RC_SRC_ERROR_REPORTER_HPP

#include <iostream>

#include "types.hpp"

namespace util
{

enum ErrorCode
{
  Func_Expects_Count = 1,
  Func_Expects_Arg = 2,
  Cannot_Be_Applied = 3,
  Undefined = 4,
  Hidden = 5,
  Requires_Value_Or_Variable = 6,
  Requires_Type = 7,
  Leaks_Pointers = 8,
};

struct ErrorReporter
{
  typedef std::vector<ErrorCode> ListType;

  ErrorReporter (size_t limit = 0, std::ostream& out = std::cerr);

  ErrorCode func_expects_count (const Location& loc,
                                const std::string& func,
                                size_t expect,
                                size_t given);
  ErrorCode func_expects_arg (const Location& loc,
                              const std::string& func,
                              size_t idx,
                              const type::Type* expect,
                              const type::Type* given);
  ErrorCode cannot_be_applied (const Location& loc,
                               const std::string& op,
                               const type::Type* type);
  ErrorCode cannot_be_applied (const Location& loc,
                               const std::string& op,
                               const type::Type* left,
                               const type::Type* right);
  ErrorCode undefined (const Location& loc,
                       const std::string& id);
  ErrorCode hidden (const Location& loc,
                    const std::string& id);
  ErrorCode requires_value_or_variable (const Location& loc);
  ErrorCode requires_type (const Location& loc);
  ErrorCode leaks_pointers (const Location& loc);

  const ListType& list () const
  {
    return list_;
  }

  size_t count () const
  {
    return list_.size ();
  }

private:
  ErrorCode bump (ErrorCode code);
  size_t const limit_;
  std::ostream& out_;
  std::vector<ErrorCode> list_;
};

}

#endif // RC_SRC_ERROR_REPORTER_HPP
