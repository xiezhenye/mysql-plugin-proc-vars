
#define MYSQL_SERVER

#include "sql_class.h"
#include "table.h"
#include "hash.h"
#include "global_threads.h"
#include "set_var.h"

bool schema_table_store_record(THD *thd, TABLE *table);

static struct st_mysql_information_schema proc_vars =
{ MYSQL_INFORMATION_SCHEMA_INTERFACE_VERSION };

static ST_FIELD_INFO proc_vars_table_fields[] =
{
  {"ID",    11,   MYSQL_TYPE_LONG,   0, MY_I_S_UNSIGNED, 0, 0},
  {"NAME",  255,   MYSQL_TYPE_STRING, 0, 0, 0, 0},
  {"VALUE", 255,  MYSQL_TYPE_STRING, 0, 0, 0, 0},
  {0, 0, MYSQL_TYPE_NULL, 0, 0, 0, 0}  
};

static bool fill_table(THD *thd, 
                 THD *cur_thd,
                 TABLE *table,
                 Item *cond);

static int proc_vars_fill_table(THD *thd, TABLE_LIST *tables, Item *cond)
{
  TABLE *table = tables->table;

  if (thd->killed != 0) {
    return 1;
  }

  mysql_mutex_lock(&LOCK_thread_count); 
  Thread_iterator it= global_thread_list_begin();
  Thread_iterator end= global_thread_list_end();
  for (; it != end; ++it)
  {
    THD *cur_thd = *it;
      if (cur_thd == NULL) 
      {
        continue;
      }
      fill_table(thd, cur_thd, table, cond);
  }
  mysql_mutex_unlock(&LOCK_thread_count);
  return 0;
}

static int proc_vars_init(void *ptr)
{
  ST_SCHEMA_TABLE *schema_table = (ST_SCHEMA_TABLE *)ptr;
  schema_table->fields_info = proc_vars_table_fields;
  schema_table->fill_table = proc_vars_fill_table;
  return 0;
}

mysql_declare_plugin(proc_vars) 
{
  MYSQL_INFORMATION_SCHEMA_PLUGIN,
  &proc_vars,
  "PROCESS_VARIABLES",
  "Xie Zhenye",
  "Variables of all process session",
  PLUGIN_LICENSE_GPL,
  proc_vars_init,
  NULL,
  0x0100,
  NULL,
  NULL,
  NULL,
  0
}
mysql_declare_plugin_end;


/*
  res= show_status_array(thd, wild, enumerate_sys_vars(thd, sorted_vars, option_type),
                         option_type, NULL, "", tables->table, upper_case_names, cond);
*/

