/* miniaudio (public domain / MIT-0) with stb_vorbis (public domain) for New Vegas's .ogg radio. */
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
#include "miniaudio.c"
#undef STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
