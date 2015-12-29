#include "main.h"
#include <opcua_string.h>
#include <assert.h>
#include <stdlib.h>

struct BrowseHelper
{
  struct wpcp_result_t* result;
  OpcUa_Int32 count;
  OpcUa_BrowseDescription browseDescription[1];
};

static OpcUa_StatusCode opcua_browse(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  struct BrowseHelper* helper = pCallbackData;
  OpcUa_BrowseResponse* pBrowseResponse = pResponse;
  OpcUa_Int32 noOfResults = pBrowseResponse->NoOfResults;
  OpcUa_BrowseResult* results = pBrowseResponse->Results;

  for (OpcUa_Int32 i = 0; i < helper->count; ++i) {
    if (i < noOfResults) {
      OpcUa_Int32 noOfReferences = results[i].NoOfReferences;
      wpcp_return_browse(helper->result, NULL, noOfReferences);

      for (OpcUa_Int32 j = 0; j < noOfReferences; ++j) {
        const OpcUa_ReferenceDescription* referenceDescription = &results[i].References[j];
        const OpcUa_String* browseName = &referenceDescription->BrowseName.Name;
        const OpcUa_String* displayName = &referenceDescription->DisplayName.Text;
        char idBuffer[1024];
        char typeBuffer[1024];
        struct wpcp_value_t id;
        struct wpcp_value_t type;

        toWpcpId(&referenceDescription->NodeId.NodeId, &id, idBuffer, sizeof(idBuffer));
        toWpcpId(&referenceDescription->TypeDefinition.NodeId, &type, typeBuffer, sizeof(typeBuffer));
        wpcp_return_browse_item(helper->result, &id, OpcUa_String_GetRawString(browseName), OpcUa_String_StrSize(browseName), OpcUa_String_GetRawString(displayName), OpcUa_String_StrSize(displayName), NULL, 0, &type, NULL, 0);
      }
    } else
      wpcp_return_browse(helper->result, NULL, 0);

    OpcUa_BrowseDescription_Clear(&helper->browseDescription[i]);
  }

  free(helper);

  return OpcUa_Good;
}

void browse(void* user, struct wpcp_result_t* result, const struct wpcp_value_t* id, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count)
{
  struct BrowseHelper* helper;
  if (*context)
    helper = (struct BrowseHelper*) *context;
  else {
    *context = helper = malloc(sizeof(struct BrowseHelper) + sizeof(OpcUa_BrowseDescription)* remaining);
    helper->result = result;
    helper->count = remaining + 1;
  }

  OpcUa_ViewDescription viewDescription;
  OpcUa_ViewDescription_Initialize(&viewDescription);

  OpcUa_BrowseDescription* browseDescription = &helper->browseDescription[helper->count - 1 - remaining];
  OpcUa_BrowseDescription_Initialize(browseDescription);
  toNodeId(id, &browseDescription->NodeId);
  if (!id->value.length)
    browseDescription->NodeId.Identifier.Numeric = OpcUaId_ObjectsFolder;
  browseDescription->BrowseDirection = OpcUa_BrowseDirection_Forward;
  browseDescription->IncludeSubtypes = OpcUa_True;
  browseDescription->ReferenceTypeId.Identifier.Numeric = OpcUaId_HierarchicalReferences;
  browseDescription->ResultMask = OpcUa_BrowseResultMask_All;

  if (!remaining) {
    OpcUa_RequestHeader requestHeader;
    OpcUa_StatusCode statusCode = OpcUa_ClientApi_BeginBrowse(
      setupRequestHeader(&requestHeader),
      &requestHeader,
      &viewDescription,
      0,
      helper->count,
      helper->browseDescription,
      opcua_browse,
      helper);
    if (!OpcUa_IsGood(statusCode))
      opcua_browse(OpcUa_Null, OpcUa_Null, OpcUa_Null, helper, statusCode);
  }
}

struct ReadDataHelper
{
  struct wpcp_result_t* result;
  OpcUa_Int32 count;
  OpcUa_ReadValueId readValueId[1];
};

