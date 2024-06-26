#include <kernel.h>
#include <errno.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libcdvd.h>
#include <libpad.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sbv_patches.h>
#include <osd_config.h>
#include <timer.h>
#include <limits.h>

#include <libgs.h>

#include <speedregs.h>
#include <smapregs.h>
#include <dev9regs.h>

#include "sysman/sysinfo.h"
#include "SYSMAN_rpc.h"

#include "main.h"
#include "ident.h"
#include "pad.h"
#include "graphics.h"
#include "libcdvd_add.h"
#include "dvdplayer.h"
#include "OSDInit.h"
#include "ps1.h"
#include "modelname.h"

#include "UI.h"
#include "menu.h"
#include "crc16.h"
#include "dbms.h"

extern struct UIDrawGlobal UIDrawGlobal;

#define GS_REG_CSR (volatile u64 *)0x12001000 // System Status

int GetEEInformation(struct SystemInformation *SystemInformation)
{
    unsigned short int revision;
    unsigned int value;

    revision                                       = GetCop0(15);
    SystemInformation->mainboard.ee.implementation = revision >> 8;
    SystemInformation->mainboard.ee.revision       = revision & 0xFF;

    asm("cfc1 %0, $0\n"
        : "=r"(revision)
        :);
    SystemInformation->mainboard.ee.FPUImplementation = revision >> 8;
    SystemInformation->mainboard.ee.FPURevision       = revision & 0xFF;

    value                                             = GetCop0(16);
    SystemInformation->mainboard.ee.ICacheSize        = value >> 9 & 3;
    SystemInformation->mainboard.ee.DCacheSize        = value >> 6 & 3;
    SystemInformation->mainboard.ee.RAMSize           = GetMemorySize();
    SystemInformation->mainboard.MachineType          = MachineType();

    revision                                          = (*GS_REG_CSR) >> 16;
    SystemInformation->mainboard.gs.revision          = revision & 0xFF;
    SystemInformation->mainboard.gs.id                = revision >> 8;

    ee_kmode_enter();
    SystemInformation->EE_F520 = *(volatile unsigned int *)0xB000F520;
    SystemInformation->EE_F540 = *(volatile unsigned int *)0xB000F540;
    SystemInformation->EE_F550 = *(volatile unsigned int *)0xB000F550;
    ee_kmode_exit();

    return 0;
}

static u16 CalculateCRCOfROM(void *buffer1, void *buffer2, void *start, unsigned int length)
{
    u16 crc;
    unsigned int i, size = 0, prevSize;
    void *pDestBuffer, *pSrcBuffer;

    for (i = 0, prevSize = size, crc = CRC16_INITIAL_CHECKSUM, pDestBuffer = buffer1, pSrcBuffer = start; i < length; i += size, pSrcBuffer += size)
    {
        size = length - i > MEM_IO_BLOCK_SIZE ? MEM_IO_BLOCK_SIZE : length - i;

        SysmanSync(0);
        while (SysmanReadMemory(pSrcBuffer, pDestBuffer, size, 1) != 0)
            nopdelay();

        pDestBuffer = (pDestBuffer == buffer1) ? buffer2 : buffer1;
        if (i > 0)
            crc = CalculateCRC16(UNCACHED_SEG(pDestBuffer), prevSize, crc);
        prevSize = size;
    }

    pDestBuffer = (pDestBuffer == buffer1) ? buffer2 : buffer1;
    SysmanSync(0);
    return ReflectAndXORCRC16(CalculateCRC16(UNCACHED_SEG(pDestBuffer), prevSize, crc));
}

int CheckROM(const struct PS2IDBMainboardEntry *entry)
{
    const struct PS2IDBMainboardEntry *other;

    if ((other = PS2IDBMS_LookupMatchingROM(entry)) != NULL)
    {
        if ((entry->BOOT_ROM.IsExists && (other->BOOT_ROM.crc16 != entry->BOOT_ROM.crc16)) || (other->DVD_ROM.IsExists && (other->DVD_ROM.crc16 != entry->DVD_ROM.crc16)))
        {
            printf("CheckROM: ROM mismatch:\n");
            if (entry->BOOT_ROM.IsExists)
                printf("    BOOT: 0x%04x 0x%04x\n", other->BOOT_ROM.crc16, entry->BOOT_ROM.crc16);
            if (other->DVD_ROM.IsExists)
                printf("    DVD: 0x%04x 0x%04x\n", other->DVD_ROM.crc16, entry->DVD_ROM.crc16);

            return 1;
        }
    }

    return 0;
}

/* const char *getBOOTROMnameFromcrc(unsigned int crc, const struct PS2IDBMainboardEntry *SystemInformation)
{
    const char *description;

    if (!strcmp(SystemInformation->romver, "0100JC20000117"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x6ba5)
            description = "0100JC20000117";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0101JC20000217"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x0d4d)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0101JD20000217"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xfec5)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0101XD20000224"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x5144)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0110AC20000727"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x09bc)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0110AD20000727"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x8688)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0120AC20000902"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x9a13)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0120EC20000902"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xefb8)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0120ED20000902"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x77b2)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0120JC20001027"))
    {
        if ((SystemInformation->BOOT_ROM.crc16 == 0x1c53) || (SystemInformation->BOOT_ROM.crc16 == 0xfc64))
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0150AC20001228"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xf59a)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0150AD20001228"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xf019)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0150EC20001228"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xeb2f)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0150ED20001228"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x176e)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0150JC20010118"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xda08)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0150JD20010118"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x5079)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0160AC20010427"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x162f)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0160JC20010427"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x44b3)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0160AC20010704"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xaa4c)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0160EC20010704"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xf387)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0160HC20010730"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x571d)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0160AC20011004"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xb0e9)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0160EC20011004"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x5e19)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0160AC20020207"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x9678)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0160EC20020319"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xa9f7)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0160EC20020426"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xe7c4)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0160HC20020426"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x95d7)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0160JC20020426"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x77b5)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0170JC20030206"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xaba4)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0180CD20030224"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x2896)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0170EC20030227"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x6d46)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0170ED20030227"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xa9bb)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0170AC20030325"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x6f26)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0170AD20030325"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x854d)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0190AC20030623"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xc1f4)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0190CC20030623"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x89ad)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0190EC20030623"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xf22a)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0190HC20030623"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x2829)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0190EC20030822"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x7aaf)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0190JC20030822"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xfa1e)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0180JC20031028"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xc986)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0200AC20040614"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x5fc6)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0200EC20040614"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0xd257)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0200HC20040614"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x2dc3)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0200JC20040614"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x2150)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if (!strcmp(SystemInformation->romver, "0210JC20040917"))
    {
        if (SystemInformation->BOOT_ROM.crc16 == 0x7631)
            description = "Correct";
        else
            description = "Modchip";
    }
    else if ((!strncmp(SystemInformation->romver, "022", 3) ||
              !strncmp(SystemInformation->romver, "023", 3) ||
              !strncmp(SystemInformation->romver, "025", 3)) &&
             (SystemInformation->romver[5] == 'C'))
    {
        description = "Deckard Retail";
    }
    // TODO: add dumped Deckard Debug detection (0220?D20060905)
    else
    {
        description = "Missing";
    }

    return description;
} */

