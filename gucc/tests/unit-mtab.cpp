#include "gucc/mtab.hpp"

#include <cassert>

#include <string_view>

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

int main() {
    // running system
    {
        const auto& mtab_entries = gucc::mtab::parse_mtab_content(MTAB_RUNNING_SYSTEM_TEST, "/mnt"sv);
        assert(mtab_entries.size() == 2);
        assert(mtab_entries[0].device == "/dev/nvme0n1p3");
        assert(mtab_entries[0].mountpoint == "/mnt");
        assert(mtab_entries[1].device == "/dev/nvme0n1p1");
        assert(mtab_entries[1].mountpoint == "/mnt/boot");
    }
}
