#include <PalmOS.h>
#include "cocoboot.h"
#include "cocoboot_r.h"
#include "mainform.h"
#include "cpu.h"
#include "mem.h"
#include "regs.h"
#include "imgloader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <DataMgr.h>

UInt32 reg(UInt32 addr);
int use_initrd;
int kernel_ok;

/* read a value from the given physical address */
UInt32 reg(UInt32 phys) {
	UInt32 *addr = (UInt32*)phys_to_virt(phys);
	UInt32 val;
	if(!addr) return 0;
	val = *addr;
	return EndianSwap32(val);
}

/* write a value to the given physical address */
void set_reg(UInt32 phys, UInt32 val) {
	UInt32 *addr = (UInt32*)phys_to_virt(phys);
	if(!addr) return;
	*addr = EndianSwap32(val);
}


void lcd_info()
{
	char msg[255];
	UInt32 framebuffer = 0;

	if(!phys_to_virt(LCCR0)) {
		FrmCustomAlert(ErrorAlert, "Unable to find virtual address of"
			       " LCD registers.", " ", " ");
		return;
	}

	/* fetch location of framebuffer by LCD DMA descriptor */
	if(reg(FDADR0)) {
		framebuffer = reg( reg(FDADR0) + DMA_SRC );
	}

	sprintf(msg, "  LCCR0: 0x%08lx\n"
		"  LCCR1: 0x%08lx\n"
		"  LCCR2: 0x%08lx\n"
		"  LCCR3: 0x%08lx\n"
		"  FDADR0: 0x%08lx\n"
		"  buffer: 0x%08lx"
		,
		reg(LCCR0), 
		reg(LCCR1),
		reg(LCCR2),
		reg(LCCR3),
		reg(FDADR0),
		framebuffer);

	FrmCustomAlert(InfoAlert, "PXA LCD registers:", msg, " ");

}

#define LCCR0_LDM (1<<3)
#define LCCR0_DIS (1<<10)

void lcd_test()
{
	char msg[255];
	UInt32 ret;
	//UInt32 i, lccr0;
	
	//lccr0 = reg(LCCR0);
	
	//set_reg(LCSR, 0xffffffff); /* clear status */
	//set_reg(LCCR0, lccr0 & ~LCCR0_LDM); /* enable LCD disable done */
	//set_reg(LCCR0, (lccr0 & ~LCCR0_LDM) | LCCR0_DIS); /* disable LCD */
	
	/* wait a little */
	//for (i=0; i<100000; i++);
	
	//set_reg(LCCR3, 0x4700004);
	
	/* wait a little more */
	//for (i=0; i<100000; i++);

	/* re-enable LCD controller */
	//set_reg(LCCR0, lccr0);
	
	
	ret = call_arm(arm_stack, ARM_fb_test);

	sprintf(msg, "0x%08lx", ret);
	FrmCustomAlert(InfoAlert, "LCD test result:", msg, " ");
	
}


void cpu_info()
{
	char msg[255];
	UInt32 dev[2], cpu;

	dev[0] = get_dev_id();
	dev[1] = 0; /* string terminator ;-) */

	cpu = get_cpu();

	sprintf(msg, "Machine: %s\n"
		"CPU ID: 0x%08lx\n"
		"CPU: %s %s\n",
		(char*)dev,
		get_cpu_id(),
		get_cpu_vendor(cpu),
		get_cpu_name(cpu));

	FrmCustomAlert(InfoAlert, msg, " ", " ");

}

void mem_info()
{
	char msg[255];

	sprintf(msg, "  RAM base: 9x%lx\n"
		"  Size: %ldmb (0x%lx)\n"
		"  Phys TTB: 0x%lx\n"
		"  Virt TTB: 0x%lx",
		get_ram_base(),
		get_ram_size() >> 20, get_reported_ram_size(),
		get_ttb(), get_virt_ttb());

	FrmCustomAlert(InfoAlert, "Memory:", msg, " ");

}


UInt32 load_parts(int n, char *name, void **image)
{
	/* more ugly code... */
	Err err=0;
	char loc[32];
	UInt32 size = 1000;
	Int32 vol, bytes;

	lprintf("Loading %s... ", name);
	vol = search_image(name, loc, sizeof(loc), &size);
	if(vol < -1) goto out;
	
	if((err=FtrPtrNew (CREATOR_ID, FEATURE_NUM + n, size, image))) {
		lprintf("FtrPtrNew ");
		goto out;
	}
	
	bytes = load_image(name, size, (UInt16)vol, *image);
	lprintf("%ld b ", bytes);
	
	if(bytes == size) {
		lprintf("OK.\n");
		return size;
	}

	FtrPtrFree(CREATOR_ID, FEATURE_NUM + n);

 out:
	lprintf("failed. (%d/%d)\n", vol, err);
	return 0;
}

