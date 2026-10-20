#define _POSIX_C_SOURCE 200809L
#include "../src/drive_sync.h"
#include "../src/storage.h"
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

int main(void) {
  GError *error=NULL;
  char *root=g_dir_make_tmp("evolufisico-drive-test-XXXXXX",&error);
  g_assert_no_error(error);g_assert_nonnull(root);
  char *config=g_build_filename(root,"config",NULL);
  char *data=g_build_filename(root,"data",NULL);
  char *backup=g_build_filename(root,"encrypted backup folder",NULL);
  char *bin=g_build_filename(root,"bin",NULL);
  g_assert_cmpint(g_mkdir(config,0700),==,0);
  g_assert_cmpint(g_mkdir(data,0700),==,0);
  g_assert_cmpint(g_mkdir(backup,0700),==,0);
  g_assert_cmpint(g_mkdir(bin,0700),==,0);
  g_setenv("XDG_CONFIG_HOME",config,TRUE);
  g_setenv("XDG_DATA_HOME",data,TRUE);
  g_setenv("EVOLUFISICO_DB_PASSPHRASE","Fictitious-GoogleDrive-QA-Only-2026!",TRUE);
  g_assert_false(ef_drive_valid_remote(""));
  g_assert_false(ef_drive_valid_remote("-unsafe"));
  g_assert_false(ef_drive_valid_remote("unsafe:copy"));
  g_assert_false(ef_drive_valid_remote("hello world"));
  g_assert_false(ef_drive_valid_remote("gdrive;rm"));
  g_assert_false(ef_drive_valid_remote("../path"));
  g_assert_false(ef_drive_valid_remote("x\nname"));
  g_assert_true(ef_drive_valid_remote("my_GDrive-2026"));
  g_assert_null(ef_drive_load_remote(&error));
  g_assert_no_error(error);
  g_assert_false(ef_drive_save_remote("-unsafe",&error));g_clear_error(&error);
  g_assert_true(ef_drive_save_remote("my_GDrive-2026",&error));g_assert_no_error(error);
  char *loaded=ef_drive_load_remote(&error);
  g_assert_no_error(error);g_assert_cmpstr(loaded,==,"my_GDrive-2026");g_free(loaded);
  char *settings=g_build_filename(config,"evolufisico","drive-remote.ini",NULL);
  GStatBuf sb;
  g_assert_cmpint(g_stat(settings,&sb),==,0);
  g_assert_cmpint(sb.st_mode & 077,==,0);
  gchar *contents=NULL;
  g_assert_true(g_file_get_contents(settings,&contents,NULL,&error));
  g_assert_no_error(error);
  g_assert_nonnull(strstr(contents,"remote=my_GDrive-2026"));
  g_assert_null(strstr(contents,"access_token"));
  g_assert_null(strstr(contents,"refresh_token"));
  g_free(contents);
  EfStore store={0};
  g_assert_true(ef_store_open(&store,&error));g_assert_no_error(error);
  g_assert_true(ef_store_add_client(&store,"Teste Drive (fictício)",&error));g_assert_no_error(error);
  char *snapshot=NULL;
  g_assert_true(ef_store_snapshot_to_folder(&store,backup,&snapshot,&error));
  g_assert_no_error(error);g_assert_nonnull(snapshot);
  char *log=g_build_filename(root,"arguments.log",NULL);
  char *rclone=g_build_filename(bin,"rclone",NULL);
  const char *mock="#!/bin/sh\nprintf '%s\\n' \"$@\" >> \"$MOCK_RCLONE_LOG\"\nexit 0\n";
  g_assert_true(g_file_set_contents(rclone,mock,-1,&error));g_assert_no_error(error);
  g_assert_cmpint(g_chmod(rclone,0700),==,0);
  char *prior=g_strdup(g_getenv("PATH"));
  char *path=g_strdup_printf("%s:%s",bin,prior ? prior : "/usr/bin");
  g_setenv("PATH",path,TRUE);g_setenv("MOCK_RCLONE_LOG",log,TRUE);
  g_free(path);
  g_assert_true(ef_drive_probe("my_GDrive-2026",&error));g_assert_no_error(error);
  guint count=0;
  g_assert_true(ef_drive_upload_encrypted("my_GDrive-2026",backup,&count,&error));
  g_assert_no_error(error);g_assert_cmpuint(count,==,1);
  g_assert_true(g_file_test(snapshot,G_FILE_TEST_EXISTS)); /* Local remains unchanged. */
  gchar *arguments=NULL;
  g_assert_true(g_file_get_contents(log,&arguments,NULL,&error));g_assert_no_error(error);
  g_assert_nonnull(strstr(arguments,"lsd\nmy_GDrive-2026:\n"));
  g_assert_nonnull(strstr(arguments,"copy\n"));
  g_assert_nonnull(strstr(arguments,"my_GDrive-2026:EvoluFisico-Backups\n"));
  g_assert_nonnull(strstr(arguments,"--immutable\n"));
  g_assert_null(strstr(arguments,"--delete"));
  g_free(arguments);
  g_assert_false(ef_drive_upload_encrypted("x;evil",backup,&count,&error));
  g_clear_error(&error);
  /* A symlink named like a snapshot must prevent the entire upload. */
  char *symlink_path=g_build_filename(backup,"evolufisico-malicious.sqlite",NULL);
  g_assert_cmpint(symlink(snapshot,symlink_path),==,0);
  g_assert_false(ef_drive_upload_encrypted("my_GDrive-2026",backup,&count,&error));
  g_clear_error(&error);g_unlink(symlink_path);
  /* Malformed/unencrypted files must never be uploaded. */
  g_assert_true(g_file_set_contents(symlink_path,"Not SQLCipher",-1,&error));
  g_assert_false(ef_drive_upload_encrypted("my_GDrive-2026",backup,&count,&error));
  g_clear_error(&error);g_unlink(symlink_path);
  /* Settings symlink may never be followed or overwritten. */
  g_assert_cmpint(g_unlink(settings),==,0);
  char *victim=g_build_filename(root,"victim",NULL);
  g_assert_true(g_file_set_contents(victim,"KEEP",-1,&error));g_assert_no_error(error);
  g_assert_cmpint(symlink(victim,settings),==,0);
  g_assert_false(ef_drive_save_remote("my_GDrive-2026",&error));g_clear_error(&error);
  g_assert_null(ef_drive_load_remote(&error));g_clear_error(&error);
  g_assert_true(g_file_get_contents(victim,&contents,NULL,&error));g_assert_no_error(error);
  g_assert_cmpstr(contents,==,"KEEP");g_free(contents);
  ef_store_close(&store);
  char *db=g_build_filename(data,"evolufisico","records.sqlite",NULL);
  char *datadir=g_path_get_dirname(db);
  char *settingsdir=g_path_get_dirname(settings);
  g_unlink(snapshot);g_unlink(db);g_unlink(settings);g_unlink(victim);
  g_unlink(log);g_unlink(rclone);
  g_rmdir(settingsdir);g_rmdir(datadir);
  g_rmdir(bin);g_rmdir(config);g_rmdir(data);g_rmdir(backup);g_rmdir(root);
  g_free(snapshot);g_free(db);g_free(datadir);g_free(settings);g_free(settingsdir);
  g_free(victim);g_free(log);g_free(rclone);g_free(root);
  g_free(bin);g_free(config);g_free(data);g_free(backup);g_free(prior);
  g_print("PASS: rclone OAuth remote settings, no-shell args, encrypted-only uploads, symlink and corruption protection\n");
  return 0;
}
