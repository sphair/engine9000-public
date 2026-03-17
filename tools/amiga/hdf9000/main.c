/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static const uint32_t main_blockSize = 512;
static const uint32_t main_hashTableSize = 72;
static const uint32_t main_maxNameLen = 30;
static const uint32_t main_dataBytesPerBlock = 488;
static const uint32_t main_bitmapBlocksPerPage = 127 * 32;
enum
{
    main_maxBitmapBlocks = 25
};

static const uint32_t main_typeHeader = 2;
static const uint32_t main_typeList = 16;
static const uint32_t main_typeData = 8;

static const int32_t main_secRoot = 1;
static const int32_t main_secUserDir = 2;
static const int32_t main_secFile = -3;

static const uint8_t main_standardBootBlock[] =
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
} main_amiga_time;

typedef struct
{
    uint32_t totalBlocks;
    uint32_t rootBlock;
    uint32_t bitmapBlockCount;
    uint32_t bitmapBlocks[main_maxBitmapBlocks];
} main_image_info;

typedef struct
{
    uint64_t contentBlocks;
} main_scan_info;

typedef struct
{
    char *name;
    int isDir;
} main_dir_entry;

static time_t main_fixedUnixTime = 0;
static int main_hasFixedUnixTime = 0;

static time_t
main_daysFromCivil(int year, unsigned month, unsigned day)
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
main_timegm(struct tm *timeValue)
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
    daysSinceEpoch = main_daysFromCivil(year, month, day);
    seconds = (int64_t)daysSinceEpoch * 86400;
    seconds += (int64_t)timeValue->tm_hour * 3600;
    seconds += (int64_t)timeValue->tm_min * 60;
    seconds += (int64_t)timeValue->tm_sec;
    return (time_t)seconds;
}

static uint32_t
main_readBe32(const uint8_t *ptr)
{
    return ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | (uint32_t)ptr[3];
}

static void
main_writeBe32(uint8_t *ptr, uint32_t value)
{
    ptr[0] = (uint8_t)((value >> 24) & 0xff);
    ptr[1] = (uint8_t)((value >> 16) & 0xff);
    ptr[2] = (uint8_t)((value >> 8) & 0xff);
    ptr[3] = (uint8_t)(value & 0xff);
}

static uint8_t *
main_blockPtr(uint8_t *image, uint32_t blockNumber)
{
    return image + ((size_t)blockNumber * main_blockSize);
}

static main_amiga_time
main_getAmigaTime(void)
{
    time_t now;
    struct tm *utcTime;
    struct tm amigaEpoch;
    time_t amigaEpochTime;
    time_t utcTimeValue;
    double seconds;
    main_amiga_time result;

    now = main_hasFixedUnixTime ? main_fixedUnixTime : time(NULL);
    utcTime = gmtime(&now);
    memset(&amigaEpoch, 0, sizeof(amigaEpoch));
    amigaEpoch.tm_year = 78;
    amigaEpoch.tm_mon = 0;
    amigaEpoch.tm_mday = 1;
    amigaEpoch.tm_isdst = 0;
    amigaEpochTime = main_timegm(&amigaEpoch);
    utcTimeValue = main_timegm(utcTime);
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
main_writeChecksum(uint8_t *block, uint32_t checksumOffset)
{
    uint32_t sum;
    uint32_t value;
    uint32_t index;

    main_writeBe32(block + checksumOffset, 0);
    sum = 0;
    for (index = 0; index < (main_blockSize / 4); index++)
    {
        value = main_readBe32(block + (index * 4));
        sum += value;
    }
    sum = (uint32_t)(0 - sum);
    main_writeBe32(block + checksumOffset, sum);
}

static void
main_writeBootChecksum(uint8_t *bootBlock)
{
    uint32_t sum;
    uint32_t value;
    uint32_t index;

    main_writeBe32(bootBlock + 4, 0);
    sum = 0;
    for (index = 0; index < (1024 / 4); index++)
    {
        value = main_readBe32(bootBlock + (index * 4));
        sum += value;
        if (sum < value)
        {
            sum += 1;
        }
    }
    sum = ~sum;
    main_writeBe32(bootBlock + 4, sum);
}

static int
main_hashName(const char *name)
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
        hash += value;
        hash &= 0x7ff;
    }
    hash %= main_hashTableSize;
    return (int)hash;
}

static int
main_namesEqual(const char *left, const char *right)
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
main_writeName(uint8_t *block, const char *name)
{
    uint32_t length;

    length = (uint32_t)strlen(name);
    if (length > main_maxNameLen)
    {
        length = main_maxNameLen;
    }
    memset(block + 432, 0, 1 + main_maxNameLen);
    block[432] = (uint8_t)length;
    memcpy(block + 433, name, length);
}

static void
main_writeCommentEmpty(uint8_t *block)
{
    block[328] = 0;
    memset(block + 329, 0, 79);
}

static void
main_writeDate(uint8_t *block, uint32_t offset)
{
    main_amiga_time timeValue;

    timeValue = main_getAmigaTime();
    main_writeBe32(block + offset, timeValue.days);
    main_writeBe32(block + offset + 4, timeValue.mins);
    main_writeBe32(block + offset + 8, timeValue.ticks);
}

