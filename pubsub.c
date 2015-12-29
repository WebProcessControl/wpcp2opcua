#include "main.h"
#include <opcua_string.h>
#include <wpcp_lws.h>
#include <assert.h>
#include <stdlib.h>

extern OpcUa_UInt32 g_subscriptionId;

enum subscription_type_t {
  SUBSCRIPTION_TYPE_STATE_DATA,
  SUBSCRIPTION_TYPE_FILTER_ALARM
};

struct subscription_entry_t {
  enum subscription_type_t type;
  struct wpcp_publish_handle_t* publish_handle;
  bool receivedInitalValue;
  size_t count;
  OpcUa_NodeId nodeId;
  OpcUa_UInt32 subscriptionId;
  OpcUa_UInt32 monitoredItemId;
  struct wpcp_publish_handle_t* republish_publish_handle;
};

struct subscription_entry_t g_subs[4096];
size_t g_subss;


static const char* bns[] = { "EventId", "EventType", "Message", "SourceNode", "Time", "ConditionId", "BranchId", "Retain", "AckedState", "Severity", "ConfirmedState", "Comment", NULL };

static OpcUa_SimpleAttributeOperand* getSelectClauses(OpcUa_Int32* noOfSelectClauses)
{
  static OpcUa_SimpleAttributeOperand selectClauses[128];
  static OpcUa_QualifiedName browsePath[128];
  static OpcUa_Int32 i = 0;

  if (!i) {
    while (bns[i]) {
      OpcUa_QualifiedName_Initialize(&browsePath[2 * i + 0]);
      OpcUa_QualifiedName_Initialize(&browsePath[2 * i + 1]);
      OpcUa_String_AttachReadOnly(&browsePath[2 * i].Name, bns[i]);
      OpcUa_String_AttachReadOnly(&browsePath[2 * i + 1].Name, "Id");
      selectClauses[i].TypeDefinitionId.Identifier.Numeric = OpcUaId_ConditionType;
      if (i == 5) {
        selectClauses[i].AttributeId = OpcUa_Attributes_NodeId;
        selectClauses[i].NoOfBrowsePath = 0;
      }
      else {
        selectClauses[i].AttributeId = OpcUa_Attributes_Value;
        selectClauses[i].NoOfBrowsePath = i == 8 ? 2 : 1;
        selectClauses[i].BrowsePath = &browsePath[2 * i];
      }

      ++i;
    }
  }

  *noOfSelectClauses = i;

  return selectClauses;
}

void opcua_publishDataChangeNotification(OpcUa_UInt32 subscriptionId, OpcUa_Int32 noOfMonitoredItems, const OpcUa_MonitoredItemNotification* monitoredItems)
{
  assert(subscriptionId == g_subscriptionId);

  wpcp_lws_lock();
  for (OpcUa_Int32 i = 0; i < noOfMonitoredItems; ++i) {
    const OpcUa_DataValue* dataValue = &monitoredItems[i].Value;
    struct subscription_entry_t* sube = g_subs + monitoredItems[i].ClientHandle;
    assert(sube->type == SUBSCRIPTION_TYPE_STATE_DATA);
    assert(sube->publish_handle);
    struct wpcp_value_t value;
    toWpcpValue(&dataValue->Value, &value);
    sube->receivedInitalValue = true;
    wpcp_publish_data(sube->publish_handle, &value, toWpcpTime(&dataValue->SourceTimestamp, dataValue->SourcePicoseconds), dataValue->StatusCode, NULL, 0);
  }
  wpcp_lws_unlock();
}

