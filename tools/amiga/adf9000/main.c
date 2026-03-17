/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const uint32_t adf_tool_blockSize = 512;
static const uint32_t adf_tool_totalBlocks = 1760;
static const uint32_t adf_tool_rootBlock = 880;
static const uint32_t adf_tool_bitmapBlock = 881;
static const uint32_t adf_tool_hashTableSize = 72;
static const uint32_t adf_tool_maxNameLen = 30;
static const uint32_t adf_tool_dataBytesPerBlock = 488;

static const uint32_t adf_tool_typeHeader = 2;
static const uint32_t adf_tool_typeList = 16;
static const uint32_t adf_tool_typeData = 8;

static const int32_t adf_tool_secRoot = 1;
static const int32_t adf_tool_secUserDir = 2;
static const int32_t adf_tool_secFile = -3;

static const uint8_t adf_tool_standardBootBlock[] =
{
    0x43, 0xfa, 0x00, 0x3e, 0x70, 0x25, 0x4e, 0xae, 0xfd, 0xd8, 0x4a, 0x80, 0x67, 0x0c, 0x22, 0x40,
    0x08, 0xe9, 0x00, 0x06, 0x00, 0x22, 0x4e, 0xae, 0xfe, 0x62, 0x43, 0xfa, 0x00, 0x18, 0x4e, 0xae,
    0xff, 0xa0, 0x4a, 0x80, 0x67, 0x0a, 0x20, 0x40, 0x20, 0x68, 0x00, 0x16, 0x70, 0x00, 0x4e, 0x75,
    0x70, 0xff, 0x4e, 0x75, 0x64, 0x6f, 0x73, 0x2e, 0x6c, 0x69, 0x62, 0x72, 0x61, 0x72, 0x79, 0x00,
    0x65, 0x78, 0x70, 0x61, 0x6e, 0x73, 0x69, 0x6f, 0x6e, 0x2e, 0x6c, 0x69, 0x62, 0x72, 0x61, 0x72,
    0x79, 0x00
};

typedef struct
{
    uint32_t days;
    uint32_t mins;
    uint32_t ticks;
} adf_tool_amiga_time;

static time_t
adf_tool_daysFromCivil(int year, unsigned month, unsigned day)
{
    int64_t era;
    unsigned yoe;
    unsigned doy;
    unsigned doe;
    int adjustedYear;
    int adjustedMonth;
    int monthPrime;

    adjustedYear = year;
    adjustedMonth = (int)month;
    adjustedYear -= adjustedMonth <= 2;
    era = (adjustedYear >= 0 ? adjustedYear : adjustedYear - 399) / 400;
    yoe = (unsigned)(adjustedYear - (int)(era * 400));
    monthPrime = adjustedMonth + (adjustedMonth > 2 ? -3 : 9);
    doy = (unsigned)(((153 * monthPrime) + 2) / 5 + (int)day - 1);
    doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (time_t)(era * 146097 + (int64_t)doe - 719468);
}

static time_t
adf_tool_timegm(struct tm *timeValue)
{
    time_t daysSinceEpoch;
    int64_t seconds;
    int year;
    unsigned month;
    unsigned day;

    if (timeValue == NULL)
    {
        return (time_t)-1;
    }
    year = timeValue->tm_year + 1900;
    month = (unsigned)(timeValue->tm_mon + 1);
    day = (unsigned)timeValue->tm_mday;
    daysSinceEpoch = adf_tool_daysFromCivil(year, month, day);
    seconds = (int64_t)daysSinceEpoch * 86400;
    seconds += (int64_t)timeValue->tm_hour * 3600;
    seconds += (int64_t)timeValue->tm_min * 60;
    seconds += (int64_t)timeValue->tm_sec;
    return (time_t)seconds;
}

static time_t adf_tool_fixedUnixTime = 0;
static int adf_tool_hasFixedUnixTime = 0;

static uint32_t
adf_tool_readBe32(const uint8_t *ptr)
{
    return ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | (uint32_t)ptr[3];
}

static void
adf_tool_writeBe32(uint8_t *ptr, uint32_t value)
{
    ptr[0] = (uint8_t)((value >> 24) & 0xff);
    ptr[1] = (uint8_t)((value >> 16) & 0xff);
    ptr[2] = (uint8_t)((value >> 8) & 0xff);
    ptr[3] = (uint8_t)(value & 0xff);
}

static uint8_t *
adf_tool_blockPtr(uint8_t *image, uint32_t blockNumber)
{
    return image + (blockNumber * adf_tool_blockSize);
}

static adf_tool_amiga_time
adf_tool_getAmigaTime(void)
{
    time_t now;
    struct tm *utcTime;
    struct tm amigaEpoch;
    time_t amigaEpochTime;
    time_t utcTimeValue;
    double seconds;
    adf_tool_amiga_time result;

    now = adf_tool_hasFixedUnixTime ? adf_tool_fixedUnixTime : time(NULL);
    utcTime = gmtime(&now);
    memset(&amigaEpoch, 0, sizeof(amigaEpoch));
    amigaEpoch.tm_year = 78;
    amigaEpoch.tm_mon = 0;
    amigaEpoch.tm_mday = 1;
    amigaEpoch.tm_isdst = 0;
    amigaEpochTime = adf_tool_timegm(&amigaEpoch);
    utcTimeValue = adf_tool_timegm(utcTime);
    seconds = difftime(utcTimeValue, amigaEpochTime);
    if (seconds < 0)
    {
        seconds = 0;
    }
    result.days = (uint32_t)(seconds / 86400.0);
    if (result.days == 0)
    {
        result.days = 1;
    }
    seconds -= (double)result.days * 86400.0;
    if (seconds < 0)
    {
        seconds = 0;
    }
    result.mins = (uint32_t)(seconds / 60.0);
    seconds -= (double)result.mins * 60.0;
    result.ticks = (uint32_t)(seconds * 50.0);
    return result;
}

