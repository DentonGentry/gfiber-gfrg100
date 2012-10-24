/*
 * Generic Generic NCR5380 driver defines
 *
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix and Linux consulting and custom programming)
 *	drew@colorado.edu
 *      +1 (303) 440-4894
 *
 * NCR53C400 extensions (c) 1994,1995,1996, Kevin Lentin
 *    K.Lentin@cs.monash.edu.au
 *
 * ALPHA RELEASE 1. 
 *
 * For more information, please consult 
 *
 * NCR 5380 Family
 * SCSI Protocol Controller
 * Databook
 *
 * NCR Microelectronics
 * 1635 Aeroplaza Drive
 * Colorado Springs, CO 80916
 * 1+ (719) 578-3400
 * 1+ (800) 334-5454
 */

/*
 * $Log: g_NCR5380.h,v $
 * Revision 1.4  2010/04/08 19:11:11  dmitri
 * B92240: Merge new kernel related changes from 5.3.6.1 to dev (merge 5.3 -> 1)
 * NOTIFY: cancel
 *
 * Revision 1.3.16.1  2010/04/06 18:45:32  dmitri
 * B92240: Merge new kernel related changes from 5.3.6.1 to dev
 * NOTIFY: cancel
 *
 * Revision 1.3.14.1  2010/03/29 09:00:40  dmitri
 * B92240: Merge linux kernel 2.6.21.5 upgrade related changes to 5.3.6.1 - part 4
 *
 * Revision 1.3.4.1  2009/09/24 09:53:53  felix
 * B82809: Upgrade linux to linux-mips-2_6_21_5 based on R46934, part9
 *
 * Revision 1.3.44.1  2007/07/16 15:14:56  yairh
 * B47807: upgrade to linux mips 2.6.21.5
 *
 * Revision 1.3  2006/06/15 21:32:31  itay
 * B33147 Upgrade kernel to linux-2.6.16.14
 * OPTIONS: novalidate
 *
 * Revision 1.2.34.1  2006/06/14 11:12:07  itay
 * B33147 Upgrade kernel to linux-2.6.16.14 and add support for broadcom 6358 (merge from branch-4_2_3_2)
 * OPTIONS: novalidate
 *
 * Revision 1.2.28.1  2006/05/17 15:29:30  itay
 * B33147 merge changes between linux-2_6_12 and linux-2_6_16_14
 * NOTIFY: cancel
 * OPTIONS: novalidate
 *
 * Revision 1.2  2005/08/08 06:53:23  noams
 * B24862 Initial import of linux-2.6.12
 *
 * Revision 1.1.2.1  2005/08/07 13:20:02  noams
 * B24862 Initial import of linux-2.6.12
 *
 * Revision 1.1.2.3  2007/07/02 08:19:16  yairh
 * B47807: upgrade branch-vendor to linux 2.6.18.8
 *
 */

#ifndef GENERIC_NCR5380_H
#define GENERIC_NCR5380_H


#define GENERIC_NCR5380_PUBLIC_RELEASE 1

#ifdef NCR53C400
#define BIOSPARAM
#define NCR5380_BIOSPARAM generic_NCR5380_biosparam
#else
#define NCR5380_BIOSPARAM NULL
#endif

#ifndef ASM
static int generic_NCR5380_abort(Scsi_Cmnd *);
static int generic_NCR5380_detect(struct scsi_host_template *);
static int generic_NCR5380_release_resources(struct Scsi_Host *);
static int generic_NCR5380_queue_command(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
static int generic_NCR5380_bus_reset(Scsi_Cmnd *);
static const char* generic_NCR5380_info(struct Scsi_Host *);

#ifndef CMD_PER_LUN
#define CMD_PER_LUN 2
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE 16
#endif

#ifndef HOSTS_C

#define __STRVAL(x) #x
#define STRVAL(x) __STRVAL(x)

#ifndef CONFIG_SCSI_G_NCR5380_MEM

#define NCR5380_map_config port
#define NCR5380_map_type int
#define NCR5380_map_name port
#define NCR5380_instance_name io_port
#define NCR53C400_register_offset 0
#define NCR53C400_address_adjust 8

#ifdef NCR53C400
#define NCR5380_region_size 16
#else
#define NCR5380_region_size 8
#endif

#define NCR5380_read(reg) (inb(NCR5380_map_name + (reg)))
#define NCR5380_write(reg, value) (outb((value), (NCR5380_map_name + (reg))))

#define NCR5380_implementation_fields \
    NCR5380_map_type NCR5380_map_name

#define NCR5380_local_declare() \
    register NCR5380_implementation_fields

#define NCR5380_setup(instance) \
    NCR5380_map_name = (NCR5380_map_type)((instance)->NCR5380_instance_name)

#else 
/* therefore CONFIG_SCSI_G_NCR5380_MEM */

#define NCR5380_map_config memory
#define NCR5380_map_type unsigned long
#define NCR5380_map_name base
#define NCR5380_instance_name base
#define NCR53C400_register_offset 0x108
#define NCR53C400_address_adjust 0
#define NCR53C400_mem_base 0x3880
#define NCR53C400_host_buffer 0x3900
#define NCR5380_region_size 0x3a00

#define NCR5380_read(reg) readb(iomem + NCR53C400_mem_base + (reg))
#define NCR5380_write(reg, value) writeb(value, iomem + NCR53C400_mem_base + (reg))

#define NCR5380_implementation_fields \
    NCR5380_map_type NCR5380_map_name; \
    void __iomem *iomem;

#define NCR5380_local_declare() \
    register void __iomem *iomem

#define NCR5380_setup(instance) \
    iomem = (((struct NCR5380_hostdata *)(instance)->hostdata).iomem)

#endif

#define NCR5380_intr generic_NCR5380_intr
#define NCR5380_queue_command generic_NCR5380_queue_command
#define NCR5380_abort generic_NCR5380_abort
#define NCR5380_bus_reset generic_NCR5380_bus_reset
#define NCR5380_pread generic_NCR5380_pread
#define NCR5380_pwrite generic_NCR5380_pwrite
#define NCR5380_proc_info notyet_generic_proc_info

#define BOARD_NCR5380	0
#define BOARD_NCR53C400	1
#define BOARD_NCR53C400A 2
#define BOARD_DTC3181E	3

#endif /* else def HOSTS_C */
#endif /* ndef ASM */
#endif /* GENERIC_NCR5380_H */

