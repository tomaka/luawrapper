#include <LuaContext.hpp>
#include <gtest/gtest.h>

TEST(AdvancedReadWrite, WritingVariant) {
	LuaContext context;

	boost::variant<int,std::string> val{std::string{"test"}};
    context.writeVariable("a", val);
	EXPECT_EQ("test", context.readVariable<std::string>("a"));
}

TEST(AdvancedReadWrite, ReadingVariant) {
	LuaContext context;
	
    context.writeVariable("a", "test");

	const auto val = context.readVariable<boost::variant<bool,int,std::string>>("a");
	EXPECT_EQ(2, val.which());
	EXPECT_EQ("test", boost::get<std::string>(val));
}

TEST(AdvancedReadWrite, VariantError) {
	LuaContext context;
	
    context.writeVariable("a", "test");
	EXPECT_THROW((context.readVariable<boost::variant<bool,int,bool*>>("a")), LuaContext::WrongTypeException);
}

TEST(AdvancedReadWrite, WritingVectors) {
	LuaContext context;
	
    context.writeVariable("a", std::vector<std::string>{"hello", "world"});

	EXPECT_EQ("hello", context.readVariable<std::string>("a", 0));
	EXPECT_EQ("world", context.readVariable<std::string>("a", 1));

	const auto val = context.readVariable<std::map<int,std::string>>("a");
	EXPECT_EQ("hello", val.at(0));
	EXPECT_EQ("world", val.at(1));
}

TEST(AdvancedReadWrite, VectorOfPairs) {
	LuaContext context;
	
    context.writeVariable("a", std::vector<std::pair<int,std::string>>{
		{ 1, "hello" },
		{ -23, "world" }
	});

	EXPECT_EQ("hello", context.readVariable<std::string>("a", 1));
	EXPECT_EQ("world", context.readVariable<std::string>("a", -23));

	const auto val = context.readVariable<std::vector<std::pair<int,std::string>>>("a");
	EXPECT_TRUE(val[0].first == 1 || val[0].first == -23);
	EXPECT_TRUE(val[1].first == 1 || val[1].first == -23);
	EXPECT_TRUE(val[0].second == "hello" || val[0].second == "world");
	EXPECT_TRUE(val[1].second == "hello" || val[1].second == "world");
}

TEST(AdvancedReadWrite, Maps) {
	LuaContext context;
	
    context.writeVariable("a", std::map<int,std::string>{
		{ 1, "hello" },
		{ -23, "world" }
	});

	EXPECT_EQ("hello", context.readVariable<std::string>("a", 1));
	EXPECT_EQ("world", context.readVariable<std::string>("a", -23));

	const auto val = context.readVariable<std::map<int,std::string>>("a");
	EXPECT_EQ("hello", val.at(1));
	EXPECT_EQ("world", val.at(-23));
}

TEST(AdvancedReadWrite, UnorderedMaps) {
	LuaContext context;
	
    context.writeVariable("a", std::unordered_map<int,std::string>{
		{ 1, "hello" },
		{ -23, "world" }
	});

	EXPECT_EQ("hello", context.readVariable<std::string>("a", 1));
	EXPECT_EQ("world", context.readVariable<std::string>("a", -23));

	const auto val = context.readVariable<std::unordered_map<int,std::string>>("a");
	EXPECT_EQ("hello", val.at(1));
	EXPECT_EQ("world", val.at(-23));
}

TEST(AdvancedReadWrite, AdvancedExample) {
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
