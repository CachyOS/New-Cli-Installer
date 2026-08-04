# Example installer configs

Copy, edit the placeholders, then run:

```bash
sudo cachyos-installer --config examples/<file>.json
```

| File | What it installs |
|-|-|
| `desktop-btrfs.json` | KDE desktop on Btrfs, with autologin multimedia and office suite |
| `desktop-zfs.json` | KDE desktop on ZFS, with autologin multimedia and office suite |
| `server-web.json` | Server Edition. Nginx |
| `server-db.json` | Server Edition. local PostgreSQL |
| `server-container-host.json` | Server Edition. Docker |
| `server-cockpit.json` | Server Edition. Cockpit |

See `docs/config.md` for details.
