#pragma once
#include "storage.h"
#include "folder_settings.h"

/* The local SQLCipher transaction is authoritative. Queued files remain encrypted
 * on the local disk until the selected folder becomes available again.
 */
typedef enum {
  EF_AUTO_BACKUP_SKIPPED = 0,
  EF_AUTO_BACKUP_CREATED = 1,
  EF_AUTO_BACKUP_FAILED = 2,
  EF_AUTO_BACKUP_QUEUED = 3
} EfAutoBackupResult;

/* Called only after a successful mutation of the local SQLCipher database. */
EfAutoBackupResult ef_auto_backup_after_change(EfStore *store,
  const EfFolderSettings *settings, char **saved_path, GError **error);

/* Safe to retry, including on startup. Never replaces an existing destination.
 * Processes up to 25 files on each call to limit work on the GTK main thread.
 */
gboolean ef_auto_backup_flush(const EfFolderSettings *settings,
  guint *transferred, guint *remaining, GError **error);

/* Includes only correctly named snapshots, not temporary or lock files. */
guint ef_auto_backup_pending_count(GError **error);
