#include <Driver.h>

#ifndef RESHUB_USE_HELPER_ROUTINES
#define RESHUB_USE_HELPER_ROUTINES
#endif

#include <reshub.h>
#include <wdf.h>

#include <Registry.h>
#include <SPI.h>
#include <UC120.h>
#include <WorkItems.h>
#include <Uc120GPIO.h>
#include <UsbRole.h>

#ifndef DBG_PRINT_EX_LOGGING
#include "device.tmh"
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, LumiaUSBCKmCreateDevice)
#pragma alloc_text(PAGE, LumiaUSBCDevicePrepareHardware)
#pragma alloc_text(PAGE, LumiaUSBCDeviceReleaseHardware)
#endif

void LumiaUSBCCloseResources(PDEVICE_CONTEXT pDeviceContext)
{
  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "LumiaUSBCCloseResources Entry");

  if (pDeviceContext->Spi) {
    WdfIoTargetClose(pDeviceContext->Spi);
  }

  if (pDeviceContext->VbusGpio) {
    WdfIoTargetClose(pDeviceContext->VbusGpio);
  }

  if (pDeviceContext->PolGpio) {
    WdfIoTargetClose(pDeviceContext->PolGpio);
  }

  if (pDeviceContext->AmselGpio) {
    WdfIoTargetClose(pDeviceContext->AmselGpio);
  }

  if (pDeviceContext->EnGpio) {
    WdfIoTargetClose(pDeviceContext->EnGpio);
  }

  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "LumiaUSBCCloseResources Exit");
}

NTSTATUS LumiaUSBCDeviceReleaseHardware(
    WDFDEVICE Device, WDFCMRESLIST ResourcesTranslated)
{
  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
      "LumiaUSBCDeviceReleaseHardware Entry");

  UNREFERENCED_PARAMETER(ResourcesTranslated);

  PDEVICE_CONTEXT pDeviceContext = DeviceGetContext(Device);

  LumiaUSBCCloseResources(pDeviceContext);

  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
      "LumiaUSBCDeviceReleaseHardware Exit");

  return STATUS_SUCCESS;
}

NTSTATUS LumiaUSBCOpenIOTarget(
    PDEVICE_CONTEXT pDeviceContext, LARGE_INTEGER Resource, ACCESS_MASK AccessMask,
    WDFIOTARGET *IoTarget)
{
  NTSTATUS                  status = STATUS_SUCCESS;
  WDF_OBJECT_ATTRIBUTES     ObjectAttributes;
  WDF_IO_TARGET_OPEN_PARAMS OpenParams;
  UNICODE_STRING            ReadString;
  WCHAR                     ReadStringBuffer[260];

  TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "OpenIOTarget Entry");
  DbgPrintEx(DPFLTR_IHVBUS_ID, DPFLTR_INFO_LEVEL, "LumiaUSBCOpenIOTarget entry\n");

  RtlInitEmptyUnicodeString(
      &ReadString, ReadStringBuffer, sizeof(ReadStringBuffer));

  status =
      RESOURCE_HUB_CREATE_PATH_FROM_ID(&ReadString, Resource.LowPart, Resource.HighPart);
  if (!NT_SUCCESS(status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
        "RESOURCE_HUB_CREATE_PATH_FROM_ID failed 0x%x", status);
    goto Exit;
  }

  WDF_OBJECT_ATTRIBUTES_INIT(&ObjectAttributes);
  ObjectAttributes.ParentObject = pDeviceContext->Device;

  status = WdfIoTargetCreate(pDeviceContext->Device, &ObjectAttributes, IoTarget);
  if (!NT_SUCCESS(status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "WdfIoTargetCreate failed 0x%x",
        status);
    goto Exit;
  }

  WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&OpenParams, &ReadString, AccessMask);
  status = WdfIoTargetOpen(*IoTarget, &OpenParams);
  if (!NT_SUCCESS(status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "WdfIoTargetOpen failed 0x%x",
        status);
    goto Exit;
  }

Exit:
  DbgPrintEx(DPFLTR_IHVBUS_ID, DPFLTR_INFO_LEVEL, "LumiaUSBCOpenIOTarget exit: 0x%x\n", status);
  TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "OpenIOTarget Exit");
  return status;
}