static OpcUa_StatusCode opcua_read(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  struct ReadDataHelper* readHelper = pCallbackData;
  OpcUa_ReadResponse* pReadResponse = pResponse;
  OpcUa_Int32 noOfResults = pReadResponse->NoOfResults;
  OpcUa_DataValue* results = pReadResponse->Results;

  for (OpcUa_Int32 i = 0; i < readHelper->count; ++i) {
    struct wpcp_value_t value;

    if (i < noOfResults) {
      toWpcpValue(&results[i].Value, &value);
      wpcp_return_read_data(readHelper->result, NULL, &value, toWpcpTime(&results[i].SourceTimestamp, results[i].SourcePicoseconds), results[i].StatusCode, NULL, 0);
    }
    else {
      value.type = WPCP_VALUE_TYPE_UNDEFINED;
      wpcp_return_read_data(readHelper->result, NULL, &value, 0.0, OpcUa_BadInternalError, NULL, 0);
    }

    OpcUa_ReadValueId_Clear(&readHelper->readValueId[i]);
  }

  free(readHelper);

  return OpcUa_Good;
}

void read_data(void* user, struct wpcp_result_t* result, const struct wpcp_value_t* id, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count)
{
  struct ReadDataHelper* helper;
  if (*context)
    helper = (struct ReadDataHelper*) *context;
  else {
    *context = helper = malloc(sizeof(struct ReadDataHelper) + sizeof(OpcUa_ReadValueId)* remaining);
    helper->result = result;
    helper->count = remaining + 1;
  }

  OpcUa_ReadValueId* readValueId = &helper->readValueId[helper->count - 1 - remaining];
  OpcUa_ReadValueId_Initialize(readValueId);
  toNodeId(id, &readValueId->NodeId);
  readValueId->AttributeId = OpcUa_Attributes_Value;

  if (!remaining) {
    OpcUa_RequestHeader requestHeader;
    OpcUa_StatusCode statusCode = OpcUa_ClientApi_BeginRead(
      setupRequestHeader(&requestHeader),
      &requestHeader,
      0.0, // to force the server to read a new value from the DataSource
      OpcUa_TimestampsToReturn_Source,
      helper->count,
      helper->readValueId,
      opcua_read,
      helper);
    if (!OpcUa_IsGood(statusCode))
      opcua_read(OpcUa_Null, OpcUa_Null, OpcUa_Null, helper, statusCode);
  }
}

struct WriteDataHelper
{
  struct wpcp_result_t* result;
  OpcUa_Int32 count;
  OpcUa_ReadValueId* readValueId;
  OpcUa_WriteValue* writeValue;
};

static OpcUa_StatusCode opcua_write_write(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  struct WriteDataHelper* helper = pCallbackData;
  OpcUa_WriteResponse* pWriteResponse = pResponse;
  OpcUa_Int32 noOfResults = pWriteResponse->NoOfResults;
  OpcUa_StatusCode* results = pWriteResponse->Results;

  for (OpcUa_Int32 i = 0; i < helper->count; ++i) {
    wpcp_return_write_data(helper->result, NULL, i < noOfResults && OpcUa_IsGood(results[i]));
    OpcUa_WriteValue_Clear(&helper->writeValue[i]);
  }

  free(helper);

  return OpcUa_Good;
}

