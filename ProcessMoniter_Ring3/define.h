#pragma once
#include <stdio.h>
#define FILE_DEVICE_EVENT  0x8000

//CTL_CODE宏共四个参数，32位,分成了4部分，存储设备类型，控制码，访问权限，操作功能，
//最后0，1两位就是确定缓冲区是如何与I/O和文件系统数据缓冲区进行数据传递方式，最常见的就是METHOD_BUFFERED。
//Function ：自定义的IO控制码。自己定义时取0x800到0xFFF，因为0x0到0x7FF是微软保留的。
//Method ：数据的操作模式。
//METHOD_BUFFERED：缓冲区模式
//METHOD_IN_DIRECT：直接写模式
//METHOD_OUT_DIRECT：直接读模式
//METHOD_NEITHER ：Neither模式
//Access：访问权限，可取值有：
//FILE_ANY_ACCESS：表明用户拥有所有的权限
//FILE_READ_DATA：表明权限为只读
//FILE_WRITE_DATA：表明权限为可写
//也可以 FILE_WRITE_DATA | FILE_READ_DATA：表明权限为可读可写，但还没达到FILE_ANY_ACCESS的权限。
#define CTL_CODE( DeviceType, Function, Method, Access ) (((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))


#define EVENT_IOCTL(index) CTL_CODE(FILE_DEVICE_EVENT, index, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_PASSEVENT    CTL_CODE(FILE_DEVICE_EVENT, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PASSBUF	   CTL_CODE(FILE_DEVICE_EVENT, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_UNPASSEVENT  CTL_CODE(FILE_DEVICE_EVENT, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PASSEVSTRUCT CTL_CODE(FILE_DEVICE_EVENT, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct
{
	BOOL SHOWTHREAD;
	BOOL ONLYSHOWREMOTETHREAD;
	BOOL SHOWEXITPROCESS;
	BOOL SHOWEXITTHREAD;
	BOOL SHOWIMAGE;
}CHECKLIST, * PCHECKLIST;