static void
main_writeRootDates(uint8_t *block)
{
    main_amiga_time timeValue;

    timeValue = main_getAmigaTime();
    main_writeBe32(block + 420, timeValue.days);
    main_writeBe32(block + 424, timeValue.mins);
    main_writeBe32(block + 428, timeValue.ticks);
    main_writeBe32(block + 472, timeValue.days);
    main_writeBe32(block + 476, timeValue.mins);
    main_writeBe32(block + 480, timeValue.ticks);
    main_writeBe32(block + 484, timeValue.days);
    main_writeBe32(block + 488, timeValue.mins);
    main_writeBe32(block + 492, timeValue.ticks);
}

static void
main_getBootBlock(uint8_t *bootOut, size_t *bootSizeOut)
{
    size_t standardSize;

    memset(bootOut, 0, 1024);
    standardSize = sizeof(main_standardBootBlock);
    memcpy(bootOut, main_standardBootBlock, standardSize);
    if (bootSizeOut != NULL)
    {
        *bootSizeOut = standardSize;
    }
}

static void
main_initBootBlock(uint8_t *image, const main_image_info *imageInfo)
{
    uint8_t bootCode[1024];
    size_t bootCodeSize;
    uint8_t *boot;

    main_getBootBlock(bootCode, &bootCodeSize);
    boot = main_blockPtr(image, 0);
    memset(boot, 0, 1024);
    boot[0] = 'D';
    boot[1] = 'O';
    boot[2] = 'S';
    boot[3] = 0;
    main_writeBe32(boot + 8, imageInfo->rootBlock);
    memcpy(boot + 12, bootCode, bootCodeSize);
    main_writeBootChecksum(boot);
}

static uint32_t
main_getBitmapDataOffset(uint32_t blockNumber, uint32_t *bitmapIndexOut, uint32_t *longIndexOut, uint32_t *bitIndexOut)
{
    uint32_t position;
    uint32_t bitmapIndex;
    uint32_t localPosition;
    uint32_t longIndex;
    uint32_t bitIndex;

    position = blockNumber - 2;
    bitmapIndex = position / main_bitmapBlocksPerPage;
    localPosition = position % main_bitmapBlocksPerPage;
    longIndex = localPosition / 32;
    bitIndex = localPosition % 32;
    if (bitmapIndexOut != NULL)
    {
        *bitmapIndexOut = bitmapIndex;
    }
    if (longIndexOut != NULL)
    {
        *longIndexOut = longIndex;
    }
    if (bitIndexOut != NULL)
    {
        *bitIndexOut = bitIndex;
    }
    return position;
}

static int
main_isBlockFree(uint8_t *image, const main_image_info *imageInfo, uint32_t blockNumber)
{
    uint32_t bitmapIndex;
    uint32_t longIndex;
    uint32_t bitIndex;
    uint32_t value;
    uint8_t *bitmap;

    if (blockNumber < 2 || blockNumber >= imageInfo->totalBlocks)
    {
        return 0;
    }
    main_getBitmapDataOffset(blockNumber, &bitmapIndex, &longIndex, &bitIndex);
    if (bitmapIndex >= imageInfo->bitmapBlockCount)
    {
        return 0;
    }
    bitmap = main_blockPtr(image, imageInfo->bitmapBlocks[bitmapIndex]);
    value = main_readBe32(bitmap + 4 + (longIndex * 4));
    return (value & (1u << bitIndex)) != 0;
}

static void
main_setBlockUsed(uint8_t *image, const main_image_info *imageInfo, uint32_t blockNumber, int used)
{
    uint32_t bitmapIndex;
    uint32_t longIndex;
    uint32_t bitIndex;
    uint32_t value;
    uint8_t *bitmap;

    if (blockNumber < 2 || blockNumber >= imageInfo->totalBlocks)
    {
        return;
    }
    main_getBitmapDataOffset(blockNumber, &bitmapIndex, &longIndex, &bitIndex);
    if (bitmapIndex >= imageInfo->bitmapBlockCount)
    {
        return;
    }
    bitmap = main_blockPtr(image, imageInfo->bitmapBlocks[bitmapIndex]);
    value = main_readBe32(bitmap + 4 + (longIndex * 4));
    if (used != 0)
    {
        value &= ~(1u << bitIndex);
    }
    else
    {
        value |= (1u << bitIndex);
    }
    main_writeBe32(bitmap + 4 + (longIndex * 4), value);
}

static int
main_allocBlock(uint8_t *image, const main_image_info *imageInfo, uint32_t *blockOut)
{
    uint32_t blockNumber;

    for (blockNumber = 2; blockNumber < imageInfo->totalBlocks; blockNumber++)
    {
        if (main_isBlockFree(image, imageInfo, blockNumber) != 0)
        {
            main_setBlockUsed(image, imageInfo, blockNumber, 1);
            *blockOut = blockNumber;
            return 0;
        }
    }
    return -1;
}

