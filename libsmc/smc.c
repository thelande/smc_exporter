/*
 * Apple System Management Control (SMC) Tool
 * Copyright (C) 2006 devnull
 * Portions Copyright (C) 2013 Michael Wilber
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "smc.h"

// Cache the keyInfo to lower the energy impact of SMCReadKey() / SMCReadKey2()
#define KEY_INFO_CACHE_SIZE 100
struct
{
    UInt32 key;
    SMCKeyData_keyInfo_t keyInfo;
} g_keyInfoCache[KEY_INFO_CACHE_SIZE];

int g_keyInfoCacheCount = 0;
OSSpinLock g_keyInfoSpinLock = 0;

kern_return_t SMCCall2(int index, SMCKeyData_t *inputStructure, SMCKeyData_t *outputStructure, io_connect_t conn);

#pragma mark C Helpers

UInt32 _strtoul(char *str, int size, int base)
{
    UInt32 total = 0;
    int i;

    for (i = 0; i < size; i++)
    {
        if (base == 16)
            total += str[i] << (size - 1 - i) * 8;
        else
            total += ((unsigned char)(str[i]) << (size - 1 - i) * 8);
    }
    return total;
}

void _ultostr(char *str, UInt32 val)
{
    str[0] = '\0';
    sprintf(str, "%c%c%c%c",
            (unsigned int)val >> 24,
            (unsigned int)val >> 16,
            (unsigned int)val >> 8,
            (unsigned int)val);
}

float _strtof(unsigned char *str, int size, int e)
{
    float total = 0;
    int i;

    for (i = 0; i < size; i++)
    {
        if (i == (size - 1))
            total += (str[i] & 0xff) >> e;
        else
            total += str[i] << (size - 1 - i) * (8 - e);
    }

    total += (str[size - 1] & 0x03) * 0.25;

    return total;
}

unsigned int getUInt(SMCVal_t val) {
    return (unsigned int) _strtoul((char *)val.bytes, val.dataSize, 10);
}

signed char getSI8(SMCVal_t val)
{
    return (signed char)*val.bytes;
}

short getSI16(SMCVal_t val)
{
    return ntohs(*(SInt16*)val.bytes);
}

float getFLT(SMCVal_t val)
{
    float fval;
    memcpy(&fval, val.bytes, sizeof(float));
    return fval;
}

float getFP1F(SMCVal_t val)
{
    return ntohs(*(UInt16 *)val.bytes) / 32768.0;
}

float getFP4C(SMCVal_t val)
{
    return ntohs(*(UInt16 *)val.bytes) / 4096.0;
}

float getFP5B(SMCVal_t val)
{
    return ntohs(*(UInt16 *)val.bytes) / 2048.0;
}

float getFP6A(SMCVal_t val)
{
    return ntohs(*(UInt16 *)val.bytes) / 1024.0;
}

float getFP79(SMCVal_t val)
{
    return ntohs(*(UInt16 *)val.bytes) / 512.0;
}

float getFP88(SMCVal_t val)
{
    return ntohs(*(UInt16 *)val.bytes) / 256.0;
}

float getFPA6(SMCVal_t val)
{
    return ntohs(*(UInt16 *)val.bytes) / 64.0;
}

float getFPC4(SMCVal_t val)
{
    return ntohs(*(UInt16 *)val.bytes) / 16.0;
}

float getFPE2(SMCVal_t val)
{
    return ntohs(*(UInt16 *)val.bytes) / 4.0;
}

float getSP1E(SMCVal_t val)
{
    return ((SInt16)ntohs(*(UInt16 *)val.bytes)) / 16384.0;
}

float getSP3C(SMCVal_t val)
{
    return ((SInt16)ntohs(*(UInt16 *)val.bytes)) / 4096.0;
}

float getSP4B(SMCVal_t val)
{
    return ((SInt16)ntohs(*(UInt16 *)val.bytes)) / 2048.0;
}

float getSP5A(SMCVal_t val)
{
    return ((SInt16)ntohs(*(UInt16 *)val.bytes)) / 1024.0;
}

float getSP69(SMCVal_t val)
{
    return ((SInt16)ntohs(*(UInt16 *)val.bytes)) / 512.0;
}

float getSP78(SMCVal_t val)
{
    return ((SInt16)ntohs(*(UInt16 *)val.bytes)) / 256.0;
}

float getSP87(SMCVal_t val)
{
    return ((SInt16)ntohs(*(UInt16 *)val.bytes)) / 128.0;
}

float getSP96(SMCVal_t val)
{
    return ((SInt16)ntohs(*(UInt16 *)val.bytes)) / 64.0;
}

float getSPB4(SMCVal_t val)
{
    return ((SInt16)ntohs(*(UInt16 *)val.bytes)) / 16.0;
}

float getSPF0(SMCVal_t val)
{
    return (float)ntohs(*(UInt16 *)val.bytes);
}

float getPWM(SMCVal_t val)
{
    return ntohs(*(UInt16 *)val.bytes) * 100 / 65536.0;
}

float getVal(SMCVal_t val)
{
    // printf("  %-4s  [%-4s]  ", val.key, val.dataType);
    if (val.dataSize > 0)
    {
        if (strcmp(val.dataType, DATATYPE_FLT) == 0 && val.dataSize == 4)
            return getFLT(val);
        else if (strcmp(val.dataType, DATATYPE_FP1F) == 0 && val.dataSize == 2)
            return getFP1F(val);
        else if (strcmp(val.dataType, DATATYPE_FP4C) == 0 && val.dataSize == 2)
            return getFP4C(val);
        else if (strcmp(val.dataType, DATATYPE_FP5B) == 0 && val.dataSize == 2)
            return getFP5B(val);
        else if (strcmp(val.dataType, DATATYPE_FP6A) == 0 && val.dataSize == 2)
            return getFP6A(val);
        else if (strcmp(val.dataType, DATATYPE_FP79) == 0 && val.dataSize == 2)
            return getFP79(val);
        else if (strcmp(val.dataType, DATATYPE_FP88) == 0 && val.dataSize == 2)
            return getFP88(val);
        else if (strcmp(val.dataType, DATATYPE_FPA6) == 0 && val.dataSize == 2)
            return getFPA6(val);
        else if (strcmp(val.dataType, DATATYPE_FPC4) == 0 && val.dataSize == 2)
            return getFPC4(val);
        else if (strcmp(val.dataType, DATATYPE_FPE2) == 0 && val.dataSize == 2)
            return getFPE2(val);
        else if (strcmp(val.dataType, DATATYPE_SP1E) == 0 && val.dataSize == 2)
            return getSP1E(val);
        else if (strcmp(val.dataType, DATATYPE_SP3C) == 0 && val.dataSize == 2)
            return getSP3C(val);
        else if (strcmp(val.dataType, DATATYPE_SP4B) == 0 && val.dataSize == 2)
            return getSP4B(val);
        else if (strcmp(val.dataType, DATATYPE_SP5A) == 0 && val.dataSize == 2)
            return getSP5A(val);
        else if (strcmp(val.dataType, DATATYPE_SP69) == 0 && val.dataSize == 2)
            return getSP69(val);
        else if (strcmp(val.dataType, DATATYPE_SP78) == 0 && val.dataSize == 2)
            return getSP78(val);
        else if (strcmp(val.dataType, DATATYPE_SP87) == 0 && val.dataSize == 2)
            return getSP87(val);
        else if (strcmp(val.dataType, DATATYPE_SP96) == 0 && val.dataSize == 2)
            return getSP96(val);
        else if (strcmp(val.dataType, DATATYPE_SPB4) == 0 && val.dataSize == 2)
            return getSPB4(val);
        else if (strcmp(val.dataType, DATATYPE_SPF0) == 0 && val.dataSize == 2)
            return getSPF0(val);
        else if (strcmp(val.dataType, DATATYPE_PWM) == 0 && val.dataSize == 2)
            return getPWM(val);
        else if (strcmp(val.dataType, DATATYPE_FLT) == 0 && val.dataSize == 4)
            return getFLT(val);
    }

    return -255.0;
}

unsigned int getUIntVal(SMCVal_t val) {
    if (val.dataSize > 0) {
        if ((strcmp(val.dataType, DATATYPE_UINT8) == 0) ||
            (strcmp(val.dataType, DATATYPE_UINT16) == 0) ||
            (strcmp(val.dataType, DATATYPE_UINT32) == 0))
            return getUInt(val);
    }

    return UINT_MAX;
}

int getIntVal(SMCVal_t val) {
    if (val.dataSize > 0) {
        if (strcmp(val.dataType, DATATYPE_SI8) == 0 && val.dataSize == 1)
			return (int)getSI8(val);
		else if (strcmp(val.dataType, DATATYPE_SI16) == 0 && val.dataSize == 2)
			return (int)getSI16(val);
    }

    return INT_MIN;
}

int valIsFloat(SMCVal_t val) {
    return valIsUInt(val) + valIsInt(val) == 0;
}

int valIsUInt(SMCVal_t val) {
    if (val.dataSize > 0) {
        if ((strcmp(val.dataType, DATATYPE_UINT8) == 0) ||
            (strcmp(val.dataType, DATATYPE_UINT16) == 0) ||
            (strcmp(val.dataType, DATATYPE_UINT32) == 0))
            return 1;
    }

    return 0;
}

int valIsInt(SMCVal_t val) {
    if (val.dataSize > 0) {
        if ((strcmp(val.dataType, DATATYPE_SI8) == 0 && val.dataSize == 1) ||
            (strcmp(val.dataType, DATATYPE_SI16) == 0 && val.dataSize == 2))
			return 1;
    }

    return 0;
}

kern_return_t SMCOpen(io_connect_t *conn)
{
    kern_return_t result;
    mach_port_t masterPort;
    io_iterator_t iterator;
    io_object_t device;

    IOMasterPort(MACH_PORT_NULL, &masterPort);

    CFMutableDictionaryRef matchingDictionary = IOServiceMatching("AppleSMC");
    result = IOServiceGetMatchingServices(masterPort, matchingDictionary, &iterator);
    if (result != kIOReturnSuccess)
    {
        printf("Error: IOServiceGetMatchingServices() = %08x\n", result);
        return 1;
    }

    device = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    if (device == 0)
    {
        printf("Error: no SMC found\n");
        return 1;
    }

    result = IOServiceOpen(device, mach_task_self(), 0, conn);
    IOObjectRelease(device);
    if (result != kIOReturnSuccess)
    {
        printf("Error: IOServiceOpen() = %08x\n", result);
        return 1;
    }

    return kIOReturnSuccess;
}

kern_return_t SMCClose(io_connect_t conn)
{
    return IOServiceClose(conn);
}

kern_return_t SMCCall2(int index, SMCKeyData_t *inputStructure, SMCKeyData_t *outputStructure, io_connect_t conn)
{
    size_t structureInputSize;
    size_t structureOutputSize;
    structureInputSize = sizeof(SMCKeyData_t);
    structureOutputSize = sizeof(SMCKeyData_t);

    return IOConnectCallStructMethod(conn, index, inputStructure, structureInputSize, outputStructure, &structureOutputSize);
}

// Provides key info, using a cache to dramatically improve the energy impact of smcFanControl
kern_return_t SMCGetKeyInfo(UInt32 key, SMCKeyData_keyInfo_t *keyInfo, io_connect_t conn)
{
    SMCKeyData_t inputStructure;
    SMCKeyData_t outputStructure;
    kern_return_t result = kIOReturnSuccess;
    int i = 0;

    OSSpinLockLock(&g_keyInfoSpinLock);

    for (; i < g_keyInfoCacheCount; ++i)
    {
        if (key == g_keyInfoCache[i].key)
        {
            *keyInfo = g_keyInfoCache[i].keyInfo;
            break;
        }
    }

    if (i == g_keyInfoCacheCount)
    {
        // Not in cache, must look it up.
        memset(&inputStructure, 0, sizeof(inputStructure));
        memset(&outputStructure, 0, sizeof(outputStructure));

        inputStructure.key = key;
        inputStructure.data8 = SMC_CMD_READ_KEYINFO;

        result = SMCCall2(KERNEL_INDEX_SMC, &inputStructure, &outputStructure, conn);
        if (result == kIOReturnSuccess)
        {
            *keyInfo = outputStructure.keyInfo;
            if (g_keyInfoCacheCount < KEY_INFO_CACHE_SIZE)
            {
                g_keyInfoCache[g_keyInfoCacheCount].key = key;
                g_keyInfoCache[g_keyInfoCacheCount].keyInfo = outputStructure.keyInfo;
                ++g_keyInfoCacheCount;
            }
        }
    }

    OSSpinLockUnlock(&g_keyInfoSpinLock);

    return result;
}

kern_return_t SMCReadKey2(UInt32Char_t key, SMCVal_t *val, io_connect_t conn)
{
    kern_return_t result;
    SMCKeyData_t inputStructure;
    SMCKeyData_t outputStructure;

    memset(&inputStructure, 0, sizeof(SMCKeyData_t));
    memset(&outputStructure, 0, sizeof(SMCKeyData_t));
    memset(val, 0, sizeof(SMCVal_t));

    inputStructure.key = _strtoul(key, 4, 16);
    sprintf(val->key, key);

    result = SMCGetKeyInfo(inputStructure.key, &outputStructure.keyInfo, conn);
    if (result != kIOReturnSuccess)
    {
        return result;
    }

    val->dataSize = outputStructure.keyInfo.dataSize;
    _ultostr(val->dataType, outputStructure.keyInfo.dataType);
    inputStructure.keyInfo.dataSize = val->dataSize;
    inputStructure.data8 = SMC_CMD_READ_BYTES;

    result = SMCCall2(KERNEL_INDEX_SMC, &inputStructure, &outputStructure, conn);
    if (result != kIOReturnSuccess)
    {
        return result;
    }

    memcpy(val->bytes, outputStructure.bytes, sizeof(outputStructure.bytes));

    return kIOReturnSuccess;
}

io_connect_t g_conn = 0;

void smc_init()
{
    SMCOpen(&g_conn);
}

void smc_close()
{
    SMCClose(g_conn);
}

kern_return_t SMCCall(int index, SMCKeyData_t *inputStructure, SMCKeyData_t *outputStructure)
{
    return SMCCall2(index, inputStructure, outputStructure, g_conn);
}

kern_return_t SMCReadKey(UInt32Char_t key, SMCVal_t *val)
{
    return SMCReadKey2(key, val, g_conn);
}

UInt32 SMCReadIndexCount(void)
{
    SMCVal_t val;

    SMCReadKey("#KEY", &val);
    return _strtoul((char *)val.bytes, val.dataSize, 10);
}

char* SMCGetKeyName(int key) {
    kern_return_t result;
    SMCKeyData_t inputStructure;
    SMCKeyData_t outputStructure;
    char* keyName = malloc(4);

    memset(&inputStructure, 0, sizeof(SMCKeyData_t));
    memset(&outputStructure, 0, sizeof(SMCKeyData_t));
    memset(keyName, 0, 4);

    inputStructure.data8 = SMC_CMD_READ_INDEX;
    inputStructure.data32 = key;

    result = SMCCall(KERNEL_INDEX_SMC, &inputStructure, &outputStructure);
    if (result != kIOReturnSuccess) {
        return 0;
    }

    _ultostr(keyName, outputStructure.key);
    return keyName;
}

/*
kern_return_t SMCPrintAll(void)
{
    kern_return_t result;
    SMCKeyData_t inputStructure;
    SMCKeyData_t outputStructure;

    int totalKeys, i;
    UInt32Char_t key;
    SMCVal_t val;

    totalKeys = SMCReadIndexCount();
    for (i = 0; i < totalKeys; i++)
    {
        memset(&inputStructure, 0, sizeof(SMCKeyData_t));
        memset(&outputStructure, 0, sizeof(SMCKeyData_t));
        memset(&val, 0, sizeof(SMCVal_t));

        inputStructure.data8 = SMC_CMD_READ_INDEX;
        inputStructure.data32 = i;

        result = SMCCall(KERNEL_INDEX_SMC, &inputStructure, &outputStructure);
        if (result != kIOReturnSuccess)
            continue;

        _ultostr(key, outputStructure.key);

        SMCReadKey(key, &val);
        printVal(val);
    }

    return kIOReturnSuccess;
}
*/