NTSTATUS
LumiaUSBCProbeResources(
    PDEVICE_CONTEXT DeviceContext, WDFCMRESLIST ResourcesTranslated,
    WDFCMRESLIST ResourcesRaw)
{
  PAGED_CODE();
  UNREFERENCED_PARAMETER(ResourcesRaw);

  NTSTATUS                        Status        = STATUS_SUCCESS;
  PCM_PARTIAL_RESOURCE_DESCRIPTOR ResDescriptor = NULL;
  WDF_INTERRUPT_CONFIG            InterruptConfig;

  UCHAR SpiFound       = FALSE;
  ULONG GpioFound      = 0;
  ULONG interruptFound = 0;

  ULONG PlugDetInterrupt  = 0;
  ULONG UC120Interrupt    = 0;
  ULONG MysteryInterrupt1 = 0;
  ULONG MysteryInterrupt2 = 0;

  ULONG ResourceCount;

  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "-> LumiaUSBCProbeResources");
  DbgPrintEx(
      DPFLTR_IHVBUS_ID, DPFLTR_INFO_LEVEL, "LumiaUSBCProbeResources entry\n");

  ResourceCount = WdfCmResourceListGetCount(ResourcesTranslated);

  for (ULONG i = 0; i < ResourceCount; i++) {
    ResDescriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);

    switch (ResDescriptor->Type) {
    case CmResourceTypeConnection:
      // Check for GPIO resource
      if (ResDescriptor->u.Connection.Class ==
              CM_RESOURCE_CONNECTION_CLASS_GPIO &&
          ResDescriptor->u.Connection.Type ==
              CM_RESOURCE_CONNECTION_TYPE_GPIO_IO) {
        switch (GpioFound) {
        case 0:
          DeviceContext->VbusGpioId.LowPart =
              ResDescriptor->u.Connection.IdLowPart;
          DeviceContext->VbusGpioId.HighPart =
              ResDescriptor->u.Connection.IdHighPart;
          break;
        case 1:
          DeviceContext->PolGpioId.LowPart =
              ResDescriptor->u.Connection.IdLowPart;
          DeviceContext->PolGpioId.HighPart =
              ResDescriptor->u.Connection.IdHighPart;
          break;
        case 2:
          DeviceContext->AmselGpioId.LowPart =
              ResDescriptor->u.Connection.IdLowPart;
          DeviceContext->AmselGpioId.HighPart =
              ResDescriptor->u.Connection.IdHighPart;
          break;
        case 3:
          DeviceContext->EnGpioId.LowPart =
              ResDescriptor->u.Connection.IdLowPart;
          DeviceContext->EnGpioId.HighPart =
              ResDescriptor->u.Connection.IdHighPart;
          break;
        default:
          break;
        }

        DbgPrint(
            "LumiaUSBC: Found GPIO resource id=%lu index=%lu\n", GpioFound, i);

        GpioFound++;
      }
      // Check for SPI resource
      else if (ResDescriptor->u.Connection.Class ==
              CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
          ResDescriptor->u.Connection.Type ==
              CM_RESOURCE_CONNECTION_TYPE_SERIAL_SPI) {
        DeviceContext->SpiId.LowPart  = ResDescriptor->u.Connection.IdLowPart;
        DeviceContext->SpiId.HighPart = ResDescriptor->u.Connection.IdHighPart;

        TraceEvents(
            TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
            "Found SPI resource index=%lu", i);

        SpiFound = TRUE;
      }
      break;
    case CmResourceTypeInterrupt:
      // We've found an interrupt resource.

      switch (interruptFound) {
      case 0:
        PlugDetInterrupt = i;
        break;
      case 1:
        UC120Interrupt = i;
        break;
      case 2:
        MysteryInterrupt1 = i;
        break;
      case 3:
        MysteryInterrupt2 = i;
        break;
      default:
        break;
      }

      TraceEvents(
          TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
          "Found Interrupt resource id=%lu index=%lu", interruptFound, i);

      interruptFound++;
      break;
    default:
      // We don't care about other descriptors.
      break;
    }
  }

  if (!SpiFound || GpioFound < 4 || interruptFound < 4) {
    DbgPrint(
        "LumiaUSBC: Not all resources were found, SPI = %d, GPIO = %d, Interrupts = %d", SpiFound,
        GpioFound, interruptFound);
    Status = STATUS_INSUFFICIENT_RESOURCES;
    goto Exit;
  }

  // Initialize all interrupts (consistent with IDA result)
  // UC120
  WDF_INTERRUPT_CONFIG_INIT(&InterruptConfig, EvtUc120InterruptIsr, NULL);
  InterruptConfig.EvtInterruptEnable  = Uc120InterruptEnable;
  InterruptConfig.EvtInterruptDisable = Uc120InterruptDisable;
  InterruptConfig.PassiveHandling     = TRUE;
  InterruptConfig.InterruptTranslated =
      WdfCmResourceListGetDescriptor(ResourcesTranslated, UC120Interrupt);
  InterruptConfig.InterruptRaw =
      WdfCmResourceListGetDescriptor(ResourcesRaw, UC120Interrupt);

  Status = WdfInterruptCreate(
      DeviceContext->Device, &InterruptConfig, NULL, &DeviceContext->Uc120Interrupt);

  if (!NT_SUCCESS(Status)) {
    TraceEvents(
        TRACE_LEVEL_ERROR, TRACE_DRIVER,
        "LumiaUSBCKmCreateDevice WdfInterruptCreate Uc120Interrupt failed: "
        "%!STATUS!\n",
        Status);
    goto Exit;
  }

  WdfInterruptSetPolicy(
      DeviceContext->Uc120Interrupt, WdfIrqPolicyAllProcessorsInMachine,
      WdfIrqPriorityHigh, 0);

  // Plug detection
  WDF_INTERRUPT_CONFIG_INIT(&InterruptConfig, EvtPlugDetInterruptIsr, NULL);
  InterruptConfig.PassiveHandling = TRUE;
  InterruptConfig.InterruptTranslated =
      WdfCmResourceListGetDescriptor(ResourcesTranslated, PlugDetInterrupt);
  InterruptConfig.InterruptRaw =
      WdfCmResourceListGetDescriptor(ResourcesRaw, PlugDetInterrupt);

  Status = WdfInterruptCreate(
      DeviceContext->Device, &InterruptConfig, NULL,
      &DeviceContext->PlugDetectInterrupt);

  if (!NT_SUCCESS(Status)) {
    TraceEvents(
        TRACE_LEVEL_ERROR, TRACE_DRIVER,
        "LumiaUSBCKmCreateDevice WdfInterruptCreate PlugDetectInterrupt "
        "failed: %!STATUS!\n",
        Status);
    goto Exit;
  }

  // PMIC1 Interrupt
  WDF_INTERRUPT_CONFIG_INIT(&InterruptConfig, EvtPmicInterrupt1Isr, NULL);
  InterruptConfig.PassiveHandling      = TRUE;
  InterruptConfig.EvtInterruptWorkItem = PmicInterrupt1WorkItem;
  InterruptConfig.InterruptTranslated =
      WdfCmResourceListGetDescriptor(ResourcesTranslated, MysteryInterrupt1);
  InterruptConfig.InterruptRaw =
      WdfCmResourceListGetDescriptor(ResourcesRaw, MysteryInterrupt1);

  Status = WdfInterruptCreate(
      DeviceContext->Device, &InterruptConfig, NULL,
      &DeviceContext->PmicInterrupt1);

  if (!NT_SUCCESS(Status)) {
    TraceEvents(
        TRACE_LEVEL_ERROR, TRACE_DRIVER,
        "LumiaUSBCKmCreateDevice WdfInterruptCreate PmicInterrupt1 failed: "
        "%!STATUS!\n",
        Status);
    goto Exit;
  }

  // PMIC2 Interrupt
  WDF_INTERRUPT_CONFIG_INIT(&InterruptConfig, EvtPmicInterrupt2Isr, NULL);
  InterruptConfig.PassiveHandling = TRUE;
  InterruptConfig.InterruptTranslated =
      WdfCmResourceListGetDescriptor(ResourcesTranslated, MysteryInterrupt2);
  InterruptConfig.InterruptRaw =
      WdfCmResourceListGetDescriptor(ResourcesRaw, MysteryInterrupt2);

  Status = WdfInterruptCreate(
      DeviceContext->Device, &InterruptConfig, NULL,
      &DeviceContext->PmicInterrupt2);

  if (!NT_SUCCESS(Status)) {
    TraceEvents(
        TRACE_LEVEL_ERROR, TRACE_DRIVER,
        "LumiaUSBCKmCreateDevice WdfInterruptCreate PmicInterrupt2 failed: "
        "%!STATUS!\n",
        Status);
    goto Exit;
  }

  Exit:
  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "<- LumiaUSBCProbeResources");
  DbgPrintEx(
      DPFLTR_IHVBUS_ID, DPFLTR_INFO_LEVEL, "LumiaUSBCProbeResources exit: 0x%x\n",
      Status);
  return Status;
}

