#include "doctest_compatibility.h"

#include "gucc/file_utils.hpp"
#include "gucc/mtab.hpp"

#include <filesystem>
#include <string_view>

#include <fmt/core.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

static constexpr auto MTAB_RUNNING_SYSTEM_TEST = R"(
proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0

sys /sys sysfs rw,nosuid,nodev,noexec,relatime 0 0
dev /dev devtmpfs rw,nosuid,relatime,size=8104116k,nr_inodes=2026029,mode=755,inode64 0 0
run /run tmpfs rw,nosuid,nodev,relatime,mode=755,inode64 0 0
efivarfs /sys/firmware/efi/efivars efivarfs rw,nosuid,nodev,noexec,relatime 0 0
# test string
/dev/nvme0n1p3 /mnt btrfs rw,relatime,compress=zstd:3,ssd,discard=async,space_cache,subvolid=5,subvol=/ 0 0
securityfs /sys/kernel/security securityfs rw,nosuid,nodev,noexec,relatime 0 0
tmpfs /dev/shm tmpfs rw,nosuid,nodev,inode64 0 0
devpts /dev/pts devpts rw,nosuid,noexec,relatime,gid=5,mode=620,ptmxmode=000 0 0
cgroup2 /sys/fs/cgroup cgroup2 rw,nosuid,nodev,noexec,relatime,nsdelegate,memory_recursiveprot 0 0
 # test something
pstore /sys/fs/pstore pstore rw,nosuid,nodev,noexec,relatime 0 0
bpf /sys/fs/bpf bpf rw,nosuid,nodev,noexec,relatime,mode=700 0 0
systemd-1 /proc/sys/fs/binfmt_misc autofs rw,relatime,fd=39,pgrp=1,timeout=0,minproto=5,maxproto=5,direct,pipe_ino=4748 0 0
debugfs /sys/kernel/debug debugfs rw,nosuid,nodev,noexec,relatime 0 0
mqueue /dev/mqueue mqueue rw,nosuid,nodev,noexec,relatime 0 0
hugetlbfs /dev/hugepages hugetlbfs rw,nosuid,nodev,relatime,pagesize=2M 0 0
tracefs /sys/kernel/tracing tracefs rw,nosuid,nodev,noexec,relatime 0 0
tmpfs /run/credentials/systemd-journald.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /run/credentials/systemd-udev-load-credentials.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
fusectl /sys/fs/fuse/connections fusectl rw,nosuid,nodev,noexec,relatime 0 0
tmpfs /run/credentials/systemd-tmpfiles-setup-dev-early.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
configfs /sys/kernel/config configfs rw,nosuid,nodev,noexec,relatime 0 0
tmpfs /run/credentials/systemd-sysctl.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /run/credentials/systemd-tmpfiles-setup-dev.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /tmp tmpfs rw,nosuid,nodev,nr_inodes=1048576,inode64 0 0
/dev/nvme0n1p1 /mnt/boot vfat rw,relatime,fmask=0022,dmask=0022,codepage=437,iocharset=ascii,shortname=mixed,utf8,errors=remount-ro 0 0
tmpfs /run/credentials/systemd-tmpfiles-setup.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
binfmt_misc /proc/sys/fs/binfmt_misc binfmt_misc rw,nosuid,nodev,noexec,relatime 0 0
tmpfs /run/credentials/systemd-vconsole-setup.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /run/credentials/getty@tty1.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /run/user/1000 tmpfs rw,nosuid,nodev,relatime,size=1632224k,nr_inodes=408056,mode=700,uid=1000,gid=984,inode64 0 0
portal /run/user/1000/doc fuse.portal rw,nosuid,nodev,relatime,user_id=1000,group_id=984 0 0
run /run/firejail/dbus tmpfs rw,nosuid,nodev,noexec,relatime,mode=755,inode64 0 0
run /run/firejail/firejail.ro.dir tmpfs ro,nosuid,nodev,relatime,mode=755,inode64 0 0
run /run/firejail/firejail.ro.file tmpfs ro,nosuid,nodev,relatime,mode=755,inode64 0 0
)"sv;

