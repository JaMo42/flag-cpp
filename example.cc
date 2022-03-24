#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include "flag.hh"
using namespace std::literals;

struct my_custom_type
{
  std::string key;
  std::string value;
};

namespace flag { namespace types {

// Using predicate instead of specific type:
//   template <class T>
//   struct Value_Type<T, std::enable_if_t<...>>
//   { ... };

template <>
struct Value_Type<my_custom_type>
{
  // Needs to be set to true for all specializations or calling flag::add will
  // fail a static assertion
  static constexpr bool is_supported = true;
  // Text for the type in the help message:
  // ...
  //     -foo value_name
  //         Some value
  static constexpr const char *value_name = "key:value";

  // Responsible for converting the argument and writing the result to the
  // given pointer
  static void convert_arg (const char *arg_, my_custom_type *value)
  {
    const char *exception_msg = "my_custom_type must be of format 'key:value'";
    std::string_view arg = arg_;
    const std::size_t colon_pos = arg.find (':');
    if (colon_pos == std::string_view::npos)
      throw std::invalid_argument (exception_msg);
    const std::string_view key = arg.substr (0, colon_pos);
    const std::string_view val = arg.substr (colon_pos + 1);
    if (key.empty () || val.empty ())
      throw std::invalid_argument (exception_msg);
    value->key.assign (key);
    value->value.assign (val);
  }
};

}}

// Example usage function
[[maybe_unused]]
static void
usage (const char *program_name)
{
  std::cerr << "Usage: " << program_name << " [OPTION]... [ARGUMENT]...\n";
  std::cerr << "Does something with the ARGUMENTs.\n";
}

int
main (const int argc, const char **argv)
{
  int n = 5;
  std::string str = "baz";
  bool long_flag = false;
  double scale = 1.0;
  my_custom_type x = {"<none>", "<none>"};
  bool boolean;

  // Boolean
  flag::add (long_flag, "l", "Long listing");
  // Integer
  flag::add (n, "n", "# of iterations");
  // String
  flag::add (str, "bar", "a string");
  // Float
  flag::add (scale, "scale", "scale for something");
  // Simple callable
  flag::add ([](const char *arg) {
      std::cout << "foo: " << arg << std::endl;
      return true;
    }, "foo", "Print value");
  // Callable only accepting specific values
  flag::add ([] (const char *arg_) {
      const std::string_view arg = arg_;
      if ("yes"sv == arg || "always"sv == arg || "force"sv == arg
          || "no"sv == arg || "never"sv == arg || "none"sv == arg
          || "auto"sv == arg || "tty"sv == arg || "if-tty"sv == arg)
        {
          // ...
          return true;
        }
      // Gets printed after option error message
      flag::set_description ("Valid arguments are:\n"
                             "  - ‘always’, ‘yes’, ‘force’\n"
                             "  - ‘never’, ‘no’, ‘none’\n"
                             "  - ‘auto’, ‘tty’, ‘if-tty’");
      return false;
    }, "color", "colorize the output");
  // Callable with unicode name
  flag::add ([](const char *) {
      return true;
    }, "플래그", "Flag with unicode name");
  // Custom type
  flag::add (x, "x", "x");
  // Empty help text
  flag::add (boolean, "no-help", "");
#if 0
  // Empty flag
  flag::add (boolean, "", "Empty flag");
#endif

#if 0
  // No help function
#else
#if 1
  // Default help function
  flag::add_help ();
#if 1
  flag::help_show_types (false);
#endif
#else
  // Custom help function
  flag::add_help (usage);
#endif
#endif

  std::vector<const char *> args = flag::parse (argc, argv);

  std::cout << "l: " << (long_flag ? "yes" : "no") << std::endl;
  std::cout << "n: " << n << std::endl;
  std::cout << "bar: " << str << std::endl;
  std::cout << "scale: " << scale << std::endl;
  std::cout << "x: '" << x.key << ':' << x.value << '\'' << std::endl;

  if (!args.empty ())
    {
      std::cout << "Arguments: `";
      std::cout << args[0];
      if (args.size () > 1)
        {
          for (std::size_t i = 1; i < args.size (); ++i)
            std::cout << "`, `" << args[i];
        }
      std::cout << '`' << std::endl;
    }
}
