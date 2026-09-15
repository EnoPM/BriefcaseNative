#include "HandleTable.hpp"
#include "HookRegistry.hpp"
#include "ReflectionContract.hpp"
#include <iostream>
static int checks;
static void check(bool b) {
    ++checks;
    if (!b)
        throw std::runtime_error("Reflection contract #" + std::to_string(checks));
}
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (...) {
        failed = true;
    }
    check(failed);
}
int main() {
    try {
        bc::HandleTable<int> handles(3);
        int obj = 1;
        auto first = handles.intern(1, &obj);
        auto second = handles.intern(1, &obj);
        check(first && first == second);
        check(handles.retain(1, *first));
        check(handles.release(1, *first) && handles.release(1, *first));
        check(handles.get(1, *first) == &obj && handles.get(2, *first) == nullptr);
        auto revoked = handles.invalidate_collect(&obj);
        check(revoked.size() == 1 && revoked[0].owner == 1 && revoked[0].token == *first);
        check(!handles.get(1, *first) && !handles.retain(1, *first));
        auto reused = handles.intern(1, &obj);
        check(reused && *reused != *first);
        handles.erase_owner(2);
        check(handles.get(1, *reused) == &obj);
        handles.erase_owner(1);
        check(!handles.get(1, *reused));
        bc::HookRegistry<int> hooks;
        auto pre = hooks.add(1, 1, 1, [](int &value) { value += 1; });
        auto post = hooks.add(1, 1, 2, [](int &value) { value += 10; });
        int payload = 0;
        hooks.dispatch(1, 1, payload);
        check(payload == 1);
        hooks.dispatch(1, 2, payload);
        check(payload == 11);
        check(!hooks.erase(2, pre));
        check(hooks.erase(1, pre));
        hooks.dispatch(1, 1, payload);
        check(payload == 11);
        hooks.erase_owner(1);
        check(hooks.size() == 0 && !hooks.contains(1));
        uint64_t self{};
        self = hooks.add(1, 2, 1, [&](int &p) {
            ++p;
            hooks.erase(1, self);
        });
        payload = 0;
        hooks.dispatch(2, 1, payload);
        hooks.dispatch(2, 1, payload);
        check(payload == 1);
        int recursive = 0;
        auto rec = hooks.add(1, 3, 1, [&](int &p) {
            ++recursive;
            hooks.dispatch(3, 1, p);
        });
        hooks.dispatch(3, 1, payload);
        check(recursive == 1);
        auto fail = hooks.add(1, 4, 1, [](int &) { throw std::runtime_error("fixture"); });
        check(hooks.dispatch(4, 1, payload) == 1);
        check(hooks.dispatch(4, 1, payload) == 0);
        check(hooks.erase(1, fail));
        auto secondDuringDispatch = uint64_t{};
        auto removeOther = hooks.add(1, 5, 1, [&](int &) { hooks.erase(2, secondDuringDispatch); });
        secondDuringDispatch = hooks.add(2, 5, 1, [](int &p) { ++p; });
        payload = 0;
        hooks.dispatch(5, 1, payload);
        check(payload == 0);
        hooks.erase_owner(1);
        hooks.erase_owner(2);
        check(hooks.size() == 0);
        for (int i = 0; i < 64; ++i)
            check(hooks.add(1, 6, 1, [](int &) {}));
        check(!hooks.add(1, 6, 1, [](int &) {}));
        auto schema = bc::strict_json(R"({"parameterSize":8,"flags":123,"parameters":[
      {"name":"A","type":"int32","offset":0},{"name":"ReturnValue","type":"int32","offset":4,"return":true}]})");
        auto output_schema=schema;output_schema["parameters"][0]["output"]=true;
        rejects([&]{bc::validate_signature(output_schema,schema);});
        bc::validate_signature(output_schema,output_schema);
        bc::validate_signature(schema, schema);
        check(true);
        for (auto [key, value] :
             std::vector<std::pair<std::string, nlohmann::json>>{{"parameterSize", 4}, {"flags", 124}}) {
            auto bad = schema;
            bad[key] = value;
            rejects([&] { bc::validate_signature(schema, bad); });
        }
        for (auto [key, value] : std::vector<std::pair<std::string, nlohmann::json>>{
                 {"offset", 1}, {"type", "float"}, {"name", "Other"}, {"return", true}}) {
            auto bad = schema;
            bad["parameters"][0][key] = value;
            rejects([&] { bc::validate_signature(schema, bad); });
        }
        auto bad = schema;
        bad["parameters"].push_back(bad["parameters"][0]);
        rejects([&] { bc::validate_signature(schema, bad); });
        std::cout << "PASS " << checks << " reflection/lifecycle contract checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