static void
adf_tool_writeChecksum(uint8_t *block, uint32_t checksumOffset)
{
    uint32_t sum;
    uint32_t value;
    uint32_t index;

    adf_tool_writeBe32(block + checksumOffset, 0);
    sum = 0;
    for (index = 0; index < (adf_tool_blockSize / 4); index++)
    {
        value = adf_tool_readBe32(block + (index * 4));
        sum += value;
    }
    sum = (uint32_t)(0 - sum);
    adf_tool_writeBe32(block + checksumOffset, sum);
}

static void
adf_tool_writeBootChecksum(uint8_t *bootBlock)
{
    uint32_t sum;
    uint32_t value;
    uint32_t index;

    adf_tool_writeBe32(bootBlock + 4, 0);
    sum = 0;
    for (index = 0; index < (1024 / 4); index++)
    {
        value = adf_tool_readBe32(bootBlock + (index * 4));
        sum += value;
        if (sum < value)
        {
            sum += 1;
        }
    }
    sum = ~sum;
    adf_tool_writeBe32(bootBlock + 4, sum);
}

static int
adf_tool_hashName(const char *name)
{
    uint32_t hash;
    uint32_t length;
    uint32_t index;
    uint8_t value;

    length = (uint32_t)strlen(name);
    hash = length;
    for (index = 0; index < length; index++)
    {
        value = (uint8_t)toupper((unsigned char)name[index]);
        hash = hash * 13;
        hash = hash + value;
        hash = hash & 0x7ff;
    }
    hash = hash % adf_tool_hashTableSize;
    return (int)hash;
}

static int
adf_tool_namesEqual(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0')
    {
        if (toupper((unsigned char)*left) != toupper((unsigned char)*right))
        {
            return 0;
        }
        left++;
        right++;
    }
    return (*left == '\0' && *right == '\0');
}

static void
adf_tool_writeName(uint8_t *block, const char *name)
{
    uint32_t length;

    length = (uint32_t)strlen(name);
    if (length > adf_tool_maxNameLen)
    {
        length = adf_tool_maxNameLen;
    }
    memset(block + 432, 0, 1 + adf_tool_maxNameLen);
    block[432] = (uint8_t)length;
    memcpy(block + 433, name, length);
}

static void
adf_tool_writeCommentEmpty(uint8_t *block)
{
    block[328] = 0;
    memset(block + 329, 0, 79);
}

static void
adf_tool_writeDate(uint8_t *block, uint32_t offset)
{
    adf_tool_amiga_time timeValue;

    timeValue = adf_tool_getAmigaTime();
    adf_tool_writeBe32(block + offset, timeValue.days);
    adf_tool_writeBe32(block + offset + 4, timeValue.mins);
    adf_tool_writeBe32(block + offset + 8, timeValue.ticks);
}

static void
adf_tool_writeRootDates(uint8_t *block)
{
    adf_tool_amiga_time timeValue;

    timeValue = adf_tool_getAmigaTime();
    adf_tool_writeBe32(block + 420, timeValue.days);
    adf_tool_writeBe32(block + 424, timeValue.mins);
    adf_tool_writeBe32(block + 428, timeValue.ticks);
    adf_tool_writeBe32(block + 472, timeValue.days);
    adf_tool_writeBe32(block + 476, timeValue.mins);
    adf_tool_writeBe32(block + 480, timeValue.ticks);
    adf_tool_writeBe32(block + 484, timeValue.days);
    adf_tool_writeBe32(block + 488, timeValue.mins);
    adf_tool_writeBe32(block + 492, timeValue.ticks);
}

static int
adf_tool_readImage(const char *path, uint8_t **imageOut)
{
    FILE *file;
    size_t readCount;
    uint8_t *image;

    file = fopen(path, "rb");
    if (file == NULL)
    {
        return -1;
    }
    image = (uint8_t *)malloc(adf_tool_totalBlocks * adf_tool_blockSize);
    if (image == NULL)
    {
        fclose(file);
        return -1;
    }
    readCount = fread(image, 1, adf_tool_totalBlocks * adf_tool_blockSize, file);
    fclose(file);
    if (readCount != adf_tool_totalBlocks * adf_tool_blockSize)
    {
        free(image);
        return -1;
    }
    *imageOut = image;
    return 0;
}

static int
adf_tool_writeImage(const char *path, const uint8_t *image)
{
    FILE *file;
    size_t writeCount;

    file = fopen(path, "wb");
    if (file == NULL)
    {
        return -1;
    }
    writeCount = fwrite(image, 1, adf_tool_totalBlocks * adf_tool_blockSize, file);
    fclose(file);
    if (writeCount != adf_tool_totalBlocks * adf_tool_blockSize)
    {
        return -1;
    }
    return 0;
}

