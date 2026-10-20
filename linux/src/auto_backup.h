#pragma once
#include "storage.h"
#include "folder_settings.h"

/* Local SQLCipher remains authoritative. The optional snapshot is best-effort.
 * Call only after a successful database mutation.
 */
typedef enum {
  EF_AUTO_BACKUP_SKIPPED = 0,
  EF_AUTO_BACKUP_CREATED = 1,
  EF_AUTO_BACKUP_FAILED = 2
} EfAutoBackupResult;

EfAutoBackupResult ef_auto_backup_after_change(EfStore *store,
  const EfFolderSettings *settings, char **saved_path, GError **error);
