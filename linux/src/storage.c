#include "storage.h"
#include <glib/gstdio.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <gio/gio.h>
extern int sqlite3_key(sqlite3 *db,const void *key,int key_length);
static char *session_passphrase=NULL;
void ef_store_set_passphrase(const char *passphrase) {
  if(session_passphrase) {volatile char *p=session_passphrase;for(gsize i=0;i<strlen(session_passphrase);i++)p[i]=0;g_free(session_passphrase);}
  session_passphrase=passphrase ? g_strdup(passphrase) : NULL;
}
static gboolean apply_key(sqlite3 *db,GError **error) {
  const char *passphrase=session_passphrase ? session_passphrase : g_getenv("EVOLUFISICO_DB_PASSPHRASE");
  if(!passphrase || strlen(passphrase)<12) {
    g_set_error_literal(error,g_quark_from_static_string("evolufisico-storage"),60,
      "Defina EVOLUFISICO_DB_PASSPHRASE com pelo menos 12 caracteres antes de iniciar.");
    return FALSE;
  }
  if(sqlite3_key(db,passphrase,(int)strlen(passphrase))!=SQLITE_OK) {
    g_set_error_literal(error,g_quark_from_static_string("evolufisico-storage"),61,"Falha ao aplicar chave SQLCipher");
    return FALSE;
  }
  return TRUE;
}
static GQuark ef_error(void) { return g_quark_from_static_string("evolufisico-storage"); }
static gboolean sql_error(EfStore *store, GError **error) {
  g_set_error(error, ef_error(), 1, "%s", sqlite3_errmsg(store->db)); return FALSE;
}
gboolean ef_store_open(EfStore *store, GError **error) {
  g_return_val_if_fail(store && !store->db, FALSE);
  char *dir = g_build_filename(g_get_user_data_dir(), "evolufisico", NULL);
  if (g_mkdir_with_parents(dir, 0700) != 0) {
    g_set_error_literal(error, ef_error(), 2, "Não foi possível criar diretório de dados");
    g_free(dir); return FALSE;
  }
  if(g_file_test(dir,G_FILE_TEST_IS_SYMLINK)) {
    g_set_error_literal(error,ef_error(),48,"Diretório de dados não pode ser um link simbólico");
    g_free(dir);return FALSE;
  }
  if(g_chmod(dir,0700)!=0) {
    g_set_error_literal(error,ef_error(),49,"Não foi possível proteger o diretório de dados");
    g_free(dir);return FALSE;
  }
  char *file = g_build_filename(dir, "records.sqlite", NULL);
  if(g_file_test(file,G_FILE_TEST_IS_SYMLINK)) {
    g_set_error_literal(error,ef_error(),50,"Arquivo de banco não pode ser link simbólico");
    g_free(file);g_free(dir);return FALSE;
  }
  g_free(dir);
  int result = sqlite3_open_v2(file, &store->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);
  if(result==SQLITE_OK && g_chmod(file,0600)!=0) {
    g_set_error_literal(error,ef_error(),51,"Não foi possível proteger o arquivo SQLite");
    g_free(file);ef_store_close(store);return FALSE;
  }
  g_free(file);
  if (result != SQLITE_OK) { sql_error(store, error); ef_store_close(store); return FALSE; }
  if(!apply_key(store->db,error)) {ef_store_close(store);return FALSE;}
  sqlite3_stmt *cipher_probe=NULL;
  if(sqlite3_prepare_v2(store->db,"SELECT count(*) FROM sqlite_master",-1,&cipher_probe,NULL)!=SQLITE_OK) {
    g_set_error_literal(error,ef_error(),62,"Senha incorreta ou banco antigo sem criptografia. Migração automática bloqueada.");
    ef_store_close(store);return FALSE;
  }
  int cipher_rc=sqlite3_step(cipher_probe);
  sqlite3_finalize(cipher_probe);
  if(cipher_rc!=SQLITE_ROW) {
    g_set_error_literal(error,ef_error(),63,"Banco criptografado indisponível ou senha incorreta.");
    ef_store_close(store);return FALSE;
  }
  sqlite3_busy_timeout(store->db, 5000);
  if(sqlite3_exec(store->db,"PRAGMA foreign_keys=ON",NULL,NULL,NULL)!=SQLITE_OK) {
    sql_error(store,error);ef_store_close(store);return FALSE;
  }
  sqlite3_stmt *version=NULL;
  if(sqlite3_prepare_v2(store->db,"PRAGMA user_version",-1,&version,NULL)!=SQLITE_OK) {
    sql_error(store,error);ef_store_close(store);return FALSE;
  }
  int rc=sqlite3_step(version);
  int schema_version=rc==SQLITE_ROW ? sqlite3_column_int(version,0) : -1;
  sqlite3_finalize(version);
  if(schema_version!=0 && schema_version!=1 && schema_version!=2) {
    g_set_error(error,ef_error(),27,"Versão de banco não suportada: %d",schema_version);
    ef_store_close(store);return FALSE;
  }
  if(sqlite3_exec(store->db,
    "CREATE TABLE IF NOT EXISTS clients(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL CHECK(length(name) BETWEEN 1 AND 180), created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
    "CREATE TABLE IF NOT EXISTS assessments(id INTEGER PRIMARY KEY,client_id INTEGER NOT NULL REFERENCES clients(id) ON DELETE CASCADE,assessment_date TEXT NOT NULL,weight_kg REAL NOT NULL,height_cm REAL NOT NULL,created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
    "CREATE TABLE IF NOT EXISTS charges(id INTEGER PRIMARY KEY,client_id INTEGER NOT NULL REFERENCES clients(id) ON DELETE CASCADE,amount_cents INTEGER NOT NULL CHECK(amount_cents>0),description TEXT NOT NULL,created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
    "CREATE INDEX IF NOT EXISTS idx_assessments_client_date ON assessments(client_id,assessment_date DESC);"
    "CREATE INDEX IF NOT EXISTS idx_charges_client ON charges(client_id);",
    NULL,NULL,NULL)!=SQLITE_OK) {
    sql_error(store,error);ef_store_close(store);return FALSE;
  }
  if(schema_version<2) {
    const char *migration="BEGIN IMMEDIATE;"
      "ALTER TABLE charges ADD COLUMN status TEXT NOT NULL DEFAULT 'pendente' CHECK(status IN ('pendente','pago'));"
      "ALTER TABLE charges ADD COLUMN received_utc TEXT;"
      "PRAGMA user_version=2;"
      "COMMIT;";
    if(sqlite3_exec(store->db,migration,NULL,NULL,NULL)!=SQLITE_OK) {
      sql_error(store,error);
      sqlite3_exec(store->db,"ROLLBACK",NULL,NULL,NULL);
      ef_store_close(store);return FALSE;
    }
  }
  return TRUE;
}
void ef_store_close(EfStore *store) {
  if (store && store->db) { sqlite3_close(store->db); store->db = NULL; }
}
gboolean ef_store_add_client(EfStore *store, const char *name, GError **error) {
  if (!store || !store->db || !name || !g_utf8_validate(name,-1,NULL) || g_utf8_strlen(name,-1)<1 || g_utf8_strlen(name,-1)>180) {
    g_set_error_literal(error, ef_error(), 3, "Nome inválido (1 a 180 caracteres)"); return FALSE;
  }
  sqlite3_stmt *stmt=NULL;
  if (sqlite3_prepare_v2(store->db,"INSERT INTO clients(name) VALUES(?)",-1,&stmt,NULL)!=SQLITE_OK) return sql_error(store,error);
  sqlite3_bind_text(stmt,1,name,-1,SQLITE_TRANSIENT);
  int result=sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return result==SQLITE_DONE ? TRUE : sql_error(store,error);
}
GPtrArray *ef_store_list_clients(EfStore *store, GError **error) {
  if (!store || !store->db) { g_set_error_literal(error,ef_error(),4,"Banco indisponível"); return NULL; }
  sqlite3_stmt *stmt=NULL;
  if (sqlite3_prepare_v2(store->db,"SELECT name FROM clients ORDER BY name COLLATE NOCASE",-1,&stmt,NULL)!=SQLITE_OK) { sql_error(store,error); return NULL; }
  GPtrArray *list=g_ptr_array_new_with_free_func(g_free);
  int rc;
  while ((rc=sqlite3_step(stmt))==SQLITE_ROW) g_ptr_array_add(list,g_strdup((const char*)sqlite3_column_text(stmt,0)));
  sqlite3_finalize(stmt);
  if (rc!=SQLITE_DONE) { g_ptr_array_unref(list); sql_error(store,error); return NULL; }
  return list;
}
gboolean ef_store_delete_client(EfStore *store, gint64 id, GError **error) {
  if (!store || !store->db || id<=0) { g_set_error_literal(error,ef_error(),5,"Identificador inválido"); return FALSE; }
  sqlite3_stmt *stmt=NULL;
  if (sqlite3_prepare_v2(store->db,"DELETE FROM clients WHERE id=?",-1,&stmt,NULL)!=SQLITE_OK) return sql_error(store,error);
  sqlite3_bind_int64(stmt,1,id);
  int rc=sqlite3_step(stmt);
  int changed=sqlite3_changes(store->db);
  sqlite3_finalize(stmt);
  if(rc!=SQLITE_DONE) return sql_error(store,error);
  if(changed!=1) { g_set_error_literal(error,ef_error(),6,"Cliente não encontrado"); return FALSE; }
  return TRUE;
}

