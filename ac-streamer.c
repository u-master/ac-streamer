
/* TODOList:
   + В таблицу channel добавляем isRecordOn (boolean).
   + В объекте cloudchannel добавляем поле isRecordOn и считываем с таблицы в момент инициализации
   + В момент старта трубы в engine проверяем поле, и если true, то запускаем, а если false, 
     то видимо пока даже не стартуем
   + Сделать таблицу записей. Будет много записей!
   + Сделать таблицу сессий, с возможностью в engine подменять таблицу.
   + Сессии хранят id, имя файла потока mp4 через http, актуальноЛи, время создания, время актуальности. 
   - По возможности хранить IP клиента, UA клиента и прочие данные клиента.
   - Сессии создаются, но не удаляются. Они просто теряют актуальность.
   - По достижении определенного количества сессий - создавать новую таблицу и делать подмену.
   - Далее делать listner. Команды:
        startrec $idchanel
          меняет флаг в таблице channel
          проверяет флаг в объекте cloudchannel
          если true - по возможности - проверить трубу записи
          если false - установить флаг и запустить трубу записи
        stoprec $idchannel
          меняет флаг в таблице channel
          устанавливает флаг в объекте cloudchannel
          останавливает трубу записи
        startview $idchannel $idsession
        stopview $idsession
        startarch $idrecord $startsec $idsession
        stoparch $idsession
   - Модификация трубы для просмотра, функции для старта и остановки просмотра.
     Организация потоков через файловую систему: создание файла потока, symlinkов в каталог web-сервера,
     заполнение таблицы сессий. Указание времени актуальности. По истечении актуальности либо при
     остановке сессии по требованию (команды слушателю stopview и stoparch) - заполнение
     флага актуальности в таблице сессий, удаление symlinkа, файла потока. Создание и контроль за 
     обновлением сессии берет на себя клиент-серверное приложение web-страницы.
   - Просмотр архива запускает новую трубу. Функции инициализации, старта и остановки потока.
     Организация потока - аналогична.
   - Далее можно приступать к разработке web-страницы.


  //old
   - Именование архива по ВремяШтампу
   - При получении ошибки немедленно создание новой трубы и запуск записи
   ...дальше-больше!

   Пока один поток пишем
   Пока не раздаем поток
   Пока нет обмена данными с внешним миром
*/


#include <glib.h>
#include <gmodule.h>
#include <string.h>
#include <gst/gst.h>
#include <gst/rtsp/gstrtsp.h>
#include <libpq-fe.h>
//#include "ac_pipeline.h"


#define AC_DEBUG_AC 1
#define AC_DEBUG_RTSPSRC 0
#define AC_DEBUG_POSTGRES 1

#define AC_CHANNEL_DEFAULT_NAME "noname"
#define AC_CHANNEL_DEFAULT_STREAM1 "rtsp://10.44.0.2:554/user=admin_password=5Va9kjwM_channel=1_stream=0"
#define AC_CHANNEL_DEFAULT_STREAM2 ""
#define AC_CHANNEL_DEFAULT_TYPE1 "1"
#define AC_CHANNEL_DEFAULT_TYPE2 "1"
#define AC_CHANNEL_DEFAULT_LOGIN ""
#define AC_CHANNEL_DEFAULT_PASSWORD ""
#define AC_CHANNEL_DEFAULT_ARCHIVE_RELPATH "/AC-c1/noname"

#define AC_LISTENER_PORT 5598

const gchar* ac_version = "0.1";
const gchar* ac_settings_filename = "./settings.ini";


/*  ---- Tools ----  */

gchar* _ac_itoa (guint number);
gboolean _ac_isNum (gchar *id, GError *error);

/*  ---- ----- ----  */



/*  ---- Settings ----  */

struct {
  gboolean isInitialize;
  PGconn* pgServerConnection;
  gchar *database_server;
  gchar *database_port;
  gchar *database_login;
  gchar *database_password;
  gchar *database_dbname;
  gchar *common_servername;
  gchar *common_dirroot;
  gchar *common_dirarchive;
  gchar *common_serverid;
} ac_globalsettings;

guint initGlobalSettings();
void doneGlobalSettings();
void printGlobalSettings();

// for new key-value add handling at: initGlobalSettings, translateValueGlobalSettings, doneGlobalSettings, printGlobalSettings

/*  ---- -------- ----  */


gchar* translateValueGlobalSettings(gchar * sourceString) {
  gchar *keystr;

  if ((keystr=g_strstr_len(sourceString, -1, "_"))==NULL) return "NULL\0";
  keystr[0]='\0';

  if (!g_strcmp0(sourceString, "common")) {
    keystr[0]='_';
    keystr++;
    if (!g_strcmp0(keystr, "dirroot")) return ac_globalsettings.common_dirroot;
    if (!g_strcmp0(keystr, "dirarchive")) return ac_globalsettings.common_dirarchive;
    if (!g_strcmp0(keystr, "servername")) return ac_globalsettings.common_servername;
    if (!g_strcmp0(keystr, "serverid")) return ac_globalsettings.common_serverid; //_ac_itoa();
  }

  if (!g_strcmp0(sourceString, "database")) {
    keystr[0]='_';
    keystr++;
    if (!g_strcmp0(keystr, "server")) return ac_globalsettings.database_server;
    if (!g_strcmp0(keystr, "port")) return ac_globalsettings.database_port;
    if (!g_strcmp0(keystr, "login")) return ac_globalsettings.database_login;
    if (!g_strcmp0(keystr, "password")) return ac_globalsettings.database_password;
    if (!g_strcmp0(keystr, "dbname")) return ac_globalsettings.database_dbname;
  }

  return "NULL\0";
}

