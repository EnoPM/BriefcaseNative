#include "HandleTable.hpp"
#include "Manifest.hpp"
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <type_traits>
static int checks = 0;
static void check(bool ok) {
    ++checks;
    if (!ok)
        throw std::runtime_error("Contract failed");
}
template <class F> static void rejects(F fn) {
    bool failed = false;
    try {
        fn();
    } catch (...) {
        failed = true;
    }
    check(failed);
}
int main() {
    try {
        static_assert(std::is_standard_layout_v<BcApi>);
        static_assert(sizeof(BcHandle) == 8 && sizeof(BcResult) == 4);
        check(bc::version("1.2.3") < bc::version("1.10.0"));
        for (auto s : {"1.2", "1.2.3.4", "-1.0.0", "01.0.0", "1.0.x", "4294967296.0.0"})
            rejects([&] { bc::version(s); });
        nlohmann::json j = {{"schemaVersion", 1},      {"id", "test.mod"},
                            {"name", "Test"},          {"author", "Test"},
                            {"version", "1.0.0"},      {"entry", "Test.dll"},
                            {"environment", "both"},   {"minimumApi", 1},
                            {"capabilities", {"log"}}, {"dependencies", nlohmann::json::array()}};
        auto a = bc::parse_manifest(j.dump());
        check(a.capabilities == BC_CAP_LOG);
        for (auto entry : {"../Test.dll", "C:\\Test.dll", "sub/Test.dll", "Test.exe"}) {
            auto bad = j;
            bad["entry"] = entry;
            rejects([&] { bc::parse_manifest(bad.dump()); });
        }
        auto bad = j;
        bad["capabilities"] = {"arbitrary-memory"};
        rejects([&] { bc::parse_manifest(bad.dump()); });
        bad = j;
        bad["minimumApi"] = 2;
        rejects([&] { bc::parse_manifest(bad.dump()); });
        bad = j;
        bad["id"] = "../bad";
        rejects([&] { bc::parse_manifest(bad.dump()); });
        auto b = a;
        b.id = "test.base";
        a.dependencies = {{b.id, {1, 0, 0}}};
        check(bc::dependency_order({a, b}) == std::vector<size_t>({1, 0}));
        rejects([&] { bc::dependency_order({a}); });
        rejects([&] { bc::dependency_order({b, b}); });
        b.dependencies = {{a.id, {1, 0, 0}}};
        rejects([&] { bc::dependency_order({a, b}); });
        b.dependencies.clear();
        b.version = {0, 9, 0};
        rejects([&] { bc::dependency_order({a, b}); });
        bad = j;
        bad["minimumApi"] = -4294967295ll;
        rejects([&] { bc::parse_manifest(bad.dump()); });
        bad = j;
        bad["schemaVersion"] = 1.0;
        rejects([&] { bc::parse_manifest(bad.dump()); });
        bad = j;
        bad["capabilities"] = {{"unexpected", "log"}};
        rejects([&] { bc::parse_manifest(bad.dump()); });
        auto duplicate = j.dump();
        auto position = duplicate.find("\"id\":");
        duplicate.insert(position, "\"id\":\"another\",");
        rejects([&] { bc::parse_manifest(duplicate); });
        bc::HandleTable<int> handles(2);
        int object = 1;
        auto token = handles.insert(1, &object);
        check(token.has_value());
        auto alive = [](int *value) { return *value == 1; };
        check(handles.valid(1, *token, alive));
        check(!handles.valid(2, *token, alive));
        check(!handles.release(2, *token));
        auto alias = handles.insert(1, &object);
        check(alias.has_value());
        check(!handles.insert(1, &object).has_value());
        handles.invalidate(&object);
        check(!handles.valid(1, *token, alive));
        check(!handles.valid(1, *alias, alive));
        auto reused = handles.insert(1, &object);
        check(reused && *reused != *token);
        check(!handles.valid(1, *token, alive));
        check(handles.release(1, *reused));
        check(!handles.release(1, *reused));
        std::cout << "PASS " << checks << " native contract checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
