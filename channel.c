#include "main.h"
#include <wpcp_lws.h>
#include <opcua_core.h>
#include <opcua_p_crypto.h>
#include <opcua_clientapi.h>
#include <assert.h>
#include <stdlib.h>

static OpcUa_Int32 g_sessionRequestedLifetime = 10000;
static OpcUa_Double g_sessionTimeout = 60000; // 1 minute
static OpcUa_Double g_subscriptionPublishInterval = 0;
static OpcUa_UInt32 g_maxRequestMessageSize = 0;

static OpcUa_UInt32 g_subscriptionLifetimeCount;
static OpcUa_UInt32 g_subscriptionMaxKeepAliveCount;
static const OpcUa_UInt32 g_subscriptionMaxNotificationsPerPublish = 128;
static const OpcUa_Byte g_subscriptionPriority = 0;

static OpcUa_NodeId g_authenticationToken;// = 0;
static OpcUa_Channel g_channel;
OpcUa_UInt32 g_subscriptionId;


static OpcUa_UInt32 g_noOfSubscriptionAcknowledgements;
static OpcUa_SubscriptionAcknowledgement* g_subscriptionAcknowledgements;
static OpcUa_Mutex g_subscriptionAcknowledgementsMutex;


static void addSubscriptionAcknowledgement(OpcUa_UInt32 subscriptionId, OpcUa_UInt32 sequenceNumber)
{
  OpcUa_UInt32 nr = g_noOfSubscriptionAcknowledgements++;
  OpcUa_Mutex_Lock(g_subscriptionAcknowledgementsMutex);
  g_subscriptionAcknowledgements = OpcUa_Memory_ReAlloc(g_subscriptionAcknowledgements, sizeof(*g_subscriptionAcknowledgements) * g_noOfSubscriptionAcknowledgements);
  g_subscriptionAcknowledgements[nr].SubscriptionId = subscriptionId;
  g_subscriptionAcknowledgements[nr].SequenceNumber = sequenceNumber;
  OpcUa_Mutex_Unlock(g_subscriptionAcknowledgementsMutex);
}

OpcUa_Channel setupRequestHeader(OpcUa_RequestHeader *requestHeader)
{
  OpcUa_RequestHeader_Initialize(requestHeader);
  requestHeader->TimeoutHint = 300000;
  requestHeader->Timestamp = OpcUa_DateTime_UtcNow();
  requestHeader->AuthenticationToken = g_authenticationToken;
  return g_channel;
}

static OpcUa_StatusCode opcua_cmi(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  OpcUa_CreateMonitoredItemsResponse * r2 = pResponse;
  OpcUa_EventFilterResult* r = r2->Results->FilterResult.Body.EncodeableObject.Object;

  if (r) {
    for (int i = 0; i < r->NoOfSelectClauseResults; ++i) {
      assert(OpcUa_IsGood(r->SelectClauseResults[i]));
    }
  }
  return 0;
}

OpcUa_StatusCode kickofPublish(void);

static OpcUa_StatusCode opc_publish(OpcUa_Channel hChannel, OpcUa_Void* pResponse, OpcUa_EncodeableType* pResponseType, OpcUa_Void* pCallbackData, OpcUa_StatusCode uStatus)
{
  OpcUa_PublishResponse* publishResponse = (OpcUa_PublishResponse*)pResponse;

  if (OpcUa_IsGood(uStatus) && OpcUa_IsGood(publishResponse->ResponseHeader.ServiceResult)) {
    kickofPublish();

    addSubscriptionAcknowledgement(publishResponse->SubscriptionId, publishResponse->NotificationMessage.SequenceNumber);

    if (publishResponse->NotificationMessage.NoOfNotificationData) {
      for (OpcUa_Int32 i = 0; i < publishResponse->NotificationMessage.NoOfNotificationData; ++i) {
        OpcUa_ExtensionObject* notificationData = &publishResponse->NotificationMessage.NotificationData[i];

        if (notificationData->Encoding != OpcUa_ExtensionObjectEncoding_EncodeableObject || notificationData->Body.EncodeableObject.Object == OpcUa_Null || notificationData[i].Body.EncodeableObject.Type == OpcUa_Null)
          continue;

        if (notificationData->Body.EncodeableObject.Type->TypeId == OpcUaId_DataChangeNotification) {
          OpcUa_DataChangeNotification* notification = notificationData->Body.EncodeableObject.Object;
          opcua_publishDataChangeNotification(publishResponse->SubscriptionId, notification->NoOfMonitoredItems, notification->MonitoredItems);
        } else if (notificationData->Body.EncodeableObject.Type->TypeId == OpcUaId_EventNotificationList) {
          OpcUa_EventNotificationList* notification = notificationData->Body.EncodeableObject.Object;
          opcua_publishEventNotificationList(publishResponse->SubscriptionId, notification->NoOfEvents, notification->Events);
        } else if (notificationData->Body.EncodeableObject.Type->TypeId == OpcUaId_StatusChangeNotification) {
          OpcUa_StatusChangeNotification* notification = notificationData->Body.EncodeableObject.Object;
          printf("STATUS: %x\n", notification->Status);
        } else {
          assert(false);
        }
      }
    } else {
      // keep alive
    }
  }

  return uStatus;
}

