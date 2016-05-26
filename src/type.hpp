#ifndef RC_SRC_TYPE_HPP
#define RC_SRC_TYPE_HPP

#include "types.hpp"

#include <vector>
#include <string>
#include <sstream>
#include <cstring>

#include "util.hpp"

#define TYPE_NOT_REACHED(type) do { std::cerr << '\n' << type << std::endl; NOT_REACHED; } while (0);

#define STRING_RETURNER(name, string) struct name { const char* operator() () const { return string; } }

namespace type
{

class Visitor;

class Array;
class Slice;
class Struct;
class Pointer;
class Function;
class Interface;
class Map;
class Heap;

typedef uint64_t UintValueType;
typedef int64_t IntValueType;

enum Kind
{
  // Untyped types.
  kNil,
  kBoolean,
  kRune,
  kInteger,
  kFloat,
  kComplex,
  kString,

  // Typed types.
  kVoid,

  kBool,
  kUint8,
  kUint16,
  kUint32,
  kUint64,
  kInt8,
  kInt16,
  kInt32,
  kInt64,
  kFloat32,
  kFloat64,
  kComplex64,
  kComplex128,
  kUint,
  kInt,
  kUintptr,
  kStringU,

  kStruct,
  kComponent,
  kArray,
  kMap,

  kPointer,
  kSlice,
  kHeap,

  kFunction,
  kMethod,
  kInterface,
  kTemplate,

  kFileDescriptor,

  // Named types.
  kNamed,
};

struct Type
{
  enum TypeLevel
  {
    UNTYPED, // Represent untyped literals.
    UNNAMED, // Types constructed through type literals and the "builtin" types.
    NAMED,   // Types named with a type declaration.
  };
  Type () : pointer_ (NULL), slice_ (NULL), heap_ (NULL) { }
  virtual ~Type () { }
  virtual void Accept (Visitor& visitor) const = 0;
  virtual std::string to_string () const = 0;
  virtual size_t Alignment () const = 0;
  virtual size_t Size () const = 0;
  virtual Kind kind () const = 0;
  virtual Kind underlying_kind () const
  {
    return UnderlyingType ()->kind ();
  }
  // When give the choice between two types, use the one with high level.
  virtual TypeLevel Level () const = 0;
  virtual const Type* UnderlyingType () const
  {
    return this;
  }
  virtual const Type* DefaultType () const
  {
    return this;
  }
  bool IsUntyped () const
  {
    return Level () == UNTYPED;
  }
  virtual bool IsNumeric () const
  {
    return false;
  }
  virtual bool IsFloatingPoint () const
  {
    return false;
  }
  virtual bool IsInteger () const
  {
    return false;
  }
  const Pointer* get_pointer () const;
  const Slice* get_slice () const;
  const Array* get_array (IntValueType dimension) const;
  const Heap* get_heap () const;
  virtual Field* select_field (const std::string& name) const
  {
    return NULL;
  }
  virtual decl::Callable* select_callable (const std::string& name) const
  {
    return NULL;
  }
  virtual const NamedType* to_named_type () const
  {
    return NULL;
  }
  virtual const Array* to_array () const
  {
    return NULL;
  }
  virtual const Slice* to_slice () const
  {
    return NULL;
  }
  virtual const Struct* to_struct () const
  {
    return NULL;
  }
  const Pointer* to_pointer () const
  {
    return to_pointer_i ();
  }
  const Pointer* u_to_pointer () const
  {
    return UnderlyingType ()->to_pointer_i ();
  }
  virtual const Function* to_function () const
  {
    return NULL;
  }
  virtual const Interface* to_interface () const
  {
    return NULL;
  }
  virtual const Map* to_map () const
  {
    return NULL;
  }
  virtual const Heap* to_heap () const
  {
    return NULL;
  }
  Field* get_field (const std::string& name) const
  {
    return UnderlyingType ()->get_field_i (name);
  }
  virtual decl::Method* get_method (const std::string& identifier) const
  {
    return NULL;
  }
  virtual decl::Initializer* get_initializer (const std::string& identifier) const
  {
    return NULL;
  }
  virtual decl::Getter* get_getter (const std::string& identifier) const
  {
    return NULL;
  }
  virtual decl::Action* get_action (const std::string& identifier) const
  {
    return NULL;
  }
  virtual decl::Reaction* get_reaction (const std::string& identifier) const
  {
    return NULL;
  }
  virtual decl::Bind* get_bind (const std::string& identifier) const
  {
    return NULL;
  }
  // Return type of selected field, method, or reaction.
  const Type* select (const std::string& identifier) const;

  // Heap operations.
  const Type* move () const;
  const Type* merge_change () const;

protected:
  virtual Field* get_field_i (const std::string& name) const
  {
    return NULL;
  }
  virtual const Pointer* to_pointer_i () const
  {
    return NULL;
  }
private:
  const Pointer* pointer_;
  const Slice* slice_;
  typedef std::map<IntValueType, const Array*> ArraysType;
  ArraysType arrays_;
  const Heap* heap_;
};

std::ostream&
operator<< (std::ostream& o, const Type& type);

class NamedType : public Type
{
public:
  typedef std::vector<decl::Getter*> GettersType;
  typedef std::vector<decl::Action*> ActionsType;
  typedef std::vector<decl::Reaction*> ReactionsType;
  typedef std::vector<decl::Bind*> BindsType;

  NamedType (const std::string& name)
    : name_ (name)
    , underlyingType_ (NULL)
  { }

  NamedType (const std::string& name,
             const Type* underlyingType);