static void
main_initRootBlock(uint8_t *image, const main_image_info *imageInfo, const char *label)
{
    uint8_t *root;
    uint32_t index;

    root = main_blockPtr(image, imageInfo->rootBlock);
    memset(root, 0, main_blockSize);
    main_writeBe32(root + 0, main_typeHeader);
    main_writeBe32(root + 4, 0);
    main_writeBe32(root + 8, 0);
    main_writeBe32(root + 12, main_hashTableSize);
    main_writeBe32(root + 16, 0);
    memset(root + 24, 0, main_hashTableSize * 4);
    main_writeBe32(root + 312, 0xffffffffu);
    for (index = 0; index < imageInfo->bitmapBlockCount; index++)
    {
        main_writeBe32(root + 316 + (index * 4), imageInfo->bitmapBlocks[index]);
    }
    main_writeBe32(root + 408, 0);
    main_writeRootDates(root);
    main_writeName(root, label);
    main_writeBe32(root + 504, 0);
    main_writeBe32(root + 508, (uint32_t)main_secRoot);
    main_writeChecksum(root, 20);
}

static void
main_initBitmap(uint8_t *image, const main_image_info *imageInfo)
{
    uint32_t bitmapIndex;
    uint32_t longIndex;
    uint32_t blockNumber;
    uint32_t longsPerBitmap;
    uint8_t *bitmap;

    longsPerBitmap = (main_blockSize - 4) / 4;
    for (bitmapIndex = 0; bitmapIndex < imageInfo->bitmapBlockCount; bitmapIndex++)
    {
        bitmap = main_blockPtr(image, imageInfo->bitmapBlocks[bitmapIndex]);
        memset(bitmap, 0, main_blockSize);
        for (longIndex = 0; longIndex < longsPerBitmap; longIndex++)
        {
            main_writeBe32(bitmap + 4 + (longIndex * 4), 0xffffffffu);
        }
    }
    main_setBlockUsed(image, imageInfo, imageInfo->rootBlock, 1);
    for (bitmapIndex = 0; bitmapIndex < imageInfo->bitmapBlockCount; bitmapIndex++)
    {
        main_setBlockUsed(image, imageInfo, imageInfo->bitmapBlocks[bitmapIndex], 1);
    }
    for (blockNumber = 2; blockNumber < imageInfo->totalBlocks; blockNumber++)
    {
        if ((blockNumber - 2) / main_bitmapBlocksPerPage >= imageInfo->bitmapBlockCount)
        {
            main_setBlockUsed(image, imageInfo, blockNumber, 1);
        }
    }
    for (bitmapIndex = 0; bitmapIndex < imageInfo->bitmapBlockCount; bitmapIndex++)
    {
        main_writeChecksum(main_blockPtr(image, imageInfo->bitmapBlocks[bitmapIndex]), 0);
    }
}