static gboolean valid_name(const char *name) {
  return name && g_utf8_validate(name,-1,NULL) && g_utf8_strlen(name,-1)>=1 && g_utf8_strlen(name,-1)<=180;
}
gboolean ef_store_rename_client(EfStore *store, gint64 id, const char *name, GError **error) {
  if (!store || !store->db || id<=0 || !valid_name(name)) {
    g_set_error_literal(error,ef_error(),7,"Identificador ou nome inválido"); return FALSE;
  }
  sqlite3_stmt *stmt=NULL;
  if(sqlite3_prepare_v2(store->db,"UPDATE clients SET name=? WHERE id=?",-1,&stmt,NULL)!=SQLITE_OK) return sql_error(store,error);
  sqlite3_bind_text(stmt,1,name,-1,SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt,2,id);
  int rc=sqlite3_step(stmt);
  int changes=sqlite3_changes(store->db);
  sqlite3_finalize(stmt);
  if(rc!=SQLITE_DONE) return sql_error(store,error);
  if(changes!=1) { g_set_error_literal(error,ef_error(),8,"Cliente não encontrado"); return FALSE; }
  return TRUE;
}
gboolean ef_store_check_integrity(EfStore *store, GError **error) {
  if(!store || !store->db) { g_set_error_literal(error,ef_error(),9,"Banco indisponível"); return FALSE; }
  sqlite3_stmt *stmt=NULL;
  if(sqlite3_prepare_v2(store->db,"PRAGMA integrity_check",-1,&stmt,NULL)!=SQLITE_OK) return sql_error(store,error);
  int rc=sqlite3_step(stmt);
  gboolean ok=rc==SQLITE_ROW && g_strcmp0((const char*)sqlite3_column_text(stmt,0),"ok")==0;
  sqlite3_finalize(stmt);
  if(!ok) { g_set_error_literal(error,ef_error(),10,"Falha na verificação de integridade SQLite"); return FALSE; }
  return TRUE;
}
static gboolean encrypted_file_copy(const char *source,const char *destination,GError **error) {
  GFile *src=g_file_new_for_path(source),*dst=g_file_new_for_path(destination);
  gboolean ok=g_file_copy(src,dst,G_FILE_COPY_NONE,NULL,NULL,NULL,error);
  g_object_unref(src);g_object_unref(dst);
  return ok;
}
gboolean ef_store_verify_snapshot(const char *path,GError **error) {
  sqlite3 *db=NULL;
  if(sqlite3_open_v2(path,&db,SQLITE_OPEN_READONLY,NULL)!=SQLITE_OK) {
    if(db)sqlite3_close(db);
    g_set_error_literal(error,ef_error(),70,"Não foi possível abrir backup criptografado");return FALSE;
  }
  gboolean ok=apply_key(db,error);
  if(ok) {
    EfStore check={db};
    ok=ef_store_check_integrity(&check,error);
  }
  if(ok) {
    sqlite3_stmt *stmt=NULL;
    if(sqlite3_prepare_v2(db,"SELECT count(*) FROM sqlite_master WHERE type='table' AND name IN ('clients','assessments','charges')",-1,&stmt,NULL)!=SQLITE_OK)ok=FALSE;
    else {
      ok=sqlite3_step(stmt)==SQLITE_ROW && sqlite3_column_int(stmt,0)==3;
      sqlite3_finalize(stmt);
    }
    if(!ok && error && !*error)g_set_error_literal(error,ef_error(),71,"Esquema do backup incompatível");
  }
  if(ok) {
    sqlite3_stmt *stmt=NULL;
    if(sqlite3_prepare_v2(db,"PRAGMA user_version",-1,&stmt,NULL)!=SQLITE_OK)ok=FALSE;
    else {ok=sqlite3_step(stmt)==SQLITE_ROW && (sqlite3_column_int(stmt,0)==1 || sqlite3_column_int(stmt,0)==2);sqlite3_finalize(stmt);}
    if(!ok && error && !*error)g_set_error_literal(error,ef_error(),72,"Versão do backup incompatível");
  }
  sqlite3_close(db);
  return ok;
}
gboolean ef_store_backup(EfStore *store,const char *destination,GError **error) {
  if(!store || !store->db || !destination || !g_path_is_absolute(destination)) {
    g_set_error_literal(error,ef_error(),73,"Destino de backup absoluto obrigatório");return FALSE;
  }
  if(g_file_test(destination,G_FILE_TEST_EXISTS) || g_file_test(destination,G_FILE_TEST_IS_SYMLINK)) {
    g_set_error_literal(error,ef_error(),74,"Backup existente não pode ser sobrescrito");return FALSE;
  }
  if(!ef_store_check_integrity(store,error))return FALSE;
  sqlite3_stmt *journal=NULL;
  if(sqlite3_prepare_v2(store->db,"PRAGMA journal_mode",-1,&journal,NULL)!=SQLITE_OK) {
    return sql_error(store,error);
  }
  int journal_rc=sqlite3_step(journal);
  const char *mode=journal_rc==SQLITE_ROW ? (const char*)sqlite3_column_text(journal,0) : NULL;
  gboolean wal=mode && g_ascii_strcasecmp(mode,"wal")==0;
  sqlite3_finalize(journal);
  if(wal) {
    g_set_error_literal(error,ef_error(),81,"Backup em modo WAL não suportado nesta versão; operação recusada para evitar perda de dados.");
    return FALSE;
  }
  const char *live=sqlite3_db_filename(store->db,"main");
  if(!live)return FALSE;
  if(sqlite3_exec(store->db,"BEGIN IMMEDIATE",NULL,NULL,NULL)!=SQLITE_OK)
    return sql_error(store,error);
  char *temporary=g_strdup_printf("%s.tmp-%s",destination,g_uuid_string_random());
  gboolean ok=encrypted_file_copy(live,temporary,error);
  sqlite3_exec(store->db,"ROLLBACK",NULL,NULL,NULL);
  if(ok)ok=ef_store_verify_snapshot(temporary,error);
  if(ok && g_chmod(temporary,0600)!=0) {
    g_set_error_literal(error,ef_error(),75,"Falha ao proteger backup");ok=FALSE;
  }
  if(ok && link(temporary,destination)!=0) {
    g_set_error(error,ef_error(),76,"Falha ao finalizar backup: %s",g_strerror(errno));ok=FALSE;
  }
  g_unlink(temporary);g_free(temporary);
  return ok;
}