int GetPeripheralInformation(struct SystemInformation *SystemInformation)
{
    t_SysmanHardwareInfo hwinfo;
    int result, fd, i;
    u32 stat;
    void *buffer1, *buffer2;
    char *pNewline;

    SysmanGetHardwareInfo(&hwinfo);

    memcpy(&SystemInformation->mainboard.iop, &hwinfo.iop, sizeof(SystemInformation->mainboard.iop));
    memcpy(SystemInformation->ROMs, hwinfo.ROMs, sizeof(SystemInformation->ROMs));
    memcpy(&SystemInformation->erom, &hwinfo.erom, sizeof(SystemInformation->erom));
    memcpy(&SystemInformation->mainboard.BOOT_ROM, &hwinfo.BOOT_ROM, sizeof(SystemInformation->mainboard.BOOT_ROM));
    memcpy(&SystemInformation->mainboard.DVD_ROM, &hwinfo.DVD_ROM, sizeof(SystemInformation->mainboard.DVD_ROM));
    memcpy(&SystemInformation->mainboard.ssbus, &hwinfo.ssbus, sizeof(SystemInformation->mainboard.ssbus));
    memcpy(&SystemInformation->mainboard.iLink, &hwinfo.iLink, sizeof(SystemInformation->mainboard.iLink));
    memcpy(&SystemInformation->mainboard.usb, &hwinfo.usb, sizeof(SystemInformation->mainboard.usb));
    memcpy(&SystemInformation->mainboard.spu2, &hwinfo.spu2, sizeof(SystemInformation->mainboard.spu2));
    SystemInformation->mainboard.BoardInf         = hwinfo.BoardInf;
    SystemInformation->mainboard.MPUBoardID       = hwinfo.MPUBoardID;
    SystemInformation->mainboard.ROMGEN_MonthDate = hwinfo.ROMGEN_MonthDate;
    SystemInformation->mainboard.ROMGEN_Year      = hwinfo.ROMGEN_Year;
    SystemInformation->mainboard.status           = 0;

    buffer1                                       = memalign(64, MEM_IO_BLOCK_SIZE);
    buffer2                                       = memalign(64, MEM_IO_BLOCK_SIZE);

    if (SystemInformation->mainboard.BOOT_ROM.IsExists)
    {
        SystemInformation->mainboard.BOOT_ROM.crc16 = CalculateCRCOfROM(buffer1, buffer2, (void *)SystemInformation->mainboard.BOOT_ROM.StartAddress, SystemInformation->mainboard.BOOT_ROM.size);
        DEBUG_PRINTF("BOOT ROM CRC16: 0x%04x\n", SystemInformation->mainboard.BOOT_ROM.crc16);
    }

    if (SystemInformation->mainboard.DVD_ROM.IsExists)
    {
        SystemInformation->mainboard.DVD_ROM.crc16 = CalculateCRCOfROM(buffer1, buffer2, (void *)SystemInformation->mainboard.DVD_ROM.StartAddress, SystemInformation->mainboard.DVD_ROM.size);
        DEBUG_PRINTF("DVD ROM CRC16: 0x%04x\n", SystemInformation->mainboard.DVD_ROM.crc16);
    }

    free(buffer1);
    free(buffer2);

    //Initialize model name
    if (ModelNameInit() == 0)
    {
        //Get model name
        strncpy(SystemInformation->mainboard.ModelName, ModelNameGet(), sizeof(SystemInformation->mainboard.ModelName) - 1);
        SystemInformation->mainboard.ModelName[sizeof(SystemInformation->mainboard.ModelName) - 1] = '\0';
    }
    else
    {
        SystemInformation->mainboard.status |= PS2IDB_STAT_ERR_MNAME;
        SystemInformation->mainboard.ModelName[0] = '\0';
    }

    //Get DVD Player version
    strncpy(SystemInformation->DVDPlayerVer, DVDPlayerGetVersion(), sizeof(SystemInformation->DVDPlayerVer) - 1);
    SystemInformation->DVDPlayerVer[sizeof(SystemInformation->DVDPlayerVer) - 1] = '\0';
    if ((pNewline = strrchr(SystemInformation->DVDPlayerVer, '\n')) != NULL)
        *pNewline = '\0'; //The DVD player version may have a newline in it.

    //Get OSD Player version
    strncpy(SystemInformation->OSDVer, OSDGetVersion(), sizeof(SystemInformation->OSDVer) - 1);
    SystemInformation->OSDVer[sizeof(SystemInformation->OSDVer) - 1] = '\0';
    if ((pNewline = strrchr(SystemInformation->OSDVer, '\n')) != NULL)
        *pNewline = '\0'; //The OSDVer may have a newline in it.

    //Get PS1DRV version
    strncpy(SystemInformation->PS1DRVVer, PS1DRVGetVersion(), sizeof(SystemInformation->PS1DRVVer) - 1);
    SystemInformation->PS1DRVVer[sizeof(SystemInformation->PS1DRVVer) - 1] = '\0';

    memset(SystemInformation->ConsoleID, 0, sizeof(SystemInformation->ConsoleID));
    memset(SystemInformation->iLinkID, 0, sizeof(SystemInformation->iLinkID));
    memset(SystemInformation->SMAP_MAC_address, 0, sizeof(SystemInformation->SMAP_MAC_address));
    memset(SystemInformation->mainboard.MECHACONVersion, 0, sizeof(SystemInformation->mainboard.MECHACONVersion));
    memset(SystemInformation->DSPVersion, 0, sizeof(SystemInformation->DSPVersion));
    memset(SystemInformation->mainboard.MRenewalDate, 0, sizeof(SystemInformation->mainboard.MRenewalDate));

    if (sceGetDspVersion(SystemInformation->DSPVersion, &stat) == 0 || (stat & 0x80) != 0)
    {
        printf("Failed to read DSP version. Stat: %x\n", stat);
        SystemInformation->mainboard.status |= PS2IDB_STAT_ERR_MVER;
    }
    if (sceCdAltMV(SystemInformation->mainboard.MECHACONVersion, &stat) == 0 || (stat & 0x80) != 0)
    {
        // ignore stat. DTL-H3010x set errored stat & 0x80, which is not true.
        if ((stat & 0x80) != 0)
        {
            SystemInformation->mainboard.MECHACONVersion[0] = SystemInformation->mainboard.MECHACONVersion[0] - 0x80;
        }
    }
    if (sceCdReadConsoleID(SystemInformation->ConsoleID, &result) == 0 || (result & 0x80))
    {
        printf("Failed to read console ID. Stat: %x\n", result);
        SystemInformation->mainboard.status |= PS2IDB_STAT_ERR_CONSOLEID;
    }
    if (sceCdRI(SystemInformation->iLinkID, &result) == 0 || (result & 0x80))
    {
        printf("Failed to read i.Link ID. Stat: %x\n", result);
        SystemInformation->mainboard.status |= PS2IDB_STAT_ERR_ILINKID;
    }
    if (SystemInformation->mainboard.MECHACONVersion[1] >= 5)
    { //v5.x MECHACON (SCPH-50000 and later) supports Mechacon Renewal Date.
        if (sceCdAltReadRenewalDate(SystemInformation->mainboard.MRenewalDate, &result) == 0 || (result & 0x80))
        {
            printf("Failed to read M Renewal Date. Stat: %x\n", result);
            SystemInformation->mainboard.status |= PS2IDB_STAT_ERR_MRENEWDATE;
        }
        /*mechacon 5.8 and 5.9 are the same chip patched with dex flag, so 5.8 and 5.9 -> 5.8 */
        SystemInformation->mainboard.MECHACONVersion[2] = SystemInformation->mainboard.MECHACONVersion[2] & 0xFE;
    }
    SysmanGetMACAddress(SystemInformation->SMAP_MAC_address);

    SystemInformation->mainboard.ADD010 = 0xFFFF;
    if (GetADD010(SystemInformation->mainboard.MECHACONVersion[1] >= 5 ? 0x001 : 0x010, &SystemInformation->mainboard.ADD010) != 0)
    {
        printf("Failed to read ADD0x010.\n");
        SystemInformation->mainboard.status |= PS2IDB_STAT_ERR_ADD010;
    }

    //Get the mainboard and chassis names, MODEL ID, console MODEL ID and EMCS ID.
    SystemInformation->mainboard.ModelID[0]    = SystemInformation->iLinkID[1];
    SystemInformation->mainboard.ModelID[1]    = SystemInformation->iLinkID[2];
    SystemInformation->mainboard.ModelID[2]    = SystemInformation->iLinkID[3];
    SystemInformation->mainboard.ConModelID[0] = SystemInformation->ConsoleID[0];
    SystemInformation->mainboard.ConModelID[1] = SystemInformation->ConsoleID[1];
    SystemInformation->mainboard.EMCSID        = SystemInformation->ConsoleID[7];
    strcpy(SystemInformation->mainboard.MainboardName, GetMainboardModelDesc(&SystemInformation->mainboard));
    strcpy(SystemInformation->chassis, GetChassisDesc(&SystemInformation->mainboard));

    CheckROM(&SystemInformation->mainboard);

    return 0;
}

