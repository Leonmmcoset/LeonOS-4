# Generated LeonOS private ABI inventory
# Do not edit; regenerate with tools/check_abi_migration.py.

LEONOS_AUDIO_STATUS_NO_DEVICE:
  - devtools/components/tcc/runtime/include/leonos/audio.h
  - devtools/include/leonos/audio.h
  - include/leonos/audio.h
  - kernel/ntclks/driver_manager.c
LEONOS_AUTHZ_INSTALL:
  - devtools/components/tcc/runtime/include/leonos/auth.h
  - devtools/include/leonos/auth.h
  - include/leonos/auth.h
  - kernel/ntclks/syscall.c
  - middlelayer/osmlayer/runtime.c
LEONOS_DEVICE_CLASS_AUDIO:
  - devtools/components/tcc/runtime/include/leonos/device.h
  - devtools/include/leonos/device.h
  - include/leonos/device.h
  - kernel/ntclks/syscall.c
  - userland/apps/devmgr/main.c
LEONOS_DRIVER_KIND_AUDIO:
  - devtools/components/tcc/runtime/include/leonos/driver.h
  - devtools/include/leonos/driver.h
  - drivers/ac97/ac97.c
  - drivers/es1371/es1371.c
  - include/leonos/driver.h
LEONOS_ENOTEMPTY:
  - kernel/ntclks/include/ntclks/syscall.h
  - kernel/ntclks/syscall.c
LEONOS_FS_TYPE_DEVICE:
  - devtools/components/tcc/runtime/include/leonos/fs.h
  - devtools/docs/SYSCALLS.md
  - devtools/include/leonos/fs.h
  - drivers/bootstrap/storage/storage_vfs.c
  - include/leonos/fs.h
  - kernel/ntclks/syscall.c
  - userland/apps/fileman/model.c
  - userland/apps/fileman/operations.c
  - userland/apps/shell/main.c
  - userland/libc/src/posix_directory.c
  - userland/libc/src/posix_stat.c
LEONOS_KERNEL_DEBUG_BENCH_IOCTL:
  - kernel/kerneldebug/kerneldebug.c
  - kernel/ntclks/include/ntclks/kernel_debug.h
  - kernel/ntclks/kernel_debug.c
LEONOS_LAUNCH_ERR_EMPTY:
  - devtools/components/tcc/runtime/include/leonos/launch.h
  - devtools/include/leonos/launch.h
  - userland/apps/desktop/desktop_items.c
  - userland/apps/doomlauncher/main.c
  - userland/apps/fileman/actions.c
  - userland/apps/run/main.c
  - userland/libc/include/leonos/launch.h
  - userland/libc/src/launch.c
LEONOS_MOUNT_KIND_FAT32_RAMDISK:
  - devtools/components/tcc/runtime/include/leonos/boot_handoff.h
  - devtools/include/leonos/boot_handoff.h
  - drivers/bootstrap/storage/storage_mount.c
  - include/leonos/boot_handoff.h
LEONOS_NET_AF_INET:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - include/leonos/net.h
  - kernel/ntclks/net.c
  - userland/libc/src/libc.c
LEONOS_NET_STATUS_NO_DEVICE:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - include/leonos/net.h
  - kernel/ntclks/net.c
  - userland/apps/browser/util.c
  - userland/apps/httpget/main.c
  - userland/apps/netctl/main.c
  - userland/apps/ping/main.c
  - userland/apps/serviced/main.c
LEONOS_RAW_DEVICE_KIND_DISK:
  - devtools/components/tcc/runtime/include/leonos/boot_handoff.h
  - devtools/include/leonos/boot_handoff.h
  - include/leonos/boot_handoff.h
  - kernel/ntclks/syscall.c
LEONOS_VFS_NODE_DEVICE:
  - devtools/components/tcc/runtime/include/leonos/boot_handoff.h
  - devtools/include/leonos/boot_handoff.h
  - include/leonos/boot_handoff.h
leonos_audio_configure:
  - devtools/components/tcc/runtime/include/leonos/audio.h
  - devtools/include/leonos/audio.h
  - include/leonos/audio.h
  - userland/libc/src/libc.c