  void Accept (Visitor& visitor) const;
  std::string to_string () const
  {
    return name_;
  }
  void UnderlyingType (const Type* u)
  {
    underlyingType_ = u->UnderlyingType ();
    assert (underlyingType_->Level () == UNNAMED);
  }
  const Type* UnderlyingType () const
  {
    return underlyingType_;
  }
  virtual size_t Alignment () const
  {
    return underlyingType_->Alignment ();
  }
  virtual size_t Size () const
  {
    return underlyingType_->Size ();
  }
  virtual Kind kind () const
  {
    return kNamed;
  }
  virtual TypeLevel Level () const
  {
    return NAMED;
  }
  virtual bool IsNumeric () const
  {
    return underlyingType_->IsNumeric ();
  }
  virtual bool IsFloatingPoint () const
  {
    return underlyingType_->IsFloatingPoint ();
  }
  virtual bool IsInteger () const
  {
    return underlyingType_->IsInteger ();
  }
  void insert_method (decl::Method* method)
  {
    methods_.push_back (method);
  }
  decl::Method* get_method (const std::string& identifier) const;
  void insert_initializer (decl::Initializer* initializer)
  {
    initializers_.push_back (initializer);
  }
  decl::Initializer* get_initializer (const std::string& identifier) const;
  void insert_getter (decl::Getter* getter)
  {
    getters_.push_back (getter);
  }
  decl::Getter* get_getter (const std::string& identifier) const;
  GettersType::const_iterator GettersBegin () const
  {
    return getters_.begin ();
  }
  GettersType::const_iterator GettersEnd () const
  {
    return getters_.end ();
  }
  void insert_action (decl::Action* action)
  {
    actions_.push_back (action);
  }
  decl::Action* get_action (const std::string& identifier) const;
  ActionsType::const_iterator ActionsBegin () const
  {
    return actions_.begin ();
  }
  ActionsType::const_iterator ActionsEnd () const
  {
    return actions_.end ();
  }
  void insert_reaction (decl::Reaction* reaction)
  {
    reactions_.push_back (reaction);
  }
  decl::Reaction* get_reaction (const std::string& identifier) const;
  ReactionsType::const_iterator ReactionsBegin () const
  {
    return reactions_.begin ();
  }
  ReactionsType::const_iterator ReactionsEnd () const
  {
    return reactions_.end ();
  }
  void insert_bind (decl::Bind* bind)
  {
    binds_.push_back (bind);
  }
  decl::Bind* get_bind (const std::string& identifier) const;
  BindsType::const_iterator BindsBegin () const
  {
    return binds_.begin ();
  }
  BindsType::const_iterator BindsEnd () const
  {
    return binds_.end ();
  }
  virtual Field* select_field (const std::string& name) const
  {
    return underlyingType_->select_field (name);
  }
  virtual decl::Callable* select_callable (const std::string& name) const;

  virtual const NamedType* to_named_type () const
  {
    return this;
  }

private:
  std::string const name_;
  const Type* underlyingType_;
  std::vector<decl::Method*> methods_;
  std::vector<decl::Initializer*> initializers_;
  GettersType getters_;
  ActionsType actions_;
  ReactionsType reactions_;
  BindsType binds_;
};

class Void : public Type
{
public:
  void Accept (Visitor& visitor) const;
  std::string to_string () const
  {
    return "<void>";
  }
  size_t Alignment () const
  {
    NOT_REACHED;
  }
  size_t Size () const
  {
    return 0;
  }
  virtual Kind kind () const
  {
    return kVoid;
  }
  virtual TypeLevel Level () const
  {
    return UNNAMED;
  }
  static const Void* Instance ();
private:
  Void () { }
};

template <typename T, typename S, bool Numeric, bool FloatingPoint, bool Integer, Kind k>
class Scalar : public Type
{
public:
  typedef T ValueType;
  void Accept (Visitor& visitor) const;
  std::string to_string () const
  {
    return S () ();
  }
  size_t Alignment () const
  {
    return sizeof (T);
  }
  size_t Size () const
  {
    return sizeof (T);
  }
  virtual Kind kind () const
  {
    return k;
  }
  virtual TypeLevel Level () const
  {
    return UNNAMED;
  }
  virtual bool IsNumeric () const
  {
    return Numeric;
  }
  virtual bool IsFloatingPoint () const
  {
    return FloatingPoint;
  }
  virtual bool IsInteger () const
  {
    return Integer;
  }
  static const Scalar<T, S, Numeric, FloatingPoint, Integer, k>* Instance ()
  {
    static Scalar<T, S, Numeric, FloatingPoint, Integer, k>* instance_ = new Scalar<T, S, Numeric, FloatingPoint, Integer, k> ();
    return instance_;
  }
private:
  Scalar<T, S, Numeric, FloatingPoint, Integer, k> () { }
};

STRING_RETURNER(BoolString, "<bool>");
typedef Scalar<bool, BoolString, false, false, false, kBool> Bool;

STRING_RETURNER(Uint8String, "<uint8>");
typedef Scalar<uint8_t, Uint8String, true, false, true, kUint8> Uint8;

STRING_RETURNER(Uint16String, "<uint16>");
typedef Scalar<uint16_t, Uint16String, true, false, true, kUint16> Uint16;

STRING_RETURNER(Uint32String, "<uint32>");
typedef Scalar<uint32_t, Uint32String, true, false, true, kUint32> Uint32;

STRING_RETURNER(Uint64String, "<uint64>");
typedef Scalar<uint64_t, Uint64String, true, false, true, kUint64> Uint64;

STRING_RETURNER(Int8String, "<int8>");
typedef Scalar<int8_t, Int8String, true, false, true, kInt8> Int8;

STRING_RETURNER(Int16String, "<int16>");
typedef Scalar<int16_t, Int16String, true, false, true, kInt16> Int16;

STRING_RETURNER(Int32String, "<int32>");
typedef Scalar<int32_t, Int32String, true, false, true, kInt32> Int32;

STRING_RETURNER(Int64String, "<int64>");
typedef Scalar<int64_t, Int64String, true, false, true, kInt64> Int64;

STRING_RETURNER(Float32String, "<float32>");
typedef Scalar<float, Float32String, true, true, false, kFloat32> Float32;

STRING_RETURNER(Float64String, "<float64>");
typedef Scalar<double, Float64String, true, true, false, kFloat64> Float64;

STRING_RETURNER(Complex64String, "<complex64>");
struct C64
{
  float real;
  float imag;

  bool operator== (const C64& other) const
  {
    return this->real == other.real && this->imag == other.imag;
  }

  operator double() const
  {
    return real;
  }

  C64& operator= (const Float64::ValueType& x)
  {
    this->real = x;
    this->imag = 0;
    return *this;
  }

  C64& operator++ ()
  {
    this->real += 1;
    return *this;
  }

  C64& operator-- ()
  {
    this->real -= 1;
    return *this;
  }

  C64& operator+= (const C64& x)
  {
    this->real += x.real;
    this->imag += x.imag;
    return *this;
  }
};
typedef Scalar<C64, Complex64String, true, false, false, kComplex64> Complex64;

STRING_RETURNER(Complex128String, "<complex128>");
struct C128
{
  double real;
  double imag;

  bool operator== (const C128& other) const
  {
    return this->real == other.real && this->imag == other.imag;
  }

  operator double() const
  {
    return real;
  }

  C128& operator= (const Float64::ValueType& x)
  {
    this->real = x;
    this->imag = 0;
    return *this;
  }

  C128& operator++ ()
  {
    this->real += 1;
    return *this;
  }

  C128& operator-- ()
  {
    this->real -= 1;
    return *this;
  }

  C128& operator+= (const C128& x)
  {
    this->real += x.real;
    this->imag += x.imag;
    return *this;
  }
};
typedef Scalar<C128, Complex128String, true, false, false, kComplex128> Complex128;

STRING_RETURNER(UintString, "<uint>");
typedef Scalar<UintValueType, UintString, true, false, true, kUint> Uint;

STRING_RETURNER(IntString, "<int>");
typedef Scalar<IntValueType, IntString, true, false, true, kInt> Int;

STRING_RETURNER(UintptrString, "<uintptr>");
typedef Scalar<ptrdiff_t, UintptrString, true, false, true, kUintptr> Uintptr;

STRING_RETURNER(StringUString, "<string>");
struct StringRep
{
  void* ptr;
  size_t length;