static bool covertVariant(OpcUa_Variant* variant, const OpcUa_NodeId* nodeId)
{
  if (nodeId->NamespaceIndex != 0 || nodeId->IdentifierType != OpcUa_IdentifierType_Numeric)
    return false;
  if (variant->Datatype == nodeId->Identifier.Numeric)
    return true;

  if (nodeId->Identifier.Numeric == OpcUaType_SByte) {
    if (variant->Datatype == OpcUaType_UInt64 && variant->Value.UInt64 <= OpcUa_SByte_Max) {
      variant->Datatype = OpcUaType_SByte;
      variant->Value.SByte = variant->Value.UInt64;
      return true;
    }
    if (variant->Datatype == OpcUaType_Int64 && OpcUa_SByte_Min <= variant->Value.Int64 && variant->Value.Int64 <= OpcUa_SByte_Max) {
      variant->Datatype = OpcUaType_SByte;
      variant->Value.SByte = variant->Value.Int64;
      return true;
    }
  }

  if (nodeId->Identifier.Numeric == OpcUaType_Byte) {
    if (variant->Datatype == OpcUaType_UInt64 && variant->Value.UInt64 <= OpcUa_Byte_Max) {
      variant->Datatype = OpcUaType_Byte;
      variant->Value.Byte = variant->Value.UInt64;
      return true;
    }
    if (variant->Datatype == OpcUaType_Int64 && OpcUa_Byte_Min <= variant->Value.Int64 && variant->Value.Int64 <= OpcUa_Byte_Max) {
      variant->Datatype = OpcUaType_Byte;
      variant->Value.Byte = variant->Value.Int64;
      return true;
    }
  }

  if (nodeId->Identifier.Numeric == OpcUaType_Int16) {
    if (variant->Datatype == OpcUaType_UInt64 && variant->Value.UInt64 <= OpcUa_Int16_Max) {
      variant->Datatype = OpcUaType_Int16;
      variant->Value.Int16 = variant->Value.UInt64;
      return true;
    }
    if (variant->Datatype == OpcUaType_Int64 && OpcUa_Int16_Min <= variant->Value.Int64 && variant->Value.Int64 <= OpcUa_Int16_Max) {
      variant->Datatype = OpcUaType_Int16;
      variant->Value.Int16 = variant->Value.Int64;
      return true;
    }
  }

  if (nodeId->Identifier.Numeric == OpcUaType_UInt16) {
    if (variant->Datatype == OpcUaType_UInt64 && variant->Value.UInt64 <= OpcUa_UInt16_Max) {
      variant->Datatype = OpcUaType_UInt16;
      variant->Value.UInt16 = variant->Value.UInt64;
      return true;
    }
    if (variant->Datatype == OpcUaType_Int64 && OpcUa_UInt16_Min <= variant->Value.Int64 && variant->Value.Int64 <= OpcUa_UInt16_Max) {
      variant->Datatype = OpcUaType_UInt16;
      variant->Value.UInt16 = variant->Value.Int64;
      return true;
    }
  }

  if (nodeId->Identifier.Numeric == OpcUaType_Int32) {
    if (variant->Datatype == OpcUaType_UInt64 && variant->Value.UInt64 <= OpcUa_Int32_Max) {
      variant->Datatype = OpcUaType_Int32;
      variant->Value.Int32 = variant->Value.UInt64;
      return true;
    }
    if (variant->Datatype == OpcUaType_Int64 && OpcUa_Int32_Min <= variant->Value.Int64 && variant->Value.Int64 <= OpcUa_Int32_Max) {
      variant->Datatype = OpcUaType_Int32;
      variant->Value.Int32 = variant->Value.Int64;
      return true;
    }
  }

  if (nodeId->Identifier.Numeric == OpcUaType_UInt32) {
    if (variant->Datatype == OpcUaType_UInt64 && variant->Value.UInt64 <= OpcUa_UInt32_Max) {
      variant->Datatype = OpcUaType_UInt32;
      variant->Value.UInt32 = variant->Value.UInt64;
      return true;
    }
    if (variant->Datatype == OpcUaType_Int64 && OpcUa_UInt32_Min <= variant->Value.Int64 && variant->Value.Int64 <= OpcUa_UInt32_Max) {
      variant->Datatype = OpcUaType_UInt32;
      variant->Value.UInt32 = variant->Value.Int64;
      return true;
    }
  }

  if (nodeId->Identifier.Numeric == OpcUaType_Int64) {
    if (variant->Datatype == OpcUaType_UInt64 && variant->Value.UInt64 <= OpcUa_Int64_Max) {
      variant->Datatype = OpcUaType_Int64;
      variant->Value.Int64 = variant->Value.UInt64;
      return true;
    }
  }

  if (nodeId->Identifier.Numeric == OpcUaType_UInt64) {
    if (variant->Datatype == OpcUaType_Int64 && OpcUa_UInt64_Min <= variant->Value.Int64 && variant->Value.Int64 <= OpcUa_UInt64_Max) {
      variant->Datatype = OpcUaType_UInt64;
      variant->Value.UInt64 = variant->Value.Int64;
      return true;
    }
  }

  if (nodeId->Identifier.Numeric == OpcUaType_Float) {
    if (variant->Datatype == OpcUaType_UInt64) {
      variant->Datatype = OpcUaType_Float;
      variant->Value.Float = variant->Value.UInt64;
      return true;
    }
    if (variant->Datatype == OpcUaType_Int64) {
      variant->Datatype = OpcUaType_Float;
      variant->Value.Float = variant->Value.Int64;
      return true;
    }
    if (variant->Datatype == OpcUaType_Double) {
      variant->Datatype = OpcUaType_Float;
      variant->Value.Float = variant->Value.Double;
      return true;
    }
  }

  if (nodeId->Identifier.Numeric == OpcUaType_Double) {
    if (variant->Datatype == OpcUaType_UInt64) {
      variant->Datatype = OpcUaType_Double;
      variant->Value.Double = variant->Value.UInt64;
      return true;
    }
    if (variant->Datatype == OpcUaType_Int64) {
      variant->Datatype = OpcUaType_Double;
      variant->Value.Double = variant->Value.Int64;
      return true;
    }
    if (variant->Datatype == OpcUaType_Float) {
      variant->Datatype = OpcUaType_Double;
      variant->Value.Double = variant->Value.Float;
      return true;
    }
  }

  return false;
}

