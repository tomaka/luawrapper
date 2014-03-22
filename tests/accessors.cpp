#include <LuaContext.hpp>
#include <gtest/gtest.h>

TEST(Accessors, BasicUsage) {
	LuaContext context;

	context["a"] = 5;
	EXPECT_EQ(5, static_cast<int>(context["a"]));
}

TEST(Accessors, Arrays) {
	LuaContext context;

	context["a"] = LuaContext::EmptyArray;
	context["a"]["b"] = 3;

	EXPECT_EQ(3, context.readVariable<int>("a", "b"));
}

TEST(Accessors, Calling) {
	LuaContext context;
	context.executeCode("foo = function() end");

	context["foo"]();
}
