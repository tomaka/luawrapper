#include <LuaContext.hpp>
#include <gtest/gtest.h>

TEST(AdvancedReadWrite, Test) {
	LuaContext context;

    context.writeVariable("a",
        std::vector< std::pair< boost::variant<int,std::string>, boost::variant<bool,float> >>
        {
            { "test", true },
            { 2, 6.4f },
            { "hello", 1.f },
            { "world", -7.6f },
            { 18, false }
        }
    );

	EXPECT_EQ(true, context.executeCode<bool>("return a.test"));
	EXPECT_DOUBLE_EQ(6.4f, context.executeCode<float>("return a[2]"));
    EXPECT_DOUBLE_EQ(-7.6f, context.readVariable<float>("a", "world"));
}
