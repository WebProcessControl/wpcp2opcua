#include "main.h"
#include <stdlib.h>
#include <opcua_trace.h>
#include <wpcp_lws.h>

static OpcUa_Handle g_callTable;
static OpcUa_ProxyStubConfiguration g_proxyStubConfiguration;
static OpcUa_Boolean arg_opcua_trace = OpcUa_False;
static const char* arg_opcua_url = NULL;
static const char* arg_opcua_uri = "";

static const char* handle_argument(const char* key, const char* value)
{
  if (!strcmp(key, "opcua.trace")) {
    arg_opcua_trace = OpcUa_True;
    if (value)
      return "value sepcified";
    return NULL;
  }

  if (!strcmp(key, "opcua.url")) {
    if (!value)
      return "no value sepcified";
    arg_opcua_url = value;
    return NULL;
  }

  if (!strcmp(key, "opcua.uri")) {
    if (!value)
      return "no value sepcified";
    arg_opcua_uri = value;
    return NULL;
  }

  return "unknown option";
}

static void start(void)
{
  OpcUa_StatusCode statusCode;

  if (!arg_opcua_url) {
    printf("No server url given\n");
    exit(1);
  }

  g_proxyStubConfiguration.bProxyStub_Trace_Enabled = arg_opcua_trace;

#if OPCUA_USE_STATIC_PLATFORM_INTERFACE
  statusCode = OpcUa_P_Initialize();
  statusCode = OpcUa_ProxyStub_Initialize(&g_proxyStubConfiguration);
#else
  statusCode = OpcUa_P_Initialize(&g_callTable);
  statusCode = OpcUa_ProxyStub_Initialize(g_callTable, &g_proxyStubConfiguration);
#endif

  statusCode = initializeOpcUa(arg_opcua_url, arg_opcua_uri);
}

static void stop(void)
{
  OpcUa_StatusCode statusCode;
  statusCode = clearOpcUa();

  OpcUa_ProxyStub_Clear();
#if OPCUA_USE_STATIC_PLATFORM_INTERFACE
  statusCode = OpcUa_P_Clean();
#else
  statusCode = OpcUa_P_Clean(&g_callTable);
#endif
}

static void init(struct wpcp_lws_t* lws)
{
  lws->wpcp->read_data.ex_cb = read_data;
  lws->wpcp->write_data.ex_cb = write_data;
  lws->wpcp->read_history_data.ex_cb = read_history_data;
  lws->wpcp->read_history_alarm.ex_cb = read_history_alarm;
  lws->wpcp->browse.ex_cb = browse;
  lws->wpcp->handle_alarm.ex_cb = handle_alarm;
  lws->wpcp->subscribe_data.ex_cb = subscribe_data;
  lws->wpcp->subscribe_alarm.ex_cb = subscribe_alarm;
  lws->wpcp->unsubscribe.ex_cb = unsubscribe;
  lws->wpcp->republish.cb = republish;

  lws->handle_argument = handle_argument;
  lws->start = start;
  lws->stop = stop;

//  g_proxyStubConfiguration.uProxyStub_Trace_Output = OPCUA_TRACE_OUTPUT_CONSOLE;
  g_proxyStubConfiguration.uProxyStub_Trace_Level = OPCUA_TRACE_OUTPUT_LEVEL_DEBUG;

  g_proxyStubConfiguration.iSerializer_MaxAlloc = -1;
  g_proxyStubConfiguration.iSerializer_MaxStringLength = -1;
  g_proxyStubConfiguration.iSerializer_MaxByteStringLength = -1;
  g_proxyStubConfiguration.iSerializer_MaxArrayLength = -1;
  g_proxyStubConfiguration.iSerializer_MaxMessageSize = -1;
  g_proxyStubConfiguration.bSecureListener_ThreadPool_Enabled = OpcUa_False;
  g_proxyStubConfiguration.iSecureListener_ThreadPool_MinThreads = -1;
  g_proxyStubConfiguration.iSecureListener_ThreadPool_MaxThreads = -1;
  g_proxyStubConfiguration.iSecureListener_ThreadPool_MaxJobs = -1;
  g_proxyStubConfiguration.bSecureListener_ThreadPool_BlockOnAdd = OpcUa_True;
  g_proxyStubConfiguration.uSecureListener_ThreadPool_Timeout = OPCUA_INFINITE;
  g_proxyStubConfiguration.bTcpListener_ClientThreadsEnabled = OpcUa_False;
  g_proxyStubConfiguration.iTcpListener_DefaultChunkSize = -1;
  g_proxyStubConfiguration.iTcpConnection_DefaultChunkSize = -1;
  g_proxyStubConfiguration.iTcpTransport_MaxMessageLength = -1;
  g_proxyStubConfiguration.iTcpTransport_MaxChunkCount = -1;
  g_proxyStubConfiguration.bTcpStream_ExpectWriteToBlock = OpcUa_True;
}

static void cleanup(struct wpcp_lws_t* lws)
{
  (void) lws;
}

WPCP_LWS_MAIN(init, cleanup)