static OpcUa_StatusCode opcua_write_read(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  struct WriteDataHelper* helper = pCallbackData;
  OpcUa_ReadResponse* pReadResponse = pResponse;
  OpcUa_Int32 noOfResults = pReadResponse->NoOfResults;
  OpcUa_DataValue* results = pReadResponse->Results;

  for (OpcUa_Int32 i = 0; i < helper->count; ++i) {
    if (i < noOfResults) {
      const OpcUa_Variant* variant = &results[i].Value;
      if (variant->Datatype == OpcUaType_NodeId)
        covertVariant(&helper->writeValue[i].Value.Value, variant->Value.NodeId);
    }
  }

  OpcUa_RequestHeader requestHeader;
  OpcUa_StatusCode statusCode = OpcUa_ClientApi_BeginWrite(
    setupRequestHeader(&requestHeader),
    &requestHeader,
    helper->count,
    helper->writeValue,
    opcua_write_write,
    helper);
  if (!OpcUa_IsGood(statusCode))
    opcua_write_write(OpcUa_Null, OpcUa_Null, OpcUa_Null, helper, statusCode);

  return OpcUa_Good;
}

void write_data(void* user, struct wpcp_result_t* result, const struct wpcp_value_t* id, const struct wpcp_value_t* value, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count)
{
  struct WriteDataHelper* helper;
  if (*context)
    helper = (struct WriteDataHelper*) *context;
  else {
    OpcUa_UInt32 count = remaining + 1;
    *context = helper = malloc(sizeof(struct WriteDataHelper) + count * sizeof(OpcUa_ReadValueId));
    OpcUa_Byte* data = malloc(sizeof(struct WriteDataHelper) + count * (sizeof(OpcUa_ReadValueId)+sizeof(OpcUa_WriteValue)));
    helper = *context = data;
    helper->result = result;
    helper->count = count;
    helper->readValueId = (OpcUa_ReadValueId*)(data + sizeof(struct WriteDataHelper));
    helper->writeValue = (OpcUa_WriteValue*)(data + sizeof(struct WriteDataHelper) + count * sizeof(OpcUa_ReadValueId));
  }

  OpcUa_ReadValueId* readValueId = &helper->readValueId[helper->count - 1 - remaining];
  OpcUa_WriteValue* writeValue = &helper->writeValue[helper->count - 1 - remaining];
  OpcUa_ReadValueId_Initialize(readValueId);
  OpcUa_WriteValue_Initialize(writeValue);
  toNodeId(id, &writeValue->NodeId);
  writeValue->AttributeId = OpcUa_Attributes_Value;
  toVariant(value, &writeValue->Value.Value);
  readValueId->NodeId = writeValue->NodeId;
  readValueId->AttributeId = OpcUa_Attributes_DataType;

  if (!remaining) {
    OpcUa_RequestHeader requestHeader;
    OpcUa_StatusCode statusCode = OpcUa_ClientApi_BeginRead(
      setupRequestHeader(&requestHeader),
      &requestHeader,
      0.0,
      OpcUa_TimestampsToReturn_Neither,
      helper->count,
      helper->readValueId,
      opcua_write_read,
      helper);
    if (!OpcUa_IsGood(statusCode))
      opcua_write_read(OpcUa_Null, OpcUa_Null, OpcUa_Null, helper, statusCode);
  }
}