gboolean ef_store_snapshot_to_folder(EfStore *store,const char *folder,char **saved_path,GError **error) {
  if(saved_path)*saved_path=NULL;
  if(!folder || !g_path_is_absolute(folder) || !g_file_test(folder,G_FILE_TEST_IS_DIR) ||
     g_file_test(folder,G_FILE_TEST_IS_SYMLINK)) {
    g_set_error_literal(error,ef_error(),90,"Selecione uma pasta absoluta existente, sem links simbólicos.");
    return FALSE;
  }
  /* Reject symlinks in every parent component, not only the final folder. */
  char **parts=g_strsplit(folder,G_DIR_SEPARATOR_S,0);
  GString *prefix=g_string_new(G_DIR_SEPARATOR_S);
  gboolean safe=TRUE;
  for(guint i=0;parts[i];i++) {
    if(!parts[i][0])continue;
    if(strcmp(parts[i],".")==0 || strcmp(parts[i],"..")==0) {safe=FALSE;break;}
    if(prefix->len>1)g_string_append_c(prefix,G_DIR_SEPARATOR);
    g_string_append(prefix,parts[i]);
    if(g_file_test(prefix->str,G_FILE_TEST_IS_SYMLINK)) {safe=FALSE;break;}
  }
  g_strfreev(parts);g_string_free(prefix,TRUE);
  if(!safe) {
    g_set_error_literal(error,ef_error(),91,"Pasta de cópia contém caminho inseguro ou link simbólico.");
    return FALSE;
  }
  char *name=g_strdup_printf("evolufisico-%s.sqlite",g_uuid_string_random());
  char *destination=g_build_filename(folder,name,NULL);
  g_free(name);
  gboolean ok=ef_store_backup(store,destination,error);
  if(ok && saved_path)*saved_path=g_strdup(destination);
  g_free(destination);
  return ok;
}
gboolean ef_store_restore(EfStore *store,const char *source,GError **error) {
  if(!store || !store->db || !source || !g_path_is_absolute(source) || g_file_test(source,G_FILE_TEST_IS_SYMLINK)) {
    g_set_error_literal(error,ef_error(),77,"Origem de restauração inválida");return FALSE;
  }
  const char *filename=sqlite3_db_filename(store->db,"main");
  if(!filename)return FALSE;
  char *live=g_strdup(filename);
  GStatBuf live_stat,source_stat;
  if(g_stat(live,&live_stat)!=0 || g_stat(source,&source_stat)!=0 ||
    (live_stat.st_dev==source_stat.st_dev && live_stat.st_ino==source_stat.st_ino)) {
    g_set_error_literal(error,ef_error(),78,"Backup inexistente ou igual ao banco ativo");
    g_free(live);return FALSE;
  }
  if(!ef_store_verify_snapshot(source,error)){g_free(live);return FALSE;}
  char *wal_file=g_strconcat(live,"-wal",NULL);
  gboolean has_wal=g_file_test(wal_file,G_FILE_TEST_EXISTS);
  g_free(wal_file);
  if(has_wal) {
    g_set_error_literal(error,ef_error(),82,"Restauração bloqueada enquanto existir arquivo WAL do banco ativo.");
    g_free(live);return FALSE;
  }
  char *rollback=g_strdup_printf("%s.rollback-%s",live,g_uuid_string_random());
  char *replacement=g_strdup_printf("%s.restore-%s",live,g_uuid_string_random());
  gboolean ok=ef_store_check_integrity(store,error);
  if(ok)ok=encrypted_file_copy(live,rollback,error);
  if(ok)ok=encrypted_file_copy(source,replacement,error);
  if(ok && g_chmod(replacement,0600)!=0) {
    g_set_error_literal(error,ef_error(),79,"Não foi possível proteger restauração");ok=FALSE;
  }
  if(ok) {
    ef_store_close(store);
    if(g_rename(replacement,live)!=0) {
      g_set_error_literal(error,ef_error(),80,"Falha ao substituir banco");ok=FALSE;
    }
    if(ok && !ef_store_open(store,error))ok=FALSE;
    if(!ok) {
      g_rename(rollback,live);
      GError *recovery=NULL;
      if(!store->db && !ef_store_open(store,&recovery))
        g_warning("Falha crítica ao reabrir banco anterior: %s",recovery ? recovery->message : "erro");
      g_clear_error(&recovery);
    }
  }
  g_unlink(rollback);g_unlink(replacement);
  g_free(rollback);g_free(replacement);g_free(live);
  return ok;
}
GPtrArray *ef_store_search_clients(EfStore *store, const char *query, GError **error) {
  if(!store || !store->db || !query || !g_utf8_validate(query,-1,NULL)) {
    g_set_error_literal(error,ef_error(),19,"Busca inválida");return NULL;
  }
  sqlite3_stmt *stmt=NULL;
  if(sqlite3_prepare_v2(store->db,"SELECT id,name FROM clients WHERE instr(lower(name),lower(?))>0 ORDER BY name COLLATE NOCASE",-1,&stmt,NULL)!=SQLITE_OK) {
    sql_error(store,error);return NULL;
  }
  sqlite3_bind_text(stmt,1,query,-1,SQLITE_TRANSIENT);
  GPtrArray *list=g_ptr_array_new_with_free_func(g_free);
  int rc;
  while((rc=sqlite3_step(stmt))==SQLITE_ROW) {
    char *line=g_strdup_printf("%" G_GINT64_FORMAT " — %s",(gint64)sqlite3_column_int64(stmt,0),(const char*)sqlite3_column_text(stmt,1));
    g_ptr_array_add(list,line);
  }
  sqlite3_finalize(stmt);
  if(rc!=SQLITE_DONE) {g_ptr_array_unref(list);sql_error(store,error);return NULL;}
  return list;
}

