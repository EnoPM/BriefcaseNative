# Native client API v1

Header: `sdk/Briefcase.ClientModApi/include/Briefcase/ClientModApi.h`.
Complete sample: `samples/Briefcase.NativeOverlaySample/Overlay.cpp`.

The shared `BcApi` v1 ABI remains 80 bytes, with its historical 72-byte prefix
unchanged. Request services with
`get_service(context, name, 1, &service)`, then check both the result and `size`.

| Service | Manifest capability | Table |
| --- | --- | --- |
| `briefcase.client.render` | `client.render` | `BcClientRenderApi` |
| `briefcase.client.input` | `client.input` | `BcClientInputApi` |

The server returns `BC_DENIED` for these services without loading a graphics
module. Package environments (`client`, `server`, `both`) are filtered before
loading native libraries. Client mods use the `ready` phase; the pre-entry-point
`startup` phase remains server-only.

## Rendering

- `subscribe` returns a handle owned by the mod. Limits are 16 callbacks per mod
  and 128 overall. They run on the thread presenting the frame.
- `unsubscribe` rejects another owner's handle. Removing a callback during dispatch
  prevents later invocations; a callback removed before its turn is not called.
- The host invalidates callbacks before logical mod unload. Dispatch and removal
  are serialized. Libraries remain mapped until process exit; there is no hot reload.
- `viewport` returns physical backbuffer size, DPI, frame number and interval. It
  returns `BC_NOT_READY` before graphics discovery.
- `to_pixels` maps normalized `[0,1]` coordinates to `[0,width] / [0,height]` edge
  coordinates with a top-left origin. NaN, infinity and out-of-range values fail.
- `text` and `rectangle` work only inside an owner callback. Colors use
  `0xAABBGGRR`; text is explicit-length UTF-8 up to 4,096 bytes. Each callback may
  issue at most 1,024 primitives per frame.
- Mod primitives render behind the framework menu. The API exposes no graphics
  device, Unreal object or ImGui context.

Callbacks begin after lazy context creation. Open the F1 menu once after shader
compilation; the sample then keeps drawing while the menu is closed. Never access
Unreal from a render callback.

## Input and lifetime

`key` accepts Windows virtual-key values from 1 to 255. `down`, `pressed` and
`released` remain stable for one `frame_number`; repeated reads do not consume an
edge. Focus loss clears published states. Reading a key does not capture it.

`capturing` reports whether the framework menu is open and focused. The public API
does not expose input simulation.

Exported mod functions and callbacks must contain their exceptions. The host and
renderer also catch C++ exceptions at mod call sites and disable a failing render
callback. The sample imports only the Briefcase ABI and owns neither ImGui nor a
graphics hook.