static int
main_readEntryName(uint8_t *block, char *nameOut, size_t nameOutSize)
{
    uint8_t length;

    length = block[432];
    if (length > main_maxNameLen)
    {
        length = main_maxNameLen;
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
main_readHashChain(uint8_t *block)
{
    return main_readBe32(block + 496);
}

static void
main_writeHashChain(uint8_t *block, uint32_t nextBlock)
{
    main_writeBe32(block + 496, nextBlock);
}

static void
main_writeParent(uint8_t *block, uint32_t parentBlock)
{
    main_writeBe32(block + 500, parentBlock);
}

static void
main_writeExtension(uint8_t *block, uint32_t extensionBlock)
{
    main_writeBe32(block + 504, extensionBlock);
}

static void
main_writeSecType(uint8_t *block, int32_t secType)
{
    main_writeBe32(block + 508, (uint32_t)secType);
}

static int
main_findEntry(uint8_t *image, const main_image_info *imageInfo, uint32_t dirBlockNumber, const char *name, uint32_t *blockOut, int32_t *secTypeOut)
{
    uint8_t *dirBlock;
    uint32_t hashValue;
    uint32_t entryBlockNumber;
    uint8_t *entryBlock;
    char entryName[64];
    uint32_t nextBlock;

    (void)imageInfo;
    dirBlock = main_blockPtr(image, dirBlockNumber);
    hashValue = (uint32_t)main_hashName(name);
    entryBlockNumber = main_readBe32(dirBlock + 24 + (hashValue * 4));
    while (entryBlockNumber != 0)
    {
        entryBlock = main_blockPtr(image, entryBlockNumber);
        if (main_readEntryName(entryBlock, entryName, sizeof(entryName)) == 0)
        {
            if (main_namesEqual(entryName, name) != 0)
            {
                if (blockOut != NULL)
                {
                    *blockOut = entryBlockNumber;
                }
                if (secTypeOut != NULL)
                {
                    *secTypeOut = (int32_t)main_readBe32(entryBlock + 508);
                }
                return 0;
            }
        }
        nextBlock = main_readHashChain(entryBlock);
        entryBlockNumber = nextBlock;
    }
    return -1;
}

static int
main_insertEntry(uint8_t *image, const main_image_info *imageInfo, uint32_t dirBlockNumber, uint32_t newEntryBlockNumber, const char *name)
{
    uint8_t *dirBlock;
    uint32_t hashValue;
    uint32_t entryBlockNumber;
    uint8_t *entryBlock;
    char entryName[64];
    uint32_t nextBlock;

    (void)imageInfo;
    dirBlock = main_blockPtr(image, dirBlockNumber);
    hashValue = (uint32_t)main_hashName(name);
    entryBlockNumber = main_readBe32(dirBlock + 24 + (hashValue * 4));
    if (entryBlockNumber == 0)
    {
        main_writeBe32(dirBlock + 24 + (hashValue * 4), newEntryBlockNumber);
        return 0;
    }
    while (entryBlockNumber != 0)
    {
        entryBlock = main_blockPtr(image, entryBlockNumber);
        if (main_readEntryName(entryBlock, entryName, sizeof(entryName)) == 0)
        {
            if (main_namesEqual(entryName, name) != 0)
            {
                return -1;
            }
        }
        nextBlock = main_readHashChain(entryBlock);
        if (nextBlock == 0)
        {
            main_writeHashChain(entryBlock, newEntryBlockNumber);
            main_writeChecksum(entryBlock, 20);
            return 0;
        }
        entryBlockNumber = nextBlock;
    }
    return -1;
}

static int
main_resolveDir(uint8_t *image, const main_image_info *imageInfo, const char *path, uint32_t *dirBlockOut)
{
    char *pathCopy;
    char *token;
    char *savePtr;
    uint32_t currentBlock;
    uint32_t nextBlock;
    int32_t secType;

    currentBlock = imageInfo->rootBlock;
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
        if (main_findEntry(image, imageInfo, currentBlock, token, &nextBlock, &secType) != 0)
        {
            free(pathCopy);
            return -1;
        }
        if (secType != main_secUserDir && secType != main_secRoot)
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
main_splitPath(const char *path, char *parentOut, size_t parentOutSize, char *nameOut, size_t nameOutSize)
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
main_initDirBlock(uint8_t *block, uint32_t blockNumber, uint32_t parentBlockNumber, const char *name)
{
    memset(block, 0, main_blockSize);
    main_writeBe32(block + 0, main_typeHeader);
    main_writeBe32(block + 4, blockNumber);
    main_writeBe32(block + 8, 0);
    main_writeBe32(block + 12, 0);
    main_writeBe32(block + 16, 0);
    memset(block + 24, 0, main_hashTableSize * 4);
    main_writeBe32(block + 320, 0);
    main_writeCommentEmpty(block);
    main_writeDate(block, 420);
    main_writeName(block, name);
    main_writeHashChain(block, 0);
    main_writeParent(block, parentBlockNumber);
    main_writeExtension(block, 0);
    main_writeSecType(block, main_secUserDir);
    main_writeChecksum(block, 20);
}

static void
main_initFileHeader(uint8_t *block, uint32_t blockNumber, uint32_t parentBlockNumber, const char *name)
{
    memset(block, 0, main_blockSize);
    main_writeBe32(block + 0, main_typeHeader);
    main_writeBe32(block + 4, blockNumber);
    main_writeBe32(block + 8, 0);
    main_writeBe32(block + 12, 0);
    main_writeBe32(block + 16, 0);
    memset(block + 24, 0, main_hashTableSize * 4);
    main_writeBe32(block + 320, 0);
    main_writeBe32(block + 324, 0);
    main_writeCommentEmpty(block);
    main_writeDate(block, 420);
    main_writeName(block, name);
    main_writeHashChain(block, 0);
    main_writeParent(block, parentBlockNumber);
    main_writeExtension(block, 0);
    main_writeSecType(block, main_secFile);
}

static void
main_initFileExt(uint8_t *block, uint32_t blockNumber, uint32_t parentBlockNumber, uint32_t nextExt)
{
    memset(block, 0, main_blockSize);
    main_writeBe32(block + 0, main_typeList);
    main_writeBe32(block + 4, blockNumber);
    main_writeBe32(block + 8, 0);
    main_writeBe32(block + 12, 0);
    main_writeBe32(block + 16, 0);
    memset(block + 24, 0, main_hashTableSize * 4);
    main_writeParent(block, parentBlockNumber);
    main_writeExtension(block, nextExt);
    main_writeSecType(block, main_secFile);
}

static void
main_fillDataBlockPointers(uint8_t *block, const uint32_t *dataBlocks, uint32_t count)
{
    uint32_t index;
    uint32_t tableIndex;

    main_writeBe32(block + 8, count);
    for (index = 0; index < count; index++)
    {
        tableIndex = (main_hashTableSize - 1) - index;
        main_writeBe32(block + 24 + (tableIndex * 4), dataBlocks[index]);
    }
}

static void
main_writeFileDataBlocks(uint8_t *image, const uint8_t *data, size_t dataSize, uint32_t headerBlockNumber, uint32_t *dataBlocks, uint32_t dataBlockCount)
{
    uint32_t index;
    uint32_t blockNumber;
    uint8_t *block;
    size_t offset;
    size_t remaining;
    uint32_t chunk;

    offset = 0;
    for (index = 0; index < dataBlockCount; index++)
    {
        blockNumber = dataBlocks[index];
        block = main_blockPtr(image, blockNumber);
        memset(block, 0, main_blockSize);
        main_writeBe32(block + 0, main_typeData);
        main_writeBe32(block + 4, headerBlockNumber);
        main_writeBe32(block + 8, index + 1);
        remaining = dataSize - offset;
        chunk = remaining > main_dataBytesPerBlock ? main_dataBytesPerBlock : (uint32_t)remaining;
        main_writeBe32(block + 12, chunk);
        if (index + 1 < dataBlockCount)
        {
            main_writeBe32(block + 16, dataBlocks[index + 1]);
        }
        else
        {
            main_writeBe32(block + 16, 0);
        }
        if (chunk > 0)
        {
            memcpy(block + 24, data + offset, chunk);
        }
        main_writeChecksum(block, 20);
        offset += chunk;
    }
}

static void
main_refreshMetadata(uint8_t *image, const main_image_info *imageInfo, uint32_t parentBlock)
{
    uint8_t *parent;
    uint8_t *root;
    uint32_t bitmapIndex;

    parent = main_blockPtr(image, parentBlock);
    main_writeDate(parent, 420);
    main_writeChecksum(parent, 20);
    root = main_blockPtr(image, imageInfo->rootBlock);
    main_writeRootDates(root);
    main_writeChecksum(root, 20);
    for (bitmapIndex = 0; bitmapIndex < imageInfo->bitmapBlockCount; bitmapIndex++)
    {
        main_writeChecksum(main_blockPtr(image, imageInfo->bitmapBlocks[bitmapIndex]), 0);
    }
}

static int
main_makeDir(uint8_t *image, const main_image_info *imageInfo, const char *destPath)
{
    char parentPath[512];
    char dirName[64];
    uint32_t parentBlock;
    uint32_t dirBlock;
    uint8_t *dirBlockPtr;
    int32_t secType;

    if (main_splitPath(destPath, parentPath, sizeof(parentPath), dirName, sizeof(dirName)) != 0)
    {
        return -1;
    }
    if (strlen(dirName) == 0 || strlen(dirName) > main_maxNameLen)
    {
        return -1;
    }
    if (main_resolveDir(image, imageInfo, parentPath, &parentBlock) != 0)
    {
        return -1;
    }
    if (main_findEntry(image, imageInfo, parentBlock, dirName, NULL, &secType) == 0)
    {
        return -1;
    }
    if (main_allocBlock(image, imageInfo, &dirBlock) != 0)
    {
        return -1;
    }
    dirBlockPtr = main_blockPtr(image, dirBlock);
    main_initDirBlock(dirBlockPtr, dirBlock, parentBlock, dirName);
    if (main_insertEntry(image, imageInfo, parentBlock, dirBlock, dirName) != 0)
    {
        return -1;
    }
    main_refreshMetadata(image, imageInfo, parentBlock);
    return 0;
}

static int
main_addFile(uint8_t *image, const main_image_info *imageInfo, const char *srcPath, const char *destPath)
{
    char parentPath[512];
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

    if (main_splitPath(destPath, parentPath, sizeof(parentPath), fileName, sizeof(fileName)) != 0)
    {
        return -1;
    }
    if (strlen(fileName) == 0 || strlen(fileName) > main_maxNameLen)
    {
        return -1;
    }
    if (main_resolveDir(image, imageInfo, parentPath, &parentBlock) != 0)
    {
        return -1;
    }
    if (main_findEntry(image, imageInfo, parentBlock, fileName, NULL, &secType) == 0)
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
    dataBlockCount = (uint32_t)((fileSize + main_dataBytesPerBlock - 1) / main_dataBytesPerBlock);
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
            if (main_allocBlock(image, imageInfo, &dataBlocks[dataIndex]) != 0)
            {
                free(dataBlocks);
                free(fileData);
                return -1;
            }
        }
    }
    if (main_allocBlock(image, imageInfo, &headerBlock) != 0)
    {
        free(dataBlocks);
        free(fileData);
        return -1;
    }
    header = main_blockPtr(image, headerBlock);
    main_initFileHeader(header, headerBlock, parentBlock, fileName);
    main_writeBe32(header + 324, (uint32_t)fileSize);
    if (dataBlockCount > 0)
    {
        main_writeBe32(header + 16, dataBlocks[0]);
        remainingBlocks = dataBlockCount;
        countThisBlock = remainingBlocks > main_hashTableSize ? main_hashTableSize : remainingBlocks;
        main_fillDataBlockPointers(header, dataBlocks, countThisBlock);
    }
    extBlockCount = 0;
    if (dataBlockCount > main_hashTableSize)
    {
        extBlockCount = (dataBlockCount - main_hashTableSize + main_hashTableSize - 1) / main_hashTableSize;
    }
    nextExtBlockNumber = 0;
    if (extBlockCount > 0)
    {
        if (main_allocBlock(image, imageInfo, &nextExtBlockNumber) != 0)
        {
            free(dataBlocks);
            free(fileData);
            return -1;
        }
        main_writeExtension(header, nextExtBlockNumber);
    }
    dataIndex = main_hashTableSize;
    for (extIndex = 0; extIndex < extBlockCount; extIndex++)
    {
        extBlockNumber = nextExtBlockNumber;
        if (extIndex + 1 < extBlockCount)
        {
            if (main_allocBlock(image, imageInfo, &nextExtBlockNumber) != 0)
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
        extBlock = main_blockPtr(image, extBlockNumber);
        main_initFileExt(extBlock, extBlockNumber, headerBlock, nextExtBlockNumber);
        remainingBlocks = dataBlockCount - dataIndex;
        countThisBlock = remainingBlocks > main_hashTableSize ? main_hashTableSize : remainingBlocks;
        if (countThisBlock > 0)
        {
            main_fillDataBlockPointers(extBlock, dataBlocks + dataIndex, countThisBlock);
        }
        dataIndex += countThisBlock;
        main_writeChecksum(extBlock, 20);
    }
    main_writeChecksum(header, 20);
    if (dataBlockCount > 0)
    {
        main_writeFileDataBlocks(image, fileData, fileSize, headerBlock, dataBlocks, dataBlockCount);
    }
    free(fileData);
    free(dataBlocks);
    if (main_insertEntry(image, imageInfo, parentBlock, headerBlock, fileName) != 0)
    {
        return -1;
    }
    main_refreshMetadata(image, imageInfo, parentBlock);
    return 0;
}

static int
main_validateName(const char *name)
{
    size_t length;

    length = strlen(name);
    if (length == 0 || length > main_maxNameLen)
    {
        return -1;
    }
    if (strchr(name, '/') != NULL || strchr(name, ':') != NULL)
    {
        return -1;
    }
    return 0;
}

static char *
main_joinPath(const char *left, const char *right, int rootStyle)
{
    size_t leftLength;
    size_t rightLength;
    size_t totalLength;
    char *joined;

    leftLength = strlen(left);
    rightLength = strlen(right);
    totalLength = leftLength + rightLength + 2;
    joined = (char *)malloc(totalLength);
    if (joined == NULL)
    {
        return NULL;
    }
    if (rootStyle != 0 && strcmp(left, "/") == 0)
    {
        joined[0] = '/';
        memcpy(joined + 1, right, rightLength);
        joined[1 + rightLength] = '\0';
    }
    else
    {
        memcpy(joined, left, leftLength);
        joined[leftLength] = '/';
        memcpy(joined + leftLength + 1, right, rightLength);
        joined[leftLength + 1 + rightLength] = '\0';
    }
    return joined;
}

static int
main_compareDirEntries(const void *leftValue, const void *rightValue)
{
    const main_dir_entry *left;
    const main_dir_entry *right;

    left = (const main_dir_entry *)leftValue;
    right = (const main_dir_entry *)rightValue;
    return strcmp(left->name, right->name);
}

static void
main_freeDirEntries(main_dir_entry *entries, size_t count)
{
    size_t index;

    if (entries == NULL)
    {
        return;
    }
    for (index = 0; index < count; index++)
    {
        free(entries[index].name);
    }
    free(entries);
}

static int
main_readDirEntries(const char *sourcePath, main_dir_entry **entriesOut, size_t *countOut)
{
    DIR *dir;
    struct dirent *entry;
    main_dir_entry *entries;
    size_t count;
    size_t capacity;
    char *fullPath;
    struct stat statInfo;

    dir = opendir(sourcePath);
    if (dir == NULL)
    {
        return -1;
    }
    entries = NULL;
    count = 0;
    capacity = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }
        if (main_validateName(entry->d_name) != 0)
        {
            fprintf(stderr, "Invalid Amiga name: %s\n", entry->d_name);
            main_freeDirEntries(entries, count);
            closedir(dir);
            return -1;
        }
        fullPath = main_joinPath(sourcePath, entry->d_name, 0);
        if (fullPath == NULL)
        {
            main_freeDirEntries(entries, count);
            closedir(dir);
            return -1;
        }
        if (stat(fullPath, &statInfo) != 0)
        {
            free(fullPath);
            main_freeDirEntries(entries, count);
            closedir(dir);
            return -1;
        }
        free(fullPath);
        if (!S_ISDIR(statInfo.st_mode) && !S_ISREG(statInfo.st_mode))
        {
            fprintf(stderr, "Unsupported entry type: %s/%s\n", sourcePath, entry->d_name);
            main_freeDirEntries(entries, count);
            closedir(dir);
            return -1;
        }
        if (count == capacity)
        {
            main_dir_entry *newEntries;

            capacity = capacity == 0 ? 16 : capacity * 2;
            newEntries = (main_dir_entry *)realloc(entries, sizeof(main_dir_entry) * capacity);
            if (newEntries == NULL)
            {
                main_freeDirEntries(entries, count);
                closedir(dir);
                return -1;
            }
            entries = newEntries;
        }
        entries[count].name = strdup(entry->d_name);
        if (entries[count].name == NULL)
        {
            main_freeDirEntries(entries, count);
            closedir(dir);
            return -1;
        }
        entries[count].isDir = S_ISDIR(statInfo.st_mode) ? 1 : 0;
        count++;
    }
    closedir(dir);
    qsort(entries, count, sizeof(main_dir_entry), main_compareDirEntries);
    *entriesOut = entries;
    *countOut = count;
    return 0;
}