GPtrArray *ef_store_list_client_rows(EfStore *store, GError **error) {
  if(!store || !store->db) {
    g_set_error_literal(error,ef_error(),23,"Banco indisponível");
    return NULL;
  }
  sqlite3_stmt *stmt=NULL;
  if(sqlite3_prepare_v2(store->db,"SELECT id,name FROM clients ORDER BY name COLLATE NOCASE",-1,&stmt,NULL)!=SQLITE_OK) {
    sql_error(store,error);return NULL;
  }
  GPtrArray *rows=g_ptr_array_new_with_free_func(g_free);
  int rc;
  while((rc=sqlite3_step(stmt))==SQLITE_ROW) {
    char *line=g_strdup_printf("%" G_GINT64_FORMAT " — %s",(gint64)sqlite3_column_int64(stmt,0),(const char*)sqlite3_column_text(stmt,1));
    g_ptr_array_add(rows,line);
  }
  sqlite3_finalize(stmt);
  if(rc!=SQLITE_DONE) {g_ptr_array_unref(rows);sql_error(store,error);return NULL;}
  return rows;
}

static gboolean valid_date(const char *date) {
  if(!date || strlen(date)!=10 || date[4]!='-' || date[7]!='-')return FALSE;
  for(int i=0;i<10;i++)if(i!=4 && i!=7 && (date[i]<'0'||date[i]>'9'))return FALSE;
  int year=atoi(date),month=atoi(date+5),day=atoi(date+8);
  GDateTime *dt=g_date_time_new_utc(year,month,day,12,0,0);
  if(!dt)return FALSE;
  gboolean valid=g_date_time_get_year(dt)==year && g_date_time_get_month(dt)==month && g_date_time_get_day_of_month(dt)==day;
  g_date_time_unref(dt);
  return valid;
}
gboolean ef_store_add_assessment(EfStore *store,gint64 client_id,const char *date,double weight_kg,double height_cm,GError **error) {
  if(!store || !store->db || client_id<=0 || !valid_date(date) || !isfinite(weight_kg) || !isfinite(height_cm) ||
     weight_kg<=0 || weight_kg>500 || height_cm<=0 || height_cm>300) {
    g_set_error_literal(error,ef_error(),30,"Dados da avaliação inválidos");return FALSE;
  }
  sqlite3_stmt *stmt=NULL;
  if(sqlite3_prepare_v2(store->db,"INSERT INTO assessments(client_id,assessment_date,weight_kg,height_cm) VALUES(?,?,?,?)",-1,&stmt,NULL)!=SQLITE_OK)return sql_error(store,error);
  sqlite3_bind_int64(stmt,1,client_id);
  sqlite3_bind_text(stmt,2,date,-1,SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt,3,weight_kg);
  sqlite3_bind_double(stmt,4,height_cm);
  int rc=sqlite3_step(stmt);
  if(rc!=SQLITE_DONE)sql_error(store,error);
  sqlite3_finalize(stmt);
  return rc==SQLITE_DONE;
}
GPtrArray *ef_store_list_assessments(EfStore *store,gint64 client_id,GError **error) {
  if(!store || !store->db || client_id<=0) {g_set_error_literal(error,ef_error(),31,"Cliente inválido");return NULL;}
  sqlite3_stmt *stmt=NULL;
  if(sqlite3_prepare_v2(store->db,"SELECT assessment_date,weight_kg,height_cm FROM assessments WHERE client_id=? ORDER BY assessment_date DESC,id DESC",-1,&stmt,NULL)!=SQLITE_OK) {sql_error(store,error);return NULL;}
  sqlite3_bind_int64(stmt,1,client_id);
  GPtrArray *rows=g_ptr_array_new_with_free_func(g_free);
  int rc;
  while((rc=sqlite3_step(stmt))==SQLITE_ROW)
    g_ptr_array_add(rows,g_strdup_printf("%s | %.1f kg | %.1f cm",(const char*)sqlite3_column_text(stmt,0),sqlite3_column_double(stmt,1),sqlite3_column_double(stmt,2)));
  sqlite3_finalize(stmt);
  if(rc!=SQLITE_DONE) {g_ptr_array_unref(rows);sql_error(store,error);return NULL;}
  return rows;
}
gboolean ef_store_add_charge(EfStore *store,gint64 client_id,gint64 cents,const char *description,GError **error) {
  if(!store || !store->db || client_id<=0 || cents<=0 || cents>100000000000LL ||
     !description || !g_utf8_validate(description,-1,NULL) || g_utf8_strlen(description,-1)<1 || g_utf8_strlen(description,-1)>240) {
    g_set_error_literal(error,ef_error(),32,"Cobrança inválida");return FALSE;
  }
  sqlite3_stmt *stmt=NULL;
  if(sqlite3_prepare_v2(store->db,"INSERT INTO charges(client_id,amount_cents,description) VALUES(?,?,?)",-1,&stmt,NULL)!=SQLITE_OK)return sql_error(store,error);
  sqlite3_bind_int64(stmt,1,client_id);
  sqlite3_bind_int64(stmt,2,cents);
  sqlite3_bind_text(stmt,3,description,-1,SQLITE_TRANSIENT);
  int rc=sqlite3_step(stmt);
  if(rc!=SQLITE_DONE)sql_error(store,error);
  sqlite3_finalize(stmt);
  return rc==SQLITE_DONE;
}
GPtrArray *ef_store_list_charges(EfStore *store,gint64 client_id,GError **error) {
  if(!store || !store->db || client_id<=0) {g_set_error_literal(error,ef_error(),33,"Cliente inválido");return NULL;}
  sqlite3_stmt *stmt=NULL;
  if(sqlite3_prepare_v2(store->db,"SELECT amount_cents,description FROM charges WHERE client_id=? ORDER BY id DESC",-1,&stmt,NULL)!=SQLITE_OK) {sql_error(store,error);return NULL;}
  sqlite3_bind_int64(stmt,1,client_id);
  GPtrArray *rows=g_ptr_array_new_with_free_func(g_free);
  int rc;
  while((rc=sqlite3_step(stmt))==SQLITE_ROW) {
    gint64 cents=(gint64)sqlite3_column_int64(stmt,0);
    g_ptr_array_add(rows,g_strdup_printf("R$ %" G_GINT64_FORMAT ",%02" G_GINT64_FORMAT " | %s",cents/100,cents%100,(const char*)sqlite3_column_text(stmt,1)));
  }
  sqlite3_finalize(stmt);
  if(rc!=SQLITE_DONE) {g_ptr_array_unref(rows);sql_error(store,error);return NULL;}
  return rows;
}

