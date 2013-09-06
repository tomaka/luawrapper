#include <LuaContext.hpp>
#include <gtest/gtest.h>

TEST(FunctionsWrite, NativeFunctions) {
	struct Foo {
		static int increment(int x)
		{
			return x + 1;
		}
	};

	LuaContext context;
	
	context.writeVariable("f", &Foo::increment);
	context.writeFunction<int (int)>("g", &Foo::increment);
	context.writeFunction("h", &Foo::increment);

	EXPECT_EQ(3, context.executeCode<int>("return f(2)"));
	EXPECT_EQ(13, context.executeCode<int>("return g(12)"));
	EXPECT_EQ(9, context.executeCode<int>("return h(8)"));
}

TEST(FunctionsWrite, FunctionObjects) {
	struct Foo {
		int operator()(int x) {
			return x + 1;
		}

		double operator()(double) {
			EXPECT_TRUE(false);
			return 0;
		}
	};


	LuaContext context;
	
	context.writeVariable("f", std::function<int (int)>(Foo{}));
	context.writeFunction<int (int)>("g", Foo{});

	EXPECT_EQ(3, context.executeCode<int>("return f(2)"));
	EXPECT_EQ(13, context.executeCode<int>("return g(12)"));
}

TEST(FunctionsWrite, FunctionObjectsConst) {
	struct Foo {
		int operator()(int x) {
			return x + 1;
		}
		
		int operator()(int x) const {
			return x + 1;
		}
	};


	LuaContext context;
	
	context.writeVariable("f", std::function<int (int)>(Foo{}));
	context.writeFunction<int (int)>("g", Foo{});

	EXPECT_EQ(3, context.executeCode<int>("return f(2)"));
	EXPECT_EQ(13, context.executeCode<int>("return g(12)"));
}

TEST(FunctionsWrite, FunctionObjectsAutodetect) {
	struct Foo {
		int operator()(int x) {
			return x + 1;
		}
	};


	LuaContext context;
	
	context.writeVariable("f", std::function<int (int)>(Foo{}));
	context.writeFunction<int (int)>("g", Foo{});
	context.writeFunction("h", Foo{});

	EXPECT_EQ(3, context.executeCode<int>("return f(2)"));
	EXPECT_EQ(13, context.executeCode<int>("return g(12)"));
	EXPECT_EQ(9, context.executeCode<int>("return h(8)"));
}

TEST(FunctionsWrite, Lambdas) {
	LuaContext context;
	
	const auto lambda = [](int x) { return x + 1; };
	context.writeVariable("f", std::function<int (int)>(lambda));
	context.writeFunction<int (int)>("g", lambda);
	context.writeFunction("h", lambda);

	EXPECT_EQ(3, context.executeCode<int>("return f(2)"));
	EXPECT_EQ(13, context.executeCode<int>("return g(12)"));
	EXPECT_EQ(9, context.executeCode<int>("return h(8)"));
}

TEST(FunctionsWrite, DestructorCalled) {
	struct Foo {
		int operator()(int x) {
			return x + 1;
		}

		std::shared_ptr<char> dummy;
	};

	auto foo = Foo{ std::make_shared<char>() };
	std::weak_ptr<char> dummy = foo.dummy;

	auto context = std::make_shared<LuaContext>();
	context->writeFunction("f", foo);
	foo.dummy.reset();

	EXPECT_FALSE(dummy.expired());
	context.reset();
	EXPECT_TRUE(dummy.expired());
}

TEST(FunctionsWrite, ReturningMultipleValues) {
	LuaContext context;
	
	context.writeFunction("f", [](int x) { return std::make_tuple(x, x+1, "hello"); });
	context.executeCode("a, b, c = f(2)");

	EXPECT_EQ(2, context.readVariable<int>("a"));
	EXPECT_EQ(3, context.readVariable<int>("b"));
	EXPECT_EQ("hello", context.readVariable<std::string>("c"));
}