int DumpRom(const char *filename, const struct SystemInformation *SystemInformation, struct DumpingStatus *DumpingStatus, unsigned int DumpingRegion)
{
    FILE *file;
    int result = 0;
    unsigned int BytesToRead, BytesRemaining, ROMSize, prevSize;
    const unsigned char *MemDumpStart;
    void *buffer1, *buffer2, *pBuffer;

    switch (DumpingRegion)
    {
        case DUMP_REGION_BOOT_ROM:
            ROMSize      = SystemInformation->mainboard.BOOT_ROM.size;
            MemDumpStart = (const unsigned char *)SystemInformation->mainboard.BOOT_ROM.StartAddress;
            break;
        case DUMP_REGION_DVD_ROM:
            ROMSize      = SystemInformation->mainboard.DVD_ROM.size;
            MemDumpStart = (const unsigned char *)SystemInformation->mainboard.DVD_ROM.StartAddress;
            break;
        default:
            return -EINVAL;
    }

    buffer1        = memalign(64, MEM_IO_BLOCK_SIZE);
    buffer2        = memalign(64, MEM_IO_BLOCK_SIZE);

    BytesRemaining = ROMSize;
    if ((file = fopen(filename, "wb")) != NULL)
    {
        for (pBuffer = buffer1, prevSize = BytesRemaining; BytesRemaining > 0; MemDumpStart += BytesToRead, BytesRemaining -= BytesToRead)
        {
            BytesToRead = BytesRemaining > MEM_IO_BLOCK_SIZE ? MEM_IO_BLOCK_SIZE : BytesRemaining;

            SysmanSync(0);
            while (SysmanReadMemory(MemDumpStart, pBuffer, BytesToRead, 1) != 0)
                nopdelay();

            RedrawDumpingScreen(SystemInformation, DumpingStatus);
            pBuffer = pBuffer == buffer1 ? buffer2 : buffer1;
            if (BytesRemaining < ROMSize)
            {
                if (fwrite(UNCACHED_SEG(pBuffer), 1, prevSize, file) != prevSize)
                {
                    result = -EIO;
                    break;
                }

                DumpingStatus[DumpingRegion].progress = 1.00f - (float)BytesRemaining / ROMSize;
            }
            prevSize = BytesToRead;
        }

        if (result == 0)
        {
            pBuffer = pBuffer == buffer1 ? buffer2 : buffer1;
            SysmanSync(0);

            if (fwrite(UNCACHED_SEG(pBuffer), 1, prevSize, file) == prevSize)
                DumpingStatus[DumpingRegion].progress = 1.00f - (float)BytesRemaining / ROMSize;
            else
                result = -EIO;
        }

        fclose(file);
    }
    else
        result = -ENOENT;

    DumpingStatus[DumpingRegion].status = (result == 0) ? 1 : result;

    free(buffer1);
    free(buffer2);

    return result;
}

int GetADD010(u16 address, u16 *word)
{
    unsigned char stat;

    if (sceCdReadNVM(address, word, &stat) != 1 || stat != 0)
        return -1;

    return 0;
}

int DumpMECHACON_EEPROM(const char *filename)
{
    FILE *file;
    int result;
    unsigned char stat;
    unsigned short int i;
    static unsigned short int IOBuffer[512];

    result = 0;
    if ((file = fopen(filename, "wb")) != NULL)
    {
        for (i = 0; i < 512; i++)
        {
            if (sceCdReadNVM(i, &IOBuffer[i], &stat) != 1 || stat != 0)
            {
                result = -EIO;
                break;
            }
        }

        if (fwrite(IOBuffer, 1, sizeof(IOBuffer), file) != sizeof(IOBuffer))
        {
            result = EIO;
        }
        fclose(file);
    }
    else
        result = -ENOENT;

    return result;
}

int DumpMECHACON_VERSION(const char *filename)
{
    FILE *file;
    int result = 0;

    if ((file = fopen(filename, "wb")) != NULL)
    {

        if (fwrite(SystemInformation->mainboard.MECHACONVersion, 1, 4, file) != 4)
        {
            result = EIO;
        }
        fclose(file);
    }
    else
        result = -ENOENT;

    return result;
}