GPtrArray *ef_store_client_choices(EfStore *store,GError **error) {
  return ef_store_list_client_rows(store,error);
}

static gboolean delete_record(EfStore *store,gint64 id,const char *sql,GError **error) {
  if(!store || !store->db || id<=0) {g_set_error_literal(error,ef_error(),40,"Identificador inválido");return FALSE;}
  sqlite3_stmt *stmt=NULL;
  if(sqlite3_prepare_v2(store->db,sql,-1,&stmt,NULL)!=SQLITE_OK)return sql_error(store,error);
  sqlite3_bind_int64(stmt,1,id);
  int rc=sqlite3_step(stmt);
  int changes=sqlite3_changes(store->db);
  if(rc!=SQLITE_DONE)sql_error(store,error);
  sqlite3_finalize(stmt);
  if(rc!=SQLITE_DONE)return FALSE;
  if(changes!=1) {g_set_error_literal(error,ef_error(),41,"Registro não encontrado");return FALSE;}
  return TRUE;
}
gboolean ef_store_delete_assessment(EfStore *store,gint64 id,GError **error) {
  return delete_record(store,id,"DELETE FROM assessments WHERE id=?",error);
}
gboolean ef_store_delete_charge(EfStore *store,gint64 id,GError **error) {
  return delete_record(store,id,"DELETE FROM charges WHERE id=?",error);
}