static int
adf_tool_isBlockFree(uint8_t *image, uint32_t blockNumber)
{
    uint32_t pos;
    uint32_t longIndex;
    uint32_t bitIndex;
    uint32_t value;
    uint8_t *bitmap;

    if (blockNumber < 2 || blockNumber >= adf_tool_totalBlocks)
    {
        return 0;
    }
    pos = blockNumber - 2;
    longIndex = pos / 32;
    bitIndex = pos % 32;
    bitmap = adf_tool_blockPtr(image, adf_tool_bitmapBlock);
    value = adf_tool_readBe32(bitmap + 4 + (longIndex * 4));
    return (value & (1u << bitIndex)) != 0;
}

static void
adf_tool_setBlockUsed(uint8_t *image, uint32_t blockNumber, int used)
{
    uint32_t pos;
    uint32_t longIndex;
    uint32_t bitIndex;
    uint32_t value;
    uint8_t *bitmap;

    if (blockNumber < 2 || blockNumber >= adf_tool_totalBlocks)
    {
        return;
    }
    pos = blockNumber - 2;
    longIndex = pos / 32;
    bitIndex = pos % 32;
    bitmap = adf_tool_blockPtr(image, adf_tool_bitmapBlock);
    value = adf_tool_readBe32(bitmap + 4 + (longIndex * 4));
    if (used != 0)
    {
        value &= ~(1u << bitIndex);
    }
    else
    {
        value |= (1u << bitIndex);
    }
    adf_tool_writeBe32(bitmap + 4 + (longIndex * 4), value);
}

static int
adf_tool_allocBlock(uint8_t *image, uint32_t *blockOut)
{
    uint32_t blockNumber;

    for (blockNumber = 2; blockNumber < adf_tool_totalBlocks; blockNumber++)
    {
        if (adf_tool_isBlockFree(image, blockNumber) != 0)
        {
            adf_tool_setBlockUsed(image, blockNumber, 1);
            *blockOut = blockNumber;
            return 0;
        }
    }
    return -1;
}

static void
adf_tool_initBitmap(uint8_t *image)
{
    uint8_t *bitmap;
    uint32_t index;
    uint32_t mapLongs;

    bitmap = adf_tool_blockPtr(image, adf_tool_bitmapBlock);
    memset(bitmap, 0, adf_tool_blockSize);
    mapLongs = (adf_tool_totalBlocks - 2 + 31) / 32;
    for (index = 0; index < mapLongs; index++)
    {
        adf_tool_writeBe32(bitmap + 4 + (index * 4), 0xffffffffu);
    }
    adf_tool_setBlockUsed(image, adf_tool_rootBlock, 1);
    adf_tool_setBlockUsed(image, adf_tool_bitmapBlock, 1);
    adf_tool_writeChecksum(bitmap, 0);
}

static void
adf_tool_initRootBlock(uint8_t *image, const char *label)
{
    uint8_t *root;

    root = adf_tool_blockPtr(image, adf_tool_rootBlock);
    memset(root, 0, adf_tool_blockSize);
    adf_tool_writeBe32(root + 0, adf_tool_typeHeader);
    adf_tool_writeBe32(root + 4, 0);
    adf_tool_writeBe32(root + 8, 0);
    adf_tool_writeBe32(root + 12, adf_tool_hashTableSize);
    adf_tool_writeBe32(root + 16, 0);
    memset(root + 24, 0, adf_tool_hashTableSize * 4);
    adf_tool_writeBe32(root + 312, 0xffffffffu);
    adf_tool_writeBe32(root + 316, adf_tool_bitmapBlock);
    adf_tool_writeBe32(root + 408, 0);
    adf_tool_writeRootDates(root);
    adf_tool_writeName(root, label);
    adf_tool_writeBe32(root + 504, 0);
    adf_tool_writeBe32(root + 508, (uint32_t)adf_tool_secRoot);
    adf_tool_writeChecksum(root, 20);
}

static void
adf_tool_initBootBlock(uint8_t *image, const uint8_t *bootCode, size_t bootCodeSize)
{
    uint8_t *boot;

    boot = adf_tool_blockPtr(image, 0);
    memset(boot, 0, 1024);
    boot[0] = 'D';
    boot[1] = 'O';
    boot[2] = 'S';
    boot[3] = 0;
    adf_tool_writeBe32(boot + 8, adf_tool_rootBlock);
    if (bootCode != NULL && bootCodeSize > 0)
    {
        if (bootCodeSize > 1024 - 12)
        {
            bootCodeSize = 1024 - 12;
        }
        memcpy(boot + 12, bootCode, bootCodeSize);
    }
    adf_tool_writeBootChecksum(boot);
}

static void
adf_tool_getBootBlock(uint8_t *bootOut, size_t *bootSizeOut)
{
    size_t standardSize;

    memset(bootOut, 0, 1024);
    standardSize = sizeof(adf_tool_standardBootBlock);
    memcpy(bootOut, adf_tool_standardBootBlock, standardSize);
    if (bootSizeOut != NULL)
    {
        *bootSizeOut = standardSize;
    }
}