leonos_audio_format:
  - devtools/components/tcc/runtime/include/leonos/audio.h
  - devtools/components/tcc/runtime/include/leonos/driver.h
  - devtools/include/leonos/audio.h
  - devtools/include/leonos/driver.h
  - drivers/ac97/ac97.c
  - drivers/es1371/es1371.c
  - include/leonos/audio.h
  - include/leonos/driver.h
  - kernel/ntclks/driver_manager.c
  - kernel/ntclks/include/ntclks/driver_manager.h
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_audio_get_state:
  - devtools/components/tcc/runtime/include/leonos/audio.h
  - devtools/include/leonos/audio.h
  - include/leonos/audio.h
  - userland/libc/src/libc.c
leonos_audio_state:
  - devtools/components/tcc/runtime/include/leonos/audio.h
  - devtools/components/tcc/runtime/include/leonos/driver.h
  - devtools/include/leonos/audio.h
  - devtools/include/leonos/driver.h
  - drivers/ac97/ac97.c
  - drivers/es1371/es1371.c
  - include/leonos/audio.h
  - include/leonos/driver.h
  - kernel/ntclks/driver_manager.c
  - kernel/ntclks/include/ntclks/driver_manager.h
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_audio_write:
  - devtools/components/tcc/runtime/include/leonos/audio.h
  - devtools/include/leonos/audio.h
  - include/leonos/audio.h
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_device_catalog_query:
  - devtools/components/tcc/runtime/include/leonos/boot_handoff.h
  - devtools/include/leonos/boot_handoff.h
  - docs/ABI.md
  - include/leonos/boot_handoff.h
  - kernel/ntclks/include/ntclks/osmlayer.h
  - kernel/ntclks/osmlayer_bridge.c
  - kernel/ntclks/syscall.c
leonos_device_characteristics:
  - userland/sqlite/leonos_sqlite_vfs.c
leonos_device_info:
  - devtools/components/tcc/runtime/include/leonos/boot_handoff.h
  - devtools/components/tcc/runtime/include/leonos/device.h
  - devtools/include/leonos/boot_handoff.h
  - devtools/include/leonos/device.h
  - docs/ABI.md
  - include/leonos/boot_handoff.h
  - include/leonos/device.h
  - kernel/ntclks/syscall.c
  - userland/apps/devmgr/main.c
  - userland/libc/src/libc.c
leonos_device_list:
  - devtools/components/tcc/runtime/include/leonos/device.h
  - devtools/include/leonos/device.h
  - include/leonos/device.h
  - kernel/ntclks/syscall.c
  - userland/apps/devmgr/main.c
  - userland/libc/src/libc.c
leonos_disk_gpt_initialize:
  - drivers/bootstrap/storage/storage_disk.c
  - kernel/ntclks/include/ntclks/storage.h
leonos_disk_partition:
  - drivers/bootstrap/storage/storage_disk.c
  - kernel/ntclks/include/ntclks/storage.h
leonos_disk_partition_create:
  - drivers/bootstrap/storage/storage_disk.c
  - kernel/ntclks/include/ntclks/storage.h
leonos_disk_partition_delete:
  - drivers/bootstrap/storage/storage_disk.c
  - kernel/ntclks/include/ntclks/storage.h
leonos_disk_partition_edit:
  - drivers/bootstrap/storage/storage_disk.c
  - kernel/ntclks/include/ntclks/storage.h
leonos_disk_partition_format:
  - drivers/bootstrap/storage/storage_disk.c
  - kernel/ntclks/include/ntclks/storage.h
leonos_disk_partition_mount:
  - drivers/bootstrap/storage/storage_disk.c
  - kernel/ntclks/include/ntclks/storage.h
leonos_disk_partition_unmount:
  - drivers/bootstrap/storage/storage_disk.c
  - kernel/ntclks/include/ntclks/storage.h
leonos_driver_audio_ops:
  - devtools/components/tcc/runtime/include/leonos/driver.h
  - devtools/include/leonos/driver.h
  - drivers/ac97/ac97.c
  - drivers/es1371/es1371.c
  - include/leonos/driver.h
  - kernel/ntclks/driver_manager.c