UInt32 read_mach_id()
{
	FormPtr form = FrmGetActiveForm();
	UInt32 num;
	FieldPtr id_p = FrmGetObjectPtr(form, FrmGetObjectIndex(form, MachIdField));
	MemHandle id_h = FldGetTextHandle(id_p);
	char *id = MemHandleLock(id_h);
	num = atoi(id);
	MemHandleUnlock(id_h);
	return num;
}

char *read_command_line()
{
	FormPtr form = FrmGetActiveForm();
	FieldPtr id_p = FrmGetObjectPtr(form, FrmGetObjectIndex(form, CommandLine));
	MemHandle id_h = FldGetTextHandle(id_p);
	char *cmdline;
	if (!id_h) return "root=bug1";
	cmdline = MemHandleLock(id_h);
	if (!cmdline) return "root=bug2";
	return cmdline;
}

void boot_linux()
{
	void *kernel=NULL, *initrd=NULL;
	UInt32 kernel_size=0, initrd_size=0;
	UInt32 ret;
	char *cmdline;

		
	kernel_size = load_parts(0, "/zImage", &kernel);
	if(kernel_size) {
		if(use_initrd) {
			initrd_size = load_parts(1, "/initrd.gz", &initrd);
		}

		if(!use_initrd || initrd_size) {
			cmdline = read_command_line();

			//PrefSetAppPreferences ('CcBt', 1, 0, cmdline, 256, true);
			//lprintf("Fare thee well 68k world!\n");

			arm_globals.mach_num = read_mach_id();
			
			push_uint32(arm_stack, (UInt32)cmdline);
			push_uint32(arm_stack, initrd_size);
			push_uint32(arm_stack, (UInt32)initrd);
			push_uint32(arm_stack, kernel_size);
			push_uint32(arm_stack, (UInt32)kernel);

			ret = call_arm(arm_stack, ARM_boot_linux);

			/* we're back?! Boot must have failed. */
			lprintf("Returned: %lx\n", ret);

			FtrPtrFree(CREATOR_ID, FEATURE_NUM);
		}
		FtrPtrFree(CREATOR_ID, FEATURE_NUM + 1);
	}
	//lprintf("Boot aborted.\n");
}

Boolean mainform_menu_event(Int16 id)
{
	switch(id) {
	case MenuItemLCD:
		lcd_info();
		return true;
	case MenuItemLCDTest:
		lcd_test();
		return true;
	case MenuItemCPU:
		cpu_info();
		return true;
	case MenuItemMem:
		mem_info();
		return true;
	}
	return false;
}

int check_image(char *name)
{
	char loc[32];
	UInt32 size = 0;
	Int32 vol;

	vol = search_image(name, loc, sizeof(loc), &size);
	if(vol>=-1) {
		lprintf("[%s] %s: %ld bytes\n", loc, name, size);
		return 1;
	} else {
		lprintf("%s not found! (%ld)\n", name, vol);
	}
	return 0;
}

void display_mach(FormPtr form)
{
	char id[8];
	FieldPtr id_p = FrmGetObjectPtr(form, FrmGetObjectIndex(form, MachIdField));
	FieldPtr name_p = FrmGetObjectPtr(form, FrmGetObjectIndex(form, MachNameField));

	sprintf(id, "%ld", get_linux_mach_id());
	SetFieldTextFromStr(id_p, id, true);
	SetFieldTextFromStr(name_p, get_mach_name(), true);
}



Boolean mainform_event(EventPtr event)
{
	Boolean handled = false;
	FormPtr form = NULL;
	FieldPtr cmdline_p;
	MemHandle cmdline_th;
	char *cmdline_tp;
	UInt16 size;

	if (event->eType == frmOpenEvent) {
		form = FrmGetActiveForm();

		/* setup command line buffer */
		size = 256;
		cmdline_p = FrmGetObjectPtr(form, FrmGetObjectIndex(form, CommandLine));
	        cmdline_th = MemHandleNew(size);
	        cmdline_tp = MemHandleLock(cmdline_th);
		StrCopy(cmdline_tp, "init=/linuxrc"); /* default value */
		//PrefGetAppPreferences ('CcBt', 1, cmdline_tp, &size, true);
		MemHandleUnlock(cmdline_th);
		FldSetTextHandle(cmdline_p, cmdline_th);
		
		display_mach(form);
		FrmDrawForm(form);
		handled = true;
		
		kernel_ok = check_image("/zImage");
		use_initrd = check_image("/initrd.gz");

	} else if (event->eType == menuEvent) {
		return mainform_menu_event(event->data.menu.itemID);
	}

	if (event->eType == ctlSelectEvent) {
		if (event->data.ctlSelect.controlID == LinuxButton) {
#ifdef WARNING
			if (FrmAlert (StartupAlert) == 1) {
				FrmCloseAllForms ();
				return 0;
			}
#endif
			boot_linux();
			return 0;
		}
	}


	return handled;
}