static OpcUa_StatusCode opcua_read_history_data(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  struct wpcp_result_t* result = pCallbackData;
  OpcUa_HistoryReadResponse* pHistoryReadResponse = pResponse;
  OpcUa_Int32 noOfResults = pHistoryReadResponse->NoOfResults;
  OpcUa_HistoryReadResult* results = pHistoryReadResponse->Results;

  if (OpcUa_IsBad(pHistoryReadResponse->ResponseHeader.ServiceResult)) {
    wpcp_return_read_history_data(result, NULL, 0);
    return OpcUa_Bad;
  }

  assert(noOfResults == 1);
  for (OpcUa_Int32 i = 0; i < noOfResults; ++i) {
    //OpcUa_BadHistoryOperationUnsupported
    if (OpcUa_IsGood(results[i].StatusCode)) {
      OpcUa_Int32 noOfDataValues;
      const OpcUa_DataValue* dataValues;

      const OpcUa_ExtensionObject* resultHistoryData = &results[i].HistoryData;
      if (resultHistoryData->Encoding == OpcUa_ExtensionObjectEncoding_EncodeableObject && resultHistoryData->Body.EncodeableObject.Type->TypeId == OpcUaId_HistoryData) {
        const OpcUa_HistoryData* historyData = resultHistoryData->Body.EncodeableObject.Object;
        noOfDataValues = historyData->NoOfDataValues;
        dataValues = historyData->DataValues;
      }
      else if (resultHistoryData->Encoding == OpcUa_ExtensionObjectEncoding_EncodeableObject && resultHistoryData->Body.EncodeableObject.Type->TypeId == OpcUaId_HistoryModifiedData) {
        const OpcUa_HistoryModifiedData* historyModifiedData = resultHistoryData->Body.EncodeableObject.Object;
        noOfDataValues = historyModifiedData->NoOfDataValues;
        dataValues = historyModifiedData->DataValues;
      }
      else {
        noOfDataValues = 0;
        dataValues = NULL;
      }

      wpcp_return_read_history_data(result, NULL, noOfDataValues);
      for (OpcUa_Int32 j = 0; j < noOfDataValues; ++j) {
        struct wpcp_value_t value;
        toWpcpValue(&dataValues[j].Value, &value);
        wpcp_return_read_history_data_item(result, &value, toWpcpTime(&dataValues[j].SourceTimestamp, dataValues[j].SourcePicoseconds), dataValues[j].StatusCode, NULL, 0);
      }
    }
    else
      wpcp_return_read_history_data(result, NULL, 0);
  }

  return OpcUa_Good;
}

OpcUa_UInt64 toUInt64(const OpcUa_DateTime* dateTime)
{
  OpcUa_UInt64 ret = dateTime->dwHighDateTime;
  return ret << 32 | dateTime->dwLowDateTime;
}

