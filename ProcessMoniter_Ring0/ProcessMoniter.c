﻿#include "ntddk.h"
#include "windef.h"
#include "define.h"

#define SYSNAME "System"
#define VERSIONLEN 100

const WCHAR devLink[] = L"\\DosDevices\\MonitorProcess";
const WCHAR devName[] = L"\\Device\\MonitorProcess";
UNICODE_STRING           devNameUnicd;
UNICODE_STRING           devLinkUnicd;
PVOID                    gpEventObject = NULL;	// 与应用程序通信的 Event 对象
ULONG                    ProcessNameOffset = 0;
CHAR                     outBuf[255];
BOOL                     g_bMainThread;
ULONG                    g_dwParentId;
CHECKLIST                CheckList;
ULONG                    BuildNumber;	//系统版本号        
ULONG                    SYSTEMID;      //System进程的ID
PWCHAR                   Version[VERSIONLEN];

NTSTATUS PsLookupProcessByProcessId(IN ULONG ulProcId, OUT PEPROCESS* pEProcess);


/*
通过PsGetCurrentProcess函数来获取当前调用驱动的进程的EPROCESS结构地址的0x174偏移处存放的进程名.
思路如下:
驱动程序的加载函数DriverEntry是运行在System进程中的．
(1) 通过PsGetCurrentProcess可以获取System进程的内核EPROCESS结构的地址,
(2) 从该地址开始寻找"System"字符串．
(3) 找到了便是EPROCESS的进程名存放的偏移处, 得到进程名在EPROCESS结构的偏移后,
(4) 进程调用驱动的时候, 就可以直接在该偏移处获取当前进程名．
*/
ULONG GetProcessNameOffset()
{
	PEPROCESS curproc;
	int i; 

	curproc = PsGetCurrentProcess();

	for (i = 0; i < 3 * PAGE_SIZE; i++)
	{	//如果当前进程
		if (0 == strncmp(SYSNAME, (PCHAR)curproc + i, strlen(SYSNAME)))
		{
			return i;
		}
	}

	return 0;
}