static bool fill_table(THD *thd, 
                 THD *cur_thd,
                 TABLE *table,
                 Item *cond)
{
  //const char *wild = NullS;
  enum enum_var_type value_type = OPT_SESSION;
  struct system_status_var *status_var = NULL;
  const char *prefix = "";
  bool ucase_names = false;
  SHOW_VAR* variables = enumerate_sys_vars(cur_thd, ucase_names, value_type); 

  my_aligned_storage<SHOW_VAR_FUNC_BUFF_SIZE, MY_ALIGNOF(long)> buffer;
  char * const buff= buffer.data;
  char *prefix_end;
  /* the variable name should not be longer than 64 characters */
  char name_buffer[64];
  int len;
  LEX_STRING null_lex_str;
  SHOW_VAR tmp, *var;
  //Item *partial_cond= 0;
  enum_check_fields save_count_cuted_fields= cur_thd->count_cuted_fields;
  bool res= FALSE;
  const CHARSET_INFO *charset= system_charset_info;

  cur_thd->count_cuted_fields= CHECK_FIELD_WARN;  
  null_lex_str.str= 0;				// For sys_var->value_ptr()
  null_lex_str.length= 0;

  prefix_end=strnmov(name_buffer, prefix, sizeof(name_buffer)-1);
  if (*prefix)
    *prefix_end++= '_';
  len=name_buffer + sizeof(name_buffer) - prefix_end;
  //partial_cond= make_cond_for_info_schema(cond, table->pos_in_table_list);

  for (; variables->name; variables++)
  {
    strnmov(prefix_end, variables->name, len);
    name_buffer[sizeof(name_buffer)-1]=0;       /* Safety */
    /*
    if (ucase_names)
      make_upper(name_buffer);
    */
    restore_record(table, s->default_values);
    table->field[0]->store(cur_thd->thread_id);
    table->field[1]->store(name_buffer, strlen(name_buffer),
                           system_charset_info);
    /*
      if var->type is SHOW_FUNC, call the function.
      Repeat as necessary, if new var is again SHOW_FUNC
    */
    for (var=variables; var->type == SHOW_FUNC; var= &tmp)
      ((mysql_show_var_func)(var->value))(cur_thd, &tmp, buff);

    SHOW_TYPE show_type=var->type;
    if (show_type == SHOW_ARRAY)
    {
/*
      show_status_array(thd, wild, (SHOW_VAR *) var->value, value_type,
                        status_var, name_buffer, table, ucase_names, partial_cond);
*/
      
    }
    else
    {
/*      if (!(wild && wild[0] && wild_case_compare(system_charset_info,
                                                 name_buffer, wild)) &&
          (!partial_cond || partial_cond->val_int()))
      {*/
        char *value=var->value;
        const char *pos, *end;                  // We assign a lot of const's

        mysql_mutex_lock(&LOCK_global_system_variables);

        if (show_type == SHOW_SYS)
        {
          sys_var *var= ((sys_var *) value);
          show_type= var->show_type();
          value= (char*) var->value_ptr(cur_thd, value_type, &null_lex_str);
          charset= var->charset(cur_thd);
        }

        pos= end= buff;
        /*
          note that value may be == buff. All SHOW_xxx code below
          should still work in this case
        */
        switch (show_type) {
        case SHOW_DOUBLE_STATUS:
          value= ((char *) status_var + (ulong) value);
          /* fall through */
        case SHOW_DOUBLE:
          /* 6 is the default precision for '%f' in sprintf() */
          end= buff + my_fcvt(*(double *) value, 6, buff, NULL);
          break;
        case SHOW_LONG_STATUS:
          value= ((char *) status_var + (ulong) value);
          /* fall through */
        case SHOW_LONG:
        case SHOW_LONG_NOFLUSH: // the difference lies in refresh_status()
          end= int10_to_str(*(long*) value, buff, 10);
          break;
        case SHOW_SIGNED_LONG:
          end= int10_to_str(*(long*) value, buff, -10);
          break;
        case SHOW_LONGLONG_STATUS:
          value= ((char *) status_var + (ulong) value);
          /* fall through */
        case SHOW_LONGLONG:
          end= longlong10_to_str(*(longlong*) value, buff, 10);
          break;
        case SHOW_HA_ROWS:
          end= longlong10_to_str((longlong) *(ha_rows*) value, buff, 10);
          break;
        case SHOW_BOOL:
          end= strmov(buff, *(bool*) value ? "ON" : "OFF");
          break;
        case SHOW_MY_BOOL:
          end= strmov(buff, *(my_bool*) value ? "ON" : "OFF");
          break;
        case SHOW_INT:
          end= int10_to_str((long) *(uint32*) value, buff, 10);
          break;
        case SHOW_HAVE:
        {
          SHOW_COMP_OPTION tmp= *(SHOW_COMP_OPTION*) value;
          pos= show_comp_option_name[(int) tmp];
          end= strend(pos);
          break;
        }
        case SHOW_CHAR:
        {
          if (!(pos= value))
            pos= "";
          end= strend(pos);
          break;
        }
       case SHOW_CHAR_PTR:
        {
          if (!(pos= *(char**) value))
            pos= "";

          DBUG_EXECUTE_IF("alter_server_version_str",
                          if (!my_strcasecmp(system_charset_info,
                                             variables->name,
                                             "version")) {
                            pos= "some-other-version";
                          });

          end= strend(pos);
          break;
        }
        case SHOW_LEX_STRING:
        {
          LEX_STRING *ls=(LEX_STRING*)value;
          if (!(pos= ls->str))
            end= pos= "";
          else
            end= pos + ls->length;
          break;
        }
        case SHOW_KEY_CACHE_LONG:
          value= (char*) dflt_key_cache + (ulong)value;
          end= int10_to_str(*(long*) value, buff, 10);
          break;
        case SHOW_KEY_CACHE_LONGLONG:
          value= (char*) dflt_key_cache + (ulong)value;
	  end= longlong10_to_str(*(longlong*) value, buff, 10);
	  break;
        case SHOW_UNDEF:
          break;                                        // Return empty string
        case SHOW_SYS:                                  // Cannot happen
        default:
          DBUG_ASSERT(0);
          break;
        }
        table->field[2]->store(pos, (uint32) (end - pos), charset);
        cur_thd->count_cuted_fields= CHECK_FIELD_IGNORE;
        table->field[2]->set_notnull();

        mysql_mutex_unlock(&LOCK_global_system_variables);

        if (schema_table_store_record(thd, table))
        {
          res= TRUE;
          goto end;
        }
     //}
    }
  }
end:
  thd->count_cuted_fields= save_count_cuted_fields;
  DBUG_RETURN(res);
}



