#include "passport_lua.h"

#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"
#include "passport_model.h"

enum { LUA_HEAP_LIMIT = 16 * 1024, LUA_INSTRUCTION_LIMIT = 10000 };

typedef struct {
    const char *script;
    bool compile_only;
    const passport_lua_device_t *device;
    size_t allocated;
} lua_run_t;

static void *bounded_alloc(void *data, void *ptr, size_t old_size, size_t new_size)
{
    lua_run_t *run = data;
    size_t other = run->allocated - (ptr ? old_size : 0);
    if (!new_size) {
        free(ptr);
        run->allocated = other;
        return NULL;
    }
    if (new_size > LUA_HEAP_LIMIT - other) return NULL;
    void *next = realloc(ptr, new_size);
    if (next) run->allocated = other + new_size;
    return next;
}

static void instruction_limit(lua_State *lua, lua_Debug *debug)
{
    (void)debug;
    luaL_error(lua, "instruction limit exceeded");
}

static const passport_lua_device_t *device_for(lua_State *lua)
{
    void *context;
    lua_getallocf(lua, &context);
    return ((lua_run_t *)context)->device;
}

static int battery(lua_State *lua)
{
    int soc = device_for(lua)->battery();
    if (soc < 0) lua_pushnil(lua);
    else lua_pushinteger(lua, soc);
    return 1;
}

static int backlight(lua_State *lua)
{
    lua_Integer percent = luaL_checkinteger(lua, 1);
    luaL_argcheck(lua, percent >= 10 && percent <= 100, 1, "use 10 to 100");
    if (device_for(lua)->backlight((int)percent) != ESP_OK) return luaL_error(lua, "display unavailable");
    return 0;
}

static int run_chunk(lua_State *lua)
{
    lua_run_t *run = lua_touserdata(lua, 1);
    if (luaL_loadbufferx(lua, run->script, strlen(run->script), "passport", "t") != LUA_OK) {
        return lua_error(lua);
    }
    if (run->compile_only) return 0;

    // Only the board functions are available to model-generated code.
    const luaL_Reg functions[] = {{"battery", battery}, {"backlight", backlight}, {NULL, NULL}};
    luaL_newlib(lua, functions);
    lua_setglobal(lua, "passport");
    lua_sethook(lua, instruction_limit, LUA_MASKCOUNT, LUA_INSTRUCTION_LIMIT);
    lua_call(lua, 0, 0);
    return 0;
}

esp_err_t passport_lua_run(const char *script, bool compile_only, const passport_lua_device_t *device,
                          char *error, size_t error_size)
{
    if (!script[0] || strlen(script) > PASSPORT_SCRIPT_BYTES) return ESP_ERR_INVALID_ARG;
    lua_run_t run = {.script = script, .compile_only = compile_only, .device = device};
    lua_State *lua = lua_newstate(bounded_alloc, &run, luaL_makeseed(NULL));
    if (!lua) return ESP_ERR_NO_MEM;

    lua_pushcfunction(lua, run_chunk);
    lua_pushlightuserdata(lua, &run);
    int status = lua_pcall(lua, 1, 0, 0);
    if (status != LUA_OK && error_size) {
        const char *message = lua_tostring(lua, -1);
        if (!message) message = "Lua error";
        size_t bytes = passport_utf8_prefix(message, error_size - 1);
        memcpy(error, message, bytes);
        error[bytes] = '\0';
    }
    lua_close(lua);
    return status == LUA_OK ? ESP_OK : ESP_FAIL;
}