GPtrArray *ef_store_list_assessment_rows(EfStore *store,gint64 client_id,GError **error) {
  if(!store || !store->db || client_id<=0) {g_set_error_literal(error,ef_error(),42,"Cliente inválido");return NULL;}
  sqlite3_stmt *stmt=NULL;
  if(sqlite3_prepare_v2(store->db,"SELECT id,assessment_date,weight_kg,height_cm FROM assessments WHERE client_id=? ORDER BY assessment_date DESC,id DESC",-1,&stmt,NULL)!=SQLITE_OK) {sql_error(store,error);return NULL;}
  sqlite3_bind_int64(stmt,1,client_id);
  GPtrArray *rows=g_ptr_array_new_with_free_func(g_free);int rc;
  while((rc=sqlite3_step(stmt))==SQLITE_ROW)
    g_ptr_array_add(rows,g_strdup_printf("%" G_GINT64_FORMAT " — %s | %.1f kg | %.1f cm",(gint64)sqlite3_column_int64(stmt,0),(const char*)sqlite3_column_text(stmt,1),sqlite3_column_double(stmt,2),sqlite3_column_double(stmt,3)));
  sqlite3_finalize(stmt);
  if(rc!=SQLITE_DONE) {g_ptr_array_unref(rows);sql_error(store,error);return NULL;}
  return rows;
}
GPtrArray *ef_store_list_charge_rows(EfStore *store,gint64 client_id,GError **error) {
  if(!store || !store->db || client_id<=0) {g_set_error_literal(error,ef_error(),43,"Cliente inválido");return NULL;}
  sqlite3_stmt *stmt=NULL;
  if(sqlite3_prepare_v2(store->db,"SELECT id,amount_cents,description,status,received_utc FROM charges WHERE client_id=? ORDER BY id DESC",-1,&stmt,NULL)!=SQLITE_OK) {sql_error(store,error);return NULL;}
  sqlite3_bind_int64(stmt,1,client_id);
  GPtrArray *rows=g_ptr_array_new_with_free_func(g_free);int rc;
  while((rc=sqlite3_step(stmt))==SQLITE_ROW) {
    gint64 cents=sqlite3_column_int64(stmt,1);
    g_ptr_array_add(rows,g_strdup_printf("%" G_GINT64_FORMAT " — R$ %" G_GINT64_FORMAT ",%02" G_GINT64_FORMAT " | %s | %s%s%s",(gint64)sqlite3_column_int64(stmt,0),cents/100,cents%100,(const char*)sqlite3_column_text(stmt,2),(const char*)sqlite3_column_text(stmt,3),sqlite3_column_type(stmt,4)==SQLITE_NULL?"":" | recebido em ",sqlite3_column_type(stmt,4)==SQLITE_NULL?"":(const char*)sqlite3_column_text(stmt,4)));
  }
  sqlite3_finalize(stmt);
  if(rc!=SQLITE_DONE) {g_ptr_array_unref(rows);sql_error(store,error);return NULL;}
  return rows;
}