void opcua_publishEventNotificationList(OpcUa_UInt32 subscriptionId, OpcUa_Int32 noOfEvents, const OpcUa_EventFieldList* events)
{
  wpcp_lws_lock();

  for (OpcUa_Int32 i = 0; i < noOfEvents; ++i) {
    const OpcUa_Variant* eventFields = events[i].EventFields;
    struct subscription_entry_t* sube = g_subs + events[i].ClientHandle;
    assert(sube->type == SUBSCRIPTION_TYPE_FILTER_ALARM);
    assert(sube->subscriptionId == subscriptionId);
    assert(sube->publish_handle);
    struct wpcp_value_t handle;
    struct wpcp_value_t id;
    struct wpcp_value_t branchId;
    OpcUa_String nix = OPCUA_STRING_STATICINITIALIZER;
    OpcUa_String* message = eventFields[2].Datatype == OpcUaType_LocalizedText ? &eventFields[2].Value.LocalizedText->Text : &nix;
    bool retain = eventFields[7].Value.Boolean != OpcUa_False;
    bool acknowledged = eventFields[8].Value.Boolean != OpcUa_False;
    OpcUa_UInt16 severity = eventFields[9].Value.UInt16;
    bool confirmedState = eventFields[10].Value.Boolean != OpcUa_False;
    OpcUa_String* comment = eventFields[11].Datatype == OpcUaType_LocalizedText ? &eventFields[11].Value.LocalizedText->Text : &nix;

    if (eventFields[1].Datatype != OpcUaType_NodeId)
      continue;

    const OpcUa_NodeId* eventType = eventFields[1].Value.NodeId;
    if (eventType->NamespaceIndex != 0 && eventType->IdentifierType != OpcUa_IdentifierType_Numeric)
      continue;

    if (eventType->Identifier.Numeric == OpcUaId_RefreshStartEventType) {
      continue;
    }
    if (eventType->Identifier.Numeric == OpcUaId_RefreshEndEventType) {
      continue;
    }

    char idBuffer[1024];
    char branchIdBuffer[1024];

    toWpcpValue(&eventFields[0], &handle);
    toWpcpValue2(&eventFields[5], &id, idBuffer, sizeof(idBuffer));
    toWpcpValue2(&eventFields[6], &branchId, branchIdBuffer, sizeof(branchIdBuffer));

    char key[256];
    uint32_t key_length = 0;
    if (id.type == WPCP_VALUE_TYPE_TEXT_STRING && branchId.type == WPCP_VALUE_TYPE_TEXT_STRING) {
      memcpy(key, id.data.text_string, id.value.length);
      key_length += id.value.length;
      key[key_length++] = '!';
      memcpy(key + key_length, branchId.data.text_string, branchId.value.length);
      key_length += branchId.value.length;
    }


    char token[256];
    uint32_t token_length = 0;
    if (id.type == WPCP_VALUE_TYPE_TEXT_STRING) {
      memcpy(token, id.data.text_string, id.value.length);
      token_length += id.value.length;
      token[token_length++] = '!';
      variant2string(&handle, token + token_length, sizeof(token)-token_length);
      handle.type = WPCP_VALUE_TYPE_TEXT_STRING;
      handle.value.length = strlen(token);
      handle.data.text_string = token;
    }

    wpcp_publish_alarm(sube->publish_handle, key, key_length, retain, &handle, &id, toWpcpTime(&eventFields[4].Value.DateTime, 0), severity, OpcUa_String_GetRawString(message), OpcUa_String_StrSize(message), acknowledged, NULL, 0);
  }

  wpcp_lws_unlock();

#if 0
  for (int j = 0; j < noOfEvents; ++j) {
    int k = 0;
    while (bns[k]) {
      struct wpcp_value_t tmp;
      char tmpBuffer[512];
      char buffer[512];
      toWpcpValue2(&events[j].EventFields[k], &tmp, tmpBuffer, sizeof(tmpBuffer));
      variant2string(&tmp, buffer, sizeof(buffer));

      printf("%s: %s\n", bns[k], buffer);
      ++k;
    }
    printf("----\n");
  }
#endif
}

struct SubscribeStateDataHelperItem
{
  struct wpcp_subscription_t* subscription;
  OpcUa_Int32 monitoredItemCreateRequestNr;
};

struct SubscribeStateDataHelper
{
  struct wpcp_result_t* result;
  OpcUa_Int32 count;
  OpcUa_Int32 countMonitoredItems;
  struct SubscribeStateDataHelperItem* items;
  OpcUa_MonitoredItemCreateRequest* monitoredItemCreateRequests;
};


