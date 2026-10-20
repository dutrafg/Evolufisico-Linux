#define _POSIX_C_SOURCE 200809L
#include "auto_backup.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#define MAX_SEND_PER_CALL 25u
#define MAX_PENDING_FILES 256u
static GQuark queue_error(void) {
  return g_quark_from_static_string("evolufisico-backup-queue");
}

static char *queue_directory(gboolean create,GError **error) {
  char *parent=g_build_filename(g_get_user_data_dir(),"evolufisico",NULL);
  if(!g_file_test(parent,G_FILE_TEST_EXISTS) && !create) {
    g_free(parent);return NULL;
  }
  if(!ef_folder_settings_validate(parent,error)) {
    g_free(parent);return NULL;
  }
  char *queue=g_build_filename(parent,"pending-snapshots",NULL);
  g_free(parent);
  if(create) {
    if(g_mkdir(queue,0700)!=0 && errno!=EEXIST) {
      g_set_error(error,queue_error(),1,"Não foi possível criar fila privada: %s",g_strerror(errno));
      g_free(queue);return NULL;
    }
  }else if(!g_file_test(queue,G_FILE_TEST_EXISTS)) {
    g_free(queue);return NULL;
  }
  if(!ef_folder_settings_validate(queue,error)) {
    g_free(queue);return NULL;
  }
  if(g_chmod(queue,0700)!=0) {
    g_set_error(error,queue_error(),2,"Não foi possível proteger fila de cópias: %s",g_strerror(errno));
    g_free(queue);return NULL;
  }
  return queue;
}
static gboolean queued_name(const char *name) {
  return g_str_has_prefix(name,"evolufisico-") && g_str_has_suffix(name,".sqlite");
}
static gint compare_strings(gconstpointer a,gconstpointer b) {
  return g_strcmp0(*(const char * const *)a,*(const char * const *)b);
}
static GPtrArray *queued_files(const char *queue,GError **error) {
  GDir *dir=g_dir_open(queue,0,error);
  if(!dir)return NULL;
  GPtrArray *files=g_ptr_array_new_with_free_func(g_free);
  const char *name=NULL;
  while((name=g_dir_read_name(dir))) {
    if(queued_name(name))g_ptr_array_add(files,g_strdup(name));
  }
  g_dir_close(dir);
  g_ptr_array_sort(files,compare_strings);
  return files;
}

