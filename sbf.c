/*
 * Clear SBF flags to indicate succesful boot.
 * Dave Jones <davej@suse.de>. June 21st 2001.
 * Large sections of code butchered from acpidmp.
 *
 * Some changes / fixups by Randy Dunlap <rddunlap@osdlab.org>
 * Clear bits 1 (BOOTING) and 2 (DIAG) to prevent the
 * BIOS doing POST. Bit 7 (PARITY) should be set to odd
 * parity depending on the rest of the register.
 * Clears the Reserved bits.
 *
 * Bit 2 (DIAG) is optionally SET if "-d" is specified on the command line.
 *
 * Build with:  gcc -O or -O2
 *
 * 0.1 - first version
 * 0.2 - Randy Dunlaps various fixes.
 * 0.3 - re-merge the /dev/nvram stuff that got lost.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/types.h>
#include <sys/io.h>

#ifndef RTC_PORT
#define RTC_PORT(x) (0x70 + (x))
#define RTC_ALWAYS_BCD  1   /* RTC operates in binary mode */
#endif

#define VER_STR		"0.3"

#define ACPI_BIOS_ROM_BASE 0x000e0000
#define ACPI_BIOS_ROM_END  0x00100000
#define ACPI_BIOS_ROM_SIZE (ACPI_BIOS_ROM_END - ACPI_BIOS_ROM_BASE)

#define ACPI_RSDP1_SIG 0x20445352 /* 'RSD ' */
#define ACPI_RSDP2_SIG 0x20525450 /* 'PTR ' */
#define ACPI_BOOT_SIG  0x544f4f42 /* 'BOOT' */

#define PAGE_OFFSET(addr) ((unsigned long)(addr) & (getpagesize() - 1))

#define BOOT_RESERVED_BITS		0x78

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

struct acpi_rsdp
{
    u32 signature[2];
    u8 checksum;
    u8 oem[6];
    u8 reserved;
    u32 rsdt;
};

struct acpi_boot
{
	u8 signature[4];
	u32 length;
	u8 revision;
	u8 csum;
	u8 oemid[6];
	u8 oemtable[8];
	u8 revdata[4];
	u8 creator[4];
	u8 creatrev[4];
	u8 cmos;
	u8 reserved[3];
};

struct acpi_table
{
    u32 signature;
    u32 length;
    u8 rev;
    u8 checksum;
    u8 oem[6];
    u8 oem_table[8];
    u32 oem_rev;
    u32 creator;
    u32 creator_rev;
};


/*
 * Map an ACPI table into virtual memory
 */
static struct acpi_table *acpi_map_table (int mem, unsigned long addr)
{
	/* mmap header to determine table size */
	struct acpi_table *table = NULL;
	unsigned long offset = PAGE_OFFSET (addr);
	__u8 *mapped = mmap (NULL,
			     sizeof (struct acpi_table) + offset,
			     PROT_READ,
			     MAP_PRIVATE,
			     mem,
			     (unsigned long) addr - offset);
	table = (struct acpi_table *)
	    ((mapped != MAP_FAILED) ? (mapped + offset) : NULL);
	if (table) {
		/* re-mmap entire table */
		unsigned long size = table->length;
		munmap ((__u8 *) table - offset, sizeof (struct acpi_table) + offset);
		mapped = mmap (NULL, size + offset, PROT_READ, MAP_PRIVATE, mem, (unsigned long) addr - offset);
		table = (struct acpi_table *)
		    ((mapped != MAP_FAILED) ? (mapped + offset) : NULL);
	}
	return table;
}


/*
 * Unmap an ACPI table from virtual memory
 */
static void acpi_unmap_table (struct acpi_table *table)
{
	if (table) {
		unsigned long offset = PAGE_OFFSET (table);
		munmap ((__u8 *) table - offset, table->length + offset);
	}
}


/*
 * Locate the RSDP
 */
static unsigned long acpi_find_rsdp (int mem, unsigned long base, unsigned long size, struct acpi_rsdp *rsdp)
{
	unsigned long addr = 0;
	__u8 *mapped = mmap (NULL, size, PROT_READ, MAP_PRIVATE, mem, base);
	if (mapped != MAP_FAILED) {
		__u8 *i;
		for (i = mapped; i < mapped + size; i += 16) {
			struct acpi_rsdp *r = (struct acpi_rsdp *) i;
			if (r->signature[0] == ACPI_RSDP1_SIG && r->signature[1] == ACPI_RSDP2_SIG) {
				memcpy (rsdp, r, sizeof (*rsdp));
				addr = base + (i - mapped);
				break;
			}
		}
		munmap (mapped, size);
	}
	return addr;
}