NTSTATUS
LumiaUSBCOpenResources(PDEVICE_CONTEXT pDeviceContext)
{
  NTSTATUS status = STATUS_SUCCESS;
  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "LumiaUSBCOpenResources Entry");
  DbgPrintEx(
      DPFLTR_IHVBUS_ID, DPFLTR_INFO_LEVEL, "LumiaUSBCOpenResources entry\n");

  status = LumiaUSBCOpenIOTarget(
      pDeviceContext, pDeviceContext->SpiId, GENERIC_READ | GENERIC_WRITE,
      &pDeviceContext->Spi);
  if (!NT_SUCCESS(status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
        "OpenIOTarget failed for SPI 0x%x Falling back to fake SPI.", status);
    goto Exit;
  }

  	status = LumiaUSBCOpenIOTarget(
      pDeviceContext, pDeviceContext->VbusGpioId, GENERIC_READ | GENERIC_WRITE,
      &pDeviceContext->VbusGpio);
  if (!NT_SUCCESS(status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
        "OpenIOTarget failed for VBUS GPIO 0x%x", status);
    goto Exit;
  }

  status = LumiaUSBCOpenIOTarget(
      pDeviceContext, pDeviceContext->PolGpioId, GENERIC_READ | GENERIC_WRITE,
      &pDeviceContext->PolGpio);
  if (!NT_SUCCESS(status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
        "OpenIOTarget failed for polarity GPIO 0x%x", status);
    goto Exit;
  }

  status = LumiaUSBCOpenIOTarget(
      pDeviceContext, pDeviceContext->AmselGpioId, GENERIC_READ | GENERIC_WRITE,
      &pDeviceContext->AmselGpio);
  if (!NT_SUCCESS(status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
        "OpenIOTarget failed for alternate mode selection GPIO 0x%x", status);
    goto Exit;
  }

  status = LumiaUSBCOpenIOTarget(
      pDeviceContext, pDeviceContext->EnGpioId, GENERIC_READ | GENERIC_WRITE,
      &pDeviceContext->EnGpio);
  if (!NT_SUCCESS(status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
        "OpenIOTarget failed for mux enable GPIO 0x%x", status);
    goto Exit;
  }