guint ef_auto_backup_pending_count(GError **error) {
  char *queue=queue_directory(FALSE,error);
  if(!queue)return 0;
  GPtrArray *files=queued_files(queue,error);
  guint count=files ? files->len : 0;
  if(files)g_ptr_array_unref(files);
  g_free(queue);
  return count;
}
static gboolean fsync_regular(const char *path,GError **error) {
  int fd=g_open(path,O_RDONLY | O_NOFOLLOW | O_CLOEXEC,0);
  if(fd<0) {
    g_set_error(error,queue_error(),3,"Falha ao abrir cópia: %s",g_strerror(errno));
    return FALSE;
  }
  int result=fsync(fd),saved_errno=errno;
  close(fd);
  if(result!=0) {
    g_set_error(error,queue_error(),4,"Falha ao sincronizar cópia: %s",g_strerror(saved_errno));
    return FALSE;
  }
  return TRUE;
}
static gboolean fsync_directory(const char *folder,GError **error) {
  int fd=g_open(folder,O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW,0);
  if(fd<0) {
    g_set_error(error,queue_error(),5,"Falha ao abrir diretório: %s",g_strerror(errno));
    return FALSE;
  }
  int result=fsync(fd),saved_errno=errno;
  close(fd);
  if(result!=0) {
    g_set_error(error,queue_error(),6,"Falha ao confirmar pasta no disco: %s",g_strerror(saved_errno));
    return FALSE;
  }
  return TRUE;
}
/* Bounded streaming comparison for idempotence after interrupted publication. */
static gboolean same_bytes(const char *a,const char *b) {
  int fa=g_open(a,O_RDONLY | O_NOFOLLOW | O_CLOEXEC,0);
  if(fa<0)return FALSE;
  int fb=g_open(b,O_RDONLY | O_NOFOLLOW | O_CLOEXEC,0);
  if(fb<0){close(fa);return FALSE;}
  char ab[65536],bb[65536];gboolean equal=TRUE;
  while(TRUE) {
    ssize_t x=read(fa,ab,sizeof(ab));
    if(x<0 && errno==EINTR)continue;
    if(x<=0) {
      if(x<0)equal=FALSE;
      else {
        ssize_t y=read(fb,bb,1);
        equal=y==0;
      }
      break;
    }
    ssize_t received=0;
    while(received<x) {
      ssize_t y=read(fb,bb+received,(size_t)(x-received));
      if(y<0 && errno==EINTR)continue;
      if(y<=0){equal=FALSE;break;}
      received+=y;
    }
    if(!equal || memcmp(ab,bb,(size_t)x)!=0){equal=FALSE;break;}
  }
  close(fa);close(fb);return equal;
}
static gboolean is_regular_nosymlink(const char *path,GError **error) {
  GStatBuf st;
  if(g_lstat(path,&st)!=0 || !S_ISREG(st.st_mode)) {
    g_set_error_literal(error,queue_error(),7,"Cópia pendente inválida ou link simbólico; mantida para revisão.");
    return FALSE;
  }
  return TRUE;
}
static gboolean send_one(const char *queue,const char *folder,const char *name,GError **error) {
  char *source=g_build_filename(queue,name,NULL);
  char *target=g_build_filename(folder,name,NULL);
  gboolean ok=is_regular_nosymlink(source,error);
  if(ok)ok=ef_store_verify_snapshot(source,error);
  if(ok) {
    GStatBuf statbuf;
    if(g_lstat(target,&statbuf)==0) {
      /* Recovery from a crash after the previous link but before dequeuing. */
      ok=S_ISREG(statbuf.st_mode) && ef_store_verify_snapshot(target,error)
         && same_bytes(source,target);
      if(!ok && (!error || !*error))
        g_set_error_literal(error,queue_error(),8,"Destino já existe com conteúdo diferente; nenhuma substituição feita.");
    }else if(errno!=ENOENT) {
      g_set_error(error,queue_error(),9,"Não foi possível verificar destino: %s",g_strerror(errno));
      ok=FALSE;
    }else {
      char *tmpname=g_strdup_printf(".evolufisico-partial-%s",g_uuid_string_random());
      char *temporary=g_build_filename(folder,tmpname,NULL);
      g_free(tmpname);
      GFile *from=g_file_new_for_path(source),*to=g_file_new_for_path(temporary);
      ok=g_file_copy(from,to,G_FILE_COPY_NOFOLLOW_SYMLINKS,NULL,NULL,NULL,error);
      g_object_unref(from);g_object_unref(to);
      if(ok && g_chmod(temporary,0600)!=0) {
        g_set_error(error,queue_error(),10,"Falha ao proteger arquivo temporário: %s",g_strerror(errno));
        ok=FALSE;
      }
      if(ok)ok=ef_store_verify_snapshot(temporary,error);
      if(ok)ok=fsync_regular(temporary,error);
      if(ok && link(temporary,target)!=0) {
        g_set_error(error,queue_error(),11,"Destino indisponível ou já existente: %s",g_strerror(errno));
        ok=FALSE;
      }
      g_unlink(temporary);
      g_free(temporary);
    }
  }
  if(ok)ok=fsync_directory(folder,error);
  if(ok && g_unlink(source)!=0) {
    g_set_error(error,queue_error(),12,"Cópia enviada, mas não foi possível retirar da fila: %s",g_strerror(errno));
    ok=FALSE;
  }
  if(ok)ok=fsync_directory(queue,error);
  g_free(source);g_free(target);
  return ok;
}
static int lock_queue(const char *queue,GError **error) {
  char *lockpath=g_build_filename(queue,".transfer.lock",NULL);
  int fd=g_open(lockpath,O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC,0600);
  g_free(lockpath);
  if(fd<0) {
    g_set_error(error,queue_error(),13,"Fila bloqueada ou inválida: %s",g_strerror(errno));
    return -1;
  }
  if(flock(fd,LOCK_EX | LOCK_NB)!=0) {
    g_set_error(error,queue_error(),14,"Fila de cópias ocupada por outro processo.");
    close(fd);return -1;
  }
  return fd;
}
gboolean ef_auto_backup_flush(const EfFolderSettings *settings,
  guint *transferred,guint *remaining,GError **error) {
  if(transferred)*transferred=0;
  if(remaining)*remaining=0;
  char *queue=queue_directory(FALSE,error);
  if(!queue)return !error || !*error;
  GPtrArray *files=queued_files(queue,error);
  if(!files){g_free(queue);return FALSE;}
  guint count=files->len;
  if(remaining)*remaining=count;
  if(count==0){g_ptr_array_unref(files);g_free(queue);return TRUE;}
  if(!settings || !settings->folder || !settings->folder[0]) {
    g_set_error_literal(error,queue_error(),15,"Configure a pasta para enviar cópias pendentes.");
    g_ptr_array_unref(files);g_free(queue);return FALSE;
  }
  if(!ef_folder_settings_validate(settings->folder,error)) {
    g_ptr_array_unref(files);g_free(queue);return FALSE;
  }
  int lockfd=lock_queue(queue,error);
  if(lockfd<0){g_ptr_array_unref(files);g_free(queue);return FALSE;}
  /* Refresh after acquiring lock, preventing duplicate cross-process replay. */
  g_ptr_array_unref(files);
  files=queued_files(queue,error);
  if(!files){flock(lockfd,LOCK_UN);close(lockfd);g_free(queue);return FALSE;}
  guint completed=0;
  gboolean ok=TRUE;
  for(guint i=0;i<files->len && completed<MAX_SEND_PER_CALL;i++) {
    if(!send_one(queue,settings->folder,g_ptr_array_index(files,i),error)) {
      ok=FALSE;break;
    }
    completed++;
  }
  if(transferred)*transferred=completed;
  if(remaining)*remaining=files->len-completed;
  g_ptr_array_unref(files);
  flock(lockfd,LOCK_UN);close(lockfd);g_free(queue);
  return ok;
}

