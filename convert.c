#include "main.h"
#include <opcua_guid.h>
#include <opcua_string.h>
#include <windows.h>

#define EPOCHE 116444736000000000ULL

OpcUa_StatusCode toDateTime(const struct wpcp_value_t* value, OpcUa_DateTime* timestamp)
{
  OpcUa_UInt64 ts = 10000;

  if (value->type == WPCP_VALUE_TYPE_UINT64)
    ts *= value->value.uint;
  else if (value->type == WPCP_VALUE_TYPE_FLOAT)
    ts *= value->value.flt;
  else if (value->type == WPCP_VALUE_TYPE_DOUBLE)
    ts *= value->value.dbl;
  else
    return OpcUa_BadInvalidArgument;

  ts += EPOCHE;
  timestamp->dwHighDateTime = ts >> 32;
  timestamp->dwLowDateTime = ts & 0xFFFFFFFF;
  return OpcUa_Good;
}

OpcUa_StatusCode toNodeId(const struct wpcp_value_t* id, OpcUa_NodeId* nodeId)
{
  const char* str = id->data.text_string;
  size_t length = id->value.length;

  if (id->type != WPCP_VALUE_TYPE_TEXT_STRING)
    return OpcUa_BadInvalidArgument;

  OpcUa_NodeId_Initialize(nodeId);

  if (length > 3 && str[0] == 'n' && str[1] == 's' && str[2] == '=' && str[3] != ';') {
    size_t i = 3;
    while (str[i] != ';') {
      if (str[i] < '0' || '9' < str[i])
        return OpcUa_Bad;
      ++i;
    }

    char* end = 0;
    unsigned long ns = strtoul(str + 3, &end, 10);
    if (ns > OpcUa_UInt16_Max || end != str + i)
      return OpcUa_Bad;
    length -= i + 1;
    str += i + 1;
    nodeId->NamespaceIndex = (OpcUa_UInt16)ns;
  }

  if (length < 3 || str[1] != '=')
    return OpcUa_Bad;

  if (str[0] == 'i') {
    nodeId->IdentifierType = OpcUa_IdentifierType_Numeric;
    nodeId->Identifier.Numeric = atoi(str + 2); // TODO: Possible problem with end character
    return OpcUa_Good;
  }

  if (str[0] == 's') {
    nodeId->IdentifierType = OpcUa_IdentifierType_String;
    return OpcUa_String_AttachToString((OpcUa_StringA)str + 2, length - 2, length - 2, OpcUa_True, OpcUa_False, &nodeId->Identifier.String);
  }

  return OpcUa_Bad;
}

OpcUa_StatusCode toVariant(const struct wpcp_value_t* value, OpcUa_Variant* variant)
{
  OpcUa_Variant_Initialize(variant);

  switch (value->type) {
  case WPCP_VALUE_TYPE_FALSE:
    variant->Datatype = OpcUaType_Boolean;
    variant->Value.Boolean = OpcUa_False;
    return OpcUa_Good;

  case WPCP_VALUE_TYPE_TRUE:
    variant->Datatype = OpcUaType_Boolean;
    variant->Value.Boolean = OpcUa_True;
    return OpcUa_Good;

  case WPCP_VALUE_TYPE_NULL:
    variant->Datatype = OpcUaType_Null;
    variant->Value.Boolean = OpcUa_True;
    return OpcUa_Good;

  case WPCP_VALUE_TYPE_FLOAT:
    variant->Datatype = OpcUaType_Float;
    variant->Value.Float = value->value.flt;
    return OpcUa_Good;

  case WPCP_VALUE_TYPE_DOUBLE:
    variant->Datatype = OpcUaType_Double;
    variant->Value.Double = value->value.dbl;
    return OpcUa_Good;

  case WPCP_VALUE_TYPE_UINT64:
    variant->Datatype = OpcUaType_UInt64;
    variant->Value.UInt64 = value->value.uint;
    return OpcUa_Good;

  case WPCP_VALUE_TYPE_INT64:
    variant->Datatype = OpcUaType_Int64;
    variant->Value.Int64 = value->value.sint;
    return OpcUa_Good;

  case WPCP_VALUE_TYPE_BYTE_STRING:
    variant->Datatype = OpcUaType_ByteString;
    {
      const OpcUa_ByteString uaString = { value->value.length, (OpcUa_Byte*)value->data.byte_string };
      OpcUa_ByteString_CopyTo(&uaString, &variant->Value.ByteString);
    }
    return OpcUa_Good;

  case WPCP_VALUE_TYPE_TEXT_STRING:
    variant->Datatype = OpcUaType_String;
    {
      const OpcUa_String uaString = OPCUA_STRING_STATICINITIALIZEWITH((OpcUa_CharA*)value->data.text_string, value->value.length);
      OpcUa_String_CopyTo(&uaString, &variant->Value.String);
    }
    return OpcUa_Good;

  case WPCP_VALUE_TYPE_ARRAY:
  case WPCP_VALUE_TYPE_MAP:
  case WPCP_VALUE_TYPE_TAG:
  default:
    break;
  }

  return OpcUa_Bad;
}

