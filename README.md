## Tomaka's lua wrapper - under new maintainership
This Lua Wrapper was authored by Pierre Krieger. After a tremendous amount of work on the code, Pierre has moved on to other things. Thank you Pierre! PowerDNS depends on Luawrapper, so we have committed to keep the code compiling with modern compilers, fix bugs and perhaps do the odd enhancement here and there.

### What is this library?
This lua wrapper for C++ is a library which allows you to easily manage lua code. It was designed to be as simple as possible to use.

### Why this library?
You may wonder why I chose to create my own library while there as so many other libraries available. Well, none of them 100 % satisfied me.

Some are just basic wrappers (with functions to directly manipulate the stack), some others use an external executable to compute the list of functions of a class, and some others are just too complicated to use.

This library was designed to be very simple to use: you can write Lua variables (with either a number, a string, a function, an array or any object), read Lua variables, and of course execute Lua code. That's all.

### How to use it?
This is a headers-only library.
Simply add the `include` directory to your include paths, and you can use the `LuaContext` class.
You can also just copy-paste the files into your own project if you don't want to modify your include paths.

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
* uses exceptions and RTTI

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
    std::cout << lua.readVariable<int>("x") << std::endl;       // prints 7

Reading and writing global variables of the Lua context can be done with `writeVariable` and `readVariable`.

All basic language types (`int`, `float`, `bool`, `char`, ...), plus `std::string`, can be read or written. `enum`s can also be read or written but are turned into numbers.

`readVariable` requires a template parameter which tells the type of the variable that should be read. A `WrongTypeException` is thrown if Lua can't convert the variable to the type you requested, or if you try to read a non-existing variable.

If you don't know the type of a variable in advance, you can read a `boost::variant`. If you want to read a variable but don't know whether it exists, you can read a `boost::optional`. More informations about this below.

#### Writing functions

Writing a function is as easy as writing a value:

    void show(int value) {
        std::cout << value << std::endl;
    }

    LuaContext lua;
    lua.writeVariable("show", &show);
    lua.executeCode("show(5)");     // calls the show() function in C++, which prints 5
    lua.executeCode("show(7)");     // prints 7

`writeVariable` also supports `std::function`s:

    std::function<void (int)> f = [](int v) { std::cout << v << std::endl; };

    LuaContext lua;
    lua.writeVariable("show", f);
    lua.executeCode("show(3)");     // prints 3
    lua.executeCode("show(8)");     // prints 8

The function's parameters and return type are handled as if they were read and written by `readVariable` and `writeVariable`, which means that all types supported by these functions can also be used as function parameters or return type.

If some Lua code attempts to call a function with fewer parameters or with parameters of the wrong type, then a Lua error is triggered.

Converting a lambda function to a `std::function` is costly. Instead you can use `writeFunction`:
    
    // note: this is C++14
    const auto increment = [](auto v) { return v + 1; }

    LuaContext lua;
    lua.writeFunction<int (int)>("incrementInt", increment);
    lua.writeFunction<double (double)>("incrementDouble", increment);

If you pass a function object with a single operator(), you can also skip the template parameter of `writeFunction`, in which case the type will be automatically detected.
This is the easiest way to write a lambda.

    LuaContext lua;
    lua.writeFunction("show", [](int v) { std::cout << v << std::endl; });


#### Executing code

Executing Lua code is done with the `executeCode` function.
By default it takes no template parameter, but if the code returns a value, you can pass one to request the specific type to be returned.

    LuaContext lua;
    std::cout << lua.executeCode<int>("return 1 + 2") << std::endl;     // prints 3

A `SyntaxErrorException` is thrown in case of a parse error in the code. An `ExecutionErrorException` is thrown in case of an unhandled Lua error during execution.

`executeCode` also accepts `std::istream` objects. You can easily read lua code (including pre-compiled) from a file like this:

    LuaContext lua;
    lua.executeCode(std::ifstream{"script.lua"});