static OpcUa_StatusCode opcua_subscribe(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  struct SubscribeStateDataHelper* helper = pCallbackData;
  OpcUa_CreateMonitoredItemsResponse* pCreateMonitoredItemsResponse = pResponse;
  OpcUa_Int32 noOfResults = pCreateMonitoredItemsResponse ? pCreateMonitoredItemsResponse->NoOfResults : 0;
  OpcUa_MonitoredItemCreateResult* results = pCreateMonitoredItemsResponse ? pCreateMonitoredItemsResponse->Results : NULL;

  wpcp_lws_lock();

  for (OpcUa_Int32 i = 0; i < helper->count; ++i) {
    struct SubscribeStateDataHelperItem* item = &helper->items[i];
    struct subscription_entry_t* sube = wpcp_subscription_get_user(item->subscription);

    if (item->monitoredItemCreateRequestNr < 0) {
      struct wpcp_publish_handle_t* publish_handle = wpcp_return_subscribe_accept(helper->result, NULL, item->subscription);
      assert(sube->publish_handle == publish_handle);
    } else {
      if (item->monitoredItemCreateRequestNr < noOfResults && OpcUa_IsGood(results[item->monitoredItemCreateRequestNr].StatusCode)) {
        sube->monitoredItemId = results[item->monitoredItemCreateRequestNr].MonitoredItemId;
        sube->publish_handle = wpcp_return_subscribe_accept(helper->result, NULL, item->subscription);
      } else {
        assert(sube->publish_handle == NULL);
        wpcp_return_subscribe_reject(helper->result, NULL, item->subscription);
      }
    }
  }

  wpcp_lws_unlock();

  free(helper);

  return OpcUa_Good;
}

void subscribe_data(void* user, struct wpcp_result_t* result, struct wpcp_subscription_t* subscription, const struct wpcp_value_t* id, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count)
{
  struct SubscribeStateDataHelper* helper;
  if (*context)
    helper = (struct SubscribeStateDataHelper*) *context;
  else {
    OpcUa_UInt32 count = remaining + 1;
    OpcUa_Byte* data = malloc(sizeof(struct SubscribeStateDataHelper) + count * (sizeof(struct SubscribeStateDataHelperItem) + sizeof(OpcUa_MonitoredItemCreateRequest)));
    helper = *context = data;
    helper->result = result;
    helper->count = count;
    helper->countMonitoredItems = 0;
    helper->items = (struct SubscribeStateDataHelperItem*)(data + sizeof(struct SubscribeStateDataHelper));
    helper->monitoredItemCreateRequests = (OpcUa_MonitoredItemCreateRequest*)(data + sizeof(struct SubscribeStateDataHelper) + count * sizeof(struct SubscribeStateDataHelperItem));
  }

  OpcUa_UInt32 nr = helper->count - 1 - remaining;
  struct SubscribeStateDataHelperItem* item = &helper->items[nr];
  item->subscription = subscription;

  struct subscription_entry_t* sube = wpcp_subscription_get_user(subscription);

  if (sube) {
    sube->count += 1;
    item->monitoredItemCreateRequestNr = -1;
    assert(sube->publish_handle && "Handling for failing subscribe missing");
  }
  else {
    size_t gid = g_subss++;
    sube = &g_subs[gid];
    sube->type = SUBSCRIPTION_TYPE_STATE_DATA;
    sube->receivedInitalValue = false;
    sube->count = 1;
    sube->publish_handle = NULL;
    toNodeId(id, &sube->nodeId);
    item->monitoredItemCreateRequestNr = helper->countMonitoredItems++;

    OpcUa_MonitoredItemCreateRequest* monitoredItemCreateRequest = &helper->monitoredItemCreateRequests[item->monitoredItemCreateRequestNr];
    OpcUa_MonitoredItemCreateRequest_Initialize(monitoredItemCreateRequest);
    monitoredItemCreateRequest->ItemToMonitor.NodeId = sube->nodeId;
    monitoredItemCreateRequest->ItemToMonitor.AttributeId = OpcUa_Attributes_Value;
    monitoredItemCreateRequest->MonitoringMode = OpcUa_MonitoringMode_Reporting;
    monitoredItemCreateRequest->RequestedParameters.ClientHandle = gid;
    wpcp_subscription_set_user(subscription, sube);
  }

  if (!remaining) {
    if (helper->countMonitoredItems) {
      OpcUa_RequestHeader requestHeader;
      OpcUa_StatusCode statusCode = OpcUa_ClientApi_BeginCreateMonitoredItems(
        setupRequestHeader(&requestHeader),
        &requestHeader,
        g_subscriptionId,
        OpcUa_TimestampsToReturn_Source,
        helper->countMonitoredItems,
        helper->monitoredItemCreateRequests,
        opcua_subscribe,
        helper);
      if (!OpcUa_IsGood(statusCode))
        opcua_subscribe(OpcUa_Null, OpcUa_Null, OpcUa_Null, helper, statusCode);
    }
    else
      opcua_subscribe(NULL, NULL, NULL, helper, OpcUa_Good);
  }
}

