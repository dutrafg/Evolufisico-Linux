#pragma once
#include <glib.h>
#include <gio/gio.h>

/* Only the rclone remote name is persisted. OAuth tokens remain with rclone. */
gboolean ef_drive_valid_remote(const char *name);
char *ef_drive_load_remote(GError **error);
gboolean ef_drive_save_remote(const char *name, GError **error);
gboolean ef_drive_open_oauth_setup(GError **error);

/* Blocking functions: call in worker threads, NEVER on the GTK main thread.
 * No deletes, downloads or bidirectional sync. All uploads use --immutable.
 */
gboolean ef_drive_probe(const char *remote, GError **error);
gboolean ef_drive_upload_encrypted(const char *remote, const char *folder,
                                   guint *verified_files, GError **error);