Exit:
  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "LumiaUSBCOpenResources Exit");
  DbgPrintEx(
      DPFLTR_IHVBUS_ID, DPFLTR_INFO_LEVEL,
      "LumiaUSBCOpenResources exit: 0x%x\n", status);
  return status;
}

NTSTATUS
LumiaUSBCKmCreateDevice(_Inout_ PWDFDEVICE_INIT DeviceInit)
{
  WDF_OBJECT_ATTRIBUTES        DeviceAttributes;
  WDF_OBJECT_ATTRIBUTES        ObjAttrib;
  WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;
  PDEVICE_CONTEXT              DeviceContext;
  WDFDEVICE                    Device;
  UCM_MANAGER_CONFIG           UcmConfig;
  NTSTATUS                     Status;

  PAGED_CODE();

  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "LumiaUSBCKmCreateDevice Entry");
  DbgPrintEx(
      DPFLTR_IHVBUS_ID, DPFLTR_INFO_LEVEL, "LumiaUSBCKmCreateDevice entry\n");

  WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
  PnpPowerCallbacks.EvtDevicePrepareHardware = LumiaUSBCDevicePrepareHardware;
  PnpPowerCallbacks.EvtDeviceReleaseHardware = LumiaUSBCDeviceReleaseHardware;
  PnpPowerCallbacks.EvtDeviceD0Entry         = LumiaUSBCDeviceD0Entry;
  WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPowerCallbacks);

  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&DeviceAttributes, DEVICE_CONTEXT);

  Status = WdfDeviceCreate(&DeviceInit, &DeviceAttributes, &Device);

  if (NT_SUCCESS(Status)) {
    DeviceContext = DeviceGetContext(Device);

    //
    // Initialize the context.
    //
    DeviceContext->Device              = Device;
    DeviceContext->State3              = 4;
    DeviceContext->PdStateMachineIndex = 7;
    DeviceContext->State0   = TRUE;
    DeviceContext->Polarity            = 0;
    DeviceContext->PowerSource         = 2;
    DeviceContext->Connector           = NULL;

    UCM_MANAGER_CONFIG_INIT(&UcmConfig);
    Status = UcmInitializeDevice(Device, &UcmConfig);
    if (!NT_SUCCESS(Status)) {
      TraceEvents(
          TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
          "LumiaUSBCKmCreateDevice Exit: 0x%x\n", Status);
      goto Exit;
    }

    // DeviceColletion
    WDF_OBJECT_ATTRIBUTES_INIT(&ObjAttrib);
    ObjAttrib.ParentObject = Device;

    Status = WdfCollectionCreate(&ObjAttrib, &DeviceContext->DevicePendingIoReqCollection);
    if (!NT_SUCCESS(Status)) {
      TraceEvents(
          TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
          "LumiaUSBCKmCreateDevice failed to WdfCollectionCreate: %!STATUS!\n",
          Status);
      goto Exit;
    }

    // Waitlock
    WDF_OBJECT_ATTRIBUTES_INIT(&ObjAttrib);
    ObjAttrib.ParentObject = Device;

    Status = WdfWaitLockCreate(&ObjAttrib, &DeviceContext->DeviceWaitLock);

    if (!NT_SUCCESS(Status)) {
      TraceEvents(
          TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
          "LumiaUSBCKmCreateDevice failed to WdfWaitLockCreate: %!STATUS!\n",
          Status);
      goto Exit;
    }

    // IO Queue
    Status = LumiaUSBCInitializeIoQueue(Device);
    if (!NT_SUCCESS(Status)) {
      TraceEvents(
          TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
          "LumiaUSBCKmCreateDevice failed to LumiaUSBCInitializeIoQueue: %!STATUS!\n",
          Status);
      goto Exit;
    }
  }