struct SubscribeMatchAlarmHelper;

struct SubscribeMatchAlarmHelperItem
{
  struct SubscribeMatchAlarmHelper* helper;
  struct wpcp_subscription_t* subscription;
  OpcUa_Int32 monitoredItemCreateRequestNr;
  OpcUa_StatusCode monitoredItemCreateStatusCode;
  OpcUa_EventFilter* eventFilter;
};

struct SubscribeMatchAlarmHelper
{
  struct wpcp_result_t* result;
  struct SubscribeMatchAlarmHelperItem* items;
  struct wpcp_subscription_t* subscription;
  OpcUa_UInt32 subscriptionId;
  OpcUa_Int32 count;
  OpcUa_Int32 countCallbacks;
  OpcUa_Int32 countMonitoredItems;
  OpcUa_ReadValueId* readValueId;
  OpcUa_WriteValue* writeValue;
  OpcUa_MonitoredItemCreateRequest* monitoredItemCreateRequests;
};

static OpcUa_StatusCode opcua_subscribe_alarm_2(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  struct SubscribeMatchAlarmHelperItem* cbItem = pCallbackData;
  struct SubscribeMatchAlarmHelper* helper = cbItem->helper;
  OpcUa_CreateMonitoredItemsResponse* pCreateMonitoredItemsResponse = pResponse;

  if (pCreateMonitoredItemsResponse && pCreateMonitoredItemsResponse->NoOfResults == 1) {
    struct subscription_entry_t* sube = wpcp_subscription_get_user(cbItem->subscription);
    sube->monitoredItemId = pCreateMonitoredItemsResponse->Results[0].MonitoredItemId;
    cbItem->monitoredItemCreateStatusCode = pCreateMonitoredItemsResponse->Results[0].StatusCode;
  } else
    cbItem->monitoredItemCreateStatusCode = OpcUa_Bad;

  wpcp_lws_lock();

  helper->countCallbacks += 1;
  if (helper->countCallbacks == helper->count) {
    for (OpcUa_Int32 i = 0; i < helper->count; ++i) {
      struct SubscribeMatchAlarmHelperItem* item = &helper->items[i];
      struct subscription_entry_t* sube = wpcp_subscription_get_user(item->subscription);

      if (item->monitoredItemCreateRequestNr < 0) {
        struct wpcp_publish_handle_t* publish_handle = wpcp_return_subscribe_accept(helper->result, NULL, item->subscription);
        assert(sube->publish_handle == publish_handle);
      }
      else {
        if (OpcUa_IsGood(item->monitoredItemCreateStatusCode)) {
          sube->publish_handle = wpcp_return_subscribe_accept(helper->result, NULL, item->subscription);
        }
        else {
          assert(sube->publish_handle == NULL);
          wpcp_return_subscribe_reject(helper->result, NULL, item->subscription);
        }
      }
    }

    free(helper);
  }

  wpcp_lws_unlock();

  return OpcUa_Good;
}

static OpcUa_StatusCode opcua_subscribe_alarm(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  struct SubscribeMatchAlarmHelperItem* item = pCallbackData;
  struct SubscribeMatchAlarmHelper* helper = item->helper;

  if (pResponse) {
    struct subscription_entry_t* sube = wpcp_subscription_get_user(item->subscription);
    OpcUa_CreateSubscriptionResponse * pCreateSubscriptionResponse = pResponse;
    sube->subscriptionId = pCreateSubscriptionResponse->SubscriptionId;

    OpcUa_RequestHeader requestHeader;
    uStatus = OpcUa_ClientApi_BeginCreateMonitoredItems(
      setupRequestHeader(&requestHeader),
      &requestHeader,
      sube->subscriptionId,
      OpcUa_TimestampsToReturn_Source,
      1,
      helper->monitoredItemCreateRequests + item->monitoredItemCreateRequestNr,
      opcua_subscribe_alarm_2,
      item);
    if (OpcUa_IsGood(uStatus))
      return OpcUa_Good;
  }

  return opcua_subscribe_alarm_2(OpcUa_Null, OpcUa_Null, OpcUa_Null, item, uStatus);
}

