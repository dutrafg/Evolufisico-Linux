#include <gtk/gtk.h>
#include "storage.h"
#include "folder_settings.h"
#include "auto_backup.h"
#include <errno.h>
#include <math.h>

typedef struct {
  EfStore store;
  EfFolderSettings folder_settings;
  GtkWidget *folder_settings_label;
  GtkWidget *folder_settings_status;
  GtkWidget *client_list;
  GtkWidget *client_entry;
  GtkWidget *status;
  GtkWidget *search_entry;
  GtkWidget *backup_entry;
  GtkWidget *settings_status;
  GtkWidget *restore_entry;
  GtkWidget *client_edit_entry;
  GtkWidget *client_id_entry;
  GtkWidget *assessment_date;
  GtkWidget *assessment_weight;
  GtkWidget *assessment_height;
  GtkWidget *assessment_status;
  GtkWidget *assessment_folder;
  GtkWidget *assessment_list;
  GtkWidget *charge_cents;
  GtkWidget *charge_description;
  GtkWidget *charge_status;
  GtkWidget *charge_list;
  GtkWidget *assessment_picker;
  GtkWidget *charge_picker;
  GtkWidget *assessment_delete_id;
  GtkWidget *charge_delete_id;
  GtkWidget *charge_receive_id;
  GtkWidget *client_delete_id;
  GtkWidget *client_scroll;
  GtkWidget *assessment_scroll;
  GtkWidget *charge_scroll;
  GArray *client_choice_ids;
} AppState;

