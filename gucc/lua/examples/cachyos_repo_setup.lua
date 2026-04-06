#!/usr/bin/env lua
-- CachyOS Repository Setup Script
-- Replaces: detect-architecture, try-v3, shellprocess_initialize_pacman.conf
--
-- Usage:
--   lua cachyos_repo_setup.lua [--online|--offline]
--
-- --online:  Detect arch on live system, generate pacman-target.conf, copy to target
-- --offline: Detect arch inside chroot, modify /etc/pacman.conf directly

local gucc = require "gucc"

local mode = arg[1] or "--online"

local function detect_arch()
    local isa_levels = gucc.cpu.get_isa_levels()
    local vendor = gucc.cpu.get_vendor()
    print("CPU vendor: " .. vendor)

    for _, level in ipairs(isa_levels) do
        if level == "x86-64-v4" then
            print("Architecture: x86-64-v4")
            return "v4"
        end
        if level == "x86-64-v3" then
            print("Architecture: x86-64-v3")
            return "v3"
        end
    end

    -- Check for znver4/znver5 (AMD Zen 4/5 optimized)
    for _, level in ipairs(isa_levels) do
        if level:find("znver") then
            print("Architecture: znver4")
            return "znver4"
        end
    end

    print("Architecture: x86_64 (legacy)")
    return "legacy"
end

local function get_repo_config(arch)
    local repos_config = [[
[cachyos]
Include = /etc/pacman.d/cachyos-mirrorlist
]]
    if arch == "v3" then
        repos_config = [[
[cachyos-v3]
Include = /etc/pacman.d/cachyos-v3-mirrorlist
[cachyos]
Include = /etc/pacman.d/cachyos-mirrorlist
]]
    elseif arch == "v4" then
        repos_config = [[
[cachyos-v4]
Include = /etc/pacman.d/cachyos-v4-mirrorlist
[cachyos]
Include = /etc/pacman.d/cachyos-mirrorlist
]]
    elseif arch == "znver4" then
        repos_config = [[
[cachyos-v3]
Include = /etc/pacman.d/cachyos-v3-mirrorlist
[cachyos]
Include = /etc/pacman.d/cachyos-mirrorlist
]]
    end
    return repos_config
end

local root_mountpoint = os.getenv("ROOT") or "/mnt"

if mode == "--online" then
    print("=== Online mode: detect arch, configure repos for target ===")
    local arch = detect_arch()

    -- Add repos to live system pacman.conf first
    gucc.pacmanconf.push_repos_front("/etc/pacman.conf", get_repo_config(arch))

    -- Initialize pacman keyring on host
    gucc.repos.install_cachyos_repos()

    -- Copy pacman.conf to target
    local pacman_conf = gucc.file_utils.read_whole_file("/etc/pacman.conf")
    if pacman_conf then
        gucc.file_utils.write_to_file(pacman_conf, root_mountpoint .. "/etc/pacman.conf")
    end

    print("Repos configured and copied to " .. root_mountpoint)
elseif mode == "--offline" then
    print("=== Offline mode: configure repos in chroot ===")
    local arch = detect_arch()
    gucc.pacmanconf.push_repos_front("/etc/pacman.conf", get_repo_config(arch))
    print("Repos configured in chroot")
else
    print("Usage: lua cachyos_repo_setup.lua [--online|--offline]")
    os.exit(1)
end
