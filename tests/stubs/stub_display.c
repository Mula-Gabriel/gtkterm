/* Bouchons de l'affichage et du tampon : enregistrent les octets envoyés et reçus. */

#include <glib.h>

#include "buffer.h"
#include "stubs.h"

static GMutex octets_mutex;
static GByteArray *envoyes = NULL;
static GByteArray *recus = NULL;
static GThread *thread_envoi = NULL;
static void (*ecoute)(const char *, unsigned int) = NULL;

void
stub_display_reset(void)
{
	g_mutex_lock(&octets_mutex);
	if(envoyes != NULL)
		g_byte_array_set_size(envoyes, 0);
	if(recus != NULL)
		g_byte_array_set_size(recus, 0);
	thread_envoi = NULL;
	g_mutex_unlock(&octets_mutex);
}

gint
send_serial(gchar *string, gint len)
{
	g_mutex_lock(&octets_mutex);
	if(envoyes == NULL)
		envoyes = g_byte_array_new();
	g_byte_array_append(envoyes, (const guint8 *)string, len);
	thread_envoi = g_thread_self();
	g_mutex_unlock(&octets_mutex);
	stub_record("send_serial", NULL);
	return len;
}

void
put_chars(const char *chars, unsigned int size, gboolean crlf_auto)
{
	g_mutex_lock(&octets_mutex);
	if(recus == NULL)
		recus = g_byte_array_new();
	g_byte_array_append(recus, (const guint8 *)chars, size);
	g_mutex_unlock(&octets_mutex);
}

void
set_tap_func(void (*func)(const char *, unsigned int))
{
	ecoute = func;
}

void
unset_tap_func(void)
{
	ecoute = NULL;
}

void
clear_buffer(void)
{
	stub_record("clear_buffer", NULL);
}

GByteArray *
stub_sent(void)
{
	if(envoyes == NULL)
		envoyes = g_byte_array_new();
	return envoyes;
}

GThread *
stub_send_thread(void)
{
	return thread_envoi;
}

GByteArray *
stub_received(void)
{
	if(recus == NULL)
		recus = g_byte_array_new();
	return recus;
}

void
stub_inject_rx(const gchar *data, guint len)
{
	if(ecoute != NULL)
		ecoute(data, len);
}