static int
adf_tool_readEntryName(uint8_t *block, char *nameOut, size_t nameOutSize)
{
    uint8_t length;

    length = block[432];
    if (length > adf_tool_maxNameLen)
    {
        length = adf_tool_maxNameLen;
    }
    if (nameOutSize < (size_t)length + 1)
    {
        return -1;
    }
    memcpy(nameOut, block + 433, length);
    nameOut[length] = '\0';
    return 0;
}

static uint32_t
adf_tool_readHashChain(uint8_t *block)
{
    return adf_tool_readBe32(block + 496);
}

static void
adf_tool_writeHashChain(uint8_t *block, uint32_t nextBlock)
{
    adf_tool_writeBe32(block + 496, nextBlock);
}

static void
adf_tool_writeParent(uint8_t *block, uint32_t parentBlock)
{
    adf_tool_writeBe32(block + 500, parentBlock);
}

static void
adf_tool_writeExtension(uint8_t *block, uint32_t extensionBlock)
{
    adf_tool_writeBe32(block + 504, extensionBlock);
}

static void
adf_tool_writeSecType(uint8_t *block, int32_t secType)
{
    adf_tool_writeBe32(block + 508, (uint32_t)secType);
}

static int
adf_tool_findEntry(uint8_t *image, uint32_t dirBlockNumber, const char *name, uint32_t *blockOut, int32_t *secTypeOut)
{
    uint8_t *dirBlock;
    uint32_t hashValue;
    uint32_t entryBlockNumber;
    uint8_t *entryBlock;
    char entryName[64];
    uint32_t nextBlock;

    dirBlock = adf_tool_blockPtr(image, dirBlockNumber);
    hashValue = (uint32_t)adf_tool_hashName(name);
    entryBlockNumber = adf_tool_readBe32(dirBlock + 24 + (hashValue * 4));
    while (entryBlockNumber != 0)
    {
        entryBlock = adf_tool_blockPtr(image, entryBlockNumber);
        if (adf_tool_readEntryName(entryBlock, entryName, sizeof(entryName)) == 0)
        {
            if (adf_tool_namesEqual(entryName, name) != 0)
            {
                if (blockOut != NULL)
                {
                    *blockOut = entryBlockNumber;
                }
                if (secTypeOut != NULL)
                {
                    *secTypeOut = (int32_t)adf_tool_readBe32(entryBlock + 508);
                }
                return 0;
            }
        }
        nextBlock = adf_tool_readHashChain(entryBlock);
        entryBlockNumber = nextBlock;
    }
    return -1;
}

static int
adf_tool_insertEntry(uint8_t *image, uint32_t dirBlockNumber, uint32_t newEntryBlockNumber, const char *name)
{
    uint8_t *dirBlock;
    uint32_t hashValue;
    uint32_t entryBlockNumber;
    uint8_t *entryBlock;
    char entryName[64];
    uint32_t nextBlock;

    dirBlock = adf_tool_blockPtr(image, dirBlockNumber);
    hashValue = (uint32_t)adf_tool_hashName(name);
    entryBlockNumber = adf_tool_readBe32(dirBlock + 24 + (hashValue * 4));
    if (entryBlockNumber == 0)
    {
        adf_tool_writeBe32(dirBlock + 24 + (hashValue * 4), newEntryBlockNumber);
        return 0;
    }
    while (entryBlockNumber != 0)
    {
        entryBlock = adf_tool_blockPtr(image, entryBlockNumber);
        if (adf_tool_readEntryName(entryBlock, entryName, sizeof(entryName)) == 0)
        {
            if (adf_tool_namesEqual(entryName, name) != 0)
            {
                return -1;
            }
        }
        nextBlock = adf_tool_readHashChain(entryBlock);
        if (nextBlock == 0)
        {
            adf_tool_writeHashChain(entryBlock, newEntryBlockNumber);
            return 0;
        }
        entryBlockNumber = nextBlock;
    }
    return -1;
}

static int
adf_tool_resolveDir(uint8_t *image, const char *path, uint32_t *dirBlockOut)
{
    char *pathCopy;
    char *token;
    char *savePtr;
    uint32_t currentBlock;
    uint32_t nextBlock;
    int32_t secType;

    currentBlock = adf_tool_rootBlock;
    if (path == NULL || path[0] == '\0' || strcmp(path, "/") == 0)
    {
        *dirBlockOut = currentBlock;
        return 0;
    }
    pathCopy = strdup(path);
    if (pathCopy == NULL)
    {
        return -1;
    }
    token = strtok_r(pathCopy, "/", &savePtr);
    while (token != NULL)
    {
        if (adf_tool_findEntry(image, currentBlock, token, &nextBlock, &secType) != 0)
        {
            free(pathCopy);
            return -1;
        }
        if (secType != adf_tool_secUserDir && secType != adf_tool_secRoot)
        {
            free(pathCopy);
            return -1;
        }
        currentBlock = nextBlock;
        token = strtok_r(NULL, "/", &savePtr);
    }
    free(pathCopy);
    *dirBlockOut = currentBlock;
    return 0;
}

