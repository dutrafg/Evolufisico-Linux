#pragma once
#include <glib.h>

/* Persistent local folder choice. No cloud tokens or automatic network sync. */
typedef struct { char *folder; } EfFolderSettings;

void ef_folder_settings_clear(EfFolderSettings *settings);
gboolean ef_folder_settings_validate(const char *folder, GError **error);
gboolean ef_folder_settings_load(EfFolderSettings *settings, GError **error);
gboolean ef_folder_settings_save(EfFolderSettings *settings, const char *folder, GError **error);