double toWpcpTime(const OpcUa_DateTime* timestamp, OpcUa_UInt16 picoseconds)
{
  OpcUa_UInt64 ts = timestamp->dwHighDateTime;
  ts = ts << 32 | timestamp->dwLowDateTime;
  ts -= EPOCHE;
  return ts / 10000.0 + picoseconds * 1.0e-9;
}

bool toWpcpId(const OpcUa_NodeId* nodeid, struct wpcp_value_t* value, char* buffer, uint32_t buffer_length)
{
  value->type = WPCP_VALUE_TYPE_TEXT_STRING;
  value->data.text_string = buffer;

  if (nodeid->IdentifierType == OpcUa_IdentifierType_Numeric)
    value->value.length = OpcUa_SnPrintfA(buffer, buffer_length, "ns=%u;i=%u", nodeid->NamespaceIndex, nodeid->Identifier.Numeric);
  else if (nodeid->IdentifierType == OpcUa_IdentifierType_String) {
    uint64_t len = OpcUa_SnPrintfA(buffer, buffer_length, "ns=%u;s=", nodeid->NamespaceIndex);
    OpcUa_UInt32 strsize = OpcUa_String_StrSize(&nodeid->Identifier.String);
    memcpy(buffer + len, OpcUa_String_GetRawString(&nodeid->Identifier.String), strsize);
    value->value.length = len + strsize;
  }
  else if (nodeid->IdentifierType == OpcUa_IdentifierType_Guid) {
    OpcUa_String* str = 0;
    OpcUa_Guid_ToString(nodeid->Identifier.Guid, &str);
    uint64_t len = OpcUa_SnPrintfA(buffer, buffer_length, "ns=%u;g=", nodeid->NamespaceIndex);
    OpcUa_UInt32 strsize = OpcUa_String_StrSize(str);
    memcpy(buffer + len, OpcUa_String_GetRawString(str), strsize);
    value->value.length = len + strsize;
    OpcUa_String_Delete(&str);
  }
  else
    value->value.length = 0;

  return true;
}

bool toWpcpString(const OpcUa_String* string, struct wpcp_value_t* value)
{
  value->type = WPCP_VALUE_TYPE_TEXT_STRING;
  value->value.length = OpcUa_String_StrSize(string);
  value->data.text_string = OpcUa_String_GetRawString(string);
  return true;
}

bool toWpcpValue2(const OpcUa_Variant* variant, struct wpcp_value_t* value, char* buffer, uint32_t buffer_length)
{
  switch (variant->Datatype) {
  case OpcUaType_Null:
    value->type = WPCP_VALUE_TYPE_NULL;
    return true;

  case OpcUaType_Boolean:
    if (variant->Value.Boolean == OpcUa_False)
      value->type = WPCP_VALUE_TYPE_FALSE;
    else
      value->type = WPCP_VALUE_TYPE_TRUE;
    return true;

  case OpcUaType_SByte:
    value->type = WPCP_VALUE_TYPE_INT64;
    value->value.sint = variant->Value.SByte;
    return true;

  case OpcUaType_Byte:
    value->type = WPCP_VALUE_TYPE_UINT64;
    value->value.uint = variant->Value.Byte;
    return true;

  case OpcUaType_Int16:
    value->type = WPCP_VALUE_TYPE_INT64;
    value->value.sint = variant->Value.Int16;
    return true;

  case OpcUaType_UInt16:
    value->type = WPCP_VALUE_TYPE_UINT64;
    value->value.uint = variant->Value.UInt16;
    return true;

  case OpcUaType_Int32:
    value->type = WPCP_VALUE_TYPE_INT64;
    value->value.sint = variant->Value.Int32;
    return true;

  case OpcUaType_UInt32:
    value->type = WPCP_VALUE_TYPE_UINT64;
    value->value.uint = variant->Value.UInt32;
    return true;

  case OpcUaType_Int64:
    value->type = WPCP_VALUE_TYPE_INT64;
    value->value.sint = variant->Value.Int64;
    return true;

  case OpcUaType_UInt64:
    value->type = WPCP_VALUE_TYPE_UINT64;
    value->value.uint = variant->Value.UInt64;
    return true;

  case OpcUaType_Float:
    value->type = WPCP_VALUE_TYPE_FLOAT;
    value->value.flt = variant->Value.Float;
    return true;

  case OpcUaType_Double:
    value->type = WPCP_VALUE_TYPE_DOUBLE;
    value->value.dbl = variant->Value.Double;
    return true;

  case OpcUaType_String:
    return toWpcpString(&variant->Value.String, value);

  case OpcUaType_DateTime:
    value->type = WPCP_VALUE_TYPE_DOUBLE;
    value->value.dbl = toWpcpTime(&variant->Value.DateTime, 0);
    return true;

  case OpcUaType_Guid:
    value->type = WPCP_VALUE_TYPE_UNDEFINED;
    return false;

  case OpcUaType_ByteString:
    value->type = WPCP_VALUE_TYPE_BYTE_STRING;
    if (variant->Value.ByteString.Length >= 0) {
      value->value.length = variant->Value.ByteString.Length;
      value->data.byte_string = variant->Value.ByteString.Data;
    }
    else
      value->value.length = 0;
    return true;

  case OpcUaType_XmlElement:
    value->type = WPCP_VALUE_TYPE_BYTE_STRING;
    if (variant->Value.ByteString.Length >= 0) {
      value->value.length = variant->Value.ByteString.Length;
      value->data.byte_string = variant->Value.ByteString.Data;
    }
    else
      value->value.length = 0;
    return true;

  case OpcUaType_StatusCode:
    value->type = WPCP_VALUE_TYPE_UINT64;
    value->value.uint = variant->Value.StatusCode;
    return true;

  case OpcUaType_NodeId:
    return toWpcpId(variant->Value.NodeId, value, buffer, buffer_length);

  case OpcUaType_ExpandedNodeId:
    return toWpcpId(&variant->Value.ExpandedNodeId->NodeId, value, buffer, buffer_length);

  case OpcUaType_QualifiedName:
    return toWpcpString(&variant->Value.QualifiedName->Name, value);

  case OpcUaType_LocalizedText:
    return toWpcpString(&variant->Value.LocalizedText->Text, value);

  case OpcUaType_ExtensionObject:
  case OpcUaType_DataValue:
  case OpcUaType_Variant:
  case OpcUaType_DiagnosticInfo:
  default:
    value->type = WPCP_VALUE_TYPE_UNDEFINED;
    return false;
  }
}