gchar* parseValueGlobalSettings(gchar *parseString) {
  if (parseString==NULL) return NULL;
  gchar *beginstr, *endstr, *variablestr, *translatedstr, *resultString;
  if ((beginstr=g_strstr_len(parseString, -1, "$"))!=NULL) {
    beginstr++;
    if ((*beginstr)!='\0' && (endstr=g_strstr_len(beginstr, -1, "$"))!=NULL) { // && (beginstr+1)<endstr
      variablestr=g_strndup(beginstr, endstr-beginstr);
      translatedstr=translateValueGlobalSettings(variablestr);  // Get string from global instance! Don't free it!!!
      g_free(variablestr);
      variablestr=g_strndup(parseString, beginstr-parseString-1);
      resultString=g_strconcat (variablestr, translatedstr,endstr+1, NULL);
      g_free(variablestr);
      g_free(parseString);
      return parseValueGlobalSettings(resultString);
    }
  }
  return parseString;
}

gchar* getValueGlobalSettings (GKeyFile *fileSettings, gchar *groupName, gchar *keyName, GError **error) {
  gchar *result=NULL;
  if ((result=g_key_file_get_string (fileSettings, groupName, keyName, error))==NULL) return NULL;
  result=parseValueGlobalSettings(g_strstrip(result)); // In function body parameter will deallocated from memory. But returned string must be cleared!
  return result;
}

guint initGlobalSettings() {
  GError *error = NULL;
  GKeyFile *fileSettings = NULL;
  gchar *pgString = NULL;
  gchar *pgQuery = NULL;
  PGresult *pgServerResult = NULL;

  // Read settings from INI file.

  fileSettings = g_key_file_new();

  if (!g_key_file_load_from_file(fileSettings, ac_settings_filename, G_KEY_FILE_NONE, &error)) {
    g_printerr (" >  Error (%i): %s\n", error->code, error->message);
    g_error_free (error);
    return 1;
  }

  if ((ac_globalsettings.common_dirroot=getValueGlobalSettings(fileSettings, "common", "dirroot", &error))==NULL) {
    g_printerr (" >  Error (%i): %s\n", error->code, error->message);
    g_error_free (error);
    if (AC_DEBUG_AC)
      g_print (" >  Error getting value from %s. Group: %s. Key: %s.\n", ac_settings_filename, "common", "dirroot");
    return 2;
  }

  if ((ac_globalsettings.common_dirarchive=getValueGlobalSettings(fileSettings, "common", "dirarchive", &error))==NULL) {
    g_printerr (" >  Error (%i): %s\n", error->code, error->message);
    g_error_free (error);
    if (AC_DEBUG_AC)
      g_print (" >  Error getting value from %s. Group: %s. Key: %s.\n", ac_settings_filename, "common", "dirarchive");
    doneGlobalSettings();
    return 2;
  }

  if ((ac_globalsettings.database_server=getValueGlobalSettings(fileSettings, "database", "server", &error))==NULL) {
    g_printerr (" >  Error (%i): %s\n", error->code, error->message);
    g_error_free (error);
    if (AC_DEBUG_AC)
      g_print (" >  Error getting value from %s. Group: %s. Key: %s.\n", ac_settings_filename, "database", "server");
    doneGlobalSettings();
    return 2;
  }

  if ((ac_globalsettings.database_port=getValueGlobalSettings(fileSettings, "database", "port", &error))==NULL) {
    g_printerr (" >  Error (%i): %s\n", error->code, error->message);
    g_error_free (error);
    if (AC_DEBUG_AC)
      g_print (" >  Error getting value from %s. Group: %s. Key: %s.\n", ac_settings_filename, "database", "port");
    doneGlobalSettings();
    return 2;
  }

  if ((ac_globalsettings.database_login=getValueGlobalSettings(fileSettings, "database", "login", &error))==NULL) {
    g_printerr (" >  Error (%i): %s\n", error->code, error->message);
    g_error_free (error);
    if (AC_DEBUG_AC)
      g_print (" >  Error getting value from %s. Group: %s. Key: %s.\n", ac_settings_filename, "database", "login");
    doneGlobalSettings();
    return 2;
  }

  if ((ac_globalsettings.database_password=getValueGlobalSettings(fileSettings, "database", "password", &error))==NULL) {
    g_printerr (" >  Error (%i): %s\n", error->code, error->message);
    g_error_free (error);
    if (AC_DEBUG_AC)
      g_print (" >  Error getting value from %s. Group: %s. Key: %s.\n", ac_settings_filename, "database", "password");
    doneGlobalSettings();
    return 2;
  }

  if ((ac_globalsettings.database_dbname=getValueGlobalSettings(fileSettings, "database", "dbname", &error))==NULL) {
    g_printerr (" >  Error (%i): %s\n", error->code, error->message);
    g_error_free (error);
    if (AC_DEBUG_AC)
      g_print (" >  Error getting value from %s. Group: %s. Key: %s.\n", ac_settings_filename, "database", "dbname");
    doneGlobalSettings();
    return 2;
  }

  if ((ac_globalsettings.common_servername=getValueGlobalSettings(fileSettings, "common", "servername", &error))==NULL) {
    g_printerr (" >  Error (%i): %s\n", error->code, error->message);
    g_error_free (error);
    if (AC_DEBUG_AC)
      g_print (" >  Error getting value from %s. Group: %s. Key: %s.\n", ac_settings_filename, "common", "servername");
    doneGlobalSettings();
    return 2;
  }

  if (error!=NULL) 
    g_error_free (error);
  g_key_file_free (fileSettings);

  // Initialize connection with PostgreSQL Server

  pgString=g_strconcat("host=", ac_globalsettings.database_server, " port=", ac_globalsettings.database_port,
    " user=", ac_globalsettings.database_login, " password=", ac_globalsettings.database_password, 
    " dbname=",ac_globalsettings.database_dbname, NULL);
  ac_globalsettings.pgServerConnection=PQconnectdb(pgString);
  g_free(pgString);

  if (ac_globalsettings.pgServerConnection==NULL || (PQstatus(ac_globalsettings.pgServerConnection))!=CONNECTION_OK) {
    g_printerr (" >  Error: database connection failed!\n");
    if (AC_DEBUG_AC) {
      pgString=g_strconcat("host=", ac_globalsettings.database_server, " port=", ac_globalsettings.database_port,
        " user=", ac_globalsettings.database_login, " password=******* dbname=",ac_globalsettings.database_dbname, NULL);
      g_print (" >  Error database connection: %s. Parameters: %s\n", PQerrorMessage(ac_globalsettings.pgServerConnection), pgString);
      g_free(pgString);
    }
    doneGlobalSettings();
    return 3;
  }

  // BY POSTGRESQL: Set always-secure search path, so malicous users can't take control.
 /* pgServerResult = PQexec(ac_globalsettings.pgServerConnection, "SELECT pg_catalog.set_config('search_path', '', false);");
  g_print("SELECT pg_catalog.set_config('search_path', '', false);");
  if (PQresultStatus(pgServerResult) != PGRES_COMMAND_OK) {
    g_printerr (" >  Error during database security set.\n");
    if (AC_DEBUG_AC) 
      g_print(" >  SET failed: %s\n", PQerrorMessage(ac_globalsettings.pgServerConnection));
    PQclear(pgServerResult);
    doneGlobalSettings();
    return 4;
  }
  PQclear(pgServerResult);*/

  // Read settings from database

  pgString=PQescapeLiteral(ac_globalsettings.pgServerConnection,ac_globalsettings.common_servername,strlen(ac_globalsettings.common_servername));
  pgQuery=g_strconcat("SELECT server_id, root_dir, archive_dir FROM t_General WHERE name=", pgString, NULL);
  pgServerResult = PQexec(ac_globalsettings.pgServerConnection, pgQuery);
  PQfreemem(pgString);
  if (PQresultStatus(pgServerResult) != PGRES_TUPLES_OK) {
    g_printerr (" >  Error: no settings in database.\n");
    if (AC_DEBUG_AC) 
      g_print(" >  SELECT failed: %s. Query string: %s\n", PQerrorMessage(ac_globalsettings.pgServerConnection), pgQuery);
    g_free(pgQuery);
    PQclear(pgServerResult);
    doneGlobalSettings();
    return 5;
  }

  g_free(pgQuery);
  if ((pgString=PQgetvalue(pgServerResult, 0, 0))!=NULL) {
    g_free (ac_globalsettings.common_serverid);
    ac_globalsettings.common_serverid=g_strdup (pgString);
  }
  if ((pgString=PQgetvalue(pgServerResult, 0, 1))!=NULL) {
    g_free (ac_globalsettings.common_dirroot);
    ac_globalsettings.common_dirroot=g_strdup (pgString);
  }
  if ((pgString=PQgetvalue(pgServerResult, 0, 2))!=NULL) {
    g_free (ac_globalsettings.common_dirarchive);
    ac_globalsettings.common_dirarchive=g_strdup (pgString);
  }
  PQclear(pgServerResult);

  // All success!

  ac_globalsettings.isInitialize=TRUE;
  return 0;
}