If you write your own derivate of `std::istream` (for example a decompressor), you can of course also use it.
Note however that `executeCode` will block until it reaches eof. You should remember this if you use a custom derivate of `std::istream` which awaits for data.


#### Exception safety

You can safely throw exceptions from inside functions called by Lua and they will be turned automatically into Lua errors.

If the error is not handled by the Lua code, then it will propagate outside of `executeCode`. An `ExecutionErrorException` will be thrown by `executeCode` with the exception thrown by the callback attached as a nested exception.
    
    lua.writeFunction("test", []() { throw std::runtime_error("Problem"); });
    
    try {
        lua.executeCode("test()");

    } catch(const ExecutionErrorException& e) {
        std::cout << e.what() << std::endl;           // prints an error message

        try {
            std::rethrow_if_nested(e);
        } catch(const std::runtime_error& e) {
            // e is the exception that was thrown from inside the lambda
            std::cout << e.what() << std::endl;       // prints "Problem"
        }
    }


#### Writing custom types

    class Object {
    public:
      Object() : value(10) {}
      
      void increment() { std::cout << "incrementing" << std::endl; value++; } 
      
      int value;
    };
    
    LuaContext lua;
    lua.registerFunction("increment", &Object::increment);
    
    lua.writeVariable("obj", Object{});
    lua.executeCode("obj:increment();");

    std::cout << lua.readVariable<Object>("obj").value << std::endl;

Prints `incrementing` and `11`.

In addition to basic types and functions, you can also pass any object to `writeVariable`. The object will be moved into Lua by calling its copy or move constructor.

Remember that since they are not a native type, you can't clone an object from within Lua. Attempting to copy the object into another variable will instead make the two variables point to the same object.

If you want to call an object's member function, you must register it with `registerFunction`, just like in the example above.
It doesn't matter whether you call `registerFunction` before or after writing the objects, it works in both cases.

If you pass a plain object type as template parameter to `readVariable` (for example `readVariable<Object>`, juste like in the code above), then it will read a copy of the object. However if you pass a reference (for example `readVariable<Object&>`), then a reference to the object held by Lua will be returned.