int WriteNewMainboardDBRecord(const char *path, const struct PS2IDBMainboardEntry *SystemInformation)
{
    FILE *file;
    int result;
    struct PS2IDB_NewMainboardEntryHeader header;

    if ((file = fopen(path, "wb")) != NULL)
    {
        header.magic[0] = '2';
        header.magic[1] = 'N';
        header.version  = PS2IDB_NEWENT_FORMAT_VERSION;
        if (fwrite(&header, sizeof(struct PS2IDB_NewMainboardEntryHeader), 1, file) == 1)
        {
            result = fwrite(SystemInformation, sizeof(struct PS2IDBMainboardEntry), 1, file) == 1 ? 0 : EIO;
        }
        else
            result = EIO;

        fclose(file);
    }
    else
        result = EIO;

    return result;
}

const char *GetiLinkSpeedDesc(unsigned char speed)
{
    static const char *speeds[] = {
        "S100",
        "S200",
        "S400",
        "Unknown"};

    if (speed > 3)
        speed = 3;

    return speeds[speed];
}

const char *GetiLinkComplianceLvlDesc(unsigned char level)
{
    static const char *levels[] = {
        "IEEE1394-1995",
        "IEEE1394A-2000",
        "Unknown"};

    if (level > 2)
        level = 2;

    return levels[level];
}

const char *GetiLinkVendorDesc(unsigned int vendor)
{
    const char *description;

    switch (vendor)
    {
        case 0x00A0B8:
            description = "LSI Logic";
            break;
        default:
            description = "Unknown";
    }

    return description;
}

const char *GetSSBUSIFDesc(unsigned char revision)
{
    const char *description;

    if ((description = PS2IDBMS_LookupComponentModel(PS2IDB_COMPONENT_SSBUSIF, revision)) == NULL)
    {
        description = "Missing";
    }

    return description;
}

const char *GetSPEEDDesc(unsigned short int revision)
{
    const char *description;

    if ((description = PS2IDBMS_LookupComponentModel(PS2IDB_COMPONENT_SPEED, revision)) == NULL)
    {
        description = "Missing";
    }

    return description;
}

const char *GetSPEEDCapsDesc(unsigned short int caps)
{
    static char capsbuffer[64];
    unsigned int i;
    unsigned char capability, NumCapabilities;
    static const char *capabilities[] = {
        "SMAP",
        "ATA",
        "Unknown",
        "UART",
        "DVR",
        "Flash",
        "Unknown"};

    if (caps != 0)
    {
        capsbuffer[0] = '\0';
        for (i = 0, NumCapabilities = 0; i < 8; i++)
        {
            if (caps >> i & 1)
            {
                if (NumCapabilities > 0)
                    strcat(capsbuffer, ", ");

                capability = (i < 6) ? i : 6;
                strcat(capsbuffer, capabilities[capability]);
                NumCapabilities++;
            }
        }
    }
    else
        strcpy(capsbuffer, "None");

    return capsbuffer;
}

const char *GetPHYVendDesc(unsigned int oui)
{
    const char *description;

    if ((description = PS2IDBMS_LookupComponentModel(PS2IDB_COMPONENT_ETH_PHY_VEND, oui)) == NULL)
    {
        description = "Unknown";
    }

    return description;
}

const char *GetPHYModelDesc(unsigned int oui, unsigned char model)
{
    unsigned int revision;
    const char *description;

    if ((description = PS2IDBMS_LookupComponentModel(PS2IDB_COMPONENT_ETH_PHY_MODEL, oui << 8 | model)) == NULL)
    {
        description = "Unknown";
    }

    return description;
}

const char *GetGSChipDesc(unsigned short int revision)
{
    const char *description;

    if ((description = PS2IDBMS_LookupComponentModel(PS2IDB_COMPONENT_GS, revision)) == NULL)
    {
        description = "Missing";
    }

    return description;
}

const char *GetEEChipDesc(unsigned short int revision)
{
    const char *description;

    if ((description = PS2IDBMS_LookupComponentModel(PS2IDB_COMPONENT_EE, revision)) == NULL)
    {
        description = "Missing";
    }

    return description;
}

const char *GetIOPChipDesc(unsigned short int revision)
{
    const char *description;

    if ((description = PS2IDBMS_LookupComponentModel(PS2IDB_COMPONENT_IOP, revision)) == NULL)
    {
        description = "Missing";
    }

    return description;
}

const char *GetSPU2ChipDesc(unsigned short int revision)
{
    const char *description;

    if ((description = PS2IDBMS_LookupComponentModel(PS2IDB_COMPONENT_SPU2, revision)) == NULL)
    {
        description = "Missing";
    }

    return description;
}

/* const char *GetMECHACONChipDesc(unsigned short int revision)
{
    const char *description;

    if ((description = PS2IDBMS_LookupComponentModel(PS2IDB_COMPONENT_MECHACON, revision)) == NULL)
    {
        description = "Missing";
    }

    return description;
} */

const char *GetSystemTypeDesc(unsigned char type)
{
    const char *description;

    if ((description = PS2IDBMS_LookupComponentModel(PS2IDB_COMPONENT_SYSTEM_TYPE, type)) == NULL)
    {
        description = "Unknown";
    }

    return description;
}

const char *GetRegionDesc(unsigned char region)
{
    const char *description;

    if ((description = PS2IDBMS_LookupComponentModel(PS2IDB_COMPONENT_MG_REGION, region)) == NULL)
    {
        description = "Unknown";
    }

    return description;
}

const char *GetMainboardModelDesc(const struct PS2IDBMainboardEntry *SystemInformation)
{
    const char *description;
    const struct PS2IDBMainboardEntry *ModelData;

    if (!strncmp(SystemInformation->romver, "0170", 4))
        description = "GH-023";
    else if (!strncmp(SystemInformation->romver, "0190", 4) && (SystemInformation->mainboard.MECHACONVersion[2] < 10)) // mechacon < 5.12 - I-chassis
        description = "GH-026";
    else if (!strncmp(SystemInformation->romver, "0190", 4) && SystemInformation->mainboard.MECHACONVersion[2] == 0xc) // 5.12 - J-Chassis, other with 190 bootrom - I-Chassis
        description = "GH-029";
    else if ((ModelData = PS2IDBMS_LookupMainboardModel(SystemInformation)) != NULL)
        description = ModelData->MainboardName;
    else if (!strncmp(SystemInformation->romver, "0200", 4) && (SystemInformation->mainboard.ee.revision == 3))
        description = "GH-032-xx (Missing)"; // EE and GS separate
    else if (!strncmp(SystemInformation->romver, "0200", 4) && (SystemInformation->mainboard.ee.revision == 4))
        description = "GH-035-xx (Missing)"; // EE and GS combined
    else
        description = "Missing";


    return description;
}

const char *GetMRPDesc(unsigned short int id)
{
    const char *description;

    if ((description = PS2IDBMS_LookupComponentModel(PS2IDB_COMPONENT_MRP_BOARD, id & 0xF8)) == NULL)
    {
        description = "Unknown";
    }

    return description;
}