static gboolean read_picker_id(GtkWidget *picker,gint64 *out) {
  guint index=gtk_drop_down_get_selected(GTK_DROP_DOWN(picker));
  AppState *state=g_object_get_data(G_OBJECT(picker),"state");
  if(!state || !state->client_choice_ids || index==GTK_INVALID_LIST_POSITION ||
     index>=state->client_choice_ids->len)return FALSE;
  *out=g_array_index(state->client_choice_ids,gint64,index);
  return *out>0;
}
static gboolean read_id(GtkWidget *entry,gint64 *out) {
  const char *value=gtk_editable_get_text(GTK_EDITABLE(entry));
  if(!*value)return FALSE;
  errno=0;char *end=NULL;
  gint64 parsed=g_ascii_strtoll(value,&end,10);
  if(errno==ERANGE || !end || *end || parsed<=0)return FALSE;
  *out=parsed;return TRUE;
}
static gboolean read_positive_double(GtkWidget *entry,double *out) {
  const char *value=gtk_editable_get_text(GTK_EDITABLE(entry));
  if(!*value)return FALSE;
  errno=0;char *end=NULL;
  double parsed=g_ascii_strtod(value,&end);
  if(errno==ERANGE || !end || *end || !isfinite(parsed) || parsed<=0)return FALSE;
  *out=parsed;return TRUE;
}
static void refresh_pickers(AppState *state) {
  if(!state->assessment_picker || !state->charge_picker || !state->store.db)return;
  gint64 previous_a=0,previous_b=0;
  read_picker_id(state->assessment_picker,&previous_a);
  read_picker_id(state->charge_picker,&previous_b);
  GtkStringList *names=gtk_string_list_new(NULL);
  GArray *ids=g_array_new(FALSE,FALSE,sizeof(gint64));
  GError *error=NULL;
  GPtrArray *rows=ef_store_client_choices(&state->store,&error);
  if(!rows) {
    g_warning("Erro ao atualizar seleção: %s",error ? error->message : "erro desconhecido");
    g_clear_error(&error);
  }else{
    for(guint i=0;i<rows->len;i++) {
      const char *row=g_ptr_array_index(rows,i);
      char *end=NULL;
      gint64 id=g_ascii_strtoll(row,&end,10);
      if(id<=0 || !end || *end!=' ')continue;
      gtk_string_list_append(names,row);
      g_array_append_val(ids,id);
    }
    g_ptr_array_unref(rows);
  }
  gtk_drop_down_set_model(GTK_DROP_DOWN(state->assessment_picker),G_LIST_MODEL(names));
  gtk_drop_down_set_model(GTK_DROP_DOWN(state->charge_picker),G_LIST_MODEL(names));
  g_object_unref(names);
  if(state->client_choice_ids)g_array_unref(state->client_choice_ids);
  state->client_choice_ids=ids;
  guint selected_a=GTK_INVALID_LIST_POSITION,selected_b=GTK_INVALID_LIST_POSITION;
  for(guint i=0;i<ids->len;i++){
    gint64 id=g_array_index(ids,gint64,i);
    if(id==previous_a)selected_a=i;
    if(id==previous_b)selected_b=i;
  }
  gtk_drop_down_set_selected(GTK_DROP_DOWN(state->assessment_picker),selected_a);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(state->charge_picker),selected_b);
}
static void refresh_clients(AppState *state) {
  GtkWidget *child;
  while ((child=gtk_widget_get_first_child(state->client_list)))
    gtk_list_box_remove(GTK_LIST_BOX(state->client_list),child);
  GError *error=NULL;
  const char *query=state->search_entry ? gtk_editable_get_text(GTK_EDITABLE(state->search_entry)) : "";
  GPtrArray *clients=*query ? ef_store_search_clients(&state->store,query,&error) : ef_store_list_client_rows(&state->store,&error);
  if(!clients) {
    gtk_label_set_text(GTK_LABEL(state->status),error ? error->message : "Falha na consulta.");
    g_clear_error(&error);return;
  }
  for(guint i=0;i<clients->len;i++){
    GtkWidget *label=gtk_label_new(g_ptr_array_index(clients,i));
    gtk_widget_set_halign(label,GTK_ALIGN_START);
    gtk_widget_set_margin_top(label,8);
    gtk_widget_set_margin_bottom(label,8);
    gtk_list_box_append(GTK_LIST_BOX(state->client_list),label);
  }
  g_ptr_array_unref(clients);
  refresh_pickers(state);
}
static void fill_list(GtkWidget *list,GPtrArray *rows) {
  GtkWidget *child=NULL;
  while((child=gtk_widget_get_first_child(list)))gtk_list_box_remove(GTK_LIST_BOX(list),child);
  for(guint i=0;i<rows->len;i++) {
    GtkWidget *label=gtk_label_new(g_ptr_array_index(rows,i));
    gtk_widget_set_halign(label,GTK_ALIGN_START);
    gtk_widget_set_margin_top(label,7);
    gtk_widget_set_margin_bottom(label,7);
    gtk_list_box_append(GTK_LIST_BOX(list),label);
  }
}
static void refresh_assessments(AppState *state,gint64 client_id) {
  GError *error=NULL;
  GPtrArray *rows=ef_store_list_assessment_rows(&state->store,client_id,&error);
  if(rows) {fill_list(state->assessment_list,rows);g_ptr_array_unref(rows);}
  else {gtk_label_set_text(GTK_LABEL(state->assessment_status),error ? error->message : "Falha ao consultar avaliações.");g_clear_error(&error);}
}
static void refresh_charges(AppState *state,gint64 client_id) {
  GError *error=NULL;
  GPtrArray *rows=ef_store_list_charge_rows(&state->store,client_id,&error);
  if(rows) {fill_list(state->charge_list,rows);g_ptr_array_unref(rows);}
  else {gtk_label_set_text(GTK_LABEL(state->charge_status),error ? error->message : "Falha ao consultar cobranças.");g_clear_error(&error);}
}
static void show_assessments(GtkButton *button,gpointer data) {
  (void)button;AppState *state=data;gint64 id=0;
  if(!read_picker_id(state->assessment_picker,&id)) {
    gtk_label_set_text(GTK_LABEL(state->assessment_status),"Selecione um cliente.");return;
  }
  refresh_assessments(state,id);
}
static void show_charges(GtkButton *button,gpointer data) {
  (void)button;AppState *state=data;gint64 id=0;
  if(!read_picker_id(state->charge_picker,&id)) {
    gtk_label_set_text(GTK_LABEL(state->charge_status),"Selecione um cliente.");return;
  }
  refresh_charges(state,id);
}
/* Never conflate successful SQLCipher commits with optional snapshot failures. */
static void notify_saved_and_copy(AppState *state, GtkWidget *status, const char *operation) {
  GError *error=NULL;
  char *saved_path=NULL;
  EfAutoBackupResult result=ef_auto_backup_after_change(
      &state->store,&state->folder_settings,&saved_path,&error);
  char *message=NULL;
  if(result==EF_AUTO_BACKUP_CREATED) {
    message=g_strdup_printf("%s Banco local salvo; cópia criptografada automática: %s",
                            operation,saved_path ? saved_path : "(sem caminho)");
  } else if(result==EF_AUTO_BACKUP_SKIPPED) {
    message=g_strdup_printf("%s Banco local salvo. Configure uma pasta em Configurações para cópias automáticas.",
                            operation);
  } else {
    message=g_strdup_printf("%s Banco local salvo, mas CÓPIA AUTOMÁTICA FALHOU: %s",
                            operation,error ? error->message : "erro desconhecido");
  }
  gtk_label_set_text(GTK_LABEL(status),message);
  g_free(message);g_free(saved_path);g_clear_error(&error);
}
typedef struct { AppState *state; gint64 record_id; gint64 client_id; gboolean assessment; } RecordDeleteRequest;
static void delete_record_confirmed(GObject *source,GAsyncResult *result,gpointer data) {
  RecordDeleteRequest *request=data;
  AppState *state=request->state;
  GError *choice_error=NULL;
  int response=gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source),result,&choice_error);
  g_clear_error(&choice_error);
  if(response!=1) {g_free(request);return;}
  gboolean assessment=request->assessment;
  GtkWidget *field=assessment ? state->assessment_delete_id : state->charge_delete_id;
  GtkWidget *status=assessment ? state->assessment_status : state->charge_status;
  GtkWidget *picker=assessment ? state->assessment_picker : state->charge_picker;
  gint64 id=0,client=0;
  if(!read_id(field,&id) || !read_picker_id(picker,&client)) {
    gtk_label_set_text(GTK_LABEL(status),"Selecione um cliente e informe o ID do registro.");g_free(request);return;
  }
  gint64 confirmed_id=request->record_id;
  gint64 confirmed_client=request->client_id;
  if(id!=confirmed_id || client!=confirmed_client) {
    gtk_label_set_text(GTK_LABEL(status),"Seleção alterada. Confirme novamente a exclusão.");g_free(request);return;
  }
  GError *error=NULL;
  g_free(request);
  gboolean owns=assessment ? ef_store_assessment_belongs_to(&state->store,id,client,&error) : ef_store_charge_belongs_to(&state->store,id,client,&error);
  if(!owns) {
    gtk_label_set_text(GTK_LABEL(status),error ? error->message : "Registro de outro cliente.");
    g_clear_error(&error);return;
  }
  gboolean ok=assessment ? ef_store_delete_assessment_for_client(&state->store,id,client,&error) : ef_store_delete_charge_for_client(&state->store,id,client,&error);
  if(ok) {
    if(assessment)refresh_assessments(state,client);else refresh_charges(state,client);
    notify_saved_and_copy(state,status,"Registro excluído.");
  }else{
    gtk_label_set_text(GTK_LABEL(status),error ? error->message : "Falha ao excluir.");
    g_clear_error(&error);
  }
}
static void delete_record_clicked(GtkButton *button,gpointer data) {
  AppState *state=data;
  gboolean assessment=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),"assessment"));
  GtkWidget *field=assessment ? state->assessment_delete_id : state->charge_delete_id;
  gint64 id=0;
  gint64 selected_client=0;
  GtkWidget *picker=assessment ? state->assessment_picker : state->charge_picker;
  if(!read_id(field,&id) || !read_picker_id(picker,&selected_client)) {
    gtk_label_set_text(GTK_LABEL(assessment ? state->assessment_status : state->charge_status),"Selecione o cliente e informe um ID válido.");
    return;
  }
  GtkWidget *window=gtk_widget_get_ancestor(GTK_WIDGET(button),GTK_TYPE_WINDOW);
  GtkAlertDialog *dialog=gtk_alert_dialog_new("Excluir permanentemente o registro %" G_GINT64_FORMAT "?",id);
  const char *buttons[]={"Cancelar","Excluir",NULL};
  gtk_alert_dialog_set_buttons(dialog,buttons);
  gtk_alert_dialog_set_cancel_button(dialog,0);
  RecordDeleteRequest *request=g_new0(RecordDeleteRequest,1);
  request->state=state;request->record_id=id;request->client_id=selected_client;request->assessment=assessment;
  gtk_alert_dialog_choose(dialog,GTK_WINDOW(window),NULL,delete_record_confirmed,request);
  g_object_unref(dialog);
}
static void save_assessment(GtkButton *button,gpointer data) {
  (void)button;AppState *state=data;gint64 id=0;double weight=0,height=0;
  if(!read_picker_id(state->assessment_picker,&id) || !read_positive_double(state->assessment_weight,&weight) ||
     !read_positive_double(state->assessment_height,&height)) {
    gtk_label_set_text(GTK_LABEL(state->assessment_status),"Selecione um cliente e confira peso e altura.");return;
  }
  const char *date=gtk_editable_get_text(GTK_EDITABLE(state->assessment_date));
  GError *error=NULL;
  if(ef_store_add_assessment(&state->store,id,date,weight,height,&error)) {
    notify_saved_and_copy(state,state->assessment_status,"Avaliação registrada.");
    refresh_assessments(state,id);
    gtk_editable_set_text(GTK_EDITABLE(state->assessment_weight),"");
    gtk_editable_set_text(GTK_EDITABLE(state->assessment_height),"");
  }else{
    gtk_label_set_text(GTK_LABEL(state->assessment_status),error ? error->message : "Falha ao salvar avaliação.");
    g_clear_error(&error);
  }
}
typedef struct {AppState *state;gint64 charge_id;gint64 client_id;} ReceiveRequest;
static void receive_charge_confirmed(GObject *source,GAsyncResult *result,gpointer data) {
  ReceiveRequest *request=data;
  GError *choice_error=NULL;
  int response=gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source),result,&choice_error);
  g_clear_error(&choice_error);
  if(response==1) {
    GError *error=NULL;
    if(ef_store_receive_charge(&request->state->store,request->charge_id,request->client_id,&error)) {
      refresh_charges(request->state,request->client_id);
      notify_saved_and_copy(request->state,request->state->charge_status,"Recebimento confirmado.");
      gtk_editable_set_text(GTK_EDITABLE(request->state->charge_receive_id),"");
    }else{
      gtk_label_set_text(GTK_LABEL(request->state->charge_status),error ? error->message : "Não foi possível receber a cobrança.");
      g_clear_error(&error);
    }
  }
  g_free(request);
}
static void receive_charge_clicked(GtkButton *button,gpointer data) {
  AppState *state=data;gint64 charge_id=0,client_id=0;
  if(!read_id(state->charge_receive_id,&charge_id) || !read_picker_id(state->charge_picker,&client_id)) {
    gtk_label_set_text(GTK_LABEL(state->charge_status),"Selecione o cliente e informe o ID da cobrança pendente.");return;
  }
  GtkWidget *window=gtk_widget_get_ancestor(GTK_WIDGET(button),GTK_TYPE_WINDOW);
  GtkAlertDialog *dialog=gtk_alert_dialog_new("Confirmar recebimento da cobrança %" G_GINT64_FORMAT "?",charge_id);
  const char *buttons[]={"Cancelar","Receber",NULL};
  gtk_alert_dialog_set_buttons(dialog,buttons);
  gtk_alert_dialog_set_cancel_button(dialog,0);
  ReceiveRequest *request=g_new0(ReceiveRequest,1);
  request->state=state;request->charge_id=charge_id;request->client_id=client_id;
  gtk_alert_dialog_choose(dialog,GTK_WINDOW(window),NULL,receive_charge_confirmed,request);
  g_object_unref(dialog);
}
static void save_charge(GtkButton *button,gpointer data) {
  (void)button;AppState *state=data;gint64 id=0,cents=0;
  if(!read_picker_id(state->charge_picker,&id) || !read_id(state->charge_cents,&cents)) {
    gtk_label_set_text(GTK_LABEL(state->charge_status),"Selecione um cliente e informe o valor em centavos.");return;
  }
  const char *description=gtk_editable_get_text(GTK_EDITABLE(state->charge_description));
  GError *error=NULL;
  if(ef_store_add_charge(&state->store,id,cents,description,&error)) {
    refresh_charges(state,id);
    notify_saved_and_copy(state,state->charge_status,"Cobrança pendente registrada.");
    gtk_editable_set_text(GTK_EDITABLE(state->charge_cents),"");
    gtk_editable_set_text(GTK_EDITABLE(state->charge_description),"");
  }else{
    gtk_label_set_text(GTK_LABEL(state->charge_status),error ? error->message : "Falha ao salvar cobrança.");
    g_clear_error(&error);
  }
}
typedef struct { AppState *state; gint64 id; } ClientDeleteRequest;
static void delete_client_confirmed(GObject *source,GAsyncResult *result,gpointer data) {
  ClientDeleteRequest *request=data;
  AppState *state=request->state;
  gint64 id=request->id;
  GError *choice_error=NULL;
  int response=gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source),result,&choice_error);
  g_clear_error(&choice_error);g_free(request);
  if(response!=1)return;
  GError *error=NULL;
  if(ef_store_delete_client(&state->store,id,&error)) {
    refresh_clients(state);
    notify_saved_and_copy(state,state->status,"Cliente e registros associados excluídos.");
  }else{
    gtk_label_set_text(GTK_LABEL(state->status),error ? error->message : "Erro na exclusão.");
    g_clear_error(&error);
  }
}
static void delete_client_clicked(GtkButton *button,gpointer data) {
  AppState *state=data;gint64 id=0;
  if(!read_id(state->client_delete_id,&id)) {
    gtk_label_set_text(GTK_LABEL(state->status),"Informe ID de cliente válido.");return;
  }
  GtkWidget *window=gtk_widget_get_ancestor(GTK_WIDGET(button),GTK_TYPE_WINDOW);
  GtkAlertDialog *dialog=gtk_alert_dialog_new(
    "Excluir cliente %" G_GINT64_FORMAT " e TODAS as avaliações e cobranças vinculadas?",id);
  const char *buttons[]={"Cancelar","Excluir tudo",NULL};
  gtk_alert_dialog_set_buttons(dialog,buttons);
  gtk_alert_dialog_set_cancel_button(dialog,0);
  ClientDeleteRequest *request=g_new0(ClientDeleteRequest,1);
  request->state=state;request->id=id;
  gtk_alert_dialog_choose(dialog,GTK_WINDOW(window),NULL,delete_client_confirmed,request);
  g_object_unref(dialog);
}
static void rename_client_clicked(GtkButton *button,gpointer data) {
  (void)button;
  AppState *state=data;
  gint64 id=0;
  if(!read_id(state->client_id_entry,&id)) {
    gtk_label_set_text(GTK_LABEL(state->status),"Informe um ID numérico válido.");
    return;
  }
  char *name=g_strdup(gtk_editable_get_text(GTK_EDITABLE(state->client_edit_entry)));
  g_strstrip(name);
  GError *error=NULL;
  if(ef_store_rename_client(&state->store,id,name,&error)) {
    refresh_clients(state);
    notify_saved_and_copy(state,state->status,"Cliente atualizado.");
  }else{
    gtk_label_set_text(GTK_LABEL(state->status),error ? error->message : "Falha na edição.");
    g_clear_error(&error);
  }
  g_free(name);
}
static void on_search(GtkEditable *entry, gpointer data) {
  (void)entry;
  refresh_clients(data);
}
static void add_client(GtkButton *button, gpointer data) {
  (void)button;
  AppState *state=data;
  const char *name=gtk_editable_get_text(GTK_EDITABLE(state->client_entry));
  char *clean=g_strdup(name);
  g_strstrip(clean);
  GError *error=NULL;
  if(!ef_store_add_client(&state->store,clean,&error)) {
    gtk_label_set_text(GTK_LABEL(state->status),error->message);
    g_clear_error(&error);
  } else {
    gtk_editable_set_text(GTK_EDITABLE(state->client_entry),"");
    refresh_clients(state);
    notify_saved_and_copy(state,state->status,"Cliente cadastrado.");
  }
  g_free(clean);
}
/* The chosen folder is shared by Settings and encrypted automatic copies.
 * GTK folder choice is saved only after validation; offline mounts remain configured.
 */