static int
adf_tool_splitPath(const char *path, char *parentOut, size_t parentOutSize, char *nameOut, size_t nameOutSize)
{
    const char *lastSlash;
    size_t parentLength;

    lastSlash = strrchr(path, '/');
    if (lastSlash == NULL)
    {
        if (strlen(path) + 1 > nameOutSize)
        {
            return -1;
        }
        strcpy(nameOut, path);
        if (parentOutSize > 0)
        {
            parentOut[0] = '\0';
        }
        return 0;
    }
    parentLength = (size_t)(lastSlash - path);
    if (parentLength == 0)
    {
        parentLength = 1;
    }
    if (parentLength + 1 > parentOutSize)
    {
        return -1;
    }
    memcpy(parentOut, path, parentLength);
    parentOut[parentLength] = '\0';
    if (strlen(lastSlash + 1) + 1 > nameOutSize)
    {
        return -1;
    }
    strcpy(nameOut, lastSlash + 1);
    return 0;
}

static void
adf_tool_initDirBlock(uint8_t *block, uint32_t blockNumber, uint32_t parentBlockNumber, const char *name)
{
    memset(block, 0, adf_tool_blockSize);
    adf_tool_writeBe32(block + 0, adf_tool_typeHeader);
    adf_tool_writeBe32(block + 4, blockNumber);
    adf_tool_writeBe32(block + 8, 0);
    adf_tool_writeBe32(block + 12, 0);
    adf_tool_writeBe32(block + 16, 0);
    memset(block + 24, 0, adf_tool_hashTableSize * 4);
    adf_tool_writeBe32(block + 320, 0);
    adf_tool_writeCommentEmpty(block);
    adf_tool_writeDate(block, 420);
    adf_tool_writeName(block, name);
    adf_tool_writeHashChain(block, 0);
    adf_tool_writeParent(block, parentBlockNumber);
    adf_tool_writeExtension(block, 0);
    adf_tool_writeSecType(block, adf_tool_secUserDir);
    adf_tool_writeChecksum(block, 20);
}

static void
adf_tool_initFileHeader(uint8_t *block, uint32_t blockNumber, uint32_t parentBlockNumber, const char *name)
{
    memset(block, 0, adf_tool_blockSize);
    adf_tool_writeBe32(block + 0, adf_tool_typeHeader);
    adf_tool_writeBe32(block + 4, blockNumber);
    adf_tool_writeBe32(block + 8, 0);
    adf_tool_writeBe32(block + 12, 0);
    adf_tool_writeBe32(block + 16, 0);
    memset(block + 24, 0, adf_tool_hashTableSize * 4);
    adf_tool_writeBe32(block + 320, 0);
    adf_tool_writeBe32(block + 324, 0);
    adf_tool_writeCommentEmpty(block);
    adf_tool_writeDate(block, 420);
    adf_tool_writeName(block, name);
    adf_tool_writeHashChain(block, 0);
    adf_tool_writeParent(block, parentBlockNumber);
    adf_tool_writeExtension(block, 0);
    adf_tool_writeSecType(block, adf_tool_secFile);
}

static void
adf_tool_initFileExt(uint8_t *block, uint32_t blockNumber, uint32_t parentBlockNumber, uint32_t nextExt)
{
    memset(block, 0, adf_tool_blockSize);
    adf_tool_writeBe32(block + 0, adf_tool_typeList);
    adf_tool_writeBe32(block + 4, blockNumber);
    adf_tool_writeBe32(block + 8, 0);
    adf_tool_writeBe32(block + 12, 0);
    adf_tool_writeBe32(block + 16, 0);
    memset(block + 24, 0, adf_tool_hashTableSize * 4);
    adf_tool_writeParent(block, parentBlockNumber);
    adf_tool_writeExtension(block, nextExt);
    adf_tool_writeSecType(block, adf_tool_secFile);
}

static void
adf_tool_writeFileDataBlocks(uint8_t *image, const uint8_t *data, size_t dataSize, uint32_t headerBlockNumber, uint32_t *dataBlocks, uint32_t dataBlockCount)
{
    uint32_t index;
    uint32_t blockNumber;
    uint8_t *block;
    uint32_t offset;
    uint32_t remaining;
    uint32_t chunk;

    offset = 0;
    for (index = 0; index < dataBlockCount; index++)
    {
        blockNumber = dataBlocks[index];
        block = adf_tool_blockPtr(image, blockNumber);
        memset(block, 0, adf_tool_blockSize);
        adf_tool_writeBe32(block + 0, adf_tool_typeData);
        adf_tool_writeBe32(block + 4, headerBlockNumber);
        adf_tool_writeBe32(block + 8, index + 1);
        remaining = (uint32_t)(dataSize - offset);
        chunk = remaining > adf_tool_dataBytesPerBlock ? adf_tool_dataBytesPerBlock : remaining;
        adf_tool_writeBe32(block + 12, chunk);
        if (index + 1 < dataBlockCount)
        {
            adf_tool_writeBe32(block + 16, dataBlocks[index + 1]);
        }
        else
        {
            adf_tool_writeBe32(block + 16, 0);
        }
        if (chunk > 0)
        {
            memcpy(block + 24, data + offset, chunk);
        }
        adf_tool_writeChecksum(block, 20);
        offset += chunk;
    }
}

static void
adf_tool_fillDataBlockPointers(uint8_t *block, const uint32_t *dataBlocks, uint32_t count)
{
    uint32_t index;
    uint32_t tableIndex;

    adf_tool_writeBe32(block + 8, count);
    for (index = 0; index < count; index++)
    {
        tableIndex = (adf_tool_hashTableSize - 1) - index;
        adf_tool_writeBe32(block + 24 + (tableIndex * 4), dataBlocks[index]);
    }
}

