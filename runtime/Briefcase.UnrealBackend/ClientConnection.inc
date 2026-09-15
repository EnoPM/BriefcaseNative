// Private client action, dispatched through the existing game-thread queue.
// Mirrors CommunityServerConnection.cs and the generated SDK for 6A96564B-06283000.
BcResult backend_client_connect(std::string_view endpoint, std::string &error) {
    if (auto result = thread_check(); result != BC_OK)
        return result;
    auto *klass = UObjectGlobals::StaticFindObject<UClass *>(nullptr, nullptr,
                                                             L"/Script/DeceiveInc.EOSServerBrowserSubsystem");
    auto *fn = UObjectGlobals::StaticFindObject<UFunction *>(
        nullptr, nullptr, L"/Script/DeceiveInc.EOSServerBrowserSubsystem:DirectConnect");
    if (!klass || !fn) {
        error = "DirectConnect is unavailable in this build.";
        return BC_NOT_FOUND;
    }
    std::array<ConnectParameter, 2> params{};
    std::array<std::wstring, 2> names;
    size_t count = 0;
    for (auto *p : fn->ForEachProperty()) {
        if (!p->HasAnyPropertyFlags(CPF_Parm))
            continue;
        if (count == 2)
            return BC_VERSION_MISMATCH;
        names[count] = p->GetName();
        params[count] = {names[count],
                         p->GetOffset_Internal(),
                         p->GetElementSize(),
                         p->GetArrayDim(),
                         p->GetClass().GetName() == L"StrProperty",
                         !p->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm)};
        ++count;
    }
    if (params[0].offset > params[1].offset)
        std::swap(params[0], params[1]);
    if (count != 2 || fn->GetNumParms() != 2 || !direct_connect_contract(fn->GetParmsSize(), params)) {
        error = "Incompatible DirectConnect signature.";
        return BC_VERSION_MISMATCH;
    }
    UObject *browser{};
    UObjectGlobals::ForEachUObject([&](UObject *object, int32_t, int32_t) {
        if (alive(object) &&
            !object->HasAnyFlags(static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject)) &&
            object->IsA(klass)) {
            browser = object;
            return RC::LoopAction::Break;
        }
        return RC::LoopAction::Continue;
    });
    if (!browser) {
        error = "Open the main game menu first: server browser unavailable.";
        return BC_NOT_READY;
    }
    // Borrowed, input-only UTF-16 buffers remain alive throughout synchronous ProcessEvent.
    // No engine allocation or FString destructor owns this memory.
    auto address = wide(std::string(endpoint).c_str(), 255);
    wchar_t empty[1]{};
    struct StringView {
        const wchar_t *data;
        int32_t count, capacity;
    };
    struct Parameters {
        StringView ip_port, password;
    };
    static_assert(sizeof(Parameters) == 32);
    Parameters parameters{{address.c_str(), int32_t(address.size() + 1), int32_t(address.size() + 1)},
                          {empty, 1, 1}};
    browser->ProcessEvent(fn, &parameters);
    log("Client DirectConnect dispatched to " + std::string(endpoint));
    return BC_OK;
}
