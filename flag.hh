#include <string_view>
#include <vector>
#include <functional>
#include <memory>
#include <limits>
#include <algorithm>
#include <iterator>
#include <cstdlib>
#include <cctype>
#if defined (_WIN32) && defined (FLAG_SHORTEN_WINDOWS_PROGRAM_PATH)
#  include <filesystem>
#endif

namespace flag
{
using Option_Callable = std::function<bool (const char *)>;

using Help_Function = std::function<void (const char *)>;

using Collect_Arg = std::function<void (const char *)>;

namespace detail
{
template <class T>
constexpr bool is_signed_int = std::is_integral_v<T> && std::is_signed_v<T>;

template <class T>
constexpr bool is_unsigned_int = std::is_integral_v<T> && std::is_unsigned_v<T>;

template <class T>
constexpr bool is_string = (std::is_same_v<T, const char *>
                            || std::is_same_v<T, std::string>
                            || std::is_same_v<T, std::string_view>);
} // namespace detail

namespace types
{
template <class T, typename __enable_if_dummy=void>
struct Value_Type
{
  static constexpr bool is_supported = false;
  static constexpr const char *value_name = nullptr;
  static void convert_arg (const char *arg, T *value) {}
};

template <class T>
struct Value_Type<T, std::enable_if_t<detail::is_signed_int<T>>>
{
  static constexpr bool is_supported = true;
  static constexpr const char *value_name = "int";

  static void convert_arg (const char *arg, T *value)
  {
    const long long my_value = std::strtoll (arg, nullptr, 0);
    if (my_value > std::numeric_limits<T>::max ())
      throw std::range_error ("value too large");
    else if (my_value < std::numeric_limits<T>::min ())
      throw std::range_error ("value too small");
    *value = static_cast<T> (my_value);
  }
};

template <class T>
struct Value_Type<T, std::enable_if_t<detail::is_unsigned_int<T>>>
{
  static constexpr bool is_supported = true;
  static constexpr const char *value_name = "unsigned";

  static void convert_arg (const char *arg, T *value)
  {
    const unsigned long long my_value = std::strtoull (arg, nullptr, 0);
    if (my_value > std::numeric_limits<T>::max ())
      throw std::range_error ("value too large");
    *value = static_cast<T> (my_value);
  }
};

template <class T>
struct Value_Type<T, std::enable_if_t<std::is_floating_point_v<T>>>
{
  static constexpr bool is_supported = true;
  static constexpr const char *value_name = "float";

  static void convert_arg (const char *arg, T *value)
  {
    const double my_value = std::strtod (arg, nullptr);
    *value = static_cast<T> (my_value);
  }
};

template <class T>
struct Value_Type<T, std::enable_if_t<detail::is_string<T>>>
{
  static constexpr bool is_supported = true;
  static constexpr const char *value_name = "string";

  static void convert_arg (const char *arg, T *value)
  {
    *value = arg;
  }
};

} // namespace types

namespace detail
{
struct Option_Base
{
  std::string_view flag_;
  std::string_view help_text_;

  virtual ~Option_Base () {}

  Option_Base (std::string_view flag, std::string_view help_text)
  : flag_ (flag), help_text_ (help_text)
  {}

  std::string_view flag () const { return flag_; }
  std::string_view help_text () const { return help_text_; }

  bool operator== (std::string_view test) const
  { return flag_ == test; }

  virtual bool parse_arg (const char *) = 0;
  virtual bool takes_value () const = 0;
  virtual const char * value_name () const = 0;
};

template <class T>
struct Option_Type : Option_Base
{
  T *value_;

  Option_Type (T *value, std::string_view flag, std::string_view help_text)
  : Option_Base (flag, help_text), value_ (value)
  {}

  bool parse_arg (const char *arg) override
  {
    types::Value_Type<T>::convert_arg (arg, value_);
    return true;
  }

  bool takes_value () const override
  { return true; }

  const char * value_name () const override
  { return types::Value_Type<T>::value_name; }
};

template <>
struct Option_Type<bool> : Option_Base
{
  bool *value_;
  const bool target_value_;

  Option_Type (bool *value, std::string_view flag, std::string_view help_text)
  : Option_Base (flag, help_text), value_ (value), target_value_ (!*value_)
  {}

  bool parse_arg (const char *) override
  {
    *value_ = target_value_;
    return true;
  }

  bool takes_value () const override
  { return false; }

  // Unused
  const char * value_name () const override
  { return nullptr; }
};

template <>
struct Option_Type<Option_Callable> : Option_Base
{
  Option_Callable function_;

  Option_Type (Option_Callable function, std::string_view flag,
               std::string_view help_text)
  : Option_Base (flag, help_text), function_ (function)
  {}

  bool parse_arg (const char *arg) override
  { return function_ (arg); }

  bool takes_value () const override
  { return true; }

