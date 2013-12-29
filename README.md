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
You can also just copy-paste it into your own source code if you don't want to modify your include paths.

The `include/misc/exception.hpp` file is required only for VC++.

All the files outside of the `include` directory are only for testing purposes and can be ignored.

### Why should I use it?
* very easy to use
* no macros (we are in the 21st century!)
* no external program to run on your source code
* not intrusive, you don't need to change the layout of your functions or classes
* you aren't forced to use object-oriented code
* portable (doesn't use anything that is compiler-specific or platform-specific)
* can do everything other libraries can do

### Why should I not use it?
* requires support for [C++11](http://en.wikipedia.org/wiki/C%2B%2B11), the latest C++ standard
* requires [Boost](http://boost.org) (headers only)
* inheritance not supported (and won't be supported until some reflection is added to the C++ language)

### I have the old (2010) version of your library, what did change?
* you need an up-to-date compiler now
* you now need some headers-only libraries from [Boost](http://boost.org)
* breaking change: `LuaContext` is no longer in the `Lua` namespace
* breaking change: you can't pass directly lambdas to `writeVariable` anymore, use `writeFunction` instead or convert them to `std::function`
* breaking change: the functions `clearVariable`, `doesVariableExist`, `writeEmptyArray` and `callLuaFunction` no longer exist, but you can reproduce their effect with `writeVariable` and `readVariable`
* a lot of features have been added: lua arrays, polymorphic functions, etc.
* the implementation is really a lot cleaner, and probably faster and with less bugs

### Documentation
All the examples below are in C++, except the parameter passed to `executeCode` which is Lua code.

#### Reading and writing variables

    LuaContext lua;
    lua.writeVariable("x", 5);
    lua.executeCode("x = x + 2;");
    std::cout << lua.readVariable<int>("x") << std::endl;

Prints `7`.

All basic language types (`int`, `float`, `bool`, `char`, ...), plus `std::string`, can be read or written. `enum`s can also be read or written but are turned into numbers.

An exception is thrown if you try to read a value of the wrong type or if you try to read a non-existing variable.
If you don't know the type of a variable in advance, you can read a `boost::variant`. If you want to read a variable but don't know whether it exists, you can read a `boost::optional`. More informations about this below.

#### Writing functions

    void show(int value) {
        std::cout << value << std::endl;
    }

    LuaContext lua;
    lua.writeVariable("show", &show);
    lua.executeCode("show(5);");
    lua.executeCode("show(7);");

Prints `5` and `7`.

    LuaContext lua;
    lua.writeVariable("show", std::function<void (int)>{[](int v) { std::cout << v << std::endl; }});
    lua.executeCode("show(3);");
    lua.executeCode("show(8);");

Prints `3` and `8`.

`writeVariable` supports both `std::function` and native function pointers or references.

The function's parameters and return type are handled as if they were read and written by `readVariable` and `writeVariable`.
If there are not enough parameters or if parameters are of the wrong type, then a Lua error is triggered.

If you pass a function object with a single operator(), you can also auto-detect the function type using `writeFunction`:

    LuaContext lua;
    lua.writeFunction("show", [](int v) { std::cout << v << std::endl; });

`writeFunction` also accepts an explicit template argument if the type is ambiguous:

    // note: this is C++14
    const auto increment = [](auto v) { return v + 1; };
    
    LuaContext lua;
    lua.writeFunction<int (int)>("incrementInt", increment);
    lua.writeFunction<double (double)>("incrementDouble", increment);
    
Using `writeFunction` is both easier and faster than `writeVariable` in case of function objects, and you should prefer this method.

#### Writing custom types

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

    std::cout << lua.readVariable<Object>("obj").value << std::endl;

Prints `incrementing` and `11`.

In addition to basic types and functions, you can also pass any object to `writeVariable`.
The object will then be moved into Lua by calling its copy or move constructor. Remember that since they are not a native type, attempting to copy the object into another variable using Lua code will instead make the two variables point to the same object.

If you want to call an object's member function, you must register it with `registerFunction`, just like in the example above.
It doesn't matter whether you call `registerFunction` before or after writing the objects, it works in both cases.

If you pass a plain object type as template parameter to `readVariable` (for example `readVariable<Object>`, juste like in the code above), then it will read a copy of the object. However if you pass a reference (for example `readVariable<Object&>`), then a reference to the object held by Lua will be returned.

You also have the possibility to write and read pointers instead of plain objects. Raw pointers, `unique_ptr`s and `shared_ptr`s are also supported. Functions that have been registered for a type also work with these.

Note however that inheritance is not supported.
You need to register all of a type's functions, even if you have already registered the functions of its parents.


#### Reading lua code from a file

    LuaContext lua;
    lua.executeCode(std::ifstream{"script.lua"});

This simple example shows that you can easily read lua code (including pre-compiled) from a file.

If you write your own derivate of `std::istream` (for example a decompressor), you can of course also use it.
Note however that `executeCode` will block until it reaches eof. You should remember this if you use a custom derivate of `std::istream` which awaits for data.


#### Executing lua functions

    LuaContext lua;

    lua.executeCode("foo = function(i) return i + 2 end");

    const auto function = lua.readVariable<std::function<int (int)>>("foo");
    std::cout << function(3) << std::endl;

Prints `5`.

`readVariable` also supports `std::function`. This allows you to read any function, even the functions created by lua.
Note however that calling the function after the LuaContext has been destroyed leads to undefined behavior (and likely to a crash), even when the function was originally a C++ function.

The only types that are supported by `writeVariable` but not by `readVariable` are native function pointers and `unique_ptr`s, for obvious reasons.


#### Polymorphic functions

If you want to read a value but don't know in advance whether it is of type A or type B, `writeVariable` and `readVariable` also support `boost::variant`.

    LuaContext lua;

    lua.writeFunction("foo", 
        [](boost::variant<std::string,bool> value)
        {
            if (value.which() == 0) {
                std::cout << "Value is a string: " << boost::get<std::string>(value);
            } else {
                std::cout << "Value is a bool: " << boost::get<bool>(value);
            }
        }
    );

    lua.executeCode("foo(\"hello\")");
    lua.executeCode("foo(true)");

Prints `Value is a string: hello` and `Value is a bool: true`.

See the documentation of [`boost::variant`](http://www.boost.org/doc/libs/release/doc/html/variant.html).


#### Variadic-like functions

If you want functions that take a varying number of parameters, you can have some parameters as `boost::optional`s.

    LuaContext lua;

    lua.writeFunction("foo", 
        [](int param1, boost::optional<int> param2, boost::optional<int> param3)
        {
            if (param3) {
                std::cout << "3 parameters" << std::endl;
            } else if (param2) {
                std::cout << "2 parameters" << std::endl;
            } else {
                std::cout << "1 parameter" << std::endl;
            }
        }
    );

    lua.executeCode("foo(7)");
    lua.executeCode("foo(7, 7)");
    lua.executeCode("foo(7, 7, 7)");

Just like C/C++ functions with default parameter values, you have to put the `boost::optional`s at the end of the parameters list.

This means that for example:

    lua.writeFunction("foo", 
        [](boost::optional<int> param1, int param2) {}
    );

    lua.executeCode("foo(7, 7)");

This code will trigger a Lua error because the `foo` function requires at least two parameters.


#### Handling lua arrays

`writeVariable` and `readVariable` support `std::vector` of `std::pair`s, `std::map` and `std::unordered_map`.
This allows you to read and write arrays. Combined with `boost::variant`, this allows you to write real polymorphic arrays.

If you have a `std::vector` which contains `std::pair`s, it will be considered as an associative array, where the first member of the pair is the key and the second member is the value.
    
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

Prints `true` and `6.4`.

You can also use `readVariable` and `writeVariable` to directly read or write inside an array.
    
    std::cout << lua.readVariable("a", "test") << std::endl;
    std::cout << lua.readVariable("a", 2) << std::endl;
    
You can also write an empty array, like this:

    LuaContext lua;
    lua.writeVariable("a", LuaEmptyArray);
    
`LuaEmptyArray` is a global variable, and trying to write its value in a Lua variable instead writes an empty array (note: the reason why it is not a member of `LuaContext` is because of programming issues).

Remember that you can create recursive variants, so you can read arrays which contain arrays which contain arrays, and so forth.

    typedef typename boost::make_recursive_variant
        <
            std::string,
            double,
            bool,
            std::vector<std::pair<
                boost::variant<std::string,double,bool>,
                boost::recursive_variant_
            >>
        >::type
            AnyValue;

    lua.readVariable<AnyValue>("something");

This `AnyValue` can store any lua value, except functions and custom objects.

#### Returning multiple values

    LuaContext lua;
    lua.writeFunction("f1", [](int a, int b, int c) { return std::make_tuple(a + b + c, "test"); });

    lua.executeCode("a, b = f1(1, 2, 3);");
    
    std::cout << lua.readVariable<int>("a") << std::endl;
    std::cout << lua.readVariable<std::string>("b") << std::endl;

Prints `6` and `test`.

Lua supports functions that return multiple values at once. A C++ function can do so by returning a tuple.
In this example we return at the same time an int and a string.

Tuples are only supported when returning as a return value for a function. Attempting to write or read a tuple with `writeVariable` or `readVariable` would lead to a compilation error.


#### Destroying a lua variable

    LuaContext lua;
    
    lua.writeVariable("a", 2);
    
    lua.writeVariable("a", nullptr);        // destroys a

The C++ equivalent for `nil` is `nullptr`.

Note that `nullptr` has its own type, which is different from `0` and `NULL`.


#### Custom member functions

In example 3, we saw that you can register functions for a given type with `registerFunction`.

But you can also register functions that don't exist in the class.

    struct Foo { int value; };

    LuaContext lua;
    
    lua.registerFunction<void (Foo::*)(int)>("add", [](Foo& object, int num) { object.value += num; });

    lua.writeVariable("a", Foo{5});
    lua.executeCode("a:add(3)");

    std::cout << lua.readVariable<Foo>("a").value;  // 8
    
The template parameter must be a pointer-to-member-function type. The callback must take as first parameter a reference to the object.

There is an alternative syntax if you want to register a function for a pointer type, because it is illegal to write `void (Foo*::*)(int)`.

    lua.registerFunction<Foo, void (int)>("add", [](Foo& object, int num) { object.value += num; });


#### Member objects and custom member objects

You can also register member variables for objects.

    struct Foo { int value; };

    LuaContext lua;

    lua.registerMember("value", &Foo::value);

    lua.writeVariable("a", Foo{});

    lua.executeCode("a.value = 14");
    std::cout << lua.readVariable<Foo>("a").value;  // 14

Just like `registerFunction`, you can register virtual member variables.

The second parameter is a function or function object that is called in order to read the value of the variable.
The third parameter is a function or function object that is called in order to write the value. The third parameter is optional.
    
    lua.registerMember<bool (Foo::*)>("value_plus_one",
        [](Foo& object) -> int {
            // called when lua code wants to read the variable
            return object.value + 1;
        },
        [](Foo& object, int value_plus_one) {
            // called when lua code wants to modify the variable
            object.value = value_plus_one - 1;
        }
    );

    lua.writeVariable("a", Foo{8});
    std::cout << lua.executeCode<int>("return a.value_plus_one");    // 9
    
    lua.writeVariable("b", Foo{1});
    lua.executeCode("b.higher_than_five = 5");
    std::cout << lua.readVariable<Object>("b").value;               // 4

The alternative syntax also exists.

    lua.registerMember<Foo, bool>("higher_than_five", ...same as above...);

Finally you can register functions that will be called by default when a non-existing variable is read or written.
The syntax is the same than above, except that the callbacks take an extra `name` parameter.
    
    lua.registerMember<boost::variant<int,bool> (Foo::*)>(
        [](Foo& object, const std::string& memberName) -> int {
            std::cout << "Trying to read member " << memberName << " of object" << std::endl;
            return 1;
        },
        [](Foo& object, const std::string& memberName, int value) {
            std::cout << "Trying to write member " << memberName << " of object with value " << value << std::endl;
        }
    );
    
Remember that you can return `std::function` from the read callback, allowing you to create real virtual objects.


#### Exception safety

You can safely throw exceptions from inside functions called by Lua.
They are considered as errors by Lua, and if not handled they will be propagated outside.

    lua.writeFunction("test", []() { throw std::runtime_error("Problem"); });
    
    try {
        lua.executeCode("test()");
    } catch(const std::runtime_error& e) {
        std::cout << e.what() << std::endl;     // prints "Problem"
    }


### Compilation
This code uses new functionalities from [C++11](http://en.wikipedia.org/wiki/C%2B%2B11).

[![build status](https://secure.travis-ci.org/Tomaka17/luawrapper.png)](http://travis-ci.org/Tomaka17/luawrapper)

Does it compile on:
  * Visual C++ 2012 or below: no
  * Visual C++ 2013: yes
  * gcc 4.7.2: doesn't always compile because of known bug in the compiler
  * gcc 4.8.1: yes
  * clang 3.2: yes
