/*
 * This program is free software ; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <PalmOS.h>
#include "arm.h"
#include "mem.h"
#include "regs.h"
#include "cpu.h"
#include "elf.h"

#define MACH_TYPE_T3XSCALE	829
#define MACH_XSCALE_TREO680	1230

#define TAG_OFFSET		0x100
#define INITRD_OFFSET		0x0400000

#define T3_INITRD_OFFSET	0x1500000
#define T680_INITRD_OFFSET	0x0600000


static void jump_to_kernel(void *kernel_base, UInt32 tag_base, UInt32 mach)
{
	asm volatile (	"mov r0, #0\n"
			"mov r1, %0\n" /* mach id */
			"mov r2, %1\n" /* tag base */
			"mov pc, %2\n" /* kernel address (jumps) */
			: 
			: "r"(mach), "r"(tag_base), "r"(kernel_base) 
			: "r0", "r1", "r2" );
	
	/* never returns */
}

static void disable_mmu()
{
	/* disable mmu and data cache */
	asm volatile ("mrc p15, 0, r0, c1, c0, 0");
	asm volatile ("bic r0, r0, #0x00002300");     // clear bits 13, 9:8 (--V- --RS)
	asm volatile ("bic r0, r0, #0x00000087");     // clear bits 7, 2:0 (B--- -CAM)
	asm volatile ("orr r0, r0, #0x00001000");     // set bit 12 (I) I-Cache
	asm volatile ("mcr p15, 0, r0, c1, c0, 0");
	CPWAIT
  
	/* invalidate TLB */
	asm volatile ("mov r0, #0");
	asm volatile ("mcr p15, 0, r0, c8, c7, 0");
	CPWAIT
}

static void copy_image(UInt32 *dest, UInt32 *src, UInt32 size)
{
	UInt32 i;

	for(i=0; i<size; i+=4) {
		*(dest++) = *(src++);
	}
}
#ifdef MOVE_FRAMEBUFFER
static void map_lcd(void)
{
#define reg(a) (*(UInt32 *)(a))
	 reg( reg(FDADR0) + DMA_SRC)=0xa1d68000;
#undef reg
}
#endif

UInt32 boot_linux(ArmGlobals *g, void *kernel, UInt32 kernel_size,
		  void *initrd, UInt32 initrd_size, char *cmdline)
{
	ArmGlobals *pg=NULL;
	void *vstack=NULL,	*pstack=NULL;
	void *vphys_jump=NULL,	*pphys_jump=NULL;
	int elf = 0;

	if(!kernel || !cmdline) {
		return 0xc0ffee;
	}

	/* Disable memory protection (page table is read-only
	 * on Treos and Centro) */
	asm volatile ("mcr p15, 0, %0, c3, c3, 0" : : "r"(0xffffffff) );
	
	UInt32 initrd_offset;

	/* We do this before tinkering with hardware, it's safer */
	elf = test_elf((UInt32 *)kernel);

	/* since we're going to turn off the MMU, we need to translate
	 *  all out pointers to physical addresses.
	 */
	kernel = (void *)virt_to_phys(g, (UInt32) kernel);
	cmdline = (char *)virt_to_phys(g, (UInt32) cmdline);

	if(initrd)
		initrd = (void *)virt_to_phys(g, (UInt32) initrd);
	pg = (void *)virt_to_phys(g, (UInt32) g);

	if(!kernel | !cmdline | !pg) { 
		return 0xbadc01a;
	}

	/* that includes the stack pointer ... */
	asm volatile ("mov %0, sp" : "=r"(vstack) );
	pstack = (void *)virt_to_phys(g, (UInt32) vstack);

	if(!pstack) { 
		return 0xbadc01a2;
	}

	/* Work out the physical address of the phys_jump_label below */
	asm volatile ("adr %0, phys_jump_label" : "=r"(vphys_jump));	
	pphys_jump = (void *)virt_to_phys(g, (UInt32) vphys_jump);

	if(!pphys_jump) return 0xbadc01a3; /* running out of creative errors */

	/* From here on, if we're interrupted, PalmOS will hang us! 
	 * Lock the door! What it won't know, won't hurt it.
	 */

	irq_off();	

	/* Map the page containing pphys_jump to identity */
	map(g, (UInt32)pphys_jump, (UInt32)pphys_jump);

	/* make sure the mapping worked */
	if(*(UInt32*)(vphys_jump) != *(UInt32*)(pphys_jump)) {
		irq_on();
		return 0xc01d;
	}

	/* save physical stack pointer and make the jump to physical addresss
	 * space
	 */
	asm volatile ("mov r3, %0\n"
	              "mov pc, %1" : : "r"(pstack), "r"(pphys_jump) : "r3");

	/* something's gone wrong! (I've included the if to make sure the compiler
	 * still generates the rest of the function
	 */
	if (pphys_jump) {
		return 0xc0de;
	}
	
	/*** From this point on we're running at our physical address ***/
	asm volatile ("phys_jump_label: nop\n"
			"mov sp, r3" 		/* setup physical stack pointer */
			);

	/* now we're quite safe to shut off the MMU */
	disable_mmu();

#ifdef MOVE_FRAMEBUFFER
	map_lcd();
#endif
	/* do CPU-specific configuration (like interrupt masking) */
	if (pg->cpu & CPUV_INTEL) {
		setup_xscale_cpu(elf);
	}

	if (pg->mach_num==MACH_TYPE_T3XSCALE){
	    initrd_offset=T3_INITRD_OFFSET;
	} else if (pg->mach_num==MACH_XSCALE_TREO680){
	    initrd_offset=T680_INITRD_OFFSET;
	} else {
	    initrd_offset=INITRD_OFFSET;
	}
	
	/* place kernel parameters in memory */
	setup_atags(pg->ram_base + TAG_OFFSET, pg->ram_base, pg->ram_size, cmdline,
		    pg->ram_base + initrd_offset, initrd_size);

	/* copy initrd into place */
	if (initrd) {
	    copy_image((void*)(pg->ram_base + initrd_offset), initrd, initrd_size);
	}

	if (elf)
		/* handle additional stuff necessary to load ELF kernel */
		relocate_elf(kernel, kernel_size, pg->mach_num);
	else
		/* bring on the penguin! */
		jump_to_kernel(kernel, pg->ram_base + TAG_OFFSET, pg->mach_num);

	return 0xe4;	/* sadly, this return will never be executed */
}