leonos_driver_control:
  - devtools/components/tcc/runtime/include/leonos/driver.h
  - devtools/include/leonos/driver.h
  - include/leonos/driver.h
  - kernel/ntclks/driver_manager.c
  - kernel/ntclks/include/ntclks/driver_manager.h
  - kernel/ntclks/syscall.c
  - userland/apps/drvmgr/main.c
  - userland/libc/src/libc.c
leonos_driver_e1000_info:
  - devtools/components/tcc/runtime/include/leonos/driver.h
  - devtools/include/leonos/driver.h
  - drivers/e1000/e1000.c
  - include/leonos/driver.h
  - kernel/ntclks/driver_manager.c
leonos_driver_e1000_ops:
  - devtools/components/tcc/runtime/include/leonos/driver.h
  - devtools/include/leonos/driver.h
  - drivers/e1000/e1000.c
  - include/leonos/driver.h
  - kernel/ntclks/driver_manager.c
leonos_driver_info:
  - devtools/components/tcc/runtime/include/leonos/driver.h
  - devtools/include/leonos/driver.h
  - include/leonos/driver.h
  - kernel/ntclks/driver_manager.c
  - kernel/ntclks/syscall.c
  - userland/apps/drvmgr/main.c
  - userland/libc/src/libc.c
leonos_driver_kernel_api:
  - devtools/components/tcc/runtime/include/leonos/driver.h
  - devtools/include/leonos/driver.h
  - docs/DRIVERS.md
  - drivers/ac97/ac97.c
  - drivers/e1000/e1000.c
  - drivers/es1371/es1371.c
  - drivers/mouse/mouse.c
  - drivers/serial/serial.c
  - include/leonos/driver.h
  - kernel/ntclks/driver_manager.c
leonos_driver_list:
  - devtools/components/tcc/runtime/include/leonos/driver.h
  - devtools/include/leonos/driver.h
  - include/leonos/driver.h
  - kernel/ntclks/driver_manager.c
  - kernel/ntclks/include/ntclks/driver_manager.h
  - kernel/ntclks/syscall.c
  - userland/apps/drvmgr/main.c
  - userland/libc/src/libc.c
leonos_driver_module:
  - devtools/components/tcc/runtime/include/leonos/driver.h
  - devtools/include/leonos/driver.h
  - docs/DRIVERS.md
  - drivers/ac97/ac97.c
  - drivers/e1000/e1000.c
  - drivers/es1371/es1371.c
  - drivers/mouse/mouse.c
  - drivers/serial/serial.c
  - include/leonos/driver.h
  - kernel/ntclks/driver_manager.c
leonos_driver_mouse_ops:
  - devtools/components/tcc/runtime/include/leonos/driver.h
  - devtools/include/leonos/driver.h
  - drivers/mouse/mouse.c
  - include/leonos/driver.h
  - kernel/ntclks/driver_manager.c
leonos_driver_mouse_state:
  - devtools/components/tcc/runtime/include/leonos/driver.h
  - devtools/include/leonos/driver.h
  - drivers/mouse/mouse.c
  - include/leonos/driver.h
  - kernel/ntclks/driver_manager.c
leonos_driver_pci_device:
  - devtools/components/tcc/runtime/include/leonos/driver.h
  - devtools/include/leonos/driver.h
  - drivers/ac97/ac97.c
  - drivers/e1000/e1000.c
  - drivers/es1371/es1371.c
  - include/leonos/driver.h
  - kernel/ntclks/driver_manager.c
leonos_driver_serial_ops:
  - devtools/components/tcc/runtime/include/leonos/driver.h
  - devtools/include/leonos/driver.h
  - drivers/serial/serial.c
  - include/leonos/driver.h
  - kernel/ntclks/driver_manager.c
leonos_inputm_active_request:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - kernel/ntclks/inputm.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_config_request:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - kernel/ntclks/inputm.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_context:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - kernel/ntclks/inputm.c
  - userland/apps/browser/main.c
  - userland/apps/login/main.c
  - userland/apps/oobe/main.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_get_state:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/docs/INPUTM.md
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/apps/desktop/inputm.c
  - userland/apps/oschinpt/main.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_key_event:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/docs/INPUTM.md
  - devtools/examples/inputm_provider/main.c
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - kernel/ntclks/inputm.c
  - userland/apps/oschinpt/main.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_list:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/apps/desktop/inputm.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_note_gui_window:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
  - userland/libc/src/libc.c
