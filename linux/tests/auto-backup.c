#define _POSIX_C_SOURCE 200809L
#include "../src/auto_backup.h"
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static guint snapshot_count(const char *folder) {
  GError *error=NULL;
  GDir *dir=g_dir_open(folder,0,&error);
  g_assert_no_error(error);g_assert_nonnull(dir);
  guint result=0;
  const char *name=NULL;
  while((name=g_dir_read_name(dir))) {
    if(g_str_has_prefix(name,"evolufisico-") && g_str_has_suffix(name,".sqlite"))result++;
  }
  g_dir_close(dir);
  return result;
}
static char *snapshot_after(EfStore *store,EfFolderSettings *settings,guint expected) {
  GError *error=NULL;
  char *path=NULL;
  g_assert_cmpint(ef_auto_backup_after_change(store,settings,&path,&error),==,EF_AUTO_BACKUP_CREATED);
  g_assert_no_error(error);
  g_assert_nonnull(path);
  g_assert_true(g_file_test(path,G_FILE_TEST_IS_REGULAR));
  GStatBuf sb;
  g_assert_cmpint(g_stat(path,&sb),==,0);
  g_assert_cmpint(sb.st_mode & 077,==,0);
  FILE *file=fopen(path,"rb");
  g_assert_nonnull(file);
  char header[16]={0};
  g_assert_cmpint(fread(header,1,sizeof(header),file),==,16);
  fclose(file);
  g_assert_cmpint(memcmp(header,"SQLite format 3",15),!=,0);
  g_assert_cmpuint(snapshot_count(settings->folder),==,expected);
  return path;
}
int main(void) {
  GError *error=NULL;
  char *root=g_dir_make_tmp("evolufisico-auto-backup-XXXXXX",&error);
  g_assert_no_error(error);g_assert_nonnull(root);
  char *local=g_build_filename(root,"local",NULL);
  char *conf=g_build_filename(root,"config",NULL);
  char *folder=g_build_filename(root,"copies",NULL);
  g_assert_cmpint(g_mkdir(local,0700),==,0);
  g_assert_cmpint(g_mkdir(conf,0700),==,0);
  g_assert_cmpint(g_mkdir(folder,0700),==,0);
  g_setenv("XDG_DATA_HOME",local,TRUE);
  g_setenv("XDG_CONFIG_HOME",conf,TRUE);
  g_setenv("EVOLUFISICO_DB_PASSPHRASE","QA-fictitious-encrypted-snapshots-2026!",TRUE);
  EfStore store={0};
  g_assert_true(ef_store_open(&store,&error));
  g_assert_no_error(error);
  EfFolderSettings settings={0};
  char *saved=NULL;
  g_assert_cmpint(ef_auto_backup_after_change(&store,&settings,&saved,&error),==,EF_AUTO_BACKUP_SKIPPED);
  g_assert_no_error(error);g_assert_null(saved);

  g_assert_true(ef_folder_settings_save(&settings,folder,&error));
  g_assert_no_error(error);
  ef_folder_settings_clear(&settings);
  g_assert_true(ef_folder_settings_load(&settings,&error));
  g_assert_no_error(error);
  g_assert_cmpstr(settings.folder,==,folder);
  char *paths[10]={0};

  g_assert_true(ef_store_add_client(&store,"Cliente Fictício",&error));
  paths[0]=snapshot_after(&store,&settings,1);
  g_assert_true(ef_store_rename_client(&store,1,"Cliente Renomeado",&error));
  paths[1]=snapshot_after(&store,&settings,2);
  g_assert_true(ef_store_add_assessment(&store,1,"2026-10-19",70.0,170.0,&error));
  paths[2]=snapshot_after(&store,&settings,3);
  g_assert_true(ef_store_add_charge(&store,1,2500,"Exemplo",&error));
  paths[3]=snapshot_after(&store,&settings,4);
  g_assert_true(ef_store_receive_charge(&store,1,1,&error));
  paths[4]=snapshot_after(&store,&settings,5);
  g_assert_false(ef_store_receive_charge(&store,1,1,&error));
  g_clear_error(&error);
  g_assert_cmpuint(snapshot_count(folder),==,5); /* failed mutation has no copy */
  g_assert_true(ef_store_delete_assessment_for_client(&store,1,1,&error));
  paths[5]=snapshot_after(&store,&settings,6);
  g_assert_true(ef_store_delete_charge_for_client(&store,1,1,&error));
  paths[6]=snapshot_after(&store,&settings,7);
  g_assert_true(ef_store_delete_client(&store,1,&error));
  paths[7]=snapshot_after(&store,&settings,8);
  g_assert_no_error(error);

  /* Offline folder: local commit succeeds, encrypted snapshot survives process restart. */
  char *disconnected=g_build_filename(root,"copies-disconnected",NULL);
  g_assert_cmpint(g_rename(folder,disconnected),==,0);
  g_assert_true(ef_store_add_client(&store,"Cliente Offline",&error));
  g_assert_cmpint(ef_auto_backup_after_change(&store,&settings,&saved,&error),==,EF_AUTO_BACKUP_QUEUED);
  g_assert_null(saved);
  g_assert_nonnull(error);g_clear_error(&error);
  g_assert_cmpuint(ef_auto_backup_pending_count(&error),==,1);
  g_assert_no_error(error);
  GPtrArray *rows=ef_store_list_clients(&store,&error);
  g_assert_no_error(error);g_assert_cmpuint(rows->len,==,1);
  g_ptr_array_unref(rows);
  ef_folder_settings_clear(&settings);
  g_assert_true(ef_folder_settings_load(&settings,&error));g_assert_no_error(error);
  g_assert_cmpstr(settings.folder,==,folder);
  guint sent=123,remaining=123;
  g_assert_false(ef_auto_backup_flush(&settings,&sent,&remaining,&error));
  g_assert_nonnull(error);g_clear_error(&error);
  g_assert_cmpuint(sent,==,0);g_assert_cmpuint(remaining,==,1);

  char *queuedir=g_build_filename(local,"evolufisico","pending-snapshots",NULL);
  GDir *pendingdir=g_dir_open(queuedir,0,&error);
  g_assert_no_error(error);g_assert_nonnull(pendingdir);
  char *queued_name=NULL;
  const char *pending_name=NULL;
  while((pending_name=g_dir_read_name(pendingdir))) {
    if(g_str_has_prefix(pending_name,"evolufisico-") &&
       g_str_has_suffix(pending_name,".sqlite")) {
      queued_name=g_strdup(pending_name);break;
    }
  }
  g_dir_close(pendingdir);g_assert_nonnull(queued_name);
  char *queued_file=g_build_filename(queuedir,queued_name,NULL);
  g_assert_true(ef_store_verify_snapshot(queued_file,&error));g_assert_no_error(error);
  g_assert_cmpint(g_rename(disconnected,folder),==,0);

  /* Never overwrite an existing foreign file; leave the encrypted snapshot queued. */
  char *conflict=g_build_filename(folder,queued_name,NULL);
  g_assert_true(g_file_set_contents(conflict,"do-not-overwrite",-1,&error));
  g_assert_no_error(error);
  g_assert_false(ef_auto_backup_flush(&settings,&sent,&remaining,&error));
  g_assert_nonnull(error);g_clear_error(&error);
  g_assert_cmpuint(remaining,==,1);
  char *sentinel=NULL;
  g_assert_true(g_file_get_contents(conflict,&sentinel,NULL,&error));
  g_assert_no_error(error);g_assert_cmpstr(sentinel,==,"do-not-overwrite");
  g_free(sentinel);
  g_assert_cmpint(g_unlink(conflict),==,0);
  g_assert_true(ef_auto_backup_flush(&settings,&sent,&remaining,&error));
  g_assert_no_error(error);g_assert_cmpuint(sent,==,1);
  g_assert_cmpuint(remaining,==,0);
  g_assert_cmpuint(ef_auto_backup_pending_count(&error),==,0);
  g_assert_no_error(error);
  g_assert_cmpuint(snapshot_count(folder),==,9);
  g_assert_true(ef_store_verify_snapshot(conflict,&error));g_assert_no_error(error);
  g_free(queued_file);g_free(queued_name);g_free(conflict);
  paths[8]=snapshot_after(&store,&settings,10);
  /* Restore a previous encrypted version and produce its own post-restore copy. */
  g_assert_true(ef_store_restore(&store,paths[4],&error));
  g_assert_no_error(error);
  rows=ef_store_list_clients(&store,&error);
  g_assert_no_error(error);g_assert_cmpuint(rows->len,==,1);
  g_ptr_array_unref(rows);
  paths[9]=snapshot_after(&store,&settings,11);

  for(guint i=0;i<10;i++){g_assert_nonnull(paths[i]);g_assert_cmpint(g_unlink(paths[i]),==,0);g_free(paths[i]);}
  /* Delete the ninth snapshot, which was replayed from the offline queue. */
  GDir *copies=g_dir_open(folder,0,&error);
  g_assert_no_error(error);g_assert_nonnull(copies);
  const char *item=NULL;
  guint extras=0;
  while((item=g_dir_read_name(copies))) {
    if(g_str_has_prefix(item,"evolufisico-") && g_str_has_suffix(item,".sqlite")) {
      char *extra=g_build_filename(folder,item,NULL);
      g_assert_cmpint(g_unlink(extra),==,0);
      g_free(extra);extras++;
    }
  }
  g_dir_close(copies);g_assert_cmpuint(extras,==,1);
  ef_folder_settings_clear(&settings);
  ef_store_close(&store);
  char *database=g_build_filename(local,"evolufisico","records.sqlite",NULL);
  char *datadir=g_path_get_dirname(database);
  char *configfile=g_build_filename(conf,"evolufisico","sync-folder.ini",NULL);
  char *configdir=g_path_get_dirname(configfile);
  g_assert_cmpint(g_unlink(database),==,0);
  g_assert_cmpint(g_unlink(configfile),==,0);
  char *lockpath=g_build_filename(queuedir,".transfer.lock",NULL);
  g_assert_cmpint(g_unlink(lockpath),==,0);
  g_assert_cmpint(g_rmdir(queuedir),==,0);
  g_free(lockpath);g_free(queuedir);
  g_assert_cmpint(g_rmdir(datadir),==,0);
  g_assert_cmpint(g_rmdir(configdir),==,0);
  g_assert_cmpint(g_rmdir(local),==,0);
  g_assert_cmpint(g_rmdir(conf),==,0);
  g_assert_cmpint(g_rmdir(folder),==,0);
  g_assert_cmpint(g_rmdir(root),==,0);
  g_free(database);g_free(datadir);g_free(configfile);g_free(configdir);
  g_free(local);g_free(conf);g_free(folder);g_free(disconnected);g_free(root);
  g_print("PASS: every successful mutation creates an encrypted snapshot; failures preserve local commits\n");
  return 0;
}
