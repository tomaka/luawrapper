#include <LuaContext.hpp>
#include <gtest/gtest.h>

TEST(Maps, MapString) {
    LuaContext context;

    using map_type = std::map<std::string, int>;

    map_type a{{"foo", 1}, {"bar", 2}};
    context.writeVariable("a", a);

    const auto b = context.readVariable<map_type>("a");
    EXPECT_EQ(1, b.at("foo"));
    EXPECT_EQ(2, b.at("bar"));
}

TEST(Maps, UnorderedMapString) {
    LuaContext context;

    using map_type = std::unordered_map<std::string, int>;

    map_type a{{"foo", 1}, {"bar", 2}};
    context.writeVariable("a", a);

    const auto b = context.readVariable<map_type>("a");
    EXPECT_EQ(1, b.at("foo"));
    EXPECT_EQ(2, b.at("bar"));
}

TEST(Maps, MapVariant) {
    LuaContext context;

    // if `std::string` would come first, any `int` key would be converted
    // to `std::string` when reading.
    using map_type = std::map<boost::variant<int, std::string>, int>;

    map_type a{{"foo", 1}, {2, 3}};
    EXPECT_EQ(1, a.at("foo"));
    EXPECT_EQ(3, a.at(2));

    context.writeVariable("a", a);
    EXPECT_EQ(1, context.readVariable<int>("a", "foo"));
    EXPECT_EQ(3, context.readVariable<int>("a", 2));

    const auto b = context.readVariable<map_type>("a");
    EXPECT_EQ(1, b.at("foo"));
    EXPECT_EQ(3, b.at(2));
}