void subscribe_alarm(void* user, struct wpcp_result_t* result, struct wpcp_subscription_t* subscription, const struct wpcp_value_t* id, const struct wpcp_value_t* filter, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count)
{
  struct SubscribeMatchAlarmHelper* helper;
  if (*context)
    helper = (struct SubscribeMatchAlarmHelper*) *context;
  else {
    OpcUa_UInt32 count = remaining + 1;
    OpcUa_Byte* data = malloc(sizeof(struct SubscribeMatchAlarmHelper) + count * (sizeof(struct SubscribeMatchAlarmHelperItem) + sizeof(OpcUa_MonitoredItemCreateRequest)));
    helper = *context = data;
    helper->result = result;
    helper->subscription = subscription;
    helper->count = count;
    helper->countCallbacks = 0;
    helper->countMonitoredItems = 0;
    helper->items = (struct SubscribeMatchAlarmHelperItem*)(data + sizeof(struct SubscribeMatchAlarmHelper));
    helper->monitoredItemCreateRequests = (OpcUa_MonitoredItemCreateRequest*)(data + sizeof(struct SubscribeMatchAlarmHelper) + count * sizeof(struct SubscribeMatchAlarmHelperItem));
  }

  OpcUa_UInt32 nr = helper->count - 1 - remaining;
  struct SubscribeMatchAlarmHelperItem* item = &helper->items[nr];
  item->helper = helper;
  item->subscription = subscription;

  struct subscription_entry_t* sube = wpcp_subscription_get_user(subscription);

  if (sube) {
    sube->count += 1;
    item->monitoredItemCreateRequestNr = -1;
    assert(sube->publish_handle && "Handling for failing subscribe missing");
    opcua_subscribe_alarm(OpcUa_Null, OpcUa_Null, OpcUa_Null, item, OpcUa_Good);
  } else {
    size_t gid = g_subss++;
    sube = &g_subs[gid];
    sube->type = SUBSCRIPTION_TYPE_FILTER_ALARM;
    sube->receivedInitalValue = false;
    sube->count = 1;
    sube->publish_handle = NULL;
    item->monitoredItemCreateRequestNr = helper->countMonitoredItems++;

    OpcUa_MonitoredItemCreateRequest* monitoredItemCreateRequest = &helper->monitoredItemCreateRequests[item->monitoredItemCreateRequestNr];
    OpcUa_MonitoredItemCreateRequest_Initialize(monitoredItemCreateRequest);
    monitoredItemCreateRequest->ItemToMonitor.NodeId.Identifier.Numeric = OpcUaId_Server;
    monitoredItemCreateRequest->ItemToMonitor.AttributeId = OpcUa_Attributes_EventNotifier;
    monitoredItemCreateRequest->MonitoringMode = OpcUa_MonitoringMode_Reporting;
    monitoredItemCreateRequest->RequestedParameters.ClientHandle = gid;
    OpcUa_EncodeableObject_CreateExtension(&OpcUa_EventFilter_EncodeableType, &monitoredItemCreateRequest->RequestedParameters.Filter, &item->eventFilter);
    OpcUa_EventFilter_Initialize(item->eventFilter);
    item->eventFilter->SelectClauses = getSelectClauses(&item->eventFilter->NoOfSelectClauses);
    wpcp_subscription_set_user(subscription, sube);

    OpcUa_RequestHeader requestHeader;
    OpcUa_StatusCode statusCode = OpcUa_ClientApi_BeginCreateSubscription(
      setupRequestHeader(&requestHeader),
      &requestHeader,
      0.0,
      0,
      0,
      0,
      OpcUa_True,
      0,
      opcua_subscribe_alarm,
      item);
    if (!OpcUa_IsGood(statusCode))
      opcua_subscribe_alarm(OpcUa_Null, OpcUa_Null, OpcUa_Null, item, statusCode);
  }
}

struct UnsubscribeStateDataHelperItem
{
  struct wpcp_subscription_t* subscription;
  OpcUa_Int32 deleteMonitoredItemNr;
  OpcUa_StatusCode deleteMonitoredItemStatusCode;
};