Exit:
  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
      "LumiaUSBCKmCreateDevice Exit: 0x%x\n", Status);
  DbgPrintEx(
      DPFLTR_IHVBUS_ID, DPFLTR_INFO_LEVEL,
      "LumiaUSBCKmCreateDevice exit: 0x%x\n", Status);
  return Status;
}

NTSTATUS
LumiaUSBCSetDataRole(UCMCONNECTOR Connector, UCM_DATA_ROLE DataRole)
{
  PCONNECTOR_CONTEXT connCtx;

  // UNREFERENCED_PARAMETER(DataRole);

  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "LumiaUSBCSetDataRole Entry");

  connCtx = ConnectorGetContext(Connector);

  RtlWriteRegistryValue(
      RTL_REGISTRY_ABSOLUTE, (PCWSTR)L"\\Registry\\Machine\\System\\usbc",
      L"DataRoleRequested", REG_DWORD, &DataRole, sizeof(ULONG));

  UcmConnectorDataDirectionChanged(Connector, TRUE, DataRole);

  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "LumiaUSBCSetDataRole Exit");

  return STATUS_SUCCESS;
}

NTSTATUS
LumiaUSBCSetPowerRole(UCMCONNECTOR Connector, UCM_POWER_ROLE PowerRole)
{
  PCONNECTOR_CONTEXT connCtx;

  // UNREFERENCED_PARAMETER(PowerRole);

  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "LumiaUSBCSetPowerRole Entry");

  connCtx = ConnectorGetContext(Connector);

  RtlWriteRegistryValue(
      RTL_REGISTRY_ABSOLUTE, (PCWSTR)L"\\Registry\\Machine\\System\\usbc",
      L"PowerRoleRequested", REG_DWORD, &PowerRole, sizeof(ULONG));

  UcmConnectorPowerDirectionChanged(Connector, TRUE, PowerRole);

  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "LumiaUSBCSetPowerRole Exit");

  return STATUS_SUCCESS;
}