static void refresh_folder_settings(AppState *state) {
  const char *folder=state->folder_settings.folder;
  if(state->assessment_folder)
    gtk_editable_set_text(GTK_EDITABLE(state->assessment_folder),folder ? folder : "");
  if(state->folder_settings_label)
    gtk_label_set_text(GTK_LABEL(state->folder_settings_label),folder ? folder : "Nenhuma pasta selecionada.");
  if(state->folder_settings_status) {
    if(!folder) {
      gtk_label_set_text(GTK_LABEL(state->folder_settings_status),
        "Pasta não configurada. Escolha onde guardar cópias criptografadas.");
    }else{
      GError *error=NULL;
      gboolean valid=ef_folder_settings_validate(folder,&error);
      gtk_label_set_text(GTK_LABEL(state->folder_settings_status),
        valid ? "Pasta acessível: cópias criptografadas após alterações. Sem sincronização contínua em segundo plano."
              : "Pasta configurada, mas indisponível ou sem permissão. Verifique a montagem antes de salvar.");
      g_clear_error(&error);
    }
  }
}
static void folder_selected(GObject *source,GAsyncResult *result,gpointer data) {
  GtkWidget *window=GTK_WIDGET(data);
  AppState *state=g_object_get_data(G_OBJECT(window),"app-state");
  GError *error=NULL;
  GFile *folder=gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source),result,&error);
  if(folder) {
    char *path=g_file_get_path(folder);
    if(path && ef_folder_settings_save(&state->folder_settings,path,&error)) {
      refresh_folder_settings(state);
      gtk_label_set_text(GTK_LABEL(state->folder_settings_status),
        "Pasta salva. A escolha será recuperada ao reiniciar o aplicativo.");
    } else {
      gtk_label_set_text(GTK_LABEL(state->folder_settings_status),
        error ? error->message : "Escolha uma pasta local montada e acessível.");
    }
    g_free(path);g_object_unref(folder);
  } else if(error && !g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED)) {
    gtk_label_set_text(GTK_LABEL(state->folder_settings_status),error->message);
  }
  g_clear_error(&error);
  g_object_unref(window);
}
static void choose_folder_clicked(GtkButton *button,gpointer data) {
  AppState *state=data;
  GtkWidget *window=gtk_widget_get_ancestor(GTK_WIDGET(button),GTK_TYPE_WINDOW);
  GtkFileDialog *dialog=gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog,"Selecionar pasta de cópias criptografadas");
  if(state->folder_settings.folder) {
    GError *error=NULL;
    if(ef_folder_settings_validate(state->folder_settings.folder,&error)) {
      GFile *initial=g_file_new_for_path(state->folder_settings.folder);
      gtk_file_dialog_set_initial_folder(dialog,initial);
      g_object_unref(initial);
    }
    g_clear_error(&error);
  }
  g_object_ref(window);
  gtk_file_dialog_select_folder(dialog,GTK_WINDOW(window),NULL,folder_selected,window);
  g_object_unref(dialog);
}
static void clear_folder_clicked(GtkButton *button,gpointer data) {
  (void)button;
  AppState *state=data;
  GError *error=NULL;
  if(ef_folder_settings_save(&state->folder_settings,NULL,&error)) {
    refresh_folder_settings(state);
    gtk_label_set_text(GTK_LABEL(state->folder_settings_status),"Pasta desassociada. Cópias existentes não foram excluídas.");
  } else {
    gtk_label_set_text(GTK_LABEL(state->folder_settings_status),
      error ? error->message : "Falha ao remover configuração.");
  }
  g_clear_error(&error);
}
static void validate_folder_clicked(GtkButton *button,gpointer data) {
  (void)button;
  refresh_folder_settings(data);
}