leonos_inputm_notify_config:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/docs/INPUTM.md
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/apps/settings/main.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/api.c
  - userland/libc/src/inputm.c
leonos_inputm_observe_gui_key:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
  - userland/libc/src/libc.c
leonos_inputm_poll_gui_commit:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
  - userland/libc/src/libc.c
leonos_inputm_poll_result:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/docs/INPUTM.md
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_provider:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/docs/INPUTM.md
  - devtools/examples/inputm_provider/main.c
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - kernel/ntclks/inputm.c
  - userland/apps/desktop/inputm.c
  - userland/apps/oschinpt/main.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_provider_list:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - kernel/ntclks/inputm.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_provider_next:
  - devtools/README.md
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/docs/INPUTM.md
  - devtools/examples/inputm_provider/main.c
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/apps/oschinpt/main.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_provider_result:
  - devtools/README.md
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/docs/INPUTM.md
  - devtools/examples/inputm_provider/main.c
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/apps/oschinpt/main.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_register:
  - devtools/README.md
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/docs/INPUTM.md
  - devtools/examples/inputm_provider/main.c
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/apps/oschinpt/main.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_result:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/docs/INPUTM.md
  - devtools/examples/inputm_provider/main.c
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - kernel/ntclks/inputm.c
  - userland/apps/oschinpt/main.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_set_active:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/apps/desktop/inputm.c
  - userland/apps/settings/main.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_set_context:
  - devtools/README.md
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/apps/browser/main.c
  - userland/apps/login/main.c
  - userland/apps/oobe/main.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_set_current_context:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/docs/INPUTM.md
  - devtools/docs/UI.md
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
  - userland/libc/src/ui_edit.c
leonos_inputm_state:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - kernel/ntclks/inputm.c
  - userland/apps/desktop/desktop.h
  - userland/apps/desktop/inputm.c
  - userland/apps/desktop/state.c
  - userland/apps/oschinpt/main.c
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_submit_key:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/docs/INPUTM.md
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_inputm_take_key:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/docs/INPUTM.md
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
  - userland/libc/src/libc.c
leonos_inputm_take_text:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/docs/INPUTM.md
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
  - userland/libc/src/ui_edit.c
leonos_inputm_unregister:
  - devtools/components/tcc/runtime/include/leonos/inputm.h
  - devtools/docs/INPUTM.md
  - devtools/include/leonos/inputm.h
  - include/leonos/inputm.h
  - userland/libc/include/leonos/inputm.h
  - userland/libc/src/inputm.c
leonos_install_disk:
  - drivers/bootstrap/storage/storage_disk.c
  - kernel/ntclks/include/ntclks/storage.h
  - kernel/ntclks/syscall.c
leonos_mouse_clear_regions:
  - devtools/components/tcc/runtime/include/leonos/mouse.h
  - devtools/docs/GUI.md
  - devtools/include/leonos/mouse.h
  - userland/libc/include/leonos/mouse.h
  - userland/libc/src/libc.c
  - userland/libc/src/ui_surface.c
leonos_mouse_get_position:
  - devtools/components/tcc/runtime/include/leonos/mouse.h
  - devtools/docs/GUI.md
  - devtools/include/leonos/mouse.h
  - userland/libc/include/leonos/mouse.h
  - userland/libc/src/libc.c
leonos_mouse_get_state:
  - devtools/components/tcc/runtime/include/leonos/gui.h
  - devtools/components/tcc/runtime/include/leonos/mouse.h
  - devtools/docs/GUI.md
  - devtools/include/leonos/gui.h
  - devtools/include/leonos/mouse.h
  - userland/libc/include/leonos/gui.h
  - userland/libc/include/leonos/mouse.h
  - userland/libc/src/libc.c
leonos_mouse_hide:
  - devtools/components/tcc/runtime/include/leonos/mouse.h
  - devtools/docs/GUI.md
  - devtools/include/leonos/mouse.h
  - userland/apps/doom/main.c
  - userland/libc/include/leonos/mouse.h
  - userland/libc/src/libc.c
leonos_mouse_is_visible:
  - devtools/components/tcc/runtime/include/leonos/mouse.h
  - devtools/include/leonos/mouse.h
  - userland/apps/desktop/desktop_run.c
  - userland/apps/desktop/screen.c
  - userland/libc/include/leonos/mouse.h
  - userland/libc/src/libc.c