static constexpr auto MTAB_LIVE_ISO_TEST = R"(
proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0
sys /sys sysfs rw,nosuid,nodev,noexec,relatime 0 0
dev /dev devtmpfs rw,nosuid,relatime,size=3722300k,nr_inodes=930575,mode=755,inode64 0 0
run /run tmpfs rw,nosuid,nodev,relatime,mode=755,inode64 0 0
efivarfs /sys/firmware/efi/efivars efivarfs rw,nosuid,nodev,noexec,relatime 0 0
/dev/sr0 /run/archiso/bootmnt iso9660 ro,relatime,nojoliet,check=s,map=n,blocksize=2048,iocharset=utf8 0 0
cowspace /run/archiso/cowspace tmpfs rw,relatime,size=10485760k,mode=755,inode64 0 0
/dev/loop0 /run/archiso/airootfs squashfs ro,relatime,errors=continue,threads=single 0 0
airootfs / overlay rw,relatime,lowerdir=/run/archiso/airootfs,upperdir=/run/archiso/cowspace/persistent_/x86_64/upperdir,workdir=/run/archiso/cowspace/persistent_/x86_64/workdir,uuid=on 0 0
securityfs /sys/kernel/security securityfs rw,nosuid,nodev,noexec,relatime 0 0
tmpfs /dev/shm tmpfs rw,nosuid,nodev,inode64 0 0
devpts /dev/pts devpts rw,nosuid,noexec,relatime,gid=5,mode=620,ptmxmode=000 0 0
cgroup2 /sys/fs/cgroup cgroup2 rw,nosuid,nodev,noexec,relatime,nsdelegate,memory_recursiveprot 0 0
pstore /sys/fs/pstore pstore rw,nosuid,nodev,noexec,relatime 0 0
bpf /sys/fs/bpf bpf rw,nosuid,nodev,noexec,relatime,mode=700 0 0
systemd-1 /proc/sys/fs/binfmt_misc autofs rw,relatime,fd=39,pgrp=1,timeout=0,minproto=5,maxproto=5,direct,pipe_ino=5202 0 0
tracefs /sys/kernel/tracing tracefs rw,nosuid,nodev,noexec,relatime 0 0
mqueue /dev/mqueue mqueue rw,nosuid,nodev,noexec,relatime 0 0
hugetlbfs /dev/hugepages hugetlbfs rw,nosuid,nodev,relatime,pagesize=2M 0 0
debugfs /sys/kernel/debug debugfs rw,nosuid,nodev,noexec,relatime 0 0
tmpfs /run/credentials/systemd-journald.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /run/credentials/systemd-network-generator.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /run/credentials/systemd-udev-load-credentials.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
fusectl /sys/fs/fuse/connections fusectl rw,nosuid,nodev,noexec,relatime 0 0
configfs /sys/kernel/config configfs rw,nosuid,nodev,noexec,relatime 0 0
tmpfs /run/credentials/systemd-tmpfiles-setup-dev-early.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /run/credentials/systemd-sysctl.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /run/credentials/systemd-sysusers.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /run/credentials/systemd-tmpfiles-setup-dev.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /run/credentials/systemd-networkd.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /run/credentials/systemd-vconsole-setup.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /etc/pacman.d/gnupg tmpfs rw,relatime,mode=755,inode64,noswap 0 0
tmpfs /tmp tmpfs rw,nosuid,nodev,size=3799484k,nr_inodes=1048576,inode64 0 0
tmpfs /run/credentials/systemd-tmpfiles-setup.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /run/credentials/systemd-firstboot.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /run/credentials/systemd-resolved.service tmpfs ro,nosuid,nodev,noexec,relatime,nosymfollow,size=1024k,nr_inodes=1024,mode=700,inode64,noswap 0 0
tmpfs /run/user/1000 tmpfs rw,nosuid,nodev,relatime,size=759896k,nr_inodes=189974,mode=700,uid=1000,gid=1000,inode64 0 0
portal /run/user/1000/doc fuse.portal rw,nosuid,nodev,relatime,user_id=0,group_id=0 0 0
/dev/sda2 /tmp/calamares-root-q_z5rdlx btrfs rw,noatime,compress=zstd:3,space_cache=v2,commit=120,subvolid=256,subvol=/@ 0 0
/dev/sda2 /tmp/calamares-root-q_z5rdlx/home btrfs rw,noatime,compress=zstd:3,space_cache=v2,commit=120,subvolid=257,subvol=/@home 0 0
/dev/sda2 /tmp/calamares-root-q_z5rdlx/root btrfs rw,noatime,compress=zstd:3,space_cache=v2,commit=120,subvolid=258,subvol=/@root 0 0
/dev/sda2 /tmp/calamares-root-q_z5rdlx/srv btrfs rw,noatime,compress=zstd:3,space_cache=v2,commit=120,subvolid=259,subvol=/@srv 0 0
/dev/sda2 /tmp/calamares-root-q_z5rdlx/var/cache btrfs rw,noatime,compress=zstd:3,space_cache=v2,commit=120,subvolid=260,subvol=/@cache 0 0
/dev/sda2 /tmp/calamares-root-q_z5rdlx/var/tmp btrfs rw,noatime,compress=zstd:3,space_cache=v2,commit=120,subvolid=261,subvol=/@tmp 0 0
/dev/sda2 /tmp/calamares-root-q_z5rdlx/var/log btrfs rw,noatime,compress=zstd:3,space_cache=v2,commit=120,subvolid=262,subvol=/@log 0 0
/dev/sda1 /tmp/calamares-root-q_z5rdlx/boot vfat rw,relatime,fmask=0022,dmask=0022,codepage=437,iocharset=ascii,shortname=mixed,utf8,errors=remount-ro 0 0
dev /tmp/calamares-root-q_z5rdlx/dev devtmpfs rw,nosuid,relatime,size=3722300k,nr_inodes=930575,mode=755,inode64 0 0
proc /tmp/calamares-root-q_z5rdlx/proc proc rw,noatime 0 0
tmpfs /tmp/calamares-root-q_z5rdlx/run tmpfs rw,noatime,inode64 0 0
run /tmp/calamares-root-q_z5rdlx/run/udev tmpfs rw,nosuid,nodev,relatime,mode=755,inode64 0 0
sys /tmp/calamares-root-q_z5rdlx/sys sysfs rw,noatime 0 0
efivarfs /tmp/calamares-root-q_z5rdlx/sys/firmware/efi/efivars efivarfs rw,noatime 0 0
)"sv;

