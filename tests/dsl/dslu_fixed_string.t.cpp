//
// Created by Dominic Kloecker on 29/08/2026.
//

#include <gtest/gtest.h>
#include "dslu_fixed_string.h"
#include <string_view>
#include <sstream>

template<dsl::FixedString Name>
struct NamedComponent {
    static constexpr auto name = Name;
    static constexpr std::string_view name_view = Name.view();
};

TEST(FixedStringTest, NTTPUsage) {
    using Foo = NamedComponent<"Foo">;
    using Bar = NamedComponent<"Bar">;
    using Empty = NamedComponent<"">;

    static_assert(Foo::name == "Foo");
    static_assert(Foo::name.size() == 3);
    static_assert(Foo::name.length() == 3);
    static_assert(!Foo::name.empty());
    static_assert(Foo::name_view == "Foo");

    static_assert(Bar::name == "Bar");
    static_assert(Bar::name.size() == 3);
    static_assert(Bar::name_view == "Bar");

    static_assert(Empty::name == "");
    static_assert(Empty::name.size() == 0);
    static_assert(Empty::name.empty());
    static_assert(Empty::name_view == "");

    EXPECT_EQ(Foo::name.view(), "Foo");
    EXPECT_EQ(Bar::name.view(), "Bar");
    EXPECT_EQ(Empty::name.view(), "");

    static_assert(Foo::name != Bar::name);
    static_assert(Foo::name > Bar::name);
}

TEST(FixedStringTest, BasicOperations) {
    constexpr dsl::FixedString str{"Hello"};
    static_assert(str.size() == 5);
    static_assert(str.length() == 5);
    static_assert(!str.empty());
    static_assert(str[0] == 'H');
    static_assert(str[4] == 'o');
    static_assert(str.view() == "Hello");
    static_assert(str == "Hello");
    static_assert(str == std::string_view{"Hello"});
    static_assert(str == dsl::FixedString{"Hello"});
    static_assert(str != dsl::FixedString{"World"});

    EXPECT_STREQ(str.c_str(), "Hello");
    EXPECT_STREQ(str.data(), "Hello");
    EXPECT_EQ(str[1], 'e');

    std::ostringstream oss;
    oss << str;
    EXPECT_EQ(oss.str(), "Hello");
}

TEST(FixedStringTest, Iterators) {
    dsl::FixedString str{"Test"};
    std::string collected;
    for (char c : str) {
        collected.push_back(c);
    }
    EXPECT_EQ(collected, "Test");
}