You also have the possibility to write and read pointers instead of plain objects. Raw pointers, `unique_ptr`s and `shared_ptr`s are also supported (`unique_ptr`s can't be read for obvious reasons). Functions that have been registered for a type also work if you write pointers to this type.

Note however that inheritance is not supported.
You need to register all of a type's functions, even if you have already registered the functions of its parents. You can't write an object and attempt to read a reference to its parent type either, this would trigger an exception.


#### Executing Lua functions

    LuaContext lua;

    lua.executeCode("foo = function(i) return i + 2 end");

    const auto function = lua.readVariable<std::function<int (int)>>("foo");
    std::cout << function(3) << std::endl;

Prints `5`.

`readVariable` also supports `std::function`. This allows you to read any function, even the functions created by lua.
Note however that calling the function after the LuaContext has been destroyed leads to undefined behavior (and likely to a crash), even when the function was originally a C++ function.

If you want to get maximum performances, you can also ask `readVariable` to read a `LuaContext::LuaFunctionCaller<int (int)>` instead of a `std::function<int (int)>`.
When `readVariable` returns a `std::function`, in fact it is just a wrapping around a `LuaFunctionCaller`.


#### Polymorphic functions

If you want to read a value but don't know in advance whether it is of type A or type B, `writeVariable` and `readVariable` also support `boost::variant`.

    LuaContext lua;

    auto value = lua.readVariable<boost::variant<std::string, bool>>("value");

    if (const auto strValue = boost::get<std::string>(&value))
        ...
    else if (const auto boolValue = boost::get<bool>(&value))
        ...

This can be used to create polymorphic functions, ie. functions that can take different types of arguments.

    LuaContext lua;

    lua.writeFunction("foo", 
        [](boost::variant<std::string, bool> value)
        {
            if (value.which() == 0) {
                std::cout << "Value is a string: " << boost::get<std::string>(value);
            } else {
                std::cout << "Value is a bool: " << boost::get<bool>(value);
            }
        }
    );

    lua.executeCode("foo(\"hello\")");      // prints "Value is a string: hello"
    lua.executeCode("foo(true)");           // prints "Value is a bool true"

See the documentation of [`boost::variant`](http://www.boost.org/doc/libs/release/doc/html/variant.html) for more informations.


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

    lua.executeCode("foo(7)");

This code will trigger a Lua error because the `foo` function requires at least two parameters.


#### Writing and reading arrays

`writeVariable` and `readVariable` can also read of write associative arrays in the form of `std::vector` of `std::pair`s, `std::map` and `std::unordered_map`. For `std::vector` which contains `std::pair`s, the first member of the pair is the key and the second member is the value.

    LuaContext lua;

    lua.writeVariable("a",
        std::unordered_map<int, std::string>
        {
            { 12, "hello" },
            { 794, "goodbye" },
            { 4, "how are" },
            { 40, "you" }
        }
    );

    std::cout << lua.executeCode<std::string>("return a[40]") << std::endl;       // prints "you"

Remember that Lua arrays start at offset 1. When you read a `std::map` from a table created in Lua like this `a = { 5, 12 }`, then the first key is 1.

You can combine this with `boost::variant`, which allows you to write polymorphic arrays.

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

    std::cout << lua.executeCode<bool>("return a.test") << std::endl;       // prints "true"
    std::cout << lua.executeCode<float>("return a[2]") << std::endl;        // prints "6.4"

Also note that you can create recursive variants, so you can read arrays which contain arrays which contain arrays, and so forth.

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


#### Writing and reading inside arrays

You can also use `readVariable`, `writeVariable` and `writeFunction` to directly read or write inside an array. Again, remember that the first offset of a Lua array is 1.

    std::cout << lua.readVariable<int>("a", "test") << std::endl;        // reads the offset "test" of the array "a"
    std::cout << lua.writeVariable("a", 2, true) << std::endl;      // writes "true" at the offset "2" of the array "a"

You can also write an empty array, like this:

    LuaContext lua;
    lua.writeVariable("a", LuaContext::EmptyArray);
    
Trying to write `LuaContext::EmptyArray` in a Lua variable instead writes an empty array.


#### Metatables

You can read or write the metatable of an object with `readVariable`, `writeVariable` or `writeFunction` as if it was an array, using the special `LuaContext::Metatable` index.
The metatable is automatically created if it doesn't exist.

    struct Foo {};
    
    LuaContext lua;
    lua.writeVariable("foo", Foo{});
    lua.writeFunction("foo", LuaContext::Metatable, "__call", [](Foo&) {  });
    lua.executeCode("foo()");   // calls the lambda above

Note that functions and custom objects written by this library work thanks to their metatable.
Modifying the metatable of a custom object can break it, especially modifying the `__gc` entry can lead to a memory leak.


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


#### Destroying a Lua variable

    LuaContext lua;
    
    lua.writeVariable("a", 2);
    
    lua.writeVariable("a", nullptr);        // destroys a

The C++ equivalent for `nil` is `nullptr`.

Note that `nullptr` has its own type, which is different from `0` and `NULL`.


#### Comparing objects

    class CustomClass {
       bool operator==(const CustomClass &rhs) {
         /* do something complicated */
       }
    };

    LuaContext lua;

    lua.registerEqFunction(&CustomClass::operator==);

This will register function `__eq` to Lua for the object, and also use it for metatable comparison event.


#### Getting string representation of object

    class CustomClass {
       const std::string toString() const {
         return "CustomClass";
       }
    };

    LuaContext lua;

    lua.registerToStringFunction(&CustomClass::toString);


This will register function `__tostring` to Lua for the object, and also use it for metatable tostring event.


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
    lua.executeCode("b.value_plus_one = 5");
    std::cout << lua.readVariable<Object>("b").value;               // 4

The alternative syntax also exists.

    lua.registerMember<Foo, bool>("value_plus_one", ...same as above...);

Finally you can register functions that will be called by default when a non-existing variable is read or written.
The syntax is the same than above, except that the callbacks take an extra `name` parameter.
    
    lua.registerMember<int (Foo::*)>(
        [](Foo& object, const std::string& memberName) -> int {
            std::cout << "Trying to read member " << memberName << " of object" << std::endl;
            return 1;
        },
        [](Foo& object, const std::string& memberName, int value) {
            std::cout << "Trying to write member " << memberName << " of object with value " << value << std::endl;
        }
    );
    
Remember that you can return `std::function` from the read callback, allowing you to create real virtual objects.


### Clean assembly generation

This library is heavily-templated, which means that it may take additional time to compile but will generate clean assembly code.

For example this:

    writeVariable("test", "a", 12);

Will generate something like this:

    movl    %ebx, (%esp)
    movl    $2, 8(%esp)
    movl    $-1001000, 4(%esp)
    call    lua_rawgeti
    movl    %ebx, (%esp)
    movl    $.LC0, 4(%esp)      # contains "test"
    call    lua_pushstring
    movl    %ebx, (%esp)
    movl    $-2, 4(%esp)
    call    lua_gettable
    movl    %ebx, (%esp)
    movl    $12, 4(%esp)
    call    lua_pushinteger
    movl    %ebx, (%esp)
    movl    $.LC1, 8(%esp)      # contains "a"
    movl    $-2, 4(%esp)
    call    lua_setfield
    movl    %ebx, (%esp)
    movl    $-3, 4(%esp)
    call    lua_settop

Another example. This code:

    lua.writeFunction("foo", [](int a) { return a + 1; });

Will generate this in the main function:

    movl    %ebx, (%esp)
    movl    $_ZZ4mainENUliE_4_FUNEi, 4(%esp)
    call    lua_pushlightuserdata
    movl    %ebx, (%esp)
    movl    $1, 8(%esp)
    movl    $_ZZN10LuaContext6PusherIFiiEvE4pushEP9lua_StatePS1_ENUlS4_E_4_FUNES4_, 4(%esp)
    call    lua_pushcclosure
    movl    %ebx, (%esp)
    movl    $.LC3, 4(%esp)
    call    lua_setglobal

And this helper function:

    pushl   %ebp
    movl    %esp, %ebp
    pushl   %edi
    pushl   %esi
    pushl   %ebx
    subl    $60, %esp
    movl    8(%ebp), %ebx
    movl    $-1001001, 4(%esp)
    movl    %ebx, (%esp)
    call    lua_touserdata
    movl    %ebx, (%esp)
    call    lua_gettop
    testl   %eax, %eax
    jle .L458               # handling not enough parameters
    cmpl    $1, %eax
    jne .L459               # handling too many parameters
    leal    -36(%ebp), %esi
    movl    %esi, 8(%esp)
    movl    $-1, 4(%esp)
    movl    %ebx, (%esp)
    call    lua_tointegerx
    movl    -36(%ebp), %ecx
    testl   %ecx, %ecx
    je  .L436               # handling wrong parameter type
    addl    $1, %eax
    movl    %eax, 4(%esp)
    movl    %ebx, (%esp)
    call    lua_pushinteger
    leal    -12(%ebp), %esp
    movl    $1, %eax
    popl    %ebx
    popl    %esi
    popl    %edi
    popl    %ebp
    ret

(generated with `g++-4.8 -O3 -NDEBUG -std=c++11`)


### Compilation
This code uses new functionalities from [C++11](http://en.wikipedia.org/wiki/C%2B%2B11).

[![build status](https://secure.travis-ci.org/Tomaka17/luawrapper.png)](http://travis-ci.org/Tomaka17/luawrapper)

Does it compile on:
  * Visual C++ 2012 or below: no
  * Visual C++ 2013: no
  * Visual C++ 2013 with november CTP: yes
  * gcc 4.7.2: doesn't always compile because of known bug in the compiler
  * gcc 4.8.1: yes
  * clang 3.2: doesn't always compile because of known bug in the compiler
  * clang 3.5: yes
