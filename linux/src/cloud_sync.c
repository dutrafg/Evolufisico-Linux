#define _POSIX_C_SOURCE 200809L
#include "cloud_sync.h"
#include "folder_settings.h"
#include "storage.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#define CLOUD_BATCH_LIMIT 8u

static GQuark cloud_error(void) {
  return g_quark_from_static_string("evolufisico-cloud-sync");
}
static char *cloud_config_dir(GError **error) {
  char *directory=g_build_filename(g_get_user_config_dir(),"evolufisico",NULL);
  if(g_mkdir_with_parents(directory,0700)!=0) {
    g_set_error(error,cloud_error(),1,"Falha ao criar configuração de nuvem: %s",g_strerror(errno));
    g_free(directory);return NULL;
  }
  if(!ef_folder_settings_validate(directory,error) || g_chmod(directory,0700)!=0) {
    if(error && !*error)
      g_set_error(error,cloud_error(),2,"Falha ao proteger configuração: %s",g_strerror(errno));
    g_free(directory);return NULL;
  }
  return directory;
}
static gboolean safe_regular_config(const char *path,GError **error) {
  GStatBuf sb;
  if(g_lstat(path,&sb)!=0) {
    if(errno==ENOENT)return TRUE;
    g_set_error(error,cloud_error(),3,"Falha ao verificar configuração: %s",g_strerror(errno));
    return FALSE;
  }
  if(!S_ISREG(sb.st_mode)) {
    g_set_error_literal(error,cloud_error(),4,"Arquivo de configuração não é regular; link simbólico bloqueado.");
    return FALSE;
  }
  return TRUE;
}
static gboolean write_keyfile(GKeyFile *key,const char *path,GError **error) {
  if(!safe_regular_config(path,error))return FALSE;
  gsize length=0;
  gchar *text=g_key_file_to_data(key,&length,error);
  if(!text)return FALSE;
  GFile *file=g_file_new_for_path(path);
  gboolean ok=g_file_replace_contents(file,text,length,NULL,FALSE,
                G_FILE_CREATE_PRIVATE|G_FILE_CREATE_REPLACE_DESTINATION,
                NULL,NULL,error);
  g_object_unref(file);g_free(text);
  if(ok && g_chmod(path,0600)!=0) {
    g_set_error(error,cloud_error(),5,"Falha ao proteger arquivo: %s",g_strerror(errno));
    return FALSE;
  }
  return ok;
}
static gboolean read_keyfile(GKeyFile *key,const char *path,GError **error) {
  if(!safe_regular_config(path,error))return FALSE;
  if(!g_file_test(path,G_FILE_TEST_EXISTS))return TRUE;
  return g_key_file_load_from_file(key,path,G_KEY_FILE_NONE,error);
}

