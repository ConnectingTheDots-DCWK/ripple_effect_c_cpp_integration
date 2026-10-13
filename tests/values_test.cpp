// A value through the arena and back, through the engine where it can be:
// an Int at the edge of 64 bits, a Float that is not a number, a map in the
// order it was written.
#include <ripple/ripple.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

TEST(Values, AnArenaReadsBackAsItWasWritten)
{
    const ripple::Value written(ripple::Value::Map{
        {"zeta", ripple::Value(std::numeric_limits<std::int64_t>::max())},
        {"alpha", ripple::Value(std::nan(""))},
        {"list", ripple::Value(ripple::Value::List{ripple::Value(1), ripple::Value(1.0),
                                                   ripple::Value("é")})},
        {"nothing", ripple::Value()},
    });
    ripple::detail::Arena arena(written);
    const ripple::Value read = ripple::detail::value(arena.view());
    const auto* map = read.get_if<ripple::Value::Map>();
    ASSERT_NE(map, nullptr);
    ASSERT_EQ(map->size(), 4u);
    EXPECT_EQ((*map)[0].first, "zeta");
    EXPECT_EQ((*map)[1].first, "alpha");
    EXPECT_EQ(*(*map)[0].second.get_if<std::int64_t>(), std::numeric_limits<std::int64_t>::max());
    EXPECT_TRUE(std::isnan(*(*map)[1].second.get_if<double>()));
    const auto* list = (*map)[2].second.get_if<ripple::Value::List>();
    ASSERT_NE(list, nullptr);
    EXPECT_TRUE((*list)[0].is_int());
    EXPECT_TRUE((*list)[1].is_float());
    EXPECT_EQ(*(*list)[2].get_if<std::string>(), "é");
    EXPECT_TRUE((*map)[3].second.is_null());
}

TEST(Values, TheEngineReadsAnIntAndAFloatApart)
{
    EXPECT_EQ(ripple::as_text(ripple::Value(std::numeric_limits<std::int64_t>::max())),
              "9223372036854775807");
    EXPECT_NE(ripple::as_text(ripple::Value(7)), ripple::as_text(ripple::Value(7.5)));
}

TEST(Values, AHostileArenaReadsAsNull)
{
    // A list naming itself, and a child past the end.
    RippleValueNode nodes[2]{};
    const std::uint32_t children[] = {0, 1, 9};
    nodes[0].kind = RIPPLE_VALUE_KIND_ITEMS;
    nodes[0].data.items = RippleIndices{children, 3};
    nodes[1].kind = RIPPLE_VALUE_KIND_INTEGER;
    nodes[1].data.integer = 7;
    const ripple::Value read = ripple::detail::value(RippleValue{nodes, 2});
    const auto* list = read.get_if<ripple::Value::List>();
    ASSERT_NE(list, nullptr);
    EXPECT_TRUE((*list)[0].is_null());
    EXPECT_EQ(*(*list)[1].get_if<std::int64_t>(), 7);
    EXPECT_TRUE((*list)[2].is_null());
}