leonos_mouse_set_auto:
  - devtools/components/tcc/runtime/include/leonos/mouse.h
  - devtools/docs/GUI.md
  - devtools/docs/UI.md
  - devtools/include/leonos/mouse.h
  - userland/apps/guitest/main.c
  - userland/libc/include/leonos/mouse.h
  - userland/libc/src/libc.c
leonos_mouse_set_position:
  - devtools/README.md
  - devtools/components/tcc/runtime/include/leonos/mouse.h
  - devtools/docs/GUI.md
  - devtools/include/leonos/mouse.h
  - userland/apps/guitest/main.c
  - userland/libc/include/leonos/mouse.h
  - userland/libc/src/libc.c
leonos_mouse_set_region:
  - devtools/components/tcc/runtime/include/leonos/mouse.h
  - devtools/docs/GUI.md
  - devtools/include/leonos/mouse.h
  - userland/libc/include/leonos/mouse.h
  - userland/libc/src/libc.c
  - userland/libc/src/ui_surface.c
leonos_mouse_set_style:
  - devtools/README.md
  - devtools/components/tcc/runtime/include/leonos/mouse.h
  - devtools/docs/GUI.md
  - devtools/docs/UI.md
  - devtools/include/leonos/mouse.h
  - userland/apps/guitest/main.c
  - userland/libc/include/leonos/mouse.h
  - userland/libc/src/libc.c
leonos_mouse_show:
  - devtools/components/tcc/runtime/include/leonos/mouse.h
  - devtools/docs/GUI.md
  - devtools/include/leonos/mouse.h
  - userland/apps/doom/main.c
  - userland/libc/include/leonos/mouse.h
  - userland/libc/src/libc.c
leonos_mouse_state:
  - devtools/components/tcc/runtime/include/leonos/gui.h
  - devtools/components/tcc/runtime/include/leonos/mouse.h
  - devtools/include/leonos/gui.h
  - devtools/include/leonos/mouse.h
  - userland/libc/include/leonos/gui.h
  - userland/libc/include/leonos/mouse.h
  - userland/libc/src/libc.c
leonos_net_config:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - docs/SYSCALLS.md
  - include/leonos/net.h
  - kernel/ntclks/include/ntclks/net.h
  - kernel/ntclks/net.c
  - kernel/ntclks/syscall.c
  - userland/apps/desktop/screen.c
  - userland/apps/netctl/main.c
  - userland/apps/ping/main.c
  - userland/apps/serviced/main.c
  - userland/libc/src/libc.c
  - userland/libc/src/license.c
leonos_net_connection_info:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - include/leonos/net.h
  - kernel/ntclks/net.c
  - kernel/ntclks/syscall.c
  - userland/apps/netctl/main.c
  - userland/libc/src/libc.c
leonos_net_connection_list:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - include/leonos/net.h
  - kernel/ntclks/include/ntclks/net.h
  - kernel/ntclks/net.c
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_net_connections:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - docs/ABI.md
  - docs/SYSCALLS.md
  - include/leonos/net.h
  - userland/apps/netctl/main.c
  - userland/libc/src/libc.c
leonos_net_dhcp:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - include/leonos/net.h
  - kernel/ntclks/include/ntclks/net.h
  - kernel/ntclks/net.c
  - kernel/ntclks/syscall.c
  - userland/apps/netctl/main.c
  - userland/apps/oobe/main.c
  - userland/apps/serviced/main.c
  - userland/libc/src/libc.c
leonos_net_dhcp_renew:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - docs/SYSCALLS.md
  - include/leonos/net.h
  - userland/apps/netctl/main.c
  - userland/apps/oobe/main.c
  - userland/apps/serviced/main.c
  - userland/libc/src/libc.c
leonos_net_dns:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - include/leonos/net.h
  - kernel/ntclks/include/ntclks/net.h
  - kernel/ntclks/net.c
  - kernel/ntclks/syscall.c
  - userland/apps/netctl/main.c
  - userland/libc/src/libc.c