static gboolean belongs_to(EfStore *store,gint64 record_id,gint64 client_id,const char *sql,GError **error) {
  if(!store || !store->db || record_id<=0 || client_id<=0) {g_set_error_literal(error,ef_error(),44,"Identificador inválido");return FALSE;}
  sqlite3_stmt *stmt=NULL;
  if(sqlite3_prepare_v2(store->db,sql,-1,&stmt,NULL)!=SQLITE_OK)return sql_error(store,error);
  sqlite3_bind_int64(stmt,1,record_id);sqlite3_bind_int64(stmt,2,client_id);
  int rc=sqlite3_step(stmt);
  gboolean exists=rc==SQLITE_ROW;
  sqlite3_finalize(stmt);
  if(rc!=SQLITE_ROW && rc!=SQLITE_DONE)return sql_error(store,error);
  if(!exists)g_set_error_literal(error,ef_error(),45,"Registro não pertence ao cliente selecionado");
  return exists;
}
gboolean ef_store_assessment_belongs_to(EfStore *store,gint64 record_id,gint64 client_id,GError **error) {
  return belongs_to(store,record_id,client_id,"SELECT 1 FROM assessments WHERE id=? AND client_id=?",error);
}
gboolean ef_store_charge_belongs_to(EfStore *store,gint64 record_id,gint64 client_id,GError **error) {
  return belongs_to(store,record_id,client_id,"SELECT 1 FROM charges WHERE id=? AND client_id=?",error);
}