  const char * value_name () const override
  { return nullptr; }
};

inline std::vector<std::unique_ptr<Option_Base>> options = {};
inline Help_Function usage = nullptr;
inline std::string_view error_description = "";

static inline void
print_type_name (Option_Base *option)
{
  const char *value_name = option->value_name ();
  std::cout << "\x1b[2m";  // Dim
  if (value_name)
    std::cout << value_name;
  else
    {
      // If the option cannot provide it's own value name we use the flag in
      // uppercase.
      const std::string_view flag_name = option->flag ();
      std::transform (flag_name.begin (), flag_name.end (),
                      std::ostream_iterator<char> (std::cout),
                      [&] (char ch) -> char {
                        // Don't touch unicode
                        if (ch & 0x80)
                          return ch;
                        return std::toupper (ch);
                      });
    }
  std::cout << "\x1b[0m";
}

static void
default_usage (const char *program)
{
  std::cout << "Usage: " << program << " ...\n";
  for (auto &option : options)
    {
      std::cout << "    -" << option->flag ();
      if (option->takes_value ())
        {
          std::cout << ' ';
          print_type_name (option.get ());
        }
      std::cout << '\n';
      if (!option->help_text ().empty ())
        std::cout << "        " << option->help_text () << '\n';
    }
}

enum class Process_Result
{
  Ok,
  Invalid_Option,
  Missing_Value,
  Unexpected_Value,
  Invalid_Value
};

static Process_Result
process_flag (std::string_view flag, std::string_view &value,
                int &argind, int argc, const char *const *argv)
{
  const auto it = std::find_if (options.begin (), options.end (),
                                [&flag] (const auto &test) {
                                  return test->operator== (flag);
                                });
  if (it == options.end ())
    return Process_Result::Invalid_Option;
  auto &option = *it;
  if (option->takes_value ())
    {
      if (value.empty ())
        {
          if ((argind + 1) < argc)
            value = argv[++argind];
          else
            return Process_Result::Missing_Value;
        }
      if (!option->parse_arg (value.data ()))
        return Process_Result::Invalid_Value;
    }
  else
    {
      if (!value.empty ())
        return Process_Result::Unexpected_Value;
      if (!option->parse_arg (nullptr))
        return Process_Result::Invalid_Value;
    }
  return Process_Result::Ok;
}

static inline void
complain (const char *program, Process_Result about, std::string_view flag,
          std::string_view value, bool double_dash)
{
  std::cerr << program << ": ";
  // Since we extract the flag name from the arg-element we need to add the
  // correct number of dashes to the error message
  std::string_view dash = double_dash ? "--" : "-";
  switch (about)
    {
      break; case Process_Result::Ok: // To suppress warnings
      break; case Process_Result::Invalid_Option:
        std::cerr << "unrecognized option ‘" << dash << flag << "’";
      break; case Process_Result::Missing_Value:
        std::cerr << "option ‘" << dash << flag <<  "’ requires an argument";
      break; case Process_Result::Unexpected_Value:
        std::cerr << "option ‘" << dash << flag
                  << "’ doesn't allow an argument";
      break; case Process_Result::Invalid_Value:
        std::cerr << "invalid argument ‘" << value <<  "’ for ‘" << dash
                  << flag << "’";
    }
  std::cerr << std::endl;
  if (!error_description.empty ())
    std::cerr << error_description << std::endl;
}

} // namespace detail

template <class T>
static inline void
add (T &value, std::string_view flag, std::string_view help_text)
{
  using namespace std::literals;
  static_assert (types::Value_Type<T>::is_supported, "Unsupported type");
  if (flag.empty ())
    throw std::invalid_argument ("Empty flag");
  auto *opt = new detail::Option_Type<T> (&value, flag, help_text);
  detail::options.emplace_back (opt);
}

static inline void
add (Option_Callable func, std::string_view flag, std::string_view help_text)
{
  if (flag.empty ())
    throw std::invalid_argument ("Empty flag");
  auto *opt = new detail::Option_Type<Option_Callable> (func, flag, help_text);
  detail::options.emplace_back (opt);
}

static inline void
add_help (Help_Function usage)
{
  detail::usage = usage;
}

static inline void
add_help ()
{
  add_help (detail::default_usage);
}

static inline void
parse (int argc, const char *const *argv, Collect_Arg collect_arg)
{
  using namespace std::literals;
  using namespace detail;

  const bool has_usage = bool (usage);

#if defined (_WIN32) && defined (FLAG_SHORTEN_WINDOWS_PROGRAM_PATH)
  // Powershell always gives the full path of the executable so
  // we use this option to allow for it to be shortened to just the
  // name of the executable (still including the .exe)
  const auto program = std::filesystem::path (argv[0]).filename ().string ();
  const char *const argv0 = program.c_str ();
#else
  const char *const argv0 = argv[0];
#endif

  int i;
  for (i = 1; i < argc; ++i)
    {
      if (argv[i][0] == '-')
        {
          const std::string_view arg = argv[i] + 1 + (argv[i][1] == '-');
          if (arg.empty ())
            {
              ++i;
              break;
            }
          if (has_usage && arg == "help")
            {
              usage (argv0);
              std::exit (0);
            }
          const std::size_t eq_pos = arg.find ('=');
          const std::string_view flag = arg.substr (0, eq_pos);
          // If this is empty now it will recieve the value of the following
          // argv-element in `detail::process_flag`.
          std::string_view value = (eq_pos == std::string_view::npos
                                    ? ""
                                    : arg.substr (eq_pos + 1));
          const auto result = process_flag (flag, value, i, argc, argv);
          if (result != Process_Result::Ok)
            {
              complain (argv0, result, flag, value, (argv[i][1] == '-'));
              if (has_usage)
                std::cerr << "Try '" << argv0
                          << " -help' for more information.\n";
              std::exit (1);
            }
        }
      else
        collect_arg (argv[i]);
    }

  // Collect remaining arguments if we broke out of the above loop
  for (; i < argc; ++i)
    collect_arg (argv[i]);
}

template <class T>
static inline void
parse (int argc, const char *const *argv, std::vector<T> &args)
{
  parse (argc, argv, [&args] (const char *arg) {
    args.emplace_back (arg);
  });
}

template <class T = const char *>
static inline std::vector<T>
parse (int argc, const char *const *argv)
{
  std::vector<T> args;
  parse (argc, argv, [&args] (const char *arg) {
    args.emplace_back (arg);
  });
  return args;
}

void
set_description (std::string_view description)
{
  detail::error_description = description;
}

}