leonos_net_dns_policy:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - include/leonos/net.h
  - kernel/ntclks/include/ntclks/net.h
  - kernel/ntclks/net.c
  - kernel/ntclks/syscall.c
  - userland/apps/netctl/main.c
  - userland/libc/src/libc.c
leonos_net_dns_resolve:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - docs/SYSCALLS.md
  - include/leonos/net.h
  - userland/apps/netctl/main.c
  - userland/libc/src/libc.c
leonos_net_get_dns_policy:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - include/leonos/net.h
  - userland/apps/netctl/main.c
  - userland/libc/src/libc.c
leonos_net_http_get:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - docs/ABI.md
  - docs/SYSCALLS.md
  - include/leonos/net.h
  - kernel/ntclks/include/ntclks/net.h
  - kernel/ntclks/net.c
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_net_ping:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - docs/SYSCALLS.md
  - include/leonos/net.h
  - kernel/ntclks/include/ntclks/net.h
  - kernel/ntclks/net.c
  - kernel/ntclks/syscall.c
  - userland/apps/ping/main.c
  - userland/libc/src/libc.c
leonos_net_set_dns_policy:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - include/leonos/net.h
  - userland/apps/netctl/main.c
  - userland/libc/src/libc.c
leonos_net_socket_close:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - include/leonos/net.h
  - kernel/ntclks/include/ntclks/net.h
  - kernel/ntclks/net.c
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_net_socket_connect:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - include/leonos/net.h
  - kernel/ntclks/include/ntclks/net.h
  - kernel/ntclks/net.c
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_net_socket_io:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - include/leonos/net.h
  - kernel/ntclks/include/ntclks/net.h
  - kernel/ntclks/net.c
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_net_socket_open:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - include/leonos/net.h
  - kernel/ntclks/include/ntclks/net.h
  - kernel/ntclks/net.c
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_pty_create:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - userland/libc/src/libc.c
leonos_pty_destroy:
  - include/leonos/pty.h
  - userland/libc/src/libc.c
leonos_pty_error:
  - userland/libc/src/libc.c
leonos_pty_get_termios:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - userland/libc/src/libc.c
leonos_pty_get_winsize:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - userland/libc/src/libc.c
leonos_pty_input_available:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - userland/libc/src/libc.c
leonos_pty_io:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_pty_read_output:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - userland/libc/src/libc.c
leonos_pty_self:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - userland/libc/src/libc.c
leonos_pty_set_termios:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - userland/libc/src/libc.c
leonos_pty_set_winsize:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - userland/libc/src/libc.c
leonos_pty_spawn:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_pty_spawn_argv:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/docs/PROGRAMS.md
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - userland/libc/src/libc.c
leonos_pty_spawn_argv_with_fds:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - userland/libc/src/libc.c
leonos_pty_termios:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - kernel/ntclks/include/ntclks/pty.h
  - kernel/ntclks/pty.c
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_pty_termios_io:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_pty_termios_request:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_pty_winsize:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - kernel/ntclks/include/ntclks/pty.h
  - kernel/ntclks/pty.c
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_pty_winsize_io:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - kernel/ntclks/syscall.c
  - userland/libc/src/libc.c
leonos_pty_write_input:
  - devtools/components/tcc/runtime/include/leonos/pty.h
  - devtools/include/leonos/pty.h
  - include/leonos/pty.h
  - userland/libc/src/libc.c
leonos_socket_close:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - docs/ABI.md
  - docs/SYSCALLS.md
  - include/leonos/net.h
  - userland/libc/src/libc.c
leonos_socket_connect:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - docs/ABI.md
  - docs/SYSCALLS.md
  - include/leonos/net.h
  - userland/libc/src/libc.c
leonos_socket_recv:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - docs/ABI.md
  - docs/SYSCALLS.md
  - include/leonos/net.h
  - userland/libc/src/libc.c
  - userland/libc/src/tls.c
leonos_socket_send:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - docs/ABI.md
  - docs/SYSCALLS.md
  - include/leonos/net.h
  - userland/libc/src/libc.c
  - userland/libc/src/tls.c
leonos_socket_tcp:
  - devtools/components/tcc/runtime/include/leonos/net.h
  - devtools/include/leonos/net.h
  - docs/ABI.md
  - docs/SYSCALLS.md
  - include/leonos/net.h
  - userland/libc/src/libc.c