struct UnsubscribeStateDataHelper
{
  struct wpcp_result_t* result;
  OpcUa_Int32 count;
  OpcUa_Int32 countMonitoredItems;
  OpcUa_Int32 countSubscriptions;
  struct UnsubscribeStateDataHelperItem* items;
  OpcUa_UInt32* ids;
};

static OpcUa_StatusCode opcua_unsubscribe_2(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  struct UnsubscribeStateDataHelper* helper = pCallbackData;
  OpcUa_DeleteSubscriptionsResponse* pDeleteSubscriptionsResponse = pResponse;
  OpcUa_Int32 noOfResults = pDeleteSubscriptionsResponse ? pDeleteSubscriptionsResponse->NoOfResults : 0;
  OpcUa_StatusCode* results = pDeleteSubscriptionsResponse ? pDeleteSubscriptionsResponse->Results : NULL;

  wpcp_lws_lock();

  for (OpcUa_Int32 i = 0; i < helper->count; ++i) {
    struct UnsubscribeStateDataHelperItem* item = &helper->items[i];
    struct subscription_entry_t* sube = wpcp_subscription_get_user(item->subscription);

    if (!sube->count) {
      sube->publish_handle = NULL;
      OpcUa_NodeId_Clear(&sube->nodeId);
    }

    if (item->deleteMonitoredItemNr < 0)
      wpcp_return_unsubscribe(helper->result, NULL, item->subscription);
    else if (item->deleteMonitoredItemNr < helper->countMonitoredItems) {
      if (OpcUa_IsGood(item->deleteMonitoredItemStatusCode))
        wpcp_return_unsubscribe(helper->result, NULL, item->subscription);
      else {
        wpcp_return_unsubscribe(helper->result, NULL, NULL);
        assert(false);
      }
    } else {
      OpcUa_Int32 nr = helper->count - 1 - item->deleteMonitoredItemNr;
      if (nr < noOfResults && OpcUa_IsGood(results[nr]))
        wpcp_return_unsubscribe(helper->result, NULL, item->subscription);
      else {
        wpcp_return_unsubscribe(helper->result, NULL, NULL);
        assert(false);
      }
    }
  }

  wpcp_lws_unlock();

  free(helper);

  return OpcUa_Good;
}

static OpcUa_StatusCode opcua_unsubscribe(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  struct UnsubscribeStateDataHelper* helper = pCallbackData;
  OpcUa_DeleteMonitoredItemsResponse* pDeleteMonitoredItemsResponse = pResponse;
  OpcUa_Int32 noOfResults = pDeleteMonitoredItemsResponse ? pDeleteMonitoredItemsResponse->NoOfResults : 0;

  for (OpcUa_Int32 i = 0; i < noOfResults; ++i)
    helper->items[i].deleteMonitoredItemStatusCode = pDeleteMonitoredItemsResponse->Results[noOfResults - 1 - i];

  if (helper->countSubscriptions) {
    OpcUa_RequestHeader requestHeader;
    uStatus = OpcUa_ClientApi_BeginDeleteSubscriptions(
      setupRequestHeader(&requestHeader),
      &requestHeader,
      helper->countSubscriptions,
      helper->ids + helper->count - helper->countSubscriptions,
      opcua_unsubscribe_2,
      helper);
    if (OpcUa_IsGood(uStatus))
      return OpcUa_Good;
  }

  return opcua_unsubscribe_2(OpcUa_Null, OpcUa_Null, OpcUa_Null, helper, uStatus);
}

