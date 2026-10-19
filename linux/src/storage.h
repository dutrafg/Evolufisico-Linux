#pragma once
#include <glib.h>
#include <sqlite3.h>
typedef struct { sqlite3 *db; } EfStore;
gboolean ef_store_open(EfStore *store, GError **error);
void ef_store_close(EfStore *store);
gboolean ef_store_add_client(EfStore *store, const char *name, GError **error);
GPtrArray *ef_store_list_clients(EfStore *store, GError **error);
gboolean ef_store_delete_client(EfStore *store, gint64 id, GError **error);
gboolean ef_store_rename_client(EfStore *store, gint64 id, const char *name, GError **error);
gboolean ef_store_check_integrity(EfStore *store, GError **error);
gboolean ef_store_backup(EfStore *store, const char *destination, GError **error);
gboolean ef_store_restore(EfStore *store, const char *source, GError **error);
GPtrArray *ef_store_search_clients(EfStore *store, const char *query, GError **error);
GPtrArray *ef_store_list_client_rows(EfStore *store, GError **error);
gboolean ef_store_add_assessment(EfStore *store, gint64 client_id, const char *date, double weight_kg, double height_cm, GError **error);
GPtrArray *ef_store_list_assessments(EfStore *store, gint64 client_id, GError **error);
gboolean ef_store_add_charge(EfStore *store, gint64 client_id, gint64 cents, const char *description, GError **error);
GPtrArray *ef_store_list_charges(EfStore *store, gint64 client_id, GError **error);
GPtrArray *ef_store_client_choices(EfStore *store,GError **error);
gboolean ef_store_delete_assessment(EfStore *store,gint64 assessment_id,GError **error);
gboolean ef_store_delete_charge(EfStore *store,gint64 charge_id,GError **error);
GPtrArray *ef_store_list_assessment_rows(EfStore *store,gint64 client_id,GError **error);
GPtrArray *ef_store_list_charge_rows(EfStore *store,gint64 client_id,GError **error);
gboolean ef_store_assessment_belongs_to(EfStore *store,gint64 assessment_id,gint64 client_id,GError **error);
gboolean ef_store_charge_belongs_to(EfStore *store,gint64 charge_id,gint64 client_id,GError **error);
gboolean ef_store_delete_assessment_for_client(EfStore *store,gint64 assessment_id,gint64 client_id,GError **error);
gboolean ef_store_delete_charge_for_client(EfStore *store,gint64 charge_id,gint64 client_id,GError **error);
void ef_store_set_passphrase(const char *passphrase);
gboolean ef_store_receive_charge(EfStore *store,gint64 charge_id,gint64 client_id,GError **error);

gboolean ef_store_snapshot_to_folder(EfStore *store,const char *folder,char **saved_path,GError **error);