  bool operator== (const StringRep& other) const
  {
    if (this->ptr == other.ptr &&
        this->length == other.length)
      {
        return true;
      }

    if (this->length != other.length)
      {
        return false;
      }

    return memcmp (this->ptr, other.ptr, this->length) == 0;
  }

  bool operator< (const StringRep& other) const
  {
    int x = memcmp (this->ptr, other.ptr, std::min (this->length, other.length));
    if (x < 0)
      {
        return true;
      }
    else if (x > 0)
      {
        return false;
      }
    else
      {
        return this->length < other.length;
      }
  }
};
typedef Scalar<StringRep, StringUString, false, false, false, kStringU> StringU;

// Helper class for types that have a base type.
struct BaseType
{
  BaseType (const Type* a_base_type) : base_type (a_base_type) { }
  const Type* const base_type;
};

class Pointer : public Type, public BaseType
{
public:
  typedef void* ValueType;
  void Accept (Visitor& visitor) const;
  std::string to_string () const
  {
    return "*" + base_type->to_string ();
  }
  size_t Alignment () const
  {
    return sizeof (ValueType);
  }
  size_t Size () const
  {
    return sizeof (ValueType);
  }
  virtual Kind kind () const
  {
    return kPointer;
  }
  virtual TypeLevel Level () const
  {
    return UNNAMED;
  }
  virtual Field* select_field (const std::string& name) const
  {
    return base_type->select_field (name);
  }
  virtual decl::Callable* select_callable (const std::string& name) const
  {
    return base_type->select_callable (name);
  }
  virtual const Pointer* to_pointer_i () const
  {
    return this;
  }
private:
  friend class Type;
  Pointer (const Type* base) : BaseType (base) { }
};

class Slice : public Type, public BaseType
{
public:
  struct ValueType
  {
    void* ptr;
    Uint::ValueType length;
    Uint::ValueType capacity;
  };
  virtual void Accept (Visitor& visitor) const;
  virtual std::string to_string () const
  {
    return "[]" + base_type->to_string ();
  }
  virtual size_t Alignment () const
  {
    return sizeof (void*);
  }
  virtual size_t Size () const
  {
    return sizeof (ValueType);
  }
  virtual Kind kind () const
  {
    return kSlice;
  }
  virtual TypeLevel Level () const
  {
    return UNNAMED;
  }
  size_t UnitSize () const
  {
    return util::align_up (base_type->Size (), base_type->Alignment ());
  }
  virtual const Slice* to_slice () const
  {
    return this;
  }
private:
  friend class Type;
  Slice (const Type* base) : BaseType (base) { }
};

inline std::ostream& operator<< (std::ostream& out, const Slice::ValueType& s)
{
  out << '{' << s.ptr << ',' << s.length << ',' << s.capacity << '}';
  return out;
}

class Array : public Type, public BaseType
{
public:
  void Accept (Visitor& visitor) const;
  std::string to_string () const;
  size_t Alignment () const
  {
    return base_type->Alignment ();
  }
  size_t Size () const
  {
    return UnitSize () * dimension;
  }
  virtual Kind kind () const
  {
    return kArray;
  }
  virtual TypeLevel Level () const
  {
    return UNNAMED;
  }
  const Int::ValueType dimension;
  size_t UnitSize () const
  {
    return util::align_up (base_type->Size (), base_type->Alignment ());
  }
  virtual const Array* to_array () const
  {
    return this;
  }
private:
  friend class Type;
  Array (Int::ValueType d, const Type* base) : BaseType (base), dimension (d) { }
};

struct Map : public Type
{
  Map (const Type* a_key_type,
       const Type* a_value_type)
    : key_type (a_key_type)
    , value_type (a_value_type)
  { }
  virtual void Accept (Visitor& visitor) const;
  virtual std::string to_string () const;
  virtual size_t Alignment () const
  {
    UNIMPLEMENTED;
  }
  virtual size_t Size () const
  {
    UNIMPLEMENTED;
  }
  virtual Kind kind () const
  {
    return kMap;
  }
  virtual TypeLevel Level () const
  {
    return UNNAMED;
  }
  virtual const Map* to_map () const
  {
    return this;
  }

  const Type* const key_type;
  const Type* const value_type;
};

struct Heap : public Type, public BaseType
{
  void Accept (Visitor& visitor) const;
  std::string to_string () const
  {
    return "heap " + base_type->to_string ();
  }
  size_t Alignment () const
  {
    NOT_REACHED;
  }
  size_t Size () const
  {
    NOT_REACHED;
  }
  virtual Kind kind () const
  {
    return kHeap;
  }
  virtual TypeLevel Level () const
  {
    return UNNAMED;
  }
  virtual const Heap* to_heap () const
  {
    return this;
  }
private:
  friend class Type;
  Heap (const Type* base) : BaseType (base) { }
};

class Struct : public Type
{
public:
  typedef std::vector<Field*> FieldsType;
  typedef FieldsType::const_iterator const_iterator;
  Struct ();
  void Accept (Visitor& visitor) const;
  virtual Kind kind () const
  {
    return kStruct;
  }
  std::string to_string () const;
  size_t Alignment () const
  {
    return alignment_;
  }
  size_t Size () const
  {
    return offset_;
  }
  virtual TypeLevel Level () const
  {
    return UNNAMED;
  }
  const_iterator Begin () const
  {
    return fields_.begin ();
  }
  const_iterator End () const
  {
    return fields_.end ();
  }
  Struct* append_field (decl::Package* package, bool is_anonymous, const std::string& field_name, const Type* field_type, const TagSet& tags);
  Field* get_field_i (const std::string& name) const;
  virtual Field* select_field (const std::string& name) const;
  size_t field_count () const
  {
    return fields_.size ();
  }
  virtual const Struct* to_struct () const
  {
    return this;
  }
private:
  FieldsType fields_;
  ptrdiff_t offset_;
  size_t alignment_;
};

struct Component : public Struct
{
  Component (decl::Package* package);