void unsubscribe(void* user, struct wpcp_result_t* result, struct wpcp_subscription_t* subscription, void** context, uint32_t remaining)
{
  struct UnsubscribeStateDataHelper* helper;
  if (*context)
    helper = (struct UnsubscribeStateDataHelper*) *context;
  else {
    OpcUa_UInt32 count = remaining + 1;
    OpcUa_Byte* data = malloc(sizeof(struct UnsubscribeStateDataHelper) + count * (sizeof(struct UnsubscribeStateDataHelperItem) + sizeof(OpcUa_UInt32)));
    helper = *context = data;
    helper->result = result;
    helper->count = count;
    helper->countMonitoredItems = 0;
    helper->countSubscriptions = 0;
    helper->items = (struct UnsubscribeStateDataHelperItem*)(data + sizeof(struct UnsubscribeStateDataHelper));
    helper->ids = (OpcUa_UInt32*)(data + sizeof(struct UnsubscribeStateDataHelper) + count * sizeof(struct UnsubscribeStateDataHelperItem));
  }

  OpcUa_UInt32 nr = helper->count - 1 - remaining;
  struct UnsubscribeStateDataHelperItem* item = &helper->items[nr];
  item->subscription = subscription;
  item->deleteMonitoredItemStatusCode = OpcUa_Bad;

  struct subscription_entry_t* sube = wpcp_subscription_get_user(subscription);

  sube->count -= 1;

  if (sube->count) {
    item->deleteMonitoredItemNr = -1;
  } else if (sube->type == SUBSCRIPTION_TYPE_STATE_DATA) {
    item->deleteMonitoredItemNr = helper->countMonitoredItems++;
    helper->ids[item->deleteMonitoredItemNr] = sube->monitoredItemId;
  } else if (sube->type == SUBSCRIPTION_TYPE_FILTER_ALARM) {
    item->deleteMonitoredItemNr = helper->count - ++(helper->countSubscriptions);
    helper->ids[item->deleteMonitoredItemNr] = sube->subscriptionId;
  } else
    assert(false);

  if (!remaining) {
    if (helper->countMonitoredItems) {
      OpcUa_RequestHeader requestHeader;
      OpcUa_StatusCode statusCode = OpcUa_ClientApi_BeginDeleteMonitoredItems(
        setupRequestHeader(&requestHeader),
        &requestHeader,
        g_subscriptionId,
        helper->countMonitoredItems,
        helper->ids,
        opcua_unsubscribe,
        helper);
      if (!OpcUa_IsGood(statusCode))
        opcua_unsubscribe(OpcUa_Null, OpcUa_Null, OpcUa_Null, helper, statusCode);
    }
    else
      opcua_unsubscribe(NULL, NULL, NULL, helper, OpcUa_Good);
  }
}

static OpcUa_StatusCode opcua_republish_state_data(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  struct wpcp_publish_handle_t* publish_handle = pCallbackData;
  OpcUa_ReadResponse* pReadResponse = pResponse;
  OpcUa_Int32 noOfResults = pReadResponse->NoOfResults;
  OpcUa_DataValue* results = pReadResponse->Results;

  assert(noOfResults == 1);

  wpcp_lws_lock();

  struct wpcp_value_t value;
  toWpcpValue(&results->Value, &value);
  wpcp_publish_data(publish_handle, &value, toWpcpTime(&results->SourceTimestamp, results->SourcePicoseconds), results->StatusCode, NULL, 0);

  wpcp_return_republish(publish_handle);

  wpcp_lws_unlock();

  return OpcUa_Good;
}

static OpcUa_StatusCode opcua_republish_filter_alarm(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  OpcUa_CallResponse* pCallResponse = pResponse;
  assert(pCallResponse->NoOfResults == 1);
  assert(OpcUa_IsGood(pCallResponse->Results[0].StatusCode));
  return OpcUa_Good;
}