bool toWpcpValue(const OpcUa_Variant* variant, struct wpcp_value_t* value)
{
  return toWpcpValue2(variant, value, NULL, 0);
}

void variant2string(const struct wpcp_value_t* value, char* buffer, size_t size)
{
  size_t i;
  switch (value->type) {
  case WPCP_VALUE_TYPE_UINT64:
    OpcUa_SnPrintfA(buffer, size, "%I64u", value->value.uint);
    break;

  case WPCP_VALUE_TYPE_INT64:
    OpcUa_SnPrintfA(buffer, size, "%I64d", value->value.sint);
    break;

  case WPCP_VALUE_TYPE_BYTE_STRING:
    for (i = 0; i < value->value.length; ++i) {
      const unsigned char* byte_string = value->data.byte_string;
      OpcUa_SnPrintfA(buffer + i * 2, size - i * 2, "%02x", byte_string[i]);
    }
    break;

  case WPCP_VALUE_TYPE_TEXT_STRING:
    //TODO MEMORY
    memcpy(buffer, value->data.text_string, value->value.length);
    buffer[value->value.length] = '\0';
    break;

  case WPCP_VALUE_TYPE_FALSE:
    OpcUa_SnPrintfA(buffer, size, "false");
    break;

  case WPCP_VALUE_TYPE_TRUE:
    OpcUa_SnPrintfA(buffer, size, "true");
    break;

  case WPCP_VALUE_TYPE_NULL:
    OpcUa_SnPrintfA(buffer, size, "null");
    break;

  case WPCP_VALUE_TYPE_UNDEFINED:
    OpcUa_SnPrintfA(buffer, size, "undefined");
    break;

  case WPCP_VALUE_TYPE_SIMPLE_VALUE:
    OpcUa_SnPrintfA(buffer, size, "SIMPLE VALUE %I64u", value->value.uint);
    break;

  case WPCP_VALUE_TYPE_FLOAT:
    OpcUa_SnPrintfA(buffer, size, "%g", value->value.flt);
    break;

  case WPCP_VALUE_TYPE_DOUBLE:
    OpcUa_SnPrintfA(buffer, size, "%g", value->value.dbl);
    break;
    /*
  case WPCP_VALUE_TYPE_TAG:
    wpcp_cbor_write_buffer_write_type_and_uint64_value(buffer, WPCP_CBOR_MAJOR_TYPE_TAG, value->value.uint);
    wpcp_cbor_write_buffer_write_wpcp_value(buffer, value->data.first_child);
    break;

  case WPCP_VALUE_TYPE_ARRAY:
    wpcp_cbor_write_buffer_write_type_and_size_value(buffer, WPCP_CBOR_MAJOR_TYPE_ARRAY, value->value.length);
    wpcp_cbor_write_buffer_write_wpcp_values(buffer, value->value.length, value->data.first_child);
    break;

  case WPCP_VALUE_TYPE_MAP:
    wpcp_cbor_write_buffer_write_type_and_size_value(buffer, WPCP_CBOR_MAJOR_TYPE_MAP, value->value.length);
    wpcp_cbor_write_buffer_write_wpcp_values(buffer, value->value.length * 2, value->data.first_child);
    break;*/

  default:
    OpcUa_SnPrintfA(buffer, size, "WTF");
    break;
  }
}