  virtual Kind kind () const
  {
    return kComponent;
  }
  void Accept (Visitor& visitor) const;
  std::string to_string () const
  {
    UNIMPLEMENTED;
  }
};

class Function : public Type
{
public:
  enum FunctionKind
  {
    FUNCTION,
    PUSH_PORT,
    PULL_PORT
  };
  Function (FunctionKind k,
            const decl::ParameterList* a_parameter_list,
            const decl::ParameterList* a_return_parameter_list)
    : function_kind (k)
    , parameter_list (a_parameter_list)
    , return_parameter_list (a_return_parameter_list)
  { }
  void Accept (Visitor& visitor) const;
  std::string to_string () const;
  size_t Alignment () const
  {
    return sizeof (void*);
  }
  size_t Size () const
  {
    return function_kind == PULL_PORT ? sizeof (pull_port_t) : sizeof (void*);
  }
  virtual Kind kind () const
  {
    return kFunction;
  }
  virtual TypeLevel Level () const
  {
    return UNNAMED;
  }
  virtual const Function* to_function () const
  {
    return this;
  }
  decl::ParameterSymbol* GetParameter (const std::string& name) const;
  decl::ParameterSymbol* GetReturnParameter () const;
  const Type* GetReturnType () const;
  FunctionKind const function_kind;
  const decl::ParameterList* const parameter_list;
  const decl::ParameterList* const return_parameter_list;
};

class Method : public Type
{
public:
  enum MethodKind
  {
    METHOD,
    INITIALIZER,
    GETTER,
    REACTION,
  };
  Method (MethodKind k,
          const NamedType* named_type_,
          decl::ParameterSymbol* receiver_parameter_,
          const decl::ParameterList* parameter_list,
          const decl::ParameterList* return_parameter_list);
  void Accept (Visitor& visitor) const;
  std::string to_string () const;
  size_t Alignment () const
  {
    return sizeof (void*);
  }
  size_t Size () const
  {
    return sizeof (void*);
  }
  virtual Kind kind () const
  {
    return kMethod;
  }
  virtual TypeLevel Level () const
  {
    return UNNAMED;
  }
  MethodKind const method_kind;
  const NamedType* const named_type;
  const Type* receiver_type () const;
  decl::ParameterSymbol* const receiver_parameter;
  const Function* const function_type;
  const decl::ParameterList* const parameter_list;
  const decl::ParameterList* const return_parameter_list;
  const Type* return_type () const;
private:
  static Function* make_function_type (decl::ParameterSymbol* receiver_parameter,
                                       const decl::ParameterList* parameter_list,
                                       const decl::ParameterList* return_parameter_list);
};

struct Interface : public Type
{
  Interface (decl::Package* a_package)
    : package (a_package)
  { }

  virtual void Accept (Visitor& visitor) const;
  virtual std::string to_string () const;
  virtual size_t Alignment () const
  {
    UNIMPLEMENTED;
  }
  virtual size_t Size () const
  {
    UNIMPLEMENTED;
  }
  virtual Kind kind () const
  {
    return kInterface;
  }
  virtual TypeLevel Level () const
  {
    return UNNAMED;
  }
  void insert (const std::string& name,
               const Function* func)
  {
    methods[name] = func;
  }
  virtual const Interface* to_interface () const
  {
    return this;
  }

  decl::Package* const package;
  typedef std::map<std::string, const Function*> MethodsType;
  MethodsType methods;
};

class Untyped : public Type
{
public:
  size_t Alignment () const
  {
    NOT_REACHED;
  }
  size_t Size () const
  {
    NOT_REACHED;
  }
  virtual TypeLevel Level () const
  {
    return UNTYPED;
  }
};

class Nil : public Untyped
{
public:
  virtual Kind kind () const
  {
    return kNil;
  }
  void Accept (Visitor& visitor) const;
  std::string to_string () const
  {
    return "<<nil>>";
  }
  static const Nil* Instance ();
private:
  Nil () { }
};

class Boolean : public Untyped
{
public:
  typedef bool ValueType;
  virtual Kind kind () const
  {
    return kBoolean;
  }
  virtual const Type* DefaultType () const;
  void Accept (Visitor& visitor) const;
  std::string to_string () const
  {
    return "<<boolean>>";
  }
  static const Boolean* Instance ();
private:
  Boolean () { }
};

class Rune : public Untyped
{
public:
  typedef int32_t ValueType;
  virtual Kind kind () const
  {
    return kRune;
  }
  virtual const Type* DefaultType () const;
  virtual bool IsNumeric () const
  {
    return true;
  }
  virtual bool IsInteger () const
  {
    return true;
  }
  void Accept (Visitor& visitor) const;
  std::string to_string () const
  {
    return "<<rune>>";
  }
  static const Rune* Instance ();
private:
  Rune () { }
};

class Integer : public Untyped
{
public:
  typedef long long ValueType;
  virtual Kind kind () const
  {
    return kInteger;
  }
  virtual const Type* DefaultType () const;
  virtual bool IsNumeric () const
  {
    return true;
  }
  virtual bool IsInteger () const
  {
    return true;
  }
  void Accept (Visitor& visitor) const;
  std::string to_string () const
  {
    return "<<integer>>";
  }
  static const Integer* Instance ();
private:
  Integer () { }
};

class Float : public Untyped
{
public:
  typedef double ValueType;
  virtual Kind kind () const
  {
    return kFloat;
  }
  virtual const Type* DefaultType () const;
  virtual bool IsNumeric () const
  {
    return true;
  }
  virtual bool IsFloatingPoint () const
  {
    return true;
  }
  void Accept (Visitor& visitor) const;
  std::string to_string () const
  {
    return "<<float>>";
  }
  static const Float* Instance ();
private:
  Float () { }
};

class Complex : public Untyped
{
public:
  struct ValueType
  {
    double real;
    double imag;
    static ValueType make (double r, double i)
    {
      ValueType retval;
      retval.real = r;
      retval.imag = i;
      return retval;
    }

    bool operator== (const ValueType& other) const
    {
      return this->real == other.real && this->imag == other.imag;
    }

    ValueType& operator= (const Float::ValueType& x)
    {
      this->real = x;
      this->imag = 0;
      return *this;
    }

    ValueType& operator= (const Complex64::ValueType& x)
    {
      this->real = x.real;
      this->imag = x.imag;
      return *this;
    }

    ValueType& operator= (const Complex128::ValueType& x)
    {
      this->real = x.real;
      this->imag = x.imag;
      return *this;
    }

