## Tomaka17's lua wrapper

### What is this library?
This lua wrapper for C++ is a library which allows you to easily manage lua code. It was designed to be as simple as possible to use.

### Why this library?
You may wonder why I chose to create my own library while there as so many other libraries available. Well, none of them 100 % satisfied me.

Some are just basic wrappers (with functions to directly manipulate the stack), some others use an external executable to compute the list of functions of a class, and some others are just too complicated to use.

This library was designed to be very simple to use: you can write lua variables (with either a number, a string, a function, an array or a smart pointer to an object), read lua variables, call functions stored in lua variables, and of course execute lua code. That's all.

### How to use it?
This is a headers-only library.
Simply include the file `include/LuaContext.hpp` in your code, and you can use the `LuaContext` class.

All the other files in the source code are only for testing purposes.

### Why should I use it?
* very easy to use
* no macros (we are in 21st century!)
* no external program to run on your source code
* not intrusive, you don't need to change your existing code
* flexible, you aren't forced to use object-oriented code
* portable (doesn't use anything that is compiler-specific or platform-specific)
* can do everything other libraries can do

### Why should I not use it?
* requires support for [C++11](http://en.wikipedia.org/wiki/C%2B%2B11), the latest C++ standard
* requires [Boost](boost.org)
* polymorphic functions not (yet) supported

### Examples
All the examples are in C++, except of course the parameter passed to "executeCode".

#### Example 1: reading and writing variables

    LuaContext lua;
    lua.writeVariable("x", 5);
    lua.executeCode("x = x + 2;");
    std::cout << lua.readVariable<int>("x") << std::endl;

Prints `7`

All basic language types (`int`, `float`, `bool`, `char`, ...), plus `std::string`, can be read or written.

#### Example 2: giving functions to lua

    LuaContext lua;
    lua.writeVariable<std::function<int (int)>>("show", [](int v) { std::cout << v << std::endl; });
    lua.executeCode("show(3);");
    lua.executeCode("show(8);");

Prints `3` and `8`

You can also write function pointers or functors using `writeVariable`. The function's parameters and return type must be supported by `readVariable` and `writeVariable`.

`writeVariable` supports `std::function` and basic function pointers or references.


#### Example 3: writing custom types

    class Object {
    public:
     Object() : value(10) {}
     
     void  increment() { std::cout << "incrementing" << std::endl; value++; } 
     
     int value;
    };
    
    LuaContext lua;
    lua.registerFunction("increment", &Object::increment);
    
    lua.writeVariable("obj", Object{});
    lua.executeCode("obj:increment();");

Prints `incrementing`.

In addition to basic types and functions, you can pass any object to `writeVariable`, including raw pointers, std::unique_ptr and std::shared_ptr. The object will then be copied into lua.
Before doing so, you can call "registerFunction" so lua scripts can call the object's functions just like in the example above.

`readVariable` can be used to read a copy of the object.

If you don't want to manipulate copies, you should write and read pointers instead of plain objects. Function that have been registered for a type also work with raw pointers and `shared_ptr`s of this type.


#### Example 4: reading lua code from a file

    LuaContext lua;
    lua.executeCode(std::ifstream("script.lua"));

This simple example shows that you can easily read lua code (including pre-compiled) from a file.

If you write your own derivate of std::istream (for example a decompressor), you can of course also use it.
Note however that `executeCode` will block until it reaches eof. So if you use a custom derivate of std::istream which awaits for data, the execution will be blocked.


#### Example 5: executing lua functions

    LuaContext lua;

    lua.executeCode("foo = function(i) return i + 2 end");

    const auto function = lua.readVariable<std::function<int (int)>>("foo");
    std::cout << function(3) << std::endl;

Prints `5`.

`readVariable` also supports `std::function`. This allows you to read any function, even the functions created by lua.

*Warning*: calling the function after the LuaContext has been destroyed leads to undefined behavior (and likely to a crash).


#### Example 6: handling arrays and more complex types

`writeVariable` and `readVariable` support `std::vector`, `std::map`, `std::unordered_map`, `boost::optional` and `boost::variant`.
This allows you to do more complicated things, like this:
    
    LuaContext lua;

    lua.writeVariable("a",
        std::vector< std::pair< boost::variant<int,std::string>, boost::variant<bool,float> > >
        {
            { "test", true },
            { 2, 6.4f },
            { "hello", 1.f },
            { "world", -7.6f },
            { 18, false }
        }
    );

    std::cout << lua.executeCode<bool>("return a.test") << std::endl;
    std::cout << lua.executeCode<float>("return a[2]") << std::endl;

Prints `true` and `6.4`

A `std::vector` of `std::pair`s is automatically treated as an array by lua. Same for `std::map` and `std::unordered_map`.
`boost::variant` allows you to read a value whose type is not precisely known in advance.

#### Example 7: returning multiple values

    LuaContext lua;
    
    lua.writeVariable<std::function<std::tuple<int,std::string> (int,int,int)>>("f1", [](int a, int b, int c) { return std::make_tuple(a+b+c, "test"); });
    lua.executeCode("a,b = f1(1, 2, 3);");
    
    std::cout << lua.readVariable<int>("a") << std::endl;
    std::cout << lua.readVariable<std::string>("b") << std::endl;

Prints `6` and `test`

A function can return multiple values by returning a tuple.
In this example we return at the same time an int and a string.

#### Example 8: destroying a lua variable

    LuaContext lua;
    
    lua.writeVariable("a", 2);

    lua.writeVariable("a", nullptr);        // destroys a

The C++ equivalent for `nil` is `nullptr`.


### Compilation
This code uses new functionalities from [C++11](http://en.wikipedia.org/wiki/C%2B%2B11).

[![build status](https://secure.travis-ci.org/Tomaka17/luawrapper.png)](http://travis-ci.org/Tomaka17/luawrapper)

Does it compile on:
  * Visual C++ 2012 or below: no
  * Visual C++ 2013: yes
  * GCC 4.7.2: doesn't compile because of known bug in the compiler
  * GCC 4.8: not tested, but should probably
