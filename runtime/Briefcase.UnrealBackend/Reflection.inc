// Private Unreal marshalling and metadata. Included in Backend.cpp.
using json = nlohmann::json;
struct Parameter {
    FProperty *property;
    std::string name;
    uint32_t kind;
    bool result;
    bool output;
};
struct Function {
    UFunction *object;
    UClass *owner;
    uint32_t size;
    std::vector<Parameter> parameters;
    json description;
};
static std::unordered_map<UFunction *, std::shared_ptr<Function>> functions;
static std::unordered_map<uint64_t, std::unordered_set<UFunction *>> validated_functions;
static std::wstring wide(const char *text, size_t limit = 4096) {
    if (!text || strnlen_s(text, limit + 1) > limit)
        throw std::runtime_error("Invalid UTF-8 input length");
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out.data(), n);
    out.pop_back();
    return out;
}
static std::string utf8(std::wstring_view text) {
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                nullptr, 0, nullptr, nullptr);
    if (n < 0)
        throw std::runtime_error("Invalid UTF-16");
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(),
                        n, nullptr, nullptr);
    return out;
}
static bool alive(UObject *object) {
    return object && UObjectArray::IsValid(object->GetObjectItem(), false);
}
static UObject *object_for(uint64_t owner, BcHandle h) {
    auto *p = handles.get(owner, h);
    return alive(p) ? p : nullptr;
}
static uint32_t property_kind(FProperty *p) {
    const auto name = p->GetClass().GetName();
    if (p->GetArrayDim() != 1)
        return BC_VALUE_NONE;
    if (name == L"IntProperty" && p->GetElementSize() == 4)
        return BC_VALUE_I32;
    if (name == L"FloatProperty" && p->GetElementSize() == 4)
        return BC_VALUE_F32;
    if (name == L"BoolProperty" && p->GetElementSize() == 1)
        return BC_VALUE_BOOL;
    if ((name == L"ObjectProperty" || name == L"ClassProperty") && p->GetElementSize() == 8)
        return BC_VALUE_OBJECT;
    if ((name == L"ByteProperty" || name == L"EnumProperty") && p->GetElementSize() == 1)
        return BC_VALUE_U8;
    if (name == L"StructProperty" && p->GetElementSize() == 12 &&
        static_cast<FStructProperty *>(p)->GetStruct()->GetPathName() == L"/Script/CoreUObject.Vector")
        return BC_VALUE_VECTOR;
    if (name == L"StructProperty") {
        const auto type = static_cast<FStructProperty *>(p)->GetStruct()->GetPathName();
        if (p->GetElementSize() == 12 && type == L"/Script/CoreUObject.Rotator") return BC_VALUE_ROTATOR;
        if (p->GetElementSize() == 8 && type == L"/Script/CoreUObject.Vector2D") return BC_VALUE_VECTOR2;
    }
    return BC_VALUE_NONE;
}
static const char *type_name(uint32_t kind) {
    switch (kind) {
    case BC_VALUE_I32:
        return "int32";
    case BC_VALUE_F32:
        return "float";
    case BC_VALUE_BOOL:
        return "bool";
    case BC_VALUE_OBJECT:
        return "object";
    case BC_VALUE_U8:
        return "byte";
    case BC_VALUE_VECTOR:
        return "vector";
    case BC_VALUE_ROTATOR: return "rotator";
    case BC_VALUE_VECTOR2: return "vector2";
    default:
        return "unsupported";
    }
}
static std::shared_ptr<Function> metadata(UFunction *fn) {
    if (auto it = functions.find(fn); it != functions.end())
        return it->second;
    if (functions.size() >= 256)
        throw std::runtime_error("Function metadata limit");
    const auto path = fn->GetPathName();
    // Compiled /Script metadata remains valid until engine shutdown. Dynamic Blueprint metadata is not
    // cached.
    if (!path.starts_with(L"/Script/"))
        throw std::runtime_error("Only persistent /Script functions supported");
    auto *owner = Cast<UClass>(fn->GetOuterPrivate());
    if (!owner || fn->GetParmsSize() > 4096)
        throw std::runtime_error("Invalid function owner/parameter buffer");
    auto result = std::make_shared<Function>();
    result->object = fn;
    result->owner = owner;
    result->size = fn->GetParmsSize();
    auto params = json::array();
    size_t seen = 0;
    for (auto *p : fn->ForEachProperty()) {
        if (++seen > 64)
            throw std::runtime_error("Parameter traversal limit");
        if (!p->HasAnyPropertyFlags(CPF_Parm))
            continue;
        if (p->GetOffset_Internal() < 0 || p->GetSize() < 0 ||
            uint64_t(p->GetOffset_Internal()) + p->GetSize() > result->size)
            throw std::runtime_error("Parameter outside buffer");
        const auto kind = property_kind(p);
        const bool ret = p->HasAnyPropertyFlags(CPF_ReturnParm);
        const bool output = p->HasAnyPropertyFlags(CPF_OutParm) && !p->HasAnyPropertyFlags(CPF_ConstParm) && !ret;
        if (p->HasAnyPropertyFlags(CPF_ReferenceParm) && !p->HasAnyPropertyFlags(CPF_ConstParm)) throw std::runtime_error("Reference parameters unsupported");
        auto name = utf8(p->GetName());
        result->parameters.push_back({p, name, kind, ret, output});
        params.push_back({{"name", name},
                          {"type", type_name(kind)},
                          {"offset", p->GetOffset_Internal()},
                          {"return", ret}, {"output", output},
                          {"unrealType", utf8(p->GetClass().GetName())}});
    }
    if (result->parameters.size() != fn->GetNumParms())
        throw std::runtime_error("Parameter count mismatch");
    result->description = {{"path", utf8(path)},
                           {"parameterSize", result->size},
                           {"flags", uint32_t(fn->GetFunctionFlags())},
                           {"parameters", params}};
    functions.emplace(fn, result);
    return result;
}
static std::shared_ptr<Function> function_for(uint64_t owner, BcHandle h) {
    auto *obj = object_for(owner, h);
    auto *fn = obj ? Cast<UFunction>(obj) : nullptr;
    return fn ? metadata(fn) : nullptr;
}
static BcResult read_value(uint64_t owner, FProperty *p, const void *container, BcValue *out) {
    if (!out || !container)
        return BC_INVALID_ARGUMENT;
    *out = {};
    out->kind = property_kind(p);
    const void *address = p->ContainerPtrToValuePtr<void>(container);
    switch (out->kind) {
    case BC_VALUE_I32: {
        int32_t value;
        std::memcpy(&value, address, 4);
        out->data.integer = value;
        break;
    }
    case BC_VALUE_F32: {
        float value;
        std::memcpy(&value, address, 4);
        out->data.number = value;
        break;
    }
    case BC_VALUE_BOOL:
        out->data.integer = static_cast<FBoolProperty *>(p)->GetPropertyValue(address);
        break;
    case BC_VALUE_U8:
        out->data.integer = *static_cast<const uint8_t *>(address);
        break;
    case BC_VALUE_OBJECT: {
        UObject *obj{};
        std::memcpy(&obj, address, 8);
        if (!obj)
            break;
        if (!alive(obj))
            return BC_STALE_HANDLE;
        auto h = handles.intern(owner, obj);
        if (!h)
            return BC_LIMIT;
        out->data.object = *h;
        break;
    }
    case BC_VALUE_ROTATOR:
    case BC_VALUE_VECTOR2:
    case BC_VALUE_VECTOR: {
        float values[3]{};
        std::memcpy(values, address, out->kind == BC_VALUE_VECTOR2 ? 8 : 12);
        for (int i = 0; i < 3; ++i)
            out->data.vector[i] = values[i];
        break;
    }
    default:
        return BC_UNSUPPORTED;
    }
    return BC_OK;
}
static BcResult write_value(uint64_t owner, FProperty *p, void *container, const BcValue &v) {
    const auto kind = property_kind(p);
    if (v.reserved || v.kind != kind || !kind)
        return BC_INVALID_ARGUMENT;
    void *address = p->ContainerPtrToValuePtr<void>(container);
    switch (kind) {
    case BC_VALUE_I32: {
        if (v.data.integer < INT32_MIN || v.data.integer > INT32_MAX)
            return BC_INVALID_ARGUMENT;
        auto value = int32_t(v.data.integer);
        std::memcpy(address, &value, 4);
        break;
    }
    case BC_VALUE_F32: {
        if (!std::isfinite(v.data.number) || std::abs(v.data.number) > FLT_MAX)
            return BC_INVALID_ARGUMENT;
        auto value = float(v.data.number);
        std::memcpy(address, &value, 4);
        break;
    }
    case BC_VALUE_BOOL:
        if (v.data.integer != 0 && v.data.integer != 1)
            return BC_INVALID_ARGUMENT;
        static_cast<FBoolProperty *>(p)->SetPropertyValue(address, v.data.integer != 0);
        break;
    case BC_VALUE_U8:
        if (v.data.integer < 0 || v.data.integer > 255)
            return BC_INVALID_ARGUMENT;
        *static_cast<uint8_t *>(address) = uint8_t(v.data.integer);
        break;
    case BC_VALUE_OBJECT: {
        auto *obj = v.data.object ? object_for(owner, v.data.object) : nullptr;
        if (v.data.object && !obj)
            return BC_STALE_HANDLE;
        if (obj && !obj->IsA(static_cast<FObjectPropertyBase *>(p)->GetPropertyClass()))
            return BC_INVALID_ARGUMENT;
        std::memcpy(address, &obj, 8);
        break;
    }
    case BC_VALUE_ROTATOR:
    case BC_VALUE_VECTOR2:
    case BC_VALUE_VECTOR: {
        float values[3]{};
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(v.data.vector[i]) || std::abs(v.data.vector[i]) > FLT_MAX)
                return BC_INVALID_ARGUMENT;
            values[i] = float(v.data.vector[i]);
        }
        std::memcpy(address, values, kind == BC_VALUE_VECTOR2 ? 8 : 12);
        break;
    }
    default:
        return BC_UNSUPPORTED;
    }
    return BC_OK;
}
BcResult backend_resolve(uint64_t owner, const char *path, const char *signature, BcHandle *out) {
    if (auto e = thread_check(); e != BC_OK)
        return e;
    if (!out || !signature || strnlen_s(signature, 65537) > 65536)
        return BC_INVALID_ARGUMENT;
    *out = 0;
    auto w = wide(path);
    auto *obj = UObjectGlobals::StaticFindObject<UObject *>(nullptr, nullptr, w.c_str());
    auto *fn = alive(obj) ? Cast<UFunction>(obj) : nullptr;
    if (!fn)
        return BC_NOT_FOUND;
    auto meta = metadata(fn);
    validate_signature(meta->description, strict_json(signature));
    validated_functions[owner].insert(fn);
    auto token = handles.intern(owner, fn);
    if (!token)
        return BC_LIMIT;
    *out = *token;
    log("Reflected signature validated: " + utf8(fn->GetPathName()));
    return BC_OK;
}
BcResult backend_describe(uint64_t owner, BcHandle h, char *out, uint32_t capacity, uint32_t *required) {
    if (auto e = thread_check(); e != BC_OK)
        return e;
    if (!required)
        return BC_INVALID_ARGUMENT;
    auto fn = function_for(owner, h);
    if (!fn)
        return BC_STALE_HANDLE;
    auto text = fn->description.dump();
    *required = uint32_t(text.size() + 1);
    if (!out || capacity < *required)
        return BC_LIMIT;
    std::memcpy(out, text.c_str(), *required);
    return BC_OK;
}
BcResult backend_info(uint64_t owner, BcHandle h, BcObjectInfo *out) {
    if (auto e = thread_check(); e != BC_OK)
        return e;
    if (!out || out->size < sizeof(*out))
        return BC_INVALID_ARGUMENT;
    auto *obj = object_for(owner, h);
    if (!obj)
        return BC_STALE_HANDLE;
    const auto path = utf8(obj->GetPathName()), cls = utf8(obj->GetClassPrivate()->GetPathName());
    if (path.size() >= sizeof(out->path) || cls.size() >= sizeof(out->class_path))
        return BC_LIMIT;
    *out = {};
    out->size = sizeof(*out);
    out->flags = (obj->HasAnyFlags(RF_ClassDefaultObject) ? 16u : 0u) |
                 (obj->HasAnyFlags(RF_ArchetypeObject) ? 32u : 0u);
    std::memcpy(out->path, path.c_str(), path.size() + 1);
    std::memcpy(out->class_path, cls.c_str(), cls.size() + 1);
    return BC_OK;
}
BcResult backend_retain(uint64_t owner, BcHandle h) {
    if (auto e = thread_check(); e != BC_OK)
        return e;
    return object_for(owner, h) && handles.retain(owner, h) ? BC_OK : BC_STALE_HANDLE;
}
static BcResult access_property(uint64_t owner, BcHandle h, const char *name, BcValue *out,
                                const BcValue *write) {
    if (auto e = thread_check(); e != BC_OK)
        return e;
    auto *obj = object_for(owner, h);
    if (!obj)
        return BC_STALE_HANDLE;
    if (!name || strnlen_s(name, 257) > 256)
        return BC_INVALID_ARGUMENT;
    if (write && (obj->HasAnyFlags(RF_ClassDefaultObject) || obj->HasAnyFlags(RF_ArchetypeObject)))
        return BC_DENIED;
    const auto parts = property_path(name);
    UStruct *container = obj->GetClassPrivate();
    void *data = obj;
    for (size_t index = 0; index < parts.size(); ++index) {
        const auto w = wide(std::string(parts[index]).c_str(), 64);
        FProperty *found{};
        size_t visited = 0;
        if (index == 0)
            found = obj->GetPropertyByNameInChain(w.c_str());
        else
            for (auto *candidate : container->ForEachPropertyInChain()) {
                if (++visited > 2048)
                    return BC_LIMIT;
                if (candidate->GetName() == w) {
                    found = candidate;
                    break;
                }
            }
        if (!found)
            return BC_NOT_FOUND;
        if (found->GetOffset_Internal() < 0 || found->GetSize() < 0 ||
            uint64_t(found->GetOffset_Internal()) + found->GetSize() >
                uint64_t(container->GetPropertiesSize()))
            return BC_INVALID_ARGUMENT;
        if (index + 1 == parts.size()) {
            if (!write)
                return read_value(owner, found, data, out);
            if (property_kind(found) == BC_VALUE_OBJECT)
                return BC_UNSUPPORTED;
            return write_value(owner, found, data, *write);
        }
        if (found->GetClass().GetName() != L"StructProperty" || found->GetArrayDim() != 1)
            return BC_UNSUPPORTED;
        auto *next = static_cast<FStructProperty *>(found)->GetStruct();
        if (!next || next->GetPropertiesSize() > found->GetElementSize())
            return BC_UNSUPPORTED;
        data = found->ContainerPtrToValuePtr<void>(data);
        container = next;
    }
    return BC_NOT_FOUND;
}
BcResult backend_read_property(uint64_t owner, BcHandle h, const char *name, BcValue *out) {
    return access_property(owner, h, name, out, nullptr);
}
BcResult backend_write_property(uint64_t owner, BcHandle h, const char *name, const BcValue *value) {
    if (!value)
        return BC_INVALID_ARGUMENT;
    return access_property(owner, h, name, nullptr, value);
}
BcResult backend_invoke_outputs(uint64_t owner, BcHandle self, BcHandle h, const BcNamedValue *args, uint32_t count,
                        BcNamedValue *outputs, uint32_t output_count, BcValue *out) {
    if (auto e = thread_check(); e != BC_OK)
        return e;
    if (!out || count > 32 || (count && !args) || output_count > 32 || (output_count && !outputs))
        return BC_INVALID_ARGUMENT;
    *out = {};
    auto *obj = object_for(owner, self);
    auto fn = function_for(owner, h);
    if (!obj || !fn)
        return BC_STALE_HANDLE;
    if (!validated_functions[owner].contains(fn->object))
        return BC_DENIED;
    if (!obj->IsA(fn->owner))
        return BC_INVALID_ARGUMENT;
    std::set<std::string> names;
    for (uint32_t i = 0; i < count; ++i)
        if (!args[i].name || strnlen_s(args[i].name, 257) > 256 || !names.insert(args[i].name).second)
            return BC_INVALID_ARGUMENT;
    alignas(16) std::array<uint8_t, 4096> buffer{};
    std::set<std::string> output_names;
    for (uint32_t i = 0; i < output_count; ++i) {
        if (!outputs[i].name || strnlen_s(outputs[i].name,257)>256 || !output_names.insert(outputs[i].name).second)
            return BC_INVALID_ARGUMENT;
        outputs[i].value = {};
    }
    uint32_t inputs = 0, expected_outputs = 0;
    for (const auto &p : fn->parameters) {
        if (!p.kind)
            return BC_UNSUPPORTED;
        if (p.result) continue;
        if (p.output) {
            ++expected_outputs;
            if (!output_names.contains(p.name)) return BC_INVALID_ARGUMENT;
            continue;
        }
        ++inputs;
        const BcValue *value = nullptr;
        for (uint32_t i = 0; i < count; ++i)
            if (p.name == args[i].name)
                value = &args[i].value;
        if (!value)
            return BC_INVALID_ARGUMENT;
        if (auto e = write_value(owner, p.property, buffer.data(), *value); e != BC_OK)
            return e;
    }
    if (inputs != count || expected_outputs != output_count)
        return BC_INVALID_ARGUMENT;
    obj->ProcessEvent(fn->object, fn->size ? buffer.data() : nullptr);
    BcResult result = BC_OK;
    for (const auto &p : fn->parameters) {
        if (p.result) result = read_value(owner, p.property, buffer.data(), out);
        if (p.output) for (uint32_t i=0;i<output_count;++i) if (p.name==outputs[i].name)
            result = read_value(owner,p.property,buffer.data(),&outputs[i].value);
        if (result != BC_OK) {
            if(out->kind==BC_VALUE_OBJECT && out->data.object) handles.release(owner,out->data.object);
            *out={};
            for(uint32_t i=0;i<output_count;++i) {
                if(outputs[i].value.kind==BC_VALUE_OBJECT && outputs[i].value.data.object)
                    handles.release(owner,outputs[i].value.data.object);
                outputs[i].value={};
            }
            return result;
        }
    }
    return BC_OK;
}
BcResult backend_invoke(uint64_t owner, BcHandle self, BcHandle fn, const BcNamedValue *args, uint32_t count, BcValue *out) {
    return backend_invoke_outputs(owner,self,fn,args,count,nullptr,0,out);
}
BcResult backend_enumerate_actors(uint64_t owner, BcHandle context, const char *path, BcHandle *out, uint32_t capacity, uint32_t *count) {
    if(auto e=thread_check();e!=BC_OK)return e;
    if(!out || !count || !capacity || capacity>512)return BC_INVALID_ARGUMENT;
    *count=0;
    const auto name=wide(path);
    auto *klass=UObjectGlobals::StaticFindObject<UClass *>(nullptr,nullptr,name.c_str());
    auto *actor_class=UObjectGlobals::StaticFindObject<UClass *>(nullptr,nullptr,L"/Script/Engine.Actor");
    if(!klass || !actor_class || !klass->IsChildOf(actor_class))return BC_INVALID_ARGUMENT;
    auto *world=context?object_for(owner,context):nullptr;
    if(context && !world)return BC_STALE_HANDLE;
    std::vector<UObject*> found;
    auto add=[&](UObject *o) {
        if(alive(o) && !o->HasAnyFlags(static_cast<EObjectFlags>(0x18030)) && o->IsA(klass))
            found.push_back(o);
    };
    if(world) {
        std::vector<AActor*> actors;
        UGameplayStatics::GetAllActorsOfClass(world,klass,actors);
        for(auto *a:actors) if(a && a->GetWorld()==world->GetWorld()) add(a);
    } else {
        UObjectGlobals::ForEachUObject([&](UObject *o,int32_t,int32_t) {
            add(o); return found.size()>capacity?RC::LoopAction::Break:RC::LoopAction::Continue;
        });
    }
    if(found.size()>capacity)return BC_LIMIT;
    for(auto *o:found) {
        auto h=handles.intern(owner,o);
        if(!h) {for(uint32_t i=0;i<*count;++i)handles.release(owner,out[i]);*count=0;return BC_LIMIT;}
        out[(*count)++]=*h;
    }
    return BC_OK;
}