static gboolean delete_scoped(EfStore *store,gint64 record_id,gint64 client_id,const char *sql,GError **error) {
  if(!store || !store->db || record_id<=0 || client_id<=0) {g_set_error_literal(error,ef_error(),46,"Identificador inválido");return FALSE;}
  sqlite3_stmt *stmt=NULL;
  if(sqlite3_prepare_v2(store->db,sql,-1,&stmt,NULL)!=SQLITE_OK)return sql_error(store,error);
  sqlite3_bind_int64(stmt,1,record_id);sqlite3_bind_int64(stmt,2,client_id);
  int rc=sqlite3_step(stmt);int changed=sqlite3_changes(store->db);
  if(rc!=SQLITE_DONE)sql_error(store,error);
  sqlite3_finalize(stmt);
  if(rc!=SQLITE_DONE)return FALSE;
  if(changed!=1) {g_set_error_literal(error,ef_error(),47,"Registro não encontrado para este cliente");return FALSE;}
  return TRUE;
}
gboolean ef_store_delete_assessment_for_client(EfStore *store,gint64 record_id,gint64 client_id,GError **error) {
  return delete_scoped(store,record_id,client_id,"DELETE FROM assessments WHERE id=? AND client_id=?",error);
}
gboolean ef_store_delete_charge_for_client(EfStore *store,gint64 record_id,gint64 client_id,GError **error) {
  return delete_scoped(store,record_id,client_id,"DELETE FROM charges WHERE id=? AND client_id=?",error);
}

gboolean ef_store_receive_charge(EfStore *store,gint64 charge_id,gint64 client_id,GError **error) {
  if(!store || !store->db || charge_id<=0 || client_id<=0) {
    g_set_error_literal(error,ef_error(),83,"Identificadores de recebimento inválidos");return FALSE;
  }
  sqlite3_stmt *stmt=NULL;
  if(sqlite3_prepare_v2(store->db,
    "UPDATE charges SET status='pago',received_utc=strftime('%Y-%m-%dT%H:%M:%SZ','now') "
    "WHERE id=? AND client_id=? AND status='pendente'",-1,&stmt,NULL)!=SQLITE_OK)
    return sql_error(store,error);
  sqlite3_bind_int64(stmt,1,charge_id);
  sqlite3_bind_int64(stmt,2,client_id);
  int rc=sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if(rc!=SQLITE_DONE)return sql_error(store,error);
  if(sqlite3_changes(store->db)!=1) {
    g_set_error_literal(error,ef_error(),84,"Cobrança inexistente, de outro cliente ou já recebida");
    return FALSE;
  }
  return TRUE;
}
