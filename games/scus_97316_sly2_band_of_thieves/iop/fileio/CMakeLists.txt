# FILEIO_service (the IOPRP300 reboot image's fileio, version 2.12; RPC servers 0x80000001 /
# 0x80000003) at base 0x20000: text 0x2e80 + rodata 0x3a0 + data 0xd0 + sbss/bss -> ends at
# 0x24280, below lgAud at 0x28000 (iop_modules.h). Booted by the composition at the reboot
# (sceSifRebootIop), not by a sceSifLoadModuleBuffer.
ps2r_sly2_iop_module(fileio FILEIO_service 0x20000)
