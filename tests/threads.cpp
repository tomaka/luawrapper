#include <LuaContext.hpp>
#include <LuaContextThread.hpp>
#include <gtest/gtest.h>

TEST(Threads, Fork) {
	LuaContext context;
	context.writeVariable("a", "hello");

	auto thread1 = context.createThread();
	context.forkGlobals(thread1);
	auto thread2 = context.createThread();
	context.forkGlobals(thread2);

	context.executeCode(thread1, "a = 3");
	context.executeCode(thread2, "a = 18");

	EXPECT_EQ(3, context.readVariable<int>(thread1, "a"));

	context.executeCode(thread1, "a = 5");
	context.writeVariable("a", "hello");
	EXPECT_EQ(18, context.readVariable<int>(thread2, "a"));

	EXPECT_EQ("hello", context.readVariable<std::string>("a"));
}

TEST(Threads, RAIIFork) {
	LuaContext context;
	context.writeVariable("a", "hello");

	LuaContextThread thread1(&context);
	thread1.forkGlobals();

	LuaContextThread thread2(&context);
	thread2.forkGlobals();

	thread1.executeCode("a = 3");
	thread2.executeCode("a = 18");

	EXPECT_EQ(3, thread1.readVariable<int>("a"));

	thread1.executeCode("a = 5");
	context.writeVariable("a", "hello");
	EXPECT_EQ(18, thread2.readVariable<int>("a"));

	EXPECT_EQ("hello", context.readVariable<std::string>("a"));
}
