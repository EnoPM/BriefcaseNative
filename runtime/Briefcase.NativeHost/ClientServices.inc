static const BcClientModuleApi *client_module{};
static std::atomic<bool> client_unreal_requested{}, client_shutdown_requested{};
static Scope *client_scope(void *context, uint64_t capability) {
    if (environment != Environment::client || !client_module)
        return nullptr;
    return scope(context, capability);
}
static BcResult BC_CALL cr_subscribe(void *c, BcRenderCallback cb, void *user, BcHandle *out) noexcept {
    auto *s = client_scope(c, BC_CAP_CLIENT_RENDER);
    return s ? client_module->subscribe(s->id, cb, user, out) : BC_DENIED;
}
static BcResult BC_CALL cr_unsubscribe(void *c, BcHandle h) noexcept {
    auto *s = client_scope(c, BC_CAP_CLIENT_RENDER);
    return s ? client_module->unsubscribe(s->id, h) : BC_DENIED;
}
static BcResult BC_CALL cr_viewport(void *c, BcViewport *v) noexcept {
    return client_scope(c, BC_CAP_CLIENT_RENDER) ? client_module->viewport(v) : BC_DENIED;
}
static BcResult BC_CALL cr_pixels(void *c, double nx, double ny, float *x, float *y) noexcept {
    if (!client_scope(c, BC_CAP_CLIENT_RENDER))
        return BC_DENIED;
    if (!x || !y || !std::isfinite(nx) || !std::isfinite(ny) || nx < 0 || nx > 1 || ny < 0 || ny > 1)
        return BC_INVALID_ARGUMENT;
    BcViewport v{};
    v.size = sizeof(v);
    auto r = client_module->viewport(&v);
    if (r != BC_OK)
        return r;
    *x = float(nx * v.width);
    *y = float(ny * v.height);
    return BC_OK;
}
static BcResult BC_CALL cr_text(void *c, float x, float y, uint32_t color, const char *text,
                                uint32_t size) noexcept {
    auto *s = client_scope(c, BC_CAP_CLIENT_RENDER);
    return s ? client_module->text(s->id, x, y, color, text, size) : BC_DENIED;
}
static BcResult BC_CALL cr_rectangle(void *c, float x, float y, float w, float h, uint32_t color) noexcept {
    auto *s = client_scope(c, BC_CAP_CLIENT_RENDER);
    return s ? client_module->rectangle(s->id, x, y, w, h, color) : BC_DENIED;
}
static BcResult BC_CALL ci_key(void *c, uint32_t key, BcKeyState *state) noexcept {
    return client_scope(c, BC_CAP_CLIENT_INPUT) ? client_module->key(key, state) : BC_DENIED;
}
static BcResult BC_CALL ci_capturing(void *c, uint32_t *value) noexcept {
    if (!client_scope(c, BC_CAP_CLIENT_INPUT))
        return BC_DENIED;
    if (!value)
        return BC_INVALID_ARGUMENT;
    *value = client_module->capturing();
    return BC_OK;
}
static BcResult BC_CALL cr_line(void *c,float x,float y,float x2,float y2,uint32_t color,float thickness) noexcept {
 auto *s=client_scope(c,BC_CAP_CLIENT_RENDER);return s?client_module->line(s->id,x,y,x2,y2,color,thickness):BC_DENIED;
}
static BcResult BC_CALL cr_circle(void *c,float x,float y,float radius,uint32_t color,float thickness) noexcept {
 auto *s=client_scope(c,BC_CAP_CLIENT_RENDER);return s?client_module->circle(s->id,x,y,radius,color,thickness):BC_DENIED;
}
static const BcClientRenderApi client_render_service{sizeof(BcClientRenderApi),
                                                     1,
                                                     cr_subscribe,
                                                     cr_unsubscribe,
                                                     cr_viewport,
                                                     cr_pixels,
                                                     cr_text,
                                                     cr_rectangle, cr_line, cr_circle};
static const BcClientInputApi client_input_service{sizeof(BcClientInputApi), 1, ci_key, ci_capturing};