    operator double() const
    {
      return this->real;
    }
  };
  virtual Kind kind () const
  {
    return kComplex;
  }
  virtual const Type* DefaultType () const;
  virtual bool IsNumeric () const
  {
    return true;
  }
  void Accept (Visitor& visitor) const;
  std::string to_string () const
  {
    return "<<complex>>";
  }
  static const Complex* Instance ();
private:
  Complex () { }
};

class String : public Untyped
{
public:
  typedef StringRep ValueType;
  virtual Kind kind () const
  {
    return kString;
  }
  virtual const Type* DefaultType () const;
  void Accept (Visitor& visitor) const;
  std::string to_string () const
  {
    return "<<string>>";
  }
  static const String* Instance ();
private:
  String () { }
};

class Template : public Type
{
public:
  void Accept (Visitor& visitor) const;
  virtual std::string to_string () const
  {
    return "<<template>>";
  }
  virtual size_t Alignment () const
  {
    NOT_REACHED;
  }
  virtual size_t Size () const
  {
    NOT_REACHED;
  }
  virtual Kind kind () const
  {
    return kTemplate;
  }
  virtual TypeLevel Level () const
  {
    return UNTYPED;
  }
};

struct FileDescriptor : public Type
{
  void Accept (Visitor& visitor) const;
  std::string to_string () const
  {
    return "<FileDescriptor>";
  }
  size_t Alignment () const
  {
    return sizeof (void*);
  }
  size_t Size () const
  {
    return sizeof (void*);
  }
  virtual Kind kind () const
  {
    return kFileDescriptor;
  }
  virtual TypeLevel Level () const
  {
    return UNNAMED;
  }
  static const FileDescriptor* Instance ();
private:
  FileDescriptor () { }
};

struct Visitor
{
  virtual ~Visitor () { }
  virtual void visit (const Array& type) = 0;
  virtual void visit (const Bool& type) = 0;
  virtual void visit (const Boolean& type) = 0;
  virtual void visit (const Complex& type) = 0;
  virtual void visit (const Complex128& type) = 0;
  virtual void visit (const Complex64& type) = 0;
  virtual void visit (const Component& type) = 0;
  virtual void visit (const FileDescriptor& type) = 0;
  virtual void visit (const Float& type) = 0;
  virtual void visit (const Float32& type) = 0;
  virtual void visit (const Float64& type) = 0;
  virtual void visit (const Function& type) = 0;
  virtual void visit (const Heap& type) = 0;
  virtual void visit (const Int& type) = 0;
  virtual void visit (const Int16& type) = 0;
  virtual void visit (const Int32& type) = 0;
  virtual void visit (const Int64& type) = 0;
  virtual void visit (const Int8& type) = 0;
  virtual void visit (const Integer& type) = 0;
  virtual void visit (const Interface& type) = 0;
  virtual void visit (const Map& type) = 0;
  virtual void visit (const Method& type) = 0;
  virtual void visit (const NamedType& type) = 0;
  virtual void visit (const Nil& type) = 0;
  virtual void visit (const Pointer& type) = 0;
  virtual void visit (const Rune& type) = 0;
  virtual void visit (const Slice& type) = 0;
  virtual void visit (const String& type) = 0;
  virtual void visit (const StringU& type) = 0;
  virtual void visit (const Struct& type) = 0;
  virtual void visit (const Template& type) = 0;
  virtual void visit (const Uint& type) = 0;
  virtual void visit (const Uint16& type) = 0;
  virtual void visit (const Uint32& type) = 0;
  virtual void visit (const Uint64& type) = 0;
  virtual void visit (const Uint8& type) = 0;
  virtual void visit (const Uintptr& type) = 0;
  virtual void visit (const Void& type) = 0;
};

template <typename T>
struct ComparableVisitor : public Visitor
{
  typedef T DispatchType;

  T& t;
  ComparableVisitor (T& t_) : t (t_) { }

  virtual void visit (const Array& type)
  {
    t.NotComparable (type);
  }
  virtual void visit (const Slice& type)
  {
    t.NotComparable (type);
  }
  virtual void visit (const Bool& type)
  {
    t (type);
  }
  virtual void visit (const Component& type)
  {
    t.NotComparable (type);
  }
  virtual void visit (const Function& type)
  {
    t.NotComparable (type);
  }
  virtual void visit (const Method& type)
  {
    t.NotComparable (type);
  }
  virtual void visit (const Heap& type)
  {
    t.NotComparable (type);
  }
  virtual void visit (const FileDescriptor& type)
  {
    t.NotComparable (type);
  }
  virtual void visit (const NamedType& type)
  {
    type.UnderlyingType ()->Accept (*this);
  }
  virtual void visit (const Pointer& type)
  {
    t (type);
  }
  virtual void visit (const Struct& type)
  {
    t.NotComparable (type);
  }
  virtual void visit (const Int& type)
  {
    t (type);
  }
  virtual void visit (const Int8& type)
  {
    t (type);
  }
  virtual void visit (const Int16& type)
  {
    t (type);
  }
  virtual void visit (const Int32& type)
  {
    t (type);
  }
  virtual void visit (const Int64& type)
  {
    t (type);
  }
  virtual void visit (const Uint& type)
  {
    t (type);
  }
  virtual void visit (const Uint8& type)
  {
    t (type);
  }
  virtual void visit (const Uint16& type)
  {
    t (type);
  }
  virtual void visit (const Uint32& type)
  {
    t (type);
  }
  virtual void visit (const Uint64& type)
  {
    t (type);
  }
  virtual void visit (const Float32& type)
  {
    t (type);
  }
  virtual void visit (const Float64& type)
  {
    t (type);
  }
  virtual void visit (const Complex64& type)
  {
    t (type);
  }
  virtual void visit (const Complex128& type)
  {
    t (type);
  }
  virtual void visit (const StringU& type)
  {
    t (type);
  }
  virtual void visit (const Nil& type)
  {
    t.NotComparable (type);
  }
  virtual void visit (const Boolean& type)
  {
    t (type);
  }
  virtual void visit (const Rune& type)
  {
    t (type);
  }
  virtual void visit (const Integer& type)
  {
    t (type);
  }
  virtual void visit (const Float& type)
  {
    t (type);
  }
  virtual void visit (const Complex& type)
  {
    t (type);
  }
  virtual void visit (const String& type)
  {
    t (type);
  }
  virtual void visit (const Void& type)
  {
    t.NotComparable (type);
  }
  virtual void visit (const Template& type)
  {
    t.NotComparable (type);
  }
  virtual void visit (const Uintptr& type)
  {
    t (type);
  }
};

template <typename T>
struct OrderableVisitor : public Visitor
{
  typedef T DispatchType;

  T& t;
  OrderableVisitor (T& t_) : t (t_) { }