void doneGlobalSettings() {
  ac_globalsettings.isInitialize = FALSE;
  g_free (ac_globalsettings.common_dirroot);
  g_free (ac_globalsettings.common_dirarchive);
  g_free (ac_globalsettings.database_server);
  g_free (ac_globalsettings.database_port);
  g_free (ac_globalsettings.database_login);
  g_free (ac_globalsettings.database_password);
  g_free (ac_globalsettings.database_dbname);
  g_free (ac_globalsettings.common_servername);
  g_free (ac_globalsettings.common_serverid);
  PQfinish(ac_globalsettings.pgServerConnection);
}

void printGlobalSettings() {
  g_print ("isInitialize = %i\n", ac_globalsettings.isInitialize);
  g_print ("common_servername = %s\n", ac_globalsettings.common_servername);
  g_print ("common_serverid = %s\n", ac_globalsettings.common_serverid);
  g_print ("common_dirroot = %s\n", ac_globalsettings.common_dirroot);
  g_print ("common_dirarchive = %s\n", ac_globalsettings.common_dirarchive);
  g_print ("database_server = %s\n", ac_globalsettings.database_server);
  g_print ("database_port = %s\n", ac_globalsettings.database_port);
  g_print ("database_login = %s\n", ac_globalsettings.database_login);
  g_print ("database_password = %s\n", "*******");
  //g_print ("database_password = %s\n", ac_globalsettings.database_password);
  g_print ("database_dbname = %s\n", ac_globalsettings.database_dbname);
  g_print ("DB connection status: ");
  if ((PQstatus(ac_globalsettings.pgServerConnection))==CONNECTION_OK)
    g_print ("CONNECTED\n");
  else
    g_print ("DISCONNECTED\n");
}



/*  ---- Listner ----  */

