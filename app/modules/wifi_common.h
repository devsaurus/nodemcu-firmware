#ifndef APP_MODULES_WIFI_COMMON_H_
#define APP_MODULES_WIFI_COMMON_H_

#include "module.h"
#include "lauxlib.h"
#include "platform.h"

#include <string.h>
#include <stdlib.h>
#include "c_types.h"
#include "user_interface.h"
#include "user_config.h"
#include <stdio.h>
#include "task/task.h"

void wifi_add_sprintf_field(lua_State* L, char* name, char* string, ...);
void wifi_add_int_field(lua_State* L, char* name, lua_Integer integer);

static inline void register_lua_cb(lua_State* L,int* cb_ref)
{
  int ref=luaL_ref(L, LUA_REGISTRYINDEX);
  if( *cb_ref != LUA_NOREF)
  {
	luaL_unref(L, LUA_REGISTRYINDEX, *cb_ref);
  }
  *cb_ref = ref;
}

static inline void unregister_lua_cb(lua_State* L, int* cb_ref)
{
  if(*cb_ref != LUA_NOREF)
  {
	luaL_unref(L, LUA_REGISTRYINDEX, *cb_ref);
  	*cb_ref = LUA_NOREF;
  }
}

#ifdef NODE_DEBUG
#define EVENT_DBG(...) printf(__VA_ARGS__)
#else
#define EVENT_DBG(...) //printf(__VA_ARGS__)
#endif

#ifdef WIFI_SDK_EVENT_MONITOR_ENABLE
  extern const LUA_REG_TYPE wifi_event_monitor_map[];
  void wifi_eventmon_init();
#endif
#ifdef WIFI_STATION_STATUS_MONITOR_ENABLE
  int wifi_station_event_mon_start(lua_State* L);
  int wifi_station_event_mon_reg(lua_State* L);
  void wifi_station_event_mon_stop(lua_State* L);
#endif

#endif /* APP_MODULES_WIFI_COMMON_H_ */