  virtual void visit (const Array& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const Slice& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const Bool& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const Component& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const Function& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const Method& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const Heap& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const FileDescriptor& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const NamedType& type)
  {
    type.UnderlyingType ()->Accept (*this);
  }
  virtual void visit (const Pointer& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const Struct& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const Int& type)
  {
    t (type);
  }
  virtual void visit (const Int8& type)
  {
    t (type);
  }
  virtual void visit (const Int16& type)
  {
    t (type);
  }
  virtual void visit (const Int32& type)
  {
    t (type);
  }
  virtual void visit (const Int64& type)
  {
    t (type);
  }
  virtual void visit (const Uint& type)
  {
    t (type);
  }
  virtual void visit (const Uint8& type)
  {
    t (type);
  }
  virtual void visit (const Uint16& type)
  {
    t (type);
  }
  virtual void visit (const Uint32& type)
  {
    t (type);
  }
  virtual void visit (const Uint64& type)
  {
    t (type);
  }
  virtual void visit (const Float32& type)
  {
    t (type);
  }
  virtual void visit (const Float64& type)
  {
    t (type);
  }
  virtual void visit (const Complex64& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const Complex128& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const StringU& type)
  {
    t (type);
  }
  virtual void visit (const Nil& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const Boolean& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const Rune& type)
  {
    t (type);
  }
  virtual void visit (const Integer& type)
  {
    t (type);
  }
  virtual void visit (const Float& type)
  {
    t (type);
  }
  virtual void visit (const Complex& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const String& type)
  {
    t (type);
  }
  virtual void visit (const Void& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const Template& type)
  {
    t.NotOrderable (type);
  }
  virtual void visit (const Uintptr& type)
  {
    t (type);
  }
};

template <typename T>
struct ArithmeticVisitor : public Visitor
{
  typedef T DispatchType;

  T& t;
  ArithmeticVisitor (T& t_) : t (t_) { }

  virtual void visit (const Array& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const Slice& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const Bool& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const Component& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const Function& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const Method& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const Heap& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const FileDescriptor& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const NamedType& type)
  {
    type.UnderlyingType ()->Accept (*this);
  }
  virtual void visit (const Pointer& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const Struct& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const Int& type)
  {
    t (type);
  }
  virtual void visit (const Int8& type)
  {
    t (type);
  }
  virtual void visit (const Int16& type)
  {
    t (type);
  }
  virtual void visit (const Int32& type)
  {
    t (type);
  }
  virtual void visit (const Int64& type)
  {
    t (type);
  }
  virtual void visit (const Uint& type)
  {
    t (type);
  }
  virtual void visit (const Uint8& type)
  {
    t (type);
  }
  virtual void visit (const Uint16& type)
  {
    t (type);
  }
  virtual void visit (const Uint32& type)
  {
    t (type);
  }
  virtual void visit (const Uint64& type)
  {
    t (type);
  }
  virtual void visit (const Float32& type)
  {
    t (type);
  }
  virtual void visit (const Float64& type)
  {
    t (type);
  }
  virtual void visit (const Complex64& type)
  {
    t (type);
  }
  virtual void visit (const Complex128& type)
  {
    t (type);
  }
  virtual void visit (const StringU& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const Nil& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const Boolean& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const Rune& type)
  {
    t (type);
  }
  virtual void visit (const Integer& type)
  {
    t (type);
  }
  virtual void visit (const Float& type)
  {
    t (type);
  }
  virtual void visit (const Complex& type)
  {
    t (type);
  }
  virtual void visit (const String& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const Void& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const Template& type)
  {
    t.NotArithmetic (type);
  }
  virtual void visit (const Uintptr& type)
  {
    t (type);
  }
};

template <typename T>
struct IntegralVisitor : public Visitor
{
  typedef T DispatchType;

  T& t;
  IntegralVisitor (T& t_) : t (t_) { }

  virtual void visit (const Array& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Slice& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Bool& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Component& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Function& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Method& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Heap& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const FileDescriptor& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const NamedType& type)
  {
    type.UnderlyingType ()->Accept (*this);
  }
  virtual void visit (const Pointer& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Struct& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Int& type)
  {
    t (type);
  }
  virtual void visit (const Int8& type)
  {
    t (type);
  }
  virtual void visit (const Int16& type)
  {
    t (type);
  }
  virtual void visit (const Int32& type)
  {
    t (type);
  }
  virtual void visit (const Int64& type)
  {
    t (type);
  }
  virtual void visit (const Uint& type)
  {
    t (type);
  }
  virtual void visit (const Uint8& type)
  {
    t (type);
  }
  virtual void visit (const Uint16& type)
  {
    t (type);
  }
  virtual void visit (const Uint32& type)
  {
    t (type);
  }
  virtual void visit (const Uint64& type)
  {
    t (type);
  }
  virtual void visit (const Float32& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Float64& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Complex64& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Complex128& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const StringU& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Nil& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Boolean& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Rune& type)
  {
    t (type);
  }
  virtual void visit (const Integer& type)
  {
    t (type);
  }
  virtual void visit (const Float& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Complex& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const String& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Void& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Template& type)
  {
    t.NotIntegral (type);
  }
  virtual void visit (const Uintptr& type)
  {
    t (type);
  }
};

template <typename T>
struct LogicalVisitor : public Visitor
{
  typedef T DispatchType;

  T& t;
  LogicalVisitor (T& t_) : t (t_) { }

  virtual void visit (const Array& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Slice& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Bool& type)
  {
    t (type);
  }
  virtual void visit (const Component& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Function& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Method& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Heap& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const FileDescriptor& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const NamedType& type)
  {
    type.UnderlyingType ()->Accept (*this);
  }
  virtual void visit (const Pointer& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Struct& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Int& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Int8& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Int16& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Int32& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Int64& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Uint& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Uint8& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Uint16& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Uint32& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Uint64& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Float32& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Float64& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Complex64& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Complex128& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const StringU& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Nil& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Boolean& type)
  {
    t (type);
  }
  virtual void visit (const Rune& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Integer& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Float& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Complex& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const String& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Void& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Template& type)
  {
    t.NotLogical (type);
  }
  virtual void visit (const Uintptr& type)
  {
    t.NotLogical (type);
  }
};

struct DefaultVisitor : public Visitor
{
  virtual void visit (const Array& type)
  {
    default_action (type);
  }
  virtual void visit (const Map& type)
  {
    default_action (type);
  }
  virtual void visit (const Interface& type)
  {
    default_action (type);
  }
  virtual void visit (const Slice& type)
  {
    default_action (type);
  }
  virtual void visit (const Bool& type)
  {
    default_action (type);
  }
  virtual void visit (const Component& type)
  {
    default_action (type);
  }
  virtual void visit (const Function& type)
  {
    default_action (type);
  }
  virtual void visit (const Method& type)
  {
    default_action (type);
  }
  virtual void visit (const Heap& type)
  {
    default_action (type);
  }
  virtual void visit (const FileDescriptor& type)
  {
    default_action (type);
  }
  virtual void visit (const NamedType& type)
  {
    default_action (type);
  }
  virtual void visit (const Pointer& type)
  {
    default_action (type);
  }
  virtual void visit (const Struct& type)
  {
    default_action (type);
  }
  virtual void visit (const Int& type)
  {
    default_action (type);
  }
  virtual void visit (const Int8& type)
  {
    default_action (type);
  }
  virtual void visit (const Int16& type)
  {
    default_action (type);
  }
  virtual void visit (const Int32& type)
  {
    default_action (type);
  }
  virtual void visit (const Int64& type)
  {
    default_action (type);
  }
  virtual void visit (const Uint& type)
  {
    default_action (type);
  }
  virtual void visit (const Uint8& type)
  {
    default_action (type);
  }
  virtual void visit (const Uint16& type)
  {
    default_action (type);
  }
  virtual void visit (const Uint32& type)
  {
    default_action (type);
  }
  virtual void visit (const Uint64& type)
  {
    default_action (type);
  }
  virtual void visit (const Float32& type)
  {
    default_action (type);
  }
  virtual void visit (const Float64& type)
  {
    default_action (type);
  }
  virtual void visit (const Complex64& type)
  {
    default_action (type);
  }
  virtual void visit (const Complex128& type)
  {
    default_action (type);
  }
  virtual void visit (const StringU& type)
  {
    default_action (type);
  }
  virtual void visit (const Nil& type)
  {
    default_action (type);
  }
  virtual void visit (const Boolean& type)
  {
    default_action (type);
  }
  virtual void visit (const Rune& type)
  {
    default_action (type);
  }
  virtual void visit (const Integer& type)
  {
    default_action (type);
  }
  virtual void visit (const Float& type)
  {
    default_action (type);
  }
  virtual void visit (const Complex& type)
  {
    default_action (type);
  }
  virtual void visit (const String& type)
  {
    default_action (type);
  }
  virtual void visit (const Void& type)
  {
    default_action (type);
  }
  virtual void visit (const Template& type)
  {
    default_action (type);
  }
  virtual void visit (const Uintptr& type)
  {
    default_action (type);
  }