NTSTATUS
LumiaUSBCDevicePrepareHardware(
    WDFDEVICE Device, WDFCMRESLIST ResourcesRaw,
    WDFCMRESLIST ResourcesTranslated)
{
  NTSTATUS                   Status = STATUS_SUCCESS;
  PDEVICE_CONTEXT            pDeviceContext;
  UCM_MANAGER_CONFIG         ucmCfg;
  UCM_CONNECTOR_CONFIG       connCfg;
  UCM_CONNECTOR_TYPEC_CONFIG typeCConfig;
  UCM_CONNECTOR_PD_CONFIG    pdConfig;
  WDF_OBJECT_ATTRIBUTES      attr;

  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
      "LumiaUSBCDevicePrepareHardware Entry");
  DbgPrintEx(
      DPFLTR_IHVBUS_ID, DPFLTR_INFO_LEVEL, "LumiaUSBCDevicePrepareHardware entry\n");

  pDeviceContext = DeviceGetContext(Device);

  Status = LumiaUSBCProbeResources(
      pDeviceContext, ResourcesTranslated, ResourcesRaw);

  if (!NT_SUCCESS(Status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
        "LumiaUSBCProbeResources failed 0x%x\n", Status);
    goto Exit;
  }

  // Open SPI device
  Status = LumiaUSBCOpenResources(pDeviceContext);
  if (!NT_SUCCESS(Status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
        "LumiaUSBCOpenResources failed 0x%x\n", Status);
    goto Exit;
  }

  // Create Device Interface
  Status = WdfDeviceCreateDeviceInterface(
      Device, &GUID_DEVINTERFACE_LumiaUSBCKm, NULL);
  if (!NT_SUCCESS(Status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
        "WdfDeviceCreateDeviceInterface failed 0x%x\n", Status);
    goto Exit;
  }

  // Initialize PD event
  KeInitializeEvent(&pDeviceContext->PdEvent, NotificationEvent, FALSE);

  if (pDeviceContext->Connector) {
    goto Exit;
  }

  ///
  // Initialize UCM Manager
  //
  UCM_MANAGER_CONFIG_INIT(&ucmCfg);

  Status = UcmInitializeDevice(Device, &ucmCfg);
  if (!NT_SUCCESS(Status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
        "UcmInitializeDevice failed 0x%x\n", Status);
    goto Exit;
  }

  //
  // Assemble the Type-C and PD configuration for UCM.
  //

  UCM_CONNECTOR_CONFIG_INIT(&connCfg, 0);

  UCM_CONNECTOR_TYPEC_CONFIG_INIT(
      &typeCConfig,
      UcmTypeCOperatingModeDrp | UcmTypeCOperatingModeUfp |
          UcmTypeCOperatingModeDfp,
      UcmTypeCCurrent3000mA | UcmTypeCCurrent1500mA |
          UcmTypeCCurrentDefaultUsb);

  typeCConfig.EvtSetDataRole        = LumiaUSBCSetDataRole;
  typeCConfig.AudioAccessoryCapable = FALSE;

  UCM_CONNECTOR_PD_CONFIG_INIT(
      &pdConfig, UcmPowerRoleSink | UcmPowerRoleSource);

  pdConfig.EvtSetPowerRole = LumiaUSBCSetPowerRole;

  connCfg.TypeCConfig = &typeCConfig;
  connCfg.PdConfig    = &pdConfig;

  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, CONNECTOR_CONTEXT);

  //
  // Create the UCM connector object.
  //

  Status = UcmConnectorCreate(Device, &connCfg, &attr, &pDeviceContext->Connector);
  if (!NT_SUCCESS(Status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
        "UcmConnectorCreate failed 0x%x\n", Status);
    goto Exit;
  }

  Status = LumiaUSBCEvaluateManualMode(pDeviceContext);

Exit:
  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
      "LumiaUSBCDevicePrepareHardware Exit");
  DbgPrintEx(
      DPFLTR_IHVBUS_ID, DPFLTR_INFO_LEVEL,
      "LumiaUSBCDevicePrepareHardware exit: 0x%x\n", Status);
  return Status;
}