static int
main_scanFolder(const char *sourcePath, main_scan_info *scanInfo)
{
    main_dir_entry *entries;
    size_t entryCount;
    size_t entryIndex;
    char *fullPath;
    struct stat statInfo;
    uint64_t dataBlockCount;
    uint64_t extBlockCount;
    int result;

    if (main_readDirEntries(sourcePath, &entries, &entryCount) != 0)
    {
        return -1;
    }
    for (entryIndex = 0; entryIndex < entryCount; entryIndex++)
    {
        fullPath = main_joinPath(sourcePath, entries[entryIndex].name, 0);
        if (fullPath == NULL)
        {
            main_freeDirEntries(entries, entryCount);
            return -1;
        }
        if (stat(fullPath, &statInfo) != 0)
        {
            free(fullPath);
            main_freeDirEntries(entries, entryCount);
            return -1;
        }
        if (entries[entryIndex].isDir != 0)
        {
            scanInfo->contentBlocks += 1;
            result = main_scanFolder(fullPath, scanInfo);
            free(fullPath);
            if (result != 0)
            {
                main_freeDirEntries(entries, entryCount);
                return -1;
            }
        }
        else
        {
            dataBlockCount = ((uint64_t)statInfo.st_size + main_dataBytesPerBlock - 1) / main_dataBytesPerBlock;
            extBlockCount = 0;
            if (dataBlockCount > main_hashTableSize)
            {
                extBlockCount = (dataBlockCount - main_hashTableSize + main_hashTableSize - 1) / main_hashTableSize;
            }
            scanInfo->contentBlocks += 1 + dataBlockCount + extBlockCount;
            free(fullPath);
        }
    }
    main_freeDirEntries(entries, entryCount);
    return 0;
}

