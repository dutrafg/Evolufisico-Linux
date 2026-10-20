#define _POSIX_C_SOURCE 200809L
#include "folder_settings.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

static GQuark folder_error(void) {
  return g_quark_from_static_string("evolufisico-folder-settings");
}
static char *settings_path(void) {
  return g_build_filename(g_get_user_config_dir(),"evolufisico","sync-folder.ini",NULL);
}
static gboolean safe_path(const char *path,gboolean require_folder,GError **error) {
  if(!path || !g_utf8_validate(path,-1,NULL) || !g_path_is_absolute(path) ||
     g_strcmp0(path,G_DIR_SEPARATOR_S)==0) {
    g_set_error_literal(error,folder_error(),1,"Selecione uma pasta absoluta válida, diferente da raiz.");
    return FALSE;
  }
  char **parts=g_strsplit(path,G_DIR_SEPARATOR_S,-1);
  GString *prefix=g_string_new(G_DIR_SEPARATOR_S);
  gboolean ok=TRUE;
  for(guint i=0;parts[i];i++) {
    if(!parts[i][0])continue;
    if(strcmp(parts[i],".")==0 || strcmp(parts[i],"..")==0) {
      g_set_error_literal(error,folder_error(),2,"Não use . ou .. no caminho da pasta.");
      ok=FALSE;break;
    }
    if(prefix->len>1)g_string_append_c(prefix,G_DIR_SEPARATOR);
    g_string_append(prefix,parts[i]);
    GStatBuf sb;
    if(g_lstat(prefix->str,&sb)!=0) {
      if(require_folder) {
        g_set_error(error,folder_error(),3,"Pasta indisponível: %s",g_strerror(errno));
        ok=FALSE;
      }
      break; /* Preserve offline removable/cloud mount configuration on load. */
    }
    if(S_ISLNK(sb.st_mode) || !S_ISDIR(sb.st_mode)) {
      g_set_error_literal(error,folder_error(),4,"Caminho contém link simbólico ou arquivo que não é pasta.");
      ok=FALSE;break;
    }
  }
  g_string_free(prefix,TRUE);g_strfreev(parts);
  return ok;
}
void ef_folder_settings_clear(EfFolderSettings *settings) {
  if(settings)g_clear_pointer(&settings->folder,g_free);
}
gboolean ef_folder_settings_validate(const char *folder,GError **error) {
  if(!safe_path(folder,TRUE,error))return FALSE;
  if(g_access(folder,W_OK | X_OK)!=0) {
    g_set_error(error,folder_error(),5,"Pasta sem permissão de gravação: %s",g_strerror(errno));
    return FALSE;
  }
  return TRUE;
}
gboolean ef_folder_settings_load(EfFolderSettings *settings,GError **error) {
  g_return_val_if_fail(settings,FALSE);
  char *path=settings_path();
  if(g_file_test(path,G_FILE_TEST_IS_SYMLINK)) {
    g_set_error_literal(error,folder_error(),6,"Configuração insegura: link simbólico.");
    g_free(path);return FALSE;
  }
  if(!g_file_test(path,G_FILE_TEST_EXISTS)) {
    ef_folder_settings_clear(settings);g_free(path);return TRUE;
  }
  GKeyFile *key=g_key_file_new();
  gboolean ok=g_key_file_load_from_file(key,path,G_KEY_FILE_NONE,error);
  int version=0;char *folder=NULL;
  if(ok) {
    version=g_key_file_get_integer(key,"sync","version",error);
    ok=error==NULL || *error==NULL;
  }
  if(ok && version!=1) {
    g_set_error_literal(error,folder_error(),7,"Versão da configuração incompatível.");
    ok=FALSE;
  }
  if(ok) {
    folder=g_key_file_get_string(key,"sync","folder",error);
    ok=error==NULL || *error==NULL;
  }
  if(ok && folder && *folder)ok=safe_path(folder,FALSE,error);
  if(ok) {
    ef_folder_settings_clear(settings);
    settings->folder=folder && *folder ? g_strdup(folder) : NULL;
  }
  g_free(folder);g_key_file_unref(key);g_free(path);
  return ok;
}
gboolean ef_folder_settings_save(EfFolderSettings *settings,const char *folder,GError **error) {
  g_return_val_if_fail(settings,FALSE);
  if(folder && *folder && !ef_folder_settings_validate(folder,error))return FALSE;
  char *path=settings_path();
  char *directory=g_path_get_dirname(path);
  if(g_mkdir_with_parents(directory,0700)!=0 || !safe_path(directory,TRUE,error)) {
    if(error && !*error)g_set_error(error,folder_error(),8,"Falha ao criar configuração: %s",g_strerror(errno));
    g_free(directory);g_free(path);return FALSE;
  }
  if(g_chmod(directory,0700)!=0 || g_file_test(path,G_FILE_TEST_IS_SYMLINK)) {
    g_set_error_literal(error,folder_error(),9,"Diretório ou arquivo de configuração inseguro.");
    g_free(directory);g_free(path);return FALSE;
  }
  GKeyFile *key=g_key_file_new();
  g_key_file_set_integer(key,"sync","version",1);
  g_key_file_set_string(key,"sync","folder",folder ? folder : "");
  gsize length=0;
  char *serialized=g_key_file_to_data(key,&length,error);
  gboolean ok=serialized!=NULL;
  if(ok) {
    GFile *file=g_file_new_for_path(path);
    ok=g_file_replace_contents(file,serialized,length,NULL,FALSE,
       G_FILE_CREATE_PRIVATE | G_FILE_CREATE_REPLACE_DESTINATION,NULL,NULL,error);
    g_object_unref(file);
  }
  if(ok && g_chmod(path,0600)!=0) {
    g_set_error(error,folder_error(),10,"Falha ao proteger configuração: %s",g_strerror(errno));
    ok=FALSE;
  }
  if(ok) {
    ef_folder_settings_clear(settings);
    settings->folder=folder && *folder ? g_strdup(folder) : NULL;
  }
  g_free(serialized);g_key_file_unref(key);g_free(directory);g_free(path);
  return ok;
}