static int
adf_tool_addFile(uint8_t *image, const char *srcPath, const char *destPath)
{
    char parentPath[256];
    char fileName[64];
    uint32_t parentBlock;
    uint32_t headerBlock;
    uint32_t *dataBlocks;
    uint32_t dataBlockCount;
    uint32_t extBlockCount;
    uint32_t extIndex;
    uint32_t dataIndex;
    uint32_t remainingBlocks;
    uint32_t countThisBlock;
    uint8_t *header;
    uint8_t *extBlock;
    uint32_t extBlockNumber;
    uint32_t nextExtBlockNumber;
    FILE *file;
    uint8_t *fileData;
    size_t fileSize;
    size_t readCount;
    int32_t secType;

    if (adf_tool_splitPath(destPath, parentPath, sizeof(parentPath), fileName, sizeof(fileName)) != 0)
    {
        return -1;
    }
    if (strlen(fileName) == 0)
    {
        return -1;
    }
    if (strlen(fileName) > adf_tool_maxNameLen)
    {
        return -1;
    }
    if (adf_tool_resolveDir(image, parentPath, &parentBlock) != 0)
    {
        return -1;
    }
    if (adf_tool_findEntry(image, parentBlock, fileName, NULL, &secType) == 0)
    {
        return -1;
    }
    file = fopen(srcPath, "rb");
    if (file == NULL)
    {
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return -1;
    }
    fileSize = (size_t)ftell(file);
    if (fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return -1;
    }
    fileData = NULL;
    if (fileSize > 0)
    {
        fileData = (uint8_t *)malloc(fileSize);
        if (fileData == NULL)
        {
            fclose(file);
            return -1;
        }
        readCount = fread(fileData, 1, fileSize, file);
        if (readCount != fileSize)
        {
            free(fileData);
            fclose(file);
            return -1;
        }
    }
    fclose(file);
    dataBlockCount = (uint32_t)((fileSize + adf_tool_dataBytesPerBlock - 1) / adf_tool_dataBytesPerBlock);
    dataBlocks = NULL;
    if (dataBlockCount > 0)
    {
        dataBlocks = (uint32_t *)malloc(sizeof(uint32_t) * dataBlockCount);
        if (dataBlocks == NULL)
        {
            free(fileData);
            return -1;
        }
        for (dataIndex = 0; dataIndex < dataBlockCount; dataIndex++)
        {
            if (adf_tool_allocBlock(image, &dataBlocks[dataIndex]) != 0)
            {
                free(dataBlocks);
                free(fileData);
                return -1;
            }
        }
    }
    if (adf_tool_allocBlock(image, &headerBlock) != 0)
    {
        free(dataBlocks);
        free(fileData);
        return -1;
    }
    header = adf_tool_blockPtr(image, headerBlock);
    adf_tool_initFileHeader(header, headerBlock, parentBlock, fileName);
    adf_tool_writeBe32(header + 324, (uint32_t)fileSize);
    if (dataBlockCount > 0)
    {
        adf_tool_writeBe32(header + 16, dataBlocks[0]);
        remainingBlocks = dataBlockCount;
        countThisBlock = remainingBlocks > adf_tool_hashTableSize ? adf_tool_hashTableSize : remainingBlocks;
        adf_tool_fillDataBlockPointers(header, dataBlocks, countThisBlock);
        remainingBlocks -= countThisBlock;
    }
    else
    {
        adf_tool_writeBe32(header + 16, 0);
    }
    extBlockCount = 0;
    if (dataBlockCount > adf_tool_hashTableSize)
    {
        extBlockCount = (dataBlockCount - adf_tool_hashTableSize + adf_tool_hashTableSize - 1) / adf_tool_hashTableSize;
    }
    nextExtBlockNumber = 0;
    if (extBlockCount > 0)
    {
        if (adf_tool_allocBlock(image, &nextExtBlockNumber) != 0)
        {
            free(dataBlocks);
            free(fileData);
            return -1;
        }
        adf_tool_writeExtension(header, nextExtBlockNumber);
    }
    dataIndex = adf_tool_hashTableSize;
    for (extIndex = 0; extIndex < extBlockCount; extIndex++)
    {
        extBlockNumber = nextExtBlockNumber;
        if (extIndex + 1 < extBlockCount)
        {
            if (adf_tool_allocBlock(image, &nextExtBlockNumber) != 0)
            {
                free(dataBlocks);
                free(fileData);
                return -1;
            }
        }
        else
        {
            nextExtBlockNumber = 0;
        }
        extBlock = adf_tool_blockPtr(image, extBlockNumber);
        adf_tool_initFileExt(extBlock, extBlockNumber, headerBlock, nextExtBlockNumber);
        remainingBlocks = dataBlockCount - dataIndex;
        countThisBlock = remainingBlocks > adf_tool_hashTableSize ? adf_tool_hashTableSize : remainingBlocks;
        if (countThisBlock > 0)
        {
            adf_tool_fillDataBlockPointers(extBlock, dataBlocks + dataIndex, countThisBlock);
        }
        dataIndex += countThisBlock;
        adf_tool_writeChecksum(extBlock, 20);
    }
    adf_tool_writeChecksum(header, 20);
    if (dataBlockCount > 0)
    {
        adf_tool_writeFileDataBlocks(image, fileData, fileSize, headerBlock, dataBlocks, dataBlockCount);
    }
    free(fileData);
    free(dataBlocks);
    if (adf_tool_insertEntry(image, parentBlock, headerBlock, fileName) != 0)
    {
        return -1;
    }
    adf_tool_writeDate(adf_tool_blockPtr(image, parentBlock), 420);
    adf_tool_writeChecksum(adf_tool_blockPtr(image, parentBlock), 20);
    adf_tool_writeRootDates(adf_tool_blockPtr(image, adf_tool_rootBlock));
    adf_tool_writeChecksum(adf_tool_blockPtr(image, adf_tool_rootBlock), 20);
    adf_tool_writeChecksum(adf_tool_blockPtr(image, adf_tool_bitmapBlock), 0);
    return 0;
}

