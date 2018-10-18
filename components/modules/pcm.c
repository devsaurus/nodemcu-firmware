
#include <string.h>

#include "module.h"
#include "lauxlib.h"
#include "lmem.h"

#include "esp_log.h"

#define MINIMP3_NO_STDIO
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION

// refer https://github.com/lieff/minimp3/issues/44
#define MAX_FRAME_SYNC_MATCHES 3

#include "minimp3/minimp3.h"


//static const char *TAG = "mp3dec";

static struct {
  bool init_done;
  int cb_ref;
  size_t buffer_size;
  size_t buffer_used;
  char *buffer;
  mp3d_sample_t *pcm;
  mp3dec_t mp3d;
} mp3dec_state;

static struct {
  int8_t volume;
} pcm_state;


static void fill_buffer( lua_State *L )
{
  bool active = true;

  while (mp3dec_state.buffer_used < mp3dec_state.buffer_size && active) {
    size_t headroom = mp3dec_state.buffer_size - mp3dec_state.buffer_used;
    lua_rawgeti(L, LUA_REGISTRYINDEX, mp3dec_state.cb_ref);
    lua_pushinteger( L, headroom );
    lua_call( L, 1, 1 );

    if (lua_isstring( L, -1 )) {
      size_t len;
      const char *data = luaL_checklstring( L, -1, &len );
      if (len > 0) {
        if (len > headroom)
          luaL_error( L, "callback returned too many bytes" );
        memcpy( mp3dec_state.buffer + mp3dec_state.buffer_used, data, len );
        mp3dec_state.buffer_used += len;
      } else
        active = false;
    } else
      active = false;
    lua_pop( L, 1 );
  }
}

static int lmp3dec_decode( lua_State *L )
{
  if (!mp3dec_state.init_done)
    return luaL_error( L, "not initialized" );

  bool frame_info = false;
  if (lua_isboolean( L, 1 ))
    frame_info = lua_toboolean( L, 1 );

  mp3dec_frame_info_t info;
  int samples;
  do {
    fill_buffer( L );

    samples = mp3dec_decode_frame( &mp3dec_state.mp3d,
                                   (uint8_t *)mp3dec_state.buffer,
                                   mp3dec_state.buffer_used,
                                   mp3dec_state.pcm, &info );
    if (info.frame_bytes > 0) {
      memmove( mp3dec_state.buffer, mp3dec_state.buffer + info.frame_bytes,
               mp3dec_state.buffer_used - info.frame_bytes );
      mp3dec_state.buffer_used -= info.frame_bytes;
    }
    //ESP_LOGI(TAG, "loop: samples %d, framebytes %d, buffer_used %d", samples, info.frame_bytes, mp3dec_state.buffer_used);
  } while (samples == 0 && info.frame_bytes > 0 && mp3dec_state.buffer_used > 0);

  //ESP_LOGI(TAG, "samples %d, framebytes %d, buffer_used %d", samples, info.frame_bytes, mp3dec_state.buffer_used);

  if (samples > 0 && info.frame_bytes > 0) {
    size_t byte_len = samples*sizeof( mp3d_sample_t ) * info.channels;
    size_t int_len = byte_len >> 1;
    size_t pos;
    for (pos = 0; pos < int_len; pos++) {

      // apply volume setting
      int32_t sample = (int32_t)(mp3dec_state.pcm[pos]);
      if (pcm_state.volume > 0)
        sample *= pcm_state.volume;
      else if (pcm_state.volume < 0)
        sample /= -pcm_state.volume;

      // clip
      sample = sample > INT16_MAX ? INT16_MAX : sample;
      sample = sample < INT16_MIN ? INT16_MIN : sample;

      // convert to unsigned
      mp3dec_state.pcm[pos] = (mp3d_sample_t)(sample + 0x8000);
    }

    lua_pushlstring( L, (char *)mp3dec_state.pcm, byte_len );
  } else {
    lua_pushnil( L );
  }
  int ret = 1;

  if (frame_info) {
    lua_createtable( L, 0, 4 );
    lua_pushinteger( L, info.channels );
    lua_setfield( L, -2, "channels" );
    lua_pushinteger( L, info.hz );
    lua_setfield( L, -2, "hz" );
    lua_pushinteger( L, info.layer );
    lua_setfield( L, -2, "layer" );
    lua_pushinteger( L, info.bitrate_kbps );
    lua_setfield( L, -2, "bitrate_kbps" );

    ret++;
  }

  return ret;
}


static int lmp3dec_init( lua_State *L )
{
  int stack = 0;

  mp3dec_state.buffer_size = luaL_checkint( L, ++stack );

  if (!lua_isfunction( L, ++stack ))
    return luaL_argerror( L, stack, "callback needed" );
  lua_pushvalue( L, stack );
  mp3dec_state.cb_ref = luaL_ref( L, LUA_REGISTRYINDEX );

  if ((mp3dec_state.buffer = (char *)malloc( mp3dec_state.buffer_size )) != NULL) {
    mp3dec_state.buffer_used = 0;

    if ((mp3dec_state.pcm = (mp3d_sample_t *)malloc( sizeof( mp3d_sample_t ) * MINIMP3_MAX_SAMPLES_PER_FRAME )) != NULL) {
      mp3dec_state.init_done = true;

      mp3dec_init(&mp3dec_state.mp3d);

      return 0;
    } else {
      free( mp3dec_state.buffer );
    }
  }

  return luaL_error( L, "no memory" );
}

static int lmp3dec_close( lua_State *L )
{
  if (!mp3dec_state.init_done)
    return 0;

  mp3dec_state.init_done = false;
  free( mp3dec_state.buffer );
  luaL_unref( L, LUA_REGISTRYINDEX, mp3dec_state.cb_ref );
  free( mp3dec_state.pcm );

  return 0;
}

static int lpcm_volume( lua_State *L )
{
  int stack = 0;

  if (lua_isnumber( L, ++stack )) {
    int volume = luaL_checkint( L, stack );
    luaL_argcheck( L, volume >= -32 && volume <= 32, stack, "out of range" );
    pcm_state.volume = (int8_t)volume;
  }

  lua_pushinteger( L, pcm_state.volume );
  return 1;
}


static const LUA_REG_TYPE mp3dec_map[] =
{
  { LSTRKEY( "init" ),   LFUNCVAL( lmp3dec_init ) },
  { LSTRKEY( "decode" ), LFUNCVAL( lmp3dec_decode ) },
  { LSTRKEY( "close" ),  LFUNCVAL( lmp3dec_close ) },
  { LSTRKEY( "L3_FRAME_BYTES" ), LNUMVAL( MAX_L3_FRAME_PAYLOAD_BYTES ) },
  { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE pcm_map[] =
{
  { LSTRKEY( "mp3dec" ), LROVAL( mp3dec_map ) },
  { LSTRKEY( "volume" ), LFUNCVAL( lpcm_volume ) },
  { LNILKEY, LNILVAL }
};

int luaopen_pcm( lua_State *L )
{
  pcm_state.volume = 0;
  mp3dec_state.init_done = false;

  return 0;
}

NODEMCU_MODULE(PCM, "pcm", pcm_map, luaopen_pcm);