void read_history_data(void* user, struct wpcp_result_t* result, const struct wpcp_value_t* id, const struct wpcp_value_t* starttime, const struct wpcp_value_t* endtime, const struct wpcp_value_t* maxresults, const struct wpcp_value_t* aggregation, const struct wpcp_value_t* interval, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count)
{
  OpcUa_HistoryReadValueId nodesToRead;
  OpcUa_HistoryReadValueId_Initialize(&nodesToRead);
  toNodeId(id, &nodesToRead.NodeId);

  OpcUa_ExtensionObject historyReadDetails;
  OpcUa_ReadProcessedDetails* readProcessedDetails = NULL;
  OpcUa_ReadRawModifiedDetails* readRawModifiedDetails = NULL;
  OpcUa_NodeId aggregateType;

  if (aggregation) {
    OpcUa_EncodeableObject_CreateExtension(&OpcUa_ReadProcessedDetails_EncodeableType, &historyReadDetails, &readProcessedDetails);
    OpcUa_NodeId_Initialize(&aggregateType);

    if (starttime)
      toDateTime(starttime, &readProcessedDetails->StartTime);
    if (endtime)
      toDateTime(endtime, &readProcessedDetails->EndTime);
    if (interval) {
      if (interval->type == WPCP_VALUE_TYPE_DOUBLE)
        readProcessedDetails->ProcessingInterval = interval->value.dbl;
      else if (interval->type == WPCP_VALUE_TYPE_UINT64)
        readProcessedDetails->ProcessingInterval = (double)interval->value.uint;
    }

#define XX(str, opcid) \
    else if (aggregation->value.length == (sizeof(str)-1) && !memcmp(aggregation->data.text_string, str, sizeof(str)-1)) \
      aggregateType.Identifier.Numeric = opcid;

    if (aggregation->type != WPCP_VALUE_TYPE_TEXT_STRING)
    {
    }
    XX("start", OpcUaId_AggregateFunction_Start)
    XX("minimum", OpcUaId_AggregateFunction_Minimum)
    XX("maximum", OpcUaId_AggregateFunction_Maximum)
    XX("average", OpcUaId_AggregateFunction_Average)

    readProcessedDetails->AggregateType = &aggregateType;
    readProcessedDetails->NoOfAggregateType = 1;
  }
  else {
    OpcUa_EncodeableObject_CreateExtension(&OpcUa_ReadRawModifiedDetails_EncodeableType, &historyReadDetails, &readRawModifiedDetails);

    if (starttime)
      toDateTime(starttime, &readRawModifiedDetails->StartTime);
    if (endtime)
      toDateTime(endtime, &readRawModifiedDetails->EndTime);
    if (maxresults && maxresults->type == WPCP_VALUE_TYPE_UINT64)
      readRawModifiedDetails->NumValuesPerNode = (OpcUa_UInt32) maxresults->value.uint;
  }

  OpcUa_RequestHeader requestHeader;
  OpcUa_StatusCode statusCode = OpcUa_ClientApi_BeginHistoryRead(
    setupRequestHeader(&requestHeader),
    &requestHeader,
    &historyReadDetails,
    OpcUa_TimestampsToReturn_Source,
    OpcUa_False,
    1,
    &nodesToRead,
    opcua_read_history_data,
    result);
  if (!OpcUa_IsGood(statusCode))
    opcua_read_history_data(OpcUa_Null, OpcUa_Null, OpcUa_Null, result, statusCode);

/*  if (aggregation)
    OpcUa_EncodeableObject_Delete(&OpcUa_ReadProcessedDetails_EncodeableType, &readProcessedDetails);
  else
    OpcUa_EncodeableObject_Delete(&OpcUa_ReadRawModifiedDetails_EncodeableType, &readRawModifiedDetails);
    */
}

