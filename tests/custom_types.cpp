#include <LuaContext.hpp>
#include <gtest/gtest.h>

TEST(CustomTypes, Custom) {
	class Object {
    public:
		Object() : value(10) {}
		void  increment() { ++value; } 
		int value;
    };
    
    LuaContext context;
    context.registerFunction("increment", &Object::increment);
    
    context.writeVariable("obj", Object{});
    context.executeCode("obj:increment()");

	EXPECT_EQ(11, context.readVariable<Object>("obj").value);
}