static int
adf_tool_makeDir(uint8_t *image, const char *destPath)
{
    char parentPath[256];
    char dirName[64];
    uint32_t parentBlock;
    uint32_t dirBlock;
    uint8_t *dirBlockPtr;
    int32_t secType;

    if (adf_tool_splitPath(destPath, parentPath, sizeof(parentPath), dirName, sizeof(dirName)) != 0)
    {
        return -1;
    }
    if (strlen(dirName) == 0)
    {
        return -1;
    }
    if (strlen(dirName) > adf_tool_maxNameLen)
    {
        return -1;
    }
    if (adf_tool_resolveDir(image, parentPath, &parentBlock) != 0)
    {
        return -1;
    }
    if (adf_tool_findEntry(image, parentBlock, dirName, NULL, &secType) == 0)
    {
        return -1;
    }
    if (adf_tool_allocBlock(image, &dirBlock) != 0)
    {
        return -1;
    }
    dirBlockPtr = adf_tool_blockPtr(image, dirBlock);
    adf_tool_initDirBlock(dirBlockPtr, dirBlock, parentBlock, dirName);
    if (adf_tool_insertEntry(image, parentBlock, dirBlock, dirName) != 0)
    {
        return -1;
    }
    adf_tool_writeDate(adf_tool_blockPtr(image, parentBlock), 420);
    adf_tool_writeChecksum(adf_tool_blockPtr(image, parentBlock), 20);
    adf_tool_writeRootDates(adf_tool_blockPtr(image, adf_tool_rootBlock));
    adf_tool_writeChecksum(adf_tool_blockPtr(image, adf_tool_rootBlock), 20);
    adf_tool_writeChecksum(adf_tool_blockPtr(image, adf_tool_bitmapBlock), 0);
    return 0;
}

static int
adf_tool_createImage(const char *path, const char *label)
{
    uint8_t *image;
    uint8_t bootCode[1024];
    size_t bootCodeSize;

    image = (uint8_t *)calloc(adf_tool_totalBlocks, adf_tool_blockSize);
    if (image == NULL)
    {
        return -1;
    }
    adf_tool_getBootBlock(bootCode, &bootCodeSize);
    adf_tool_initBootBlock(image, bootCode, bootCodeSize);
    adf_tool_initRootBlock(image, label);
    adf_tool_initBitmap(image);
    if (adf_tool_writeImage(path, image) != 0)
    {
        free(image);
        return -1;
    }
    free(image);
    return 0;
}

static void
adf_tool_printUsage(void)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  adf_tool <image.adf> [--time <unix>] create --label <name>\n");
    fprintf(stderr, "  adf_tool <image.adf> [--time <unix>] mkdir <path>\n");
    fprintf(stderr, "  adf_tool <image.adf> [--time <unix>] cp <src> <dest>\n");
    fprintf(stderr, "  adf_tool <image.adf> [--time <unix>] ls [path]\n");
}

static int
adf_tool_listDir(uint8_t *image, const char *path)
{
    uint32_t dirBlockNumber;
    uint8_t *dirBlock;
    uint32_t bucketIndex;
    uint32_t entryBlockNumber;
    uint8_t *entryBlock;
    char entryName[64];
    int32_t secType;
    uint32_t fileSize;
    char parentPath[256];
    char namePart[64];
    int32_t entryType;

    if (adf_tool_resolveDir(image, path, &dirBlockNumber) != 0)
    {
        if (adf_tool_splitPath(path, parentPath, sizeof(parentPath), namePart, sizeof(namePart)) != 0)
        {
            return -1;
        }
        if (adf_tool_resolveDir(image, parentPath, &dirBlockNumber) != 0)
        {
            return -1;
        }
        if (adf_tool_findEntry(image, dirBlockNumber, namePart, &entryBlockNumber, &entryType) != 0)
        {
            return -1;
        }
        if (entryType == adf_tool_secUserDir)
        {
            dirBlockNumber = entryBlockNumber;
        }
        else if (entryType == adf_tool_secFile)
        {
            entryBlock = adf_tool_blockPtr(image, entryBlockNumber);
            if (adf_tool_readEntryName(entryBlock, entryName, sizeof(entryName)) == 0)
            {
                fileSize = adf_tool_readBe32(entryBlock + 324);
                printf("%s (%u bytes)\n", entryName, fileSize);
                return 0;
            }
            return -1;
        }
        else
        {
            return -1;
        }
    }
    dirBlock = adf_tool_blockPtr(image, dirBlockNumber);
    for (bucketIndex = 0; bucketIndex < adf_tool_hashTableSize; bucketIndex++)
    {
        entryBlockNumber = adf_tool_readBe32(dirBlock + 24 + (bucketIndex * 4));
        while (entryBlockNumber != 0)
        {
            entryBlock = adf_tool_blockPtr(image, entryBlockNumber);
            if (adf_tool_readEntryName(entryBlock, entryName, sizeof(entryName)) == 0)
            {
                secType = (int32_t)adf_tool_readBe32(entryBlock + 508);
                if (secType == adf_tool_secUserDir)
                {
                    printf("%s/\n", entryName);
                }
                else if (secType == adf_tool_secFile)
                {
                    fileSize = adf_tool_readBe32(entryBlock + 324);
                    printf("%s (%u bytes)\n", entryName, fileSize);
                }
                else if (secType == adf_tool_secRoot)
                {
                    printf("%s\n", entryName);
                }
                else
                {
                    printf("%s?\n", entryName);
                }
            }
            entryBlockNumber = adf_tool_readHashChain(entryBlock);
        }
    }
    return 0;
}

