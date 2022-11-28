#include <string_view>
#include <vector>
#include <functional>
#include <memory>
#include <limits>
#include <algorithm>
#include <iterator>
#include <cstdlib>
#include <cctype>
#include <iostream>
#include <map>
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
inline std::map<std::string_view, std::string_view> aliases = {};
inline Help_Function usage = nullptr;
inline std::string_view error_description = "";
inline bool help_show_types = true;

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
      if (!aliases.empty ())
        {
          // TODO: support multiple aliases for the same flag
          const auto flag = option->flag ();
          const auto alias_it = std::find_if (aliases.begin (), aliases.end (),
                                              [&flag](const auto &check) {
                                                return check.second == flag;
                                              });
          if (alias_it != aliases.end ())
            std::cout << ", -" << alias_it->first;
        }
      if (help_show_types && option->takes_value ())
        {
          std::cout << ' ';
          print_type_name (option.get ());
        }
      std::cout << '\n';
      if (!option->help_text ().empty ())
        std::cout << "        " << option->help_text () << '\n';
    }
}

static Option_Base *
find_option (std::string_view flag)
{
  auto do_find = [](std::string_view flag) {
    return std::find_if (options.begin (), options.end (),
                         [&flag] (const auto &test) {
                           return test->operator== (flag);
                         });
  };
  auto it = do_find (flag);
  if (it == options.end ())
    {
      if (aliases.empty ())
        return nullptr;
      else
        {
          const auto alias_it = aliases.find (flag);
          if (alias_it == aliases.end ()
              || (it = do_find (alias_it->second)) == options.end ())
            return nullptr;
        }
    }
  return it->get ();
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
  Option_Base *option = find_option (flag);
  if (option == nullptr)
    return Process_Result::Invalid_Option;
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

// https://en.wikipedia.org/wiki/Jaro%E2%80%93Winkler_distance#Jaro_similarity
static double
jaro_similarity (std::string_view a, std::string_view b)
{
  // Note: This byte-wise processing does work for UTF-8 text, however
  //       the text may need to be closer in appearance than with ascii
  //       as characters using multiple bytes are treated the same as multiple
  //       characters and therefore get a higher similarity penalty if they are
  //       not equal.

  // Trivial cases
  if (a.empty () && b.empty ())
    return 1.0;
  else if (a.empty () || b.empty ())
    return 0.0;
  else if (a.size () == 1 && b.size () == 1)
    return a[0] == b[0] ? 1.0 : 0.0;

  // Distance a character can have from a position and still be considered matching.
  const auto match_range = std::max (a.size (), b.size ()) / 2 - 1;
  // Keeps tracl of characters in B we have already matched
  auto used = std::vector (b.size (), false);
  // Position of last character mached in B, used for order checking
  auto b_pos = std::size_t {};

  auto matches = 0.0;
  auto transpositions = 0.0;

  for (std::size_t i = 0; i < a.size (); ++i)
    {
      const auto c = a[i];
      const auto lo = i > match_range ? i - match_range : 0;
      const auto hi = std::min (i + match_range, b.size () - 1);
      for (std::size_t j = lo; j <= hi; ++j)
        {
          const auto d = b[j];
          if (c == d && !used[j])
            {
              used[j] = true;
              ++matches;
              if (j < b_pos)
                ++transpositions;
              b_pos = j;
              break;
            }
        }
    }

  if (matches == 0.0)
    return 0.0;
  else
    return 0.333 * ((matches / a.size ())
                    + (matches / b.size ())
                    + ((matches - transpositions) / matches));
}

// https://en.wikipedia.org/wiki/Jaro%E2%80%93Winkler_distance#Jaro%E2%80%93Winkler_similarity
static inline double
jaro_winkler_similarity (std::string_view a, std::string_view b)
{
  constexpr double SCALING_FACTOR = 0.1;
  const auto sim_j = jaro_similarity (a, b);
  const auto l = std::distance (
    a.begin (),
    std::mismatch (a.begin (), a.end (), b.begin (), b.end ()).first
  );
  const auto sim_w = sim_j - l * SCALING_FACTOR * (1.0 - sim_j);
  return sim_w <= 1.0 ? sim_w : 1.0;
}

static inline void
look_for_similar (std::string_view dash, std::string_view flag)
{
  constexpr double THRESHHOLD = 0.8;
  std::string_view best_match = {};
  double most_similar = 0.0;
  for (const auto &option : options)
    {
      const auto opt = option->flag ();
      const auto sim = jaro_winkler_similarity (opt, flag);
      if (sim > THRESHHOLD && sim > most_similar)
        {
          best_match = opt;
          most_similar = sim;
        }
    }
  if (not best_match.empty ())
    std::cerr << ", did you mean " << dash << best_match << "?";
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
        look_for_similar (dash, flag);
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
add (T &value, std::string_view flag, std::string_view help_text = "")
{
  using namespace std::literals;
  static_assert (types::Value_Type<T>::is_supported, "Unsupported type");
  if (flag.empty ())
    throw std::invalid_argument ("Empty flag");
  auto *opt = new detail::Option_Type<T> (&value, flag, help_text);
  detail::options.emplace_back (opt);
}

static inline void
add (Option_Callable func, std::string_view flag, std::string_view help_text = "")
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
help_show_types (bool v)
{
  detail::help_show_types = v;
}

static inline void
alias (std::string_view flag, std::string_view alias)
{
  detail::aliases[alias] = flag;
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