static uint32_t
main_alignUp(uint32_t value, uint32_t multiple)
{
    if (multiple == 0)
    {
        return value;
    }
    return ((value + multiple - 1) / multiple) * multiple;
}

static int
main_buildImageInfo(const main_scan_info *scanInfo, main_image_info *imageInfo)
{
    uint64_t slackBlocks;
    uint64_t baseBlocks;
    uint32_t totalBlocks;
    uint32_t bitmapBlockCount;
    uint32_t prevBitmapCount;
    uint32_t bitmapIndex;

    slackBlocks = scanInfo->contentBlocks / 10;
    if (slackBlocks < 128)
    {
        slackBlocks = 128;
    }
    baseBlocks = 2 + 1 + scanInfo->contentBlocks + slackBlocks;
    if (baseBlocks < 256)
    {
        baseBlocks = 256;
    }
    totalBlocks = main_alignUp((uint32_t)baseBlocks, 32);
    bitmapBlockCount = 0;
    do
    {
        prevBitmapCount = bitmapBlockCount;
        bitmapBlockCount = (totalBlocks - 2 + main_bitmapBlocksPerPage - 1) / main_bitmapBlocksPerPage;
        totalBlocks = main_alignUp((uint32_t)(baseBlocks + bitmapBlockCount), 32);
    } while (bitmapBlockCount != prevBitmapCount);
    if (bitmapBlockCount == 0 || bitmapBlockCount > main_maxBitmapBlocks)
    {
        fprintf(stderr, "Folder is too large for this simple HDF layout.\n");
        return -1;
    }
    imageInfo->totalBlocks = totalBlocks;
    imageInfo->rootBlock = (2 + (totalBlocks - 1)) / 2;
    imageInfo->bitmapBlockCount = bitmapBlockCount;
    for (bitmapIndex = 0; bitmapIndex < bitmapBlockCount; bitmapIndex++)
    {
        imageInfo->bitmapBlocks[bitmapIndex] = imageInfo->rootBlock + 1 + bitmapIndex;
    }
    return 0;
}