NTSTATUS
LumiaUSBCEvaluateManualMode(PDEVICE_CONTEXT pDeviceContext)
{
  unsigned char vbus = (unsigned char)0;
  unsigned char polarity = (unsigned char)0;
  unsigned long target   = UcmTypeCPartnerDfp;

  if (!NT_SUCCESS(LocalReadRegistryValue(
          (PCWSTR)L"\\Registry\\Machine\\System\\usbc", (PCWSTR)L"Polarity",
          REG_DWORD, &polarity, sizeof(ULONG)))) {
    polarity = 0;
  }

  if (!NT_SUCCESS(LocalReadRegistryValue(
          (PCWSTR)L"\\Registry\\Machine\\System\\usbc", (PCWSTR)L"Target",
          REG_DWORD, &target, sizeof(ULONG)))) {
    target = UcmTypeCPartnerDfp;
  }

  if (!NT_SUCCESS(LocalReadRegistryValue(
          (PCWSTR)L"\\Registry\\Machine\\System\\usbc", (PCWSTR)L"VbusEnable",
          REG_DWORD, &vbus, sizeof(ULONG)))) {
    vbus = 0;
  }

  if (vbus) {
    target = UcmTypeCPartnerUfp;
  }

  return USBC_ChangeRole(pDeviceContext, target, polarity);
}