  virtual void default_action (const Type& type) { }
};

template <typename T, typename T1>
struct visitor2 : public DefaultVisitor
{
  const T1& type1;
  T& t;

  visitor2 (const T1& t1, T& t_) : type1 (t1), t (t_) { }

  void default_action (const Type& type)
  {
    TYPE_NOT_REACHED (type);
  }

  void visit (const Map& type2)
  {
    t (type1, type2);
  }

  void visit (const Interface& type2)
  {
    t (type1, type2);
  }

  void visit (const Array& type2)
  {
    t (type1, type2);
  }

  void visit (const Bool& type2)
  {
    t (type1, type2);
  }

  void visit (const Int& type2)
  {
    t (type1, type2);
  }
  void visit (const Int8& type2)
  {
    t (type1, type2);
  }
  void visit (const Int16& type2)
  {
    t (type1, type2);
  }
  void visit (const Int32& type2)
  {
    t (type1, type2);
  }
  void visit (const Int64& type2)
  {
    t (type1, type2);
  }

  void visit (const Uint& type2)
  {
    t (type1, type2);
  }
  void visit (const Uint8& type2)
  {
    t (type1, type2);
  }
  void visit (const Uint16& type2)
  {
    t (type1, type2);
  }
  void visit (const Uint32& type2)
  {
    t (type1, type2);
  }
  void visit (const Uint64& type2)
  {
    t (type1, type2);
  }

  void visit (const Float32& type2)
  {
    t (type1, type2);
  }
  void visit (const Float64& type2)
  {
    t (type1, type2);
  }

  void visit (const Complex64& type2)
  {
    t (type1, type2);
  }
  void visit (const Complex128& type2)
  {
    t (type1, type2);
  }

  void visit (const Pointer& type2)
  {
    t (type1, type2);
  }

  void visit (const Slice& type2)
  {
    t (type1, type2);
  }

  void visit (const StringU& type2)
  {
    t (type1, type2);
  }

  void visit (const Struct& type2)
  {
    t (type1, type2);
  }

  void visit (const Boolean& type2)
  {
    t (type1, type2);
  }
  void visit (const Rune& type2)
  {
    t (type1, type2);
  }
  void visit (const Integer& type2)
  {
    t (type1, type2);
  }
  void visit (const Float& type2)
  {
    t (type1, type2);
  }

  void visit (const String& type2)
  {
    t (type1, type2);
  }

  void visit (const Nil& type2)
  {
    t (type1, type2);
  }

  void visit (const Function& type2)
  {
    t (type1, type2);
  }
};

template <typename T, typename T1>
static void doubleDispatchHelper (const T1& type1, const Type* type2, T& t)
{
  visitor2<T, T1> v (type1, t);
  type2->Accept (v);
}

template <typename T>
struct visitor1 : public DefaultVisitor
{
  const Type* type2;
  T& t;
  visitor1 (const Type* t2, T& t_) : type2 (t2), t (t_) { }

  void default_action (const Type& type)
  {
    TYPE_NOT_REACHED (type);
  }

  void visit (const Map& type)
  {
    doubleDispatchHelper (type, type2, t);
  }

  void visit (const Interface& type)
  {
    doubleDispatchHelper (type, type2, t);
  }

  void visit (const Bool& type)
  {
    doubleDispatchHelper (type, type2, t);
  }

  void visit (const Int& type)
  {
    doubleDispatchHelper (type, type2, t);
  }
  void visit (const Int8& type)
  {
    doubleDispatchHelper (type, type2, t);
  }
  void visit (const Int16& type)
  {
    doubleDispatchHelper (type, type2, t);
  }
  void visit (const Int32& type)
  {
    doubleDispatchHelper (type, type2, t);
  }
  void visit (const Int64& type)
  {
    doubleDispatchHelper (type, type2, t);
  }

  void visit (const Uint& type)
  {
    doubleDispatchHelper (type, type2, t);
  }
  void visit (const Uint8& type)
  {
    doubleDispatchHelper (type, type2, t);
  }
  void visit (const Uint16& type)
  {
    doubleDispatchHelper (type, type2, t);
  }
  void visit (const Uint32& type)
  {
    doubleDispatchHelper (type, type2, t);
  }
  void visit (const Uint64& type)
  {
    doubleDispatchHelper (type, type2, t);
  }
  void visit (const Float32& type)
  {
    doubleDispatchHelper (type, type2, t);
  }
  void visit (const Float64& type)
  {
    doubleDispatchHelper (type, type2, t);
  }
  void visit (const Complex64& type)
  {
    doubleDispatchHelper (type, type2, t);
  }
  void visit (const Complex128& type)
  {
    doubleDispatchHelper (type, type2, t);
  }

  void visit (const Pointer& type)
  {
    doubleDispatchHelper (type, type2, t);
  }

  void visit (const Array& type)
  {
    doubleDispatchHelper (type, type2, t);
  }

  void visit (const StringU& type)
  {
    doubleDispatchHelper (type, type2, t);
  }

  void visit (const Slice& type)
  {
    doubleDispatchHelper (type, type2, t);
  }

  void visit (const Struct& type)
  {
    doubleDispatchHelper (type, type2, t);
  }

  void visit (const Boolean& type)
  {
    doubleDispatchHelper (type, type2, t);
  }