static int
main_importFolder(uint8_t *image, const main_image_info *imageInfo, const char *sourcePath, const char *destPath)
{
    main_dir_entry *entries;
    size_t entryCount;
    size_t entryIndex;
    char *fullPath;
    char *destChildPath;
    int result;

    if (main_readDirEntries(sourcePath, &entries, &entryCount) != 0)
    {
        return -1;
    }
    for (entryIndex = 0; entryIndex < entryCount; entryIndex++)
    {
        fullPath = main_joinPath(sourcePath, entries[entryIndex].name, 0);
        if (fullPath == NULL)
        {
            main_freeDirEntries(entries, entryCount);
            return -1;
        }
        destChildPath = main_joinPath(destPath, entries[entryIndex].name, 1);
        if (destChildPath == NULL)
        {
            free(fullPath);
            main_freeDirEntries(entries, entryCount);
            return -1;
        }
        if (entries[entryIndex].isDir != 0)
        {
            result = main_makeDir(image, imageInfo, destChildPath);
            if (result == 0)
            {
                result = main_importFolder(image, imageInfo, fullPath, destChildPath);
            }
        }
        else
        {
            result = main_addFile(image, imageInfo, fullPath, destChildPath);
        }
        free(destChildPath);
        free(fullPath);
        if (result != 0)
        {
            main_freeDirEntries(entries, entryCount);
            return -1;
        }
    }
    main_freeDirEntries(entries, entryCount);
    return 0;
}

static int
main_writeImage(const char *path, const uint8_t *image, const main_image_info *imageInfo)
{
    FILE *file;
    size_t writeCount;
    size_t imageSize;

    file = fopen(path, "wb");
    if (file == NULL)
    {
        return -1;
    }
    imageSize = (size_t)imageInfo->totalBlocks * main_blockSize;
    writeCount = fwrite(image, 1, imageSize, file);
    fclose(file);
    if (writeCount != imageSize)
    {
        return -1;
    }
    return 0;
}

static const char *
main_pathBaseName(const char *path)
{
    const char *end;
    const char *start;

    end = path + strlen(path);
    while (end > path && end[-1] == '/')
    {
        end--;
    }
    start = end;
    while (start > path && start[-1] != '/')
    {
        start--;
    }
    return start;
}