static OpcUa_StatusCode opcua_read_history_alarm(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  struct wpcp_result_t* result = pCallbackData;
  OpcUa_HistoryReadResponse* pHistoryReadResponse = pResponse;
  OpcUa_Int32 noOfResults = pHistoryReadResponse->NoOfResults;
  OpcUa_HistoryReadResult* results = pHistoryReadResponse->Results;

  if (OpcUa_IsBad(pHistoryReadResponse->ResponseHeader.ServiceResult)) {
    wpcp_return_read_history_alarm(result, NULL, 0);
    return OpcUa_Bad;
  }

  assert(noOfResults == 1);
  for (OpcUa_Int32 i = 0; i < noOfResults; ++i) {
    if (OpcUa_IsGood(results[i].StatusCode)) {
      OpcUa_Int32 noOfDataValues;
      const OpcUa_HistoryEventFieldList* events;

      const OpcUa_ExtensionObject* resultHistoryData = &results[i].HistoryData;
      if (resultHistoryData->Encoding == OpcUa_ExtensionObjectEncoding_EncodeableObject && resultHistoryData->Body.EncodeableObject.Type->TypeId == OpcUaId_HistoryEvent) {
        const OpcUa_HistoryEvent* historyEvent = resultHistoryData->Body.EncodeableObject.Object;
        noOfDataValues = historyEvent->NoOfEvents;
        events = historyEvent->Events;
      }
      else {
        noOfDataValues = 0;
        events = NULL;
      }

      wpcp_return_read_history_alarm(result, NULL, noOfDataValues);
      for (OpcUa_Int32 j = 0; j < noOfDataValues; ++j) {
        struct wpcp_value_t value;
        toWpcpValue(&events[j].EventFields[0].Value, &value);
        wpcp_return_read_history_alarm_item(result, NULL, 0, false, &value, &value, 0.0, 0, NULL, 0, false, NULL, 0);
      }
    }
    else
      wpcp_return_read_history_alarm(result, NULL, 0);
  }

  return OpcUa_Good;
}

void read_history_alarm(void* user, struct wpcp_result_t* result, const struct wpcp_value_t* id, const struct wpcp_value_t* starttime, const struct wpcp_value_t* endtime, const struct wpcp_value_t* maxresults, const struct wpcp_value_t* filter, void** context, uint32_t remaining, const struct wpcp_key_value_pair_t* additional, uint32_t additional_count)
{
  OpcUa_HistoryReadValueId nodesToRead;
  OpcUa_HistoryReadValueId_Initialize(&nodesToRead);
  toNodeId(id, &nodesToRead.NodeId);

  OpcUa_ExtensionObject historyReadDetails;
  OpcUa_ReadEventDetails* readEventDetails = NULL;
  OpcUa_NodeId aggregateType;
  OpcUa_RequestHeader requestHeader;
  OpcUa_StatusCode statusCode;

  OpcUa_EncodeableObject_CreateExtension(&OpcUa_ReadEventDetails_EncodeableType, &historyReadDetails, &readEventDetails);

  if (starttime)
    toDateTime(starttime, &readEventDetails->StartTime);
  if (endtime)
    toDateTime(endtime, &readEventDetails->EndTime);
  if (maxresults && maxresults->type == WPCP_VALUE_TYPE_UINT64)
    readEventDetails->NumValuesPerNode = (OpcUa_UInt32)maxresults->value.uint;

  statusCode = OpcUa_ClientApi_BeginHistoryRead(
    setupRequestHeader(&requestHeader),
    &requestHeader,
    &historyReadDetails,
    OpcUa_TimestampsToReturn_Source,
    OpcUa_False,
    1,
    &nodesToRead,
    opcua_read_history_alarm,
    result);
  if (!OpcUa_IsGood(statusCode))
    opcua_read_history_alarm(OpcUa_Null, OpcUa_Null, OpcUa_Null, result, statusCode);

  /*
  OpcUa_EncodeableObject_Delete(&OpcUa_ReadEventDetails_EncodeableType, &readEventDetails);
  */
}