const char *GetChassisDesc(const struct PS2IDBMainboardEntry *SystemInformation)
{
    const char *description;

    if (!strncmp(SystemInformation->MainboardName, "GH-001", 6) || !strncmp(SystemInformation->MainboardName, "GH-003", 6))
        description = "A-chassis"; // SCPH-10000 and SCPH-15000
    else if (!strncmp(SystemInformation->MainboardName, "GH-003", 6) && strncmp("0101", SystemInformation->romver, 4))
        description = "A-chassis+"; // SCPH-18000 with GH-003
    else if (!strncmp(SystemInformation->MainboardName, "GH-004", 6) || !strncmp(SystemInformation->MainboardName, "GH-005", 6))
        description = "B-chassis"; // SCPH-30000
    else if (!strncmp(SystemInformation->MainboardName, "GH-006", 6) || !strncmp(SystemInformation->MainboardName, "GH-007", 6))
        description = "C-chassis"; // SCPH-30000
    else if (!strncmp(SystemInformation->MainboardName, "GH-010", 6) ||
             !strncmp(SystemInformation->MainboardName, "GH-011", 6) ||
             !strncmp(SystemInformation->MainboardName, "GH-012", 6) ||
             !strncmp(SystemInformation->MainboardName, "GH-013", 6) ||
             !strncmp(SystemInformation->MainboardName, "GH-014", 6))
        description = "D-chassis"; // SCPH-30000, SCPH-30000R and SCPH-35000
    else if (!strncmp(SystemInformation->MainboardName, "GH-016", 6))
        description = "DF-chassis"; // SCPH-30000, GH-016
    else if ((SystemInformation->mainboard.ADD010 & 0xfffe) == 0x0800)
        description = "AB-chassis"; // SCPH-18000, GH-008, ADD0x10 0x0801
    else if ((SystemInformation->mainboard.ADD010 & 0xffcf) == 0xA809)
        description = "F-chassis"; // SCPH-30000 and SCPH-30000R, ADD0x10 0xa809, 0xa829, GH-015
    else if ((SystemInformation->mainboard.ADD010 & 0xffcf) == 0xB009)
        description = "G-chassis"; // SCPH-37000 and SCPH-39000, ADD0x10 0xb009, 0xb029, GH-017, GH-018, GH-019, GH-022
    else if ((SystemInformation->mainboard.MECHACONVersion[1] == 5) && SystemInformation->DVDPlayerVer[0] == '3' && SystemInformation->DVDPlayerVer[3] == '0')
        description = "H-chassis"; // SCPH-50000, DVD ver 3.00, GH-023
    else if ((SystemInformation->mainboard.MECHACONVersion[1] == 5) && SystemInformation->DVDPlayerVer[0] == '3' && SystemInformation->DVDPlayerVer[3] == '2')
        description = "I-chassis"; // SCPH-50000, DVD ver 3.02, GH-026
    else if ((SystemInformation->mainboard.MECHACONVersion[1] == 5) && SystemInformation->DVDPlayerVer[0] == '3' && (SystemInformation->DVDPlayerVer[3] == '3' || SystemInformation->DVDPlayerVer[3] == '4'))
        description = "J-chassis"; // SCPH-50000, DVD ver 3.03-3.04, GH-026
    else if ((SystemInformation->mainboard.MECHACONVersion[1] == 6) && (SystemInformation->mainboard.MECHACONVersion[2] < 6))
        description = "K-chassis"; // SCPH-70000, GH-032-xx, GH-035-xx, Mechacon 6.0-6.5
    else if ((SystemInformation->mainboard.MECHACONVersion[1] == 6) && ((SystemInformation->mainboard.MECHACONVersion[2] & 0xfe) == 6))
        description = "L-chassis"; // SCPH-75000, GH-037-xx, GH-040-xx, GH-041-xx, Mechacon 6.6, 6.7
    else if ((SystemInformation->mainboard.MECHACONVersion[1] == 6) && ((SystemInformation->mainboard.MECHACONVersion[2] & 0xfe) == 10))
        description = "M-chassis"; // SCPH-77000, GH-051-xx, GH-052-xx, Mechacon 6.10, 6.11
    else if ((SystemInformation->mainboard.MECHACONVersion[1] == 6) && (SystemInformation->mainboard.ModelID >= 0x20d466) && (SystemInformation->mainboard.ModelID <= 0x20d475))
        description = "N-chassis"; // SCPH-79000, GH-061-xx, GH-062-xx
    else if ((SystemInformation->mainboard.MECHACONVersion[1] == 6) && strncmp("0230", SystemInformation->romver, 4) && (SystemInformation->mainboard.ModelID > 0x20d475))
        description = "P-chassis"; // SCPH-90000, TVcombo, GH-070, GH-071, bootrom 0220
    else if ((SystemInformation->mainboard.MECHACONVersion[1] == 6) && !strncmp("0230", SystemInformation->romver, 4) && (SystemInformation->mainboard.ModelID > 0x20d475))
        description = "R-chassis"; // SCPH-90000, GH-072, bootrom 0230
    else if ((SystemInformation->mainboard.MECHACONVersion[1] == 5) && ((SystemInformation->mainboard.MECHACONVersion[2] & 0xfe) == 10) && ((SystemInformation->mainboard.MECHACONVersion[2] & 0xfe) == 14))
        description = "X-chassis"; // PSX, XPD-001, XPD-005
    else
        description = "Unknown";

    return description;
}

const char *GetModelIDDesc(unsigned int id)
{
    const char *description;

    if ((description = PS2IDBMS_LookupComponentModel(PS2IDB_COMPONENT_MODEL_ID, id)) == NULL)
    {
        description = "Sticker";
    }

    return description;
}

const char *GetEMCSIDDesc(unsigned char id)
{
    const char *description;

    if ((description = PS2IDBMS_LookupComponentModel(PS2IDB_COMPONENT_EMCS_ID, id)) == NULL)
    {
        description = "Sticker";
    }

    return description;
}

const char *GetADD010Desc(unsigned short int id)
{
    const char *description;

    if ((description = PS2IDBMS_LookupComponentModel(PS2IDB_COMPONENT_ADD010, id)) == NULL)
    {
        description = "Unknown";
    }

    return description;
}

const char *GetDSPDesc(unsigned char revision)
{
    static const char *revisions[] = {
        "CXD1869Q",
        "CXD1869AQ",
        "CXD1869BQ/CXD1886Q-1/CXD1886",
        "CXD3098Q/CXD1886Q-1/CXD3098AQ",
        "Missing"};

    if (revision > 4)
        revision = 4;

    return revisions[revision];
}

