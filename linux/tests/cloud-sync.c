#define _POSIX_C_SOURCE 200809L
#include "../src/cloud_sync.h"
#include "../src/storage.h"
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
static guint call_count(const char *path) {
  gchar *v=NULL;
  if(!g_file_get_contents(path,&v,NULL,NULL))return 0;
  guint n=0;
  const char *p=v;
  while((p=strstr(p,"copyto\n"))){n++;p+=7;}
  g_free(v);return n;
}
int main(void) {
  GError *e=NULL;
  char *root=g_dir_make_tmp("evolufisico-cloud-XXXXXX",&e);
  g_assert_no_error(e);g_assert_nonnull(root);
  char *conf=g_build_filename(root,"config",NULL);
  char *data=g_build_filename(root,"data",NULL);
  char *folder=g_build_filename(root,"backups",NULL);
  char *bin=g_build_filename(root,"bin",NULL);
  g_assert_cmpint(g_mkdir(conf,0700),==,0);
  g_assert_cmpint(g_mkdir(data,0700),==,0);
  g_assert_cmpint(g_mkdir(folder,0700),==,0);
  g_assert_cmpint(g_mkdir(bin,0700),==,0);
  g_setenv("XDG_CONFIG_HOME",conf,TRUE);
  g_setenv("XDG_DATA_HOME",data,TRUE);
  g_setenv("EVOLUFISICO_DB_PASSPHRASE","Cloud-Fictitious-Testing-2026!",TRUE);
  gboolean enabled=TRUE;
  g_assert_true(ef_cloud_auto_load(&enabled,&e));g_assert_no_error(e);
  g_assert_false(enabled);
  g_assert_true(ef_cloud_auto_save(TRUE,&e));g_assert_no_error(e);
  g_assert_true(ef_cloud_auto_load(&enabled,&e));g_assert_true(enabled);
  EfStore db={0};
  g_assert_true(ef_store_open(&db,&e));g_assert_no_error(e);
  g_assert_true(ef_store_add_client(&db,"Dados fictícios",&e));g_assert_no_error(e);
  char *snapshots[11]={0};
  for(guint i=0;i<10;i++) {
    g_assert_true(ef_store_snapshot_to_folder(&db,folder,&snapshots[i],&e));
    g_assert_no_error(e);
  }
  char *log=g_build_filename(root,"rclone.log",NULL);
  char *mock=g_build_filename(bin,"rclone",NULL);
  const char *script="#!/bin/sh\nprintf '%s\\n' \"$@\" >> \"$MOCK_RCLONE_LOG\"\nif [ \"$MOCK_RCLONE_FAIL\" = 1 ]; then exit 17; fi\nexit 0\n";
  g_assert_true(g_file_set_contents(mock,script,-1,&e));g_assert_no_error(e);
  g_assert_cmpint(g_chmod(mock,0700),==,0);
  char *original_path=g_strdup(g_getenv("PATH"));
  char *new_path=g_strdup_printf("%s:%s",bin,original_path?original_path:"/usr/bin");
  g_setenv("PATH",new_path,TRUE);g_setenv("MOCK_RCLONE_LOG",log,TRUE);
  g_setenv("MOCK_RCLONE_FAIL","0",TRUE);g_free(new_path);
  guint sent=0,pending=0;
  g_assert_false(ef_cloud_upload_pending("-invalid",folder,&sent,&pending,&e));
  g_clear_error(&e);
  g_assert_true(ef_cloud_upload_pending("drive_qa",folder,&sent,&pending,&e));
  g_assert_no_error(e);g_assert_cmpuint(sent,==,8);g_assert_cmpuint(pending,==,2);
  g_assert_cmpuint(call_count(log),==,8);
  g_assert_true(ef_cloud_upload_pending("drive_qa",folder,&sent,&pending,&e));
  g_assert_no_error(e);g_assert_cmpuint(sent,==,2);g_assert_cmpuint(pending,==,0);
  g_assert_cmpuint(call_count(log),==,10);
  g_assert_true(ef_cloud_upload_pending("drive_qa",folder,&sent,&pending,&e));
  g_assert_no_error(e);g_assert_cmpuint(sent,==,0);g_assert_cmpuint(pending,==,0);
  g_assert_cmpuint(call_count(log),==,10);
  g_assert_true(ef_store_add_charge(&db,1,2500,"Teste",&e));g_assert_no_error(e);
  g_assert_true(ef_store_snapshot_to_folder(&db,folder,&snapshots[10],&e));
  g_assert_no_error(e);
  g_setenv("MOCK_RCLONE_FAIL","1",TRUE);
  g_assert_false(ef_cloud_upload_pending("drive_qa",folder,&sent,&pending,&e));
  g_assert_nonnull(e);g_clear_error(&e);
  g_assert_cmpuint(sent,==,0);g_assert_cmpuint(pending,>=,1);
  g_setenv("MOCK_RCLONE_FAIL","0",TRUE);
  g_assert_true(ef_cloud_upload_pending("drive_qa",folder,&sent,&pending,&e));
  g_assert_no_error(e);g_assert_cmpuint(sent,==,1);g_assert_cmpuint(pending,==,0);
  g_assert_true(g_file_test(snapshots[10],G_FILE_TEST_EXISTS));
  /* A different Google account gets a separate completion ledger. */
  g_assert_true(ef_cloud_upload_pending("second_drive",folder,&sent,&pending,&e));
  g_assert_no_error(e);g_assert_cmpuint(sent,==,8);g_assert_cmpuint(pending,==,3);
  /* Reject a forged symlink or unencrypted snapshot-looking file. */
  char *trap=g_build_filename(folder,"evolufisico-0000-trap.sqlite",NULL);
  g_assert_cmpint(symlink(snapshots[0],trap),==,0);
  g_assert_false(ef_cloud_upload_pending("drive_qa",folder,&sent,&pending,&e));
  g_clear_error(&e);g_assert_cmpint(g_unlink(trap),==,0);
  g_assert_true(g_file_set_contents(trap,"not SQLCipher",-1,&e));
  g_assert_false(ef_cloud_upload_pending("drive_qa",folder,&sent,&pending,&e));
  g_clear_error(&e);g_assert_cmpint(g_unlink(trap),==,0);
  g_assert_true(ef_cloud_auto_save(FALSE,&e));g_assert_no_error(e);
  enabled=TRUE;
  g_assert_true(ef_cloud_auto_load(&enabled,&e));g_assert_no_error(e);
  g_assert_false(enabled);
  char *settingsdir=g_build_filename(conf,"evolufisico",NULL);
  char *setting=g_build_filename(settingsdir,"drive-auto.ini",NULL);
  GStatBuf sb;
  g_assert_cmpint(g_stat(setting,&sb),==,0);
  g_assert_cmpint(sb.st_mode & 077,==,0);
  char *victim=g_build_filename(root,"victim",NULL);
  g_assert_true(g_file_set_contents(victim,"KEEP",-1,&e));g_assert_no_error(e);
  g_assert_cmpint(g_unlink(setting),==,0);
  g_assert_cmpint(symlink(victim,setting),==,0);
  g_assert_false(ef_cloud_auto_save(TRUE,&e));g_clear_error(&e);
  g_assert_false(ef_cloud_auto_load(&enabled,&e));g_clear_error(&e);
  gchar *check=NULL;
  g_assert_true(g_file_get_contents(victim,&check,NULL,&e));g_assert_no_error(e);
  g_assert_cmpstr(check,==,"KEEP");g_free(check);
  g_assert_cmpint(g_unlink(setting),==,0);
  ef_store_close(&db);
  for(guint i=0;i<11;i++){g_assert_cmpint(g_unlink(snapshots[i]),==,0);g_free(snapshots[i]);}
  char *dbfile=g_build_filename(data,"evolufisico","records.sqlite",NULL);
  char *dbdir=g_path_get_dirname(dbfile);
  g_assert_cmpint(g_unlink(dbfile),==,0);g_assert_cmpint(g_rmdir(dbdir),==,0);
  GDir *dir=g_dir_open(settingsdir,0,&e);g_assert_no_error(e);g_assert_nonnull(dir);
  const char *name;
  while((name=g_dir_read_name(dir))) {
    char *p=g_build_filename(settingsdir,name,NULL);
    g_assert_cmpint(g_unlink(p),==,0);g_free(p);
  }
  g_dir_close(dir);g_assert_cmpint(g_rmdir(settingsdir),==,0);
  g_assert_cmpint(g_unlink(victim),==,0);
  g_assert_cmpint(g_unlink(mock),==,0);
  g_assert_cmpint(g_unlink(log),==,0);
  g_assert_cmpint(g_rmdir(bin),==,0);
  g_assert_cmpint(g_rmdir(folder),==,0);
  g_assert_cmpint(g_rmdir(conf),==,0);
  g_assert_cmpint(g_rmdir(data),==,0);
  g_assert_cmpint(g_rmdir(root),==,0);
  g_free(dbfile);g_free(dbdir);g_free(settingsdir);g_free(setting);
  g_free(victim);g_free(trap);g_free(mock);g_free(log);g_free(original_path);
  g_free(conf);g_free(data);g_free(folder);g_free(bin);g_free(root);
  g_print("PASS: cloud opt-in, durable ledger, batch replay, network recovery and symlink safety\n");
  return 0;
}
