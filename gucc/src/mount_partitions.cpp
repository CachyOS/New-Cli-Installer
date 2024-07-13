#include "gucc/mount_partitions.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_view_literals;

namespace gucc::mount {

auto mount_partition(std::string_view partition, std::string_view mount_dir, std::string_view mount_opts) noexcept -> bool {
    if (!mount_opts.empty()) {
        return utils::exec_checked(fmt::format(FMT_COMPILE("mount -o {} {} {}"), mount_opts, partition, mount_dir));
    }
    return utils::exec_checked(fmt::format(FMT_COMPILE("mount {} {}"), partition, mount_dir));
}

auto query_partition(std::string_view partition, std::int32_t& is_luks, std::int32_t& is_lvm, std::string& luks_name, std::string& luks_dev, std::string& luks_uuid) noexcept -> bool {
    // Identify if mounted partition is type "crypt" (LUKS on LVM, or LUKS alone)
    if (utils::exec_checked(fmt::format(FMT_COMPILE("lsblk -lno TYPE {} | grep -q 'crypt'"), partition))) {
        // cryptname for bootloader configuration either way
        is_luks   = true;
        luks_name = utils::exec(fmt::format(FMT_COMPILE("echo {} | sed \"s~^/dev/mapper/~~g\""), partition));

        const auto& check_cryptparts = [&](auto&& cryptparts, const auto& functor) {
            for (const auto& cryptpart : cryptparts) {
                if (utils::exec_checked(fmt::format(FMT_COMPILE("lsblk -lno NAME {} | grep -q '{}'"), cryptpart, luks_name))) {
                    functor(cryptpart);
                    return true;
                }
            }
            return false;
        };

        // Check if LUKS on LVM (parent = lvm /dev/mapper/...)
        auto check_functor = [&](auto&& cryptpart) {
            luks_dev = fmt::format(FMT_COMPILE("{} cryptdevice={}:{}"), luks_dev, cryptpart, luks_name);
            is_lvm   = true;
        };
        auto cryptparts = utils::make_multiline(utils::exec(R"(lsblk -lno NAME,FSTYPE,TYPE | grep 'lvm' | grep -i 'crypto_luks' | uniq | awk '{print "/dev/mapper/"$1}')"));
        if (check_cryptparts(cryptparts, check_functor)) {
            return true;
        }

        // Check if LVM on LUKS
        cryptparts = utils::make_multiline(utils::exec(R"(lsblk -lno NAME,FSTYPE,TYPE | grep ' crypt$' | grep -i 'LVM2_member' | uniq | awk '{print "/dev/mapper/"$1}')"));
        if (check_cryptparts(cryptparts, check_functor)) {
            return true;
        }

        // Check if LUKS alone (parent = part /dev/...)
        const auto& check_func_dev = [&](auto&& cryptpart) {
            luks_uuid = utils::exec(fmt::format(FMT_COMPILE("lsblk -lno UUID,TYPE,FSTYPE {} | grep 'part' | grep -i 'crypto_luks' | {}"), cryptpart, "awk '{print $1}'"));
            luks_dev  = fmt::format(FMT_COMPILE("{} cryptdevice=UUID={}:{}"), luks_dev, luks_uuid, luks_name);
        };
        cryptparts = utils::make_multiline(utils::exec(R"(lsblk -lno NAME,FSTYPE,TYPE | grep 'part' | grep -i 'crypto_luks' | uniq | awk '{print "/dev/"$1}')"));
        if (check_cryptparts(cryptparts, check_func_dev)) {
            return true;
        }
    }
    /*

        // If LVM logical volume....
    elif [[ $(lsblk -lno TYPE ${PARTITION} | grep "lvm") != "" ]]; then
        LVM=1

        // First get crypt name (code above would get lv name)
        cryptparts=$(lsblk -lno NAME,TYPE,FSTYPE | grep "crypt" | grep -i "lvm2_member" | uniq | awk '{print "/dev/mapper/"$1}')
        for i in ${cryptparts}; do
            if [[ $(lsblk -lno NAME ${i} | grep $(echo $PARTITION | sed "s~^/dev/mapper/~~g")) != "" ]]; then
                LUKS_NAME=$(echo ${i} | sed s~/dev/mapper/~~g)
                return 0;
            fi
        done

        // Now get the device (/dev/...) for the crypt name
        cryptparts=$(lsblk -lno NAME,FSTYPE,TYPE | grep "part" | grep -i "crypto_luks" | uniq | awk '{print "/dev/"$1}')
        for i in ${cryptparts}; do
            if [[ $(lsblk -lno NAME ${i} | grep $LUKS_NAME) != "" ]]; then
                # Create UUID for comparison
                LUKS_UUID=$(lsblk -lno UUID,TYPE,FSTYPE ${i} | grep "part" | grep -i "crypto_luks" | awk '{print $1}')

                # Check if not already added as a LUKS DEVICE (i.e. multiple LVs on one crypt). If not, add.
                if [[ $(echo $LUKS_DEV | grep $LUKS_UUID) == "" ]]; then
                    LUKS_DEV="$LUKS_DEV cryptdevice=UUID=$LUKS_UUID:$LUKS_NAME"
                    LUKS=1
                fi

                return 0;
            fi
    fi*/
    return true;
}

}  // namespace gucc::mount