static char *
main_defaultOutputPath(const char *sourcePath)
{
    const char *baseName;
    size_t baseLength;
    char *outputPath;

    baseName = main_pathBaseName(sourcePath);
    baseLength = strlen(baseName);
    while (baseLength > 0 && baseName[baseLength - 1] == '/')
    {
        baseLength--;
    }
    if (baseLength == 0)
    {
        return strdup("output.hdf");
    }
    outputPath = (char *)malloc(baseLength + 5);
    if (outputPath == NULL)
    {
        return NULL;
    }
    memcpy(outputPath, baseName, baseLength);
    memcpy(outputPath + baseLength, ".hdf", 5);
    return outputPath;
}

static char *
main_defaultLabel(const char *sourcePath)
{
    const char *baseName;
    size_t baseLength;
    char *label;

    baseName = main_pathBaseName(sourcePath);
    baseLength = strlen(baseName);
    while (baseLength > 0 && baseName[baseLength - 1] == '/')
    {
        baseLength--;
    }
    if (baseLength == 0)
    {
        return strdup("HDF9000");
    }
    if (baseLength > main_maxNameLen)
    {
        baseLength = main_maxNameLen;
    }
    label = (char *)malloc(baseLength + 1);
    if (label == NULL)
    {
        return NULL;
    }
    memcpy(label, baseName, baseLength);
    label[baseLength] = '\0';
    return label;
}

static void
main_printUsage(void)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  hdf9000 <folder> [output.hdf] [--label <name>] [--time <unix>]\n");
}

int
main(int argc, char **argv)
{
    const char *sourcePath;
    char *outputPath;
    char *label;
    int argIndex;
    const char *timeArg;
    char *timeEnd;
    unsigned long long timeValue;
    struct stat statInfo;
    main_scan_info scanInfo;
    main_image_info imageInfo;
    uint8_t *image;
    int result;

    if (argc < 2)
    {
        main_printUsage();
        return 1;
    }
    sourcePath = argv[1];
    outputPath = NULL;
    label = NULL;
    argIndex = 2;
    if (argIndex < argc && strncmp(argv[argIndex], "--", 2) != 0)
    {
        outputPath = strdup(argv[argIndex]);
        if (outputPath == NULL)
        {
            return 1;
        }
        argIndex++;
    }
    while (argIndex < argc)
    {
        if (strcmp(argv[argIndex], "--label") == 0)
        {
            if (argIndex + 1 >= argc)
            {
                main_printUsage();
                free(outputPath);
                return 1;
            }
            free(label);
            label = strdup(argv[argIndex + 1]);
            if (label == NULL)
            {
                free(outputPath);
                return 1;
            }
            argIndex += 2;
            continue;
        }
        if (strcmp(argv[argIndex], "--time") == 0)
        {
            if (argIndex + 1 >= argc)
            {
                main_printUsage();
                free(outputPath);
                free(label);
                return 1;
            }
            timeArg = argv[argIndex + 1];
            errno = 0;
            timeValue = strtoull(timeArg, &timeEnd, 10);
            if (errno != 0 || timeEnd == timeArg || *timeEnd != '\0')
            {
                fprintf(stderr, "Invalid --time value.\n");
                free(outputPath);
                free(label);
                return 1;
            }
            main_fixedUnixTime = (time_t)timeValue;
            main_hasFixedUnixTime = 1;
            argIndex += 2;
            continue;
        }
        main_printUsage();
        free(outputPath);
        free(label);
        return 1;
    }
    if (stat(sourcePath, &statInfo) != 0 || !S_ISDIR(statInfo.st_mode))
    {
        fprintf(stderr, "Source folder not found: %s\n", sourcePath);
        free(outputPath);
        free(label);
        return 1;
    }
    if (outputPath == NULL)
    {
        outputPath = main_defaultOutputPath(sourcePath);
        if (outputPath == NULL)
        {
            free(label);
            return 1;
        }
    }
    if (label == NULL)
    {
        label = main_defaultLabel(sourcePath);
        if (label == NULL)
        {
            free(outputPath);
            return 1;
        }
    }
    if (main_validateName(label) != 0)
    {
        fprintf(stderr, "Label must be 1..30 characters and must not contain '/' or ':'.\n");
        free(outputPath);
        free(label);
        return 1;
    }
    memset(&scanInfo, 0, sizeof(scanInfo));
    if (main_scanFolder(sourcePath, &scanInfo) != 0)
    {
        free(outputPath);
        free(label);
        return 1;
    }
    if (main_buildImageInfo(&scanInfo, &imageInfo) != 0)
    {
        free(outputPath);
        free(label);
        return 1;
    }
    image = (uint8_t *)calloc(imageInfo.totalBlocks, main_blockSize);
    if (image == NULL)
    {
        free(outputPath);
        free(label);
        return 1;
    }
    main_initBootBlock(image, &imageInfo);
    main_initRootBlock(image, &imageInfo, label);
    main_initBitmap(image, &imageInfo);
    result = main_importFolder(image, &imageInfo, sourcePath, "/");
    if (result == 0)
    {
        result = main_writeImage(outputPath, image, &imageInfo);
    }
    free(image);
    if (result != 0)
    {
        fprintf(stderr, "Failed to create HDF image.\n");
        free(outputPath);
        free(label);
        return 1;
    }
    printf("%s\n", outputPath);
    free(outputPath);
    free(label);
    return 0;
}