/*
 * Output ACPI table
 */
static void acpi_show_table (struct acpi_table *table, unsigned long addr)
{
	char sig[5];

	memcpy (sig, &table->signature, 4);
	sig[4] = '\0';
	printf ("%s @ 0x%08lx\n", sig, addr);
}

static int parity(u8 v)
{
	int x = 0;
	int i;
	
	for(i=0;i<8;i++)
	{
		x^=(v&1);
		v>>=1;
	}
	return x;
}

static int sbf_value_valid(u8 v)
{
	if (v == 0xff || v == 0xfd)
		return 1;	/* never been init */
	if(v&BOOT_RESERVED_BITS)
		return 0;
	if(!parity(v))
		return 0;
	return 1;
}

void do_cmos(int reg, int set_diag)
{
	u8 value;
	int nvram, i;

	printf ("CMOS register: 0x%x\n", reg);

	nvram = open ("/dev/nvram", O_RDWR);
	if (nvram < 0) {
		printf ("Couldn't open /dev/nvram\n");
		exit(1);
	}
	lseek (nvram, reg, SEEK_SET);
	i = read (nvram, &value, 1);
	if (i==0) {
		printf ("Can't read from offset 0x%x. Probably need newer kernel.\n", reg);
		close (nvram);
		exit(1);
	}
	printf ("Read current value := 0x%x\n", value);

	if(!sbf_value_valid(value)) {
		close (nvram);
		return;
	}

	value &= ~(1<<1);	/* Clear BOOTING flag */
//	value &= ~BOOT_RESERVED_BITS;	/* clear the Reserved bits */
	value &= ~(1<<2);	/* clear DIAG flag*/
	value |= 1;		/* always set the PNPOS flag */
	value |= set_diag;	/* maybe set the DIAG flag */

	lseek (nvram, reg, SEEK_SET);
	write (nvram, &value, 1);

	sleep(1);
	lseek (nvram, reg, SEEK_SET);
	i = read (nvram, &value, 1);
	printf ("Read updated value := 0x%x\n", value);

	close (nvram);
}

void usage (void)
{
	fprintf (stderr, "usage:  sbf [-d]  {ver. %s}\n", VER_STR);
	fprintf (stderr, "  where -d is used to set the DIAG request flag\n");
	exit (1);
}

int main (int argc, char *argv[])
{
	int mem;
	struct acpi_rsdp rsdp;
	unsigned long rsdp_addr;
	struct acpi_boot boot;
	struct acpi_boot *bootptr=&boot;
	struct acpi_table *rsdt;
	int rsdt_entry_count;
	__u32 *rsdt_entry;
	int set_diag = 0;

	if (argc > 1) {
		if (strcmp(argv[1], "-d") == 0) {
			set_diag = 1<<2;
			printf ("will set DIAG\n");
		}
		else
			usage();
	}

	mem = open ("/dev/mem", O_RDONLY);
	if (mem < 0) {
		perror ("/dev/mem");
		return 1;
	}

	rsdp_addr = acpi_find_rsdp (mem, ACPI_BIOS_ROM_BASE, ACPI_BIOS_ROM_SIZE, &rsdp);
	if (!rsdp_addr) {
		close (mem);
		return 1;
	}

	rsdt = acpi_map_table (mem, rsdp.rsdt);
	if (!rsdt) {
		perror ("cannot map the RSDT\n");
		close (mem);
		return 1;
	}

	rsdt_entry = (__u32 *) (rsdt + 1);
	rsdt_entry_count = (rsdt->length - sizeof (*rsdt)) >> 2;

	while (rsdt_entry_count) {
		struct acpi_table *dt = acpi_map_table (mem, *rsdt_entry);
		if (dt) {
			if (dt->signature == ACPI_BOOT_SIG) {
				memcpy (bootptr, dt, 40);
				acpi_show_table (dt, *rsdt_entry);

				if (ioperm(0x80, 1, 1)) {
					perror ("ioperm:0x80");
					break;	// TBD: cleanup
				}
				if (ioperm(RTC_PORT(0), 2, 1)) {
					perror ("ioperm:RTC");
					break;	// TBD: cleanup
				}
				do_cmos(bootptr->cmos, set_diag);
				ioperm(RTC_PORT(0), 2, 0);
				ioperm(0x80, 1, 0);
			}
		}
		acpi_unmap_table (dt);
		rsdt_entry++;
		rsdt_entry_count--;
	}
	acpi_unmap_table (rsdt);

	close (mem);
	return 0;
}
