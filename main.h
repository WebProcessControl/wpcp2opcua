#include <opcua_platformdefs.h>
#include <opcua_config.h>
#include <opcua_crypto.h>
#include <opcua_pki.h>
#include <opcua_p_interface.h>
#include <opcua_proxystub.h>
#include <opcua_clientapi.h>
#include <opcua_types.h>
#include <wpcp.h>

void handle_alarm(void* user, struct wpcp_result_t* result, const struct wpcp_value_t* token, const struct wpcp_value_t* acknowledge, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count);
void browse(void* user, struct wpcp_result_t* result, const struct wpcp_value_t* id, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count);
void read_data(void* user, struct wpcp_result_t* result, const struct wpcp_value_t* id, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count);
void write_data(void* user, struct wpcp_result_t* result, const struct wpcp_value_t* id, const struct wpcp_value_t* value, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count);
void read_history_data(void* user, struct wpcp_result_t* result, const struct wpcp_value_t* id, const struct wpcp_value_t* starttime, const struct wpcp_value_t* endtime, const struct wpcp_value_t* maxresults, const struct wpcp_value_t* aggregation, const struct wpcp_value_t* interval, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count);
void read_history_alarm(void* user, struct wpcp_result_t* result, const struct wpcp_value_t* id, const struct wpcp_value_t* starttime, const struct wpcp_value_t* endtime, const struct wpcp_value_t* maxresults, const struct wpcp_value_t* filter, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count);
void subscribe_data(void* user, struct wpcp_result_t* result, struct wpcp_subscription_t* subscription, const struct wpcp_value_t* id, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count);
void subscribe_alarm(void* user, struct wpcp_result_t* result, struct wpcp_subscription_t* subscription, const struct wpcp_value_t* id, const struct wpcp_value_t* filter, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count);
void unsubscribe(void* user, struct wpcp_result_t* result, struct wpcp_subscription_t* subscription, void** context, uint32_t remaining);
void republish(void* user, struct wpcp_publish_handle_t* publish_handle, struct wpcp_subscription_t* subscription);

OpcUa_StatusCode toDateTime(const struct wpcp_value_t* id, OpcUa_DateTime* dateTime);
OpcUa_StatusCode toNodeId(const struct wpcp_value_t* id, OpcUa_NodeId* nodeId);
OpcUa_StatusCode toVariant(const struct wpcp_value_t* value, OpcUa_Variant* variant);
double toWpcpTime(const OpcUa_DateTime* timestamp, OpcUa_UInt16 picoseconds);
bool toWpcpId(const OpcUa_NodeId* nodeid, struct wpcp_value_t* value, char* buffer, uint32_t buffer_length);
bool toWpcpValue(const OpcUa_Variant* variant, struct wpcp_value_t* value);
bool toWpcpValue2(const OpcUa_Variant* variant, struct wpcp_value_t* value, char* buffer, uint32_t buffer_length);

OpcUa_Channel setupRequestHeader(OpcUa_RequestHeader* requestHeader);
OpcUa_StatusCode initializeOpcUa(const OpcUa_CharA* url, const OpcUa_CharA* uri);
OpcUa_StatusCode clearOpcUa(void);

void variant2string(const struct wpcp_value_t* value, char* buffer, size_t size);


void opcua_publishDataChangeNotification(OpcUa_UInt32 subscriptionId, OpcUa_Int32 noOfMonitoredItems, const OpcUa_MonitoredItemNotification* monitoredItems);
void opcua_publishEventNotificationList(OpcUa_UInt32 subscriptionId, OpcUa_Int32 noOfEvents, const OpcUa_EventFieldList* events);