TEST_CASE("mtab test")
{
    SECTION("running system")
    {
        const auto& mtab_entries = gucc::mtab::parse_mtab_content(MTAB_RUNNING_SYSTEM_TEST, "/mnt"sv);
        REQUIRE_EQ(mtab_entries.size(), 2);
        REQUIRE_EQ(mtab_entries[0].device, "/dev/nvme0n1p3");
        REQUIRE_EQ(mtab_entries[0].mountpoint, "/mnt");
        REQUIRE_EQ(mtab_entries[1].device, "/dev/nvme0n1p1");
        REQUIRE_EQ(mtab_entries[1].mountpoint, "/mnt/boot");
    }
    SECTION("live iso system")
    {
        static constexpr std::string_view filename{"/tmp/mtab.conf"};
        REQUIRE(file_utils::create_file_for_overwrite(filename, MTAB_LIVE_ISO_TEST));

        const auto& mtab_entries = gucc::mtab::parse_mtab("/tmp/calamares-root-q_z5rdlx"sv, filename);
        REQUIRE(mtab_entries.has_value());

        // Cleanup.
        fs::remove(filename);

        const auto& entries = *mtab_entries;
        REQUIRE_EQ(entries.size(), 14);
        REQUIRE_EQ(entries[0].device, "/dev/sda2");
        REQUIRE_EQ(entries[0].mountpoint, "/tmp/calamares-root-q_z5rdlx");
        REQUIRE_EQ(entries[1].device, "/dev/sda2");
        REQUIRE_EQ(entries[1].mountpoint, "/tmp/calamares-root-q_z5rdlx/home");
        REQUIRE_EQ(entries[2].device, "/dev/sda2");
        REQUIRE_EQ(entries[2].mountpoint, "/tmp/calamares-root-q_z5rdlx/root");
        REQUIRE_EQ(entries[3].device, "/dev/sda2");
        REQUIRE_EQ(entries[3].mountpoint, "/tmp/calamares-root-q_z5rdlx/srv");
        REQUIRE_EQ(entries[4].device, "/dev/sda2");
        REQUIRE_EQ(entries[4].mountpoint, "/tmp/calamares-root-q_z5rdlx/var/cache");
        REQUIRE_EQ(entries[5].device, "/dev/sda2");
        REQUIRE_EQ(entries[5].mountpoint, "/tmp/calamares-root-q_z5rdlx/var/tmp");
        REQUIRE_EQ(entries[6].device, "/dev/sda2");
        REQUIRE_EQ(entries[6].mountpoint, "/tmp/calamares-root-q_z5rdlx/var/log");
        REQUIRE_EQ(entries[7].device, "/dev/sda1");
        REQUIRE_EQ(entries[7].mountpoint, "/tmp/calamares-root-q_z5rdlx/boot");

        REQUIRE_EQ(entries[8].device, "dev");
        REQUIRE_EQ(entries[8].mountpoint, "/tmp/calamares-root-q_z5rdlx/dev");
        REQUIRE_EQ(entries[9].device, "proc");
        REQUIRE_EQ(entries[9].mountpoint, "/tmp/calamares-root-q_z5rdlx/proc");
        REQUIRE_EQ(entries[10].device, "tmpfs");
        REQUIRE_EQ(entries[10].mountpoint, "/tmp/calamares-root-q_z5rdlx/run");
        REQUIRE_EQ(entries[11].device, "run");
        REQUIRE_EQ(entries[11].mountpoint, "/tmp/calamares-root-q_z5rdlx/run/udev");
        REQUIRE_EQ(entries[12].device, "sys");
        REQUIRE_EQ(entries[12].mountpoint, "/tmp/calamares-root-q_z5rdlx/sys");
        REQUIRE_EQ(entries[13].device, "efivarfs");
        REQUIRE_EQ(entries[13].mountpoint, "/tmp/calamares-root-q_z5rdlx/sys/firmware/efi/efivars");
    }
    SECTION("empty file")
    {
        static constexpr std::string_view filename{"/tmp/mtab.conf"};
        REQUIRE(file_utils::create_file_for_overwrite(filename, ""));

        const auto& mtab_entries = gucc::mtab::parse_mtab("/tmp/calamares-root-q_z5rdlx"sv, filename);
        REQUIRE(!mtab_entries.has_value());

        // Cleanup.
        fs::remove(filename);
    }
}