  void visit (const Rune& type)
  {
    doubleDispatchHelper (type, type2, t);
  }
  void visit (const Integer& type)
  {
    doubleDispatchHelper (type, type2, t);
  }
  void visit (const Float& type)
  {
    doubleDispatchHelper (type, type2, t);
  }

  void visit (const String& type)
  {
    doubleDispatchHelper (type, type2, t);
  }

  void visit (const Nil& type)
  {
    doubleDispatchHelper (type, type2, t);
  }

  void visit (const Function& type)
  {
    doubleDispatchHelper (type, type2, t);
  }
};

template <typename T>
static void DoubleDispatch (const Type* type1, const Type* type2, T& t)
{
  visitor1<T> v (type2, t);
  type1->Accept (v);
}

// Return the type of indexing into the other type.
const Type*
type_index (const Type* base, const Type* index);

bool
are_identical (const Type* x, const Type* y);

bool
assignable (const Type* from, const semantic::Value& from_value, const Type* to);

const Type*
Choose (const Type* x, const Type* y);

// True if any pointer is accessible.
bool
type_contains_pointer (const Type* type);

bool is_typed_boolean (const Type* type);
bool is_untyped_boolean (const Type* type);
bool is_any_boolean (const Type* type);

bool is_typed_string (const Type* type);
bool is_untyped_string (const Type* type);
bool is_any_string (const Type* type);

bool is_typed_integer (const Type* type);
bool is_typed_unsigned_integer (const Type* type);
bool is_typed_float (const Type* type);
bool is_typed_complex (const Type* type);
// True to typed numeric types.
bool is_typed_numeric (const Type* type);
// True for untyped numeric types.
bool is_untyped_numeric (const Type* type);
bool is_any_numeric (const Type* type);
bool is_slice_of_bytes (const Type* type);
bool is_slice_of_runes (const Type* type);

// True if the type is comparable.
bool comparable (const Type* type);

// True if the type is orderable.
bool orderable (const Type* type);

// True if the types are arithmetic.
bool arithmetic (const Type* type);

bool integral (const Type* type);

// True if type is an integer.
bool
type_is_integral (const Type* type);

// True if type is an unsigned integer.
bool
type_is_unsigned_integral (const Type* type);

// True if type is floating-point.
bool
type_is_floating (const Type* type);

// True if type is numeric.
inline bool
type_is_numeric (const Type* type)
{
  return type_is_integral (type) || type_is_floating (type);
}

// True if == or != can be applied to values of this type.
bool
type_is_comparable (const Type* type);

// True if <, <=, >, or >= can be applied to values of this type.
bool
type_is_orderable (const Type* type);

// True if index is valid.
bool
type_is_index (const Type* type, Int::ValueType index);

// True if x can be cast to y.
bool
type_is_castable (const Type* x, const Type* y);

bool
type_is_pointer_compare (const Type* left, const Type* right);

// Remove a NamedType.
const Type*
type_strip (const Type* type);

// Cast a type to a specific type.
template<typename T>
const T*
type_cast (const Type * type)
{
  if (type == NULL) return NULL;

  struct visitor : public DefaultVisitor
  {
    const T* retval;

    visitor () : retval (NULL) { }

    void visit (const T& type)
    {
      retval = &type;
    }
  };
  visitor v;
  type->Accept (v);
  return v.retval;
}

template<typename T>
const T*
type_strip_cast (const Type* type)
{
  if (type == NULL) return NULL;
  return type_cast<T> (type_strip (type));
}

// Return a Pointer if a pointer to an array or NULL.
const Pointer*
pointer_to_array (const Type* type);

// Select the type for *, /, +, -.
// NULL means no suitable type.
struct Arithmetic
{
  static const Type* pick (const Type* left_type, const Type* right_type);
};

// Select the type for %.
// NULL means no suitable type.
struct Integral
{
  static const Type* pick (const Type* left_type, const Type* right_type);
};

// Select the type for == and !=.
// NULL means no suitable type.
struct Comparable
{
  static const type::Type* pick (const type::Type* left_type, const type::Type* right_type);
};

// Select the type for <, <=, >, and >=.
// NULL means no suitable type.
struct Orderable
{
  static const type::Type* pick (const type::Type* left_type, const type::Type* right_type);
};

// Select the type for || and &&.
// NULL means no suitable type.
struct Logical
{
  static const type::Type* pick (const type::Type* left_type, const type::Type* right_type);
};

inline C64 operator* (const C64&, const C64&)
{
  UNIMPLEMENTED;
}
inline C64 operator/ (const C64&, const C64&)
{
  UNIMPLEMENTED;
}


inline C128 operator* (const C128&, const C128&)
{
  UNIMPLEMENTED;
}
inline C128 operator/ (const C128&, const C128&)
{
  UNIMPLEMENTED;
}

inline Complex::ValueType operator* (const Complex::ValueType&, const Complex::ValueType&)
{
  UNIMPLEMENTED;
}
inline Complex::ValueType operator/ (const Complex::ValueType&, const Complex::ValueType&)
{
  UNIMPLEMENTED;
}

inline C64 operator+ (const C64&, const C64&)
{
  UNIMPLEMENTED;
}
inline C64 operator- (const C64&, const C64&)
{
  UNIMPLEMENTED;
}
inline C64 operator- (const C64&)
{
  UNIMPLEMENTED;
}

inline C128 operator+ (const C128&, const C128&)
{
  UNIMPLEMENTED;
}
inline C128 operator- (const C128&, const C128&)
{
  UNIMPLEMENTED;
}
inline C128 operator- (const C128&)
{
  UNIMPLEMENTED;
}

inline Complex::ValueType operator+ (const Complex::ValueType&, const Complex::ValueType&)
{
  UNIMPLEMENTED;
}
inline Complex::ValueType operator- (const Complex::ValueType&, const Complex::ValueType&)
{
  UNIMPLEMENTED;
}
inline Complex::ValueType operator- (const Complex::ValueType&)
{
  UNIMPLEMENTED;
}

extern NamedType named_bool;

extern NamedType named_uint8;
extern NamedType named_uint16;
extern NamedType named_uint32;
extern NamedType named_uint64;

extern NamedType named_int8;
extern NamedType named_int16;
extern NamedType named_int32;
extern NamedType named_int64;

extern NamedType named_float32;
extern NamedType named_float64;

extern NamedType named_complex64;
extern NamedType named_complex128;

extern NamedType named_uint;
extern NamedType named_int;
extern NamedType named_uintptr;

extern NamedType named_rune;
extern NamedType named_byte;
extern NamedType named_string;

extern NamedType named_file_descriptor;
extern NamedType named_timespec;

inline std::ostream& operator<< (std::ostream& out, const StringRep& s)
{
  return out << std::string (static_cast<const char*> (s.ptr), s.length);
}

}

#endif // RC_SRC_TYPE_HPP
