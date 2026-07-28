#pragma once
/* Stub for native/dev builds: EMSCRIPTEN_KEEPALIVE keeps jacl_emit_ir exported.
   Under a real emcc build the SDK header is used instead. */
#define EMSCRIPTEN_KEEPALIVE __attribute__((used, visibility("default")))
