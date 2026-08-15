#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

std::string quote(const std::filesystem::path &path) { return "\"" + path.string() + "\""; }

std::string readFile(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

std::string refactor(std::string_view source) {
    static unsigned counter = 0;
    const auto path =
        std::filesystem::temp_directory_path() / ("refactor_tool_test_" + std::to_string(++counter) + ".cpp");

    {
        std::ofstream output(path, std::ios::binary);
        output << source;
    }

    const std::string command = quote(REFACTOR_TOOL_PATH) + " " + quote(path) + " -- -std=c++20";
    const int result = std::system(command.c_str());
    EXPECT_EQ(result, 0) << "refactor_tool failed for " << path;

    const std::string transformed = readFile(path);
    std::error_code error;
    std::filesystem::remove(path, error);
    return transformed;
}

}  // namespace

TEST(VirtualDestructor, AddsVirtualToBaseWithDerivedClass) {
    const auto result = refactor(R"cpp(
class Base {
public:
    ~Base() = default;
};
class Derived : public Base {};
)cpp");

    EXPECT_NE(result.find("virtual ~Base() = default"), std::string::npos);
}

TEST(VirtualDestructor, DoesNotTouchStandaloneOrAlreadyVirtualDestructor) {
    const auto result = refactor(R"cpp(
class Standalone {
public:
    ~Standalone() = default;
};
class Base {
public:
    virtual ~Base() = default;
};
class Derived : public Base {};
)cpp");

    EXPECT_NE(result.find("~Standalone() = default"), std::string::npos);
    EXPECT_EQ(result.find("virtual ~Standalone()"), std::string::npos);
    EXPECT_EQ(result.find("virtual virtual ~Base"), std::string::npos);
}

TEST(Override, AddsOverrideToOverridingMethods) {
    const auto result = refactor(R"cpp(
struct Base {
    virtual void f() = 0;
};
struct Derived : Base {
    void f() {}
};
)cpp");

    EXPECT_NE(result.find("void f() override"), std::string::npos);
}

TEST(Override, KeepsExistingOverrideAndHandlesSuffixes) {
    const auto result = refactor(R"cpp(
struct Base {
    virtual void f() const & noexcept = 0;
    virtual void g() = 0;
    virtual ~Base() = default;
};
struct Derived : Base {
    void f() const & noexcept {}
    void g() override {}
    ~Derived() = default;
};
)cpp");

    EXPECT_NE(result.find("void f() const & noexcept override"), std::string::npos);
    EXPECT_EQ(result.find("override override"), std::string::npos);
    EXPECT_EQ(result.find("~Derived() override"), std::string::npos);
}

TEST(RangeFor, AddsReferenceForConstClassValues) {
    const auto result = refactor(R"cpp(
#include <initializer_list>
struct Item { int value; };
void f(Item* begin, Item* end) {
    for (const Item item : {Item{}, Item{}}) {
        (void)item;
    }
}
)cpp");

    EXPECT_NE(result.find("const Item& item"), std::string::npos);
}

TEST(RangeFor, SkipsFundamentalTypesAndExistingReferences) {
    const auto result = refactor(R"cpp(
#include <initializer_list>
struct Item { int value; };
void f() {
    for (const int value : {1, 2, 3}) {
        (void)value;
    }
    for (const Item& item : {Item{}, Item{}}) {
        (void)item;
    }
}
)cpp");

    EXPECT_NE(result.find("const int value"), std::string::npos);
    EXPECT_EQ(result.find("const int& value"), std::string::npos);
    EXPECT_EQ(result.find("const Item&& item"), std::string::npos);
}
