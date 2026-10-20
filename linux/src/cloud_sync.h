#pragma once
#include "drive_sync.h"

/* Opt-in automatic cloud upload (default FALSE). Credentials remain in rclone. */
gboolean ef_cloud_auto_load(gboolean *enabled,GError **error);
gboolean ef_cloud_auto_save(gboolean enabled,GError **error);

/* Blocking, worker-thread-only, replay-safe upload of up to eight immutable
 * SQLCipher snapshots. Uses per-remote, per-file SHA-256 completion ledger.
 * Partial failures keep unsent snapshots for the next retry.
 */
gboolean ef_cloud_upload_pending(const char *remote,const char *folder,
                                 guint *uploaded,guint *remaining,GError **error);