OpcUa_StatusCode kickofPublish(void)
{
  OpcUa_StatusCode statusCode;
  OpcUa_RequestHeader requestHeader;
  OpcUa_Channel channel = setupRequestHeader(&requestHeader);
  OpcUa_Thread_Sleep(100);
  OpcUa_Mutex_Lock(g_subscriptionAcknowledgementsMutex);
  statusCode = OpcUa_ClientApi_BeginPublish(
    channel,
    &requestHeader,
    g_noOfSubscriptionAcknowledgements,
    g_subscriptionAcknowledgements,
    opc_publish,
    NULL);
  g_noOfSubscriptionAcknowledgements = 0;
  OpcUa_Mutex_Unlock(g_subscriptionAcknowledgementsMutex);
  return statusCode;
}



OpcUa_StatusCode initializeOpcUa(const OpcUa_CharA* url, const OpcUa_CharA* uri)
{
  OpcUa_StatusCode statusCode;
  OpcUa_RequestHeader requestHeader;
  OpcUa_ResponseHeader responseHeader;
  OpcUa_String sessionName = OPCUA_STRING_STATICINITIALIZEWITH("wpcp2opcua", 10);
  OpcUa_String securityPolicy = OPCUA_STRING_STATICINITIALIZEWITH(OpcUa_SecurityPolicy_None, sizeof(OpcUa_SecurityPolicy_None)-1);

  OpcUa_Mutex_Create(&g_subscriptionAcknowledgementsMutex);

  OpcUa_ByteString clientCertificate;
  OpcUa_ByteString clientPrivateKey;
  OpcUa_ByteString serverCertificate;
  OpcUa_Channel_SecurityToken* securityToken;

  OpcUa_ByteString_Initialize(&clientCertificate);
  OpcUa_ByteString_Initialize(&clientPrivateKey);
  OpcUa_ByteString_Initialize(&serverCertificate);

#ifdef OPCUA_PKI_TYPE_NONE
  OpcUa_CertificateStoreConfiguration certificateStoreConfiguration;
  OpcUa_CertificateStoreConfiguration_Initialize(&certificateStoreConfiguration);
  statusCode = OpcUa_CertificateStoreConfiguration_Set(
    &certificateStoreConfiguration,
    OPCUA_PKI_TYPE_NONE,
    ".",
    ".",
    ".",
    ".",
    NULL,
    NULL,
    0,
    NULL);
#else
  OpcUa_P_OpenSSL_CertificateStore_Config certificateStoreConfiguration;
  memset(&certificateStoreConfiguration, 0, sizeof(certificateStoreConfiguration));
  certificateStoreConfiguration.PkiType = OpcUa_NO_PKI;
#endif

  OpcUa_Channel_Create(&g_channel, OpcUa_Channel_SerializerType_Binary);
  statusCode = OpcUa_Channel_Connect(
    g_channel,
    url,
    OpcUa_TransportProfile_UaTcp,
    NULL,
    NULL,
    &clientCertificate,
    &clientPrivateKey,
    &serverCertificate,
    &certificateStoreConfiguration,
    &securityPolicy,
    g_sessionRequestedLifetime,
    OpcUa_MessageSecurityMode_None,
    &securityToken,
    100000);

  {
    OpcUa_ApplicationDescription applicationDescription;
    OpcUa_String serverUri = OPCUA_STRING_STATICINITIALIZEWITH((OpcUa_CharA*)uri, OpcUa_StrLenA(uri));
    OpcUa_String endpointUrl = OPCUA_STRING_STATICINITIALIZEWITH((OpcUa_CharA*)url, OpcUa_StrLenA(url));
    OpcUa_ByteString clientNonce;
    OpcUa_ByteString clientCertificate;
    OpcUa_NodeId sessionId;
    OpcUa_ByteString serverNonce;
    OpcUa_ByteString serverCertificate;
    OpcUa_Int32 noOfServerEndpoints;
    OpcUa_EndpointDescription* serverEndpoints;
    OpcUa_Int32 noOfServerSoftwareCertificates;
    OpcUa_SignedSoftwareCertificate* serverSoftwareCertificates;
    OpcUa_SignatureData serverSignature;

    OpcUa_ApplicationDescription_Initialize(&applicationDescription);
    OpcUa_ByteString_Initialize(&clientNonce);
    OpcUa_ByteString_Initialize(&clientCertificate);
    OpcUa_NodeId_Initialize(&sessionId);
    OpcUa_ByteString_Initialize(&serverNonce);
    OpcUa_ByteString_Initialize(&serverCertificate);
    OpcUa_SignatureData_Initialize(&serverSignature);

    OpcUa_RequestHeader_Initialize(&requestHeader);
    OpcUa_ResponseHeader_Initialize(&responseHeader);
    statusCode = OpcUa_ClientApi_CreateSession(
      g_channel,
      &requestHeader,
      &applicationDescription,
      &serverUri,
      &endpointUrl,
      &sessionName,
      &clientNonce,
      &clientCertificate,
      g_sessionTimeout,
      g_maxRequestMessageSize,
      &responseHeader,
      &sessionId,
      &g_authenticationToken,
      &g_sessionTimeout,
      &serverNonce,
      &serverCertificate,
      &noOfServerEndpoints,
      &serverEndpoints,
      &noOfServerSoftwareCertificates,
      &serverSoftwareCertificates,
      &serverSignature,
      &g_maxRequestMessageSize);

    if (OpcUa_IsBad(statusCode) || OpcUa_IsBad(responseHeader.ServiceResult)) {
      printf("Can not create session\n");
      exit(1);
    }

    OpcUa_ResponseHeader_Clear(&responseHeader);
  }

  {
    OpcUa_SignatureData clientSignature;
    OpcUa_ExtensionObject userIdentityToken;
    OpcUa_SignatureData userTokenSignature;
    OpcUa_ByteString serverNonce;
    OpcUa_Int32 noOfResults = 0;
    OpcUa_StatusCode* results = 0;
    OpcUa_Int32 noOfDiagnosticInfos = 0;
    OpcUa_DiagnosticInfo* diagnosticInfos = 0;

    OpcUa_SignatureData_Initialize(&clientSignature);
    OpcUa_ExtensionObject_Initialize(&userIdentityToken);
    OpcUa_SignatureData_Initialize(&userTokenSignature);
    OpcUa_ByteString_Initialize(&serverNonce);

    OpcUa_ResponseHeader_Initialize(&responseHeader);
    statusCode = OpcUa_ClientApi_ActivateSession(
      setupRequestHeader(&requestHeader),
      &requestHeader,
      &clientSignature,
      0,
      NULL,
      0,
      NULL,
      &userIdentityToken,
      &userTokenSignature,
      &responseHeader,
      &serverNonce,
      &noOfResults,
      &results,
      &noOfDiagnosticInfos,
      &diagnosticInfos);

    if (OpcUa_IsBad(statusCode) || OpcUa_IsBad(responseHeader.ServiceResult)) {
      printf("Can not activate session\n");
      exit(1);
    }

    OpcUa_ResponseHeader_Clear(&responseHeader);
  }

  OpcUa_ResponseHeader_Initialize(&responseHeader);
  statusCode = OpcUa_ClientApi_CreateSubscription(
    setupRequestHeader(&requestHeader),
    &requestHeader,
    g_subscriptionPublishInterval,
    g_subscriptionLifetimeCount,
    g_subscriptionMaxKeepAliveCount,
    g_subscriptionMaxNotificationsPerPublish,
    OpcUa_True,
    g_subscriptionPriority,
    &responseHeader,
    &g_subscriptionId,
    &g_subscriptionPublishInterval,
    &g_subscriptionLifetimeCount,
    &g_subscriptionMaxKeepAliveCount);

  if (OpcUa_IsBad(statusCode) || OpcUa_IsBad(responseHeader.ServiceResult)) {
    printf("Can not create subscription\n");
    exit(1);
  }

  OpcUa_ResponseHeader_Clear(&responseHeader);

  kickofPublish();
  kickofPublish();

  return statusCode;
}

OpcUa_StatusCode clearOpcUa(void)
{
  OpcUa_StatusCode statusCode = OpcUa_Good;
  OpcUa_Mutex_Delete(&g_subscriptionAcknowledgementsMutex);
  OpcUa_Memory_Free(g_subscriptionAcknowledgements);
  return statusCode;
}
