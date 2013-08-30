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

    Lua::LuaContext lua;
    lua.writeVariable("x", 5);
    lua.executeCode("x = x + 2;");
    std::cout << lua.readVariable<int>("x") << std::endl;

Prints 7

Supported types for reading and writing include: int, long, float, double, bool and std::string.

#### Example 2: giving functions to lua

    Lua::LuaContext lua;
    lua.writeVariable("show", [](int v) { std::cout << v << std::endl; });
    lua.executeCode("show(3);");
    lua.executeCode("show(8);");

Prints 3 and 8

You can also write function pointers or functors using "writeVariable". You can't read them with "readVariable" (will give a compilation error). The types of the function's parameters must be supported by "readVariable", and the return type of the function must be supported by "writeVariable".


#### Example 3: writing custom types

    class Object {
    public:
     Object() : value(10) {}
     
     void  increment() { std::cout << "incrementing" << std::endl; value++; } 
     
     int value;
    };
    
    Lua::LuaContext lua;
    lua.registerFunction("increment", &Object::increment);
    
    lua.writeVariable("obj", std::unique_ptr<Object>(new Object()));
    lua.executeCode("obj:increment();");

Prints "incrementing".

In addition to basic types and functions, you can pass any object to `writeVariable`, including raw pointers, std::unique_ptr and std::shared_ptr.
Before doing so, you can call "registerFunction" so lua scripts can call the object's functions just like in the example above.


#### Example 4: reading back custom types
(we reuse the code from previous example without the call to executeCode)

    lua.readVariable<std::shared_ptr<Object>>("obj")->increment();

    lua.writeVariable("getVal", [](std::shared_ptr<Object> g) { return g->value; });

    lua.executeCode("value = getVal(obj);");

    std::cout << lua.readVariable<int>("value") << std::endl;

Prints "incrementing" (first line) and "11" (last line).

First line shows that you can read std::shared_ptr using "readVariable". You can't read std::unique_ptrs since we don't want to remove the object from lua. Note that for the moment this library doesn't check whether the types are matching.

At line 3, we write a function named "getVal" which takes as parameter a shared_ptr of type "Game" and returns its "value" member variable. Then we call this function inside lua with our "game" object as parameter. This demonstrates what I wrote above: the available types for functions parameters are the same as the ones supported by "readVariable".

#### Example 5: reading lua code from a file

    Lua::LuaContext lua;
    lua.executeCode(std::ifstream("script.lua"));

This simple example shows that you can easily read lua code (including pre-compiled) from a file.

If you write your own derivate of std::istream (for example a decompressor), you can of course also use it.
Note however that `executeCode` will block until it reaches eof. So if you use a custom derivate of std::istream which waits for data, the execution will be blocked.


#### Example 6: using arrays
_To do_

#### Example 7 : returning multiple values

    Lua::LuaContext lua;
    
    lua.writeVariable("f1", [](int a, int b, int c) { return std::make_tuple(a+b+c, "test"); });
    lua.executeCode("a,b = f1(1, 2, 3);");
    
    std::cout << lua.readVariable<int>("a") << std::endl;
    std::cout << lua.readVariable<std::string>("b") << std::endl;

Prints `6` and `test`

A function can return multiple values by returning a tuple.
In this example we return at the same time an int and a string.


### Compilation
This code uses new functionalities from [C++11](http://en.wikipedia.org/wiki/C%2B%2B11).

Does it compile on:
  * Visual C++ 2012 or below: no
  * Visual C++ 2013: yes
  * GCC 4.8: no tested yet
