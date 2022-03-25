# flag-cpp

C++ command line argument parsing library loosely inspired by Go's [flag package](https://pkg.go.dev/flag).

## Argument format

For boolean flags:

```
-x
--x
```

For non-boolean flags:

```
-x v
--x v
-x=v
--x=v
```

Flags and non-flag arguments may be mixed, flag parsing only stops at the terminator `--`.

See the [Parsing section](#parsing) for more information about how non-flag arguments are handled.

## Usage

### Adding flags

With a value reference (see [Types section](#types)):
```cpp
int n;
flag::add (n, "n", "# of iterations");
```
With a callback:
```cpp
flag::add ([](const char *arg) {
  // ...
  return true;
}, "color", "colorize the output");
```

The second argument is the name of the flag.

The third argument is a description for the argument used for the default usage function.

If the flag uses a callback it should return whether the argument was valid or not. ([Argument errors](#argument-errors))

To print additional information about the error `flag::set_description ("...")` is used to print the given string after the argument error message.

Aliasing a flag:

```cpp
flag::add (recursive_flag, "R", "recursively do something");
flag::alias ("R", "recursive");
```

This maps `-recursive` to trigger the `-R` flag.

### Help flag

```cpp
void my_usage (const char *program_name) {}
```

A help flag is added by calling `flag::add_help (my_usage)` or `flag::add_help ()` to use the default usage function.

If either of these has been called, the `-help` flag gets caught manually and quit the program after calling the usage function.

It is possible to add a `help` flag with `flag::add` however if `flag::add_help` has been called such a flag will never be triggered.

If a help function has been added using `flag::add_help`, `Try 'program -help' for more information` gets printed after argument errors.

###  Parsing

There are 3 ways of calling `flag::parse`
```cpp
// 1)
flag::parse (argc, argv, [](const char*arg) { ... });
// 2)
std::vector<const char *> arguments;
flag::parse (argc, argv, arguments);
// 3)
std::vector<const char *> arguments = flags::parse (argc, argv);
```

These only differ in how they handle non-flag arguments:

- 1) Each arguments gets passed to the given function

- 2) Each argument gets added to the given vector using `emplace_back`

- 3) Each argument is added to a vector using `emplace_back` and it is returned

Since (2) and (3) use `emplace_back` the vector can have any value type that be constructed from a `const char *`.

If (3) uses a type other than `const char *` it has to be given explicitly: `flag::parse<T> (argc, argv)`.

### Types

By default these types are supported for flags:

- `bool`

- any signed integer type

- any unsigned integer type

- `float`, `double`, `long double`

- `const char *`, `std::string_view`, `std::string`

Additional types can be added by specializing the `flag::types::Value_Type` structure.

If is declared as:

```cpp
template <...>
struct Value_Type
{
  static constexpr bool is_supported = false;
  static constexpr const char *value_name = nullptr;
  static void convert_arg (const char *arg, T *value) {}
};

// Specialize specific type:
template <>
struct Value_Type<type> { ... };

// Specialize based on predicate:
template <class T>
struct Value_Type<T, std::enable_if_t<...>> { ... };
```

The `is_supported` value has to be set to `true` in order to accept the type.

`value_name` is used by the default help function, it may be `nullptr`.

The `convert_arg` function converts the argument and writes the result to the value pointer.
If the argument is in an invalid format an exception has to be used to report this error.

### The default help function

The default help function generates output in this form:

```
Usage: program ...
    -n int
        # of iterations
    -color COLOR
        colorize the output
    -f
        a boolean flag
    -R, -recursive
        recursively do something
```

Where `n` and `color` would have been added as above and `f` is added using a `bool` reference.

For flags using a non-boolean value reference the type is printed for the value.

`flag::help_show_types (true/false)` enables/disables printing of the type.

For callbacks or types for which the type name is specified as `nullptr` the flag name in uppercase is used.

### Argument errors

General format:

```
program: <error message>
<error description from flag::set_description>
Try 'program -help' for more information. <if flag::add_help has been called>
```

Errors arise from these situations:

- A flag does not exist

- A flag is missing a value

- A flag got a value but was not expecting one

- The value for a flag was invalid (callback returned `false`)

The program will terminate after printing the error message.

### Misc

Powershell on Windows will always give the full path of the executable as `argv[0]`, define FLAG_SHORTEN_WINDOWS_PROGRAM_PATH` to shorten this to just the filename in error messages and the usage function.
