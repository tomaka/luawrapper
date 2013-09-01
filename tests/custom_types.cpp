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
		int value;
    };
    
    LuaContext context;
	context.registerFunction<void (Object&)>("increment", [](Object& obj) { ++obj.value; });
	context.registerFunction<int (Object&, int)>("add", [](Object& obj, int x) { obj.value += x; return obj.value; });
    
    context.writeVariable("obj", Object{10});
    context.executeCode("obj:increment()");
	EXPECT_EQ(11, context.readVariable<Object>("obj").value);
    EXPECT_EQ(14, context.executeCode<int>("return obj:add(3)"));
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