BcResult backend_is_a(uint64_t owner,BcHandle handle,const char *path,uint32_t *out) {
    if(auto e=thread_check();e!=BC_OK)return e;
    if(!out)return BC_INVALID_ARGUMENT;*out=0;
    auto *object=object_for(owner,handle);if(!object)return BC_STALE_HANDLE;
    const auto w=wide(path);auto *klass=UObjectGlobals::StaticFindObject<UClass *>(nullptr,nullptr,w.c_str());
    if(!klass)return BC_NOT_FOUND;*out=object->IsA(klass);return BC_OK;
}
BcResult backend_enum(uint64_t, const char *path, const char *name, int64_t *out) {
    if (auto e = thread_check(); e != BC_OK)
        return e;
    if (!out)
        return BC_INVALID_ARGUMENT;
    auto p = wide(path), n = wide(name, 256);
    if (!p.starts_with(L"/Script/"))
        return BC_DENIED;
    auto *obj = UObjectGlobals::StaticFindObject<UObject *>(nullptr, nullptr, p.c_str());
    auto *type = alive(obj) ? Cast<UEnum>(obj) : nullptr;
    if (!type)
        return BC_NOT_FOUND;
    size_t visited = 0;
    for (auto pair : type->ForEachName()) {
        if (++visited > 512)
            return BC_LIMIT;
        if (pair.Key.ToString() == n) {
            *out = pair.Value;
            return BC_OK;
        }
    }
    return BC_NOT_FOUND;
}
