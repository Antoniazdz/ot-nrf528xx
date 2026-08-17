/* Linker script to configure memory regions. */

SEARCH_DIR(.)
GROUP(-lgcc -lc)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 0x17D000 /* Inside global RRAM0 */
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0x20000
  RAM1 (rwx) : ORIGIN = 0x20020000, LENGTH = 0x20000
}


INCLUDE "nrf_common.ld"

/* OpenThread settings: 4 x 4 KiB pages (two 8 KiB swap blocks). */
__ot_flash_data_size = 0x4000;

SECTIONS
{
  .ot_flash_data ORIGIN(FLASH) + LENGTH(FLASH) - __ot_flash_data_size (NOLOAD) :
  {
    __start_ot_flash_data = .;
    . = . + __ot_flash_data_size;
    __stop_ot_flash_data = .;
  } > FLASH
}