const char *GetMechaDesc(unsigned int vendor)
{
    const char *description;

    if (vendor >= 0x050000)
        vendor = vendor & 0xfffeff; // Retail and debug chips are identical
    if (vendor != 0x050607)
        vendor = vendor & 0xffff00; // Mexico unit is unique

    switch (vendor)
    {
        case 0x010200:
            description = "CXP101064-605R";
            break;
        case 0x010300:
            description = "CXP101064-602R"; // DTL-T10000 and retail models, Japan region locked
            break;
        case 0x010900:
            description = "CXP102064-751R"; // only DTL-T10000
            break;
        case 0x020501:
        case 0x020502:
        case 0x020503:
            description = "CXP102064-702R"; // DTL-H3000x
            break;
        case 0x020701:
        case 0x020702:
        case 0x020703:
            description = "CXP102064-703R"; // DTL-H3000x, DTL-H3010x
            break;
        case 0x020900:
        case 0x020901:
        case 0x020902:
        case 0x020903:
        case 0x020904:
            description = "CXP102064-704R"; // DTL-H3000x, DTL-H3010x
            break;
        case 0x020D00:
        case 0x020D01:
        case 0x020D02:
        case 0x020D04:
        case 0x020D05:
            description = "CXP102064-705R/-752R"; // DTL-H3000x, DTL-H3010x, DTL-T10000
            break;
        // Japanese region only v1-v2
        case 0x010600:
            description = "CXP102064-001R (Not confirmed)";
            break;
        case 0x010700:
            description = "CXP102064-003R";
            break;
        case 0x010800:
            description = "CXP102064-002R";
            break;
        case 0x020000:
            description = "CXP102064-004R (Not confirmed)";
            break;
        case 0x020200:
            description = "CXP102064-005R";
            break;
        case 0x020800:
            description = "CXP102064-006R";
            break;
        case 0x020C00:
            description = "CXP102064-007R";
            break;
        // US region only
        case 0x020401:
            description = "CXP102064-101R";
            break;
        case 0x020601:
            description = "CXP102064-102R";
            break;
        case 0x020C01:
            description = "CXP102064-103R";
            break;
        // EU region only
        case 0x020602:
            description = "CXP102064-202R";
            break;
        case 0x020C02:
            description = "CXP102064-203R";
            break;
        // Australia region only
        case 0x020603:
            description = "CXP102064-302R";
            break;
        case 0x020C03:
            description = "CXP102064-303R";
            break;
        // US region only
        case 0x030001:
            description = "CXP103049-101GG";
            break;
        case 0x030201:
            description = "CXP103049-102GG";
            break;
        case 0x030601:
            description = "CXP103049-103GG";
            break;
        // EU region only
        case 0x030002:
            description = "CXP103049-201GG";
            break;
        case 0x030202:
            description = "CXP103049-202GG";
            break;
        case 0x030602:
            description = "CXP103049-203GG";
            break;
        // Australia region only
        case 0x030003:
            description = "CXP103049-301GG";
            break;
        case 0x030203:
            description = "CXP103049-302GG";
            break;
        case 0x030603:
            description = "CXP103049-303GG";
            break;
        // Japan region only
        case 0x030200:
            description = "CXP103049-001GG";
            break;
        case 0x030600:
            description = "CXP103049-002GG";
            break;
        case 0x030800:
            description = "CXP103049-003GG";
            break;
        // Asia region only
        case 0x030404:
            description = "CXP103049-401GG";
            break;
        case 0x030604:
            description = "CXP103049-402GG";
            break;
        case 0x030804:
            description = "CXP103049-403GG";
            break;
        // Russia region only
        case 0x030605:
            description = "CXP103049-501GG";
            break;
        // Dragon
        case 0x050000:
            description = "CXR706080-101GG";
            break;
        case 0x050200:
            description = "CXR706080-102GG";
            break;
        case 0x050400:
            description = "CXR706080-103GG";
            break;
        case 0x050600:
            description = "CXR706080-104GG";
            break;
        case 0x050C00:
            description = "CXR706080-105GG/CXR706F080-1GG";
            break;
        case 0x050607:
            description = "CXR706080-106GG";
            break;
        /* case 0x050800:
            description = "CXR706080-701GG (Not confirmed)";
            break; */
        case 0x050A00:
            description = "CXR706080-702GG";
            break;
        case 0x050E00:
            description = "CXR706080-703GG";
            break;
        case 0x060000:
            description = "CXR716080-101GG";
            break;
        case 0x060200:
            description = "CXR716080-102GG";
            break;
        case 0x060400:
            description = "CXR716080-103GG";
            break;
        case 0x060600:
            description = "CXR716080-104GG";
            break;
        /* case 0x060800:
            description = "CXR716080-105GG (Not confirmed)";
            break; */
        case 0x060A00:
            description = "CXR716080-106GG";
            break;
        case 0x060C00:
            description = "CXR726080-301GB";
            break;
        default:
            description = "Unknown";
    }

    return description;
}

unsigned int CalculateCPUCacheSize(unsigned char value)
{ //2^(12+value)
    return (1U << (12 + value));
}

