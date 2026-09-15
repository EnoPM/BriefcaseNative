#include <Briefcase/ClientModApi.h>
#include <chrono>
#include <cstdio>
#include <cstring>
static const BcApi *api;
static const BcClientRenderApi *render;
static BcHandle registration;
static double total_us;
static uint64_t frames;
static void message(const char *text) noexcept {
    api->log(api->context, 1, text, uint32_t(std::strlen(text)));
}
static void BC_CALL draw(const BcClientFrame *frame, void *) noexcept {
    const auto begin = std::chrono::steady_clock::now();
    const float dpi = frame->viewport.dpi_scale;
    char line[160]{};
    const int length = std::snprintf(line, sizeof(line), "Briefcase native overlay  |  callback %.2f us",
                                     frames ? total_us / frames : 0.);
    if (length > 0) {
        render->rectangle(api->context, 18 * dpi, 18 * dpi, 410 * dpi, 36 * dpi, 0xCF201424);
        render->text(api->context, 28 * dpi, 25 * dpi, 0xFFF3D6EA, line, uint32_t(length));
    }
    total_us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count();
    ++frames;
}
extern "C" BC_EXPORT BcResult BC_CALL BriefcaseModLoad(const BcApi *host) noexcept {
    if (!host || host->size < sizeof(BcApi) || host->version != BC_API_VERSION)
        return BC_VERSION_MISMATCH;
    api = host;
    const void *service{};
    const auto result = api->get_service(api->context, BC_CLIENT_RENDER_SERVICE, 1, &service);
    if (result != BC_OK)
        return result;
    render = static_cast<const BcClientRenderApi *>(service);
    if (!render || render->size < sizeof(*render))
        return BC_VERSION_MISMATCH;
    const auto subscribed = render->subscribe(api->context, draw, nullptr, &registration);
    if (subscribed == BC_OK)
        message("Overlay sample loaded; public render callback registered; waits for lazy framework context");
    return subscribed;
}
extern "C" BC_EXPORT void BC_CALL BriefcaseModUnload() noexcept {
    if (render && registration) {
        // The host may already have invalidated this owner.
        render->unsubscribe(api->context, registration);
        registration = 0;
    }
    char report[160]{};
    std::snprintf(report, sizeof(report),
                  "Overlay sample unloaded cleanly; average callback %.3f us across %llu frames",
                  frames ? total_us / frames : 0., static_cast<unsigned long long>(frames));
    message(report);
}
