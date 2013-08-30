#include <iostream>
#include <LuaContext.hpp>

void test1() {
	LuaContext context;

	auto val = context.executeCode<int>("return 3");
	if (val != 3)			throw std::logic_error("Test 1 failed");

	auto val2 = context.executeCode<std::string>("return 'hello'");
	if (val2 != "hello")	throw std::logic_error("Test 1 failed");
}

void test2() {
	LuaContext context;
	context.executeCode("a = 2");
	if (!context.hasVariable("a"))
		throw std::logic_error("Test 2 failed");
	if (context.readVariable<int>("a") != 2)
		throw std::logic_error("Test 2 failed");
}

void test3() {
	LuaContext context;

	context.writeVariable("a", true);

	if (!context.hasVariable("a"))
		throw std::logic_error("Test 3 failed");
	if (context.readVariable<bool>("a") != true)
		throw std::logic_error("Test 3 failed");
}

void test4() {
	LuaContext context;

	context.writeFunction("foo", [](int i) { return i + 2; });

	const auto val = context.executeCode<int>("return foo(3)");
	if (val != 5)	throw std::logic_error("Test 4 failed");
}

void test5() {
	LuaContext context;

	context.executeCode("foo = function(i) return i + 2 end");

	const auto val = context.readVariable<std::function<int (int)>>("foo");
	if (val(3) != 5)	throw std::logic_error("Test 5 failed");
}

void test6() {
	LuaContext lua;

    lua.writeVariable("a",
        std::vector< std::pair< boost::variant<int,std::string>, boost::variant<bool,float> >>
        {
            { "test", true },
            { 2, 6.4f },
            { "hello", 1.f },
            { "world", -7.6f },
            { 18, false }
        }
    );

	const auto val1 = lua.executeCode<bool>("return a.test");
    const auto val2 = lua.executeCode<float>("return a[2]");
    const auto val3 = lua.readVariable<float>("a", "world");

	if (val1 != true)
		throw std::logic_error("Test 6 failed");
	if (val2 != 6.4f)
		throw std::logic_error("Test 6 failed");
	if (val3 != -7.6f)
		throw std::logic_error("Test 6 failed");
}

void test7() {
	class Object {
    public:
     Object() : value(10) {}
     
     void  increment() { ++value; } 
     
     int value;
    };
    
    LuaContext lua;
    lua.registerFunction("increment", &Object::increment);
    
    lua.writeVariable("obj", Object{});
    lua.executeCode("obj:increment()");

    const auto val = lua.readVariable<Object>("obj").value;
	if (val != 11)
		throw std::logic_error("Test 7 failed");
}

int test8_utility(int value) {
	return value * 2;
}

void test8() {
    LuaContext lua;
    lua.writeVariable("f", &test8_utility);
    if (lua.executeCode<int>("return f(5);") != 10)
		throw std::logic_error("Test 8 failed");
    if (lua.executeCode<int>("return f(7);") != 14)
		throw std::logic_error("Test 8 failed");
}

int main() {
	test1();
	test2();
	test3();
	test4();
	test5();
	test6();
	test7();
	test8();

	std::cout << "All tests are successful" << std::endl;
	return 0;
}
