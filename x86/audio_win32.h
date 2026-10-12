#ifndef Q2_PC_AUDIO_H
#define Q2_PC_AUDIO_H
#include "q2/audio.h"
typedef struct q2_pc_audio q2_pc_audio_t;
/** Windows worker downloads bounded MP3/WAV media and plays it through the default audio device. */
q2_result_t q2_pc_audio_create(q2_pc_audio_t **out, q2_audio_backend_t *backend);
/** Cancel and join on process teardown only; never call from a Lua callback. */
q2_result_t q2_pc_audio_destroy(q2_pc_audio_t **audio);
#endif
