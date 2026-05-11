#ifndef SCRIPT_ENGINE_H_
#define SCRIPT_ENGINE_H_

#include <glib.h>

void script_engine_setup(void);
void script_engine_run(const char *script_text);
void script_engine_stop(void);
gboolean script_engine_is_running(void);

typedef void (*ScriptLogFunc)(const char *text, gpointer user_data);
void script_engine_set_log_func(ScriptLogFunc func, gpointer user_data);

typedef void (*ScriptDoneFunc)(gpointer user_data);
void script_engine_set_done_func(ScriptDoneFunc func, gpointer user_data);

#endif