typedef struct {
  int lsocket;
  GThread *thread;
} Listener;

guint initListener (Listener *listener);
void doneListener (Listener *listener);

/*  ---- ------- ----  */
guint initListener (Listener *listener) {

  listener->thread = g_thread_new ("Listener", )

  struct sockaddr_in addr;

  listener->lsocket = socket (AF_INET, SOCK_STREAM, 0);
  if (listener->lsocket < 0) {
    g_printerr (" >  Error: listener cannot start (socket).\n");
    return 1;
  }

  fcntl (listener->lsocket, F_SETFL, O_NONBLOCK);
  addr.sin_family = AF_INET;
  addr.sin_port = htons (AC_LISTENER_PORT);
  addr.sin_addr.s_addr = INADDR_ANY;
  if (bind (listener->lsocket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    g_printerr (" >  Error: listener cannot start (bind).\n");
    if (AC_DEBUG_AC) {
      g_print (" >  Error listener initialization: Cannot bind socket. Sockaddr_in: sin_family=%i, sin_port=%i, s_addr=%i, AC_LISTENER_PORT=%i, AF_INET=%i, INADDR_ANY=%i",
        addr.sin_family, addr.sin_port, addr.sin_addr.s_addr, AC_LISTENER_PORT, AF_INET, INADDR_ANY);
    }
    close(listener->lsocket);
    return 2;
  }

}

void _engine_eachStartChannel (GQuark key_id, gpointer data, gpointer user_data) {
  if (data != NULL) {
    ((CloudChannel*)data)->thread = g_thread_new (((CloudChannel*)data)->id, _engine_StartThread, data);
  }
}

gpointer _engine_StartThread (gpointer data) {
  if (data != NULL) {
    if (((CloudChannel*)data)->isRecordOn) {
      gchar *filename = _engine_generateRecFileName();
      playRecordPipelineChannel (((CloudChannel*)data)->pipeline, filename);
      g_free (filename);
    }
  }
}

void doneListener (Listener *listener) {

}


/*  ---- Pipeline ----  */

typedef struct {
  gboolean isInitialize;

  // Pipeline itself
  GstElement *pipeline;

  // Elements pipeline
  GstElement *elem_rtspsrc;
  GstElement *elem_depayer;
  GstElement *elem_parse;
  GstElement *elem_muxer;
  GstElement *elem_filesink;

  // Message bus and main pipeline loop
  GstBus *bus;
  guint bus_watch_id;
  GMainLoop *loop;

  // Parameters connection
  gchar *source_address;  // Local network camera's stream path
  gchar *output_dir;
  gchar *output_file;     // Current output file for stream store
  GstRTSPLowerTrans rtsp_protocol; // Protocol for interconnection with camera. Maybe 0, so default. (GST_RTSP_LOWER_TRANS_UDP | GST_RTSP_LOWER_TRANS_TCP)   
} PipelineChannel;

guint initPipelineChannel (PipelineChannel *pipe_record);
void donePipelineChannel (PipelineChannel *pipe_record);
void playRecordPipelineChannel (PipelineChannel *pipe_record, gchar* filename);
void stopRecordPipelineChannel (PipelineChannel *pipe_record);
static gboolean buscallbackPipelineChannel (GstBus *, GstMessage *, gpointer);
static void on_recordrtspsrc_pad_added (GstElement *, GstPad *, gpointer);

/*  ---- -------- ----  */

guint initPipelineChannel (PipelineChannel *pipe_record, gchar* source_address, gchar* output_dir) {
  GstPad * padsinktmp = NULL, * padsrctmp = NULL;  // For request pads connection

  if (pipe_record==NULL) {
    g_printerr (" >  Error: Pipeline is null.\n");
    return 4;
  }

  if (!ac_globalsettings.isInitialize)
    if (initGlobalSettings()) {
      g_printerr(" >  Error: Initialization of global settings failed!\n");
      return 1;
    }

  if (source_address!=NULL)
    pipe_record->source_address = g_strdup (source_address);
  else
    pipe_record->source_address = g_strdup (AC_CHANNEL_DEFAULT_STREAM1);

  if (output_file!=NULL)
    pipe_record->output_dir = g_strdup (output_dir);
  else
    pipe_record->output_dir = g_strconcat (ac_globalsettings.common_dirarchive, AC_CHANNEL_DEFAULT_ARCHIVE_RELPATH, NULL);

  pipe_record->output_file = g_strconcat (pipe_record->output_dir, "/stream.mp4", NULL);  // Default File

  /* !!!!!!!!! STUB !!!!!!!!!!! */
  pipe_record->rtsp_protocol = 0;

  /* Create gstreamer elements */
  if (!(pipe_record->pipeline = gst_pipeline_new ("ac-record")) && AC_DEBUG_AC)
    g_print("Error: Element could not be created (record#ac-record).\n ");
  if (!(pipe_record->elem_rtspsrc = gst_element_factory_make ("rtspsrc", "rtsp-source")) && AC_DEBUG_AC)
    g_print("Error: Element could not be created (record#rtsp-source).\n");
  if (!(pipe_record->elem_depayer = gst_element_factory_make ("rtph264depay", "h264-depay")) && AC_DEBUG_AC)
    g_print("Error: Element could not be created (record#h264-depay).\n ");
  if (!(pipe_record->elem_parse = gst_element_factory_make ("h264parse", "h264-parse")) && AC_DEBUG_AC)
    g_print("Error: Element could not be created (record#h264parse).\n ");
  if (!(pipe_record->elem_muxer = gst_element_factory_make ("mp4mux", "mpeg-muxer")) && AC_DEBUG_AC)
    g_print("Error: Element could not be created (record#mpeg-muxer).\n");
  if (!(pipe_record->elem_filesink = gst_element_factory_make ("filesink",  "file-sink")) && AC_DEBUG_AC)
    g_print("Error: Element could not be created (record#file-sink).\n");
  if (!pipe_record->pipeline || !pipe_record->elem_rtspsrc || !pipe_record->elem_depayer || 
      !pipe_record->elem_parse || !pipe_record->elem_muxer || !pipe_record->elem_filesink) {
    g_printerr(" >  Error: At least one element not be created. Exiting...\n");
    return 2;
  }
  g_print (" >  Elements created.\n");

  /* Set up the pipeline */
  g_print (" >  Setting up element's parameters.\n");
  g_print (" >>  Source: %s\n", pipe_record->source_address);
  g_object_set (G_OBJECT (pipe_record->elem_rtspsrc), "location", pipe_record->source_address, NULL);
  g_print (" >>  Protocol (#): %d\n", pipe_record->rtsp_protocol);
  if (pipe_record->rtsp_protocol)
    g_object_set (G_OBJECT (pipe_record->elem_rtspsrc), "protocols", pipe_record->rtsp_protocol, NULL);
  g_print (" >>  Output file: %s\n", pipe_record->output_file);
  g_object_set (G_OBJECT (pipe_record->elem_filesink), "location", pipe_record->output_file, NULL);
  g_print (" >>  Decode in \"faststart\" format\n");
  g_object_set (G_OBJECT (pipe_record->elem_muxer), "faststart", TRUE, NULL);
  if (AC_DEBUG_RTSPSRC) {
    g_print (" >>  DEBUG rtspsrc ON!\n");
    g_object_set (G_OBJECT (pipe_record->elem_rtspsrc), "debug", TRUE, NULL);
  }

  /* Add a message handler*/
  g_print (" >  Message bus initializing...\n");
  pipe_record->bus = gst_pipeline_get_bus (GST_PIPELINE (pipe_record->pipeline));
  pipe_record->loop = g_main_loop_new (NULL, FALSE);
  pipe_record->bus_watch_id = gst_bus_add_watch (pipe_record->bus, buscallbackPipelineChannel, pipe_record->loop);
  gst_object_unref (pipe_record->bus);

  /* Add all elements into the pipeline. Then link the elements together */
  /* elem_rtspsrc ~> elem_depayer -> elem_parse  *> elem_muxer -> elem_filesink */
  g_print (" >  Initialize pipeline...\n");
  g_print (" >>  Drop elements...\n");
  gst_bin_add_many (GST_BIN (pipe_record->pipeline), pipe_record->elem_rtspsrc, pipe_record->elem_depayer, pipe_record->elem_parse, pipe_record->elem_muxer, pipe_record->elem_filesink, NULL);
  g_print (" >>  Link static pads...\n");
  if (!gst_element_link (pipe_record->elem_depayer, pipe_record->elem_parse)) {
    if (AC_DEBUG_AC) 
      g_print(" >  Error: static pads cannot link (record#h264-depay->record#parse).\n");
    g_printerr(" >  Error: Cannot link pads.\n");
    return 3;
  }
  if (!gst_element_link (pipe_record->elem_muxer, pipe_record->elem_filesink)) {
    if (AC_DEBUG_AC) 
      g_print(" >  Error: static pads cannot link (record#mpeg-muxer->record#file-sink).\n");
    g_printerr(" >  Error: Cannot link pads.\n");
    return 3;
  }
  //gst_element_link_many (elem_parse, elem_muxer, elem_filesink, NULL);
  g_print (" >>  Link on-demand pads...\n");
  if (!(padsrctmp = gst_element_get_static_pad ( (GstElement *) (pipe_record->elem_parse), "src")) && AC_DEBUG_AC)
    g_print (" >  Error: request pad cannot link. Srcpad cannot get (record#parse).\n");
  if (!(padsinktmp = gst_element_get_compatible_pad ( (GstElement *) (pipe_record->elem_muxer), padsrctmp, NULL)) && AC_DEBUG_AC)
    g_print (" >  Error: request pad cannot link. Sinkpad cannot create (record#mpeg-muxer).\n");
  if (!padsrctmp || !padsinktmp) {
    g_printerr(" >  Error: Cannot link pads.\n");
    gst_object_unref (GST_OBJECT (padsinktmp));
    gst_object_unref (GST_OBJECT (padsrctmp));
    return 3;
  }
  if (gst_pad_link (padsrctmp, padsinktmp)!=GST_PAD_LINK_OK)  {
    if (AC_DEBUG_AC) 
      g_print(" >  Error: request pads cannot link (record#parse*>record#mpeg-muxer).\n");
    g_printerr(" >  Error: Cannot link pads.\n");
    gst_object_unref (GST_OBJECT (padsinktmp));
    gst_object_unref (GST_OBJECT (padsrctmp));
    return 3;
  };
  g_print (" >>  SOURCE %s to SINK %s. Linked=%i/%i.\n.", GST_PAD_NAME(padsrctmp),GST_PAD_NAME(padsinktmp),gst_pad_is_linked (padsrctmp),gst_pad_is_linked (padsinktmp));
  gst_object_unref (GST_OBJECT (padsinktmp));
  gst_object_unref (GST_OBJECT (padsrctmp));
  g_print (" >>  Callback connecting for dynamic pads...\n");
  g_signal_connect (pipe_record->elem_rtspsrc, "pad-added", G_CALLBACK (on_recordrtspsrc_pad_added), pipe_record->elem_depayer);

  g_print (" >  Pipeline get ready!\n");
  pipe_record->isInitialize=TRUE;
  return 0;
}

void donePipelineChannel (PipelineChannel *pipe_record) {
  pipe_record->isInitialize=FALSE;
  g_free(pipe_record->source_address);
  g_free(pipe_record->output_dir);
  g_free(pipe_record->output_file);
  gst_object_unref (GST_OBJECT (pipe_record->pipeline));
  g_source_remove (pipe_record->bus_watch_id);
  g_main_loop_unref (pipe_record->loop);
}

void playRecordPipelineChannel (PipelineChannel *pipe_record, gchar* filename) {
  if (filename != NULL) {
    pipe_record->output_file = g_strconcat (pipe_record->output_dir, filename, NULL);
    g_print (" >>  Output file: %s\n", pipe_record->output_file);
    g_object_set (G_OBJECT (pipe_record->elem_filesink), "location", pipe_record->output_file, NULL);
  }
  gst_element_set_state (pipe_record->pipeline, GST_STATE_PLAYING);
  g_print (" * Running...\n");
  g_main_loop_run (pipe_record->loop);
}

void stopRecordPipelineChannel (PipelineChannel *pipe_record) {
  gst_element_set_state (pipe_record->pipeline, GST_STATE_NULL);
  g_print (" * Stoped...\n");
}

static gboolean buscallbackPipelineChannel (GstBus *bus, GstMessage *msg, gpointer data) {
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_print (" >  End of stream\n");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar  *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      if (AC_DEBUG_AC)
        g_print(" >  Error in bus. Debug: %s\n", debug);
      g_printerr (" >  Error (%i): %s\n", error->code, error->message);
      g_free (debug);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static void on_recordrtspsrc_pad_added (GstElement *element, GstPad *pad, gpointer data){
  GstPad *sinkpad;
  sinkpad = gst_element_get_static_pad ( (GstElement *)data, "sink");
  gst_pad_link (pad, sinkpad);
  g_print (" >>  SOURCE %s to SINK %s. Linked=%i/%i\n.", GST_PAD_NAME(pad),GST_PAD_NAME(sinkpad),gst_pad_is_linked (pad), gst_pad_is_linked(sinkpad));
  gst_object_unref (sinkpad);
}



/*  ---- Channel ----  */

typedef struct {
  // Common settings
  gchar *id;
  gchar *name;
  gchar *connect_stream1;
  gchar *connect_stream2;
  gchar *type_stream1;
  gchar *type_stream2;
  gchar *login;
  gchar *password;
  gchar *archive_relpath;
  boolean isRecordOn;

  // Pipeline
  PipelineChannel *pipeline;

  // Thread
  GThread *thread;
} CloudChannel;

guint initChannel (CloudChannel * channel, gchar *id);
void doneChannel (CloudChannel *);

/*  ---- -------- ----  */

guint initChannel (CloudChannel *channel, gchar *id) {
  GError *error;
  gchar *pgString = NULL;
  gchar *pgQuery = NULL;
  PGresult *pgServerResult = NULL;

  if (channel == NULL) {
    g_printerr (" >  Error: channel is null.\n");
    return 1;
  }

  if (!_ac_isNum(id, error)) {
    g_printerr (" >  Error: ID of channel is not a number.\n");
    if (AC_DEBUG_AC)
      g_print (" >  Error channel initialization: id='%s' is not a number!\n", id);
    g_error_free (error);
    return 1;
  }

  if (!ac_globalsettings.isInitialize) {
    g_printerr (" >  Error: global settings is not initialized!\n");
    return 2;
  }

  if (ac_globalsettings.pgServerConnection == NULL || (PQstatus (ac_globalsettings.pgServerConnection)) != CONNECTION_OK) {
    g_printerr (" >  Error: no connection with database!\n");
    return 3;
  }  

  pgQuery = g_strconcat ("SELECT name, connect_stream1, connect_stream2, connect_type1, connect_type2, connect_login, connect_password, archive_rel_dir, isrecordon FROM t_Channels WHERE id=", 
    id, NULL);
  pgServerResult = PQexec (ac_globalsettings.pgServerConnection, pgQuery);
  if (PQresultStatus (pgServerResult) != PGRES_TUPLES_OK) {
    g_printerr (" >  Error: no channel settings in database.\n");
    if (AC_DEBUG_AC) 
      g_print (" >  SELECT failed: %s. Query string: %s\n", PQerrorMessage (ac_globalsettings.pgServerConnection), pgQuery);
    g_free (pgQuery);
    PQclear (pgServerResult);
    return 4;
  }
  g_free (pgQuery);

  doneChannel (channel);

  channel->id = g_strdup (id);

  if ((pgString = PQgetvalue (pgServerResult, 0, 0)) != NULL)
    channel->name = g_strdup (pgString);
  else
    channel->name = g_strdup (AC_CHANNEL_DEFAULT_NAME);
  if ((pgString = PQgetvalue (pgServerResult, 0, 1)) != NULL)
    channel->connect_stream1 = g_strdup (pgString);
  else
    channel->connect_stream1 = g_strdup (AC_CHANNEL_DEFAULT_STREAM1);
  if ((pgString = PQgetvalue(pgServerResult, 0, 2)) != NULL)
    channel->connect_stream2 = g_strdup (pgString);
  else
    channel->connect_stream2 = g_strdup (AC_CHANNEL_DEFAULT_STREAM2);
  if ((pgString = PQgetvalue (pgServerResult, 0, 3)) != NULL)
    channel->type_stream1 = g_strdup (pgString);
  else
    channel->type_stream1 = g_strdup (AC_CHANNEL_DEFAULT_TYPE1);
  if ((pgString = PQgetvalue (pgServerResult, 0, 4)) != NULL)
    channel->type_stream2 = g_strdup (pgString);
  else
    channel->type_stream2 = g_strdup (AC_CHANNEL_DEFAULT_TYPE2);
  if ((pgString = PQgetvalue (pgServerResult, 0, 5)) != NULL)
    channel->login = g_strdup (pgString);
  else
    channel->login = g_strdup (AC_CHANNEL_DEFAULT_LOGIN);
  if ((pgString = PQgetvalue (pgServerResult, 0, 6)) != NULL)
    channel->password = g_strdup (pgString);
  else
    channel->password = g_strdup (AC_CHANNEL_DEFAULT_PASSWORD);
  if ((pgString = PQgetvalue (pgServerResult, 0, 7)) != NULL)
    channel->archive_relpath = g_strdup (pgString);
  else
    channel->archive_relpath = g_strdup (AC_CHANNEL_DEFAULT_ARCHIVE_RELPATH);
  if ((pgString = PQgetvalue (pgServerResult, 0, 8)) != NULL)
    channel->isRecordOn = (pgString[0]=='t');
  else
    channel->isRecordOn = FALSE;
  PQclear(pgServerResult);

  channel->pipeline = g_try_new (PipelineChannel, 1); // Pipeline for recording instance
  pgString = g_strconcat (ac_globalsettings.common_dirarchive, channel->archive_relpath, NULL);
  if (initPipelineChannel (channel->pipeline, channel->type_stream1, pgString)) {
    g_printerr (" >  Error: Initialization of record pipeline failed!\n");
    if (AC_DEBUG_AC)
      g_print (" >  Error creating pipeline for channel ID=%s NAME='%s' STREAM1='%s' STREAM2='%s' TYPE1=%s TYPE2=%s LOGIN='%s' PASSWORD='%s' RELPATH='%s'\n",
        channel->id, channel->name, channel->connect_stream1, channel->connect_stream2, channel->type_stream1, channel->type_stream2, channel->login,
        channel->password, channel->archive_relpath);
    doneChannel (channel);
    g_free (pgString);
    return 5;
  }
  g_free (pgString);

  channel->thread = NULL;

  return 0;
}

void doneChannel (CloudChannel *channel) {
  if (channel->pipeline != NULL) {
    donePipelineChannel (channel->pipeline);
    g_free (channel->pipeline);
    channel->pipeline = NULL;
  }
  if (channel->thread != NULL) {
    g_thread_exit();
    channel->thread = NULL;
  }
  g_free (channel->id);
  g_free (channel->name);
  g_free (channel->connect_stream1);
  g_free (channel->connect_stream2);
  g_free (channel->type_stream1);
  g_free (channel->type_stream2);
  g_free (channel->login);
  g_free (channel->password);
  g_free (channel->archive_relpath);
}


/*  ---- Cloud Engine ----  */

typedef struct {
  GData *all_channels;

} CloudEngine;

guint initEngine (CloudEngine *engine);
void doneEngine (CloudEngine *engine);
guint initEngineAllChannels (CloudEngine *engine);
guint addEngineChannelByID (CloudEngine *engine, gchar *id);
guint removeEngineChannelByID (CloudEngine *engine, gchar *id);
CloudChannel* getdirectEngineChannelByID (CloudEngine *engine, gchar *id);
guint playEngineAllChannels (CloudEngine *engine);
guint stopEngineAllChannels (CloudEngine *engine);
guint playEngineChannelByID (CloudEngine *engine, gchar *id);
guint stopEngineChannelByID (CloudEngine *engine, gchar *id);

/*  ---- ------------ ----  */

gchar* _engine_generateRecFileName () {
  gint64 curTime = g_get_real_time ();
  gchar *result = g_strdup ("/0000000000000000000.mp4");
  for (i=19; curTime>0; curTime/=10) {
    result[i--]=(gchar)((guint8)'0'+curTime%10);
  }
  return result;
}

void _engine_onDeleteChannel (gpointer data) {
  if (data != NULL)
    doneChannel ((CloudChannel*)data);
  g_free(data);
}

void _engine_eachStartChannel (GQuark key_id, gpointer data, gpointer user_data) {
  if (data != NULL) {
    ((CloudChannel*)data)->thread = g_thread_new (((CloudChannel*)data)->id, _engine_StartThread, data);
  }
}

gpointer _engine_StartThread (gpointer data) {
  if (data != NULL) {
    if (((CloudChannel*)data)->isRecordOn) {
      gchar *filename = _engine_generateRecFileName();
      playRecordPipelineChannel (((CloudChannel*)data)->pipeline, filename);
      g_free (filename);
    }
  }
}

void _engine_eachStopChannel (GQuark key_id, gpointer data, gpointer user_data) {
  if (data != NULL)
    stopRecordPipelineChannel (((CloudChannel*)data)->pipeline);
}

guint initEngine (CloudEngine *engine) {
  g_datalist_init (&(engine->all_channels));

  return initEngineAllChannels (engine);
}

void doneEngine (CloudEngine *engine) {
  g_datalist_clear(&(engine->all_channels));

  //
}

guint initEngineAllChannels (CloudEngine *engine) {
  gchar *pgQuery = NULL;
  PGresult *pgServerResult = NULL;
  guint i;

  if (engine==NULL) {
    g_printerr (" >  Error: Engine instance is null.\n");
    return 41;
  }

  if (!ac_globalsettings.isInitialize) {
    g_printerr (" >  Error: global settings is not initialized!\n");
    return 42;
  }

  if (ac_globalsettings.pgServerConnection == NULL || (PQstatus (ac_globalsettings.pgServerConnection)) != CONNECTION_OK) {
    g_printerr (" >  Error: no connection with database!\n");
    return 43;
  }  

  pgQuery = g_strconcat ("SELECT id from t_Channels WHERE archive_server_id=", ac_globalsettings.common_serverid, NULL);
  pgServerResult = PQexec (ac_globalsettings.pgServerConnection, pgQuery);
  if (PQresultStatus (pgServerResult) != PGRES_TUPLES_OK) {
    g_printerr (" >  Error: cannot get channels data from database.\n");
    if (AC_DEBUG_AC) 
      g_print (" >  SELECT failed: %s. Query string: '%s'\n", PQerrorMessage (ac_globalsettings.pgServerConnection), pgQuery);
    g_free (pgQuery);
    PQclear (pgServerResult);
    return 44;
  }
  g_free (pgQuery);

  for (i = 0; i < PQntuples(res); i++) {
    if (addEngineChannelByID (engine, PQgetvalue (pgServerResult, i, 0))) {
      g_datalist_clear(&(engine->all_channels));
      PQclear (pgServerResult);
      return 45;
    }
  }
  PQclear (pgServerResult);

  return 0;
}

guint addEngineChannelByID (CloudEngine *engine, gchar *id) {
  if (engine == NULL || !_ac_isNum(id))
    return 61;
  CloudChannel *newchannel = newchannel = g_try_new (CloudChannel, 1);
  g_print ("* Initializing channel ID=%s\n", id);
  if (initChannel (newchannel, id))
    return 62;
  g_datalist_set_data_full (&(engine->all_channels), id, newchannel, _engine_onDeleteChannel);
  return 0;
}

guint removeEngineChannelByID (CloudEngine *engine, gchar *id) {
  if (engine == NULL || !_ac_isNum(id))
    return 71;
  g_print ("* Canceling channel ID=%s\n", id);
  g_datalist_remove_data (&(engine->all_channels), id);
  return 0;
}

CloudChannel* getdirectEngineChannelByID (CloudEngine *engine, gchar *id) {
  if (engine == NULL || !_ac_isNum(id))
    return NULL;
  return ((CloudChannel*) g_datalist_get_data (&(engine->all_channels), id));
}

guint playEngineAllChannels (CloudEngine *engine) {
  if (engine == NULL)
    return 81;
  g_datalist_foreach (&(engine->all_channels), _engine_eachStartChannel, NULL);
  return 0;
}

guint stopEngineAllChannels (CloudEngine *engine) {
  if (engine == NULL)
    return 86;
  g_datalist_foreach (&(engine->all_channels), _engine_eachStopChannel, NULL);
  return 0;
}

guint playEngineChannelByID (CloudEngine *engine, gchar *id) {
  if (engine == NULL || !_ac_isNum(id))
    return 91;
  CloudChannel* curChannel = getdirectEngineChannelByID (engine, id);
  curChannel->thread = g_thread_new (id, _engine_StartThread, curChannel);
}

guint stopEngineChannelByID (CloudEngine *engine, gchar *id) {
  if (engine == NULL || !_ac_isNum(id))
    return 96;
  stopRecordPipelineChannel((getdirectEngineChannelByID(engine, id))->pipeline);
}

/*  ---- -------- ----  */


/*******************************/



gchar* _ac_itoa (guint number) {
  gchar* buffer = NULL;
  buffer=g_try_new0(gchar,10);
  if (buffer==NULL)
    return NULL;

  for (int i=0; number>0; number/=10) {
    buffer[i++]=(gchar)((guint)'0'+number%10);
  }
  return g_strreverse (buffer);
}

gboolean _ac_isNum(gchar *id, GError *error) {
  return g_ascii_string_to_unsigned (id, 10, 1, G_MAXUINT64, NULL, &error);

  //
}


/* ********** ------ MAIN FUNCTION ------ ********** */ 
int main (int argc, char *argv[])
{

  /* Initialization */
  //PipelineChannel *pipe_record = g_new(PipelineChannel, 1); // Pipeline for recording instance
  CloudEngine *mainengine;
  
  gst_init (&argc, &argv);
  g_thread_init(NULL);
  g_print (" * Initializing...\n");

  // Initialize Global Settings
  if (initGlobalSettings()) {
    g_printerr(" >  Error: Initialization of global settings failed!\n");
    return -1;
  }

  if (AC_DEBUG_AC)
    printGlobalSettings();

  // Create and initialize Engine
  mainengine = g_try_new (CloudEngine, 1);
  if (initEngine(mainengine)) {
    g_printerr(" >  Error: Initialization of main engine failed!\n");
    return -1;
  };

  /*if (initPipelineChannel(pipe_record)) {
    g_printerr(" >  Error: Initialization of record pipeline failed!\n");
    return -1;
  };*/

  /* Set the pipeline to "playing" state */
  g_print(" * Set state PLAYING! (record)\n");
  playRecordPipelineChannel(pipe_record);

  /* Out of the main loop */
  g_print (" * Returned...\n");
  stopRecordPipelineChannel(pipe_record);

  
  /* Done */
  donePipelineChannel(pipe_record);
  doneGlobalSettings();
  g_free (pipe_record);

  return 0;
}