NTSTATUS GetRegValue(PCWSTR RegPath, PCWSTR ValueName, PWCHAR Value)
{
	int ReturnValue = 0;
	NTSTATUS Status;
	OBJECT_ATTRIBUTES ObjectAttributes;
	HANDLE KeyHandle;
	PKEY_VALUE_PARTIAL_INFORMATION valueInfoP;
	ULONG valueInfoLength, returnLength;
	UNICODE_STRING UnicodeRegPath;
	UNICODE_STRING UnicodeValueName;

	RtlInitUnicodeString(&UnicodeRegPath, RegPath);
	RtlInitUnicodeString(&UnicodeValueName, ValueName);

	InitializeObjectAttributes(&ObjectAttributes, &UnicodeRegPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

	Status = ZwOpenKey(&KeyHandle,KEY_ALL_ACCESS,&ObjectAttributes);
	if (Status != STATUS_SUCCESS)
	{
		DbgPrint("zwopenkey error\n");
		return 0;
	}

	valueInfoLength = sizeof(KEY_VALUE_PARTIAL_INFORMATION) + VERSIONLEN;
	valueInfoP = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePool(NonPagedPool, valueInfoLength);
	Status = ZwQueryValueKey(KeyHandle, &UnicodeValueName, KeyValuePartialInformation, valueInfoP, valueInfoLength, &returnLength);

	if (!NT_SUCCESS(Status))
	{
		DbgPrint("zwqueryvaluekey error:%08x\n", Status);
		ExFreePool(valueInfoP);
		ZwClose(KeyHandle);
		return Status;
	}
	else
	{
		RtlCopyMemory((PCHAR)Value, (PCHAR)valueInfoP->Data, valueInfoP->DataLength);
		ReturnValue = 1;
	}

	ExFreePool(valueInfoP);
	ZwClose(KeyHandle);
	return ReturnValue;
}



VOID ThreadCreateMon(IN HANDLE PId, IN HANDLE TId, IN BOOLEAN  bCreate)
{

	PEPROCESS   EProcess, PEProcess;
	NTSTATUS    status;
	HANDLE        dwParentPID;

	status = PsLookupProcessByProcessId((ULONG)PId, &EProcess);
	if (!NT_SUCCESS(status))
	{
		DbgPrint("error\n");
		return;
	}

	if (bCreate)
	{
		dwParentPID = PsGetCurrentProcessId();
		status = PsLookupProcessByProcessId((ULONG)dwParentPID, &PEProcess); if (!NT_SUCCESS(status))
		{
			ObDereferenceObject(EProcess);
			DbgPrint("error\n");
			return;
		}
		if (PId == 4)//System进程创建的不管
		{
			ObDereferenceObject(PEProcess);
			ObDereferenceObject(EProcess);
			return;
		}
		if ((g_bMainThread == TRUE) && (g_dwParentId != dwParentPID) && (dwParentPID != PId))
		{
			g_bMainThread = FALSE;
			sprintf(outBuf, "r_thread|%s|%d|%s|%d\n", (char*)((char*)EProcess + ProcessNameOffset), PId, (char*)((char*)PEProcess + ProcessNameOffset), dwParentPID);
			if (gpEventObject != NULL)
			{
				KeSetEvent((PRKEVENT)gpEventObject, 0, FALSE);
			}
		}
		if (CheckList.ONLYSHOWREMOTETHREAD)//只显示远线程
		{
			ObDereferenceObject(PEProcess);
			ObDereferenceObject(EProcess);
			return;
		}
		DbgPrint("thread|%s|%d|%s|%d\n", (char*)((char*)EProcess + ProcessNameOffset), PId, (char*)((char*)PEProcess + ProcessNameOffset), dwParentPID);
		sprintf(outBuf, "thread|%s|%d|%s|%d\n", (char*)((char*)EProcess + ProcessNameOffset), PId, (char*)((char*)PEProcess + ProcessNameOffset), dwParentPID);
		if (gpEventObject != NULL)
			KeSetEvent((PRKEVENT)gpEventObject, 0, FALSE);

		ObDereferenceObject(PEProcess);
	}
	else if (CheckList.SHOWTERMINATETHREAD)
	{
		DbgPrint("thread_over|%d\n", TId);
		sprintf(outBuf, "thread_over|%d\n", TId);
		if (gpEventObject != NULL)
			KeSetEvent((PRKEVENT)gpEventObject, 0, FALSE);
	}
	ObDereferenceObject(EProcess);
}

//进程创建回调函数，PsSetCreateProcessNotifyRoutine第二参数为是否Remove，若为True，则移除，为False则注册。
/*
	void PcreateProcessNotifyRoutine(HANDLE ParentId, HANDLE ProcessId, BOOLEAN Create)
	{
	}
*/
VOID ProcessCreateMon(HANDLE hParentId, HANDLE PId, BOOLEAN bCreate)
{

	PEPROCESS        EProcess, PProcess;//EPROCESS结构是一个不透明的结构，它充当进程的进程对象
	NTSTATUS        status;
	HANDLE            TId;

	g_dwParentId = hParentId;
	//Pid------ > Object(EPROCESS)原理就是通过 PsLookUpProcessByProcessId 传入PID.传出EPROCESS.
	status = PsLookupProcessByProcessId((ULONG)PId, &EProcess);
	if (!NT_SUCCESS(status))
	{
		DbgPrint("error\n");
		return;
	}
	status = PsLookupProcessByProcessId((ULONG)hParentId, &PProcess);
	if (!NT_SUCCESS(status))
	{
		DbgPrint("error\n");
		ObDereferenceObject(EProcess);//对象创建(ObCreateObject)和对象删除(ObDereferenceObject、ObpRemoveObjectRoutine)
		return;
	}

	if (bCreate)//如果为True，则为进程创建，反之则为退出
	{
		g_bMainThread = TRUE;
		DbgPrint("process|%s|%d|%s|%d\n", (char*)((char*)EProcess + ProcessNameOffset), PId, (char*)((char*)PProcess + ProcessNameOffset), hParentId);
		sprintf(outBuf, "process|%s|%d|%s|%d\n", (char*)((char*)EProcess + ProcessNameOffset), PId, (char*)((char*)PProcess + ProcessNameOffset), hParentId);
		if (gpEventObject != NULL)
		{
			KeSetEvent((PRKEVENT)gpEventObject, 0, FALSE);
		}
	}
	else if (CheckList.SHOWTERMINATEPROCESS)//进程退出
	{
		DbgPrint("process_over|%d\n", PId);
		sprintf(outBuf, "process_over|%d\n", PId);
		if (gpEventObject != NULL)
		{
			KeSetEvent((PRKEVENT)gpEventObject, 0, FALSE);
		}
	}

	ObDereferenceObject(PProcess);//删除对象
	ObDereferenceObject(EProcess);
}

VOID ImageCreateMon(IN PUNICODE_STRING  FullImageName, IN HANDLE  ProcessId, IN PIMAGE_INFO  ImageInfo)
{
	do{
		//判断系统进程
		if (ProcessId == 4)break;
		//对于新加载的内核模式组件（如驱动程序），请设置为一，对于映射到用户空间的映像，设置为零。
		if (ImageInfo->SystemModeImage != TRUE)break;

	} while (FALSE);

	DbgPrint("FullImageName: %S,Process ID: %d\n", FullImageName->Buffer, ProcessId);
	DbgPrint("ImageBase: %x,ImageSize: %d\n", ImageInfo->ImageBase, ImageInfo->ImageSize);
	DbgPrint("ImageSignatureLevel:%d,ImageSignatureType:%d,SystemModeImage:%d", ImageInfo->ImageSignatureLevel, ImageInfo->ImageSignatureType, ImageInfo->SystemModeImage);
}

NTSTATUS OnUnload(IN PDRIVER_OBJECT pDriverObject)
{
	NTSTATUS            status;

	DbgPrint("OnUnload called\n");

	if (gpEventObject)
	{
		ObDereferenceObject(gpEventObject);
	}

	PsSetCreateProcessNotifyRoutine(ProcessCreateMon, TRUE);
	PsRemoveCreateThreadNotifyRoutine(ThreadCreateMon);
	IoDeleteSymbolicLink(&devLinkUnicd);

	if (pDriverObject->DeviceObject != NULL)
	{
		IoDeleteDevice(pDriverObject->DeviceObject);
	}

	return STATUS_SUCCESS;
}

NTSTATUS DeviceIoControlDispatch(
	IN  PDEVICE_OBJECT  DeviceObject,
	IN  PIRP            pIrp
)
{
	PIO_STACK_LOCATION              irpStack;
	NTSTATUS                        status;
	PVOID                           inputBuffer;
	ULONG                           inputLength;
	PVOID                           outputBuffer;
	ULONG                           outputLength;
	OBJECT_HANDLE_INFORMATION        objHandleInfo;

	status = STATUS_SUCCESS;
	irpStack = IoGetCurrentIrpStackLocation(pIrp);

	switch (irpStack->MajorFunction)
	{
	case IRP_MJ_CREATE:
		DbgPrint("Call IRP_MJ_CREATE\n");
		break;
	case IRP_MJ_CLOSE:
		DbgPrint("Call IRP_MJ_CLOSE\n");
		break;
	case IRP_MJ_DEVICE_CONTROL:
		DbgPrint("IRP_MJ_DEVICE_CONTROL\n");
		inputLength = irpStack->Parameters.DeviceIoControl.InputBufferLength;
		outputLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
		switch (irpStack->Parameters.DeviceIoControl.IoControlCode)
		{
		case IOCTL_PASSEVENT://用事件做通信
			inputBuffer = pIrp->AssociatedIrp.SystemBuffer;

			DbgPrint("inputBuffer:%08x\n", (HANDLE)inputBuffer);
			status = ObReferenceObjectByHandle(*(HANDLE*)inputBuffer,
				GENERIC_ALL,
				NULL,
				KernelMode,
				&gpEventObject,
				&objHandleInfo);

			if (status != STATUS_SUCCESS)
			{
				DbgPrint("wrong\n");
				break;
			}
			break;
		case IOCTL_UNPASSEVENT:
			if (gpEventObject)
				ObDereferenceObject(gpEventObject);
			DbgPrint("UNPASSEVENT called\n");
			break;
		case IOCTL_PASSBUF:
			RtlCopyMemory(pIrp->UserBuffer, outBuf, outputLength);
			break;
		case IOCTL_PASSEVSTRUCT:
			inputBuffer = pIrp->AssociatedIrp.SystemBuffer;
			memset(&CheckList, 0, sizeof(CheckList));
			RtlCopyMemory(&CheckList, inputBuffer, sizeof(CheckList));
			DbgPrint("%d:%d\n", CheckList.ONLYSHOWREMOTETHREAD, CheckList.SHOWTHREAD);
			break;
		default:
			break;
		}
		break;
	default:
		DbgPrint("Call IRP_MJ_UNKNOWN\n");
		break;
	}

	pIrp->IoStatus.Status = status;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return status;
}

//入口函数(DriverEntry)：驱动的入口函数主要是对驱动程序进行初始化工作，它是由系统进程所调用。在驱动程序初始化的时候，入口函数被加载进内存，进行初始化，完成之后，就要退出内存。
NTSTATUS DriverEntry(IN PDRIVER_OBJECT pDriverObject, IN PUNICODE_STRING theRegistryPath)
{
	NTSTATUS                Status;
	PDEVICE_OBJECT            pDevice;

	DbgPrint("DriverEntry called!\n");
	g_bMainThread = FALSE;
	//清空outBuf数组,将数组的所有位都置为0
	memset(outBuf, 0, 255);
	//读取注册表键值并赋值到Version
	if (1 != GetRegValue(L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CSDVersion", Version))
	{
		DbgPrint("GetRegValueDword Wrong\r\n");
	}
	//获取操作系统版本号
	PsGetVersion(NULL, NULL, &BuildNumber, NULL);
	DbgPrint("[[[%d]]]:[[[%ws]]]", BuildNumber, Version);

	RtlInitUnicodeString(&devNameUnicd, devName);//WCHAR转换为UNICODE_STRING结构体
	RtlInitUnicodeString(&devLinkUnicd, devLink);
	//创建设备
	Status = IoCreateDevice(pDriverObject,//驱动程序对象
		0,//扩展设备的大小，由于不需要，所以为0
		&devNameUnicd,//设备名称
		FILE_DEVICE_UNKNOWN,//设备类型
		0,//指示设备允许的操作
		TRUE,//若为TRUE，表示只能由一个线程使用该设备，反之则没有限制
		&pDevice//返回的设备对象
	);
	if (!NT_SUCCESS(Status))
	{
		DbgPrint(("Can not create device.\n"));
		goto out;
	}
	//创建符号链接
	Status = IoCreateSymbolicLink(&devLinkUnicd,//存放符号链接的UNICODE_STRING
		&devNameUnicd//设备名称
	);
	//如果创建符号链接失败则卸载驱动
	if (!NT_SUCCESS(Status))
	{
		DbgPrint(("Cannot create link.\n"));
		goto CleanDevice;
	}
	//通过PsGetCurrentProcess函数来获取当前调用驱动的进程的EPROCESS结构地址的0x174偏移处存放的进程名.
	ProcessNameOffset = GetProcessNameOffset();
	//指定卸载例程函数
	pDriverObject->DriverUnload = OnUnload;

	//注册分发例程
	pDriverObject->MajorFunction[IRP_MJ_CREATE] = pDriverObject->MajorFunction[IRP_MJ_CLOSE] = pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceIoControlDispatch;

	//注册Windows进程创建回调函数，当有进程创建时会调用ProcessCreateMon()回调函数
	/*
	NTSTATUS PsSetCreateProcessNotifyRoutine(PCREATE_PROCESS_NOTIFY_ROUTINE NotifyRoutine, BOOLEAN Remove);
	PCREATE_PROCESS_NOTIFY_ROUTINE PcreateProcessNotifyRoutine;
	*/
	Status = PsSetCreateProcessNotifyRoutine(ProcessCreateMon, FALSE);
	if (!NT_SUCCESS(Status))
	{
		//如果未调用成功则移除回调函数，PsSetCreateProcessNotifyRoutine第二参数为是否Remove，若为True，则移除，为False则注册。
		DbgPrint("PsSetCreateProcessNotifyRoutine error\n");
		goto CleanSymbolLink;
	}
	//注册Windows进程创建回调函数，当有线程创建时会调用ProcessCreateMon()回调函数
	Status = PsSetCreateThreadNotifyRoutine(ThreadCreateMon);
	if (!NT_SUCCESS(Status))
	{
		//如果未调用成功则移除回调函数，PsSetCreateThreadNotifyRoutine第二参数为是否Remove，若为True，则移除，为False则注册。
		DbgPrint("PsSetCreateThreadNotifyRoutine error\n");
		goto CleanTreadNotify;
	}
	Status = PsSetLoadImageNotifyRoutine(ImageCreateMon);
	if (!NT_SUCCESS(Status))
	{
		DbgPrint("PsSetLoadImageNotifyRoutine error\n");
		goto CleanImageNotify;
		//return Status;
	}
	return STATUS_SUCCESS;
CleanTreadNotify:
	PsRemoveCreateThreadNotifyRoutine(ThreadCreateMon);
CleanImageNotify:
	PsRemoveLoadImageNotifyRoutine(ImageCreateMon);
CleanProcessNotify:
	PsSetCreateProcessNotifyRoutine(ProcessCreateMon, TRUE);
CleanSymbolLink:
	IoDeleteSymbolicLink(&devLinkUnicd);
CleanDevice:
	if (pDriverObject->DeviceObject != NULL)
	{
		IoDeleteDevice(pDriverObject->DeviceObject);
	}
out:
	return Status;
}