int WriteSystemInformation(FILE *stream, const struct SystemInformation *SystemInformation)
{
    unsigned int i, modelID;
    // unsigned short int conModelID;
    u32 Serial;
    int MayBeModded;
    const char *dvdplVer;
    const char *OSDVer;

    MayBeModded = CheckROM(&SystemInformation->mainboard);

    //Header
    fputs("Log file generated by Playstation 2 Ident v" PS2IDENT_VERSION ", built on "__DATE__
          " "__TIME__
          "\r\n\r\n",
          stream);
    fprintf(stream, "ROMVER:            %s\r\n", SystemInformation->mainboard.romver);

    //ROM region sizes
    fprintf(stream, "ROM region sizes:\r\n");
    for (i = 0; i <= 2; i++)
    {
        fprintf(stream, "    ROM%u:          ", i);
        if (SystemInformation->ROMs[i].IsExists)
            fprintf(stream, "%p (%u bytes)\r\n", SystemInformation->ROMs[i].StartAddress, SystemInformation->ROMs[i].size);
        else
            fputs("<Not detected>\r\n", stream);
    }
    fprintf(stream, "    EROM:          ");
    if (SystemInformation->erom.IsExists)
        fprintf(stream, "%p (%u bytes)\r\n", SystemInformation->erom.StartAddress, SystemInformation->erom.size);
    else
        fprintf(stream, "<Not detected>\r\n");

    //Physical ROM chip sizes
    fputs("ROM chip sizes:\r\n"
          "    Boot ROM:      ",
          stream);
    if (SystemInformation->mainboard.BOOT_ROM.IsExists)
    {
        fprintf(stream, "%p (%u Mbit)    CRC16: 0x%04x\r\n",
                SystemInformation->mainboard.BOOT_ROM.StartAddress, SystemInformation->mainboard.BOOT_ROM.size / 1024 / 128,
                SystemInformation->mainboard.BOOT_ROM.crc16);
    }
    else
        fputs("<Not detected>\r\n", stream);

    fputs("    DVD ROM:       ", stream);
    if (SystemInformation->mainboard.DVD_ROM.IsExists)
    {
        fprintf(stream, "%p (%u Mbit)    CRC16: 0x%04x\r\n",
                SystemInformation->mainboard.DVD_ROM.StartAddress, SystemInformation->mainboard.DVD_ROM.size / 1024 / 128,
                SystemInformation->mainboard.DVD_ROM.crc16);
    }
    else
        fputs("<Not detected>\r\n", stream);

    //Version numbers
    dvdplVer = SystemInformation->DVDPlayerVer[0] == '\0' ? "-" : SystemInformation->DVDPlayerVer;
    OSDVer = SystemInformation->OSDVer[0] == '\0' ? "-" : SystemInformation->OSDVer;
    fprintf(stream, "    DVD Player:    %s\r\n"
                    "    OSDVer:        %s\r\n"
                    "    PS1DRV:        %s\r\n",
            dvdplVer, OSDVer, SystemInformation->PS1DRVVer);

    //Chip revisions
    fprintf(stream, "EE/GS:\r\n"
                    "    Implementation:      0x%02x\r\n"
                    "    Revision:            %u.%u (%s)\r\n"
                    "    EE_F520:             0x%08x\r\n"
                    "    EE_F540:             0x%08x\r\n"
                    "    EE_F550:             0x%08x\r\n"
                    "    FPU implementation:  0x%02x\r\n"
                    "    FPU revision:        %u.%u\r\n"
                    "    ICache size:         0x%02x (%u KB)\r\n"
                    "    DCache size:         0x%02x (%u KB)\r\n"
                    "    RAM size:            %u bytes\r\n"
                    "    GS revision:         %u.%02u (%s)\r\n"
                    "    GS ID:               0x%02x\r\n",
            SystemInformation->mainboard.ee.implementation, SystemInformation->mainboard.ee.revision >> 4, SystemInformation->mainboard.ee.revision & 0xF, GetEEChipDesc((unsigned short int)(SystemInformation->mainboard.ee.implementation) << 8 | SystemInformation->mainboard.ee.revision),
            SystemInformation->EE_F520, SystemInformation->EE_F540, SystemInformation->EE_F550,
            SystemInformation->mainboard.ee.FPUImplementation, SystemInformation->mainboard.ee.FPURevision >> 4, SystemInformation->mainboard.ee.FPURevision & 0xF,
            SystemInformation->mainboard.ee.ICacheSize, CalculateCPUCacheSize(SystemInformation->mainboard.ee.ICacheSize) / 1024,
            SystemInformation->mainboard.ee.DCacheSize, CalculateCPUCacheSize(SystemInformation->mainboard.ee.DCacheSize) / 1024,
            SystemInformation->mainboard.ee.RAMSize,
            SystemInformation->mainboard.gs.revision >> 4, SystemInformation->mainboard.gs.revision & 0xF,
            GetGSChipDesc((u16)(SystemInformation->mainboard.gs.id) << 8 | SystemInformation->mainboard.gs.revision),
            SystemInformation->mainboard.gs.id);

    fprintf(stream, "IOP:\r\n"
                    "    Implementation:      0x%02x\r\n"
                    "    Revision:            %u.%u (%s)\r\n"
                    "    RAM size:            %u bytes\r\n"
                    "    SSBUS I/F revision:  %u.%u (%s)\r\n",
            SystemInformation->mainboard.iop.revision >> 8,
            (SystemInformation->mainboard.iop.revision & 0xFF) >> 4, SystemInformation->mainboard.iop.revision & 0xF, GetIOPChipDesc(SystemInformation->mainboard.iop.revision),
            SystemInformation->mainboard.iop.RAMSize,
            SystemInformation->mainboard.ssbus.revision >> 4, SystemInformation->mainboard.ssbus.revision & 0xF,
            GetSSBUSIFDesc(SystemInformation->mainboard.ssbus.revision));

    fputs("    AIF revision:        ", stream);
    if (SystemInformation->mainboard.ssbus.status & PS2DB_SSBUS_HAS_AIF)
        fprintf(stream, "%u\r\n", SystemInformation->mainboard.ssbus.AIFRevision);
    else
        fputs("<Not detected>\r\n", stream);

    if (!(SystemInformation->mainboard.status & PS2IDB_STAT_ERR_MVER))
    {
        fprintf(stream, "MECHACON:\r\n"
                        "    Revision:            %u.%02u (%s)\r\n"
                        "    MagicGate region:    0x%02x (%s)\r\n"
                        "    System type:         0x%02x (%s)\r\n"
                        "    DSP revision:        %u (%s)\r\n",
                SystemInformation->mainboard.MECHACONVersion[1], SystemInformation->mainboard.MECHACONVersion[2], GetMechaDesc((unsigned int)(SystemInformation->mainboard.MECHACONVersion[1]) << 16 | (unsigned int)(SystemInformation->mainboard.MECHACONVersion[2]) << 8 | SystemInformation->mainboard.MECHACONVersion[0]),
                SystemInformation->mainboard.MECHACONVersion[0], GetRegionDesc(SystemInformation->mainboard.MECHACONVersion[0]),
                SystemInformation->mainboard.MECHACONVersion[3], GetSystemTypeDesc(SystemInformation->mainboard.MECHACONVersion[3]),
                SystemInformation->DSPVersion[1],GetDSPDesc(SystemInformation->DSPVersion[1]));
    }
    else
    {
        fputs("MECHACON:\r\n"
              "    Revision:            -.-\r\n"
              "    MagicGate region:    -\r\n"
              "    System type:         -\r\n"
              "    DSP revision:        -\r\n",
              stream);
    }

    fprintf(stream, "    M Renewal Date:      ");
    if (SystemInformation->mainboard.MECHACONVersion[1] < 5 || (SystemInformation->mainboard.status & PS2IDB_STAT_ERR_MRENEWDATE))
        fprintf(stream, "----/--/-- --:--\r\n");
    else
        fprintf(stream, "20%02x/%02x/%02x %02x:%02x\r\n", SystemInformation->mainboard.MRenewalDate[0], SystemInformation->mainboard.MRenewalDate[1], SystemInformation->mainboard.MRenewalDate[2], SystemInformation->mainboard.MRenewalDate[3], SystemInformation->mainboard.MRenewalDate[4]);

    fputs("Mainboard:\r\n"
          "    Model name:          ",
          stream);
    if (!(SystemInformation->mainboard.status & PS2IDB_STAT_ERR_MNAME))
        fprintf(stream, "%s\r\n", SystemInformation->mainboard.ModelName);
    else
        fputs("-\r\n", stream);

    fprintf(stream, "    Mainboard model:     %s\r\n"
                    "    Chassis:             %s\r\n"
                    "    ROMGEN:              %04x-%04x\r\n"
                    "    Machine type:        0x%08x\r\n"
                    "    BoardInf:            0x%02x (%s)\r\n"
                    "    MPU Board ID:        0x%04x\r\n"
                    "    SPU2 revision:       0x%02x (%s)\r\n",
            SystemInformation->mainboard.MainboardName, SystemInformation->chassis,
            SystemInformation->mainboard.ROMGEN_MonthDate, SystemInformation->mainboard.ROMGEN_Year, SystemInformation->mainboard.MachineType,
            SystemInformation->mainboard.BoardInf, GetMRPDesc(SystemInformation->mainboard.BoardInf), SystemInformation->mainboard.MPUBoardID,
            SystemInformation->mainboard.spu2.revision, GetSPU2ChipDesc(SystemInformation->mainboard.spu2.revision));

    fputs("    ADD0x010:            ", stream);
    if (!(SystemInformation->mainboard.status & PS2IDB_STAT_ERR_ADD010))
    {
        fprintf(stream, "0x%04x (%s)\r\n",
                SystemInformation->mainboard.ADD010, GetADD010Desc(SystemInformation->mainboard.ADD010));
    }
    else
    {
        fputs("-\r\n", stream);
    }

    //i.Link Model ID
    fputs("    i.Link Model ID:     ", stream);
    if (!(SystemInformation->mainboard.status & PS2IDB_STAT_ERR_ILINKID))
    {
        modelID = SystemInformation->mainboard.ModelID[0] | SystemInformation->mainboard.ModelID[1] << 8 | SystemInformation->mainboard.ModelID[2] << 16;
        fprintf(stream, "0x%06x (%s)\r\n", modelID, GetModelIDDesc(modelID));
    }
    else
    {
        fputs("-\r\n", stream);
    }

    //SDMI Model ID (only 1 last byte, but we will keep 2 bytes)
    if (!(SystemInformation->mainboard.status & PS2IDB_STAT_ERR_CONSOLEID))
    {
        // conModelID = SystemInformation->mainboard.ConModelID[0] | SystemInformation->mainboard.ConModelID[1] << 8;
        Serial = (SystemInformation->ConsoleID[6]) << 16 | (SystemInformation->ConsoleID[5]) << 8 | (SystemInformation->ConsoleID[4]);
        fprintf(stream, "    Console Model ID:    0x%02x\r\n"
                        "    SDMI Company ID:     %02x-%02x-%02x\r\n"
                        "    EMCS ID:             0x%02x (%s)\r\n"
                        "    Serial range:        %03dxxxx\r\n"
                        ,
                // conModelID,
                SystemInformation->mainboard.ConModelID[0],
                SystemInformation->mainboard.ConModelID[3], SystemInformation->mainboard.ConModelID[2], SystemInformation->mainboard.ConModelID[1],
                SystemInformation->mainboard.EMCSID, GetEMCSIDDesc(SystemInformation->mainboard.EMCSID),
                Serial/10000);
    }
    else
    {
        fputs("    Console Model ID:    -\r\n"
              "    SDMI Company ID:     -\r\n"
              "    EMCS ID:             -\r\n"
              "    Serial range:        -\r\n",
              stream);
    }

    fprintf(stream, "    USB HC revision:     %u.%u\r\n",
            SystemInformation->mainboard.usb.HcRevision >> 4, SystemInformation->mainboard.usb.HcRevision & 0xF);

    if (SystemInformation->mainboard.ssbus.status & PS2DB_SSBUS_HAS_SPEED)
    {
        fprintf(stream, "DEV9:\r\n"
                        "    MAC vendor:          %02x:%02x:%02x\r\n"
                        "    SPEED revision:      0x%04x (%s)\r\n"
                        "    SPEED capabilities:  %04x.%04x (%s)\r\n",
                SystemInformation->SMAP_MAC_address[0],SystemInformation->SMAP_MAC_address[1],SystemInformation->SMAP_MAC_address[2],
                SystemInformation->mainboard.ssbus.SPEED.rev1, GetSPEEDDesc(SystemInformation->mainboard.ssbus.SPEED.rev1), SystemInformation->mainboard.ssbus.SPEED.rev3, SystemInformation->mainboard.ssbus.SPEED.rev8, GetSPEEDCapsDesc(SystemInformation->mainboard.ssbus.SPEED.rev3));
        fprintf(stream, "    PHY OUI:             0x%06x (%s)\r\n"
                        "    PHY model:           0x%02x (%s)\r\n"
                        "    PHY revision:        0x%02x\r\n",
                SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_OUI, GetPHYVendDesc(SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_OUI), SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_VMDL, GetPHYModelDesc(SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_OUI, SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_VMDL), SystemInformation->mainboard.ssbus.SPEED.SMAP_PHY_REV);
    }
    else
    {
        fprintf(stream, "DEV9:\r\n    ***No expansion device connected***\r\n");
    }

    fprintf(stream, "i.Link:\r\n"
                    "    Ports:               %u\r\n"
                    "    Max speed:           %u (%s)\r\n"
                    "    Compliance level:    %u (%s)\r\n"
                    "    Vendor ID:           0x%06x (%s)\r\n"
                    "    Product ID:          0x%06x\r\n",
            SystemInformation->mainboard.iLink.NumPorts,
            SystemInformation->mainboard.iLink.MaxSpeed,
            GetiLinkSpeedDesc(SystemInformation->mainboard.iLink.MaxSpeed),
            SystemInformation->mainboard.iLink.ComplianceLevel,
            GetiLinkComplianceLvlDesc(SystemInformation->mainboard.iLink.ComplianceLevel),
            SystemInformation->mainboard.iLink.VendorID,
            GetiLinkVendorDesc(SystemInformation->mainboard.iLink.VendorID),
            SystemInformation->mainboard.iLink.ProductID);

    if (SystemInformation->mainboard.status || MayBeModded)
    {
        fprintf(stream, "Remarks:\r\n");

        if (SystemInformation->mainboard.status & PS2IDB_STAT_ERR_MVER)
            fprintf(stream, "    Unable to get MECHACON version.\r\n");
        if (SystemInformation->mainboard.status & PS2IDB_STAT_ERR_MNAME)
            fprintf(stream, "    Unable to get model name.\r\n");
        if (SystemInformation->mainboard.status & PS2IDB_STAT_ERR_MRENEWDATE)
            fprintf(stream, "    Unable to get M renewal date.\r\n");
        if (SystemInformation->mainboard.status & PS2IDB_STAT_ERR_ILINKID)
            fprintf(stream, "    Unable to get i.Link ID.\r\n");
        if (SystemInformation->mainboard.status & PS2IDB_STAT_ERR_CONSOLEID)
            fprintf(stream, "    Unable to get console ID.\r\n");
        if (SystemInformation->mainboard.status & PS2IDB_STAT_ERR_ADD010)
            fprintf(stream, "    Unable to get ADD0x010.\r\n");
        if (MayBeModded)
            fprintf(stream, "    ROM may not be clean.\r\n");
    }

    return 0;
}
