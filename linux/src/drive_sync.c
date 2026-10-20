#define _POSIX_C_SOURCE 200809L
#include "drive_sync.h"
#include "folder_settings.h"
#include "storage.h"
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static GQuark drive_error(void) {
  return g_quark_from_static_string("evolufisico-google-drive");
}
gboolean ef_drive_valid_remote(const char *name) {
  if(!name || !(g_ascii_isalpha(name[0])))return FALSE;
  gsize length=strlen(name);
  if(length>50)return FALSE;
  for(gsize i=1;i<length;i++)
    if(!g_ascii_isalnum(name[i]) && name[i]!='_' && name[i]!='-')return FALSE;
  return TRUE;
}
static char *drive_settings_path(void) {
  return g_build_filename(g_get_user_config_dir(),"evolufisico","drive-remote.ini",NULL);
}
static gboolean safe_config_parent(const char *path,GError **error) {
  char *parent=g_path_get_dirname(path);
  if(g_mkdir_with_parents(parent,0700)!=0) {
    g_set_error(error,drive_error(),1,"Não foi possível criar a pasta de configuração: %s",g_strerror(errno));
    g_free(parent);return FALSE;
  }
  /* The config is allowed only in a non-symlinked parent directory. */
  char **parts=g_strsplit(parent,G_DIR_SEPARATOR_S,0);
  GString *prefix=g_string_new(G_DIR_SEPARATOR_S);
  gboolean ok=TRUE;
  for(guint i=0;parts[i];i++) {
    if(!parts[i][0])continue;
    if(prefix->len>1)g_string_append_c(prefix,G_DIR_SEPARATOR);
    g_string_append(prefix,parts[i]);
    GStatBuf statbuf;
    if(g_lstat(prefix->str,&statbuf)!=0 || !S_ISDIR(statbuf.st_mode)) {
      g_set_error_literal(error,drive_error(),2,"Caminho da configuração inseguro.");
      ok=FALSE;break;
    }
  }
  g_string_free(prefix,TRUE);g_strfreev(parts);
  if(ok && g_chmod(parent,0700)!=0) {
    g_set_error(error,drive_error(),3,"Sem permissão para proteger configurações: %s",g_strerror(errno));
    ok=FALSE;
  }
  g_free(parent);return ok;
}
char *ef_drive_load_remote(GError **error) {
  char *path=drive_settings_path();
  if(g_file_test(path,G_FILE_TEST_IS_SYMLINK)) {
    g_set_error_literal(error,drive_error(),4,"Arquivo de configuração é link simbólico.");
    g_free(path);return NULL;
  }
  if(!g_file_test(path,G_FILE_TEST_EXISTS)) {
    g_free(path);return NULL;
  }
  GKeyFile *key=g_key_file_new();
  char *remote=NULL;
  if(g_key_file_load_from_file(key,path,G_KEY_FILE_NONE,error))
    remote=g_key_file_get_string(key,"google-drive","remote",error);
  if(remote && !ef_drive_valid_remote(remote)) {
    g_set_error_literal(error,drive_error(),5,"Nome da conta Google Drive inválido na configuração.");
    g_clear_pointer(&remote,g_free);
  }
  g_key_file_unref(key);g_free(path);
  return remote;
}
gboolean ef_drive_save_remote(const char *name,GError **error) {
  if(name && *name && !ef_drive_valid_remote(name)) {
    g_set_error_literal(error,drive_error(),6,"Nome da conta inválido. Use letras, números, _ ou - (máximo 50).");
    return FALSE;
  }
  char *path=drive_settings_path();
  if(!safe_config_parent(path,error)) {g_free(path);return FALSE;}
  if(g_file_test(path,G_FILE_TEST_IS_SYMLINK)) {
    g_set_error_literal(error,drive_error(),7,"Configuração é um link simbólico e não será substituída.");
    g_free(path);return FALSE;
  }
  GKeyFile *key=g_key_file_new();
  g_key_file_set_string(key,"google-drive","remote",name ? name : "");
  gsize size=0;
  gchar *bytes=g_key_file_to_data(key,&size,error);
  gboolean ok=bytes!=NULL;
  if(ok) {
    GFile *file=g_file_new_for_path(path);
    ok=g_file_replace_contents(file,bytes,size,NULL,FALSE,
      G_FILE_CREATE_PRIVATE|G_FILE_CREATE_REPLACE_DESTINATION,NULL,NULL,error);
    g_object_unref(file);
  }
  if(ok && g_chmod(path,0600)!=0) {
    g_set_error(error,drive_error(),8,"Não foi possível proteger arquivo: %s",g_strerror(errno));
    ok=FALSE;
  }
  g_free(bytes);g_key_file_unref(key);g_free(path);
  return ok;
}
gboolean ef_drive_open_oauth_setup(GError **error) {
  if(!g_find_program_in_path("rclone")) {
    g_set_error_literal(error,drive_error(),9,"rclone ausente. Instale rclone antes de conectar o Google Drive.");
    return FALSE;
  }
  /* Leave OAuth credentials, callback, and browser authorization entirely to rclone.
   * No shell, tokens, secrets, or auth code pass through the application.
   */
  const char *terminal=g_find_program_in_path("x-terminal-emulator");
  if(!terminal) {
    g_set_error_literal(error,drive_error(),10,
      "Terminal x-terminal-emulator não encontrado. Abra um terminal e execute: rclone config");
    return FALSE;
  }
  GSubprocess *process=g_subprocess_new(G_SUBPROCESS_FLAGS_NONE,error,
                                       terminal,"-e","rclone","config",NULL);
  if(!process)return FALSE;
  g_object_unref(process);
  return TRUE;
}
static gboolean run_rclone(const char * const *args,GError **error) {
  char *program=g_find_program_in_path("rclone");
  if(!program) {
    g_set_error_literal(error,drive_error(),11,"Instale rclone para usar o Google Drive.");
    return FALSE;
  }
  GSubprocessLauncher *launcher=g_subprocess_launcher_new(
      G_SUBPROCESS_FLAGS_STDOUT_SILENCE|G_SUBPROCESS_FLAGS_STDERR_SILENCE);
  GSubprocess *child=g_subprocess_launcher_spawnv(launcher,args,error);
  g_object_unref(launcher);g_free(program);
  if(!child)return FALSE;
  gboolean ok=g_subprocess_wait_check(child,NULL,error);
  g_object_unref(child);
  return ok;
}
gboolean ef_drive_probe(const char *remote,GError **error) {
  if(!ef_drive_valid_remote(remote)) {
    g_set_error_literal(error,drive_error(),12,"Conta inválida.");
    return FALSE;
  }
  char *destination=g_strconcat(remote,":",NULL);
  const char *args[]={"rclone","lsd",destination,
     "--timeout","20s","--contimeout","10s","--retries","1",
     "--low-level-retries","1","--log-level","ERROR",NULL};
  gboolean ok=run_rclone(args,error);
  g_free(destination);
  return ok;
}
static gboolean valid_encrypted_name(const char *name) {
  return g_str_has_prefix(name,"evolufisico-") && g_str_has_suffix(name,".sqlite")
         && !strchr(name,'/') && strlen(name)<220;
}
static void remove_staging(const char *directory) {
  GDir *dir=g_dir_open(directory,0,NULL);
  if(dir) {
    const char *name;
    while((name=g_dir_read_name(dir))) {
      char *file=g_build_filename(directory,name,NULL);
      g_unlink(file);g_free(file);
    }
    g_dir_close(dir);
  }
  g_rmdir(directory);
}
gboolean ef_drive_upload_encrypted(const char *remote,const char *folder,
                                   guint *verified_files,GError **error) {
  if(verified_files)*verified_files=0;
  if(!ef_drive_valid_remote(remote)) {
    g_set_error_literal(error,drive_error(),13,"Conta inválida.");
    return FALSE;
  }
  if(!ef_folder_settings_validate(folder,error))return FALSE;
  GDir *dir=g_dir_open(folder,0,error);
  if(!dir)return FALSE;
  /* Private staging isolates only validated SQLCipher snapshots.
   * Never pass untrusted files in the chosen folder to rclone.
   */
  char *staging=g_dir_make_tmp("evolufisico-drive-XXXXXX",error);
  if(!staging){g_dir_close(dir);return FALSE;}
  gboolean ok=TRUE;
  guint count=0;
  const char *name;
  while((name=g_dir_read_name(dir))) {
    if(!valid_encrypted_name(name))continue;
    char *source=g_build_filename(folder,name,NULL);
    GStatBuf statbuf;
    if(g_lstat(source,&statbuf)!=0 || !S_ISREG(statbuf.st_mode)) {
      g_set_error_literal(error,drive_error(),14,"Cópia é link simbólico ou arquivo inválido; envio bloqueado.");
      ok=FALSE;g_free(source);break;
    }
    if(!ef_store_verify_snapshot(source,error)) {
      ok=FALSE;g_free(source);break;
    }
    if(++count>256) {
      g_set_error_literal(error,drive_error(),15,"Muitas cópias; limite de 256 por envio.");
      ok=FALSE;g_free(source);break;
    }
    char *target=g_build_filename(staging,name,NULL);
    GFile *src=g_file_new_for_path(source),*dst=g_file_new_for_path(target);
    ok=g_file_copy(src,dst,G_FILE_COPY_NOFOLLOW_SYMLINKS,NULL,NULL,NULL,error);
    g_object_unref(src);g_object_unref(dst);
    if(ok && g_chmod(target,0600)!=0) {
      g_set_error(error,drive_error(),16,"Erro ao proteger arquivo: %s",g_strerror(errno));
      ok=FALSE;
    }
    if(ok)ok=ef_store_verify_snapshot(target,error);
    g_free(source);g_free(target);
    if(!ok)break;
  }
  g_dir_close(dir);
  if(verified_files)*verified_files=count;
  if(ok && count==0) {
    g_set_error_literal(error,drive_error(),17,"Nenhuma cópia criptografada disponível na pasta.");
    ok=FALSE;
  }
  if(ok) {
    char *remote_folder=g_strdup_printf("%s:EvoluFisico-Backups",remote);
    const char *args[]={"rclone","copy",staging,remote_folder,
      "--immutable","--checksum","--transfers","1","--checkers","2",
      "--retries","1","--low-level-retries","2","--timeout","45s",
      "--contimeout","15s","--log-level","ERROR",NULL};
    ok=run_rclone(args,error);
    g_free(remote_folder);
  }
  remove_staging(staging);g_free(staging);
  return ok;
}