NTSTATUS
LumiaUSBCDeviceD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
  UNREFERENCED_PARAMETER(PreviousState);

  NTSTATUS        Status         = STATUS_SUCCESS;
  PDEVICE_CONTEXT pDeviceContext = DeviceGetContext(Device);

  LARGE_INTEGER Delay;

  UNICODE_STRING            CalibrationFileString;
  OBJECT_ATTRIBUTES         CalibrationFileObjectAttribute;
  HANDLE                    hCalibrationFile;
  IO_STATUS_BLOCK           CalibrationIoStatusBlock;
  UCHAR                     CalibrationBlob[UC120_CALIBRATIONFILE_SIZE + 2];
  LARGE_INTEGER             CalibrationFileByteOffset;
  FILE_STANDARD_INFORMATION CalibrationFileInfo;
  // { 0x0C, 0x7C, 0x31, 0x5E, 0x9D, 0x0D, 0x7D, 0x32, 0x5F, 0x9E };
  UCHAR DefaultCalibrationBlob[] = {0x0C, 0x7C, 0x31, 0x5E, 0x9D,
                                    0x0A, 0x7A, 0x2F, 0x5C, 0x9B};

  LONGLONG CalibrationFileSize = 0;
  UCHAR  SkipCalibration     = FALSE;

  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "LumiaUSBCDeviceD0Entry Entry");
  DbgPrintEx(
      DPFLTR_IHVBUS_ID, DPFLTR_INFO_LEVEL,
      "LumiaUSBCDeviceD0Entry entry\n");

  unsigned char value = (unsigned char)0;

  SetGPIO(pDeviceContext, pDeviceContext->PolGpio, &value);

  value = (unsigned char)0;
  SetGPIO(
      pDeviceContext, pDeviceContext->AmselGpio,
      &value); // high = HDMI only, medium (unsupported) = USB only, low = both

  value = (unsigned char)1;
  SetGPIO(pDeviceContext, pDeviceContext->EnGpio, &value);

  // Read calibration file
  RtlInitUnicodeString(
      &CalibrationFileString, L"\\DosDevices\\C:\\DPP\\MMO\\ice5lp_2k_cal.bin");
  InitializeObjectAttributes(
      &CalibrationFileObjectAttribute, &CalibrationFileString,
      OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

  // Should not happen
  if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
    Status = STATUS_INVALID_DEVICE_STATE;
    goto Exit;
  }

  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "acquire calibration file handle");
  Status = ZwCreateFile(
      &hCalibrationFile, GENERIC_READ, &CalibrationFileObjectAttribute,
      &CalibrationIoStatusBlock, NULL, FILE_ATTRIBUTE_NORMAL, 0, FILE_OPEN,
      FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

  if (!NT_SUCCESS(Status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
        "failed to open calibration file 0x%x, skipping calibration. Is this a "
        "FUSBC device?",
        Status);
    Status          = STATUS_SUCCESS;
    SkipCalibration = TRUE;
  }

  if (!SkipCalibration) {
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "stat calibration file");
    Status = ZwQueryInformationFile(
        hCalibrationFile, &CalibrationIoStatusBlock, &CalibrationFileInfo,
        sizeof(CalibrationFileInfo), FileStandardInformation);

    if (!NT_SUCCESS(Status)) {
      TraceEvents(
          TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
          "failed to stat calibration file 0x%x", Status);
      ZwClose(hCalibrationFile);
      goto Exit;
    }

    CalibrationFileSize = CalibrationFileInfo.EndOfFile.QuadPart;
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "calibration file size %lld",
        CalibrationFileSize);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "read calibration file");
    RtlZeroMemory(CalibrationBlob, sizeof(CalibrationBlob));
    CalibrationFileByteOffset.LowPart  = 0;
    CalibrationFileByteOffset.HighPart = 0;
    Status                             = ZwReadFile(
        hCalibrationFile, NULL, NULL, NULL, &CalibrationIoStatusBlock,
        CalibrationBlob, UC120_CALIBRATIONFILE_SIZE, &CalibrationFileByteOffset,
        NULL);

    ZwClose(hCalibrationFile);
    if (!NT_SUCCESS(Status)) {
      TraceEvents(
          TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
          "failed to read calibration file 0x%x", Status);
      goto Exit;
    }
  }

  // UC120 init sequence
  pDeviceContext->Register4 |= 6;
  Status = WriteRegister(
      pDeviceContext, 4, &pDeviceContext->Register4,
      sizeof(pDeviceContext->Register4));
  if (!NT_SUCCESS(Status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
        "Initseq Write Register 4 failed 0x%x", Status);
    goto Exit;
  }

  pDeviceContext->Register5 = 0x88;
  Status                    = WriteRegister(
      pDeviceContext, 5, &pDeviceContext->Register5,
      sizeof(pDeviceContext->Register5));
  if (!NT_SUCCESS(Status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
        "Initseq Write Register 5 failed 0x%x", Status);
    goto Exit;
  }

  pDeviceContext->Register13 = pDeviceContext->Register13 & 0xFC | 2;
  Status                     = WriteRegister(
      pDeviceContext, 13, &pDeviceContext->Register13,
      sizeof(pDeviceContext->Register13));
  if (!NT_SUCCESS(Status)) {
    TraceEvents(
        TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
        "Initseq Write Register 13 failed 0x%x", Status);
    goto Exit;
  }

  if (!SkipCalibration) {
    // Initialize the UC120 accordingly
    if (CalibrationFileSize == 11) {
      // Skip the first byte
      Status =
          UC120_UploadCalibrationData(pDeviceContext, &CalibrationBlob[1], 10);
    }
    else if (CalibrationFileSize == 8) {
      // No skip
      Status = UC120_UploadCalibrationData(pDeviceContext, CalibrationBlob, 8);
    }
    else {
      // Not recognized, use default
      TraceEvents(
          TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
          "Unknown calibration data, fallback to the default");
      Status = UC120_UploadCalibrationData(
          pDeviceContext, DefaultCalibrationBlob,
          sizeof(DefaultCalibrationBlob));
    }

    if (!NT_SUCCESS(Status)) {
      TraceEvents(
          TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
          "UC120_UploadCalibrationData failed 0x%x", Status);
      goto Exit;
    }

    // TODO: understand what's this
    pDeviceContext->State9 = 1;
  }

  Delay.QuadPart = -2000000;
  KeDelayExecutionThread(UserMode, TRUE, &Delay);

  RtlWriteRegistryValue(
      RTL_REGISTRY_ABSOLUTE, (PCWSTR)L"\\Registry\\Machine\\System\\usbc",
      L"Initialized", REG_DWORD, &value, sizeof(ULONG));

Exit:
  DbgPrintEx(
      DPFLTR_IHVBUS_ID, DPFLTR_INFO_LEVEL,
      "LumiaUSBCDeviceD0Entry exit: 0x%x\n", Status);
  TraceEvents(
      TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "LumiaUSBCDeviceD0Entry Exit");
  return Status;
}

