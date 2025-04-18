# -*- mode: ruby -*-
# vi: set ft=ruby :


# Fail if the vagrant-disksize plugin is not installed
#unless Vagrant.has_plugin?("vagrant-disksize")
#  raise 'vagrant-disksize is not installed!'
#end

# All Vagrant configuration is done below. The "2" in Vagrant.configure
# configures the configuration version (we support older styles for
# backwards compatibility). Please don't change it unless you know what
# you're doing.
disk = './storage.vdi'
Vagrant.configure("2") do |config|
  # The most common configuration options are documented and commented below.
  # For a complete reference, please see the online documentation at
  # https://docs.vagrantup.com.

  # Every Vagrant development environment requires a box. You can search for
  # boxes at https://vagrantcloud.com/search.
  config.vm.box = "archlinux/archlinux"

  #config.disksize.size = '60GB'

  # Disable automatic box update checking. If you disable this, then
  # boxes will only be checked for updates when the user runs
  # `vagrant box outdated`. This is not recommended.
  # config.vm.box_check_update = false

  # Create a forwarded port mapping which allows access to a specific port
  # within the machine from a port on the host machine. In the example below,
  # accessing "localhost:8080" will access port 80 on the guest machine.
  # NOTE: This will enable public access to the opened port
  # config.vm.network "forwarded_port", guest: 80, host: 8080

  # Create a forwarded port mapping which allows access to a specific port
  # within the machine from a port on the host machine and only allow access
  # via 127.0.0.1 to disable public access
  # config.vm.network "forwarded_port", guest: 80, host: 8080, host_ip: "127.0.0.1"

  # Create a private network, which allows host-only access to the machine
  # using a specific IP.
  # config.vm.network "private_network", ip: "192.168.33.10"

  # Create a public network, which generally matched to bridged network.
  # Bridged networks make the machine appear as another physical device on
  # your network.
  # config.vm.network "public_network"

  # Share an additional folder to the guest VM. The first argument is
  # the path on the host to the actual folder. The second argument is
  # the path on the guest to mount the folder. And the optional third
  # argument is a set of non-required options.
  # config.vm.synced_folder "../data", "/vagrant_data"

  # Provider-specific configuration so you can fine-tune various
  # backing providers for Vagrant. These expose provider-specific options.
  # Example for VirtualBox:
  #
  config.vm.provider "virtualbox" do |vb|
    unless File.exist?(disk)
        vb.customize [
            'createhd',
            '--filename', disk,
            '--format', 'VDI',
            '--size', 30 * 1024 # 30 GB
        ]
    end

    # Display the VirtualBox GUI when booting the machine
    vb.gui = false

    # Customize the amount of CPUs on the VM:
    vb.cpus = "4"

    # Customize the amount of memory on the VM:
    vb.memory = "6144"

    vb.customize ['storageattach', :id, '--storagectl', 'IDE Controller', '--port', 1, '--device', 0, '--type', 'hdd', '--medium', disk]
  end
  #
  # View the documentation for the provider you are using for more
  # information on available options.

  # Enable provisioning with a shell script. Additional provisioners such as
  # Ansible, Chef, Docker, Puppet and Salt are also available. Please see the
  # documentation for more information about their specific syntax and use.
  config.vm.provision "shell", inline: <<-SHELL
    # Update system
    pacman -Syu --needed --noconfirm
    # Install depends
    pacman -S --needed --noconfirm base-devel git wget clang cmake meson mold

    # Save our current dir
    SAVED_DIR="$PWD"

    # Set clang is a default compiler
    export AR=llvm-ar
    export CC=clang
    export CXX=clang++
    export NM=llvm-nm
    export RANLIB=llvm-ranlib

    #wget https://raw.githubusercontent.com/CachyOS/CachyOS-CLI-ISO/master/archiso/airootfs/etc/pacman-more.conf
    #mv pacman-more.conf /etc/

    cat << EOF > settings.json
    {
      "menus": 1,
      "headless_mode": true,
      "device": "/dev/sdb",
      "fs_name": "btrfs",
      "hostname": "cachyos",
      "locale": "en_US",
      "xkbmap": "us",
      "timezone": "America/New_York",
      "user_name": "testuser",
      "user_pass": "test",
      "user_shell": "/bin/bash",
      "root_pass": "secure",
      "kernel": "linux-cachyos",
      "desktop": "kde",
      "bootloader": "systemd-boot"
    }
    EOF

    # Remove repo if already exists
    [ -d "new-cli-installer" ] && rm -rf "new-cli-installer"

    # Clone our repo
    git clone https://github.com/cachyos/new-cli-installer.git
    cd new-cli-installer

    #meson --prefix        /usr \
    #      --libexecdir    lib \
    #      --sbindir       bin \
    #      --buildtype     release \
    #      -D              b_pie=true \
    #      -D              devenv=false \
    #      build
    #meson compile -C build --jobs $(nproc)
    cmake -S . -B build \
        -GNinja \
        -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DENABLE_DEVENV=OFF

    cmake --build build --parallel $(nproc)

    cd build
    #meson install
    cmake --install .
    cd "$SAVED_DIR"

  SHELL
end