int
main(int argc, char **argv)
{
    const char *command;
    const char *imagePath;
    const char *label;
    const char *srcPath;
    const char *destPath;
    const char *listPath;
    uint8_t *image;
    int result;
    int argIndex;
    const char *timeArg;
    char *timeEnd;
    unsigned long long timeValue;
    if (argc < 3)
    {
        adf_tool_printUsage();
        return 1;
    }
    imagePath = argv[1];
    argIndex = 2;
    if (strcmp(argv[argIndex], "--time") == 0)
    {
        if (argIndex + 2 >= argc)
        {
            adf_tool_printUsage();
            return 1;
        }
        timeArg = argv[argIndex + 1];
        errno = 0;
        timeValue = strtoull(timeArg, &timeEnd, 10);
        if (errno != 0 || timeEnd == timeArg || *timeEnd != '\0')
        {
            fprintf(stderr, "Invalid --time value.\n");
            return 1;
        }
        adf_tool_fixedUnixTime = (time_t)timeValue;
        adf_tool_hasFixedUnixTime = 1;
        argIndex += 2;
        if (argIndex >= argc)
        {
            adf_tool_printUsage();
            return 1;
        }
    }
    command = argv[argIndex];
    if (strcmp(command, "create") == 0)
    {
        if (argIndex + 3 != argc)
        {
            adf_tool_printUsage();
            return 1;
        }
        label = NULL;
        if (strcmp(argv[argIndex + 1], "--label") == 0)
        {
            label = argv[argIndex + 2];
        }
        if (label == NULL)
        {
            adf_tool_printUsage();
            return 1;
        }
        if (strlen(label) == 0 || strlen(label) > adf_tool_maxNameLen)
        {
            fprintf(stderr, "Label must be 1..30 characters.\n");
            return 1;
        }
        result = adf_tool_createImage(imagePath, label);
        if (result != 0)
        {
            fprintf(stderr, "Failed to create image: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }
    if (strcmp(command, "mkdir") == 0)
    {
        if (argIndex + 2 != argc)
        {
            adf_tool_printUsage();
            return 1;
        }
        destPath = argv[argIndex + 1];
        if (adf_tool_readImage(imagePath, &image) != 0)
        {
            fprintf(stderr, "Failed to read image.\n");
            return 1;
        }
        if (adf_tool_makeDir(image, destPath) != 0)
        {
            free(image);
            fprintf(stderr, "Failed to create directory.\n");
            return 1;
        }
        if (adf_tool_writeImage(imagePath, image) != 0)
        {
            free(image);
            fprintf(stderr, "Failed to write image.\n");
            return 1;
        }
        free(image);
        return 0;
    }
    if (strcmp(command, "cp") == 0)
    {
        if (argIndex + 3 != argc)
        {
            adf_tool_printUsage();
            return 1;
        }
        srcPath = argv[argIndex + 1];
        destPath = argv[argIndex + 2];
        if (adf_tool_readImage(imagePath, &image) != 0)
        {
            fprintf(stderr, "Failed to read image.\n");
            return 1;
        }
        if (adf_tool_addFile(image, srcPath, destPath) != 0)
        {
            free(image);
            fprintf(stderr, "Failed to add file.\n");
            return 1;
        }
        if (adf_tool_writeImage(imagePath, image) != 0)
        {
            free(image);
            fprintf(stderr, "Failed to write image.\n");
            return 1;
        }
        free(image);
        return 0;
    }
    if (strcmp(command, "ls") == 0)
    {
        if (argIndex + 1 != argc && argIndex + 2 != argc)
        {
            adf_tool_printUsage();
            return 1;
        }
        listPath = "/";
        if (argIndex + 2 == argc)
        {
            listPath = argv[argIndex + 1];
        }
        if (adf_tool_readImage(imagePath, &image) != 0)
        {
            fprintf(stderr, "Failed to read image.\n");
            return 1;
        }
        if (adf_tool_listDir(image, listPath) != 0)
        {
            free(image);
            fprintf(stderr, "Failed to list directory.\n");
            return 1;
        }
        free(image);
        return 0;
    }
    adf_tool_printUsage();
    return 1;
}
