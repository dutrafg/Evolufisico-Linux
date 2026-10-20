#include "auto_backup.h"

EfAutoBackupResult ef_auto_backup_after_change(EfStore *store,
  const EfFolderSettings *settings, char **saved_path, GError **error) {
  if(saved_path)*saved_path=NULL;
  if(!settings || !settings->folder || !settings->folder[0])
    return EF_AUTO_BACKUP_SKIPPED;
  if(!ef_folder_settings_validate(settings->folder,error))
    return EF_AUTO_BACKUP_FAILED;
  if(!ef_store_snapshot_to_folder(store,settings->folder,saved_path,error))
    return EF_AUTO_BACKUP_FAILED;
  return EF_AUTO_BACKUP_CREATED;
}