static void backup_clicked(GtkButton *button,gpointer data) {
  (void)button;
  AppState *state=data;
  const char *path=gtk_editable_get_text(GTK_EDITABLE(state->backup_entry));
  GError *error=NULL;
  if(ef_store_backup(&state->store,path,&error)) {
    gtk_label_set_text(GTK_LABEL(state->settings_status),"Backup SQLite criado e validado.");
  }else{
    gtk_label_set_text(GTK_LABEL(state->settings_status),error ? error->message : "Erro ao salvar backup.");
    g_clear_error(&error);
  }
}
typedef struct { AppState *state; char *path; } RestoreRequest;
static void restore_confirmed(GObject *source,GAsyncResult *result,gpointer data) {
  RestoreRequest *request=data;
  AppState *state=request->state;
  GError *choice_error=NULL;
  int response=gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source),result,&choice_error);
  g_clear_error(&choice_error);
  if(response!=1){g_free(request->path);g_free(request);return;}
  GError *error=NULL;
  if(ef_store_restore(&state->store,request->path,&error)) {
    refresh_clients(state);
    if(state->assessment_list) {
      GtkWidget *child=NULL;
      while((child=gtk_widget_get_first_child(state->assessment_list)))
        gtk_list_box_remove(GTK_LIST_BOX(state->assessment_list),child);
    }
    if(state->charge_list) {
      GtkWidget *child=NULL;
      while((child=gtk_widget_get_first_child(state->charge_list)))
        gtk_list_box_remove(GTK_LIST_BOX(state->charge_list),child);
    }
    notify_saved_and_copy(state,state->settings_status,"Restauração concluída.");
  }else{
    gtk_label_set_text(GTK_LABEL(state->settings_status),error ? error->message : "Restauração falhou.");
    g_clear_error(&error);
  }
  g_free(request->path);g_free(request);
}
static void restore_clicked(GtkButton *button,gpointer data) {
  AppState *state=data;
  GtkWidget *window=gtk_widget_get_ancestor(GTK_WIDGET(button),GTK_TYPE_WINDOW);
  GtkAlertDialog *dialog=gtk_alert_dialog_new("A restauração substituirá os clientes existentes. Continuar?");
  const char *buttons[]={"Cancelar","Restaurar",NULL};
  gtk_alert_dialog_set_buttons(dialog,buttons);
  gtk_alert_dialog_set_cancel_button(dialog,0);
  RestoreRequest *request=g_new0(RestoreRequest,1);
  request->state=state;
  request->path=g_strdup(gtk_editable_get_text(GTK_EDITABLE(state->restore_entry)));
  gtk_alert_dialog_choose(dialog,GTK_WINDOW(window),NULL,restore_confirmed,request);
  g_object_unref(dialog);
}
static void integrity_clicked(GtkButton *button,gpointer data) {
  (void)button;
  AppState *state=data;
  GError *error=NULL;
  if(ef_store_check_integrity(&state->store,&error))
    gtk_label_set_text(GTK_LABEL(state->settings_status),"SQLite íntegro.");
  else {
    gtk_label_set_text(GTK_LABEL(state->settings_status),error ? error->message : "Falha de integridade.");
    g_clear_error(&error);
  }
}
static void navigate(GtkButton *button, gpointer data) {
  GtkStack *stack=GTK_STACK(data);
  gtk_stack_set_visible_child(stack,GTK_WIDGET(g_object_get_data(G_OBJECT(button),"target-page")));
}
static void destroy_state(gpointer data) {
  AppState *state=data;
  ef_store_close(&state->store);
  ef_folder_settings_clear(&state->folder_settings);
  g_free(state);
}
typedef struct { GtkApplication *app; GtkWidget *window; GtkWidget *password; GtkWidget *message; } UnlockState;
static gboolean unlocked_session=FALSE;
static guint failed_unlock_attempts=0;
static gint64 unlock_blocked_until=0;
static void activate(GtkApplication *app,gpointer data);
static void unlock_clicked(GtkButton *button,gpointer data) {
  (void)button;UnlockState *unlock=data;
  if(g_get_monotonic_time()<unlock_blocked_until) {
    gtk_label_set_text(GTK_LABEL(unlock->message),"Muitas tentativas. Aguarde 30 segundos.");return;
  }
  const char *password=gtk_editable_get_text(GTK_EDITABLE(unlock->password));
  if(strlen(password)<12) {
    gtk_label_set_text(GTK_LABEL(unlock->message),"Use uma senha com pelo menos 12 caracteres.");return;
  }
  ef_store_set_passphrase(password);
  EfStore probe={0};GError *error=NULL;
  if(!ef_store_open(&probe,&error)) {
    gtk_label_set_text(GTK_LABEL(unlock->message),error ? error->message : "Não foi possível desbloquear.");
    g_clear_error(&error);ef_store_set_passphrase(NULL);
    gtk_editable_set_text(GTK_EDITABLE(unlock->password),"");
    if(++failed_unlock_attempts>=5) {
      unlock_blocked_until=g_get_monotonic_time()+30*G_USEC_PER_SEC;
      failed_unlock_attempts=0;
      gtk_label_set_text(GTK_LABEL(unlock->message),"Muitas tentativas. Aguarde 30 segundos.");
    }
    return;
  }
  ef_store_close(&probe);
  failed_unlock_attempts=0;
  unlocked_session=TRUE;
  gtk_window_destroy(GTK_WINDOW(unlock->window));
  activate(unlock->app,NULL);
  g_free(unlock);
}
static void show_unlock(GtkApplication *app) {
  UnlockState *unlock=g_new0(UnlockState,1);
  unlock->app=app;
  unlock->window=gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(unlock->window),"EvoluFisico — desbloquear");
  gtk_window_set_default_size(GTK_WINDOW(unlock->window),430,230);
  GtkWidget *layout=gtk_box_new(GTK_ORIENTATION_VERTICAL,14);
  gtk_widget_set_margin_start(layout,24);gtk_widget_set_margin_end(layout,24);
  gtk_widget_set_margin_top(layout,24);gtk_widget_set_margin_bottom(layout,24);
  gtk_box_append(GTK_BOX(layout),gtk_label_new("Senha de criptografia do banco local"));
  unlock->password=gtk_password_entry_new();
  gtk_box_append(GTK_BOX(layout),unlock->password);
  unlock->message=gtk_label_new("Banco novo: crie uma senha. Banco existente: informe a senha original.");
  gtk_label_set_wrap(GTK_LABEL(unlock->message),TRUE);
  gtk_box_append(GTK_BOX(layout),unlock->message);
  GtkWidget *button=gtk_button_new_with_label("Desbloquear");
  gtk_box_append(GTK_BOX(layout),button);
  g_signal_connect(button,"clicked",G_CALLBACK(unlock_clicked),unlock);
  gtk_window_set_child(GTK_WINDOW(unlock->window),layout);
  gtk_window_present(GTK_WINDOW(unlock->window));
}
static void activate(GtkApplication *app,gpointer data) {
  (void)data;
  if(!unlocked_session && !g_getenv("EVOLUFISICO_DB_PASSPHRASE")) {show_unlock(app);return;}
  AppState *state=g_new0(AppState,1);
  GError *folder_load_error=NULL;
  if(!ef_folder_settings_load(&state->folder_settings,&folder_load_error)) {
    g_warning("Configuração de pasta não carregada: %s",
      folder_load_error ? folder_load_error->message : "erro");
    g_clear_error(&folder_load_error);
  }
  GError *error=NULL;
  if(!ef_store_open(&state->store,&error)) {
    g_warning("SQLite indisponível: %s",error->message);
    g_clear_error(&error);
  }
  GtkWidget *window=gtk_application_window_new(app);
  g_object_set_data_full(G_OBJECT(window),"app-state",state,destroy_state);
  gtk_window_set_title(GTK_WINDOW(window),"EvoluFisico Linux — GTK4");
  gtk_window_set_default_size(GTK_WINDOW(window),1100,720);
  GtkWidget *root=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
  GtkWidget *sidebar=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);
  gtk_widget_set_size_request(sidebar,230,-1);
  gtk_widget_set_margin_start(sidebar,20);
  gtk_widget_set_margin_end(sidebar,20);
  gtk_widget_set_margin_top(sidebar,30);
  GtkWidget *brand=gtk_label_new("EvoluFisico");
  gtk_widget_add_css_class(brand,"title-2");
  gtk_box_append(GTK_BOX(sidebar),brand);
  const char *items[]={"Início","Clientes","Avaliações","Cobranças","Relatórios","Configurações"};
  GtkWidget *stack=gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(stack),GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
  for(guint i=0;i<G_N_ELEMENTS(items);i++){
    GtkWidget *button=gtk_button_new_with_label(items[i]);
    GtkWidget *page=gtk_box_new(GTK_ORIENTATION_VERTICAL,14);
    gtk_widget_set_margin_start(page,32);
    gtk_widget_set_margin_end(page,32);
    gtk_widget_set_margin_top(page,40);
    GtkWidget *heading=gtk_label_new(items[i]);
    gtk_widget_add_css_class(heading,"title-1");
    gtk_widget_set_halign(heading,GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page),heading);
    if(i==1){
      GtkWidget *form=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
      state->client_entry=gtk_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(state->client_entry),"Nome do cliente");
      gtk_widget_set_hexpand(state->client_entry,TRUE);
      GtkWidget *save=gtk_button_new_with_label("Salvar cliente");
      gtk_box_append(GTK_BOX(form),state->client_entry);
      gtk_box_append(GTK_BOX(form),save);
      gtk_box_append(GTK_BOX(page),form);
      state->status=gtk_label_new("Banco SQLCipher criptografado: prévia técnica.");
      gtk_widget_set_halign(state->status,GTK_ALIGN_START);
      gtk_box_append(GTK_BOX(page),state->status);
      GtkWidget *edit_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
      state->client_id_entry=gtk_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(state->client_id_entry),"ID do cliente");
      gtk_widget_set_size_request(state->client_id_entry,125,-1);
      state->client_edit_entry=gtk_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(state->client_edit_entry),"Novo nome");
      GtkWidget *rename=gtk_button_new_with_label("Renomear");
      gtk_box_append(GTK_BOX(edit_row),state->client_id_entry);
      gtk_box_append(GTK_BOX(edit_row),state->client_edit_entry);
      gtk_box_append(GTK_BOX(edit_row),rename);
      gtk_box_append(GTK_BOX(page),edit_row);
      g_signal_connect(rename,"clicked",G_CALLBACK(rename_client_clicked),state);
      gtk_widget_set_sensitive(rename,state->store.db!=NULL);
      GtkWidget *delete_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
      state->client_delete_id=gtk_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(state->client_delete_id),"ID do cliente a excluir");
      GtkWidget *delete_client_button=gtk_button_new_with_label("Excluir cliente...");
      gtk_box_append(GTK_BOX(delete_row),state->client_delete_id);
      gtk_box_append(GTK_BOX(delete_row),delete_client_button);
      gtk_box_append(GTK_BOX(page),delete_row);
      g_signal_connect(delete_client_button,"clicked",G_CALLBACK(delete_client_clicked),state);
      gtk_widget_set_sensitive(delete_client_button,state->store.db!=NULL);
      state->search_entry=gtk_search_entry_new();
      gtk_box_append(GTK_BOX(page),state->search_entry);
      g_signal_connect(state->search_entry,"search-changed",G_CALLBACK(on_search),state);
      state->client_list=gtk_list_box_new();
      state->client_scroll=gtk_scrolled_window_new();
      gtk_widget_set_vexpand(state->client_scroll,TRUE);
      gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(state->client_scroll),state->client_list);
      gtk_box_append(GTK_BOX(page),state->client_scroll);
      g_signal_connect(save,"clicked",G_CALLBACK(add_client),state);
      if(state->store.db) refresh_clients(state);
      else gtk_label_set_text(GTK_LABEL(state->status),"Erro ao abrir SQLite. Cadastro indisponível.");
      gtk_widget_set_sensitive(save,state->store.db!=NULL);
    }else if(i==2){
      state->assessment_picker=gtk_drop_down_new(NULL,NULL);
      g_object_set_data(G_OBJECT(state->assessment_picker),"state",state);
      state->assessment_date=gtk_entry_new();
      GDateTime *today=g_date_time_new_now_local();
      char *today_text=g_date_time_format(today,"%Y-%m-%d");
      gtk_editable_set_text(GTK_EDITABLE(state->assessment_date),today_text);
      g_free(today_text);g_date_time_unref(today);
      gtk_entry_set_placeholder_text(GTK_ENTRY(state->assessment_date),"Data AAAA-MM-DD");
      state->assessment_weight=gtk_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(state->assessment_weight),"Peso em kg (ex.: 70.5)");
      state->assessment_height=gtk_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(state->assessment_height),"Altura em cm (ex.: 170)");
      gtk_box_append(GTK_BOX(page),state->assessment_picker);
      gtk_box_append(GTK_BOX(page),state->assessment_date);
      gtk_box_append(GTK_BOX(page),state->assessment_weight);
      gtk_box_append(GTK_BOX(page),state->assessment_height);
      state->assessment_folder=gtk_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(state->assessment_folder),
        "Pasta para cópia criptografada após salvar (opcional)");
      gtk_editable_set_editable(GTK_EDITABLE(state->assessment_folder),FALSE);
      gtk_box_append(GTK_BOX(page),state->assessment_folder);
      GtkWidget *folder_hint=gtk_label_new("Configure em Configurações. Alterações em Clientes, Avaliações e Cobranças criam cópias criptografadas.");
      gtk_label_set_wrap(GTK_LABEL(folder_hint),TRUE);
      gtk_widget_set_halign(folder_hint,GTK_ALIGN_START);
      gtk_box_append(GTK_BOX(page),folder_hint);
      GtkWidget *save_assessment_button=gtk_button_new_with_label("Registrar avaliação");
      gtk_box_append(GTK_BOX(page),save_assessment_button);
      state->assessment_status=gtk_label_new("Avaliações protegidas por SQLCipher. Cópia de pasta opcional; apenas dados fictícios.");
      gtk_box_append(GTK_BOX(page),state->assessment_status);
      GtkWidget *show_assessments_button=gtk_button_new_with_label("Consultar avaliações");
      gtk_box_append(GTK_BOX(page),show_assessments_button);
      g_signal_connect(show_assessments_button,"clicked",G_CALLBACK(show_assessments),state);
      gtk_widget_set_sensitive(show_assessments_button,state->store.db!=NULL);
      state->assessment_delete_id=gtk_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(state->assessment_delete_id),"ID da avaliação a excluir");
      gtk_box_append(GTK_BOX(page),state->assessment_delete_id);
      GtkWidget *delete_assessment_button=gtk_button_new_with_label("Excluir avaliação...");
      g_object_set_data(G_OBJECT(delete_assessment_button),"assessment",GINT_TO_POINTER(TRUE));
      g_signal_connect(delete_assessment_button,"clicked",G_CALLBACK(delete_record_clicked),state);
      gtk_widget_set_sensitive(delete_assessment_button,state->store.db!=NULL);
      gtk_box_append(GTK_BOX(page),delete_assessment_button);
      state->assessment_list=gtk_list_box_new();
      state->assessment_scroll=gtk_scrolled_window_new();
      gtk_widget_set_vexpand(state->assessment_scroll,TRUE);
      gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(state->assessment_scroll),state->assessment_list);
      gtk_box_append(GTK_BOX(page),state->assessment_scroll);
      g_signal_connect(save_assessment_button,"clicked",G_CALLBACK(save_assessment),state);
      gtk_widget_set_sensitive(save_assessment_button,state->store.db!=NULL);
    }else if(i==3){
      state->charge_picker=gtk_drop_down_new(NULL,NULL);
      g_object_set_data(G_OBJECT(state->charge_picker),"state",state);
      state->charge_cents=gtk_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(state->charge_cents),"Valor em centavos (ex.: 2500 = R$ 25,00)");
      state->charge_description=gtk_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(state->charge_description),"Descrição do serviço");
      gtk_box_append(GTK_BOX(page),state->charge_picker);
      gtk_box_append(GTK_BOX(page),state->charge_cents);
      gtk_box_append(GTK_BOX(page),state->charge_description);
      GtkWidget *save_charge_button=gtk_button_new_with_label("Registrar cobrança");
      gtk_box_append(GTK_BOX(page),save_charge_button);
      state->charge_status=gtk_label_new("Cobranças no SQLCipher; utilize apenas dados fictícios.");
      gtk_box_append(GTK_BOX(page),state->charge_status);
      GtkWidget *show_charges_button=gtk_button_new_with_label("Consultar cobranças");
      gtk_box_append(GTK_BOX(page),show_charges_button);
      g_signal_connect(show_charges_button,"clicked",G_CALLBACK(show_charges),state);
      gtk_widget_set_sensitive(show_charges_button,state->store.db!=NULL);
      state->charge_receive_id=gtk_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(state->charge_receive_id),"ID da cobrança pendente a receber");
      gtk_box_append(GTK_BOX(page),state->charge_receive_id);
      GtkWidget *receive_charge_button=gtk_button_new_with_label("Receber cobrança pendente...");
      gtk_box_append(GTK_BOX(page),receive_charge_button);
      g_signal_connect(receive_charge_button,"clicked",G_CALLBACK(receive_charge_clicked),state);
      gtk_widget_set_sensitive(receive_charge_button,state->store.db!=NULL);
      state->charge_delete_id=gtk_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(state->charge_delete_id),"ID da cobrança a excluir");
      gtk_box_append(GTK_BOX(page),state->charge_delete_id);
      GtkWidget *delete_charge_button=gtk_button_new_with_label("Excluir cobrança...");
      g_object_set_data(G_OBJECT(delete_charge_button),"assessment",GINT_TO_POINTER(FALSE));
      g_signal_connect(delete_charge_button,"clicked",G_CALLBACK(delete_record_clicked),state);
      gtk_widget_set_sensitive(delete_charge_button,state->store.db!=NULL);
      gtk_box_append(GTK_BOX(page),delete_charge_button);
      state->charge_list=gtk_list_box_new();
      state->charge_scroll=gtk_scrolled_window_new();
      gtk_widget_set_vexpand(state->charge_scroll,TRUE);
      gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(state->charge_scroll),state->charge_list);
      gtk_box_append(GTK_BOX(page),state->charge_scroll);
      g_signal_connect(save_charge_button,"clicked",G_CALLBACK(save_charge),state);
      gtk_widget_set_sensitive(save_charge_button,state->store.db!=NULL);
    }else if(i==5){
      GtkWidget *folder_title=gtk_label_new("Pasta para cópias criptografadas");
      gtk_widget_add_css_class(folder_title,"heading");
      gtk_widget_set_halign(folder_title,GTK_ALIGN_START);
      gtk_box_append(GTK_BOX(page),folder_title);
      state->folder_settings_label=gtk_label_new("Nenhuma pasta selecionada.");
      gtk_widget_set_halign(state->folder_settings_label,GTK_ALIGN_START);
      gtk_label_set_selectable(GTK_LABEL(state->folder_settings_label),TRUE);
      gtk_label_set_wrap(GTK_LABEL(state->folder_settings_label),TRUE);
      gtk_box_append(GTK_BOX(page),state->folder_settings_label);
      GtkWidget *folder_buttons=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
      GtkWidget *choose_folder=gtk_button_new_with_label("Selecionar pasta...");
      GtkWidget *validate_folder=gtk_button_new_with_label("Verificar pasta");
      GtkWidget *clear_folder=gtk_button_new_with_label("Remover seleção");
      gtk_box_append(GTK_BOX(folder_buttons),choose_folder);
      gtk_box_append(GTK_BOX(folder_buttons),validate_folder);
      gtk_box_append(GTK_BOX(folder_buttons),clear_folder);
      gtk_box_append(GTK_BOX(page),folder_buttons);
      state->folder_settings_status=gtk_label_new("");
      gtk_label_set_wrap(GTK_LABEL(state->folder_settings_status),TRUE);
      gtk_widget_set_halign(state->folder_settings_status,GTK_ALIGN_START);
      gtk_box_append(GTK_BOX(page),state->folder_settings_status);
      g_signal_connect(choose_folder,"clicked",G_CALLBACK(choose_folder_clicked),state);
      g_signal_connect(validate_folder,"clicked",G_CALLBACK(validate_folder_clicked),state);
      g_signal_connect(clear_folder,"clicked",G_CALLBACK(clear_folder_clicked),state);
      state->backup_entry=gtk_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(state->backup_entry),"Caminho completo do arquivo de backup .sqlite");
      gtk_box_append(GTK_BOX(page),state->backup_entry);
      GtkWidget *backup=gtk_button_new_with_label("Criar backup");
      GtkWidget *integrity=gtk_button_new_with_label("Verificar integridade");
      state->restore_entry=gtk_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(state->restore_entry),"Arquivo .sqlite para restaurar");
      gtk_box_append(GTK_BOX(page),state->restore_entry);
      GtkWidget *restore=gtk_button_new_with_label("Restaurar backup (substitui dados atuais)");
      gtk_box_append(GTK_BOX(page),restore);
      gtk_box_append(GTK_BOX(page),backup);
      gtk_box_append(GTK_BOX(page),integrity);
      state->settings_status=gtk_label_new("Prévia experimental: backups SQLCipher criptografados.");
      gtk_widget_set_halign(state->settings_status,GTK_ALIGN_START);
      gtk_box_append(GTK_BOX(page),state->settings_status);
      g_signal_connect(backup,"clicked",G_CALLBACK(backup_clicked),state);
      g_signal_connect(integrity,"clicked",G_CALLBACK(integrity_clicked),state);
      g_signal_connect(restore,"clicked",G_CALLBACK(restore_clicked),state);
      gtk_widget_set_sensitive(backup,state->store.db!=NULL);
      gtk_widget_set_sensitive(integrity,state->store.db!=NULL);
      gtk_widget_set_sensitive(restore,state->store.db!=NULL);
    }else{
      GtkWidget *note=gtk_label_new("Módulo em desenvolvimento. Não utilize dados clínicos reais.");
      gtk_widget_set_halign(note,GTK_ALIGN_START);
      gtk_box_append(GTK_BOX(page),note);
    }
    char *name=g_strdup_printf("page-%u",i);
    gtk_stack_add_named(GTK_STACK(stack),page,name);
    g_free(name);
    g_object_set_data(G_OBJECT(button),"target-page",page);
    g_signal_connect(button,"clicked",G_CALLBACK(navigate),stack);
    gtk_box_append(GTK_BOX(sidebar),button);
  }
  refresh_pickers(state);
  refresh_folder_settings(state);
  gtk_box_append(GTK_BOX(root),sidebar);
  gtk_box_append(GTK_BOX(root),gtk_separator_new(GTK_ORIENTATION_VERTICAL));
  gtk_widget_set_hexpand(stack,TRUE);
  gtk_box_append(GTK_BOX(root),stack);
  gtk_window_set_child(GTK_WINDOW(window),root);
  gtk_window_present(GTK_WINDOW(window));
}
int main(int argc,char **argv) {
  GtkApplication *app=gtk_application_new("br.com.evolufisico",G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app,"activate",G_CALLBACK(activate),NULL);
  int result=g_application_run(G_APPLICATION(app),argc,argv);
  g_object_unref(app);
  return result;
}