gboolean ef_cloud_auto_load(gboolean *enabled,GError **error) {
  g_return_val_if_fail(enabled,FALSE);
  *enabled=FALSE;
  char *dir=cloud_config_dir(error);
  if(!dir)return FALSE;
  char *path=g_build_filename(dir,"drive-auto.ini",NULL);
  GKeyFile *key=g_key_file_new();
  gboolean ok=read_keyfile(key,path,error);
  if(ok && g_key_file_has_key(key,"automatic","enabled",NULL))
    *enabled=g_key_file_get_boolean(key,"automatic","enabled",error);
  if(error && *error){*enabled=FALSE;ok=FALSE;}
  g_key_file_unref(key);g_free(path);g_free(dir);
  return ok;
}
gboolean ef_cloud_auto_save(gboolean enabled,GError **error) {
  char *dir=cloud_config_dir(error);
  if(!dir)return FALSE;
  char *path=g_build_filename(dir,"drive-auto.ini",NULL);
  GKeyFile *key=g_key_file_new();
  g_key_file_set_boolean(key,"automatic","enabled",enabled);
  gboolean ok=write_keyfile(key,path,error);
  g_key_file_unref(key);g_free(path);g_free(dir);
  return ok;
}
static gboolean snapshot_name(const char *name) {
  if(!g_str_has_prefix(name,"evolufisico-") ||
     !g_str_has_suffix(name,".sqlite") || strlen(name)>218)return FALSE;
  for(const unsigned char *p=(const unsigned char *)name;*p;p++)
    if(!g_ascii_isalnum(*p) && *p!='-' && *p!='_' && *p!='.')return FALSE;
  return TRUE;
}
static gint compare_strings(gconstpointer x,gconstpointer y) {
  return g_strcmp0(*(const char *const *)x,*(const char *const *)y);
}
static GPtrArray *snapshot_names(const char *folder,GError **error) {
  GDir *dir=g_dir_open(folder,0,error);
  if(!dir)return NULL;
  GPtrArray *names=g_ptr_array_new_with_free_func(g_free);
  const char *name;
  while((name=g_dir_read_name(dir)))
    if(snapshot_name(name))g_ptr_array_add(names,g_strdup(name));
  g_dir_close(dir);
  g_ptr_array_sort(names,compare_strings);
  return names;
}
/* Stream encrypted bytes through SHA256; never buffer clinical datasets. */
static char *file_sha256(const char *filename,GError **error) {
  GFile *file=g_file_new_for_path(filename);
  GFileInputStream *input=g_file_read(file,NULL,error);
  g_object_unref(file);
  if(!input)return NULL;
  GChecksum *hash=g_checksum_new(G_CHECKSUM_SHA256);
  guint8 buffer[65536];
  gboolean ok=TRUE;
  gssize n;
  while((n=g_input_stream_read(G_INPUT_STREAM(input),buffer,sizeof(buffer),NULL,error))>0)
    g_checksum_update(hash,buffer,(gsize)n);
  if(n<0)ok=FALSE;
  if(!g_input_stream_close(G_INPUT_STREAM(input),NULL,error))ok=FALSE;
  char *digest=ok?g_strdup(g_checksum_get_string(hash)):NULL;
  g_checksum_free(hash);g_object_unref(input);
  return digest;
}
static gboolean run_rclone_copyto(const char *local,const char *remote,const char *name,GError **error) {
  if(!g_find_program_in_path("rclone")) {
    g_set_error_literal(error,cloud_error(),6,"rclone não instalado; cópia permanece pendente.");
    return FALSE;
  }
  char *destination=g_strdup_printf("%s:EvoluFisico-Backups/%s",remote,name);
  const char *args[]={"rclone","copyto",local,destination,"--immutable","--checksum",
     "--transfers","1","--checkers","1","--retries","1","--low-level-retries","2",
     "--timeout","45s","--contimeout","15s","--log-level","ERROR",NULL};
  GSubprocessLauncher *launcher=g_subprocess_launcher_new(
     G_SUBPROCESS_FLAGS_STDOUT_SILENCE|G_SUBPROCESS_FLAGS_STDERR_SILENCE);
  GSubprocess *child=g_subprocess_launcher_spawnv(launcher,args,error);
  g_object_unref(launcher);g_free(destination);
  if(!child)return FALSE;
  gboolean ok=g_subprocess_wait_check(child,NULL,error);
  g_object_unref(child);
  return ok;
}
static gboolean stage_and_send(const char *folder,const char *name,
                               const char *remote,GError **error) {
  char *src=g_build_filename(folder,name,NULL);
  if(!ef_store_verify_snapshot(src,error)){g_free(src);return FALSE;}
  char *tempdir=g_dir_make_tmp("evolufisico-cloud-XXXXXX",error);
  if(!tempdir){g_free(src);return FALSE;}
  char *temp=g_build_filename(tempdir,name,NULL);
  GFile *source=g_file_new_for_path(src),*target=g_file_new_for_path(temp);
  gboolean ok=g_file_copy(source,target,G_FILE_COPY_NOFOLLOW_SYMLINKS,NULL,NULL,NULL,error);
  g_object_unref(source);g_object_unref(target);
  if(ok && g_chmod(temp,0600)!=0) {
    g_set_error(error,cloud_error(),7,"Falha ao proteger cópia temporária: %s",g_strerror(errno));
    ok=FALSE;
  }
  if(ok)ok=ef_store_verify_snapshot(temp,error);
  if(ok)ok=run_rclone_copyto(temp,remote,name,error);
  g_unlink(temp);g_rmdir(tempdir);
  g_free(tempdir);g_free(temp);g_free(src);
  return ok;
}
static int acquire_lock(const char *dir,const char *remote,GError **error) {
  char *name=g_strdup_printf("drive-%s.lock",remote);
  char *path=g_build_filename(dir,name,NULL);
  int fd=g_open(path,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
  g_free(path);g_free(name);
  if(fd<0) {
    g_set_error(error,cloud_error(),8,"Falha ao bloquear sincronização: %s",g_strerror(errno));
    return -1;
  }
  if(flock(fd,LOCK_EX|LOCK_NB)!=0) {
    g_set_error_literal(error,cloud_error(),9,"Outra sincronização da conta está em andamento.");
    close(fd);return -1;
  }
  return fd;
}
gboolean ef_cloud_upload_pending(const char *remote,const char *folder,
                                 guint *uploaded,guint *remaining,GError **error) {
  if(uploaded)*uploaded=0;
  if(remaining)*remaining=0;
  if(!ef_drive_valid_remote(remote)) {
    g_set_error_literal(error,cloud_error(),10,"Nome de conta rclone inválido.");
    return FALSE;
  }
  if(!ef_folder_settings_validate(folder,error))return FALSE;
  char *dir=cloud_config_dir(error);
  if(!dir)return FALSE;
  int lockfd=acquire_lock(dir,remote,error);
  if(lockfd<0){g_free(dir);return FALSE;}
  char *ledgername=g_strdup_printf("drive-uploaded-%s.ini",remote);
  char *ledgerpath=g_build_filename(dir,ledgername,NULL);
  g_free(ledgername);
  GKeyFile *ledger=g_key_file_new();
  gboolean ok=read_keyfile(ledger,ledgerpath,error);
  GPtrArray *names=NULL;
  if(ok){names=snapshot_names(folder,error);ok=names!=NULL;}
  guint completed=0,left=0;
  if(ok) {
    for(guint i=0;i<names->len;i++) {
      const char *name=g_ptr_array_index(names,i);
      char *source=g_build_filename(folder,name,NULL);
      GStatBuf sb;
      if(g_lstat(source,&sb)!=0 || !S_ISREG(sb.st_mode)) {
        g_set_error_literal(error,cloud_error(),11,"Arquivo de backup com link simbólico ou tipo inválido.");
        ok=FALSE;g_free(source);break;
      }
      char *digest=file_sha256(source,error);
      g_free(source);
      if(!digest){ok=FALSE;break;}
      char *previous=g_key_file_get_string(ledger,"uploaded",name,NULL);
      gboolean already=previous && g_strcmp0(previous,digest)==0;
      g_free(previous);
      if(!already) {
        left++;
        if(completed<CLOUD_BATCH_LIMIT) {
          if(!stage_and_send(folder,name,remote,error)) {
            ok=FALSE;g_free(digest);break;
          }
          g_key_file_set_string(ledger,"uploaded",name,digest);
          if(!write_keyfile(ledger,ledgerpath,error)) {
            ok=FALSE;g_free(digest);break;
          }
          completed++;left--;
        }
      }
      g_free(digest);
    }
  }
  if(uploaded)*uploaded=completed;
  if(remaining)*remaining=left;
  if(names)g_ptr_array_unref(names);
  g_key_file_unref(ledger);
  flock(lockfd,LOCK_UN);close(lockfd);
  g_free(ledgerpath);g_free(dir);
  return ok;
}
