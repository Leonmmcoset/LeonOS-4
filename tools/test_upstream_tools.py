#!/usr/bin/env python3
"""Behavioral regression tests for upstream tool recipes and staging."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import build_busybox as busybox
from build import build_graph
from buildsystem.core.state import BuildPaths
from make_live_root import make_live_tree
from make_installer_root import stage_installed_payloads, stage_runtime_payload


class UpstreamToolTests(unittest.TestCase):
    def test_cmd_dispatches_fdisk_to_util_linux_not_busybox(self):
        with tempfile.TemporaryDirectory(prefix="cmd-fdisk-") as directory:
            executable = Path(directory) / "dispatch"
            subprocess.run(["cc", "-D_GNU_SOURCE", "-D_UTSNAME_LENGTH=65", "-O1",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-Dchmod=cmd_test_chmod", "-Dlink=cmd_test_link",
                "-Dsymlink=cmd_test_symlink", "-Duname=cmd_test_uname",
                "-Ithird_party/cmd", "-idirafter", "userland/libc/include",
                "-idirafter", "include", "-Iinclude/uapi",
                "tools/tests/cmd_fdisk_dispatch_test.c", "userland/cmd/leonos_cmd_shim.c",
                "-o", str(executable)], cwd=ROOT, check=True)
            subprocess.run([str(executable)], check=True, timeout=10)

    def test_official_fragment_remains_a_mapping_consumable_by_kconfig(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / ".config"
            config.write_text('CONFIG_BUSYBOX=n\nCONFIG_EXTRA_LDLIBS=""\n')
            fragment = {"CONFIG_BUSYBOX": "y", "CONFIG_LEONOS_FDISK": "y",
                        "CONFIG_EXTRA_LDLIBS": '"leonos"', "CONFIG_FDISK": "n"}
            busybox.apply_fragment(config, busybox.filter_official_fragment(fragment))
            self.assertEqual(busybox.read_fragment(config), {
                "CONFIG_BUSYBOX": "y", "CONFIG_EXTRA_LDLIBS": '""', "CONFIG_FDISK": "n"})

    def test_busybox_source_cache_separates_source_profiles(self):
        self.assertNotEqual(busybox.source_cache_key("revision", official_source=False),
                            busybox.source_cache_key("revision", official_source=True))

    def test_official_busybox_keeps_musl_library_search_path(self):
        self.assertEqual(busybox.library_search_flags(Path("/tmp/sdk"), official_source=True),
                         ["-L/tmp/sdk/musl/lib"])

    def test_official_source_exports_commit_not_dirty_worktree_and_rejects_cache_injection(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory) / "repo"
            repo.mkdir()
            def git(*args):
                return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()
            git("init", "-q")
            (repo / "Makefile").write_text("upstream\n")
            git("add", "Makefile")
            git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                "commit", "-qm", "source fixture")
            revision = git("rev-parse", "HEAD")
            environment = busybox.official_build_environment(repo, revision)
            observed = subprocess.check_output(
                [sys.executable, "-c", "import os; print(os.environ['SOURCE_DATE_EPOCH'], os.environ['TZ'])"],
                env=environment, text=True).strip()
            self.assertEqual(observed, git("show", "-s", "--format=%ct", revision) + " UTC")
            (repo / "Makefile").write_text("injected\n")
            (repo / "private.c").write_text("injected\n")
            cache = Path(directory) / "cache"
            # A dirty checkout must never become the official build input.
            exported = busybox.official_source(repo, revision, cache)
            self.assertEqual((exported / "Makefile").read_text(), "upstream\n")
            self.assertFalse((exported / "private.c").exists())
            (exported / "private.c").write_text("injected\n")
            with self.assertRaisesRegex(ValueError, "Unexpected file"):
                busybox.official_source(repo, revision, cache)


class BuildGraphTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="upstream-graph-", dir=ROOT / "build")
        self.addCleanup(self.tmp.cleanup)
        self.paths = BuildPaths(Path(self.tmp.name))
        self.paths.out.mkdir()
        config = self.paths.root / "config"
        config.write_text("")
        self.graph = build_graph(self.paths, config)

    def test_storage_packages_have_image_owners(self):
        self.assertTrue("storage-upstream" in self.graph.targets, "missing storage build target")
        self.assertTrue("esp:storage" in self.graph.targets, "missing storage staging target")
        for name in ("mkfs.ext2", "fsck.ext2", "mkfs.fat", "fsck.fat", "mkfs.exfat", "fsck.exfat"):
            output = self.paths.out / "storage-upstream/root/usr/sbin" / name
            self.assertIn(output, self.graph.targets["storage-upstream"].outputs)
            staged = self.paths.staging / "usr/sbin" / name
            self.assertIn(staged, self.graph.targets["esp:storage"].outputs)
            self.assertIn(staged, self.graph.targets["installer-root"].inputs)
            self.assertIn(staged, self.graph.targets["desktop-live-root"].inputs)
        for name in ("bin/mount", "bin/umount", "bin/lsblk", "usr/sbin/blkid", "usr/sbin/fsck"):
            self.assertIn(self.paths.out / "auth-upstream/root" / name,
                          self.graph.targets["auth-upstream"].outputs)

    def test_busybox_recipe_does_not_depend_on_private_libc(self):
        target = self.graph.targets["busybox"]
        self.assertIn("--official-source", target.command)
        self.assertNotIn("--official-source", self.graph.targets["file"].command)
        self.assertNotIn("archive:libc", target.depends_on)
        for option in ("--leonos-lib", "--leonos-include", "--leonos-libc-include"):
            self.assertNotIn(option, target.command)
        self.assertFalse(any(p.name in {"libleonos.a", "leonos_shim.c", "block_storage.c"}
                             for p in target.all_inputs()))

    def test_fdisk_and_target_libraries_have_build_graph_owners(self):
        auth = self.graph.targets["auth-upstream"]
        for name in ("usr/sbin/fdisk", "usr/lib/libfdisk.so.1",
                     "usr/lib/libsmartcols.so.1", "usr/lib/libuuid.so.1"):
            self.assertIn(self.paths.out / "auth-upstream/root" / name, auth.outputs)
        self.assertIn(self.paths.staging / "usr/sbin/fdisk",
                      self.graph.targets["installer-root"].inputs)

    def test_staging_rejects_a_stale_fdisk_when_upstream_output_is_missing(self):
        auth = self.paths.out / "auth-upstream/root"
        for directory in ("bin", "sbin", "lib", "usr", "etc"):
            (auth / directory).mkdir(parents=True)
        for name in ("usr/bin/sudo", "usr/bin/passwd", "bin/su", "sbin/unix_chkpwd"):
            p = auth / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b"fixture")
        stale = self.paths.staging / "usr/sbin/fdisk"
        stale.parent.mkdir(parents=True)
        stale.write_bytes(b"stale must not satisfy upstream contract")
        context = SimpleNamespace(copy=shutil.copy2, detail=lambda _: None,
                                  write_text=lambda p, text: p.write_text(text))
        with self.assertRaisesRegex(Exception, "fdisk"):
            self.graph.targets["esp:auth"].action(context)

    def test_staging_rejects_missing_util_linux_library_before_copying(self):
        from tools.storage_tools import UTIL_LINUX_COMMANDS, UTIL_LINUX_LIBRARIES
        auth = self.paths.out / "auth-upstream/root"
        for directory in ("bin", "sbin", "lib", "usr", "etc"):
            (auth / directory).mkdir(parents=True, exist_ok=True)
        for name in (*UTIL_LINUX_COMMANDS, *UTIL_LINUX_LIBRARIES,
                     "usr/bin/sudo", "usr/bin/passwd", "bin/su", "sbin/unix_chkpwd"):
            path = auth / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"new upstream fixture")
        (auth / "usr/lib/libmount.so.1").unlink()
        stale = self.paths.staging / "usr/lib/libmount.so.1"
        stale.parent.mkdir(parents=True)
        stale.write_bytes(b"old staging library")
        context = SimpleNamespace(copy=shutil.copy2, detail=lambda _: None,
                                  write_text=lambda p, text: p.write_text(text))
        with self.assertRaisesRegex(Exception, "libmount"):
            self.graph.targets["esp:auth"].action(context)
        self.assertEqual(stale.read_bytes(), b"old staging library")
        self.assertFalse((self.paths.staging / "usr/sbin/fdisk").exists())

    def test_layout_replaces_legacy_fdisk_and_runuser_with_upstream_aliases(self):
        stage = self.paths.staging
        (stage / "sbin").mkdir(parents=True)
        (stage / "usr/sbin").mkdir(parents=True)
        (stage / "sbin/fdisk").symlink_to("../bin/busybox")
        (stage / "sbin/runuser").write_bytes(b"old runuser")
        for name in ("fdisk", "runuser"):
            (stage / "usr/sbin" / name).write_bytes(b"upstream")
        links = self.paths.out / "userland/busybox.links"
        links.parent.mkdir()
        links.write_text("/bin/sh\n/bin/cat\n")
        context = SimpleNamespace(detail=lambda _: None,
                                  write_text=lambda p, text: p.write_text(text))
        self.graph.targets["esp:layout-links"].action(context)
        for name in ("fdisk", "runuser"):
            self.assertEqual((stage / "sbin" / name).readlink(), Path("../usr/sbin") / name)
            self.assertEqual((stage / "sbin" / name).read_bytes(), b"upstream")

    def test_live_installer_and_installed_roots_preserve_fdisk_and_libraries(self):
        stage = self.paths.staging
        artifacts = {
            "usr/sbin/fdisk": b"upstream fdisk fixture", "usr/lib/libfdisk.so.1": b"fdisk library",
            "usr/lib/libsmartcols.so.1": b"smartcols library", "usr/lib/libuuid.so.1": b"uuid library",
            "bin/busybox": b"upstream busybox fixture", "lib/ld-musl-x86_64.so.1": b"musl",
            "usr/lib/leonos/apps/dynlinkerror/dynlinkerror.elf": b"error program",
            "EFI/BOOT/BOOTX64.EFI": b"efi", "loader.elf": b"loader",
            "grub/grub.cfg": b"boot", "leonos/kernel.sys": b"kernel",
        }
        for name, data in artifacts.items():
            p = stage / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        shutil.copytree(ROOT / "system/rootfs", stage, dirs_exist_ok=True)
        (stage / "bin/sh").symlink_to("busybox")
        live = self.paths.out / "live"
        make_live_tree(stage, live)
        destination = self.paths.out / "installer"
        stage_installed_payloads(stage, destination)
        userland = self.paths.out / "userland"
        userland.mkdir()
        for app in ("imd", "windowd", "desktop", "installer", "busybox", "gptinit", "runtime"):
            (userland / f"{app}.elf").write_bytes(b"fixture")
        stage_runtime_payload(stage, destination, userland / "runtime.elf", userland,
                              userland / "gptinit.elf", userland, ())
        for root in (live, destination, destination / "install/root"):
            for name in ("usr/sbin/fdisk", "usr/lib/libfdisk.so.1", "usr/lib/libsmartcols.so.1",
                         "usr/lib/libuuid.so.1"):
                self.assertEqual((root / name).read_bytes(), artifacts[name])
            self.assertFalse((root / "usr/sbin/fdisk").is_symlink())


if __name__ == "__main__":
    unittest.main()
