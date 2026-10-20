#define _POSIX_C_SOURCE 200809L
#include "../src/folder_settings.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

int main(void) {
  GError *error=NULL;
  char *root=g_dir_make_tmp("evolufisico-folder-XXXXXX",&error);
  g_assert_no_error(error);g_assert_nonnull(root);
  char *conf=g_build_filename(root,"config",NULL);
  char *real=g_build_filename(root,"pasta teste",NULL);
  g_assert_cmpint(g_mkdir(conf,0700),==,0);
  g_assert_cmpint(g_mkdir(real,0700),==,0);
  g_setenv("XDG_CONFIG_HOME",conf,TRUE);
  EfFolderSettings settings={0};

  g_assert_true(ef_folder_settings_load(&settings,&error));
  g_assert_no_error(error);g_assert_null(settings.folder);
  g_assert_false(ef_folder_settings_save(&settings,"relativa",&error));
  g_clear_error(&error);
  g_assert_false(ef_folder_settings_save(&settings,"/",&error));
  g_clear_error(&error);
  char *missing=g_build_filename(root,"pasta-ausente",NULL);
  g_assert_false(ef_folder_settings_save(&settings,missing,&error));
  g_clear_error(&error);
  g_assert_null(settings.folder);
  char *linkpath=g_build_filename(root,"link",NULL);
  g_assert_cmpint(symlink(real,linkpath),==,0);
  g_assert_false(ef_folder_settings_save(&settings,linkpath,&error));
  g_clear_error(&error);

  g_assert_true(ef_folder_settings_save(&settings,real,&error));
  g_assert_no_error(error);
  g_assert_cmpstr(settings.folder,==,real);
  char *stored=g_build_filename(conf,"evolufisico","sync-folder.ini",NULL);
  char *config_dir=g_path_get_dirname(stored);
  GStatBuf sb;
  g_assert_cmpint(g_stat(config_dir,&sb),==,0);
  g_assert_cmpint(sb.st_mode & 077,==,0);
  g_assert_cmpint(g_stat(stored,&sb),==,0);
  g_assert_cmpint(sb.st_mode & 077,==,0);
  gchar *disk=NULL;
  g_assert_true(g_file_get_contents(stored,&disk,NULL,&error));
  g_assert_no_error(error);
  g_assert_nonnull(strstr(disk,"version=1"));
  g_free(disk);

  ef_folder_settings_clear(&settings);
  g_assert_true(ef_folder_settings_load(&settings,&error));
  g_assert_no_error(error);
  g_assert_cmpstr(settings.folder,==,real);

  /* A disconnected mount must not erase the persisted folder. */
  char *offline=g_build_filename(root,"temporariamente-desmontada",NULL);
  g_assert_cmpint(g_rename(real,offline),==,0);
  ef_folder_settings_clear(&settings);
  g_assert_true(ef_folder_settings_load(&settings,&error));
  g_assert_no_error(error);
  g_assert_cmpstr(settings.folder,==,real);
  g_assert_false(ef_folder_settings_validate(settings.folder,&error));
  g_clear_error(&error);
  g_assert_cmpint(g_rename(offline,real),==,0);
  g_assert_true(ef_folder_settings_validate(settings.folder,&error));
  g_assert_no_error(error);

  g_assert_true(ef_folder_settings_save(&settings,NULL,&error));
  g_assert_no_error(error);g_assert_null(settings.folder);
  ef_folder_settings_clear(&settings);
  g_assert_true(ef_folder_settings_load(&settings,&error));
  g_assert_no_error(error);g_assert_null(settings.folder);

  /* Do not follow the configuration file symlink, or damage external content. */
  g_assert_cmpint(g_unlink(stored),==,0);
  char *outside=g_build_filename(root,"outside.ini",NULL);
  g_assert_true(g_file_set_contents(outside,"sentinel",-1,&error));
  g_assert_no_error(error);
  g_assert_cmpint(symlink(outside,stored),==,0);
  g_assert_false(ef_folder_settings_load(&settings,&error));
  g_clear_error(&error);
  g_assert_false(ef_folder_settings_save(&settings,real,&error));
  g_clear_error(&error);
  gchar *contents=NULL;
  g_assert_true(g_file_get_contents(outside,&contents,NULL,&error));
  g_assert_no_error(error);
  g_assert_cmpstr(contents,==,"sentinel");
  g_free(contents);
  g_unlink(stored);

  /* Unknown configuration versions must be rejected. */
  g_assert_true(g_file_set_contents(stored,"[sync]\nversion=99\nfolder=/tmp\n",-1,&error));
  g_assert_no_error(error);
  g_assert_false(ef_folder_settings_load(&settings,&error));
  g_clear_error(&error);
  g_unlink(stored);
  g_assert_true(ef_folder_settings_save(&settings,real,&error));
  g_assert_no_error(error);
  ef_folder_settings_clear(&settings);

  g_unlink(stored);g_unlink(outside);g_unlink(linkpath);
  g_rmdir(config_dir);g_rmdir(conf);g_rmdir(real);g_rmdir(root);
  g_free(config_dir);g_free(stored);g_free(outside);g_free(linkpath);
  g_free(missing);g_free(offline);g_free(real);g_free(conf);g_free(root);
  g_print("PASS: folder persistence, path safety, offline mount, permissions\n");
  return 0;
}