void republish(void* user, struct wpcp_publish_handle_t* publish_handle, struct wpcp_subscription_t* subscription)
{
  struct subscription_entry_t* sube = wpcp_subscription_get_user(subscription);

  if (sube->type == SUBSCRIPTION_TYPE_STATE_DATA) {
    if (sube->receivedInitalValue) {
      OpcUa_ReadValueId readValueId;
      OpcUa_ReadValueId_Initialize(&readValueId);
      readValueId.NodeId = sube->nodeId;
      readValueId.AttributeId = OpcUa_Attributes_Value;

      OpcUa_RequestHeader requestHeader;
      OpcUa_StatusCode statusCode = OpcUa_ClientApi_BeginRead(
        setupRequestHeader(&requestHeader),
        &requestHeader,
        0.0,
        OpcUa_TimestampsToReturn_Neither,
        1,
        &readValueId,
        opcua_republish_state_data,
        publish_handle);
      if (!OpcUa_IsGood(statusCode))
        opcua_republish_state_data(OpcUa_Null, OpcUa_Null, OpcUa_Null, publish_handle, statusCode);
    }
  } else if (sube->type == SUBSCRIPTION_TYPE_FILTER_ALARM) {
    sube->republish_publish_handle = publish_handle;

    OpcUa_CallMethodRequest callMethodRequest;
    OpcUa_CallMethodRequest_Initialize(&callMethodRequest);
    callMethodRequest.ObjectId.Identifier.Numeric = OpcUaId_ConditionType;
    callMethodRequest.MethodId.Identifier.Numeric = OpcUaId_ConditionType_ConditionRefresh;
    callMethodRequest.NoOfInputArguments = 1;

    OpcUa_Variant var;
    OpcUa_Variant_Initialize(&var);
    var.Datatype = OpcUaType_UInt32;
    var.Value.UInt32 = sube->subscriptionId;
    callMethodRequest.InputArguments = &var;

    OpcUa_RequestHeader requestHeader;
    OpcUa_StatusCode statusCode = OpcUa_ClientApi_BeginCall(
      setupRequestHeader(&requestHeader),
      &requestHeader,
      1,
      &callMethodRequest,
      opcua_republish_filter_alarm,
      publish_handle);
    if (!OpcUa_IsGood(statusCode))
      opcua_republish_filter_alarm(OpcUa_Null, OpcUa_Null, OpcUa_Null, publish_handle, statusCode);
  } else
    assert(false);
}


static OpcUa_StatusCode opcua_handle_alarm(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  struct wpcp_result_t* result = pCallbackData;
  OpcUa_CallResponse* pCallResponse = pResponse;
  assert(pCallResponse->NoOfResults == 1);

  wpcp_lws_lock();
  wpcp_return_handle_alarm(result, NULL, OpcUa_IsGood(pCallResponse->Results[0].StatusCode));
  wpcp_lws_unlock();

  return OpcUa_Good;
}

void handle_alarm(void* user, struct wpcp_result_t* result, const struct wpcp_value_t* token, const struct wpcp_value_t* acknowledge, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count)
{
  OpcUa_CallMethodRequest callMethodRequest;
  OpcUa_CallMethodRequest_Initialize(&callMethodRequest);

  OpcUa_Variant var[2];
  OpcUa_Variant_Initialize(&var[0]);
  OpcUa_Variant_Initialize(&var[1]);


  OpcUa_Byte buffer[128];

  if (token) {
    for (size_t i = token->value.length; i > 0; --i) {
      if (token->data.text_string[i] == '!') {
        struct wpcp_value_t tmp = *token;
        tmp.value.length = i;
        toNodeId(&tmp, &callMethodRequest.ObjectId);

        var[0].Datatype = OpcUaType_ByteString;
        var[0].Value.ByteString.Length = (token->value.length - i - 1) / 2;
        var[0].Value.ByteString.Data = buffer;

        for (OpcUa_Int32 j = 0; j < var[0].Value.ByteString.Length; ++j) {
          const char* hex = token->data.text_string + i + 1 + 2 * j;
          var[0].Value.ByteString.Data[j] =
            (('0' <= hex[0] && hex[0] <= '9') ? (hex[0] - '0') : (10 + hex[0] - 'a')) << 4 |
            (('0' <= hex[1] && hex[1] <= '9') ? (hex[1] - '0') : (10 + hex[1] - 'a')) << 0;
        }

        break;
      }
    }
  }

  if (acknowledge && acknowledge->type == WPCP_VALUE_TYPE_TRUE) {
    callMethodRequest.MethodId.Identifier.Numeric = OpcUaId_AcknowledgeableConditionType_Acknowledge;
    callMethodRequest.NoOfInputArguments = 2;
  }

  OpcUa_LocalizedText lt;
  OpcUa_LocalizedText_Initialize(&lt);
  var[1].Datatype = OpcUaType_LocalizedText;
  var[1].Value.LocalizedText = &lt;
  callMethodRequest.InputArguments = var;

  OpcUa_RequestHeader requestHeader;
  OpcUa_StatusCode statusCode = OpcUa_ClientApi_BeginCall(
    setupRequestHeader(&requestHeader),
    &requestHeader,
    1,
    &callMethodRequest,
    opcua_handle_alarm,
    result);
  if (!OpcUa_IsGood(statusCode))
    opcua_handle_alarm(OpcUa_Null, OpcUa_Null, OpcUa_Null, result, statusCode);
}