EfAutoBackupResult ef_auto_backup_after_change(EfStore *store,
  const EfFolderSettings *settings,char **saved_path,GError **error) {
  if(saved_path)*saved_path=NULL;
  if(!settings || !settings->folder || !settings->folder[0])
    return EF_AUTO_BACKUP_SKIPPED;
  GError *local_error=NULL;
  char *queue=queue_directory(TRUE,&local_error);
  if(!queue) {
    g_propagate_error(error,local_error);
    return EF_AUTO_BACKUP_FAILED;
  }
  GPtrArray *files=queued_files(queue,&local_error);
  if(!files) {
    g_propagate_error(error,local_error);g_free(queue);
    return EF_AUTO_BACKUP_FAILED;
  }
  guint count=files->len;g_ptr_array_unref(files);
  if(count>=MAX_PENDING_FILES) {
    g_set_error(error,queue_error(),16,"Fila local cheia (%u cópias); espaço e destino precisam de revisão.",count);
    g_free(queue);return EF_AUTO_BACKUP_FAILED;
  }
  char *uuid=g_uuid_string_random();
  char *name=g_strdup_printf("evolufisico-%016" G_GINT64_FORMAT "-%s.sqlite",
                             g_get_real_time(),uuid);
  g_free(uuid);
  char *staged=g_build_filename(queue,name,NULL);
  gboolean ok=ef_store_backup(store,staged,&local_error);
  g_free(queue);
  if(!ok) {
    g_propagate_error(error,local_error);
    g_free(name);g_free(staged);
    return EF_AUTO_BACKUP_FAILED;
  }
  guint transferred=0,pending=0;
  GError *flush_error=NULL;
  ok=ef_auto_backup_flush(settings,&transferred,&pending,&flush_error);
  EfAutoBackupResult result;
  if(ok && !g_file_test(staged,G_FILE_TEST_EXISTS)) {
    result=EF_AUTO_BACKUP_CREATED;
    if(saved_path)*saved_path=g_build_filename(settings->folder,name,NULL);
  }else{
    result=EF_AUTO_BACKUP_QUEUED;
    if(flush_error)g_propagate_error(error,flush_error);
  }
  g_free(name);g_free(staged);
  return result;
}
