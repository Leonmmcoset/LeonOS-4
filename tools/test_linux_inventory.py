#!/usr/bin/env python3
"""Test kernel inventory parsers, then optionally the supplied unmodified Fastfetch in QEMU."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

ROOT=Path(__file__).resolve().parents[1]
WORK=ROOT/'build/linux-inventory'
BINARY_HASH='2ef1384e8eca57e7f163851f1989286b6d2b4638170eed85465cc1ecc604d47f'

def run(command,**kwargs):
    return subprocess.run([str(x) for x in command],cwd=ROOT,check=True,**kwargs)

def host():
    executable=WORK/'inventory-host'
    run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
         '-Iinclude','-Iinclude/uapi','-Ikernel/ntclks/include','tools/tests/linux_inventory_test.c','-o',executable])
    run([executable])
    import sys
    sys.path.insert(0,str(ROOT))
    from buildsystem.core.runner import artifact_mtime
    with tempfile.TemporaryDirectory(prefix='leonos-root-link-') as directory:
        link=Path(directory)/'mtab'; link.symlink_to('../proc/mounts')
        assert artifact_mtime(link)==link.lstat().st_mtime_ns
        target=Path(directory)/'payload'; target.write_text('payload')
        link.unlink(); link.symlink_to('payload')
        later=link.lstat().st_mtime_ns+1000000
        os.utime(target,ns=(later,later))
        assert artifact_mtime(link)==later
    print('PASS build artifacts: runtime-only symlink and target modification')

def guest(args):
    from make_live_root import make_live_tree
    from make_ext2_root import write_ext2_root
    import test_linux_ioctl_cloexec as iso_tools
    binary=args.binary.resolve()
    digest=hashlib.sha256(binary.read_bytes()).hexdigest()
    assert digest==BINARY_HASH, f'The supplied verification binary changed: {digest}'
    compiler=ROOT/'build/musl-gcc/root/opt/dyne/gcc-musl/bin/x86_64-linux-musl-gcc'
    probe=WORK/'linux-inventory.elf'
    run([compiler,'-static','-O2','-Wall','-Wextra','tools/tests/fastfetch_guest_probe.c','-o',probe])
    image=WORK/'root.ext2'
    with tempfile.TemporaryDirectory(prefix='stage-',dir=WORK) as directory:
        stage=Path(directory)
        make_live_tree(ROOT/'build/esp',stage)
        target=stage/'usr/bin/fastfetch-linux'; shutil.copy2(binary,target); target.chmod(0o755)
        assert hashlib.sha256(target.read_bytes()).hexdigest()==digest
        target=stage/'usr/lib/leonos/tests/linux-inventory.elf'; target.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(probe,target)
        write_ext2_root(stage,image)
    iso_tools.GRUB_TEMPLATE=iso_tools.GRUB_TEMPLATE.replace('autospawn=ioctlcloexec autospawn=python315','autospawn=inventory').replace('syscall-trace=/opt/python/','').replace('ioctl CLOEXEC regression','Linux inventory verification')
    iso=iso_tools.build_iso(image,WORK/'leonos4-fastfetch.iso',WORK/'grub.cfg',WORK)
    serial=WORK/'guest-serial.log'; serial.write_text('')
    stderr=WORK/'qemu.log'
    with tempfile.TemporaryDirectory(prefix='leonos-inventory-') as directory, stderr.open('w') as errors:
        qmp=Path(directory)/'qmp.sock'
        process=subprocess.Popen(['qemu-system-x86_64','-enable-kvm','-cpu','host','-machine','q35','-m','4096',
            '-smp','2,sockets=1,cores=2,threads=1','-bios','/usr/share/edk2/x64/OVMF.4m.fd',
            '-smbios','type=1,manufacturer=LeonOS-Test,product=Inventory-Machine,version=1,uuid=00112233-4455-6677-8899-aabbccddeeff',
            '-display','none','-serial',f'file:{serial}','-device','VGA,xres=1280,yres=720','-device','qemu-xhci',
            '-device','usb-tablet','-netdev','user,id=net0','-device','e1000,netdev=net0',
            '-cdrom',str(iso),'-boot','d','-qmp',f'unix:{qmp},server=on,wait=off','-no-reboot','-no-shutdown'],
            cwd=ROOT,stdout=subprocess.DEVNULL,stderr=errors)
        deadline=time.monotonic()+args.timeout
        try:
            while time.monotonic()<deadline:
                text=serial.read_text(errors='replace')
                if '[inventory] DONE' in text or 'KERNEL PANIC' in text or process.poll() is not None: break
                time.sleep(.5)
        finally:
            if process.poll() is None: iso_tools.qmp_quit(qmp,process)
    text=serial.read_text(errors='replace')
    text=re.sub(r'^\[\s*\d+\.\d+\] ?', '', text, flags=re.M)
    assert '[inventory] DONE failures=0' in text, f'Guest regression failed: {serial}'
    assert '00112233-4455-6677-8899-aabbccddeeff' in text, 'SMBIOS UUID endian mismatch'
    # The current kernel deliberately disables AP scheduling. Validate actual
    # admitted CPUs and managed pages, not QEMU's configured hardware capacity.
    cpu_match=re.search(r'SMP topology CPUs=(\d+)',text)
    memory_match=re.search(r'\[osmlayer\] init .*memory=(\d+) KiB',text)
    assert cpu_match and memory_match, 'missing independent kernel inventory'
    enabled_cpus=int(cpu_match[1]); managed_memory=int(memory_match[1])*1024
    modes={tuple(map(int,m)) for m in re.findall(r'framebuffer[^\n]*?(\d+)x(\d+)',text)}
    for label in ('console-json','terminal-json'):
        match=re.search(r'\[inventory\] BEGIN '+label+r'\n(.*?)\n\[inventory\] END '+label,text,re.S)
        assert match, label
        data=json.loads(match[1]); (WORK/f'{label}.json').write_text(json.dumps(data,indent=2)+'\n')
        modules={item['type']:item for item in data}
        for name in ('OS','Host','Kernel','Uptime','CPU','Memory','Swap','Disk','Display','GPU'):
            assert 'result' in modules[name], (name,modules[name])
        assert modules['Host']['result']['name']=='Inventory-Machine', modules['Host']
        assert modules['CPU']['result']['cores']['online']==enabled_cpus, modules['CPU']
        assert modules['Memory']['result']['total']==managed_memory,modules['Memory']
        assert modules['GPU']['result'][0]['vendor']=='QEMU',modules['GPU']
        display=modules['Display']['result'][0]['output']
        assert (display['width'],display['height']) in modes,modules['Display']
        if label=='terminal-json':
            assert modules['Shell']['result']['exeName']=='sh',modules['Shell']
            assert modules['Terminal']['result']['exePath']=='/usr/lib/leonos/apps/terminal/terminal.elf',modules['Terminal']
    (WORK/'evidence.txt').write_text(f'Provided Fastfetch SHA256 {digest}\nHost ASan/UBSan inventory: PASS\nQEMU/KVM 2 configured CPUs, {enabled_cpus} enabled by kernel: PASS\nVMware: not run\nISO {iso}\n')
    print(f'PASS unmodified Fastfetch; evidence: {WORK}')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--guest',action='store_true')
    parser.add_argument('--binary',type=Path,default=Path('/home/xiaobai/下载/fastfetch-musl'))
    parser.add_argument('--timeout',type=float,default=240)
    args=parser.parse_args(); WORK.mkdir(parents=True,exist_ok=True)
    host()
    if args.guest: guest(args)
if __name__=='__main__': main()
