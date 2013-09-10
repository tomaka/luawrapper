#include <LuaContext.hpp>
#include <gtest/gtest.h>

TEST(CustomTypes, ReadWrite) {
	struct Object {
		int value;
    };
    
    LuaContext context;
    context.writeVariable("obj", Object{5});
	EXPECT_EQ(5, context.readVariable<Object>("obj").value);
}

TEST(CustomTypes, MemberFunctions) {
	struct Object {
		void  increment() { ++value; } 
		int value;
    };
    
    LuaContext context;
    context.registerFunction("increment", &Object::increment);
    
    context.writeVariable("obj", Object{10});
    context.executeCode("obj:increment()");

	EXPECT_EQ(11, context.readVariable<Object>("obj").value);
}

TEST(CustomTypes, ConstVolatileMemberFunctions) {
	struct Object {
		int foo() { return 1; }
		int fooC() const { return 2; }
		int fooV() volatile { return 3; }
		int fooCV() const volatile { return 4; }
    };
    
    LuaContext context;
    context.registerFunction("foo", &Object::foo);
    context.registerFunction("fooC", &Object::fooC);
    context.registerFunction("fooV", &Object::fooV);
    context.registerFunction("fooCV", &Object::fooCV);
    
    context.writeVariable("obj", Object{});

    EXPECT_EQ(1, context.executeCode<int>("return obj:foo()"));
    EXPECT_EQ(2, context.executeCode<int>("return obj:fooC()"));
    EXPECT_EQ(3, context.executeCode<int>("return obj:fooV()"));
    EXPECT_EQ(4, context.executeCode<int>("return obj:fooCV()"));
}

TEST(CustomTypes, Members) {
	struct Object {
		int value;
    };
    
    LuaContext context;
    context.registerMember("value", &Object::value);
    
    context.writeVariable("obj", Object{10});
    context.executeCode("obj.value = obj.value + 5");

	EXPECT_EQ(15, context.readVariable<Object>("obj").value);
}

TEST(CustomTypes, CustomMemberFunctions) {
	struct Object {
		Object(int v) : value(v) {}
		int value;
    };
    
    LuaContext context;
	context.registerFunction<void (Object::*)()>("increment", [](Object& obj) { ++obj.value; });
	context.registerFunction<int (Object::*)(int)>("add", [](Object& obj, int x) { obj.value += x; return obj.value; });
    
	context.writeVariable("obj1", Object{10});
	Object obj{10};
	context.writeVariable("obj2", &obj);
	context.writeVariable("obj3", std::make_shared<Object>(10));

	context.executeCode("obj1:increment()");
	context.executeCode("obj2:increment()");
	context.executeCode("obj3:increment()");

	EXPECT_EQ(11, context.readVariable<Object>("obj1").value);
	EXPECT_EQ(11, context.readVariable<Object*>("obj2")->value);
	EXPECT_EQ(11, context.readVariable<std::shared_ptr<Object>>("obj3")->value);

	EXPECT_EQ(14, context.executeCode<int>("return obj1:add(3)"));
	EXPECT_EQ(14, context.executeCode<int>("return obj2:add(3)"));
	EXPECT_EQ(14, context.executeCode<int>("return obj3:add(3)"));
}

TEST(CustomTypes, CustomMembers) {
	struct Object {};
    
    LuaContext context;
	context.registerMember<Object,int>("value",
		[](const Object& obj) { return 2; },
		[](Object& obj, int val) {}
	);
    
    context.writeVariable("obj", Object{});
    EXPECT_EQ(2, context.executeCode<int>("return obj.value"));
}

TEST(CustomTypes, Unregistering) {
	struct Object {
		int  foo() { return 2; }
    };
    
    LuaContext context;
    context.writeVariable("obj", Object{});
	EXPECT_ANY_THROW(context.executeCode("return obj:foo()"));

    context.registerFunction("foo", &Object::foo);
    EXPECT_EQ(2, context.executeCode<int>("return obj:foo()"));

	context.unregisterFunction<Object>("foo");
	EXPECT_ANY_THROW(context.executeCode("return obj:foo()"));
}
